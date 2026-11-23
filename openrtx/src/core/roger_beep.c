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
