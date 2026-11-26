/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * APRS screens: Menu > APRS (stations heard, one in full, messages) and
 * Settings > APRS.
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "ui/ui_default.h"

#ifdef CONFIG_APRS

#include "core/graphics.h"
#include "interfaces/delays.h"
#include "protocols/APRS/Aprs.h"
#include "protocols/APRS/AprsStatus.h"
#include "ui/ui_strings.h"

static const color_t aprsMuted = { 150, 150, 150, 255 };
static const color_t aprsCall = { 90, 200, 255, 255 };  /* Sky blue */
static const color_t aprsToMe = { 255, 191, 0, 255 };   /* Amber */
static const color_t aprsText = { 255, 230, 140, 255 }; /* Light yellow */

/* Beacon intervals, as OpMode_FM.cpp has them */
static const char *const aprsIntervals[] = { "Manual", "10 min", "20 min",
                                             "30 min", "60 min" };
static const char *const aprsPaths[APRS_PATHS] = { "WIDE1,WIDE2", "WIDE2",
                                                   "Direct" };

/** Width of a text, a few letters at a time (see ui_main.c) */
static uint16_t _ui_aprsWidth(fontSize_t font, const char *text)
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

/** Text cut to a width */
static void _ui_aprsFit(char *out, size_t len, fontSize_t font, uint16_t width,
                        const char *text)
{
    int n = (int)strlen(text);

    for (;;) {
        snprintf(out, len, "%.*s", n, text);
        if (n == 0 || _ui_aprsWidth(font, out) <= width)
            return;
        n--;
    }
}

/** Time since: 45s, 12m, 3h */
static void _ui_aprsAge(char *buf, size_t len, long long ms)
{
    long long s = ms / 1000;

    if (s < 60)
        snprintf(buf, len, "%llds", s);
    else if (s < 3600)
        snprintf(buf, len, "%lldm", s / 60);
    else
        snprintf(buf, len, "%lldh", s / 3600);
}

/** Distance and direction from here: "12.3km NE", "" without positions */
static void _ui_aprsAway(char *buf, size_t len, const struct aprsStation *s)
{
    static const char *const compass[8] = { "N", "NE", "E", "SE",
                                            "S", "SW", "W", "NW" };
    uint32_t m;
    uint16_t b;

    buf[0] = '\0';
    if (!s->hasPos || !last_state.settings.aprsPosSet)
        return;
    aprs_distance(last_state.settings.aprsLat, last_state.settings.aprsLon,
                  s->lat, s->lon, &m, &b);
    const char *dir = compass[((b + 22) / 45) % 8];
    if (m < 1000)
        snprintf(buf, len, "%lum %s", (unsigned long)m, dir);
    else if (m < 100000)
        snprintf(buf, len, "%lu.%lukm %s", (unsigned long)(m / 1000),
                 (unsigned long)(m % 1000 / 100), dir);
    else
        snprintf(buf, len, "%lukm %s", (unsigned long)(m / 1000), dir);
}

/** A position: "43.65320N 79.38320W" */
static void _ui_aprsPos(char *buf, size_t len, int32_t lat, int32_t lon)
{
    uint32_t a = (uint32_t)(lat < 0 ? -(int64_t)lat : lat);
    uint32_t o = (uint32_t)(lon < 0 ? -(int64_t)lon : lon);

    snprintf(buf, len, "%lu.%05lu%c %lu.%05lu%c", (unsigned long)(a / 1000000),
             (unsigned long)(a % 1000000 / 10), lat < 0 ? 'S' : 'N',
             (unsigned long)(o / 1000000), (unsigned long)(o % 1000000 / 10),
             lon < 0 ? 'W' : 'E');
}

/** The base callsign, without SSID, for comparing */
static void _ui_aprsBase(char *out, size_t len, const char *call)
{
    size_t n = 0;

    while (call[n] != '\0' && call[n] != '-' && n + 1 < len) {
        out[n] = call[n];
        n++;
    }
    out[n] = '\0';
}

/** True if a message is to this radio's callsign */
static bool _ui_aprsToMe(const char *to)
{
    char me[10], them[10];

    if (last_state.settings.callsign[0] == '\0')
        return false;
    _ui_aprsBase(me, sizeof(me), last_state.settings.callsign);
    _ui_aprsBase(them, sizeof(them), to);
    return strcasecmp(me, them) == 0;
}

/** A selected row: a white bar */
static void _ui_aprsBar(point_t pos, uint16_t h)
{
    point_t bar = { 0, (uint16_t)(pos.y - layout.menu_h + 3) };
    gfx_drawRect(bar, CONFIG_SCREEN_WIDTH, h, color_white, true);
}

