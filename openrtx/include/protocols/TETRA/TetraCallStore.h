/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TETRA_CALL_STORE_H
#define TETRA_CALL_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA calls saved on the radio (CONFIG_TETRA_CALLREC): the speech frames
 * of each call as received, ACELP coded, and what is known of it. Kept in
 * flash by the platform (on the C62, call_store_partition), the oldest
 * calls written over when it is full. TetraCallRec.h records and plays
 * them.
 *
 * A call is named by its sequence number, which never repeats.
 */

/** One 30 ms speech frame: 137 bits, then the bad frame flag, packed */
#define CALLSTORE_FRAME_BYTES 18
/** Talking parties kept per call */
#define CALLSTORE_TALKERS 4

/** The time is the network's local time, else UTC */
#define CALLSTORE_LOCAL_TIME 0x01
/** A group call */
#define CALLSTORE_GROUP 0x02

struct callStoreInfo {
    uint32_t seq;                        /**< Set when saved */
    uint32_t dst;                        /**< Talkgroup, or called SSI */
    uint32_t talkers[CALLSTORE_TALKERS]; /**< First ones heard, 0: none */
    uint32_t frames;                     /**< Speech frames, 30 ms each */
    uint16_t la;                         /**< Location area of the cell */
    uint16_t year;                       /**< Start, network time; 0: none */
    uint16_t dayOfYear;                  /**< 0 for 1 January */
    uint32_t secOfDay;                   /**< Seconds since midnight */
    uint8_t flags;                       /**< CALLSTORE_... */
};

/** @return true once the store has been read (at start-up). */
bool callStore_ready(void);

/** @return the number of calls saved. */
uint16_t callStore_count(void);

/**
 * @param n: 0 for the newest call, 1 for the one before...
 * @param out: what is known of it.
 * @return false past the oldest.
 */
bool callStore_info(uint16_t n, struct callStoreInfo *out);

/**
 * Read speech frames of a saved call.
 *
 * @param seq: the call.
 * @param first: first frame.
 * @param buf: CALLSTORE_FRAME_BYTES a frame.
 * @param count: frames to read.
 * @return the frames read, or -1 if the call is gone.
 */
int callStore_read(uint32_t seq, uint32_t first, uint8_t *buf, uint32_t count);

/**
 * Save a call, writing over the oldest ones if needed. Slow: it erases and
 * writes flash, pausing in between so that the rest of the radio keeps
 * running. Call it from a thread of its own.
 *
 * @param info: what is known of the call; seq is set.
 * @param frames: its speech frames.
 * @return 0 on success, a negative value if it could not be written or
 * does not fit.
 */
int callStore_save(struct callStoreInfo *info, const uint8_t *frames);

/**
 * Delete a saved call.
 *
 * @param seq: the call.
 * @return 0 on success.
 */
int callStore_delete(uint32_t seq);

/** @return the most frames one call can have. */
uint32_t callStore_maxFrames(void);

#ifdef __cplusplus
}
#endif

#endif /* TETRA_CALL_STORE_H */
