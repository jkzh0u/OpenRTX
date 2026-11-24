/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hwconfig.h"
#include "protocols/TETRA/TetraCallRec.h"

#if defined(CONFIG_TETRA_CALLREC) && defined(CONFIG_TETRA_SPEECH)

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <openacelp.h>
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "interfaces/delays.h"
#include "protocols/TETRA/TetraStatus.h"

#define WRITER_STACK 2048
#define PLAYER_STACK 4096
/* Frames read from the store at a time: one second */
#define PLAY_CHUNK 33

#ifdef CONFIG_TETRA_CALLREC_SECTION
#define CALLREC_MEMORY __attribute__((section(CONFIG_TETRA_CALLREC_SECTION)))
#else
#define CALLREC_MEMORY
#endif

/*
 * Two buffers: one records the call under way while the other, a call
 * over, is written to flash.
 */
static uint8_t
    frames[2][CALLREC_MAX_FRAMES * CALLSTORE_FRAME_BYTES] CALLREC_MEMORY;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool enabled;
static bool recording;            /* A call in buffer cur */
static unsigned cur;              /* Buffer recording */
static struct callStoreInfo info; /* Of the call recording */
static uint16_t callId;           /* Of the call recording */
static long long lastFrame;       /* Tick of its last frame */
static bool pending;              /* Buffer !cur holds a call to write */
static struct callStoreInfo pendingInfo;
static volatile bool writing;

static pthread_t writer;
static bool writerUp;

/* Player */
static volatile uint32_t playSeq; /* Call asked for, 0: none */
static volatile uint8_t playReq;  /* Changes on each request */
static volatile bool playing;
static volatile uint32_t playingSeq;
static volatile uint16_t playedSec, lengthSec;
static pthread_t player;
static bool playerUp;
static oa_dec_t decoder;
static stream_sample_t pcm[2 * OA_FRAME_SAMPLES];
static uint8_t chunk[PLAY_CHUNK * CALLSTORE_FRAME_BYTES];

static void pack(uint8_t *out, const uint8_t *bits, uint8_t bfi)
{
    memset(out, 0, CALLSTORE_FRAME_BYTES);
    for (unsigned i = 0; i < OA_FRAME_BITS; i++) {
        if (bits[i])
            out[i / 8] |= (uint8_t)(1u << (i % 8));
    }
    if (bfi)
        out[CALLSTORE_FRAME_BYTES - 1] |= 0x80;
}

static uint8_t unpack(uint8_t *bits, const uint8_t *in)
{
    for (unsigned i = 0; i < OA_FRAME_BITS; i++)
        bits[i] = (in[i / 8] >> (i % 8)) & 1;
    return (in[CALLSTORE_FRAME_BYTES - 1] & 0x80) ? 1 : 0;
}

/** The call recording is over: hand it to the writer (under mutex) */
static void finish(void)
{
    if (recording && info.frames >= CALLREC_MIN_FRAMES && !pending) {
        pendingInfo = info;
        pending = true;
        cur ^= 1;
    }
    recording = false;
}

static void addTalker(uint32_t talker)
{
    if (talker == 0)
        return;
    for (unsigned i = 0; i < CALLSTORE_TALKERS; i++) {
        if (info.talkers[i] == talker)
            return;
        if (info.talkers[i] == 0) {
            info.talkers[i] = talker;
            return;
        }
    }
}

void tetraCallRec_frame(const uint8_t *bits, uint8_t bfi)
{
    uint16_t id, la;
    uint32_t dst, talker;
    bool group;

    if (!enabled)
        return;

    bool known = tetraStatus_callNow(&id, &dst, &talker, &group, &la);
    long long now = getTick();

    pthread_mutex_lock(&mutex);
    bool another = recording
                && ((known && id != callId)
                    || now - lastFrame > CALLREC_END_MS);
    if (another)
        finish();

    if (!recording) {
        if (pending) {
            /* Both buffers busy: this call is not kept */
            pthread_mutex_unlock(&mutex);
            return;
        }
        memset(&info, 0, sizeof(info));
        info.dst = dst;
        info.la = la;
        info.flags = group ? CALLSTORE_GROUP : 0;
        bool local;
        if (tetraStatus_timeNow(&info.year, &info.dayOfYear, &info.secOfDay,
                                &local)) {
            if (local)
                info.flags |= CALLSTORE_LOCAL_TIME;
        } else {
            info.year = 0;
        }
        callId = id;
        recording = true;
    }

    if (info.dst == 0)
        info.dst = dst;
    addTalker(talker);
    if (info.frames < CALLREC_MAX_FRAMES) {
        pack(&frames[cur][info.frames * CALLSTORE_FRAME_BYTES], bits, bfi);
        info.frames++;
    }
    lastFrame = now;
    pthread_mutex_unlock(&mutex);
}