void _ui_drawAprsList(ui_state_t *ui_state)
{
    const uint16_t rowH = layout.menu_h + 9;
    const uint16_t width = CONFIG_SCREEN_WIDTH - 2 * layout.horizontal_pad;
    uint8_t count = aprsStatus_stationCount();
    uint8_t msgs = aprsStatus_messageCount();
    long long now = getTick();

    gfx_clearScreen();
    if (msgs > 0)
        gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER,
                  color_white, "APRS  # %u msg", msgs);
    else
        gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER,
                  color_white, "APRS");

    if (count == 0) {
        gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                  color_white, "Nothing heard");
        gfx_print(layout.line3_pos, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, aprsMuted,
                  last_state.settings.aprsRxOff ? "Decode is off" :
                                                  "FM on 144.390 MHz");
        return;
    }

    uint16_t rows = (CONFIG_SCREEN_HEIGHT - layout.line1_pos.y + layout.menu_h)
                  / rowH;
    if (rows == 0)
        rows = 1;
    if (ui_state->aprs_sel >= count)
        ui_state->aprs_sel = count - 1;
    uint8_t sel = ui_state->aprs_sel;
    uint8_t first = (sel >= rows) ? (uint8_t)(sel - rows + 1) : 0;
    point_t pos = layout.line1_pos;

    for (uint8_t n = first; n < count && n < first + rows; n++) {
        struct aprsStation s;
        char right[20], line[48], fitted[48];
        if (!aprsStatus_station(n, &s))
            break;
        bool selected = n == sel;
        color_t ink = selected ? color_black : aprsCall;
        color_t small = selected ? color_black : aprsMuted;

        if (selected)
            _ui_aprsBar(pos, rowH);

        // The call, and how far or how long ago
        _ui_aprsAway(right, sizeof(right), &s);
        if (right[0] == '\0')
            _ui_aprsAge(right, sizeof(right), now - s.last);
        point_t at = { layout.horizontal_pad, pos.y };
        gfx_print(at, layout.menu_font, TEXT_ALIGN_RIGHT, small, "%s", right);
        uint16_t room = width - 6 - _ui_aprsWidth(layout.menu_font, right);
        _ui_aprsFit(fitted, sizeof(fitted), layout.menu_font, room, s.call);
        gfx_print(at, layout.menu_font, TEXT_ALIGN_LEFT, ink, "%s", fitted);

        // What it said, else what it is
        if (s.text[0] != '\0')
            snprintf(line, sizeof(line), "%s", s.text);
        else
            snprintf(line, sizeof(line), "%s",
                     aprs_symbolName(s.symTable, s.symCode));
        _ui_aprsFit(fitted, sizeof(fitted), FONT_SIZE_6PT, width, line);
        point_t sub = { layout.horizontal_pad, (uint16_t)(pos.y + 9) };
        gfx_print(sub, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, small, "%s", fitted);
        pos.y += rowH;
    }
}

/** Print text wrapped at spaces over lines of a width, from a line on */
static uint16_t _ui_aprsWrap(point_t pos, uint16_t width, uint16_t bottom,
                             const char *text, color_t color)
{
    char line[48];
    size_t len = strlen(text);
    size_t start = 0;

    while (start < len && pos.y <= bottom) {
        size_t end = start, lastSpace = 0;
        while (end < len) {
            snprintf(line, sizeof(line), "%.*s", (int)(end - start + 1),
                     text + start);
            if (_ui_aprsWidth(FONT_SIZE_6PT, line) > width)
                break;
            if (text[end] == ' ')
                lastSpace = end;
            end++;
        }
        if (end < len && lastSpace > start)
            end = lastSpace;
        if (end == start)
            end = start + 1;
        snprintf(line, sizeof(line), "%.*s", (int)(end - start), text + start);
        gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, color, "%s", line);
        pos.y += 9;
        start = end;
        while (start < len && text[start] == ' ')
            start++;
    }
    return pos.y;
}

