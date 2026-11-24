/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hwconfig.h"
#include "core/replay.h"

#ifdef CONFIG_REPLAY

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "interfaces/delays.h"

#define THREAD_STACK 2048
#define HALF 240 /* 30 ms blocks */

#ifdef CONFIG_REPLAY_SECTION
#define REPLAY_MEMORY __attribute__((section(CONFIG_REPLAY_SECTION)))
#else
#define REPLAY_MEMORY
#endif

/* Everything received, as a ring: sample n is at ring[n % REPLAY_SAMPLES] */
static int16_t ring[REPLAY_SAMPLES] REPLAY_MEMORY;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t written;   /* Samples recorded since the start */
static long long lastTime; /* Last recording, ms */
/* Where the transmissions start, the last at starts[(last) % N] */
static uint32_t starts[REPLAY_TRANSMISSIONS];
static unsigned last;  /* Index of the last transmission + 1, 0: none */
static bool recording; /* A transmission is being recorded */

/* Asked by the UI */
static volatile uint8_t wantIndex; /* 0: stop */
static volatile uint8_t wantSeq;

/* Shown to the UI */
static struct replayStatus status;

static pthread_t thread;
static volatile bool threadUp;
static stream_sample_t buf[2 * HALF];

/** Start of a transmission, 1 the last, in samples; false if not kept */
static bool span(unsigned index, uint32_t *from, uint32_t *to)
{
    if (index == 0 || index > last || index > REPLAY_TRANSMISSIONS)
        return false;

    unsigned k = last - index;
    *from = starts[k % REPLAY_TRANSMISSIONS];
    *to = (index == 1) ? written : starts[(k + 1) % REPLAY_TRANSMISSIONS];
    /* The start may have been written over: keep what is left */
    if (written - *from > REPLAY_SAMPLES)
        *from = written - REPLAY_SAMPLES;
    return *to > *from;
}

/** Transmissions still in memory */
static uint8_t kept(void)
{
    uint8_t n = 0;
    uint32_t from, to;
    while (n < REPLAY_TRANSMISSIONS && span(n + 1, &from, &to))
        n++;
    return n;
}

void replay_record(const int16_t *pcm, size_t n)
{
    long long now = getTick();

    pthread_mutex_lock(&mutex);
    if (!recording || now - lastTime > REPLAY_GAP_MS) {
        /* A new transmission, unless the last one was too short to keep:
         * then this one takes its place */
        uint32_t from, to;
        bool shortLast = last > 0 && span(1, &from, &to)
                      && to - from < REPLAY_MIN_SAMPLES;
        if (shortLast)
            written = from; /* Its samples are written over */
        else
            last++;
        starts[(last - 1) % REPLAY_TRANSMISSIONS] = written;
        recording = true;
    }
    for (size_t i = 0; i < n; i++)
        ring[(written + i) % REPLAY_SAMPLES] = pcm[i];
    written += n;
    lastTime = now;
    status.count = kept();
    pthread_mutex_unlock(&mutex);
}

/**
 * Play one transmission until it ends, is stopped or replaced, or the
 * speaker is taken.
 */
static void playOne(uint8_t index, uint8_t seq)
{
    uint32_t pos, end;

    pthread_mutex_lock(&mutex);
    bool ok = span(index, &pos, &end);
    pthread_mutex_unlock(&mutex);
    if (!ok)
        return;

    pathId path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_BEEP);
    if (audioPath_getStatus(path) != PATH_OPEN) {
        audioPath_release(path);
        return;
    }
    memset(buf, 0, sizeof(buf));
    streamId stream = audioStream_start(path, buf, 2 * HALF, REPLAY_RATE,
                                        STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (stream < 0) {
        audioPath_release(path);
        return;
    }

    status.playing = true;
    status.index = index;
    status.length = (uint16_t)((end - pos) / REPLAY_RATE);
    status.played = 0;
    uint32_t begin = pos;

    outputStream_sync(stream, false);
    while (pos < end && wantSeq == seq && wantIndex != 0
           && audioPath_getStatus(path) == PATH_OPEN) {
        stream_sample_t *out = outputStream_getIdleBuffer(stream);
        if (out == NULL)
            break;
        pthread_mutex_lock(&mutex);
        bool lost = written - pos > REPLAY_SAMPLES;
        for (unsigned i = 0; i < HALF; i++, pos++)
            out[i] = (!lost && pos < end) ? ring[pos % REPLAY_SAMPLES] : 0;
        pthread_mutex_unlock(&mutex);
        if (lost)
            break;
        status.played = (uint16_t)((pos - begin) / REPLAY_RATE);
        outputStream_sync(stream, true);
    }

    audioStream_stop(stream);
    audioPath_release(path);
    status.playing = false;
}

static void *player(void *arg)
{
    (void)arg;
    uint8_t done = 0;

    for (;;) {
        uint8_t seq = wantSeq;
        if (seq != done && wantIndex != 0) {
            done = seq;
            playOne(wantIndex, seq);
            if (wantSeq == seq)
                wantIndex = 0;
        } else {
            sleepFor(0, 20);
        }
    }
    return NULL;
}

static void startThread(void)
{
    pthread_attr_t attr;

    if (threadUp)
        return;
    pthread_attr_init(&attr);
#ifdef __ZEPHYR__
    void *stack = malloc(THREAD_STACK);
    if (stack == NULL)
        return;
    pthread_attr_setstack(&attr, stack, THREAD_STACK);
#else
    pthread_attr_setstacksize(&attr, THREAD_STACK);
#endif
    threadUp = pthread_create(&thread, &attr, player, NULL) == 0;
#ifdef __ZEPHYR__
    if (!threadUp)
        free(stack);
#endif
}

void replay_play(void)
{
    startThread();

    pthread_mutex_lock(&mutex);
    uint8_t count = kept();
    pthread_mutex_unlock(&mutex);
    if (count == 0)
        return;

    /* Playing: the one before; else the last */
    uint8_t index = 1;
    if (status.playing && status.index < count)
        index = status.index + 1;
    else if (status.playing)
        index = status.index;
    wantIndex = index;
    wantSeq++;
}

void replay_stop(void)
{
    wantIndex = 0;
    wantSeq++;
}

void replay_get(struct replayStatus *out)
{
    *out = status;
}

#endif /* CONFIG_REPLAY */
