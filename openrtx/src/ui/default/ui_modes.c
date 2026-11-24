/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Main screen of FM (CONFIG_UI_FM_SCREEN), drawn like the TETRA one: the
 * station large, the settings in effect above it, and a meter of labelled
 * bars at the bottom. For colour screens of 128 lines or more.
 */

#include <stdio.h>
#include <string.h>
#include "ui/ui_default.h"

#ifdef CONFIG_UI_FM_SCREEN

#include "core/utils.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "rtx/rtx.h"
#include "interfaces/audio.h"
#include "core/bcast.h"
#ifdef CONFIG_FAST_COPY
#include "core/fast_copy.h"
#endif
#ifdef CONFIG_SPECTRUM
#include "core/spectrum.h"
#endif

/* Colours, as on the TETRA screens */
static const color_t mdGroup = { 90, 200, 255, 255 };  /* Sky blue */
static const color_t mdVoice = { 60, 255, 120, 255 };  /* Bright green */
static const color_t mdMuted = { 150, 150, 150, 255 }; /* Grey */
static const color_t mdTx = { 240, 60, 50, 255 };      /* Red */
static const color_t mdTrack = { 45, 45, 45, 255 };
static const color_t mdGood = { 60, 230, 90, 255 };
static const color_t mdFair = { 250, 200, 40, 255 };
static const color_t mdPoor = { 240, 60, 50, 255 };

/** Traffic light colour of a reading */
static color_t _ui_mdTraffic(unsigned value, unsigned good, unsigned fair)
{
    if (value >= good)
        return mdGood;
    if (value >= fair)
        return mdFair;
    return mdPoor;
}

/**
 * Width of a text in pixels, measured a few letters at a time:
 * gfx_getTextWidth() stops counting at the screen width.
 */
static uint16_t _ui_mdTextWidth(fontSize_t font, const char *text)
{
    char part[5];
    uint16_t width = 0;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i += sizeof(part) - 1) {
        snprintf(part, sizeof(part), "%s", text + i);
        width += gfx_getTextWidth(font, part);
    }
    return width;
}

/** Shorten a text, in place, until it fits in width pixels */
static void _ui_mdFit(char *text, fontSize_t font, uint16_t width)
{
    size_t n = strlen(text);

    while (n > 0 && _ui_mdTextWidth(font, text) > width)
        text[--n] = '\0';
}

/**
 * A large line, centred: an optional prefix in its colour, then the text,
 * in the largest font up to the given one that fits the screen, less a
 * margin on each side (for a symbol).
 */
static void _ui_mdBigLine(point_t pos, fontSize_t largest, uint16_t margin,
                          const char *prefix, color_t cp, const char *text,
                          color_t ct)
{
    static const fontSize_t fonts[] = { FONT_SIZE_16PT, FONT_SIZE_12PT,
                                        FONT_SIZE_10PT, FONT_SIZE_8PT };
    uint16_t room = CONFIG_SCREEN_WIDTH - 2 * (layout.horizontal_pad + margin);
    fontSize_t font = FONT_SIZE_8PT;
    uint16_t wp = 0, wt = 0;
    char fitted[40];

    snprintf(fitted, sizeof(fitted), "%s", text);
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (fonts[i] > largest)
            continue;
        font = fonts[i];
        wp = (prefix != NULL) ? _ui_mdTextWidth(font, prefix) : 0;
        wt = _ui_mdTextWidth(font, fitted);
        if (wp + wt <= room)
            break;
    }
    if (wp + wt > room) {
        _ui_mdFit(fitted, font, room - wp);
        wt = _ui_mdTextWidth(font, fitted);
    }

    int x = ((int)CONFIG_SCREEN_WIDTH - wp - wt) / 2;
    pos.x = (uint16_t)(x < 0 ? 0 : x);
    if (prefix != NULL)
        gfx_print(pos, font, TEXT_ALIGN_LEFT, cp, "%s", prefix);
    pos.x += wp;
    gfx_print(pos, font, TEXT_ALIGN_LEFT, ct, "%s", fitted);
}

