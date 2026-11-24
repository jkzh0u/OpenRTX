/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TETRA_CALL_REC_H
#define TETRA_CALL_REC_H

#include <stdbool.h>
#include <stdint.h>
#include "protocols/TETRA/TetraCallStore.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Saving TETRA calls (CONFIG_TETRA_CALLREC), with Settings > TETRA > Save
 * calls on: the speech frames of each call heard are kept in memory while
 * it lasts, then written to the call store (TetraCallStore.h) by a thread
 * of their own once it is over. Saved calls are played back through the
 * speech decoder, with the lowest audio priority, so that a call received
 * meanwhile takes the speaker back.
 *
 * With CONFIG_TETRA_CALLREC_SECTION the memory of the call being recorded
 * goes in that linker section (the C62: PSRAM).
 */

/** Longest call kept: 3 minutes; the rest of a longer one is not kept */
#define CALLREC_MAX_FRAMES 6000
/** Calls with less speech than this, 1 s, are not kept */
#define CALLREC_MIN_FRAMES 33
/** No speech for this long: the call is over, ms */
#define CALLREC_END_MS 8000

/**
 * A speech frame of the call followed (the rtx thread, TetraSpeech.c).
 *
 * @param bits: OA_FRAME_BITS bits, one a byte.
 * @param bfi: the frame failed its check.
 */
void tetraCallRec_frame(const uint8_t *bits, uint8_t bfi);

/** Turn saving on or off (Settings > TETRA > Save calls). */
void tetraCallRec_enable(bool on);

/** Play a saved call (UI). @param seq: the call. */
void tetraCallRec_play(uint32_t seq);

/** Stop playing (UI). */
void tetraCallRec_stop(void);

/**
 * What is playing (UI).
 *
 * @param seq: the call, if playing.
 * @param played: seconds played.
 * @param length: seconds in all.
 * @return true while playing.
 */
bool tetraCallRec_playing(uint32_t *seq, uint16_t *played, uint16_t *length);

/** @return true while a call is being recorded or written (UI). */
bool tetraCallRec_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* TETRA_CALL_REC_H */