void _ui_drawAprsDetail(ui_state_t *ui_state)
{
    struct aprsStation s;
    char line[48], age[12];
    const uint16_t width = CONFIG_SCREEN_WIDTH - 2 * layout.horizontal_pad;
    long long now = getTick();

    gfx_clearScreen();
    if (!aprsStatus_station(ui_state->aprs_sel, &s)) {
        gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                  color_white, "Gone");
        return;
    }

    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, aprsCall,
              "%s", s.call);
    point_t pos = { layout.horizontal_pad, (uint16_t)(layout.line1_pos.y - 4) };

    // What it is, and how it came
    const char *what = aprs_symbolName(s.symTable, s.symCode);
    if (s.type == APRS_OBJECT)
        snprintf(line, sizeof(line), "Object by %s", s.by);
    else
        snprintf(line, sizeof(line), "%s%s%s", what, what[0] ? ", " : "",
                 s.via[0] ? "via " : "direct");
    if (s.type != APRS_OBJECT && s.via[0])
        strncat(line, s.via, sizeof(line) - strlen(line) - 1);
    gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, aprsMuted, "%s", line);
    pos.y += 10;

    if (s.hasPos) {
        _ui_aprsPos(line, sizeof(line), s.lat, s.lon);
        gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, color_white, "%s", line);
        pos.y += 10;
        char away[20];
        _ui_aprsAway(away, sizeof(away), &s);
        line[0] = '\0';
        if (away[0])
            snprintf(line, sizeof(line), "%s", away);
        if (s.speed > 0) {
            char move[24];
            snprintf(move, sizeof(move), "%s%d km/h", line[0] ? ", " : "",
                     (int)(s.speed * 1852 / 1000));
            strncat(line, move, sizeof(line) - strlen(line) - 1);
            if (s.course != APRS_NO_COURSE) {
                snprintf(move, sizeof(move), " %d deg", s.course);
                strncat(line, move, sizeof(line) - strlen(line) - 1);
            }
        }
        if (line[0]) {
            gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, color_white, "%s",
                      line);
            pos.y += 10;
        }
    }

    _ui_aprsAge(age, sizeof(age), now - s.last);
    snprintf(line, sizeof(line), "Heard %u time%s, %s ago", s.heard,
             s.heard == 1 ? "" : "s", age);
    gfx_print(pos, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, aprsMuted, "%s", line);
    pos.y += 12;

    if (s.text[0])
        _ui_aprsWrap(pos, width, CONFIG_SCREEN_HEIGHT - 2, s.text, aprsText);
}

void _ui_drawAprsMessages(ui_state_t *ui_state)
{
    const uint16_t rowH = layout.menu_h + 9;
    const uint16_t width = CONFIG_SCREEN_WIDTH - 2 * layout.horizontal_pad;
    uint8_t count = aprsStatus_messageCount();
    long long now = getTick();

    gfx_clearScreen();
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, color_white,
              "APRS messages");
    if (count == 0) {
        gfx_print(layout.line2_pos, layout.line2_font, TEXT_ALIGN_CENTER,
                  color_white, "None yet");
        return;
    }

    uint16_t rows = (CONFIG_SCREEN_HEIGHT - layout.line1_pos.y + layout.menu_h)
                  / rowH;
    if (rows == 0)
        rows = 1;
    if (ui_state->aprs_msg_sel >= count)
        ui_state->aprs_msg_sel = count - 1;
    uint8_t sel = ui_state->aprs_msg_sel;
    uint8_t first = (sel >= rows) ? (uint8_t)(sel - rows + 1) : 0;
    point_t pos = layout.line1_pos;

    for (uint8_t n = first; n < count && n < first + rows; n++) {
        struct aprsMessage m;
        char head[32], age[12], fitted[48];
        if (!aprsStatus_message(n, &m))
            break;
        bool selected = n == sel;
        bool toMe = _ui_aprsToMe(m.to);
        color_t ink = selected ? color_black : (toMe ? aprsToMe : aprsCall);
        color_t small = selected ? color_black : aprsText;

        if (selected)
            _ui_aprsBar(pos, rowH);

        _ui_aprsAge(age, sizeof(age), now - m.time);
        point_t at = { layout.horizontal_pad, pos.y };
        gfx_print(at, FONT_SIZE_6PT, TEXT_ALIGN_RIGHT,
                  selected ? color_black : aprsMuted, "%s", age);
        snprintf(head, sizeof(head), "%s>%s", m.from, m.to);
        _ui_aprsFit(fitted, sizeof(fitted), layout.menu_font,
                    width - 6 - _ui_aprsWidth(FONT_SIZE_6PT, age), head);
        gfx_print(at, layout.menu_font, TEXT_ALIGN_LEFT, ink, "%s", fitted);

        _ui_aprsFit(fitted, sizeof(fitted), FONT_SIZE_6PT, width, m.text);
        point_t sub = { layout.horizontal_pad, (uint16_t)(pos.y + 9) };
        gfx_print(sub, FONT_SIZE_6PT, TEXT_ALIGN_LEFT, small, "%s", fitted);
        pos.y += rowH;
    }
}

