/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/APRS/AprsStatus.h"
#include "hwconfig.h"
#include <pthread.h>
#include <string.h>
#include "protocols/APRS/Afsk.h"
#include "protocols/APRS/Aprs.h"
#include "protocols/APRS/Ax25.h"

/*
 * The bigger arrays can go to a memory section of the platform's
 * (CONFIG_APRS_SECTION: the C62's PSRAM). It is not cleared at start: the
 * counts, in ordinary memory, say what is in them.
 */
#ifdef CONFIG_APRS_SECTION
#define APRS_BIG __attribute__((section(CONFIG_APRS_SECTION)))
#else
#define APRS_BIG
#endif

/* The same packet again within this long is a repeat, not counted */
#define REPEAT_MS 30000

static pthread_mutex_t aprsMutex = PTHREAD_MUTEX_INITIALIZER;

/* Under aprsMutex */
static struct aprsStation stations[APRS_STATIONS] APRS_BIG;
static uint8_t stationCount;
static struct aprsMessage messages[APRS_MESSAGES] APRS_BIG;
static uint8_t msgNext;
static uint8_t msgCount;
static uint32_t packets;
static long long lastBeacon;

/* Received audio: written by the audio driver, read by the rtx thread */
static int16_t ring[APRS_RX_RING] APRS_BIG;
static volatile uint32_t ringHead; /* Written up to here */
static volatile uint32_t ringTail; /* Read up to here */

static volatile bool beaconRequested;

/* The rtx thread's own */
static struct afskDemod demod;
static bool demodReady;