/** Pieces of a line of text, each in its colour */
struct mdPiece {
    char text[24];
    color_t color;
};

/** Print pieces centred on a line, two spaces apart */
static void _ui_mdPieces(point_t pos, fontSize_t font, const struct mdPiece *p,
                         size_t n)
{
    uint16_t gap = _ui_mdTextWidth(font, "  ");
    uint16_t total = 0;

    for (size_t i = 0; i < n; i++)
        total += _ui_mdTextWidth(font, p[i].text) + ((i > 0) ? gap : 0);

    int x = ((int)CONFIG_SCREEN_WIDTH - total) / 2;
    pos.x = (uint16_t)(x < 0 ? 0 : x);
    for (size_t i = 0; i < n; i++) {
        if (i > 0)
            pos.x += gap;
        gfx_print(pos, font, TEXT_ALIGN_LEFT, p[i].color, "%s", p[i].text);
        pos.x += _ui_mdTextWidth(font, p[i].text);
    }
}

/**
 * One labelled bar of the meter: the label on the left, then a track
 * filled to value / max.
 */
static void _ui_mdBar(point_t pos, uint16_t width, uint16_t height,
                      uint16_t labelWidth, const char *label, unsigned value,
                      unsigned max, color_t color)
{
    point_t text = { pos.x, (uint16_t)(pos.y + height) };
    point_t bar = { (uint16_t)(pos.x + labelWidth), pos.y };
    uint16_t barWidth = width - labelWidth;

    gfx_print(text, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, mdMuted, "%s", label);
    gfx_drawRect(bar, barWidth, height, mdTrack, true);
    if (value > max)
        value = max;
    if (value != 0)
        gfx_drawRect(bar, (uint16_t)(barWidth * value / max), height, color,
                     true);
}

/** Where the meter goes: as the S-meter of the other modes */
static void _ui_mdMeterArea(point_t *pos, uint16_t *width, uint16_t *height)
{
    *width = CONFIG_SCREEN_WIDTH - 2 * layout.horizontal_pad;
    *height = layout.bottom_h;
    pos->x = layout.horizontal_pad;
    pos->y = CONFIG_SCREEN_HEIGHT - *height - layout.bottom_pad;
}

/**
 * The meter: a thin volume bar, then two readings.
 *
 * @param mark: on the first reading, a tick at this value (0: none).
 */
static void _ui_mdMeter(const char *l1, unsigned v1, unsigned max1, color_t c1,
                        unsigned mark, const char *l2, unsigned v2,
                        unsigned max2, color_t c2)
{
    point_t pos;
    uint16_t width, height;
    const uint16_t gap = 3;

    _ui_mdMeterArea(&pos, &width, &height);
    uint16_t vol = height / 8;
    uint16_t bar = (height - vol - 2 * gap) / 2;
    uint16_t label = gfx_getTextWidth(FONT_SIZE_5PT, "VOL") + 4;

    _ui_mdBar(pos, width, vol, label, "VOL", last_state.volume, 255, mdMuted);
    pos.y += vol + gap;
    _ui_mdBar(pos, width, bar, label, l1, v1, max1, c1);
    if (mark > 0 && mark <= max1) {
        uint16_t x = pos.x + label + (width - label - 1) * mark / max1;
        point_t top = { x, (uint16_t)(pos.y - 1) };
        point_t bottom = { x, (uint16_t)(pos.y + bar) };
        gfx_drawLine(top, bottom, yellow_fab413);
        top.x++;
        bottom.x++;
        gfx_drawLine(top, bottom, yellow_fab413);
    }
    pos.y += bar + gap;
    _ui_mdBar(pos, width, bar, label, l2, v2, max2, c2);
}

/** A frequency as text, MHz with 5 decimals */
static void _ui_mdFreq(char *buf, size_t len, uint32_t hz)
{
    snprintf(buf, len, "%lu.%05lu", (unsigned long)(hz / 1000000),
             (unsigned long)(hz % 1000000 / 10));
}