/* Settings > APRS */

/* ui_menu.c */
void _ui_drawMenuListValue(ui_state_t *ui_state, uint8_t selected,
                           int (*getCurrentEntry)(char *buf, uint8_t max_len,
                                                  uint8_t index),
                           int (*getCurrentValue)(char *buf, uint8_t max_len,
                                                  uint8_t index));

static ui_state_t *aprsSettingsState;

static int _ui_getAprsEntryName(char *buf, uint8_t max_len, uint8_t index)
{
    if (index >= settings_aprs_num)
        return -1;
    snprintf(buf, max_len, "%s", settings_aprs_items[index]);
    return 0;
}

/** A latitude or longitude: typed, set, or not set */
static void _ui_aprsCoord(char *buf, uint8_t max_len, bool lat)
{
    const ui_state_t *ui = aprsSettingsState;
    const settings_t *s = &last_state.settings;
    int degDigits = lat ? 2 : 3;
    char pos = lat ? 'N' : 'E', neg = lat ? 'S' : 'W';

    if (ui != NULL && ui->edit_mode
        && ui->menu_selected == (lat ? AP_LAT : AP_LON)) {
        // Being typed: the digits so far, the rest as -
        char d[10];
        size_t n = strlen(ui->aprs_digits);
        for (int i = 0; i < degDigits + 5; i++)
            d[i] = (i < (int)n) ? ui->aprs_digits[i] : '-';
        snprintf(buf, max_len, "%.*s.%.5s%c", degDigits, d, d + degDigits,
                 ui->aprs_neg ? neg : pos);
        return;
    }
    if (!s->aprsPosSet) {
        snprintf(buf, max_len, "Not set");
        return;
    }
    int32_t v = lat ? s->aprsLat : s->aprsLon;
    uint32_t a = (uint32_t)(v < 0 ? -(int64_t)v : v);
    snprintf(buf, max_len, "%lu.%05lu%c", (unsigned long)(a / 1000000),
             (unsigned long)(a % 1000000 / 10), v < 0 ? neg : pos);
}

static int _ui_getAprsValueName(char *buf, uint8_t max_len, uint8_t index)
{
    const settings_t *s = &last_state.settings;
    char t;
    char c;

    if (index >= settings_aprs_num)
        return -1;

    switch (index) {
        case AP_RX:
            snprintf(buf, max_len, "%s",
                     s->aprsRxOff ? currentLanguage->off : currentLanguage->on);
            break;
        case AP_BEACON:
            snprintf(buf, max_len, "%s", aprsIntervals[s->aprsBeacon % 5]);
            break;
        case AP_SEND: {
            long long last = aprsStatus_lastBeacon();
            if (!s->aprsPosSet)
                snprintf(buf, max_len, "Set position");
            else if (s->callsign[0] == '\0')
                snprintf(buf, max_len, "Set callsign");
            else if (aprsStatus_beaconRequested())
                snprintf(buf, max_len, "Waiting");
            else if (last != 0) {
                char age[12];
                _ui_aprsAge(age, sizeof(age), getTick() - last);
                snprintf(buf, max_len, "Sent %s ago", age);
            } else
                snprintf(buf, max_len, "Green");
            break;
        }
        case AP_LAT:
            _ui_aprsCoord(buf, max_len, true);
            break;
        case AP_LON:
            _ui_aprsCoord(buf, max_len, false);
            break;
        case AP_SYMBOL:
            snprintf(buf, max_len, "%s", aprs_symbol(s->aprsSymbol, &t, &c));
            break;
        case AP_PATH:
            snprintf(buf, max_len, "%s", aprsPaths[s->aprsPath % APRS_PATHS]);
            break;
        case AP_CALL:
            snprintf(buf, max_len, "%s",
                     s->callsign[0] ? s->callsign : "In M17");
            break;
        default:
            buf[0] = '\0';
            break;
    }
    return 0;
}

void _ui_drawSettingsAprs(ui_state_t *ui_state)
{
    aprsSettingsState = ui_state;
    gfx_clearScreen();
    gfx_print(layout.top_pos, layout.top_font, TEXT_ALIGN_CENTER, color_white,
              "APRS");
    _ui_drawMenuListValue(ui_state, ui_state->menu_selected,
                          _ui_getAprsEntryName, _ui_getAprsValueName);
}

#endif /* CONFIG_APRS */
