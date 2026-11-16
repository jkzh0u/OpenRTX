/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/platform.h"
#include "interfaces/cps_io.h"
#include "interfaces/delays.h"
#include <stdio.h>
#include <stdint.h>
#include "ui/ui_default.h"
#include <string.h>
#include "ui/ui_strings.h"
#include "core/utils.h"
#include "ui/utils.h"
#ifdef CONFIG_TETRA
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraStatus.h"

/* An SDS text stays on the main screen this long */
#define TETRA_SDS_SHOW_MS 30000
/* After a move to another cell, its location is highlighted this long */
#define TETRA_CELL_SHOW_MS 5000

/* Colours of the TETRA screens */
static const color_t tetraGroup = { 90, 200, 255, 255 };   /* Sky blue */
static const color_t tetraPlace = { 110, 220, 110, 255 };  /* Green */
static const color_t tetraVoice = { 60, 255, 120, 255 };   /* Bright green */
static const color_t tetraMuted = { 150, 150, 150, 255 };  /* Grey */
static const color_t tetraWarn = { 255, 150, 30, 255 };    /* Orange */
static const color_t tetraText = { 255, 230, 140, 255 };   /* Light yellow */

/* The meter: a traffic light */
static const color_t tetraGood = { 60, 230, 90, 255 };
static const color_t tetraFair = { 250, 200, 40, 255 };
static const color_t tetraPoor = { 240, 60, 50, 255 };

/**
 * Traffic light colour of a reading: green from good, yellow from fair,
 * red below.
 */
static color_t _ui_tetraTraffic(unsigned value, unsigned good, unsigned fair)
{
    if (value >= good)
        return tetraGood;
    if (value >= fair)
        return tetraFair;
    return tetraPoor;
}

/* On the red alarm screen everything is white */
static bool tetraAlarmScreen;

static color_t _ui_tetraColor(color_t c)
{
    return tetraAlarmScreen ? color_white : c;
}

/**
 * Print an SSI: its name from the built-in list, else the number, or a
 * dash if it is not known.
 */
static void _ui_printSsi(point_t pos, fontSize_t font, const char *prefix,
                         uint32_t ssi, color_t color)
{
    char name[24];

    color = _ui_tetraColor(color);
    if (tetraIds_nameOf(ssi, name, sizeof(name)))
        gfx_print(pos, font, TEXT_ALIGN_CENTER, color, "%s", name);
    else if (ssi != 0)
        gfx_print(pos, font, TEXT_ALIGN_CENTER, color, "%s%lu", prefix,
                  (unsigned long)ssi);
    else
        gfx_print(pos, font, TEXT_ALIGN_CENTER, color, "%s-", prefix);
}

/**
 * Print two pieces of text centred together on a line, each in its colour.
 */
static void _ui_print2(point_t pos, fontSize_t font, const char *a,
                       color_t ca, const char *b, color_t cb)
{
    uint16_t wa = gfx_getTextWidth(font, a);
    uint16_t wb = gfx_getTextWidth(font, b);
    int x = ((int)CONFIG_SCREEN_WIDTH - wa - wb) / 2;

    pos.x = (uint16_t)(x < 0 ? 0 : x);
    gfx_print(pos, font, TEXT_ALIGN_LEFT, _ui_tetraColor(ca), "%s", a);
    pos.x += wa;
    gfx_print(pos, font, TEXT_ALIGN_LEFT, _ui_tetraColor(cb), "%s", b);
}

/**
 * Check whether a TETRA call takes over the main screen.
 */
static bool _ui_tetraCallShown(const struct tetraStatus *ts)
{
    return ts->sync && ts->call;
}

/* Line 1 in VFO mode: signal figures instead of the last call heard */
static bool tetraShowSignal;

/**
 * Switch line 1 between the last call heard and the signal figures.
 */
void _ui_tetraToggleLine1(void)
{
    tetraShowSignal = !tetraShowSignal;
}

/**
 * Format a time since, e.g. 45s, 12m, 3h.
 */
static void _ui_tetraAge(char *buf, size_t len, long long ms)
{
    long long s = ms / 1000;

    if (s < 60)
        snprintf(buf, len, "%llds", s);
    else if (s < 3600)
        snprintf(buf, len, "%lldm", s / 60);
    else
        snprintf(buf, len, "%lldh", s / 3600);
}

/**
 * Print "prefix name suffix" into out, shortening the name until the text
 * fits a width in pixels.
 */
static void _ui_tetraFit(char *out, size_t len, fontSize_t font,
                         uint16_t width, const char *prefix, const char *name,
                         const char *suffix)
{
    int n = (int)strlen(name);

    for (;;) {
        snprintf(out, len, "%s%.*s%s", prefix, n, name, suffix);
        if (n == 0 || gfx_getTextWidth(font, out) <= width)
            return;
        n--;
        while (n > 0 && name[n - 1] == ' ')
            n--;
    }
}

