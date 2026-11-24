/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DTMF_H
#define DTMF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DTMF while transmitting in FM (CONFIG_DTMF): a keypad key held during
 * PTT sends its two tones, for repeater control, autopatch and the like.
 * The UI says which key is held, the FM mode sends it (radio.h,
 * radio_setTxDtmf()).
 */

/** Keys sent in this transmission, kept for the screen */
#define DTMF_SENT_MAX 16

/**
 * The two tones of a key.
 *
 * @param key: '0' to '9', '*', '#', 'A' to 'D'.
 * @param low: row tone, Hz.
 * @param high: column tone, Hz.
 * @return false for another character.
 */
static inline bool dtmf_tones(char key, uint16_t *low, uint16_t *high)
{
    static const char keys[] = "123A456B789C*0#D";
    static const uint16_t rows[4] = { 697, 770, 852, 941 };
    static const uint16_t cols[4] = { 1209, 1336, 1477, 1633 };

    for (unsigned i = 0; i < 16; i++) {
        if (keys[i] == key) {
            *low = rows[i / 4];
            *high = cols[i % 4];
            return true;
        }
    }
    return false;
}

/**
 * A key pressed or let go while transmitting (UI).
 *
 * @param key: the key, or 0 when let go.
 */
void dtmf_key(char key);

/** @return the key to send now, 0 for none (FM mode). */
char dtmf_current(void);

/** A new transmission: forget the keys sent (FM mode). */
void dtmf_clear(void);

/**
 * @return the keys sent in this transmission, for the screen (UI).
 */
const char *dtmf_sent(void);

#ifdef __cplusplus
}
#endif

#endif /* DTMF_H */