/** Big and small line positions, a little apart on a 128-line screen */
static void _ui_mdLines(point_t *big, point_t *small)
{
    *big = layout.line3_pos;
    *small = layout.line4_pos;
#if CONFIG_SCREEN_HEIGHT > 127
    big->y -= 6;
    small->y -= 3;
#endif
}

/**
 * The FM settings in effect, small, on line 1: channel width, tones,
 * repeater shift and power.
 */
static void _ui_drawFmInfo(void)
{
    const channel_t *ch = &last_state.channel;
    struct mdPiece p[4];
    size_t n = 0;
    char t[10], r[10];

    snprintf(p[n].text, sizeof(p[n].text), "%s",
             (ch->bandwidth == BW_12_5) ? "12.5k" : "25k");
    p[n++].color = mdMuted;

    _ui_toneName(t, sizeof(t), ch->fm.txTone, ch->txDcs, ch->txDcsInv);
    _ui_toneName(r, sizeof(r), ch->fm.rxTone, ch->rxDcs, ch->rxDcsInv);
    bool same = (ch->fm.txTone == ch->fm.rxTone) && (ch->txDcs == ch->rxDcs)
             && (!ch->txDcs || ch->txDcsInv == ch->rxDcsInv);
    if (ch->fm.txToneEn && ch->fm.rxToneEn && same)
        snprintf(p[n].text, sizeof(p[n].text), "%s%s", ch->txDcs ? "" : "TSQ ",
                 t);
    else if (ch->fm.txToneEn && ch->fm.rxToneEn)
        snprintf(p[n].text, sizeof(p[n].text), "T%s R%s", t, r);
    else if (ch->fm.txToneEn)
        snprintf(p[n].text, sizeof(p[n].text), "T %s", t);
    else if (ch->fm.rxToneEn)
        snprintf(p[n].text, sizeof(p[n].text), "R %s", r);
    else
        p[n].text[0] = '\0';
    if (p[n].text[0] != '\0')
        p[n++].color = mdGroup;

    /* Repeater shift, in MHz, or kHz under 1 MHz */
    int32_t shift = (int32_t)(ch->tx_frequency - ch->rx_frequency);
    if (shift != 0) {
        int32_t a = (shift < 0) ? -shift : shift;
        char sign = (shift < 0) ? '-' : '+';
        if (a >= 1000000)
            snprintf(p[n].text, sizeof(p[n].text), "%c%ld.%ldM", sign,
                     (long)(a / 1000000), (long)(a % 1000000 / 100000));
        else
            snprintf(p[n].text, sizeof(p[n].text), "%c%ldk", sign,
                     (long)(a / 1000));
        p[n++].color = yellow_fab413;
    }

    uint32_t mw = ch->power;
    if (ch->rx_only)
        snprintf(p[n].text, sizeof(p[n].text), "RX only");
    else if (mw % 1000 == 0)
        snprintf(p[n].text, sizeof(p[n].text), "%luW",
                 (unsigned long)(mw / 1000));
    else
        snprintf(p[n].text, sizeof(p[n].text), "%lu.%luW",
                 (unsigned long)(mw / 1000), (unsigned long)(mw % 1000 / 100));
    p[n++].color = mdMuted;

    point_t pos = layout.line1_pos;
#if CONFIG_SCREEN_HEIGHT > 127
    pos.y += 2;
#endif
    _ui_mdPieces(pos, FONT_SIZE_6PT, p, n);
}

/**
 * FM main screen: the settings in effect, then the channel name (memory
 * mode) or the frequency (VFO) large, green while receiving and red while
 * transmitting, and below it the frequency or channel; the meter shows the
 * signal against the squelch level, and the microphone when transmitting.
 */
