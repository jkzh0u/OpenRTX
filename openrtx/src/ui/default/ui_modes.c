/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Main screens of FM (CONFIG_UI_FM_SCREEN) and of DMR receive
 * (CONFIG_DMR_RX), drawn like the TETRA ones: the station large, what is
 * heard above it, and a meter of labelled bars at the bottom. For colour
 * screens of 128 lines or more.
 */

#include <stdio.h>
#include <string.h>
#include "ui/ui_default.h"

#if defined(CONFIG_UI_FM_SCREEN) || defined(CONFIG_DMR_RX)

#include "core/utils.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "rtx/rtx.h"
#ifdef CONFIG_DMR_RX
#include "protocols/DMR/DmrStatus.h"
#endif

/* Colours, as on the TETRA screens */
static const color_t mdGroup = { 90, 200, 255, 255 };  /* Sky blue */
static const color_t mdPlace = { 110, 220, 110, 255 }; /* Green */
static const color_t mdVoice = { 60, 255, 120, 255 };  /* Bright green */
static const color_t mdTalk = { 255, 191, 0, 255 };    /* Amber */
static const color_t mdMuted = { 150, 150, 150, 255 }; /* Grey */
static const color_t mdWarn = { 255, 150, 30, 255 };   /* Orange */
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

/** Text in the top bar, left, in the small font of the battery figure */
static void _ui_mdTopText(const char *label, const char *text, color_t color)
{
    point_t pos = { layout.horizontal_pad, (uint16_t)(layout.top_pos.y - 2) };
    char buf[32];

    snprintf(buf, sizeof(buf), "%s%s", label, text);
    gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, color, "%s", buf);
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

#ifdef CONFIG_UI_FM_SCREEN

/** Tone index as text, e.g. 67.0 */
static void _ui_mdTone(char *buf, size_t len, uint8_t index)
{
    uint16_t tone = ctcss_tone[(index < CTCSS_FREQ_NUM) ? index : 0];
    snprintf(buf, len, "%u.%u", tone / 10, tone % 10);
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
    char t[8], r[8];

    snprintf(p[n].text, sizeof(p[n].text), "%s",
             (ch->bandwidth == BW_12_5) ? "12.5k" : "25k");
    p[n++].color = mdMuted;

    _ui_mdTone(t, sizeof(t), ch->fm.txTone);
    _ui_mdTone(r, sizeof(r), ch->fm.rxTone);
    if (ch->fm.txToneEn && ch->fm.rxToneEn && ch->fm.txTone == ch->fm.rxTone)
        snprintf(p[n].text, sizeof(p[n].text), "TSQ %s", t);
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
    (void)ui_state;
    const channel_t *ch = &last_state.channel;
    rtxStatus_t rtx = rtx_getCurrentStatus();
    bool tx = platform_getPttStatus() && (rtx.opStatus == TX);
    bool rx = !tx && rtx_rxSquelchOpen();
    color_t color = tx ? mdTx : (rx ? mdVoice : color_white);
    point_t big, small;
    char text[40], freq[16];

    _ui_mdLines(&big, &small);
    _ui_drawFmInfo();

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
    gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER,
              tx ? mdTx : (rx ? mdVoice : mdMuted), "%s", text);

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

#endif /* CONFIG_UI_FM_SCREEN */

#ifdef CONFIG_DMR_RX

/** Length of a time as m:ss, or "now" */
static void _ui_mdDuration(char *buf, size_t len, long long ms)
{
    long long s = ms / 1000;
    snprintf(buf, len, "%lld:%02lld", s / 60, s % 60);
}

/** How long ago, short: 12s, 4m, 2h */
static void _ui_mdAge(char *buf, size_t len, long long ms)
{
    long long s = ms / 1000;
    if (s < 60)
        snprintf(buf, len, "%llds", s);
    else if (s < 3600)
        snprintf(buf, len, "%lldm", s / 60);
    else
        snprintf(buf, len, "%lldh", s / 3600);
}

/** Who a call is to: TG 3020, or the radio called */
static void _ui_mdCallDst(char *buf, size_t len, const struct dmrCall *c)
{
    if (!c->ids)
        snprintf(buf, len, "Voice");
    else if (c->group)
        snprintf(buf, len, "TG %lu", (unsigned long)c->dst);
    else
        snprintf(buf, len, "To %lu", (unsigned long)c->dst);
}

/** The timeslot of a call as text */
static const char *_ui_mdSlotName(uint8_t slot)
{
    switch (slot) {
        case 1:
            return "TS1";
        case 2:
            return "TS2";
        default:
            return "Simplex";
    }
}