/**
 * Print "prefix name" in the top bar, in the small font of the battery
 * percentage: centred if it fits between it and the same margin on the
 * left, else from the left edge, shortening the name if needed.
 */
static void _ui_tetraTopText(const char *prefix, const char *name,
                             color_t color)
{
    uint16_t side = gfx_getTextWidth(FONT_SIZE_6PT, "100%")
                  + 2 * layout.horizontal_pad;
    point_t pos = { layout.top_pos.x, layout.top_pos.y - 2 };
    char text[40];

    _ui_tetraFit(text, sizeof(text), FONT_SIZE_6PT,
                 CONFIG_SCREEN_WIDTH - 2 * side, prefix, name, "");
    if (strlen(text) == strlen(prefix) + strlen(name)) {
        gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, color, "%s", text);
        return;
    }

    _ui_tetraFit(text, sizeof(text), FONT_SIZE_6PT,
                 CONFIG_SCREEN_WIDTH - side - layout.horizontal_pad, prefix,
                 name, "");
    pos.x = layout.horizontal_pad;
    gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, color, "%s", text);
}

/**
 * Name of a talkgroup or radio from the built-in list, or its number.
 */
static void _ui_tetraSsiName(char *buf, size_t len, uint32_t ssi, bool group)
{
    if (!tetraIds_nameOf(ssi, buf, len))
        snprintf(buf, len, "%s%lu", group ? "G " : "", (unsigned long)ssi);
}

/**
 * Where the radio is: the name of the location area from the built-in
 * list, else the channel of the list on this cell's main carrier if there
 * is only one (without a trailing carrier number), else the location area.
 */
static void _ui_tetraLocation(const struct tetraStatus *ts, char *buf,
                              size_t len)
{
    const char *la = ts->sysinfo ? tetraIds_laName(ts->la) : NULL;

    if (la != NULL) {
        snprintf(buf, len, "%s", la);
        return;
    }

    if (!ts->sysinfo) {
        snprintf(buf, len, "%u-%u", ts->mcc, ts->mnc);
        return;
    }

    channel_t ch;
    unsigned matches = 0;
    for (uint16_t i = 0; cps_readChannel(&ch, i) == 0; i++) {
        if (ch.mode != OPMODE_TETRA || ch.rx_frequency != ts->dlFreq)
            continue;
        if (matches++ == 0)
            snprintf(buf, len, "%s", ch.name);
    }

    if (matches == 1) {
        size_t n = strlen(buf);
        if (n > 2 && buf[n - 2] == ' ' && buf[n - 1] >= '0' && buf[n - 1] <= '9')
            buf[n - 2] = '\0';
        return;
    }

    snprintf(buf, len, "LA %u", ts->la);
}

/**
 * TETRA cell or call information, from line 1 to line 4 during a call,
 * on line 2 otherwise.
 */
