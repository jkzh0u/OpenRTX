/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <tetra/tetra.h>
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraLog.h"
#include "protocols/TETRA/TetraLogEvents.h"

/* Network time: logged at the first broadcast, then this often, ms */
#define TIME_EVERY_MS (10 * 60 * 1000)
/* A traffic slot after this long without one starts a new transmission */
#define TRAFFIC_GAP_MS 2000

/* The serving cell as last logged */
static struct {
    bool valid;
    uint16_t mcc, mnc, la, mainCarrier;
    uint8_t band, offset;
} cell;

static uint32_t neighbours[TETRA_MAX_NEIGHBOURS];
static uint8_t nNeighbours;
static bool neighboursLogged;

static long long lastTime;
static bool timeLogged;

/*
 * Transmissions heard, one per timeslot: two calls on two timeslots of a
 * carrier are each logged once, not at every slot as they alternate
 */
static struct {
    bool seen;
    uint16_t call;
    long long last;
} traffic[4];

static void forgetTraffic(void)
{
    memset(traffic, 0, sizeof(traffic));
}

void tetraLogEvents_reset(void)
{
    cell.valid = false;
    neighboursLogged = false;
    forgetTraffic();
}

const char *tetraLog_freq(char *buf, unsigned len, uint32_t hz)
{
    snprintf(buf, len, "%lu.%05lu", (unsigned long)(hz / 1000000),
             (unsigned long)(hz % 1000000 / 10));
    return buf;
}

/**
 * An identity for a log line: "G 101011 Riding" or "100112".
 */
static const char *identity(char *buf, size_t len, uint32_t ssi, bool group)
{
    char name[40];
    int n = snprintf(buf, len, "%s%lu", group ? "G " : "", (unsigned long)ssi);

    if (n > 0 && (size_t)n < len && tetraIds_nameOf(ssi, name, sizeof(name)))
        snprintf(buf + n, len - n, " (%s)", name);
    return buf;
}

static const char *callWhat(enum tetra_call_what what)
{
    static const char *const names[] = {
        "setup",        "connect",     "tx granted", "tx ceased",  "late entry",
        "info",         "release",     "alert",      "proceeding", "tx wait",
        "tx interrupt", "tx continue", "restore",    "tx demand",
    };

    if ((unsigned)what < sizeof(names) / sizeof(names[0]))
        return names[what];
    return "?";
}

static void onCell(long long now, const struct tetra_cell *c)
{
    char freq[16];
    const char *place;

    if (!c->have_sysinfo)
        return;
    if (cell.valid && c->mcc == cell.mcc && c->mnc == cell.mnc
        && c->location_area == cell.la && c->main_carrier == cell.mainCarrier)
        return;

    cell.valid = true;
    cell.mcc = c->mcc;
    cell.mnc = c->mnc;
    cell.la = c->location_area;
    cell.mainCarrier = c->main_carrier;
    cell.band = c->band;
    cell.offset = c->offset;
    neighboursLogged = false;

    place = tetraIds_laName(c->location_area);
    tetraLog_printf(now, "cell %u-%u LA %u%s%s%s CC %u, main carrier %u = %s%s",
                    c->mcc, c->mnc, c->location_area, place ? " (" : "",
                    place ? place : "", place ? ")" : "", c->colour_code,
                    c->main_carrier,
                    tetraLog_freq(freq, sizeof(freq), c->dl_freq_hz),
                    c->air_encryption ? ", air encryption" : "");
}