/** The DMR filters, in the top bar, yellow while they hide calls */
static void _ui_drawDmrTop(void)
{
    char text[24] = "";
    uint8_t slots = last_state.settings.dmrSlotFilter;
    uint8_t cc = last_state.settings.dmrColourCode;

    if (slots == 0 && cc == 0)
        return;
    if (slots != 0)
        snprintf(text, sizeof(text), "TS%u", slots);
    if (cc != 0) {
        size_t n = strlen(text);
        snprintf(text + n, sizeof(text) - n, "%sCC%u", (n > 0) ? " " : "",
                 cc - 1);
    }
    _ui_mdTopText("", text, yellow_fab413);
}

/** The DMR meter: volume, signal and decoding quality */
static void _ui_drawDmrMeter(const struct dmrStatus *ds)
{
    unsigned slevel = rssiToSlevel(last_state.rssi);
    unsigned quality = 0;

    /* Full at 12% symbol error or better, empty at 40% */
    if (ds->locked && ds->symbolError < 40)
        quality = (ds->symbolError <= 12) ? 255 :
                                            (40 - ds->symbolError) * 255 / 28;
    _ui_mdMeter("SIG", slevel, 11, _ui_mdTraffic(slevel, 7, 4), 0, "DEC",
                quality, 255, _ui_mdTraffic(quality, 170, 90));
}

/**
 * DMR main screen. During a call: the talkgroup or radio called large, the
 * talking radio, the timeslot and colour code, and how long it has been
 * talking. Else: what each timeslot is doing, the channel or frequency
 * large, and below it the frequency and the colour code.
 */
void _ui_drawDmrMain(ui_state_t *ui_state, bool memory)
{
    (void)ui_state;
    struct dmrStatus ds;
    long long now = getTick();
    char text[40], freq[16];

    dmr_getStatus(&ds);
    _ui_drawDmrTop();

    const struct dmrStatusCall *sc = dmr_shownCall(&ds, now);
    if (sc != NULL) {
        const struct dmrCall *c = &sc->call;
        point_t l1 = layout.line1_pos, l2 = layout.line2_pos;
        point_t l3 = layout.line3_pos, l4 = layout.line4_pos;
#if CONFIG_SCREEN_HEIGHT > 127
        l1.y += 3;
        l2.y += 1;
        l3.y -= 1;
        l4.y -= 3;
#endif
        bool emergency = c->ids && (c->options & DMR_OPT_EMERGENCY);
        color_t dstColor = emergency ? mdTx : mdGroup;

        point_t sym = l1;
        sym.y -= (gfx_getFontHeight(FONT_SIZE_12PT)
                  - gfx_getFontHeight(layout.line1_font))
               / 2;
        gfx_drawSymbol(sym, layout.line1_symbol_size, TEXT_ALIGN_LEFT, dstColor,
                       SYMBOL_CALL_RECEIVED);
        _ui_mdCallDst(text, sizeof(text), c);
        _ui_mdBigLine(l1, FONT_SIZE_12PT,
                      gfx_getFontHeight(layout.line1_font) + 2, NULL, dstColor,
                      text, dstColor);

        gfx_drawSymbol(l2, layout.line2_symbol_size, TEXT_ALIGN_LEFT,
                       color_white, SYMBOL_CALL_MADE);
        if (c->ids)
            gfx_print(l2, layout.line2_font, TEXT_ALIGN_CENTER, color_white,
                      "%lu", (unsigned long)c->src);
        else
            gfx_print(l2, layout.line2_font, TEXT_ALIGN_CENTER, mdMuted,
                      "unknown");

        snprintf(text, sizeof(text), "%s  CC %u%s", _ui_mdSlotName(c->slot),
                 c->colourCode, c->lateEntry ? "  late" : "");
        gfx_print(l3, layout.line3_font, TEXT_ALIGN_CENTER, mdPlace, "%s",
                  text);

        if (emergency) {
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER, mdTx,
                      "Emergency");
        } else if (c->encrypted) {
            gfx_drawSymbol(l4, layout.line4_symbol_size, TEXT_ALIGN_LEFT,
                           mdWarn, SYMBOL_LOCK);
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER, mdWarn,
                      "Encrypted");
        } else if (sc->active) {
            char d[24];
            _ui_mdDuration(d, sizeof(d), now - sc->start);
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER, mdTalk,
                      "Voice %s", d);
        } else {
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER, mdMuted,
                      "Ended");
        }
        _ui_drawDmrMeter(&ds);
        return;
    }

    /*
     * No call. Line 1: the last call heard, or the state of the signal, and
     * the colour code. Then the station large: the channel, or the
     * frequency in VFO mode; below it the channel's frequency, or what each
     * timeslot carries.
     */
    point_t big, small;
    point_t l1 = layout.line1_pos;
#if CONFIG_SCREEN_HEIGHT > 127
    l1.y += 2;
