/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/APRS/Afsk.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Correlator tables: a bit's worth of each tone */
static float markCos[AFSK_RX_SPB], markSin[AFSK_RX_SPB];
static float spaceCos[AFSK_RX_SPB], spaceSin[AFSK_RX_SPB];
static bool tablesReady;

/* Level control, as Dire Wolf does it: peaks follow at once, fall slowly */
#define AGC_ATTACK 0.70f
#define AGC_DECAY 0.00012f

/* Bit clock: a bit a wrap, pulled towards each transition */
#define PLL_STEP ((int32_t)(4294967296.0 / AFSK_RX_SPB))
#define PLL_INERTIA_LOCKED 0.74f
#define PLL_INERTIA_SEARCHING 0.50f

static void makeTables(void)
{
    for (int k = 0; k < AFSK_RX_SPB; k++) {
        double m = 2.0 * M_PI * AFSK_MARK_HZ * k / AFSK_RX_RATE;
        double s = 2.0 * M_PI * AFSK_SPACE_HZ * k / AFSK_RX_RATE;
        markCos[k] = (float)cos(m);
        markSin[k] = (float)sin(m);
        spaceCos[k] = (float)cos(s);
        spaceSin[k] = (float)sin(s);
    }
    tablesReady = true;
}

void afskDemod_init(struct afskDemod *d)
{
    if (!tablesReady)
        makeTables();
    memset(d, 0, sizeof(*d));
    hdlcRx_init(&d->hdlc);
}

/** A level between its valley (-0.5) and its peak (+0.5) */
static float agc(float in, float *peak, float *valley)
{
    if (in >= *peak)
        *peak = in * AGC_ATTACK + *peak * (1.0f - AGC_ATTACK);
    else
        *peak = in * AGC_DECAY + *peak * (1.0f - AGC_DECAY);

    if (in <= *valley)
        *valley = in * AGC_ATTACK + *valley * (1.0f - AGC_ATTACK);
    else
        *valley = in * AGC_DECAY + *valley * (1.0f - AGC_DECAY);

    if (*peak > *valley)
        return (in - 0.5f * (*peak + *valley)) / (*peak - *valley);
    return 0.0f;
}

void afskDemod_process(struct afskDemod *d, const int16_t *pcm, size_t n,
                       void (*onFrame)(const uint8_t *frame, size_t len,
                                       void *user),
                       void *user)
{
    for (size_t i = 0; i < n; i++) {
        d->hist[d->hpos] = pcm[i];
        d->hpos = (uint8_t)((d->hpos + 1) % AFSK_RX_SPB);

        /* Each tone's strength over the last bit */
        float mi = 0, mq = 0, si = 0, sq = 0;
        for (int k = 0; k < AFSK_RX_SPB; k++) {
            float x = d->hist[(d->hpos + k) % AFSK_RX_SPB];
            mi += x * markCos[k];
            mq += x * markSin[k];
            si += x * spaceCos[k];
            sq += x * spaceSin[k];
        }
        float mark = sqrtf(mi * mi + mq * mq);
        float space = sqrtf(si * si + sq * sq);

        /* Each against its own level: pre-emphasis tilts one above the
         * other */
        float out = agc(mark, &d->markPeak, &d->markValley)
                  - agc(space, &d->spacePeak, &d->spaceValley);

        /* The bit clock */
        int32_t before = d->pll;
        d->pll = (int32_t)((uint32_t)d->pll + (uint32_t)PLL_STEP);
        if (d->pll < 0 && before > 0) {
            uint8_t level = out > 0;
            /* NRZI: no change is a one */
            uint8_t bit = (level == d->level);
            d->level = level;
            size_t len = hdlcRx_bit(&d->hdlc, bit);
            if (len > 0 && onFrame != NULL)
                onFrame(d->hdlc.buf, len, user);
        }
        if ((out > 0) != (d->prev > 0)) {
            float inertia = d->hdlc.inFrame ? PLL_INERTIA_LOCKED :
                                              PLL_INERTIA_SEARCHING;
            d->pll = (int32_t)((float)d->pll * inertia);
        }
        d->prev = out;
    }
}

void afskMod_init(struct afskMod *m, const uint8_t *frame, size_t len,
                  uint32_t rate, uint16_t flagsBefore, uint16_t flagsAfter,
                  int16_t amplitude)
{
    memset(m, 0, sizeof(*m));
    hdlcTx_init(&m->hdlc, frame, len, flagsBefore, flagsAfter);
    m->rate = rate;
    m->perBit = rate / AFSK_BAUD;
    m->mark = true;
    m->amplitude = amplitude;
}

size_t afskMod_render(struct afskMod *m, int16_t *out, size_t max)
{
    size_t n = 0;

    while (n < max && !m->done) {
        if (m->sampleInBit == 0) {
            int bit = hdlcTx_next(&m->hdlc);
            if (bit < 0) {
                m->done = true;
                break;
            }
            /* NRZI: a zero changes the tone */
            if (bit == 0)
                m->mark = !m->mark;
        }
        uint32_t hz = m->mark ? AFSK_MARK_HZ : AFSK_SPACE_HZ;
        m->phase += (uint32_t)((uint64_t)hz * 4294967296ULL / m->rate);
        float s = sinf((float)m->phase * (float)(2.0 * M_PI / 4294967296.0));
        out[n++] = (int16_t)(s * m->amplitude);
        if (++m->sampleInBit == m->perBit)
            m->sampleInBit = 0;
    }
    return n;
}
