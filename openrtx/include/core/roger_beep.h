/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ROGER_BEEP_H
#define ROGER_BEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Roger beeps: the tones sent at the end of an FM transmission, in several
 * styles. Style 0 is off. The radio plays them with a tone generator that
 * it adds to the transmitted audio.
 */

/** Styles, 0 (off) included */
#define ROGER_BEEP_STYLES 9

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

#ifdef __cplusplus
}
#endif

#endif /* ROGER_BEEP_H */