static bool isLeap(unsigned year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/** Local time broadcast by the network */
static void logTime(long long now, const struct tetra_network *nw)
{
    static const uint8_t monthDays[] = { 31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31 };
    long s = (long)nw->utc_s;
    unsigned year = nw->year;

    if (nw->offset_valid)
        s += (long)nw->offset_min * 60;

    /* The offset can move the time to the year before or after */
    if (s < 0) {
        year--;
        s += (isLeap(year) ? 366L : 365L) * 86400;
    } else if (s >= (isLeap(year) ? 366L : 365L) * 86400) {
        s -= (isLeap(year) ? 366L : 365L) * 86400;
        year++;
    }

    unsigned day = (unsigned)(s / 86400);
    unsigned month = 0;
    while (month < 11) {
        unsigned days = monthDays[month] + (month == 1 && isLeap(year));
        if (day < days)
            break;
        day -= days;
        month++;
    }

    s %= 86400;
    tetraLog_printf(now, "network time %04u-%02u-%02u %02ld:%02ld:%02ld %s",
                    year, month + 1, day + 1, s / 3600, s / 60 % 60, s % 60,
                    nw->offset_valid ? "local" : "UTC");
}

static void onNetwork(long long now, const struct tetra_network *nw)
{
    uint32_t freq[TETRA_MAX_NEIGHBOURS];
    uint8_t n = 0;

    if (nw->has_time && nw->time_valid
        && (!timeLogged || now - lastTime >= TIME_EVERY_MS)) {
        timeLogged = true;
        lastTime = now;
        logTime(now, nw);
    }

    for (unsigned i = 0; i < nw->nneighbours && n < TETRA_MAX_NEIGHBOURS; i++)
        freq[n++] = nw->neighbour[i].dl_freq_hz;
    if (n == 0)
        return;
    if (neighboursLogged && n == nNeighbours
        && memcmp(freq, neighbours, n * sizeof(freq[0])) == 0)
        return;

    memcpy(neighbours, freq, n * sizeof(freq[0]));
    nNeighbours = n;
    neighboursLogged = true;

    char line[TETRA_LOG_LINE_MAX];
    size_t len = 0;
    line[0] = '\0';
    for (unsigned i = 0; i < n && len < sizeof(line); i++) {
        char f[16];
        const struct tetra_neighbour *nb = &nw->neighbour[i];
        if (nb->dl_freq_hz != 0)
            tetraLog_freq(f, sizeof(f), nb->dl_freq_hz);
        else
            snprintf(f, sizeof(f), "carrier %u", nb->main_carrier);
        int w = snprintf(line + len, sizeof(line) - len, "%s%s%s",
                         i ? ", " : "", f,
                         (nb->mcc != 0xffff && nb->mcc != cell.mcc)
                                 || (nb->mnc != 0xffff && nb->mnc != cell.mnc)
                             ? " (other network)"
                             : "");
        if (w < 0)
            break;
        len += (size_t)w;
    }
    tetraLog_printf(now, "neighbours: %s", line);
}

static void onCall(long long now, const struct tetra_call *c, bool passed)
{
    char dst[64], src[64], where[64];

    dst[0] = src[0] = where[0] = '\0';
    if (c->dst_ssi != 0) {
        char id[56];
        snprintf(dst, sizeof(dst), " to %s",
                 identity(id, sizeof(id), c->dst_ssi, c->group));
    }
    if (c->src_ssi != 0) {
        char id[56];
        snprintf(src, sizeof(src), " from %s",
                 identity(id, sizeof(id), c->src_ssi, false));
    }
    if (c->carrier != 0xffff) {
        char f[20] = "";
        uint32_t hz = cell.valid ? tetra_carrier_hz(cell.band, c->carrier,
                                                    cell.offset)
                                 : 0;
        if (hz != 0) {
            char mhz[16];
            snprintf(f, sizeof(f), " = %s",
                     tetraLog_freq(mhz, sizeof(mhz), hz));
        }
        snprintf(where, sizeof(where), ", carrier %u%s slots %s%s%s%s%s",
                 c->carrier, f, c->timeslots ? "" : "MCCH",
                 (c->timeslots & 1) ? "1" : "", (c->timeslots & 2) ? "2" : "",
                 (c->timeslots & 4) ? "3" : "", (c->timeslots & 8) ? "4" : "");
    }

    tetraLog_printf(now, "call %u %s%s%s%s%s%s%s", c->call_id,
                    callWhat(c->what), dst, src, where,
                    c->circuit_type != 0 ? ", data" : "",
                    c->encrypted ? ", encrypted" : "",
                    passed ? "" : ", filtered out");
}

/**
 * @return true if a decoded text is worth printing: not empty, and nothing
 * but printable characters and white space up to its end, so that binary
 * data sent as a text message is not shown as text.
 */
static bool printable(const char *utf8)
{
    if (*utf8 == '\0')
        return false;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        if (*p < 0x20 && *p != '\n' && *p != '\r' && *p != '\t')
            return false;
    }
    return true;
}

