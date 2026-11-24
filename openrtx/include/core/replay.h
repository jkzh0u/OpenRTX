/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef REPLAY_H
#define REPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Instant replay (CONFIG_REPLAY): the last REPLAY_SECONDS of what was
 * received, FM channel audio and TETRA speech, kept at 8 kHz, and played
 * again on request. A pause of REPLAY_GAP_MS starts a new transmission;
 * the last REPLAY_TRANSMISSIONS are kept, the oldest cut as the memory
 * fills up. Playback has the lowest audio priority: anything received
 * takes the speaker back.
 *
 * With CONFIG_REPLAY_SECTION the memory goes in that linker section (the
 * C62: PSRAM).
 */

#define REPLAY_RATE 8000
#define REPLAY_SECONDS 30
#define REPLAY_SAMPLES (REPLAY_RATE * REPLAY_SECONDS)
/** No sound for this long: the next sound is a new transmission, ms */
#define REPLAY_GAP_MS 700
#define REPLAY_TRANSMISSIONS 8
/** Shorter transmissions are not kept: a squelch tail, a click */
#define REPLAY_MIN_SAMPLES (REPLAY_RATE / 4)

struct replayStatus {
    bool playing;
    uint8_t index;   /**< Transmission playing: 1 the last, 2 the one before */
    uint8_t count;   /**< Transmissions kept */
    uint16_t length; /**< Of the one playing, seconds */
    uint16_t played; /**< Of the one playing, seconds */
};

/**
 * Keep received audio (the rtx thread, the audio driver, the TETRA speech
 * thread).
 *
 * @param pcm: samples at REPLAY_RATE.
 * @param n: number of samples.
 */
void replay_record(const int16_t *pcm, size_t n);

/**
 * Play the last transmission; while one plays, the one before it (UI).
 */
void replay_play(void);

/** Stop playing (UI). */
void replay_stop(void);

/** Copy the state (UI). */
void replay_get(struct replayStatus *out);

#ifdef __cplusplus
}
#endif

#endif /* REPLAY_H */