static void _ui_drawTetraInfo(void)
{
    struct tetraStatus ts;
    tetra_getStatus(&ts);

    if (_ui_tetraCallShown(&ts)) {
        /*
         * Four lines of call information: the last one would touch the
         * meter below with letters such as y and p, so move them up a bit
         */
        point_t l1 = layout.line1_pos, l2 = layout.line2_pos;
        point_t l3 = layout.line3_pos, l4 = layout.line4_pos;
#if CONFIG_SCREEN_HEIGHT > 127
        l1.y -= 3;
        l2.y -= 3;
        l3.y -= 3;
        l4.y -= 3;
#endif

        // Talking party
        gfx_drawSymbol(l1, layout.line1_symbol_size,
                       TEXT_ALIGN_LEFT, color_white, SYMBOL_CALL_MADE);
        _ui_printSsi(l1, layout.line2_font, "", ts.talker, color_white);

        // Called group or individual
        gfx_drawSymbol(l2, layout.line2_symbol_size, TEXT_ALIGN_LEFT,
                       _ui_tetraColor(tetraGroup), SYMBOL_CALL_RECEIVED);
        _ui_printSsi(l2, layout.line2_font,
                     ts.callGroup ? "G " : "", ts.callDst, tetraGroup);

        // Cell, or the traffic carrier the call was followed to
        gfx_drawSymbol(l3, layout.line3_symbol_size, TEXT_ALIGN_LEFT,
                       _ui_tetraColor(tetraPlace), SYMBOL_TRANSMISSION_TOWER);
        if (ts.followHz != 0)
            gfx_print(l3, layout.line3_font, TEXT_ALIGN_CENTER,
                      _ui_tetraColor(tetraPlace), "Ch %lu.%05lu",
                      (unsigned long)(ts.followHz / 1000000),
                      (unsigned long)(ts.followHz % 1000000 / 10));
        else {
            char where[CPS_STR_SIZE];
            _ui_tetraLocation(&ts, where, sizeof(where));
            gfx_print(l3, layout.line3_font, TEXT_ALIGN_CENTER,
                      _ui_tetraColor(tetraPlace), "%s", where);
        }

        // Call state
        if (ts.callEncrypted) {
            gfx_drawSymbol(l4, layout.line4_symbol_size, TEXT_ALIGN_LEFT,
                           _ui_tetraColor(tetraWarn), SYMBOL_LOCK);
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER,
                      _ui_tetraColor(tetraWarn), "Encrypted");
        } else if (ts.voice) {
            // Length of the current transmission
            long long talk = (getTick() - ts.txStart) / 1000;
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER,
                      _ui_tetraColor(tetraVoice), "Voice %lld:%02lld",
                      talk / 60, talk % 60);
        } else {
            gfx_print(l4, layout.line4_font, TEXT_ALIGN_CENTER,
                      _ui_tetraColor(tetraMuted), "Idle");
        }
        return;
    }

    if (!ts.sync) {
        if (ts.searching)
            gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                      tetraWarn, "T scanning");
        else
            gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                      tetraMuted, "T searching");
        return;
    }

    // Just moved to another cell: say where, highlighted
    bool moved = (ts.cellChanged != 0)
              && (getTick() - ts.cellChanged < TETRA_CELL_SHOW_MS);
    color_t place = moved ? tetraVoice : tetraPlace;

    gfx_drawSymbol(layout.line2_pos, layout.line2_symbol_size,
                   TEXT_ALIGN_LEFT, place, SYMBOL_TRANSMISSION_TOWER);
    if (ts.sysinfo) {
        char where[CPS_STR_SIZE];
        _ui_tetraLocation(&ts, where, sizeof(where));
        if (moved)
            gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                      place, "> %s", where);
        else
            _ui_print2(layout.line2_pos, layout.line2_font, "T ", color_white,
                       where, place);
        if (ts.airEncryption)
            gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_RIGHT,
                      tetraWarn, "enc");
    } else {
        gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                  color_white, "T %u-%u CC %u", ts.mcc, ts.mnc,
                  ts.colourCode);
    }
}

/**
 * TETRA line 1 in VFO mode, when no call is shown: the last SDS (scrolling)
 * or the demodulator quality.
 */
static void _ui_drawTetraLine1(ui_state_t *ui_state)
{
    struct tetraStatus ts;
    tetra_getStatus(&ts);

    if (_ui_tetraCallShown(&ts))
        return;

    long long now = getTick();
    if (ts.sdsSrc != 0 && now - ts.sdsTime < TETRA_SDS_SHOW_MS) {
        char text[96];
        char msg[14];
        const char *sender = tetraIds_name(ts.sdsSrc);
        if (sender != NULL)
            snprintf(text, sizeof(text), "%s: %s   ", sender, ts.sds);
        else
            snprintf(text, sizeof(text), "%lu: %s   ",
                     (unsigned long)ts.sdsSrc, ts.sds);
        scrollTextPeek(text, msg, sizeof(msg),
                       ui_state->m17_meta_text_scroll_position);
        if (now - ui_state->m17_meta_text_last_scroll_tick >= 100) {
            scrollTextAdvance(text, sizeof(msg),
                              &ui_state->m17_meta_text_scroll_position);
            ui_state->m17_meta_text_last_scroll_tick = now;
        }
        gfx_print(layout.line1_pos, layout.line1_font, TEXT_ALIGN_CENTER,
                  tetraText, "%s", msg);
        return;
    }

    ui_state->m17_meta_text_scroll_position = 0;

    // The last call heard, unless switched to the signal figures
    if (!tetraShowSignal && ts.recent.count > 0) {
        const struct tetraCallEntry *e = &ts.recent.entry[0];
        char name[24], age[24];
        if (e->dst != 0)
            _ui_tetraSsiName(name, sizeof(name), e->dst, e->group);
        else
            _ui_tetraSsiName(name, sizeof(name), e->src, false);
        char suffix[28], text[40];
        _ui_tetraAge(age, sizeof(age), now - e->time);
        snprintf(suffix, sizeof(suffix), " %s", age);
        _ui_tetraFit(text, sizeof(text), layout.line1_font,
                     CONFIG_SCREEN_WIDTH - 2 * layout.horizontal_pad, "", name,
                     suffix);
        // The name in the talkgroup colour, the age in grey
        text[strlen(text) - strlen(suffix)] = '\0';
        _ui_print2(layout.line1_pos, layout.line1_font, text, tetraGroup,
                   suffix, tetraMuted);
        return;
    }

    // Phase error, carrier offset, lost sample blocks, resyncs
    gfx_print(layout.line1_pos, layout.line1_font, TEXT_ALIGN_CENTER,
              tetraMuted, "e%u%% %+dHz o%u r%u", ts.phaseErr, ts.carrierHz,
              ts.overruns, ts.resyncs);
}

