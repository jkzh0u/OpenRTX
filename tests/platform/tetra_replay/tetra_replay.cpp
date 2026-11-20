/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * TETRA replay: runs the radio's own TETRA mode (OpMode_TETRA.cpp, with
 * libtetra, the call follower, cell tracking, the talkgroup filter and the
 * receive log) on recorded carriers of a network, on a computer.
 *
 * Each carrier is 48 kHz complex 16-bit I/Q, all starting at the same time
 * (scripts/c62_replay.py cuts them from a wideband SDR recording). The
 * radio here stands in for the C62's receiver: it hears the carrier it is
 * tuned to, at a level set in dBm, through receiver noise, a channel
 * filter and an FM discriminator; a retune, even a 5 ms measurement of a
 * neighbour, switches the samples to that carrier from that moment, as on
 * the radio. Signal strength reads the level with some jitter.
 *
 * Levels: the C62 decodes about as an Es/N0 of (dBm + 100) dB here, from a
 * noise sweep matched to sp5wwp's measurement of a C62 at -92 dBm. A
 * carrier's own noise (the SDR's) stays in it.
 *
 * The frequency correction (AFC) is off: its loop gain is for the C62's
 * receive path, which this model does not reproduce.
 *
 * Usage (see scripts/c62_replay.py, which builds and runs it):
 *
 *   tetra_replay -c channels.bin -f MHz [-s seconds] [-q]
 *                -C MHz:dBm:file ... [-F MHz:t0:t1:dB ...]
 *
 *   -c  channel list image (scripts/chlist.py): names, filters, priorities
 *       and the TETRA channels cell tracking searches
 *   -f  frequency the radio is set to at the start, MHz
 *   -C  a carrier: frequency, level in dBm, and its I/Q file
 *   -F  fade a carrier by dB, linearly from t0 to t1 seconds, then held
 *       (negative dB weakens it)
 *   -s  stop after this many seconds
 *   -q  no receive log, the summary only
 */

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <tetra/tetra.h>
#include "core/chlist.h"
#include "core/state.h"
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "interfaces/cps_io.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "interfaces/radio.h"
#include "protocols/TETRA/TetraConfig.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraLog.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"
#include "rtx/OpMode_TETRA.hpp"
#include "rtx/rtx.h"

#define RATE 48000
#define BLOCK 1440 /* The mode's half buffer, 30 ms */
#define TAPS 31
#define CUTOFF_HZ 12500.0
#define NOISE_RSSI -125     /* dBm read where no carrier is */
#define RSSI_JITTER_DB 1.5f /* Spread of a signal strength reading */
#define NOISE_SIGMA 300.0f  /* Receiver noise, per I/Q component */

/* ------------------------------------------------------------------------
 * The network: recorded carriers
 */

struct Fade {
    double t0, t1;
    float db;
};

struct Carrier {
    uint32_t freq;
    float level; /* dBm at the start */
    std::vector<int16_t> iq;
    double power; /* Mean |iq|^2 */
    std::vector<Fade> fades;
    /* Results */
    double tuned;  /* Seconds received */
    double locked; /* Of which locked */
    unsigned frames, bad;
};

static std::vector<Carrier> carriers;
static size_t length; /* Samples of the shortest carrier */

static Carrier *carrierAt(uint32_t freq)
{
    for (auto &c : carriers) {
        /* The radio tunes within a few hundred Hz of a carrier */
        if (freq > c.freq ? freq - c.freq < 3000 : c.freq - freq < 3000)
            return &c;
    }
    return nullptr;
}

static float levelAt(const Carrier &c, double t)
{
    float l = c.level;
    for (const Fade &f : c.fades) {
        if (t <= f.t0)
            continue;
        double k = (f.t1 > f.t0) ? (t - f.t0) / (f.t1 - f.t0) : 1;
        l += f.db * (float)(k > 1 ? 1 : k);
    }
    return l;
}

/* ------------------------------------------------------------------------
 * The radio: time, tuning, receiver
 */

static size_t pos;          /* Next sample the stream gives */
static long long clockMs;   /* What getTick() says */
static rtxStatus_t rtx;     /* Passed to the mode, read by the radio */
static uint32_t tunedFreq;  /* What the radio is tuned to */
static bool quiet;

