/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <string.h>
#include "protocols/DMR/DmrDemod.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SPS DMR_DEMOD_SPS
#define TAPS DMR_DEMOD_RRC_TAPS
#define MASK (DMR_DEMOD_RING - 1)

/* Symbols of a burst, and where its sync field ends */
#define BURST_SYMBOLS 132
#define CACH_SYMBOLS 12
#define SYNC_SYMBOLS 24
#define SYNC_LAST 77 /* Symbol index of the last sync symbol */

/* Samples between bursts: one timeslot (30 ms), or a frame (60 ms) for a
 * single transmitter's bursts */
#define PERIOD_BS (144 * SPS)
#define PERIOD_SINGLE (288 * SPS)

/* Sync search */
#define SEARCH_SCORE 0.8f /* Correlation to start a candidate, unlocked */
#define TRACK_SCORE 0.6f  /* Correlation of a sync where one is expected */
#define SEARCH_ERRORS 0   /* Sync symbols of the wrong sign, unlocked */
#define TRACK_ERRORS 4
/* RMS spread of the sync symbols' magnitude, as a fraction of the level:
 * they are all outer symbols, where noise has any magnitude */
#define SEARCH_SPREAD 0.3f
#define TRACK_SPREAD 0.5f
#define PEAK_SAMPLES 80 /* Keep looking for a better peak this long */
#define WINDOW 8        /* Samples around the expected sync, locked */

/* Bursts without a sync before the lock is lost: a voice superframe has
 * one in 6 bursts of its timeslot */
#define MISSED_BS 13
#define MISSED_SINGLE 7

static float rrc[TAPS];
static bool rrcReady;

/*
 * Sync patterns, voice: the data ones are the same with every symbol
 * inverted. Base station, mobile station, direct mode timeslot 1 and 2.
 */
static const uint64_t syncPattern[4] = {
    0x755FD7DF75F7ull,
    0x7F7D5DD57DFDull,
    0x5D577F7757FFull,
    0x7DFFD5F55D5Full,
};
static const enum dmrSync syncFamily[4] = {
    DMR_SYNC_BS,
    DMR_SYNC_MS,
    DMR_SYNC_TDMA1,
    DMR_SYNC_TDMA2,
};
static int8_t syncSymbol[4][SYNC_SYMBOLS]; /* +1 or -1 (+3 or -3) */
static int syncSum[4];

static void initTables(void)
{
    const double a = 0.2;

    if (rrcReady)
        return;

    double sum = 0;
    for (int i = 0; i < TAPS; i++) {
        double t = (double)(i - TAPS / 2) / SPS;
        double h;
        if (fabs(t) < 1e-9) {
            h = 1.0 - a + 4.0 * a / M_PI;
        } else if (fabs(fabs(t) - 1.0 / (4.0 * a)) < 1e-9) {
            h = a / sqrt(2.0)
              * ((1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * a))
                 + (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * a)));
        } else {
            h = (sin(M_PI * t * (1.0 - a))
                 + 4.0 * a * t * cos(M_PI * t * (1.0 + a)))
              / (M_PI * t * (1.0 - (4.0 * a * t) * (4.0 * a * t)));
        }
        rrc[i] = (float)h;
        sum += h;
    }
    for (int i = 0; i < TAPS; i++)
        rrc[i] = (float)(rrc[i] / sum);

    for (int f = 0; f < 4; f++) {
        syncSum[f] = 0;
        for (int j = 0; j < SYNC_SYMBOLS; j++) {
            /* Dibit j, first sent first: the high bit is the sign */
            int msb = (int)((syncPattern[f] >> (47 - 2 * j)) & 1);
            syncSymbol[f][j] = msb ? -1 : 1;
            syncSum[f] += syncSymbol[f][j];
        }
    }

    rrcReady = true;
}

/** Filtered sample at absolute index i */
static inline float at(const struct dmrDemod *d, uint32_t i)
{
    return d->ring[i & MASK];
}

/** a is after b */
static inline bool after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

struct syncFit {
    float score;  /* Correlation, signed: > 0 the voice pattern */
    float level;  /* Outer symbol level, signed like score */
    float dc;
    int errors;   /* Symbols of the wrong sign */
    float spread; /* RMS spread of the magnitudes / level */
};

/**
 * Correlation of the 24 symbols ending at sample end with pattern f, and
 * the level and DC offset that fit them best.
 */
