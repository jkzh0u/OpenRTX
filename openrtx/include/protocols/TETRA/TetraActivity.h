/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_ACTIVITY_H
#define OPENRTX_TETRA_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA activity log: for each talkgroup or radio called since power on,
 * how many calls and transmissions, how long someone was talking, and the
 * talk time of each of the last 24 hours. Times are in ms, from any clock
 * that does not go back.
 */

/** Talkgroups and radios kept, the least busy is dropped for a new one */
#define TETRA_ACTIVITY_MAX 32
/** Calls followed at once */
#define TETRA_ACTIVITY_OPEN 6
/** Hours of talk time kept */
#define TETRA_ACTIVITY_HOURS 24
/** A transmission with no news for this long ended at the last news, ms */
#define TETRA_ACTIVITY_IDLE_MS 20000

/** What happened to a call */
enum tetraActivityWhat {
    TETRA_ACTIVITY_SETUP,   /**< Set up or joined, someone talking */
    TETRA_ACTIVITY_TALK,    /**< Someone started talking */
    TETRA_ACTIVITY_CEASE,   /**< The talking stopped, the call goes on */
    TETRA_ACTIVITY_SEEN,    /**< News of the call, nothing else */
    TETRA_ACTIVITY_RELEASE, /**< The call ended */
};

struct tetraActivityEntry {
    uint32_t dst;    /**< Called talkgroup or radio */
    bool group;      /**< Group calls */
    bool talking;    /**< Someone is talking now */
    uint16_t calls;  /**< Calls */
    uint16_t talks;  /**< Transmissions */
    uint32_t talkMs; /**< Talk time, the current transmission included */
    long long first; /**< First heard */
    long long last;  /**< Last heard */
    uint16_t hourSec[TETRA_ACTIVITY_HOURS]; /**< Talk time, s, per hour */
};

struct tetraActivityOpen {
    uint16_t callId; /**< Call identifier */
    uint32_t dst;    /**< Called party, 0 until known */
    bool group;      /**< Group call */
    bool used;       /**< This slot follows a call */
    bool talking;    /**< A transmission is going on */
    long long start; /**< Start of the transmission */
    long long seen;  /**< Last news of the call */
};

struct tetraActivity {
    struct tetraActivityEntry entry[TETRA_ACTIVITY_MAX];
    uint8_t count;
    struct tetraActivityOpen open[TETRA_ACTIVITY_OPEN];
    /** Hour number + 1 that each slot of hourSec holds, 0 for none */
    uint32_t slotHour[TETRA_ACTIVITY_HOURS];
    long long since; /**< When the log started */
};

/**
 * Empty the log.
 *
 * @param a: activity log.
 * @param now: current time.
 */
void tetraActivity_init(struct tetraActivity *a, long long now);

/**
 * Note something that happened to a call.
 *
 * @param a: activity log.
 * @param what: enum tetraActivityWhat.
 * @param callId: call identifier.
 * @param dst: called talkgroup or radio, 0 if unknown.
 * @param group: a group call.
 * @param now: current time.
 */
void tetraActivity_note(struct tetraActivity *a, uint8_t what, uint16_t callId,
                        uint32_t dst, bool group, long long now);

/**
 * End transmissions without news for TETRA_ACTIVITY_IDLE_MS.
 *
 * @param a: activity log.
 * @param now: current time.
 */
void tetraActivity_update(struct tetraActivity *a, long long now);

/**
 * The n-th busiest talkgroup or radio, by talk time, then the last heard.
 *
 * @param a: activity log.
 * @param n: 0 for the busiest.
 * @param now: current time.
 * @param out: its entry, with the transmission going on counted and hourSec
 * in order: [0] the current hour, [1] the one before...
 * @return false past the last.
 */
bool tetraActivity_get(const struct tetraActivity *a, uint8_t n, long long now,
                       struct tetraActivityEntry *out);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_ACTIVITY_H */
