/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <tetra/tetra.h>
#include "interfaces/delays.h"
#include "protocols/TETRA/TetraConfig.h"
#include "protocols/TETRA/TetraFocus.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"

/* A call is shown this long after its last activity, if no release is seen */
#define CALL_TIMEOUT_MS 10000
#define VOICE_TIMEOUT_MS 1000

static pthread_mutex_t statusMutex = PTHREAD_MUTEX_INITIALIZER;
static struct tetraStatus status; /* Under statusMutex */
static long long lastCall;        /* Tick of the last call activity */
static long long lastVoice;       /* Tick of the last traffic slot */
static struct tetraFocus focus;   /* The call played and shown */
/* Messages received, a ring: the next one goes at msgNext */
#define MESSAGE_REPEAT_MS 30000
static struct tetraMessage messages[TETRA_MESSAGES_KEPT];
static uint8_t msgNext;
static uint8_t msgCount;

bool tetraStatus_callNow(uint16_t *callId, uint32_t *dst, uint32_t *talker,
                         bool *group, uint16_t *la)
{
    pthread_mutex_lock(&statusMutex);
    bool call = status.call;
    *callId = status.callId;
    *dst = status.callDst;
    *talker = status.talker;
    *group = status.callGroup;
    *la = status.la;
    pthread_mutex_unlock(&statusMutex);
    return call;
}

bool tetraStatus_timeNow(uint16_t *year, uint16_t *day, uint32_t *sec,
                         bool *local)
{
    pthread_mutex_lock(&statusMutex);
    bool known = status.time;
    uint32_t s = status.timeOfDay
               + (uint32_t)((getTick() - status.timeTick) / 1000);
    *year = status.timeYear;
    *day = (uint16_t)(status.timeDay + s / 86400);
    *sec = s % 86400;
    *local = status.timeLocal;
    pthread_mutex_unlock(&statusMutex);
    return known;
}

void tetra_getStatus(struct tetraStatus *out)
{
    pthread_mutex_lock(&statusMutex);
    long long now = getTick();
    status.call = status.call && (now - lastCall < CALL_TIMEOUT_MS);
    status.voice = status.call && (now - lastVoice < VOICE_TIMEOUT_MS);
    *out = status;
    pthread_mutex_unlock(&statusMutex);
}

/**
 * Copy UTF-8 text as printable ASCII: other characters become '?'.
 */
static void toAscii(char *dst, size_t len, const char *src)
{
    size_t n = 0;
    while (*src != '\0' && n + 1 < len) {
        unsigned char c = (unsigned char)*src++;
        if (c >= 0x80) {
            /* Skip the continuation bytes of a multi-byte character */
            while (((unsigned char)*src & 0xc0) == 0x80)
                src++;
            c = '?';
        } else if (c < 0x20) {
            c = ' ';
        }
        dst[n++] = (char)c;
    }
    dst[n] = '\0';
}

static void onSds(const struct tetra_sds *s)
{
    char text[160];

    text[0] = '\0';
    if (s->type == TETRA_SDS_STATUS) {
        snprintf(text, sizeof(text), "Status %u", s->status);
    } else if (s->type == TETRA_SDS_UDD4) {
        struct tetra_sds_text t;
        if (tetra_sds_parse_text(s, &t) == 0
            && tetra_sds_text_utf8(&t, text, sizeof(text)) < 0)
            text[0] = '\0';
    }
    if (text[0] == '\0')
        return; /* Location reports and binary data are not shown */

    toAscii(status.sds, sizeof(status.sds), text);
    status.sdsSrc = s->src_ssi;
    status.sdsTime = getTick();

    /* The history, with up to 127 characters of the text: the same text
     * from the same sender soon after counts as a repeat */
    char full[sizeof(messages[0].text)];
    toAscii(full, sizeof(full), text);
    struct tetraMessage *last =
        &messages[(msgNext + TETRA_MESSAGES_KEPT - 1) % TETRA_MESSAGES_KEPT];
    if (msgCount > 0 && last->src == s->src_ssi && strcmp(last->text, full) == 0
        && status.sdsTime - last->time < MESSAGE_REPEAT_MS) {
        if (last->repeats < 255)
            last->repeats++;
        return;
    }
    struct tetraMessage *m = &messages[msgNext];
    m->src = s->src_ssi;
    m->dst = s->dst_ssi;
    m->time = status.sdsTime;
    m->repeats = 0;
    memcpy(m->text, full, sizeof(m->text));
    msgNext = (msgNext + 1) % TETRA_MESSAGES_KEPT;
    if (msgCount < TETRA_MESSAGES_KEPT)
        msgCount++;
}

