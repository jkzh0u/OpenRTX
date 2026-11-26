/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_AFSK_H
#define OPENRTX_AFSK_H

#include <stddef.h>
#include <stdint.h>
#include "protocols/APRS/Ax25.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bell 202 AFSK, 1200 baud, mark 1200 Hz, space 2200 Hz, NRZI: the physical
 * layer of APRS on VHF.
 */

#define AFSK_BAUD 1200
#define AFSK_MARK_HZ 1200
#define AFSK_SPACE_HZ 2200

/** Sample rate of the demodulator: 8 samples a bit */
#define AFSK_RX_RATE 9600
#define AFSK_RX_SPB (AFSK_RX_RATE / AFSK_BAUD)

struct afskDemod {
    float hist[AFSK_RX_SPB]; /**< The last bit's samples */
    uint8_t hpos;
    float markPeak, markValley;
    float spacePeak, spaceValley;
    int32_t pll;   /**< Bit clock: a bit is read when it wraps */
    float prev;    /**< Previous demodulator output */
    uint8_t level; /**< Tone of the last bit read, for NRZI */
    struct hdlcRx hdlc;
};

void afskDemod_init(struct afskDemod *d);

/**
 * Demodulate samples.
 *
 * @param d: demodulator.
 * @param pcm: samples at AFSK_RX_RATE.
 * @param n: how many.
 * @param onFrame: called with each good frame, without its check sequence.
 * @param user: passed to onFrame.
 */
void afskDemod_process(struct afskDemod *d, const int16_t *pcm, size_t n,
                       void (*onFrame)(const uint8_t *frame, size_t len,
                                       void *user),
                       void *user);

struct afskMod {
    struct hdlcTx hdlc;
    uint32_t rate;
    uint32_t phase;  /**< Tone phase, a full turn is 2^32 */
    uint32_t sampleInBit;
    uint32_t perBit; /**< Samples a bit */
    bool mark;       /**< Tone sent, NRZI */
    bool done;
    int16_t amplitude;
};

/**
 * Prepare to send a frame.
 *
 * @param m: modulator.
 * @param frame: the frame with its check sequence, kept until the end.
 * @param len: its length.
 * @param rate: sample rate, a multiple of 1200.
 * @param flagsBefore: flags first, so that receivers lock: 300 ms is 45.
 * @param flagsAfter: flags after the frame.
 * @param amplitude: of the tones.
 */
void afskMod_init(struct afskMod *m, const uint8_t *frame, size_t len,
                  uint32_t rate, uint16_t flagsBefore, uint16_t flagsAfter,
                  int16_t amplitude);

/**
 * @return samples written, fewer than max only at the end.
 */
size_t afskMod_render(struct afskMod *m, int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_AFSK_H */