#endif
    _ui_mdLines(&big, &small);

    struct mdPiece p[3];
    size_t n = 0;
    if (ds.nRecent > 0) {
        char age[16];
        _ui_mdCallDst(p[n].text, sizeof(p[n].text), &ds.recent[0].call);
        p[n++].color = mdGroup;
        _ui_mdAge(age, sizeof(age), now - ds.recent[0].last);
        snprintf(p[n].text, sizeof(p[n].text), "%s ago", age);
        p[n++].color = mdMuted;
    } else if (ds.locked) {
        snprintf(p[n].text, sizeof(p[n].text), "%s",
                 (ds.sync == DMR_SYNC_BS) ? "Repeater idle" : "Simplex");
        p[n++].color = mdMuted;
    }
    if (ds.locked && ds.ccKnown) {
        snprintf(p[n].text, sizeof(p[n].text), "CC %u", ds.colourCode);
        p[n++].color = mdPlace;
    }
    if (n > 0)
        _ui_mdPieces(l1, FONT_SIZE_6PT, p, n);

    uint32_t hz = (ds.tunedHz != 0) ? ds.tunedHz :
                                      last_state.channel.rx_frequency;
    _ui_mdFreq(freq, sizeof(freq), hz);

    if (!ds.locked)
        _ui_mdBigLine(big, FONT_SIZE_16PT, 0, "D ", mdMuted, "listening",
                      mdMuted);
    else
        _ui_mdBigLine(big, FONT_SIZE_16PT, 0, "D ", mdMuted,
                      memory ? last_state.channel.name : freq, color_white);

    if (memory) {
        snprintf(text, sizeof(text), "Ch %u  %s", last_state.channel_index + 1,
                 freq);
        gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER, mdMuted, "%s",
                  text);
    } else if (!ds.locked) {
        gfx_print(small, layout.line4_font, TEXT_ALIGN_CENTER, mdMuted, "%s",
                  freq);
    } else if (ds.sync == DMR_SYNC_BS) {
        /* Each timeslot: amber with a call, grey idle */
        n = 0;
        for (uint8_t t = 1; t <= 2; t++) {
            snprintf(p[n].text, sizeof(p[n].text), "TS%u %s", t,
                     ds.slot[t].active ? "call" : "idle");
            p[n++].color = ds.slot[t].active ? mdTalk : mdMuted;
        }
        _ui_mdPieces(small, layout.line4_font, p, n);
    }

    _ui_drawDmrMeter(&ds);
}

/**
 * Recent DMR calls, newest first: who was called, the talking radio, the
 * timeslot and how long ago. Up and Down scroll.
 */
void _ui_drawDmrRecent(ui_state_t *ui_state)
{
    struct dmrStatus ds;
    long long now = getTick();

    dmr_getStatus(&ds);
    gfx_clearScreen();
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, color_white,
              "DMR calls");

    if (ds.nRecent == 0) {
        gfx_print(layout.line3_pos, layout.line3_font, TEXT_ALIGN_CENTER,
                  mdMuted, "None yet");
        return;
    }

    /* Two lines a call */
    uint16_t h = gfx_getFontHeight(FONT_SIZE_8PT);
    uint16_t hs = gfx_getFontHeight(FONT_SIZE_6PT);
    uint16_t row = h + hs + 6;
    uint16_t first = (ui_state->menu_selected < ds.nRecent) ?
                         ui_state->menu_selected :
                         ds.nRecent - 1;
    point_t pos = { layout.horizontal_pad, (uint16_t)(layout.top_h + h + 4) };

    for (uint8_t i = first; i < ds.nRecent; i++) {
        if (pos.y + hs + 2 > CONFIG_SCREEN_HEIGHT)
            break;
        const struct dmrStatusCall *r = &ds.recent[i];
        char dst[24], age[16], line[40];

        _ui_mdCallDst(dst, sizeof(dst), &r->call);
        _ui_mdAge(age, sizeof(age), now - r->last);
        gfx_print(pos, FONT_SIZE_8PT, TEXT_ALIGN_LEFT,
                  r->call.encrypted ? mdWarn : mdGroup, "%s", dst);
        gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_RIGHT, mdMuted, "%s", age);

        point_t sub = { pos.x, (uint16_t)(pos.y + hs + 3) };
        if (r->call.ids)
            snprintf(line, sizeof(line), "from %lu  %s",
                     (unsigned long)r->call.src, _ui_mdSlotName(r->call.slot));
        else
            snprintf(line, sizeof(line), "%s", _ui_mdSlotName(r->call.slot));
        gfx_print(sub, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, mdMuted, "%s", line);
        pos.y += row;
    }
}

#endif /* CONFIG_DMR_RX */

#endif /* CONFIG_UI_FM_SCREEN || CONFIG_DMR_RX */