void _ui_drawFmMain(ui_state_t *ui_state, bool memory)
{
    const channel_t *ch = &last_state.channel;
    rtxStatus_t rtx = rtx_getCurrentStatus();
    /* PTT, or VOX */
    bool tx = (rtx.opStatus == TX);
    bool rx = !tx && rtx_rxSquelchOpen();
    color_t color = tx ? mdTx : (rx ? mdVoice : color_white);
    point_t big, small;
    char text[40], freq[16];

    _ui_mdLines(&big, &small);
    _ui_drawFmInfo();
    /* VOX and AI noise reduction on: top left, where line 1 has no room
     * for them */
    point_t flag = { layout.horizontal_pad, layout.top_pos.y };
    (void)flag;
#ifdef CONFIG_VOX
    if (last_state.settings.voxLevel != 0) {
        gfx_print(flag, layout.top_font, TEXT_ALIGN_LEFT, yellow_fab413,
                  "VOX ");
        flag.x += _ui_mdTextWidth(layout.top_font, "VOX ");
    }
#endif
#ifdef CONFIG_NOISE_REDUCTION
    if (last_state.settings.nrRx != 0 || last_state.settings.nrTx != 0) {
        bool overload = false;
        audio_getNoiseReductionLoad(&overload);
        gfx_print(flag, layout.top_font, TEXT_ALIGN_LEFT,
                  overload ? mdTx : mdGroup, "AI");
    }
#endif

    _ui_mdFreq(freq, sizeof(freq), tx ? ch->tx_frequency : ch->rx_frequency);
    if (memory) {
        _ui_mdBigLine(big, FONT_SIZE_16PT, 0, NULL, color, ch->name, color);
        snprintf(text, sizeof(text), "Ch %u  %s", last_state.channel_index + 1,
                 freq);
    } else {
        _ui_mdBigLine(big, FONT_SIZE_16PT, 0, NULL, color, freq, color);
        if (ch->tx_frequency != ch->rx_frequency) {
            char other[16];
            _ui_mdFreq(other, sizeof(other),
                       tx ? ch->rx_frequency : ch->tx_frequency);
            snprintf(text, sizeof(text), "%s %s", tx ? "RX" : "TX", other);
        } else {
            uint32_t step = freq_steps[last_state.step_index];
            if (step % 1000 == 0)
                snprintf(text, sizeof(text), "VFO  step %luk",
                         (unsigned long)(step / 1000));
            else
                snprintf(text, sizeof(text), "VFO  step %lu.%02luk",
                         (unsigned long)(step / 1000),
                         (unsigned long)(step % 1000 / 10));
        }
    }
    /* What the radio is doing, in place of the line below the station */
    if (tx && memory)
        snprintf(text, sizeof(text), "TX %s", freq);
    else if (tx)
        snprintf(text, sizeof(text), "Transmitting");
    else if (rx)
        snprintf(text, sizeof(text), "Receiving  S%u",
                 rssiToSlevel(last_state.rssi));
    color_t smallColor = tx ? mdTx : (rx ? mdVoice : mdMuted);
#ifdef CONFIG_SCAN
    if (!tx && !rx && ui_state->scan_on) {
        const char *what = ui_state->scan_stopped   ? "Paused" :
                           (ui_state->scan_dir > 0) ? "Scan up" :
                                                      "Scan down";
        if (memory)
            snprintf(text, sizeof(text), "%s  Ch %u", what,
                     last_state.channel_index + 1);
        else
            snprintf(text, sizeof(text), "%s", what);
        smallColor = yellow_fab413;
    }
#endif
    gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER, smallColor, "%s",
              text);

    /* Signal, with the squelch level as a tick; the squelch opens at
     * -127 + level * 66 / 15 dBm (OpMode_FM) */
    uint8_t sql = last_state.settings.sqlLevel;
    unsigned slevel = rssiToSlevel(last_state.rssi);
    unsigned mark = (sql == 0) ? 0 :
                                 rssiToSlevel((rssi_t)(-127 + sql * 66 / 15));
    if (tx)
        _ui_mdMeter("SIG", slevel, 11, _ui_mdTraffic(slevel, 7, 4), mark, "MIC",
                    platform_getMicLevel(), 255, mdTx);
    else
        _ui_mdMeter("SIG", slevel, 11,
                    rx ? mdVoice : _ui_mdTraffic(slevel, 7, 4), mark, "SQL",
                    sql, 15, yellow_fab413);
}

