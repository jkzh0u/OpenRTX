/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_CALLS_H
#define OPENRTX_TETRA_CALLS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Recent TETRA calls, newest first: one entry per called talkgroup or
 * radio, moved to the front when it is heard again.
 */

/** Calls kept */
#define TETRA_CALLS_MAX 10

/** A pause in speech this long starts a new transmission, ms */
#define TETRA_CALLS_PAUSE_MS 1500

struct tetraCallEntry {
    uint32_t dst;   /**< Called talkgroup or radio, 0 if unknown */
    uint32_t src;   /**< Last talking party, 0 if unknown */
    bool group;     /**< Group call */
    bool voice;     /**< Clear speech was heard */
    bool encrypted; /**< Encryption was signalled */
    long long time;      /**< Last activity, ms */
    long long first;     /**< First activity, ms */
    long long lastVoice; /**< Last speech, ms */
    uint16_t heard;      /**< Transmissions heard */
};

struct tetraCalls {
    struct tetraCallEntry entry[TETRA_CALLS_MAX];
    uint8_t count;
};

/**
 * @param c: call log.
 */
void tetraCalls_init(struct tetraCalls *c);

/**
 * Note activity of a call. Calls with neither party known are ignored.
 *
 * @param c: call log.
 * @param dst: called talkgroup or radio, 0 if unknown.
 * @param src: talking party, 0 if unknown.
 * @param group: group call.
 * @param voice: clear speech is heard.
 * @param encrypted: encryption is signalled.
 * @param now: current time, ms.
 */
void tetraCalls_note(struct tetraCalls *c, uint32_t dst, uint32_t src,
                     bool group, bool voice, bool encrypted, long long now);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_CALLS_H */