/* In ui_menu.c */
extern void _ui_drawMenuList(uint8_t selected,
                             int (*getCurrentEntry)(char *buf, uint8_t max_len,
                                                    uint8_t index));

static int _ui_getTetraRecentEntry(char *buf, uint8_t max_len, uint8_t index)
{
    struct tetraStatus ts;
    tetra_getStatus(&ts);

    if (index >= ts.recent.count)
        return -1;

    const struct tetraCallEntry *e = &ts.recent.entry[index];
    char name[24], age[24];
    if (e->dst != 0)
        _ui_tetraSsiName(name, sizeof(name), e->dst, e->group);
    else
        _ui_tetraSsiName(name, sizeof(name), e->src, false);
    char suffix[28];
    _ui_tetraAge(age, sizeof(age), getTick() - e->time);
    snprintf(suffix, sizeof(suffix), " %s", age);
    _ui_tetraFit(buf, max_len, layout.menu_font, CONFIG_SCREEN_WIDTH - 4, "",
                 name, suffix);
    return 0;
}

/**
 * Recent calls, newest first.
 */
void _ui_drawTetraRecent(ui_state_t *ui_state)
{
    struct tetraStatus ts;
    tetra_getStatus(&ts);

    gfx_clearScreen();
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, color_white,
              "Recent calls");
    if (ts.recent.count == 0)
        gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                  color_white, "None yet");
    else
        _ui_drawMenuList(ui_state->menu_selected, _ui_getTetraRecentEntry);
}

/**
 * @return the identity to hold: the shown call's talkgroup, or its radio
 * for a call between radios, else the last call heard. 0 if none.
 */
uint32_t _ui_tetraHoldCandidate(void)
{
    struct tetraStatus ts;
    tetra_getStatus(&ts);

    if (_ui_tetraCallShown(&ts)) {
        if (ts.callDst != 0 && (ts.callGroup || tetraIds_name(ts.callDst)))
            return ts.callDst;
        return ts.talker != 0 ? ts.talker : ts.callDst;
    }

    if (ts.recent.count > 0) {
        const struct tetraCallEntry *e = &ts.recent.entry[0];
        return e->dst != 0 ? e->dst : e->src;
    }

    return 0;
}

/**
 * @return the identity of a recent call, as for _ui_tetraHoldCandidate().
 */
uint32_t _ui_tetraRecentSsi(uint8_t index)
{
    struct tetraStatus ts;
    tetra_getStatus(&ts);

    if (index >= ts.recent.count)
        return 0;
    const struct tetraCallEntry *e = &ts.recent.entry[index];
    return e->dst != 0 ? e->dst : e->src;
}

/**
 * @return true while a TETRA call is shown: the screen stays on.
 */
bool _ui_tetraCallActive(void)
{
    struct tetraStatus ts;

    if (last_state.channel.mode != OPMODE_TETRA)
        return false;
    tetra_getStatus(&ts);
    return _ui_tetraCallShown(&ts);
}

/**
 * One recent call in detail: who was called, who talked last, how often
 * and when it was heard.
 */
void _ui_drawTetraRecentDetail(ui_state_t *ui_state)
{
    struct tetraStatus ts;
    char name[24], text[40], age[24];
    long long now = getTick();

    tetra_getStatus(&ts);
    gfx_clearScreen();
    if (ui_state->menu_selected >= ts.recent.count) {
        gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                  color_white, "Gone");
        return;
    }
    const struct tetraCallEntry *e = &ts.recent.entry[ui_state->menu_selected];

    // Top bar: what the call was
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, tetraMuted,
              "%s", e->dst == 0 ? "Call" : e->group ? "Group call"
                                                     : "Radio call");

    // Called talkgroup or radio
    gfx_drawSymbol(layout.line1_pos, layout.line1_symbol_size,
                   TEXT_ALIGN_LEFT, tetraGroup, SYMBOL_CALL_RECEIVED);
    if (e->dst != 0) {
        _ui_tetraSsiName(name, sizeof(name), e->dst, e->group);
        _ui_tetraFit(text, sizeof(text), layout.line1_font,
                     CONFIG_SCREEN_WIDTH - 2 * layout.line1_symbol_size - 8,
                     "", name, "");
    } else {
        snprintf(text, sizeof(text), "-");
    }
    gfx_print(layout.line1_pos, layout.line1_font, TEXT_ALIGN_CENTER,
              tetraGroup, "%s", text);

    // Last talker
    gfx_drawSymbol(layout.line2_pos, layout.line2_symbol_size,
                   TEXT_ALIGN_LEFT, color_white, SYMBOL_CALL_MADE);
    if (e->src != 0)
        _ui_tetraSsiName(name, sizeof(name), e->src, false);
    else
        snprintf(name, sizeof(name), "-");
    gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
              color_white, "%s", name);

    // How often, and when
    _ui_tetraAge(age, sizeof(age), now - e->time);
    if (e->heard != 0)
        gfx_print(layout.line3_pos, layout.line3_font, TEXT_ALIGN_CENTER,
                  tetraVoice, "Heard %ux, %s ago", e->heard, age);
    else
        gfx_print(layout.line3_pos, layout.line3_font, TEXT_ALIGN_CENTER,
                  e->encrypted ? tetraWarn : tetraMuted, "%s, %s ago",
                  e->encrypted ? "Encrypted" : "No voice", age);
    _ui_tetraAge(age, sizeof(age), now - e->first);
    gfx_print(layout.line4_pos, layout.line4_font, TEXT_ALIGN_CENTER,
              tetraMuted, "First %s ago", age);

    // What the keys do here
    point_t hint = { 0, CONFIG_SCREEN_HEIGHT - 3 };
    gfx_print(hint, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, tetraText,
              "Enter: hold   ESC: back");
}