static void *writeCalls(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&mutex);
        if (recording && getTick() - lastFrame > CALLREC_END_MS)
            finish();
        bool work = pending;
        struct callStoreInfo saving = pendingInfo;
        unsigned buf = cur ^ 1;
        writing = work;
        pthread_mutex_unlock(&mutex);

        if (work) {
            callStore_save(&saving, frames[buf]);
            pthread_mutex_lock(&mutex);
            pending = false;
            writing = false;
            pthread_mutex_unlock(&mutex);
        } else {
            sleepFor(0, 500);
        }
    }
    return NULL;
}

/** Start a thread with a stack of its own */
static bool spawn(pthread_t *t, size_t size, void *(*fn)(void *))
{
    pthread_attr_t attr;

    pthread_attr_init(&attr);
#ifdef __ZEPHYR__
    void *stack = malloc(size);
    if (stack == NULL)
        return false;
    pthread_attr_setstack(&attr, stack, size);
#else
    pthread_attr_setstacksize(&attr, size);
#endif
    bool ok = pthread_create(t, &attr, fn, NULL) == 0;
#ifdef __ZEPHYR__
    if (!ok)
        free(stack);
#endif
    return ok;
}

void tetraCallRec_enable(bool on)
{
    if (on && !writerUp)
        writerUp = spawn(&writer, WRITER_STACK, writeCalls);
    enabled = on && writerUp;
}

bool tetraCallRec_busy(void)
{
    return recording || writing;
}

/** Play one saved call, until its end, a new request or a call heard */
static void playCall(uint32_t seq, uint8_t req)
{
    struct callStoreInfo ci;
    uint32_t total = 0;

    for (uint16_t n = 0; callStore_info(n, &ci); n++) {
        if (ci.seq == seq) {
            total = ci.frames;
            break;
        }
    }
    if (total == 0)
        return;

    pathId path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_BEEP);
    if (audioPath_getStatus(path) != PATH_OPEN) {
        audioPath_release(path);
        return;
    }
    memset(pcm, 0, sizeof(pcm));
    streamId stream = audioStream_start(path, pcm, 2 * OA_FRAME_SAMPLES, 8000,
                                        STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (stream < 0) {
        audioPath_release(path);
        return;
    }

    oa_dec_init(&decoder, &oa_tables_tetra);
    playingSeq = seq;
    lengthSec = (uint16_t)(total * 30 / 1000);
    playedSec = 0;
    playing = true;
    outputStream_sync(stream, false);

    uint8_t bits[OA_FRAME_BITS];
    uint32_t pos = 0;
    while (pos < total && playReq == req && playSeq == seq
           && audioPath_getStatus(path) == PATH_OPEN) {
        int got = callStore_read(seq, pos, chunk, PLAY_CHUNK);
        if (got <= 0)
            break;
        for (int i = 0; i < got && playReq == req; i++) {
            stream_sample_t *out = outputStream_getIdleBuffer(stream);
            if (out == NULL)
                break;
            uint8_t bfi = unpack(bits, &chunk[i * CALLSTORE_FRAME_BYTES]);
            oa_dec_frame(&decoder, bits, bfi, out);
            outputStream_sync(stream, true);
        }
        pos += (uint32_t)got;
        playedSec = (uint16_t)(pos * 30 / 1000);
    }

    audioStream_stop(stream);
    audioPath_release(path);
    playing = false;
}

static void *playCalls(void *arg)
{
    (void)arg;
    uint8_t done = 0;

    for (;;) {
        uint8_t req = playReq;
        uint32_t seq = playSeq;
        if (req != done && seq != 0) {
            done = req;
            playCall(seq, req);
            if (playReq == req)
                playSeq = 0;
        } else {
            sleepFor(0, 20);
        }
    }
    return NULL;
}

void tetraCallRec_play(uint32_t seq)
{
    if (!playerUp)
        playerUp = spawn(&player, PLAYER_STACK, playCalls);
    playSeq = seq;
    playReq++;
}

void tetraCallRec_stop(void)
{
    playSeq = 0;
    playReq++;
}

bool tetraCallRec_playing(uint32_t *seq, uint16_t *played, uint16_t *length)
{
    *seq = playingSeq;
    *played = playedSec;
    *length = lengthSec;
    return playing;
}

#endif /* CONFIG_TETRA_CALLREC && CONFIG_TETRA_SPEECH */