#ifdef CONFIG_FAST_COPY
/**
 * Fast copy: what it is doing, and what it found. A screen of its own, from
 * a long * in FM.
 */
void _ui_drawFastCopy(ui_state_t *ui_state)
{
    (void)ui_state;
    struct fastCopyResult r;
    point_t big, small;
    char text[24], freq[16];

    fastCopy_get(&r);
    gfx_clearScreen();
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, color_white,
              "Fast copy");
    _ui_mdLines(&big, &small);
    point_t l1 = layout.line1_pos;
#if CONFIG_SCREEN_HEIGHT > 127
    l1.y += 2;
#endif

    if (r.state == FAST_COPY_FREQ || r.state == FAST_COPY_OFF) {
        gfx_print(l1, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, mdMuted,
                  "key the other radio nearby");
        _ui_mdBigLine(big, FONT_SIZE_16PT, 0, NULL, mdMuted, "listening",
                      mdMuted);
        if (r.timedOut)
            gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER, mdTx,
                      "nothing heard yet");
        else
            gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER, mdMuted,
                      "red to stop");
        return;
    }

    _ui_mdFreq(freq, sizeof(freq), fastCopy_channel(r.hz));

    if (r.state == FAST_COPY_TONE) {
        gfx_print(l1, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, mdMuted,
                  "frequency found");
        _ui_mdBigLine(big, FONT_SIZE_16PT, 0, NULL, color_white, freq,
                      color_white);
        gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER, mdMuted,
                  "reading the tone...");
        return;
    }

    if (!r.toneFound)
        snprintf(text, sizeof(text), "no tone");
    else if (r.tone & RTX_TONE_DCS)
        snprintf(text, sizeof(text), "DCS D%03o%c", r.tone & 0x1ff,
                 (r.tone & RTX_TONE_DCS_INV) ? 'I' : 'N');
    else
        snprintf(text, sizeof(text), "CTCSS %u.%u", r.tone / 10, r.tone % 10);
    gfx_print(l1, FONT_SIZE_8PT, TEXT_ALIGN_CENTER,
              r.toneFound ? mdGroup : mdMuted, "%s", text);
    _ui_mdBigLine(big, FONT_SIZE_16PT, 0, NULL, mdVoice, freq, mdVoice);
    gfx_print(small, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, mdMuted,
              "green copies, red cancels");
}

#endif

#ifdef CONFIG_SPECTRUM
/* Distance between the points of the spectrum view at each zoom, Hz: the
 * view is SPECTRUM_BINS of them wide */
static const uint32_t spectrumSteps[SPECTRUM_ZOOMS] = {
    2500, 5000, 6250, 12500, 25000, 50000, 100000, 250000,
};

uint32_t _ui_spectrumStep(uint8_t zoom)
{
    return spectrumSteps[(zoom < SPECTRUM_ZOOMS) ? zoom : SPECTRUM_ZOOM_START];
}

int _ui_spectrumPeak(const struct spectrumSweep *s)
{
    int best = -1;

    if (s->count == 0)
        return -1;
    for (int i = 0; i < SPECTRUM_BINS; i++) {
        if (s->rssi[i] == SPECTRUM_NONE)
            continue;
        if (best < 0 || s->rssi[i] > s->rssi[best])
            best = i;
    }
    return best;
}

/** A distance in Hz as text: 12.5k, 1M, 2.5M */
static void _ui_specHz(char *buf, size_t len, uint32_t hz)
{
    if (hz >= 1000000 && hz % 1000000 == 0)
        snprintf(buf, len, "%luM", (unsigned long)(hz / 1000000));
    else if (hz >= 1000000)
        snprintf(buf, len, "%lu.%luM", (unsigned long)(hz / 1000000),
                 (unsigned long)(hz % 1000000 / 100000));
    else if (hz % 1000 == 0)
        snprintf(buf, len, "%luk", (unsigned long)(hz / 1000));
    else if (hz % 100 == 0)
        snprintf(buf, len, "%lu.%luk", (unsigned long)(hz / 1000),
                 (unsigned long)(hz % 1000 / 100));
    else
        snprintf(buf, len, "%lu.%02luk", (unsigned long)(hz / 1000),
                 (unsigned long)(hz % 1000 / 10));
}