/**
 * During a call to or from an alarm talkgroup, paint the screen red.
 */
static void _ui_drawTetraAlarm(void)
{
    static const color_t alarmRed = { 170, 0, 0, 255 };
    struct tetraStatus ts;

    tetraAlarmScreen = false;
    if (last_state.channel.mode != OPMODE_TETRA)
        return;
    tetra_getStatus(&ts);
    tetraAlarmScreen = _ui_tetraCallShown(&ts) && ts.callAlarm;
    if (tetraAlarmScreen) {
        point_t origin = { 0, 0 };
        gfx_drawRect(origin, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT,
                     alarmRed, true);
    }
}
#endif

/**
 * Check whether the current mode shows a call instead of the frequency.
 */
static bool _ui_callShown(void)
{
#ifdef CONFIG_M17
    rtxStatus_t status = rtx_getCurrentStatus();
    if ((status.opMode == OPMODE_M17) && status.lsfOk)
        return true;
#endif
#ifdef CONFIG_TETRA
    if (last_state.channel.mode == OPMODE_TETRA) {
        struct tetraStatus ts;
        tetra_getStatus(&ts);
        return _ui_tetraCallShown(&ts);
    }
#endif
    return false;
}

void _ui_drawMainBackground()
{
    // Print top bar line of hline_h pixel height
    gfx_drawHLine(layout.top_h, layout.hline_h, color_grey);
    // Print bottom bar line of 1 pixel height
    gfx_drawHLine(CONFIG_SCREEN_HEIGHT - layout.bottom_h - 1, layout.hline_h, color_grey);
}

void _ui_drawMainTop(ui_state_t * ui_state)
{
#ifdef CONFIG_RTC
    // Print clock on top bar
    datetime_t local_time = utcToLocalTime(last_state.time,
                                           last_state.settings.utc_timezone);
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER,
              color_white, "%02d:%02d:%02d", local_time.hour,
              local_time.minute, local_time.second);
#endif
#ifdef CONFIG_TETRA
    // Talkgroup filter, if the built-in list defines any: yellow while it
    // holds calls back
    if ((last_state.channel.mode == OPMODE_TETRA)
        && (tetraIds_filterCount() > 1) && (tetraIds_held() == 0)) {
        // Small font, as the battery percentage, to leave room for it
        size_t filter = tetraIds_selectedFilter();
        _ui_tetraTopText("TG ", tetraIds_filterName(filter),
                         (filter != 0) ? yellow_fab413 : color_white);
    }

    // A held talkgroup, in place of the filter
    if ((last_state.channel.mode == OPMODE_TETRA) && (tetraIds_held() != 0)) {
        char name[24];
        _ui_tetraSsiName(name, sizeof(name), tetraIds_held(), false);
        _ui_tetraTopText("Hold ", name, yellow_fab413);
    }
#endif
    // If the radio has no built-in battery, print input voltage
#ifdef CONFIG_BAT_NONE
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_RIGHT,
              color_white,"%u.%uV", last_state.v_bat/1000, (last_state.v_bat % 1000) /100);
#else
    if(last_state.settings.showBatteryIcon) {
        // print battery icon on top bar, use 4 px padding
        uint16_t bat_width = CONFIG_SCREEN_WIDTH / 9;
        uint16_t bat_height = layout.top_h - (layout.status_v_pad * 2);
        point_t bat_pos = {CONFIG_SCREEN_WIDTH - bat_width - layout.horizontal_pad,
                        layout.status_v_pad};
        gfx_drawBattery(bat_pos, bat_width, bat_height, last_state.charge);
    } else {
        // print the battery percentage
        point_t bat_pos = {layout.top_pos.x, layout.top_pos.y - 2};
        gfx_print(bat_pos , FONT_SIZE_6PT, TEXT_ALIGN_RIGHT,
        color_white,"%d%%", last_state.charge);
    }
