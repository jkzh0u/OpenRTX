/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Host harness for the C62 TETRA receive chain: runs TetraDemod.c and
 * libtetra on 48 kHz discriminator audio (a C62 baseband dump or a
 * simulation) the way OpMode_TETRA does, and prints figures of merit.
 *
 *   cc -O2 -I openrtx/include -I <libtetra>/include -I <libtetra>/build/generated
 *      tests/platform/c62_tetra_host.c openrtx/src/protocols/TETRA/TetraDemod.c
 *      <libtetra>/build/libtetra.a -lm -o c62_tetra_host
 *   c62_tetra_host [-r] [-v] [-e] input.wav|input.s16
 *
 *  -r  on a demodulator jump call tetra_tmo_resync() instead of re-init
 *  -e  do not seed the equalizer with CONFIG_TETRA_EQ_INIT
 *  -v  print libtetra events
 *  -t  trace the demodulator every 0.48 s
 *  -g  input gain (float), applied before the demodulator
 *  -P  name=value: set a demodulator parameter (tetraDemodParams)
 *  -q  the input is complex 16-bit I/Q at 48 kHz (tetra_tmo_gen -r 48000)
 *      instead of discriminator audio: low-pass and FM-discriminate it,
 *      a rough stand-in for the C62's flat receive path
 *  -N  Es/N0, dB in 18 kHz: with -q, add white noise first (seeded)
 *  -m  minimum blocks per second passing the CRC, and
 *  -M  maximum percentage of speech frames failing the CRC: exit 1 if
 *      either is missed, for automated tests
 *  -a  prefix: write the clear speech of each timeslot to prefix_tsN.wav
 *      (8 kHz mono, silence between transmissions so that it keeps the
 *      recording's time). Needs OpenACELP: add -DHOST_SPEECH,
 *      -I <openacelp>/include and <openacelp>/src/oa_{dsp,bits,dec,
 *      tables_tetra}.c to the build.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tetra/tetra.h>
#include "protocols/TETRA/TetraDemod.h"
#ifdef HOST_SPEECH
#include <openacelp.h>
#endif

#define HALF_SAMPLES 1440

static const float eqInit[TETRA_DEMOD_EQ_TAPS] = { 0.007f,  -0.004f, 0.036f,
                                                   -0.185f, 1.149f,  -0.187f,
                                                   0.033f,  -0.001f, 0.0f };

static struct tetraDemod demod;
static struct tetra_tmo_rx tmo;
static int verbose;
static unsigned nTraffic, nCalls, nSds;
#define ERR_LOG_MAX 100000
static unsigned short errLog[ERR_LOG_MAX];
static unsigned nFrames, nBadFrames; /* Clear speech frames, CRC failed */
static struct tetra_speech_ws speechWs;
static int trace;
static uint32_t crcTotal;

/* -q: I/Q input, discriminated here */
#define IQ_TAPS 31
#define IQ_CUTOFF_HZ 12500.0
static int iqInput;
static float noiseDb = -1; /* -N, < 0 for none */
static float iqTaps[IQ_TAPS];
static float iqHistI[IQ_TAPS], iqHistQ[IQ_TAPS];
static unsigned iqPos;
static float lastI = 1, lastQ;
static float noiseSigma = -1; /* Set from the first block */
static uint64_t rngState = 0x2545f4914f6cdd1dull;

static float uniform(void)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return ((rngState >> 40) + 0.5f) / 16777216.0f;
}

static float gauss(void)
{
    return sqrtf(-2.0f * logf(uniform())) * cosf(6.2831853f * uniform());
}

static void iqInit(void)
{
    /* Windowed sinc low-pass: the channel filter */
    float sum = 0;
    for (int i = 0; i < IQ_TAPS; i++) {
        float t = i - (IQ_TAPS - 1) / 2.0f;
        float w = 0.54f - 0.46f * cosf(6.2831853f * i / (IQ_TAPS - 1));
        float x = 2.0f * IQ_CUTOFF_HZ / 48000.0f;
        iqTaps[i] =
            w * (t == 0 ? x : sinf(3.14159265f * x * t) / (3.14159265f * t));
        sum += iqTaps[i];
    }
    for (int i = 0; i < IQ_TAPS; i++)
        iqTaps[i] /= sum;
}