struct Tuning {
    size_t from; /* First sample */
    uint32_t freq;
};
static std::vector<Tuning> tunings;

static size_t sampleNow()
{
    return (size_t)(clockMs * (RATE / 1000));
}

static void tuneTo(uint32_t freq)
{
    size_t at = sampleNow();
    if (at < pos)
        at = pos;
    tunedFreq = freq;
    if (!tunings.empty() && tunings.back().from == at)
        tunings.back().freq = freq;
    else
        tunings.push_back({ at, freq });
}

static uint32_t tunedAt(size_t s)
{
    uint32_t f = 0;
    for (const Tuning &t : tunings) {
        if (t.from > s)
            break;
        f = t.freq;
    }
    return f;
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;

static float uniform()
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return ((rng >> 40) + 0.5f) / 16777216.0f;
}

static float gauss()
{
    return sqrtf(-2.0f * logf(uniform())) * cosf(6.2831853f * uniform());
}

/* Receiver chain state: one receiver, whatever it is tuned to */
static float taps[TAPS], histI[TAPS], histQ[TAPS];
static unsigned histPos;
static float lastI = 1, lastQ;

static void receiverInit()
{
    float sum = 0;
    for (int i = 0; i < TAPS; i++) {
        float t = i - (TAPS - 1) / 2.0f;
        float w = 0.54f - 0.46f * cosf(6.2831853f * i / (TAPS - 1));
        float x = 2.0f * CUTOFF_HZ / RATE;
        taps[i] = w * (t == 0 ? x
                              : sinf(3.14159265f * x * t) / (3.14159265f * t));
        sum += taps[i];
    }
    for (int i = 0; i < TAPS; i++)
        taps[i] /= sum;
}

/** Amplitude scale of a carrier for a level in dBm, given NOISE_SIGMA */
static float scaleFor(const Carrier &c, float dbm)
{
    /* Es/N0 = dBm + 100; N0 per Hz from the noise: sigma^2 * 2 / RATE */
    double n0 = (double)NOISE_SIGMA * NOISE_SIGMA * 2.0 / RATE;
    double es = n0 * 18000.0 * pow(10.0, (dbm + 100.0) / 10.0);
    return (float)sqrt(es / c.power);
}

/** The next discriminator sample, as the C62's flat receive path gives */
static int16_t receive(size_t s)
{
    uint32_t f = tunedAt(s);
    Carrier *c = carrierAt(f);
    float si = NOISE_SIGMA * gauss(), sq = NOISE_SIGMA * gauss();

    if (c != nullptr && s < length) {
        float a = scaleFor(*c, levelAt(*c, (double)s / RATE));
        si += a * c->iq[2 * s];
        sq += a * c->iq[2 * s + 1];
    }

    histI[histPos] = si;
    histQ[histPos] = sq;
    float yi = 0, yq = 0;
    for (int k = 0; k < TAPS; k++) {
        unsigned j = (histPos + TAPS - k) % TAPS;
        yi += taps[k] * histI[j];
        yq += taps[k] * histQ[j];
    }
    histPos = (histPos + 1) % TAPS;

    float re = yi * lastI + yq * lastQ, im = yq * lastI - yi * lastQ;
    lastI = yi;
    lastQ = yq;
    float hz = atan2f(im, re) * RATE / 6.2831853f;
    float v = hz / 18000.0f * 0.25f * 32768.0f;
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
}

/* ------------------------------------------------------------------------
 * Interfaces the mode uses, in place of the radio's
 */

state_t state;
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

long long getTick()
{
    return clockMs;
}

void delayMs(unsigned int ms)
{
    clockMs += ms;
}

void delayUs(unsigned int us)
{
    clockMs += us / 1000;
}

void sleepFor(unsigned int s, unsigned int ms)
{
    clockMs += 1000ll * s + ms;
}

void platform_ledOn(led_t led)
{
    (void)led;
}

void platform_ledOff(led_t led)
{
    (void)led;
}

void radio_enableRx()
{
    tuneTo(rtx.rxFrequency);
}

void radio_disableRtx()
{
}

void radio_updateConfiguration()
{
    tuneTo(rtx.rxFrequency);
}