static struct syncFit fitSync(const struct dmrDemod *d, uint32_t end, int f)
{
    struct syncFit r = { 0, 0, 0, SYNC_SYMBOLS, 1 };
    float y[SYNC_SYMBOLS];
    float sy = 0, suy = 0;

    for (int j = 0; j < SYNC_SYMBOLS; j++) {
        y[j] = at(d, end - (uint32_t)((SYNC_SYMBOLS - 1 - j) * SPS));
        sy += y[j];
        suy += (float)syncSymbol[f][j] * y[j];
    }

    float mean = sy / SYNC_SYMBOLS;
    float dev = 0;
    for (int j = 0; j < SYNC_SYMBOLS; j++)
        dev += fabsf(y[j] - mean);
    if (dev <= 0)
        return r;

    float su = (float)syncSum[f];
    r.score = (suy - mean * su) / dev;

    /* Least squares fit y = dc + level * symbol */
    float den = SYNC_SYMBOLS - su * su / SYNC_SYMBOLS;
    r.level = (suy - su * sy / SYNC_SYMBOLS) / den;
    r.dc = (sy - r.level * su) / SYNC_SYMBOLS;

    r.errors = 0;
    float spread = 0, mag = fabsf(r.level);
    for (int j = 0; j < SYNC_SYMBOLS; j++) {
        float v = (y[j] - r.dc) * r.level * (float)syncSymbol[f][j];
        if (v <= 0)
            r.errors++;
        float e = fabsf(y[j] - r.dc) - mag;
        spread += e * e;
    }
    r.spread = (mag > 0) ? sqrtf(spread / SYNC_SYMBOLS) / mag : 1;

    return r;
}

static uint32_t periodOf(enum dmrSync s)
{
    return (s == DMR_SYNC_BS) ? PERIOD_BS : PERIOD_SINGLE;
}

static void takeSync(struct dmrDemod *d, uint32_t at, int f,
                     const struct syncFit *fit)
{
    d->syncAt = at;
    d->syncFound = true;
    d->family = syncFamily[f];
    d->syncSign = (fit->score > 0) ? 1 : -1;
    d->syncScore = fabsf(fit->score);
    d->level = fabsf(fit->level);
    d->dc = fit->dc;
}

/** Slice the burst whose sync field ends at d->syncAt, and hand it over */
static void emitBurst(struct dmrDemod *d)
{
    struct dmrBurst b;
    float inv = d->inverted ? -1.0f : 1.0f;
    float err = 0;

    memset(&b, 0, sizeof(b));
    if (d->level <= 0)
        return;

    for (int k = -CACH_SYMBOLS; k < BURST_SYMBOLS; k++) {
        uint32_t i = d->syncAt + (uint32_t)((k - SYNC_LAST) * SPS);
        float v = inv * (at(d, i) - d->dc) / d->level;
        uint8_t sign = (v < 0) ? 1 : 0;
        uint8_t outer = (fabsf(v) > 2.0f / 3.0f) ? 1 : 0;
        float ideal = (outer ? 1.0f : 1.0f / 3.0f) * (sign ? -1.0f : 1.0f);

        if (k < 0) {
            b.cach[2 * (k + CACH_SYMBOLS)] = sign;
            b.cach[2 * (k + CACH_SYMBOLS) + 1] = outer;
        } else {
            b.bits[2 * k] = sign;
            b.bits[2 * k + 1] = outer;
            err += (v - ideal) * (v - ideal);
        }
    }

    b.sync = d->syncFound ? d->family : DMR_SYNC_NONE;
    b.voice = d->syncFound && ((d->syncSign > 0) != d->inverted);
    b.cachValid = (d->family == DMR_SYNC_BS);
    b.syncScore = d->syncFound ? d->syncScore : 0;
    b.level = d->level;
    b.dc = d->dc;
    b.error = sqrtf(err / BURST_SYMBOLS);
    b.index = d->bursts++;

    if (d->onBurst != NULL)
        d->onBurst(d->user, &b);
}

void dmrDemod_init(struct dmrDemod *d, dmrDemodBurstFn onBurst, void *user)
{
    initTables();
    memset(d, 0, sizeof(*d));
    d->onBurst = onBurst;
    d->user = user;
}

