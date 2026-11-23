/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stddef.h>
#include "core/roger_beep.h"
#include "interfaces/delays.h"

/* A tone (or silence, hz 0) for a time */
struct step {
    uint16_t hz;
    uint16_t ms;
};

/* Two rising tones */
static const struct step classic[] = { { 1000, 90 }, { 1500, 90 } };

/* One short beep */
static const struct step single[] = { { 1200, 120 } };

/* Two short beeps */
static const struct step twice[] = { { 1600, 60 }, { 0, 40 }, { 1600, 60 } };

/* Three quick rising tones */
static const struct step chirp[] = { { 1200, 45 }, { 1600, 45 }, { 2000, 60 } };

/* Falling, then low: a "bloop" */
static const struct step bloop[] = { { 1400, 50 }, { 900, 50 }, { 600, 70 } };

/* K, Morse for "over": dah dit dah */
static const struct step morseK[] = {
    { 800, 180 }, { 0, 60 }, { 800, 60 }, { 0, 60 }, { 800, 180 },
};

static const struct {
    const char *name;
    const struct step *steps;
    size_t n;
} styles[ROGER_BEEP_STYLES] = {
    { "Off", NULL, 0 },
    { "Classic", classic, sizeof(classic) / sizeof(classic[0]) },
    { "Single", single, sizeof(single) / sizeof(single[0]) },
    { "Double", twice, sizeof(twice) / sizeof(twice[0]) },
    { "Chirp", chirp, sizeof(chirp) / sizeof(chirp[0]) },
    { "Bloop", bloop, sizeof(bloop) / sizeof(bloop[0]) },
    { "Morse K", morseK, sizeof(morseK) / sizeof(morseK[0]) },
    { "Sweep", NULL, 0 },    /* Played by sweep() */
    { "Moto MDC", NULL, 0 }, /* Played by mdcBurst() */
};

#define STYLE_SWEEP 7
#define STYLE_MDC 8

/** A rising sweep, 700 to 2300 Hz in 0.2 s */
static void sweep(rogerBeepTone tone)
{
    for (uint16_t hz = 700; hz <= 2300; hz += 80) {
        tone(hz);
        delayMs(10);
    }
}

/**
 * The sound of a Motorola MDC-1200 data burst, the "brrrp" many Motorola
 * radios send when keyed or unkeyed: 1200 baud FSK, 1200 Hz for a 1 and
 * 1800 Hz for a 0, here with a fixed bit pattern (it carries no ID).
 */
static void mdcBurst(rogerBeepTone tone)
{
    /* Bit sync (alternating), a sync word, then a scrambled payload: 0.15 s */
    uint32_t lfsr = 0x1d872b41u;

    for (unsigned bit = 0; bit < 180; bit++) {
        unsigned one;
        if (bit < 24) {
            one = bit & 1;
        } else if (bit < 64) {
            one = (0x07092a446full >> (63 - bit)) & 1;
        } else {
            one = lfsr & 1;
            lfsr = (lfsr >> 1) ^ ((lfsr & 1) ? 0xa3000000u : 0);
        }
        tone(one ? 1200 : 1800);
        delayUs(833);
    }
}

/* Talk permit tones */
static const struct step tpBeep[] = { { 1000, 100 } };
static const struct step tpDouble[] = { { 1400, 50 }, { 0, 30 }, { 1400, 50 } };
/*
 * Measured from recordings of three radios: Motorola's trunking talk permit
 * (910 Hz, four bursts getting longer), three short 1000 Hz beeps, and four
 * notes, G C G E.
 */
static const struct step tpMoto[] = {
    { 910, 26 }, { 0, 21 }, { 910, 27 }, { 0, 21 },
    { 910, 51 }, { 0, 31 }, { 910, 77 },
};
static const struct step tpTriple[] = {
    { 1000, 23 }, { 0, 17 }, { 1000, 23 }, { 0, 17 }, { 1000, 23 },
};
static const struct step tpMelody[] = {
    { 1568, 30 },
    { 1047, 40 },
    { 1568, 40 },
    { 1319, 40 },
};
static const struct step tpChime[] = { { 1500, 60 }, { 1000, 90 } };

static const struct {
    const char *name;
    const struct step *steps;
    size_t n;
} permits[TALK_PERMIT_STYLES] = {
    { "Off", NULL, 0 },
    { "Beep", tpBeep, sizeof(tpBeep) / sizeof(tpBeep[0]) },
    { "Double", tpDouble, sizeof(tpDouble) / sizeof(tpDouble[0]) },
    { "Motorola", tpMoto, sizeof(tpMoto) / sizeof(tpMoto[0]) },
    { "Triple", tpTriple, sizeof(tpTriple) / sizeof(tpTriple[0]) },
    { "Melody", tpMelody, sizeof(tpMelody) / sizeof(tpMelody[0]) },
    { "Chime", tpChime, sizeof(tpChime) / sizeof(tpChime[0]) },
};

/* A sine wave, one period in 64 steps, peak 10000 */
static const int16_t sine[64] = {
    0,     980,   1951,  2903,  3827,  4714,  5556,  6344,  7071,   7730,
    8315,  8819,  9239,  9569,  9808,  9952,  10000, 9952,  9808,   9569,
    9239,  8819,  8315,  7730,  7071,  6344,  5556,  4714,  3827,   2903,
    1951,  980,   0,     -980,  -1951, -2903, -3827, -4714, -5556,  -6344,
    -7071, -7730, -8315, -8819, -9239, -9569, -9808, -9952, -10000, -9952,
    -9808, -9569, -9239, -8819, -8315, -7730, -7071, -6344, -5556,  -4714,
    -3827, -2903, -1951, -980,
};

const char *talkPermit_name(uint8_t style)
{
    return (style < TALK_PERMIT_STYLES) ? permits[style].name : permits[0].name;
}

size_t talkPermit_render(uint8_t style, int16_t *out, size_t max)
{
    /* Each tone fades in and out over 2 ms: no clicks */
    const unsigned fade = TALK_PERMIT_RATE / 500;
    uint32_t phase = 0;
    size_t n = 0;

    if (style == 0 || style >= TALK_PERMIT_STYLES)
        return 0;

    for (size_t s = 0; s < permits[style].n; s++) {
        const struct step *st = &permits[style].steps[s];
        unsigned len = (unsigned)st->ms * TALK_PERMIT_RATE / 1000;
        uint32_t inc = (uint32_t)(((uint64_t)st->hz << 32) / TALK_PERMIT_RATE);

        for (unsigned i = 0; i < len && n < max; i++, n++) {
            if (st->hz == 0) {
                out[n] = 0;
                continue;
            }
            int32_t v = sine[phase >> 26];
            unsigned edge = (i < len - 1 - i) ? i : len - 1 - i;
            if (edge < fade)
                v = v * (int32_t)edge / (int32_t)fade;
            out[n] = (int16_t)v;
            phase += inc;
        }
    }

    return n;
}

const char *rogerBeep_name(uint8_t style)
{
    return (style < ROGER_BEEP_STYLES) ? styles[style].name : styles[0].name;
}

void rogerBeep_play(uint8_t style, rogerBeepTone tone)
{
    if (style == 0 || style >= ROGER_BEEP_STYLES)
        return;

    if (style == STYLE_SWEEP) {
        sweep(tone);
    } else if (style == STYLE_MDC) {
        mdcBurst(tone);
    } else {
        for (size_t i = 0; i < styles[style].n; i++) {
            tone(styles[style].steps[i].hz);
            delayMs(styles[style].steps[i].ms);
        }
    }

    tone(0);
}