void radio_setRxOffset(int32_t hz)
{
    (void)hz; /* AFC is off in the replay */
}

rssi_t radio_getRssi()
{
    Carrier *c = carrierAt(tunedFreq);
    float l = c ? levelAt(*c, clockMs / 1000.0) : NOISE_RSSI;
    if (l < NOISE_RSSI)
        l = NOISE_RSSI;
    return (rssi_t)lrintf(l + RSSI_JITTER_DB * gauss());
}

rssi_t rtx_getRssi()
{
    return radio_getRssi();
}

static stream_sample_t streamBuf[BLOCK];

pathId audioPath_request(enum AudioSource source, enum AudioSink sink,
                         enum AudioPriority prio)
{
    (void)source;
    (void)sink;
    (void)prio;
    return 1;
}

enum PathStatus audioPath_getStatus(const pathId id)
{
    (void)id;
    return PATH_OPEN;
}

void audioPath_release(const pathId id)
{
    (void)id;
}

streamId audioStream_start(const pathId path, stream_sample_t *const buf,
                           const size_t length, const uint32_t sampleRate,
                           const uint8_t mode)
{
    (void)path;
    (void)buf;
    (void)length;
    (void)sampleRate;
    (void)mode;
    return 1;
}

void audioStream_stop(const streamId id)
{
    (void)id;
}

void audioStream_terminate(const streamId id)
{
    (void)id;
}

/* The next 30 ms of samples, once they would have been captured */
dataBlock_t inputStream_getData(streamId id)
{
    (void)id;
    dataBlock_t b = { nullptr, 0 };

    if (pos + BLOCK > length)
        return b;
    for (size_t i = 0; i < BLOCK; i++)
        streamBuf[i] = receive(pos + i);
    pos += BLOCK;

    long long end = (long long)(pos / (RATE / 1000));
    if (clockMs < end)
        clockMs = end;

    /* Account the block to the carrier it was tuned to at its end */
    Carrier *c = carrierAt(tunedAt(pos - 1));
    if (c != nullptr) {
        c->tuned += (double)BLOCK / RATE;
        if (tetraStatus_sync())
            c->locked += (double)BLOCK / RATE;
    }

    b.data = streamBuf;
    b.len = BLOCK;
    return b;
}

/* The receive log, as the radio writes it, to stdout */
void tetraLog_printf(long long now, const char *fmt, ...)
{
    if (quiet)
        return;
    va_list ap;
    printf("%6lu.%03u  ", (unsigned long)(now / 1000),
           (unsigned)(now % 1000));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* Speech: counted per carrier, as heard (speaker logic not modelled) */
static struct tetra_speech_ws speechWs;

void tetraSpeech_start(void)
{
}

void tetraSpeech_stop(void)
{
}

void tetraSpeech_traffic(const struct tetra_traffic *t)
{
    if (t->encrypted || t->circuit_type != 0)
        return;
    struct tetra_speech sp;
    tetra_speech_decode(&speechWs, t, &sp);
    Carrier *c = carrierAt(tunedFreq);
    if (c != nullptr) {
        c->frames += 2;
        c->bad += sp.bfi[0] + sp.bfi[1];
    }
}

void tetraSpeech_alert(void)
{
}

void tetraSpeech_follow(uint8_t tn)
{
    (void)tn;
}

void tetraSpeech_stats(uint32_t *frames, uint32_t *bad, uint32_t *otherTs)
{
    *frames = *bad = *otherTs = 0;
}

void tetraSpeech_playStats(uint32_t *played, uint32_t *busy, uint32_t *noStream,
                           uint32_t *noBuffer, uint32_t *bursts,
                           uint32_t *streams)
{
    *played = *busy = *noStream = *noBuffer = *bursts = *streams = 0;
}

/* ------------------------------------------------------------------------
 * Main
 */

static bool loadCarrier(const char *arg)
{
    double mhz;
    float dbm;
    char path[512];
    if (sscanf(arg, "%lf:%f:%511s", &mhz, &dbm, path) != 3)
        return false;

    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        perror(path);
        exit(1);
    }
    Carrier c;
    c.freq = (uint32_t)lrint(mhz * 1e6);
    c.level = dbm;
    c.tuned = c.locked = 0;
    c.frames = c.bad = 0;
    int16_t buf[8192];
    size_t n;
    while ((n = fread(buf, 2, 8192, f)) > 0)
        c.iq.insert(c.iq.end(), buf, buf + n);
    fclose(f);
    double p = 0;
    for (size_t i = 0; i < c.iq.size(); i++)
        p += (double)c.iq[i] * c.iq[i];
    c.power = c.iq.empty() ? 1 : p / (c.iq.size() / 2);
    carriers.push_back(c);
    return true;
}