/**
 * Read up to n samples of discriminator audio: directly, or from I/Q with
 * -q (noise, channel filter, FM discriminator, scaled as
 * scripts/c62_tetra_sim.py does).
 */
static size_t readInput(int16_t *out, size_t n, FILE *f)
{
    if (!iqInput)
        return fread(out, 2, n, f);

    static int16_t iq[2 * HALF_SAMPLES];
    size_t got = fread(iq, 4, n, f);

    if (noiseDb >= 0 && noiseSigma < 0 && got > 0) {
        double p = 0;
        for (size_t i = 0; i < got; i++)
            p += (double)iq[2 * i] * iq[2 * i]
               + (double)iq[2 * i + 1] * iq[2 * i + 1];
        p /= got;
        double n0 = p / 18000.0 / pow(10.0, noiseDb / 10.0);
        noiseSigma = (float)sqrt(n0 * 48000.0 / 2.0);
    }

    for (size_t i = 0; i < got; i++) {
        float si = iq[2 * i], sq = iq[2 * i + 1];
        if (noiseSigma > 0) {
            si += noiseSigma * gauss();
            sq += noiseSigma * gauss();
        }
        iqHistI[iqPos] = si;
        iqHistQ[iqPos] = sq;
        float yi = 0, yq = 0;
        for (int k = 0; k < IQ_TAPS; k++) {
            unsigned j = (iqPos + IQ_TAPS - k) % IQ_TAPS;
            yi += iqTaps[k] * iqHistI[j];
            yq += iqTaps[k] * iqHistQ[j];
        }
        iqPos = (iqPos + 1) % IQ_TAPS;

        /* Instantaneous frequency, Hz, to full scale per 18 kHz x 0.25 */
        float re = yi * lastI + yq * lastQ, im = yq * lastI - yi * lastQ;
        lastI = yi;
        lastQ = yq;
        float hz = atan2f(im, re) * 48000.0f / 6.2831853f;
        float v = hz / 18000.0f * 0.25f * 32768.0f;
        out[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
    }

    return got;
}
static uint32_t samplesIn; /* 48 kHz input samples decoded so far */

#ifdef HOST_SPEECH
/* Clear speech of each timeslot, decoded and written to a WAV file */
static const char *audioPrefix;
static struct {
    FILE *f;
    oa_dec_t dec;
    uint32_t written; /* 8 kHz samples */
} audio[4];

static void writeWavHeader(FILE *f, uint32_t samples)
{
    uint32_t data = samples * 2, riff = 36 + data, fmtLen = 16, rate = 8000,
             byteRate = 16000;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtLen, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    fseek(f, 0, SEEK_END);
}

static void playSpeech(unsigned tn, const struct tetra_speech *sp)
{
    if (audioPrefix == NULL || tn < 1 || tn > 4)
        return;
    tn--;
    if (audio[tn].f == NULL) {
        char path[512];
        snprintf(path, sizeof(path), "%s_ts%u.wav", audioPrefix, tn + 1);
        audio[tn].f = fopen(path, "wb");
        if (audio[tn].f == NULL) {
            perror(path);
            exit(1);
        }
        writeWavHeader(audio[tn].f, 0);
        oa_dec_init(&audio[tn].dec, &oa_tables_tetra);
    }

    /* Silence up to now, less the two frames about to be written */
    static const int16_t zero[OA_FRAME_SAMPLES];
    uint32_t now = samplesIn / 6;
    while (audio[tn].written + 3 * OA_FRAME_SAMPLES <= now) {
        fwrite(zero, 2, OA_FRAME_SAMPLES, audio[tn].f);
        audio[tn].written += OA_FRAME_SAMPLES;
    }

    for (int i = 0; i < 2; i++) {
        int16_t pcm[OA_FRAME_SAMPLES];
        oa_dec_frame(&audio[tn].dec, sp->bits[i], sp->bfi[i], pcm);
        fwrite(pcm, 2, OA_FRAME_SAMPLES, audio[tn].f);
        audio[tn].written += OA_FRAME_SAMPLES;
    }
}

static void closeAudio(void)
{
    for (unsigned i = 0; i < 4; i++) {
        if (audio[i].f == NULL)
            continue;
        writeWavHeader(audio[i].f, audio[i].written);
        fclose(audio[i].f);
        printf("speech TN%u: %.1f s written\n", i + 1,
               audio[i].written / 8000.0);
    }
}
#endif

static void onEvent(void *user, const struct tetra_event *ev)
{
    (void)user;
    switch (ev->type) {
        case TETRA_EV_TRAFFIC: {
            nTraffic++;
            const struct tetra_traffic *t = &ev->u.traffic;
            if (!t->encrypted && t->circuit_type == 0) {
                struct tetra_speech sp;
                tetra_speech_decode(&speechWs, t, &sp);
                nFrames += 2;
                nBadFrames += sp.bfi[0] + sp.bfi[1];
#ifdef HOST_SPEECH
                playSpeech(t->time.tn, &sp);
#endif
            }
            break;
        }
        case TETRA_EV_CALL:
            nCalls++;
            break;
        case TETRA_EV_SDS:
            nSds++;
            break;
        default:
            break;
    }
    if (verbose && ev->type != TETRA_EV_TRAFFIC)
        printf("ev %d\n", ev->type);
}

static void initTmo(void)
{
    struct tetra_rx_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cb = onEvent;
    cfg.promiscuous = true;
    tetra_tmo_init(&tmo, &cfg);
}

/* "name=value": set a tetraDemodParams field */
static bool setParam(const char *arg)
{
    static const struct {
        const char *name;
        float *field;
    } fields[] = {
        { "costAlpha", &tetraDemodParams.costAlpha },
        { "timingKp", &tetraDemodParams.timingKp },
        { "timingKi", &tetraDemodParams.timingKi },
        { "fitAlpha", &tetraDemodParams.fitAlpha },
        { "eqMu", &tetraDemodParams.eqMu },
        { "softScale", &tetraDemodParams.softScale },
        { "eqBiasMu", &tetraDemodParams.eqBiasMu },
        { "fitHold", &tetraDemodParams.fitHold },
        { "acqEnergy", &tetraDemodParams.acqEnergy },
        { "okHold", &tetraDemodParams.okHold },
        { "ctxMu", &tetraDemodParams.ctxMu },
        { "ctxSpan", &tetraDemodParams.ctxSpan },
    };
    const char *eq = strchr(arg, '=');
    if (eq == NULL)
        return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (strlen(fields[i].name) == (size_t)(eq - arg)
            && !strncmp(arg, fields[i].name, eq - arg)) {
            *fields[i].field = strtof(eq + 1, NULL);
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    int useResync = 0, seedEq = 1, opt;
    float inGain = 1.0f;
    float minCrc = -1, maxBad = -1;
    while ((opt = getopt(argc, argv, "rvteg:P:a:qN:m:M:")) != -1) {
        switch (opt) {
            case 'r':
                useResync = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 't':
                trace = 1;
                break;
            case 'e':
                seedEq = 0;
                break;
            case 'g':
                inGain = strtof(optarg, NULL);
                break;
            case 'q':
                iqInput = 1;
                break;
            case 'N':
                noiseDb = strtof(optarg, NULL);
                break;
            case 'm':
                minCrc = strtof(optarg, NULL);
                break;
            case 'M':
                maxBad = strtof(optarg, NULL);
                break;
            case 'a':
#ifdef HOST_SPEECH
                audioPrefix = optarg;
                break;
#else
                fprintf(stderr, "-a: built without -DHOST_SPEECH\n");
                return 2;
#endif
            case 'P':
                if (!setParam(optarg)) {
                    fprintf(stderr, "bad -P %s\n", optarg);
                    return 2;
                }
                break;
            default:
                fprintf(stderr, "usage: %s [-r] [-v] [-e] [-g gain] in\n",
                        argv[0]);
                return 2;
        }
    }
    if (optind >= argc)
        return 2;
    FILE *f = fopen(argv[optind], "rb");
    if (!f) {
        perror(argv[optind]);
        return 1;
    }
    /* WAV: skip to the data chunk; raw s16 otherwise */
    char hdr[12];
    if (fread(hdr, 1, 12, f) == 12 && !memcmp(hdr, "RIFF", 4)) {
        char id[4];
        uint32_t len;
        while (fread(id, 1, 4, f) == 4 && fread(&len, 4, 1, f) == 1) {
            if (!memcmp(id, "data", 4))
                break;
            fseek(f, len, SEEK_CUR);
        }
    } else {
        fseek(f, 0, SEEK_SET);
    }

    tetraDemod_init(&demod);
    if (seedEq)
        tetraDemod_setEqualizer(&demod, eqInit);
    initTmo();

    static int16_t buf[HALF_SAMPLES];
    static int8_t soft[HALF_SAMPLES * 3 / 4 + 8];
    uint32_t jumps = 0, samples = 0, inits = 1, lastCrc = 0;
    double errSum = 0;
    unsigned errN = 0, syncBlocks = 0, blocks = 0;
    size_t n;
    if (iqInput)
        iqInit();
    while ((n = readInput(buf, HALF_SAMPLES, f)) > 0) {
        if (inGain != 1.0f) {
            for (size_t i = 0; i < n; i++) {
                float v = buf[i] * inGain;
                buf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
            }
        }
        size_t k = tetraDemod_process(&demod, buf, n, soft);
        uint32_t j = tetraDemod_reacquisitions(&demod);
        if (j != jumps) {
            jumps = j;
            if (useResync) {
                tetra_tmo_resync(&tmo);
            } else {
                crcTotal += tetra_tmo_crc_ok(&tmo);
                initTmo();
                inits++;
            }
        }
        samplesIn = samples + n;
        tetra_tmo_push_soft(&tmo, soft, k);
        /* As OpMode_TETRA: good blocks keep the demodulator's lock */
        uint32_t crcNow = crcTotal + tetra_tmo_crc_ok(&tmo);
        if (crcNow != lastCrc) {
            lastCrc = crcNow;
            tetraDemod_signalOk(&demod);
        }
        samples += n;
        blocks++;
        if (samples > 48000) {
            errSum += tetraDemod_phaseError(&demod);
            errN++;
            if (errN <= ERR_LOG_MAX)
                errLog[errN - 1] = tetraDemod_phaseError(&demod);
        }
        if (tetra_tmo_locked(&tmo))
            syncBlocks++;
        if (trace && blocks % 16 == 0)
            printf("%6.2f err %3u fast %3.0f cfo %5d gain %.3g reacq %u "
                   "slips %u lock %d crc %u eq %.3f %.3f %.3f\n",
                   samples / 48000.0, tetraDemod_phaseError(&demod),
                   100 * sqrtf(demod.errFast) / 0.785398f,
                   tetraDemod_carrierOffset(&demod), demod.gain,
                   demod.reacquired, demod.slips, tetra_tmo_locked(&tmo),
                   tetra_tmo_crc_ok(&tmo), demod.eq[3], demod.eq[4],
                   demod.eq[5]);
    }
    crcTotal += tetra_tmo_crc_ok(&tmo);
    /* Steady state: mean phase error over the second half of the run */
    unsigned logged = errN < ERR_LOG_MAX ? errN : ERR_LOG_MAX;
    double errLate = 0;
    for (unsigned i = logged / 2; i < logged; i++)
        errLate += errLog[i];
    if (logged > 1)
        errLate /= logged - logged / 2;
    double secs = samples / 48000.0;
    printf("secs %.1f crc %u crc/s %.1f sync %.0f%% err %.1f%% "
           "reacq %u slips %u inits %u realign %u cfo %d clk %d "
           "traffic %u calls %u sds %u eq %.3f %.3f %.3f biasHz %.0f "
           "frames %u bad %u errLate %.2f%%\n",
           secs, crcTotal, crcTotal / secs, 100.0 * syncBlocks / blocks,
           errN ? errSum / errN : 0, demod.reacquired, demod.slips, inits,
           tetra_tmo_realigned(&tmo, NULL), tetraDemod_carrierOffset(&demod),
           tetraDemod_clockOffset(&demod), nTraffic, nCalls, nSds, demod.eq[3],
           demod.eq[4], demod.eq[5], demod.eqBias * 18000 / (2 * 3.14159265),
           nFrames, nBadFrames, errLate);
#ifdef HOST_SPEECH
    closeAudio();
#endif

    /* Limits for automated tests */
    int fail = 0;
    if (minCrc >= 0 && crcTotal / secs < minCrc) {
        printf("FAIL: %.1f blocks/s passed the CRC, below %.1f\n",
               crcTotal / secs, minCrc);
        fail = 1;
    }
    double badPct = nFrames ? 100.0 * nBadFrames / nFrames : 100.0;
    if (maxBad >= 0 && badPct > maxBad) {
        printf("FAIL: %.1f%% of %u speech frames bad, above %.1f%%\n", badPct,
               nFrames, maxBad);
        fail = 1;
    }
    return fail;
}
