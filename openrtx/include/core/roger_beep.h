/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ROGER_BEEP_H
#define ROGER_BEEP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Roger beeps: the tones sent at the end of an FM transmission, in several
 * styles. Style 0 is off. The radio plays them with a tone generator that
 * it adds to the transmitted audio.
 *
 * Talk permit tones: heard in the radio's own speaker when PTT is pressed,
 * before the transmitter keys up, as on Motorola radios. Not transmitted.
 */

/** Styles, 0 (off) included */
#define ROGER_BEEP_STYLES 9

/** Talk permit styles, 0 (off) included */
#define TALK_PERMIT_STYLES 7

/** Sample rate of talkPermit_render(), Hz */
#define TALK_PERMIT_RATE 8000

/** Samples of the longest talk permit tone */
#define TALK_PERMIT_MAX_SAMPLES (TALK_PERMIT_RATE * 3 / 10)

/**
 * Turn the transmit tone on at a frequency, or off.
 *
 * @param hz: frequency, Hz; 0 for off.
 */
typedef void (*rogerBeepTone)(uint16_t hz);

/**
 * @param style: 0 to ROGER_BEEP_STYLES - 1.
 * @return the style's name, for the menu.
 */
const char *rogerBeep_name(uint8_t style);

/**
 * Play a style, blocking until it ends; the tone is off afterwards.
 *
 * @param style: 0 (nothing) to ROGER_BEEP_STYLES - 1.
 * @param tone: sets the transmit tone.
 */
void rogerBeep_play(uint8_t style, rogerBeepTone tone);

/**
 * @param style: 0 to TALK_PERMIT_STYLES - 1.
 * @return the talk permit style's name, for the menu.
 */
const char *talkPermit_name(uint8_t style);

/**
 * Render a talk permit tone as audio samples, TALK_PERMIT_RATE per second.
 *
 * @param style: 0 (nothing) to TALK_PERMIT_STYLES - 1.
 * @param out: destination.
 * @param max: room in out, samples.
 * @return the number of samples written, 0 for none.
 */
size_t talkPermit_render(uint8_t style, int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* ROGER_BEEP_H */