static bool addFade(const char *arg)
{
    double mhz, t0, t1;
    float db;
    if (sscanf(arg, "%lf:%lf:%lf:%f", &mhz, &t0, &t1, &db) != 4)
        return false;
    Carrier *c = carrierAt((uint32_t)lrint(mhz * 1e6));
    if (c == nullptr) {
        fprintf(stderr, "-F %s: no such carrier (give -C first)\n", arg);
        exit(2);
    }
    c->fades.push_back({ t0, t1, db });
    return true;
}

static void usage(const char *name)
{
    fprintf(stderr,
            "usage: %s -c channels.bin -f MHz [-s seconds] [-q]\n"
            "          -C MHz:dBm:iq_file ... [-F MHz:t0:t1:dB ...]\n",
            name);
    exit(2);
}

int main(int argc, char **argv)
{
    static uint32_t image[0x90000 / 4];
    double startMhz = 0, stopS = 1e9;
    int opt;

    while ((opt = getopt(argc, argv, "c:f:C:F:s:q")) != -1) {
        switch (opt) {
            case 'c': {
                FILE *f = fopen(optarg, "rb");
                size_t n = f ? fread(image, 1, sizeof(image), f) : 0;
                if (f)
                    fclose(f);
                int ret = chlist_open(image, n);
                if (ret != 0) {
                    fprintf(stderr, "%s: not a usable channel list (%d)\n",
                            optarg, ret);
                    return 1;
                }
                break;
            }
            case 'f':
                startMhz = atof(optarg);
                break;
            case 'C':
                if (!loadCarrier(optarg))
                    usage(argv[0]);
                break;
            case 'F':
                if (!addFade(optarg))
                    usage(argv[0]);
                break;
            case 's':
                stopS = atof(optarg);
                break;
            case 'q':
                quiet = true;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (carriers.empty() || startMhz == 0)
        usage(argv[0]);

    length = (size_t)-1;
    for (const Carrier &c : carriers) {
        if (c.iq.size() / 2 < length)
            length = c.iq.size() / 2;
    }
    if ((double)length / RATE > stopS)
        length = (size_t)(stopS * RATE);

    receiverInit();
    state.settings = default_settings;
    state.settings.tetraAfcOff = 1;
    tetraConfig_set(true, true, true, true);

    memset(&rtx, 0, sizeof(rtx));
    rtx.opMode = OPMODE_TETRA;
    rtx.rxFrequency = (freq_t)lrint(startMhz * 1e6);
    rtx.txFrequency = rtx.rxFrequency;

    OpMode_TETRA mode;
    mode.enable();
    bool first = true;
    while (pos + BLOCK <= length) {
        mode.update(&rtx, first);
        first = false;
    }
    mode.disable();

    /* Summary */
    printf("\n%.1f s replayed. Time on each carrier, and how it went:\n",
           (double)length / RATE);
    for (const Carrier &c : carriers) {
        char name[32] = "";
        channel_t ch;
        for (uint16_t i = 0; cps_readChannel(&ch, i) == 0; i++) {
            if (ch.rx_frequency == c.freq) {
                snprintf(name, sizeof(name), "%s", ch.name);
                break;
            }
        }
        printf("  %.4f %-14s %6.1f dBm -> %6.1f dBm  tuned %6.1f s, locked "
               "%5.1f%%, speech %u frames (%u bad)\n",
               c.freq / 1e6, name, c.level,
               levelAt(c, (double)length / RATE), c.tuned,
               c.tuned > 0 ? 100.0 * c.locked / c.tuned : 0.0, c.frames,
               c.bad);
    }
    return 0;
}