void dmrDemod_reset(struct dmrDemod *d)
{
    dmrDemodBurstFn fn = d->onBurst;
    void *user = d->user;
    bool inverted = d->inverted;

    dmrDemod_init(d, fn, user);
    d->inverted = inverted;
}

void dmrDemod_setInverted(struct dmrDemod *d, bool inverted)
{
    d->inverted = inverted;
}

bool dmrDemod_locked(const struct dmrDemod *d)
{
    return d->locked;
}

/** Unlocked: look for any sync at the newest sample */
static void search(struct dmrDemod *d, uint32_t cur)
{
    /* Enough samples for a burst before this sync */
    if (cur < (uint32_t)((CACH_SYMBOLS + SYNC_LAST) * SPS + TAPS))
        return;

    for (int f = 0; f < 4; f++) {
        struct syncFit fit = fitSync(d, cur, f);
        float s = fabsf(fit.score);
        if (s < SEARCH_SCORE || fit.errors > SEARCH_ERRORS
            || fit.spread > SEARCH_SPREAD)
            continue;
        if (!d->candidate) {
            d->candidate = true;
            d->candidateEnd = cur + PEAK_SAMPLES;
            d->peakLevel = 0;
        }
        /* The correlation is normalised, nearly flat across a symbol: the
         * eye's centre is where the fitted level is highest */
        if (fabsf(fit.level) > d->peakLevel) {
            d->peakLevel = fabsf(fit.level);
            takeSync(d, cur, f, &fit);
        }
    }

    if (d->candidate && !after(d->candidateEnd, cur)) {
        d->candidate = false;
        d->locked = true;
        d->period = periodOf(d->family);
        d->missed = 0;
        d->bursts = 0;
        d->peakLevel = 0;
        d->decided = true;
    }
}

/** Locked: refine the timing at the expected sync */
static void track(struct dmrDemod *d, uint32_t cur)
{
    if (d->decided || after(d->syncAt - WINDOW, cur))
        return;

    int f = 0;
    while (f < 3 && syncFamily[f] != d->family)
        f++;

    struct syncFit fit = fitSync(d, cur, f);
    float s = fabsf(fit.score);
    if (s >= TRACK_SCORE && fit.errors <= TRACK_ERRORS
        && fit.spread <= TRACK_SPREAD && fabsf(fit.level) > d->peakLevel) {
        d->peakScore = s;
        d->peakAt = cur;
        d->peakSign = (fit.score > 0) ? 1 : -1;
        d->peakLevel = fabsf(fit.level);
        d->peakDc = fit.dc;
    }

    if (cur != d->syncAt + WINDOW)
        return;

    d->decided = true;
    if (d->peakLevel > 0) {
        d->syncAt = d->peakAt;
        d->syncFound = true;
        d->syncSign = d->peakSign;
        d->syncScore = d->peakScore;
        d->level = d->peakLevel;
        d->dc = d->peakDc;
        d->missed = 0;
    } else {
        d->syncFound = false;
        d->missed++;
    }
    d->peakScore = 0;
    d->peakLevel = 0;
}

void dmrDemod_process(struct dmrDemod *d, const int16_t *samples, unsigned n)
{
    for (unsigned s = 0; s < n; s++) {
        /* Filter: the input twice over, for a contiguous window */
        d->in[d->inPos] = (float)samples[s];
        d->in[d->inPos + TAPS] = (float)samples[s];
        d->inPos = (d->inPos + 1) % TAPS;
        const float *x = &d->in[d->inPos];
        float y = 0;
        for (int i = 0; i < TAPS; i++)
            y += rrc[i] * x[i];

        uint32_t cur = d->n++;
        d->ring[cur & MASK] = y;

        if (!d->locked) {
            search(d, cur);
            if (!d->locked)
                continue;
        } else {
            track(d, cur);
        }

        /* The burst is complete when its last symbol is in */
        uint32_t end = d->syncAt
                     + (uint32_t)((BURST_SYMBOLS - 1 - SYNC_LAST) * SPS);
        if (cur == end) {
            emitBurst(d);
            unsigned maxMissed = (d->family == DMR_SYNC_BS) ? MISSED_BS :
                                                              MISSED_SINGLE;
            if (d->missed >= maxMissed) {
                d->locked = false;
                d->candidate = false;
                d->peakScore = 0;
            } else {
                d->syncAt += d->period;
                d->syncFound = false;
                d->decided = false;
            }
        }
    }
}