/* Bottom and top of the graph's scale, dBm */
#define SPEC_FLOOR (-130)
#define SPEC_TOP (-40)

/**
 * Spectrum view: the frequency in the middle and the width above, a bar
 * for each point, the edges below and the strongest signal at the bottom.
 * From side key + 0 in FM.
 */
void _ui_drawSpectrum(ui_state_t *ui_state)
{
    static struct spectrumSweep s;
    uint32_t step = _ui_spectrumStep(ui_state->spec_zoom);
    char text[32], freq[16], width[12];

    spectrum_get(&s);
    gfx_clearScreen();

    /* The view asked for: the frequency in the middle, then its width */
    _ui_mdFreq(freq, sizeof(freq), ui_state->spec_center);
    point_t pos = { 0, 11 };
    gfx_print(pos, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, color_white, "%s", freq);
    _ui_specHz(text, sizeof(text), step);
    _ui_specHz(width, sizeof(width), step * SPECTRUM_BINS);
    pos.y = 20;
    gfx_print(pos, FONT_SIZE_5PT, TEXT_ALIGN_CENTER, mdMuted,
              "%s wide  step %s", width, text);

    /* The graph, two pixels a point */
    const uint16_t top = 24, bottom = 101;
    const uint16_t barW = CONFIG_SCREEN_WIDTH / SPECTRUM_BINS;
    const uint16_t left = (CONFIG_SCREEN_WIDTH - barW * SPECTRUM_BINS) / 2;
    int peak = _ui_spectrumPeak(&s);
    rssi_t sql = -127 + (last_state.settings.sqlLevel * 66) / 15;
    bool current = (s.count > 0) && (s.center == ui_state->spec_center)
                && (s.step == step);

    for (uint16_t y = top; y < bottom; y += 18) {
        point_t a = { left, y };
        point_t b = { (uint16_t)(CONFIG_SCREEN_WIDTH - left - 1), y };
        gfx_drawLine(a, b, mdTrack);
    }
    point_t ca = { (uint16_t)(left + barW * (SPECTRUM_BINS / 2)), top };
    for (uint16_t y = top; y < bottom; y += 4) {
        point_t a = { ca.x, y };
        point_t b = { ca.x, (uint16_t)(y + 1) };
        gfx_drawLine(a, b, yellow_fab413);
    }

    for (int i = 0; current && i < SPECTRUM_BINS; i++) {
        int16_t v = s.rssi[i];
        if (v == SPECTRUM_NONE || v <= SPEC_FLOOR)
            continue;
        if (v > SPEC_TOP)
            v = SPEC_TOP;
        uint16_t h = (uint16_t)((v - SPEC_FLOOR) * (bottom - top)
                                / (SPEC_TOP - SPEC_FLOOR));
        if (h == 0)
            continue;
        color_t c = (i == peak)       ? color_white :
                    (s.rssi[i] > sql) ? mdVoice :
                                        mdGroup;
        point_t bar = { (uint16_t)(left + barW * i), (uint16_t)(bottom - h) };
        gfx_drawRect(bar, barW, h, c, true);
    }
    point_t base = { left, bottom };
    gfx_drawRect(base, (uint16_t)(barW * SPECTRUM_BINS), 1, mdMuted, true);

    /* The edges */
    uint32_t half = step * (SPECTRUM_BINS / 2);
    uint32_t lo = ui_state->spec_center - half;
    uint32_t hi = ui_state->spec_center + half;
    pos.y = 110;
    pos.x = 1;
    gfx_print(pos, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, mdMuted, "%lu.%03lu",
              (unsigned long)(lo / 1000000),
              (unsigned long)(lo % 1000000 / 1000));
    pos.x = 0;
    gfx_print(pos, FONT_SIZE_5PT, TEXT_ALIGN_RIGHT, mdMuted, "%lu.%03lu",
              (unsigned long)(hi / 1000000),
              (unsigned long)(hi % 1000000 / 1000));

    /* The strongest signal, which green tunes to */
    pos.x = 0;
    pos.y = 124;
    if (!current || peak < 0) {
        gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, mdMuted,
                  "sweeping...");
        return;
    }
    _ui_mdFreq(freq, sizeof(freq), spectrum_binHz(s.center, s.step, peak));
    bool strong = s.rssi[peak] > sql;
    gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, strong ? mdVoice : mdMuted,
              "%s  %d dBm", freq, (int)s.rssi[peak]);
}
#endif

