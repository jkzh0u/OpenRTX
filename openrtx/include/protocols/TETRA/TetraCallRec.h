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

/** Which calls are saved (Settings > TETRA > Save calls) */
enum callRecMode {
    CALLREC_OFF = 0,
    CALLREC_ALL,    /**< Every call heard with speech */
    CALLREC_MARKED, /**< Talkgroups marked TF_SAVE in the channel list */
    CALLREC_FILTER, /**< Talkgroups the talkgroup filter lets through */
    CALLREC_MODES
};

/**
 * Which calls to save (Settings > TETRA > Save calls). Alarm calls are
 * saved whenever saving is on.
 *
 * @param mode: enum callRecMode.
 */
void tetraCallRec_enable(uint8_t mode);

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

/**
 * When a call saved since power on was heard (UI).
 *
 * @param seq: the call.
 * @param tick: when it started, ms.
 * @return false if it was saved before this power on.
 */
bool tetraCallRec_heardAt(uint32_t seq, long long *tick);

/** @return true while a call is being recorded or written (UI). */
bool tetraCallRec_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* TETRA_CALL_REC_H */