#endif
    if (ui_state->input_locked == true)
      gfx_drawSymbol(layout.top_pos, layout.top_symbol_size, TEXT_ALIGN_LEFT,
                     color_white, SYMBOL_LOCK);
}

void _ui_drawBankChannel()
{
    // Print Bank number, channel number and Channel name
    uint16_t b = (last_state.bank_enabled) ? last_state.bank : 0;
    gfx_print(layout.line1_pos, layout.line1_font, TEXT_ALIGN_CENTER,
              color_white, "%01d-%03d: %.12s",
              b, last_state.channel_index + 1, last_state.channel.name);
}

const char* _ui_getToneEnabledString(bool tone_tx_enable, bool tone_rx_enable,
        bool use_abbreviation)
{
    const char *strings[2][4] = {
        {
            currentLanguage->None,
            currentLanguage->Encode,
            currentLanguage->Decode,
            currentLanguage->Both,
        }, {
            "N",
            "T",
            "R",
            "B"
        }
    };


    uint8_t index = (tone_rx_enable << 1) | (tone_tx_enable);
    return strings[use_abbreviation][index];
}

void _ui_drawModeInfo(ui_state_t* ui_state)
{
    char bw_str[8] = { 0 };

    switch(last_state.channel.mode)
    {
        case OPMODE_FM:

            // Get Bandwidth string
            if(last_state.channel.bandwidth == BW_12_5)
                sniprintf(bw_str, 8, "NFM");
            else if(last_state.channel.bandwidth == BW_25)
                sniprintf(bw_str, 8, "FM");

            // Get encdec string
            bool tone_tx_enable = last_state.channel.fm.txToneEn;
            bool tone_rx_enable = last_state.channel.fm.rxToneEn;
            // Print Bandwidth, Tone and encdec info
            if (tone_tx_enable || tone_rx_enable)
            {
                uint16_t tone = ctcss_tone[last_state.channel.fm.txTone];
                gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                          color_white, "%s %d.%d %s", bw_str, (tone / 10),
                          (tone % 10), _ui_getToneEnabledString(tone_tx_enable, tone_rx_enable, true));
            }
            else
            {
                gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                          color_white, "%s", bw_str );
            }
            break;

        case OPMODE_DMR:
            // Print talkgroup
            gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                    color_white, "DMR TG%s", "");
            break;

        #ifdef CONFIG_M17
        case OPMODE_M17:
        {
            // Print M17 Destination ID on line 3 of 3
            rtxStatus_t rtxStatus = rtx_getCurrentStatus();

            if(rtxStatus.lsfOk)
            {
                // Destination address
                gfx_drawSymbol(layout.line2_pos, layout.line2_symbol_size, TEXT_ALIGN_LEFT,
                               color_white, SYMBOL_CALL_RECEIVED);

                gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                          color_white, "%s", rtxStatus.M17_dst);

                // Source address
                gfx_drawSymbol(layout.line1_pos, layout.line1_symbol_size, TEXT_ALIGN_LEFT,
                               color_white, SYMBOL_CALL_MADE);

                gfx_print(layout.line1_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                          color_white, "%s", rtxStatus.M17_src);

                // RF link (if present)
                if(rtxStatus.M17_link[0] != '\0')
                {
                    gfx_drawSymbol(layout.line4_pos, layout.line3_symbol_size, TEXT_ALIGN_LEFT,
                                   color_white, SYMBOL_ACCESS_POINT);

                    gfx_print(layout.line4_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                              color_white, "%s", rtxStatus.M17_link);
                }

                // Meta text (if present)
                if(rtxStatus.M17_meta_text[0] != '\0')
                {
                    char msg[14];
                    
                    // Always display current position
                    scrollTextPeek(rtxStatus.M17_meta_text, msg, sizeof(msg),
                                   ui_state->m17_meta_text_scroll_position);
                    // Only advance scroll every 100ms
                    long long now = getTick();
                    if(now - ui_state->m17_meta_text_last_scroll_tick >= 100)
                    {
                        scrollTextAdvance(rtxStatus.M17_meta_text, sizeof(msg),
                                          &ui_state->m17_meta_text_scroll_position);
                        ui_state->m17_meta_text_last_scroll_tick = now;
                    }
                    gfx_print(layout.line3_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                              color_white, "%s", msg);
                }
                // Reflector (if present)
                else if(rtxStatus.M17_refl[0] != '\0')
                {                    
                    gfx_drawSymbol(layout.line3_pos, layout.line4_symbol_size, TEXT_ALIGN_LEFT,
                                   color_white, SYMBOL_NETWORK);

                    gfx_print(layout.line3_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                              color_white, "%s", rtxStatus.M17_refl);
                }
                
                // Reset scroll position when meta text becomes empty
                if(rtxStatus.M17_meta_text[0] == '\0')
                {
                    ui_state->m17_meta_text_scroll_position = 0;
                }
            }
            else
            {
                const char *dst = NULL;
                if(ui_state->edit_mode)
                {
                    dst = ui_state->new_callsign;
                }
                else
                {
                    if(strnlen(rtxStatus.destination_address, 10) == 0)
                        dst = currentLanguage->broadcast;
                    else
                        dst = rtxStatus.destination_address;
                }

                gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                          color_white, "M17 #%s", dst);
            }
            break;
        }
        #endif

        #ifdef CONFIG_TETRA
        case OPMODE_TETRA:
            (void) ui_state;
            _ui_drawTetraInfo();
            break;
        #endif
    }
}