uint8_t tetraStatus_messageCount(void)
{
    pthread_mutex_lock(&statusMutex);
    uint8_t n = msgCount;
    pthread_mutex_unlock(&statusMutex);
    return n;
}

bool tetraStatus_message(uint8_t n, struct tetraMessage *out)
{
    bool ok = false;

    pthread_mutex_lock(&statusMutex);
    if (n < msgCount) {
        *out = messages[(msgNext + TETRA_MESSAGES_KEPT - 1 - n)
                        % TETRA_MESSAGES_KEPT];
        ok = true;
    }
    pthread_mutex_unlock(&statusMutex);
    return ok;
}

/** Neighbour cells, load and time, for the field test screen */
static void onNetwork(const struct tetra_network *nw)
{
    uint8_t n = 0;

    status.load = nw->load;
    for (unsigned i = 0; i < nw->nneighbours && n < TETRA_STATUS_NEIGHBOURS;
         i++) {
        const struct tetra_neighbour *nb = &nw->neighbour[i];
        struct tetraStatusNeighbour *d = &status.neighbour[n++];
        d->freq = nb->dl_freq_hz;
        d->carrier = nb->main_carrier;
        d->la = nb->la;
        d->load = nb->load;
        d->other = (nb->mcc != 0xffff && nb->mcc != status.mcc)
                || (nb->mnc != 0xffff && nb->mnc != status.mnc);
    }
    if (n != 0)
        status.nNeighbours = n;

    if (nw->has_time && nw->time_valid) {
        /* utc_s counts from the start of the year */
        long t = (long)nw->utc_s;
        unsigned year = nw->year;
        if (nw->offset_valid)
            t += (long)nw->offset_min * 60;
        unsigned yearDays =
            (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (t < 0) {
            year--;
            yearDays = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ?
                           366 :
                           365;
            t += (long)yearDays * 86400;
        } else if (t >= (long)yearDays * 86400) {
            t -= (long)yearDays * 86400;
            year++;
        }
        long s = t % 86400;
        status.timeYear = (uint16_t)year;
        status.timeDay = (uint16_t)(t / 86400);
        status.time = true;
        status.timeLocal = nw->offset_valid;
        status.timeOfDay = (uint32_t)s;
        status.timeTick = getTick();
    }
}

static void onCall(const struct tetra_call *c)
{
    long long now = getTick();
    bool alarm = tetraIds_isAlarm(c->dst_ssi) || tetraIds_isAlarm(c->src_ssi);

    if (c->what == TETRA_CALL_RELEASE) {
        tetraFocus_release(&focus, c->call_id);
        if (status.call && c->call_id == status.callId) {
            status.call = false;
            status.talker = 0;
            status.callAlarm = false;
        }
        return;
    }

    if (c->what == TETRA_CALL_ALERT || c->what == TETRA_CALL_PROCEEDING)
        return;

    /* Every call goes to the recent calls */
    tetraCalls_note(&status.recent, c->dst_ssi, c->src_ssi, c->group, false,
                    c->encrypted, now);

    /* One call at a time is shown and played */
    if (!tetraFocus_offer(&focus, c->call_id, c->usage_marker, alarm, now))
        return;

    /* A new call identifier replaces the shown call */
    if (!status.call || c->call_id != status.callId) {
        status.callDst = 0;
        status.talker = 0;
        status.callEncrypted = false;
        status.callAlarm = false;
    }

    if (c->what == TETRA_CALL_TX_CEASED)
        status.talker = 0;
    else if (c->src_ssi != 0)
        status.talker = c->src_ssi;

    status.call = true;
    status.callId = c->call_id;
    if (c->dst_ssi != 0) {
        status.callDst = c->dst_ssi;
        status.callGroup = c->group;
    }
    status.callEncrypted |= c->encrypted;
    lastCall = now;

    /* An alarm: sound the alert once per call */
    if (alarm && !status.callAlarm) {
        status.callAlarm = true;
        if (tetraConfig_alarmBeep())
            tetraSpeech_alert();
    }
}

/**
 * A traffic slot of a call.
 *
 * @return true if it belongs to the call played and shown.
 */
static bool onTraffic(const struct tetra_traffic *t)
{
    long long now = getTick();

    /* On a followed call's carrier, all traffic is that call's */
    if (status.followHz == 0
        && !tetraFocus_offer(&focus, t->call_id, t->usage_marker, false, now))
        return false;

    /* A new transmission: after a pause, or another talker */
    if (now - lastVoice >= VOICE_TIMEOUT_MS
        || (t->talker_ssi != 0 && t->talker_ssi != status.talker))
        status.txStart = now;

    if (t->talker_ssi != 0)
        status.talker = t->talker_ssi;
    status.callEncrypted |= t->encrypted;
    if (!status.call)
        status.callId = t->call_id;
    status.call = true;
    lastCall = lastVoice = now;

    tetraCalls_note(&status.recent, status.callDst, status.talker,
                    status.callGroup, !t->encrypted && t->circuit_type == 0,
                    t->encrypted, now);
    return true;
}

void tetraStatus_onEvent(void *user, const struct tetra_event *ev)
{
    bool play = false;

    (void)user;

    pthread_mutex_lock(&statusMutex);
    switch (ev->type) {
        case TETRA_EV_SYNC:
            status.sync = ev->u.sync.locked;
            if (!status.sync) {
                status.call = false;
                tetraFocus_init(&focus);
            }
            break;

        case TETRA_EV_CELL: {
            const struct tetra_cell *c = ev->u.cell;
            status.cell = true;
            status.mcc = c->mcc;
            status.mnc = c->mnc;
            status.colourCode = c->colour_code;
            if (c->have_sysinfo) {
                /*
                 * Moved to another cell: the UI says so, and where from,
                 * for a while
                 */
                if (status.sysinfo
                    && (status.la != c->location_area
                        || status.dlFreq != c->dl_freq_hz)) {
                    status.cellChanged = getTick();
                    status.prevLa = status.la;
                    status.prevDlFreq = status.dlFreq;
                }
                status.sysinfo = true;
                status.la = c->location_area;
                status.dlFreq = c->dl_freq_hz;
                status.airEncryption = c->air_encryption;
            }
            break;
        }

        case TETRA_EV_CALL:
            onCall(&ev->u.call);
            break;

        case TETRA_EV_TRAFFIC:
            play = onTraffic(&ev->u.traffic);
            break;

        case TETRA_EV_SDS:
            onSds(&ev->u.sds);
            break;

        case TETRA_EV_NETWORK:
            onNetwork(ev->u.network);
            break;

        default:
            break;
    }
    pthread_mutex_unlock(&statusMutex);

    /* Speech outside the status lock */
    if (play)
        tetraSpeech_traffic(&ev->u.traffic);
}

void tetraStatus_reset(void)
{
    pthread_mutex_lock(&statusMutex);
    /* Recent calls stay across frequency changes */
    struct tetraCalls recent = status.recent;
    memset(&status, 0, sizeof(status));
    status.recent = recent;
    status.roamState = TETRA_STATUS_NO_ROAM;
    tetraFocus_init(&focus);
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_lostSync(bool overrun)
{
    pthread_mutex_lock(&statusMutex);
    status.sync = false;
    status.resyncs++;
    if (overrun)
        status.overruns++;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_setDemod(unsigned phaseErr, int carrierHz)
{
    pthread_mutex_lock(&statusMutex);
    status.phaseErr = phaseErr;
    status.carrierHz = carrierHz;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_endCall(void)
{
    pthread_mutex_lock(&statusMutex);
    tetraFocus_init(&focus);
    status.call = false;
    status.talker = 0;
    status.callAlarm = false;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_setTuned(uint32_t freq)
{
    pthread_mutex_lock(&statusMutex);
    status.tunedHz = freq;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_setSearching(bool searching)
{
    pthread_mutex_lock(&statusMutex);
    status.searching = searching;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_setFollow(uint32_t freq)
{
    pthread_mutex_lock(&statusMutex);
    status.followHz = freq;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_setRoamState(uint8_t state)
{
    pthread_mutex_lock(&statusMutex);
    status.roamState = state;
    pthread_mutex_unlock(&statusMutex);
}

void tetraStatus_setMeasured(const struct tetraStatusMeasured *m, unsigned n)
{
    if (n > TETRA_STATUS_MEASURED)
        n = TETRA_STATUS_MEASURED;

    pthread_mutex_lock(&statusMutex);
    memcpy(status.measured, m, n * sizeof(m[0]));
    status.nMeasured = (uint8_t)n;
    status.measuredTime = getTick();
    pthread_mutex_unlock(&statusMutex);
}

/* Written by the UI, read by the receiver */
static volatile uint8_t searchRequests;

void tetraStatus_requestSearch(void)
{
    searchRequests++;
}

uint8_t tetraStatus_searchRequests(void)
{
    return searchRequests;
}

bool tetraStatus_sync(void)
{
    pthread_mutex_lock(&statusMutex);
    bool sync = status.sync;
    pthread_mutex_unlock(&statusMutex);
    return sync;
}
