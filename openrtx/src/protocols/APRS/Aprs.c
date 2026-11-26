/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/APRS/Aprs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Destination of the frames sent: APZ is for experimental software */
#define TOCALL "APZRTX"

/** Copy text, printable characters only, cut to fit */
static void copyText(char *dst, size_t len, const uint8_t *src, size_t n)
{
    size_t o = 0;

    for (size_t i = 0; i < n && o + 1 < len; i++) {
        uint8_t c = src[i];
        if (c == '\r' || c == '\n' || c == '\0')
            break;
        dst[o++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    /* No trailing spaces */
    while (o > 0 && dst[o - 1] == ' ')
        o--;
    dst[o] = '\0';
}

static bool isDigit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

/** A digit of an uncompressed position; a space (ambiguity) is a zero */
static int digit(uint8_t c)
{
    return (c == ' ') ? 0 : (isDigit(c) ? c - '0' : -1);
}

/** Read "DDMM.hh" (lat, 2 degree digits) or "DDDMM.hh" (lon, 3) */
static bool readDegrees(const uint8_t *p, int degDigits, int32_t *micro)
{
    int deg = 0, min = 0, hund = 0;

    for (int i = 0; i < degDigits; i++) {
        int d = digit(p[i]);
        if (d < 0)
            return false;
        deg = deg * 10 + d;
    }
    p += degDigits;
    int m0 = digit(p[0]), m1 = digit(p[1]);
    int h0 = digit(p[3]), h1 = digit(p[4]);
    if (m0 < 0 || m1 < 0 || p[2] != '.' || h0 < 0 || h1 < 0)
        return false;
    min = m0 * 10 + m1;
    hund = h0 * 10 + h1;
    if (min >= 60)
        return false;
    *micro = deg * 1000000 + (int32_t)((min * 100 + hund) * 10000LL / 60);
    return true;
}

/** Base 91 number of 4 characters */
static bool base91(const uint8_t *p, uint32_t *out)
{
    uint32_t v = 0;

    for (int i = 0; i < 4; i++) {
        if (p[i] < 33 || p[i] > 33 + 90)
            return false;
        v = v * 91 + (uint32_t)(p[i] - 33);
    }
    *out = v;
    return true;
}

/**
 * Read a position, uncompressed or compressed, then a course and speed.
 *
 * @return characters read, 0 if there is none.
 */
static size_t readPosition(const uint8_t *p, size_t n, struct aprsPacket *out)
{
    if (n >= 19 && (isDigit(p[0]) || p[0] == ' ')) {
        int32_t lat, lon;
        if (!readDegrees(p, 2, &lat) || !readDegrees(p + 9, 3, &lon))
            return 0;
        if (p[7] == 'S')
            lat = -lat;
        else if (p[7] != 'N')
            return 0;
        if (p[17] == 'W')
            lon = -lon;
        else if (p[17] != 'E')
            return 0;
        out->lat = lat;
        out->lon = lon;
        out->symTable = (char)p[8];
        out->symCode = (char)p[18];
        out->hasPos = true;

        /* Course and speed: "088/036" */
        size_t used = 19;
        if (n >= 26 && p[22] == '/' && isDigit(p[19]) && isDigit(p[20])
            && isDigit(p[21]) && isDigit(p[23]) && isDigit(p[24])
            && isDigit(p[25])) {
            out->course = (int16_t)((p[19] - '0') * 100 + (p[20] - '0') * 10
                                    + (p[21] - '0'));
            out->speed = (int16_t)((p[23] - '0') * 100 + (p[24] - '0') * 10
                                   + (p[25] - '0'));
            used = 26;
        }
        return used;
    }

    if (n >= 13) {
        uint32_t y, x;
        if (!base91(p + 1, &y) || !base91(p + 5, &x))
            return 0;
        out->lat = (int32_t)llround((90.0 - y / 380926.0) * 1e6);
        out->lon = (int32_t)llround((-180.0 + x / 190463.0) * 1e6);
        out->symTable = (char)p[0];
        out->symCode = (char)p[9];
        out->hasPos = true;
        uint8_t c = p[10], s = p[11], t = p[12];
        if (c >= '!' && c <= 'z' && ((t - 33) & 0x18) != 0x10) {
            out->course = (int16_t)((c - 33) * 4);
            out->speed = (int16_t)lround(pow(1.08, s - 33) - 1.0);
        }
        return 13;
    }
    return 0;
}

/** Mic-E: the latitude in the destination, the rest in the information */
static bool readMicE(const struct ax25Frame *f, struct aprsPacket *out)
{
    const char *d = f->dst.call;
    const uint8_t *p = f->info;
    int digits[6];
    bool north = false, offset = false, west = false;

    if (strlen(d) != 6 || f->infoLen < 9)
        return false;
    for (int i = 0; i < 6; i++) {
        char c = d[i];
        bool high;
        if (c >= '0' && c <= '9') {
            digits[i] = c - '0';
            high = false;
        } else if (c >= 'A' && c <= 'J') {
            digits[i] = c - 'A';
            high = false;
        } else if (c >= 'P' && c <= 'Y') {
            digits[i] = c - 'P';
            high = true;
        } else if (c == 'K' || c == 'L' || c == 'Z') {
            digits[i] = 0;
            high = c == 'Z';
        } else {
            return false;
        }
        if (i == 3)
            north = high;
        else if (i == 4)
            offset = high;
        else if (i == 5)
            west = high;
    }
    int latMinH = (digits[2] * 10 + digits[3]) * 100 + digits[4] * 10
                + digits[5];
    int32_t lat = (digits[0] * 10 + digits[1]) * 1000000
                + (int32_t)(latMinH * 10000LL / 60);

    int deg = p[1] - 28;
    if (offset)
        deg += 100;
    if (deg >= 180 && deg <= 189)
        deg -= 80;
    else if (deg >= 190 && deg <= 199)
        deg -= 190;
    int min = p[2] - 28;
    if (min >= 60)
        min -= 60;
    int hund = p[3] - 28;
    if (deg < 0 || deg > 179 || min < 0 || min > 59 || hund < 0 || hund > 99)
        return false;
    int32_t lon = deg * 1000000 + (int32_t)((min * 100 + hund) * 10000LL / 60);

    out->lat = north ? lat : -lat;
    out->lon = west ? -lon : lon;
    out->hasPos = true;

    int sp = p[4] - 28, dc = p[5] - 28, se = p[6] - 28;
    int speed = sp * 10 + dc / 10;
    int course = (dc % 10) * 100 + se;
    if (speed >= 800)
        speed -= 800;
    if (course >= 400)
        course -= 400;
    if (speed >= 0 && speed < 800)
        out->speed = (int16_t)speed;
    if (course > 0 && course <= 360)
        out->course = (int16_t)(course % 360);
    out->symCode = (char)p[7];
    out->symTable = (char)p[8];

    /* Comment, without the radio's type marker in front */
    size_t c = 9;
    if (c < f->infoLen
        && (p[c] == '>' || p[c] == ']' || p[c] == '`' || p[c] == '\''))
        c++;
    copyText(out->text, sizeof(out->text), p + c, f->infoLen - c);
    return true;
}

bool aprs_parse(const struct ax25Frame *f, struct aprsPacket *out)
{
    const uint8_t *p = f->info;
    size_t n = f->infoLen;

    memset(out, 0, sizeof(*out));
    out->course = APRS_NO_COURSE;
    out->speed = APRS_NO_SPEED;
    ax25_callText(&f->src, out->src, sizeof(out->src));
    for (int i = f->ndigi - 1; i >= 0; i--)
        if (f->digi[i].repeated) {
            ax25_callText(&f->digi[i], out->via, sizeof(out->via));
            break;
        }

    if (f->control != 0x03 || f->pid != 0xf0 || n == 0)
        return false;

    switch (p[0]) {
        case '!':
        case '=':
        case '/':
        case '@': {
            size_t at = (p[0] == '/' || p[0] == '@') ? 8 : 1;
            if (n <= at)
                return false;
            size_t used = readPosition(p + at, n - at, out);
            if (used == 0)
                return false;
            out->type = APRS_POSITION;
            copyText(out->text, sizeof(out->text), p + at + used,
                     n - at - used);
            return true;
        }

        case '`':
        case '\'':
        case 0x1c:
        case 0x1d:
            if (!readMicE(f, out))
                return false;
            out->type = APRS_POSITION;
            return true;

        case '>':
            out->type = APRS_STATUS;
            copyText(out->text, sizeof(out->text), p + 1, n - 1);
            return true;

        case ':': {
            if (n < 11 || p[10] != ':')
                return false;
            copyText(out->addressee, sizeof(out->addressee), p + 1, 9);
            const uint8_t *t = p + 11;
            size_t tn = n - 11;
            /* A message number after '{' */
            for (size_t i = 0; i < tn; i++)
                if (t[i] == '{') {
                    copyText(out->msgId, sizeof(out->msgId), t + i + 1,
                             tn - i - 1);
                    tn = i;
                    break;
                }
            copyText(out->text, sizeof(out->text), t, tn);
            out->type = APRS_MESSAGE;
            return true;
        }

        case ';': {
            /* Name, live or killed, timestamp, position */
            if (n < 18 + 13)
                return false;
            copyText(out->name, sizeof(out->name), p + 1, 9);
            size_t used = readPosition(p + 18, n - 18, out);
            if (used == 0)
                return false;
            out->type = APRS_OBJECT;
            copyText(out->text, sizeof(out->text), p + 18 + used,
                     n - 18 - used);
            return true;
        }

        case ')': {
            size_t i = 1;
            while (i < n && i <= 10 && p[i] != '!' && p[i] != '_')
                i++;
            if (i >= n || i < 4 || i > 10)
                return false;
            copyText(out->name, sizeof(out->name), p + 1, i - 1);
            size_t used = readPosition(p + i + 1, n - i - 1, out);
            if (used == 0)
                return false;
            out->type = APRS_OBJECT;
            copyText(out->text, sizeof(out->text), p + i + 1 + used,
                     n - i - 1 - used);
            return true;
        }

        default:
            out->type = APRS_OTHER;
            copyText(out->text, sizeof(out->text), p, n);
            return true;
    }
}

/** "4339.19N", "07922.99W" */
static int formatDegrees(char *buf, size_t len, int32_t micro, int degDigits,
                         char pos, char neg)
{
    uint32_t a = (uint32_t)(micro < 0 ? -(int64_t)micro : micro);
    /* In hundredths of a minute, rounded */
    uint32_t h = (uint32_t)((a * 6ULL + 500) / 1000);
    return snprintf(buf, len, "%0*lu%02lu.%02lu%c", degDigits,
                    (unsigned long)(h / 6000), (unsigned long)(h % 6000 / 100),
                    (unsigned long)(h % 100), micro < 0 ? neg : pos);
}

size_t aprs_formatPosition(char *buf, size_t len, int32_t lat, int32_t lon,
                           char symTable, char symCode, const char *comment)
{
    char la[12], lo[12];

    formatDegrees(la, sizeof(la), lat, 2, 'N', 'S');
    formatDegrees(lo, sizeof(lo), lon, 3, 'E', 'W');
    int n = snprintf(buf, len, "!%s%c%s%c%s", la, symTable, lo, symCode,
                     comment ? comment : "");
    if (n < 0)
        return 0;
    return ((size_t)n < len) ? (size_t)n : len - 1;
}

static const struct {
    char code;
    const char *name;
} symbols[] = {
    { '[', "Person" },
    { '>', "Car" },
    { '-', "House" },
    { 'b', "Bike" },
    { 'k', "Truck" },
    { 'v', "Van" },
    { 's', "Boat" },
    { 'r', "Antenna" },
    /* Heard, not chosen */
    { '#', "Digi" },
    { '&', "Gateway" },
    { '_', "Weather" },
    { 'j', "Jeep" },
    { 'u', "Truck" },
    { 'R', "RV" },
    { 'U', "Bus" },
    { '<', "Motorbike" },
    { 'O', "Balloon" },
    { '\'', "Plane" },
    { 'y', "Yagi" },
    { 'Y', "Yacht" },
    { 'a', "Ambulance" },
    { 'f', "Fire truck" },
    { 'h', "Hospital" },
    { 'n', "Node" },
};

const char *aprs_symbol(uint8_t i, char *table, char *code)
{
    if (i >= APRS_SYMBOLS)
        i = 0;
    *table = '/';
    *code = symbols[i].code;
    return symbols[i].name;
}

const char *aprs_symbolName(char table, char code)
{
    if (table != '/')
        return (code == '#') ? "Digi" : (code == '&') ? "Gateway" : "";
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++)
        if (symbols[i].code == code)
            return symbols[i].name;
    return "";
}

const char *aprs_pathText(uint8_t path)
{
    switch (path) {
        case APRS_PATH_WIDE1_WIDE2:
            return "WIDE1-1,WIDE2-1";
        case APRS_PATH_WIDE2:
            return "WIDE2-1";
        default:
            return "";
    }
}

size_t aprs_beaconFrame(uint8_t *out, size_t max, const char *call,
                        uint8_t path, int32_t lat, int32_t lon, char symTable,
                        char symCode, const char *comment)
{
    static struct ax25Frame f;
    char info[AX25_MAX_INFO];

    memset(&f, 0, sizeof(f));
    if (!ax25_parseCall(call, &f.src))
        return 0;
    ax25_parseCall(TOCALL, &f.dst);
    if (path == APRS_PATH_WIDE1_WIDE2) {
        ax25_parseCall("WIDE1-1", &f.digi[0]);
        ax25_parseCall("WIDE2-1", &f.digi[1]);
        f.ndigi = 2;
    } else if (path == APRS_PATH_WIDE2) {
        ax25_parseCall("WIDE2-1", &f.digi[0]);
        f.ndigi = 1;
    }
    f.control = 0x03;
    f.pid = 0xf0;
    size_t n = aprs_formatPosition(info, sizeof(info), lat, lon, symTable,
                                   symCode, comment);
    memcpy(f.info, info, n);
    f.infoLen = (uint16_t)n;
    return ax25_encode(&f, out, max);
}

void aprs_distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2,
                   uint32_t *meters, uint16_t *bearing)
{
    const double r = 6371000.0;
    const double k = M_PI / 180e6;
    double p1 = lat1 * k, p2 = lat2 * k;
    double dp = (lat2 - lat1) * k, dl = ((double)lon2 - lon1) * k;

    double a = sin(dp / 2) * sin(dp / 2)
             + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    *meters = (uint32_t)(2 * r * atan2(sqrt(a), sqrt(1 - a)));

    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = atan2(y, x) * 180.0 / M_PI;
    if (b < 0)
        b += 360.0;
    *bearing = (uint16_t)((int)lround(b) % 360);
}