#ifdef CONFIG_BCAST_FM
/**
 * Broadcast FM radio: the station large, what is happening below it, and
 * its signal. From Menu > FM radio.
 */
void _ui_drawBcast(ui_state_t *ui_state)
{
    struct bcastStatus st;
    point_t big, small;
    char text[32];

    bcast_get(&st);
    gfx_clearScreen();
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, color_white,
              "FM radio");
    _ui_mdLines(&big, &small);
    point_t l1 = layout.line1_pos;
#if CONFIG_SCREEN_HEIGHT > 127
    l1.y += 2;
#endif

    if (last_state.channel.mode != OPMODE_FM) {
        _ui_mdBigLine(big, FONT_SIZE_12PT, 0, NULL, mdMuted, "FM mode only",
                      mdMuted);
        gfx_print(small, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, mdMuted,
                  "side key + 5 changes mode");
        return;
    }

    uint32_t hz = (st.state != BCAST_OFF) ?
                      st.hz :
                      (uint32_t)last_state.settings.bcastFreq * 100000u;
    uint16_t unit = (uint16_t)(hz / 100000u);

    /* The saved station this is, top left */
    for (int i = 0; i < BCAST_PRESETS; i++) {
        if (last_state.settings.bcastPreset[i] == unit) {
            point_t p = { layout.horizontal_pad, layout.top_pos.y };
            gfx_print(p, layout.top_font, TEXT_ALIGN_LEFT, yellow_fab413, "%d",
                      i + 1);
            break;
        }
    }

    snprintf(text, sizeof(text), "%u.%u MHz", unit / 10, unit % 10);
    color_t c = (st.state == BCAST_SEEKING || st.channelBusy) ? mdMuted :
                                                                color_white;
    _ui_mdBigLine(big, FONT_SIZE_16PT, 0, NULL, c, text, c);

    /* Line 1: typing, a message, or the state */
    long long now = getTick();
    if (ui_state->bcast_digits[0] != '\0') {
        gfx_print(l1, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, yellow_fab413,
                  "Go to %s_", ui_state->bcast_digits);
    } else if (ui_state->bcast_note != NULL
               && now - ui_state->bcast_note_tick < 1500) {
        gfx_print(l1, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, yellow_fab413, "%s",
                  ui_state->bcast_note);
    } else if (st.state == BCAST_SEEKING) {
        gfx_print(l1, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, yellow_fab413,
                  "seeking...");
    }

    /* Below: the channel, when it is heard instead */
    if (st.channelBusy) {
        bool tx = rtx_getCurrentStatus().opStatus == TX;
        gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER,
                  tx ? mdTx : mdVoice, "%s %s", tx ? "TX" : "RX",
                  last_state.channel.name);
    } else {
        gfx_print(small, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, mdMuted,
                  "hold ^v: seek   hold 1-9: save");
    }

    unsigned rssi = st.rssi;
    _ui_mdMeter("SIG", rssi, 80, _ui_mdTraffic(rssi, 40, 20), 0, "SNR", st.snr,
                15, mdGroup);
}
#endif

#endif /* CONFIG_UI_FM_SCREEN */