void _ui_drawFrequency()
{
    freq_t freq = platform_getPttStatus() ? last_state.channel.tx_frequency
                                          : last_state.channel.rx_frequency;
#ifdef CONFIG_TETRA
    // The cell the radio moved to, rather than the one it was set to
    if (last_state.channel.mode == OPMODE_TETRA) {
        struct tetraStatus ts;
        tetra_getStatus(&ts);
        if (ts.tunedHz != 0)
            freq = ts.tunedHz;
    }
#endif

    char freq_str[16] = {0};
    sniprintf(freq_str, sizeof(freq_str), "%03lu.%05lu",
              (freq / 1000000lu), (freq % 1000000lu) / 10);

    size_t len = strlen(freq_str);
    char main_str[16] = {0};
    char small_str[3] = {0};
    strncpy(main_str, freq_str, len - 2);
    strncpy(small_str, freq_str + len - 2, 2);

    fontSize_t small_font = FONT_SIZE_5PT;
    if (layout.line3_large_font > FONT_SIZE_6PT)
    {
        small_font = (fontSize_t)(layout.line3_large_font - 2);
    }

    uint16_t main_width = gfx_getTextWidth(layout.line3_large_font, main_str);
    uint16_t small_width = gfx_getTextWidth(small_font, small_str);
    uint16_t total_width = main_width + small_width;
    int16_t start_x = (CONFIG_SCREEN_WIDTH - total_width) / 2;

    point_t main_pos = { (uint16_t)start_x, layout.line3_large_pos.y };
    gfx_print(main_pos, layout.line3_large_font, TEXT_ALIGN_LEFT,
              color_white, "%s", main_str);

    point_t small_pos = { (uint16_t)(start_x + main_width), layout.line3_large_pos.y };
    gfx_print(small_pos, small_font, TEXT_ALIGN_LEFT,
              color_white, "%s", small_str);
}

void _ui_drawVFOMiddleInput(ui_state_t* ui_state)
{
    // Add inserted number to string, skipping "Rx: "/"Tx: " and "."
    uint8_t insert_pos = ui_state->input_position + 3;
    if(ui_state->input_position > 3) insert_pos += 1;
    char input_char = ui_state->input_number + '0';

    if(ui_state->input_set == SET_RX)
    {
        if(ui_state->input_position == 0)
        {
            gfx_print(layout.line2_pos, layout.input_font, TEXT_ALIGN_CENTER,
                      color_white, ">Rx:%03lu.%04lu",
                      (unsigned long)ui_state->new_rx_frequency/1000000,
                      (unsigned long)(ui_state->new_rx_frequency%1000000)/100);
        }
        else
        {
            // Replace Rx frequency with underscorses
            if(ui_state->input_position == 1)
                strcpy(ui_state->new_rx_freq_buf, ">Rx:___.____");
            ui_state->new_rx_freq_buf[insert_pos] = input_char;
            gfx_print(layout.line2_pos, layout.input_font, TEXT_ALIGN_CENTER,
                      color_white, ui_state->new_rx_freq_buf);
        }
        gfx_print(layout.line3_large_pos, layout.input_font, TEXT_ALIGN_CENTER,
                  color_white, " Tx:%03lu.%04lu",
                  (unsigned long)last_state.channel.tx_frequency/1000000,
                  (unsigned long)(last_state.channel.tx_frequency%1000000)/100);
    }
    else if(ui_state->input_set == SET_TX)
    {
        gfx_print(layout.line2_pos, layout.input_font, TEXT_ALIGN_CENTER,
                  color_white, " Rx:%03lu.%04lu",
                  (unsigned long)ui_state->new_rx_frequency/1000000,
                  (unsigned long)(ui_state->new_rx_frequency%1000000)/100);
        // Replace Rx frequency with underscorses
        if(ui_state->input_position == 0)
        {
            gfx_print(layout.line3_large_pos, layout.input_font, TEXT_ALIGN_CENTER,
                      color_white, ">Tx:%03lu.%04lu",
                      (unsigned long)ui_state->new_rx_frequency/1000000,
                      (unsigned long)(ui_state->new_rx_frequency%1000000)/100);
        }
        else
        {
            if(ui_state->input_position == 1)
                strcpy(ui_state->new_tx_freq_buf, ">Tx:___.____");
            ui_state->new_tx_freq_buf[insert_pos] = input_char;
            gfx_print(layout.line3_large_pos, layout.input_font, TEXT_ALIGN_CENTER,
                      color_white, ui_state->new_tx_freq_buf);
        }
    }
}