void aprsStatus_pushAudio(const int16_t *pcm, size_t n)
{
    uint32_t head = __atomic_load_n(&ringHead, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&ringTail, __ATOMIC_ACQUIRE);

    for (size_t i = 0; i < n; i++) {
        if (head - tail >= APRS_RX_RING)
            break;
        ring[head % APRS_RX_RING] = pcm[i];
        head++;
    }
    __atomic_store_n(&ringHead, head, __ATOMIC_RELEASE);
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

/** The station of a call, made if new: the oldest heard makes room */
static struct aprsStation *stationOf(const char *call, long long now)
{
    struct aprsStation *s;

    for (uint8_t i = 0; i < stationCount; i++)
        if (strcmp(stations[i].call, call) == 0)
            return &stations[i];

    if (stationCount < APRS_STATIONS) {
        s = &stations[stationCount++];
    } else {
        s = &stations[0];
        for (uint8_t i = 1; i < stationCount; i++)
            if (stations[i].last < s->last)
                s = &stations[i];
    }
    memset(s, 0, sizeof(*s));
    strncpy(s->call, call, sizeof(s->call) - 1);
    s->first = now;
    s->course = APRS_NO_COURSE;
    s->speed = APRS_NO_SPEED;
    return s;
}

static void noteMessage(const struct aprsPacket *p, long long now)
{
    /* Sent again by the sender until answered: kept once */
    for (uint8_t i = 0; i < msgCount; i++) {
        const struct aprsMessage *m =
            &messages[(msgNext + APRS_MESSAGES - 1 - i) % APRS_MESSAGES];
        if (strcmp(m->from, p->src) == 0 && strcmp(m->text, p->text) == 0
            && strcmp(m->id, p->msgId) == 0)
            return;
    }

    struct aprsMessage *m = &messages[msgNext];
    memset(m, 0, sizeof(*m));
    strncpy(m->from, p->src, sizeof(m->from) - 1);
    strncpy(m->to, p->addressee, sizeof(m->to) - 1);
    strncpy(m->id, p->msgId, sizeof(m->id) - 1);
    strncpy(m->text, p->text, sizeof(m->text) - 1);
    m->time = now;
    msgNext = (uint8_t)((msgNext + 1) % APRS_MESSAGES);
    if (msgCount < APRS_MESSAGES)
        msgCount++;
}

void aprsStatus_frame(const uint8_t *frame, size_t len, long long now)
{
    static struct ax25Frame f;
    static struct aprsPacket p;

    if (!ax25_decode(frame, len, &f) || !aprs_parse(&f, &p))
        return;
    uint32_t hash = fnv(f.info, f.infoLen);

    pthread_mutex_lock(&aprsMutex);
    packets++;

    const char *call = (p.type == APRS_OBJECT) ? p.name : p.src;
    struct aprsStation *s = stationOf(call, now);
    bool repeat = s->hash == hash && now - s->last < REPEAT_MS;

    s->last = now;
    s->hash = hash;
    if (!repeat)
        s->heard++;
    strncpy(s->via, p.via, sizeof(s->via) - 1);
    s->via[sizeof(s->via) - 1] = '\0';

    if (p.type == APRS_OBJECT) {
        strncpy(s->by, p.src, sizeof(s->by) - 1);
        s->by[sizeof(s->by) - 1] = '\0';
    }
    if (p.type != APRS_MESSAGE)
        s->type = p.type;
    if (p.hasPos) {
        s->hasPos = true;
        s->lat = p.lat;
        s->lon = p.lon;
        s->symTable = p.symTable;
        s->symCode = p.symCode;
        s->course = p.course;
        s->speed = p.speed;
    }
    if (p.type != APRS_MESSAGE && p.type != APRS_OTHER && p.text[0] != '\0') {
        strncpy(s->text, p.text, sizeof(s->text) - 1);
        s->text[sizeof(s->text) - 1] = '\0';
    }
    if (p.type == APRS_MESSAGE)
        noteMessage(&p, now);
    pthread_mutex_unlock(&aprsMutex);
}

static void onFrame(const uint8_t *frame, size_t len, void *user)
{
    aprsStatus_frame(frame, len, *(const long long *)user);
}

void aprsStatus_process(long long now)
{
    int16_t block[128];

    if (!demodReady) {
        afskDemod_init(&demod);
        demodReady = true;
    }

    for (;;) {
        uint32_t head = __atomic_load_n(&ringHead, __ATOMIC_ACQUIRE);
        uint32_t tail = __atomic_load_n(&ringTail, __ATOMIC_RELAXED);
        size_t n = head - tail;
        if (n == 0)
            break;
        if (n > sizeof(block) / sizeof(block[0]))
            n = sizeof(block) / sizeof(block[0]);
        for (size_t i = 0; i < n; i++)
            block[i] = ring[(tail + i) % APRS_RX_RING];
        __atomic_store_n(&ringTail, tail + (uint32_t)n, __ATOMIC_RELEASE);
        afskDemod_process(&demod, block, n, onFrame, &now);
    }
}

uint32_t aprsStatus_packets(void)
{
    pthread_mutex_lock(&aprsMutex);
    uint32_t n = packets;
    pthread_mutex_unlock(&aprsMutex);
    return n;
}

uint8_t aprsStatus_stationCount(void)
{
    pthread_mutex_lock(&aprsMutex);
    uint8_t n = stationCount;
    pthread_mutex_unlock(&aprsMutex);
    return n;
}

bool aprsStatus_station(uint8_t n, struct aprsStation *out)
{
    bool ok = false;

    pthread_mutex_lock(&aprsMutex);
    /* The one with exactly n heard after it */
    for (uint8_t i = 0; i < stationCount && !ok; i++) {
        uint8_t newer = 0;
        for (uint8_t j = 0; j < stationCount; j++)
            if (j != i
                && (stations[j].last > stations[i].last
                    || (stations[j].last == stations[i].last && j > i)))
                newer++;
        if (newer == n) {
            *out = stations[i];
            ok = true;
        }
    }
    pthread_mutex_unlock(&aprsMutex);
    return ok;
}

uint8_t aprsStatus_messageCount(void)
{
    pthread_mutex_lock(&aprsMutex);
    uint8_t n = msgCount;
    pthread_mutex_unlock(&aprsMutex);
    return n;
}

bool aprsStatus_message(uint8_t n, struct aprsMessage *out)
{
    bool ok = false;

    pthread_mutex_lock(&aprsMutex);
    if (n < msgCount) {
        *out = messages[(msgNext + APRS_MESSAGES - 1 - n) % APRS_MESSAGES];
        ok = true;
    }
    pthread_mutex_unlock(&aprsMutex);
    return ok;
}

void aprsStatus_requestBeacon(void)
{
    beaconRequested = true;
}

bool aprsStatus_beaconRequested(void)
{
    return beaconRequested;
}

void aprsStatus_beaconSent(long long now)
{
    pthread_mutex_lock(&aprsMutex);
    lastBeacon = now;
    beaconRequested = false;
    pthread_mutex_unlock(&aprsMutex);
}

long long aprsStatus_lastBeacon(void)
{
    pthread_mutex_lock(&aprsMutex);
    long long t = lastBeacon;
    pthread_mutex_unlock(&aprsMutex);
    return t;
}

void aprsStatus_reset(void)
{
    pthread_mutex_lock(&aprsMutex);
    stationCount = 0;
    msgCount = 0;
    msgNext = 0;
    packets = 0;
    pthread_mutex_unlock(&aprsMutex);
}