/** Short data: text and status as such, other data by its size */
static void onSds(long long now, const struct tetra_sds *s, bool passed)
{
    char from[56], to[56], text[96];

    identity(from, sizeof(from), s->src_ssi, false);
    identity(to, sizeof(to), s->dst_ssi, false);

    text[0] = '\0';
    if (s->type == TETRA_SDS_STATUS) {
        snprintf(text, sizeof(text), "status %u", s->status);
    } else if (s->type == TETRA_SDS_UDD4) {
        struct tetra_sds_text t;
        char utf8[96];
        bool parsed = tetra_sds_parse_text(s, &t) == 0;
        int len = parsed ? tetra_sds_text_utf8(&t, utf8, sizeof(utf8)) : -1;
        if (len > 0 && printable(utf8)) {
            /* Printable ASCII only, for any terminal */
            size_t n = 0;
            text[n++] = '"';
            for (const char *p = utf8; *p != '\0' && n + 2 < sizeof(text); p++)
                text[n++] = ((unsigned char)*p >= 0x20
                             && (unsigned char)*p < 0x7f)
                              ? *p
                              : '?';
            text[n++] = '"';
            text[n] = '\0';
        } else if (parsed) {
            /* Binary in a text message (a NUL, control bytes): the bytes */
            int n = snprintf(text, sizeof(text), "data (protocol %u, coding "
                             "%u):", t.protocol_id, t.coding);
            for (unsigned i = 0; i < t.nbits / 8 && n > 0
                                 && (size_t)n + 4 < sizeof(text); i++) {
                unsigned bit = t.start + 8 * i;
                unsigned b = (unsigned)((t.data[bit / 8] << (bit % 8)
                                         | (bit % 8 ? t.data[bit / 8 + 1]
                                                          >> (8 - bit % 8)
                                                    : 0)) & 0xff);
                n += snprintf(text + n, sizeof(text) - n, " %02x", b);
            }
        }
    }
    if (text[0] == '\0')
        snprintf(text, sizeof(text), "%u bits of data", s->nbits);

    tetraLog_printf(now, "sds from %s to %s: %s%s", from, to, text,
                    passed ? "" : ", filtered out");
}

/** The first traffic slot of a transmission: audio is being received */
static void onTraffic(long long now, const struct tetra_traffic *t,
                      bool passed)
{
    unsigned tn = (t->time.tn >= 1 && t->time.tn <= 4) ? t->time.tn - 1 : 0;
    bool start = !traffic[tn].seen || t->call_id != traffic[tn].call
              || now - traffic[tn].last >= TRAFFIC_GAP_MS;

    traffic[tn].seen = true;
    traffic[tn].call = t->call_id;
    traffic[tn].last = now;
    if (!start)
        return;

    char talker[64] = "";
    if (t->talker_ssi != 0) {
        char id[56];
        snprintf(talker, sizeof(talker), " from %s",
                 identity(id, sizeof(id), t->talker_ssi, false));
    }
    tetraLog_printf(now, "%s call %u%s%s%s",
                    t->circuit_type == 0 ? "voice" : "data", t->call_id,
                    talker, t->encrypted ? ", encrypted" : "",
                    passed ? "" : ", filtered out");
}

void tetraLogEvents_event(long long now, const struct tetra_event *ev,
                          bool passed)
{
    if (ev->uplink)
        return;

    switch (ev->type) {
        case TETRA_EV_SYNC:
            tetraLog_printf(now, "sync %s",
                            ev->u.sync.locked ? "locked" : "lost");
            if (!ev->u.sync.locked)
                forgetTraffic();
            break;

        case TETRA_EV_CELL:
            onCell(now, ev->u.cell);
            break;

        case TETRA_EV_NETWORK:
            onNetwork(now, ev->u.network);
            break;

        case TETRA_EV_CALL:
            onCall(now, &ev->u.call, passed);
            break;

        case TETRA_EV_SDS:
            onSds(now, &ev->u.sds, passed);
            break;

        case TETRA_EV_TRAFFIC:
            onTraffic(now, &ev->u.traffic, passed);
            break;

        default:
            break;
    }
}