void _ui_drawMainBottom()
{
    // Squelch bar
    rssi_t   rssi = last_state.rssi;
    uint8_t  squelch = last_state.settings.sqlLevel;
    uint8_t  volume = last_state.volume;
    uint16_t meter_width = CONFIG_SCREEN_WIDTH - 2 * layout.horizontal_pad;
    uint16_t meter_height = layout.bottom_h;
    point_t meter_pos = { layout.horizontal_pad,
                          CONFIG_SCREEN_HEIGHT - meter_height - layout.bottom_pad};
    uint8_t mic_level = platform_getMicLevel();
    switch(last_state.channel.mode)
    {
        case OPMODE_FM:
            gfx_drawSmeter(meter_pos,
                           meter_width,
                           meter_height,
                           rssi,
                           squelch,
                           volume,
                           true,
                           yellow_fab413);
            break;
        case OPMODE_DMR:
            gfx_drawSmeterLevel(meter_pos,
                                meter_width,
                                meter_height,
                                rssi,
                                mic_level,
                                volume,
                                true);
            break;
        #ifdef CONFIG_M17
        case OPMODE_M17:
            gfx_drawSmeterLevel(meter_pos,
                                meter_width,
                                meter_height,
                                rssi,
                                mic_level,
                                volume,
                                true);
            break;
        #endif
        #ifdef CONFIG_TETRA
        case OPMODE_TETRA:
        {
            // The level bar shows decoding quality: full at 10% phase
            // error or better, empty at 45%, where decoding fails
            struct tetraStatus ts;
            tetra_getStatus(&ts);
            uint8_t quality = 0;
            if (ts.sync && ts.phaseErr < 45)
                quality = (ts.phaseErr <= 10) ? 255
                        : (uint8_t)((45 - ts.phaseErr) * 255 / 35);
            // Traffic light: each bar green when good, yellow when fair,
            // red when poor
            smeterColors_t colors = {
                .volume = tetraMuted,
                .level = _ui_tetraTraffic(quality, 170, 90),
                .rssi = _ui_tetraTraffic(rssiToSlevel(rssi), 7, 4),
                .marks = color_white,
                .majorMark = yellow_fab413,
                .overMark = { 255, 0, 0, 255 },
            };
            gfx_drawSmeterLevelColors(meter_pos,
                                      meter_width,
                                      meter_height,
                                      rssi,
                                      quality,
                                      volume,
                                      true,
                                      &colors);
            (void) mic_level;
            break;
        }
        #endif
    }
}

void _ui_drawMainVFO(ui_state_t* ui_state)
{
    gfx_clearScreen();
    #ifdef CONFIG_TETRA
    _ui_drawTetraAlarm();
    #endif
    _ui_drawMainTop(ui_state);
    _ui_drawModeInfo(ui_state);

    // Show VFO frequency unless the mode shows a call (M17 LSF, TETRA call)
    if(_ui_callShown() == false)
        _ui_drawFrequency();

    #ifdef CONFIG_TETRA
    if(last_state.channel.mode == OPMODE_TETRA)
        _ui_drawTetraLine1(ui_state);
    #endif

    _ui_drawMainBottom();
}

void _ui_drawMainVFOInput(ui_state_t* ui_state)
{
    gfx_clearScreen();
    _ui_drawMainTop(ui_state);
    _ui_drawVFOMiddleInput(ui_state);
    _ui_drawMainBottom();
}

void _ui_drawMainMEM(ui_state_t* ui_state)
{
    gfx_clearScreen();
    #ifdef CONFIG_TETRA
    _ui_drawTetraAlarm();
    #endif
    _ui_drawMainTop(ui_state);
    _ui_drawModeInfo(ui_state);

    // Show channel data unless the mode shows a call (M17 LSF, TETRA call)
    if(_ui_callShown() == false)
    {
        _ui_drawBankChannel();
        _ui_drawFrequency();
    }

    _ui_drawMainBottom();
}
