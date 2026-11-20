/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_FOLLOW_H
#define OPENRTX_TETRA_FOLLOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA call following policy: which calls the radio follows to the other
 * carriers of a cell, and where it listens.
 *
 * On a trunked cell the main carrier carries the control channel, and calls
 * are given traffic channels, often on another carrier of the same cell.
 * libtetra tracks the channel allocations and asks, through its retune
 * callback, to tune to a call's carrier and back to the main carrier when
 * the call ends. The follower answers those requests:
 *
 * - it follows one clear speech call at a time, and stays with the call it
 *   is hearing, including one on the main carrier;
 * - it keeps the radio's tuning offset from the main carrier;
 * - it tracks the call's timeslots, which libtetra reports only in call
 *   events when the call moves within a carrier;
 * - it returns to the main carrier on its own after a shorter silence than
 *   libtetra's, or when the call turns out encrypted or data. The caller
 *   then restarts libtetra's synchronisation there (tetra_tmo_resync()).
 *
 * All carriers of a cell share the TDMA timing, so the receiver keeps its
 * slot timing across a retune requested by libtetra.
 *
 * Independent of the radio and of libtetra: the caller translates events
 * and requests into the structures below and applies the target.
 */

/** All four timeslots, bit 0 = TN1 */
#define TETRA_FOLLOW_ALL_SLOTS 0x0f

/** Default silence before returning to the main carrier, ms */
#define TETRA_FOLLOW_TIMEOUT_MS 10000

/** Largest tuning error accepted between the radio and the main carrier */
#define TETRA_FOLLOW_MAX_TUNING_HZ 12500

/** Calls whose type is remembered */
#define TETRA_FOLLOW_MAX_CALLS 8

/**
 * A call is joined late only if seen on the main carrier this recently, ms:
 * its set-up or a repeat of its channel allocation (about every 5 s on the
 * TTC while it goes on). The release of a call on another carrier is sent
 * there, and may never reach the main carrier.
 */
#define TETRA_FOLLOW_JOIN_MAX_AGE_MS 10000

/**
 * After a priority call, the radio stays on the main carrier this long
 * before leaving for a call of lower priority, ms: a conversation on a
 * priority talkgroup goes on as new calls a few seconds apart, whose
 * set-up cannot be seen from another carrier.
 *
 * Priorities: 0 none, 1 (lowest) to 15.
 */
#define TETRA_FOLLOW_PRIORITY_HOLD_MS 10000

enum tetraFollowEvent {
    TETRA_FOLLOW_UPDATE,  /**< Set-up, connect, grant or other call update */
    TETRA_FOLLOW_CEASED,  /**< Transmission ceased, the call continues */
    TETRA_FOLLOW_RELEASE, /**< Call released or lost */
};

/** A call control event, as the follower needs it */
struct tetraFollowCall {
    enum tetraFollowEvent event;
    uint16_t callId;
    uint16_t carrier; /**< Allocated carrier number, 0xffff if none */
    uint8_t slots;    /**< Allocated timeslots, bit 0 = TN1; 0 if none */
    bool speech;      /**< Speech call (not circuit mode data) */
    bool encrypted;   /**< End-to-end or air interface encryption */
    uint32_t freq;    /**< Carrier downlink from the cell's numbering, Hz;
                           0 if unknown */
    uint32_t mainFreq; /**< Main carrier downlink, Hz; 0 if unknown */
    uint8_t priority;  /**< 0 none, 1 (lowest) to 15 */
};

/** A retune request from the receiver */
struct tetraFollowRetune {
    bool main;         /**< Back to the main carrier, else follow a call */
    uint16_t callId;   /**< Call to follow */
    uint16_t carrier;  /**< Carrier number */
    uint32_t freq;     /**< Carrier downlink from the cell's numbering, Hz */
    uint32_t mainFreq; /**< Main carrier downlink, Hz; 0 if unknown */
    uint8_t slots;     /**< Timeslots of the call, bit 0 = TN1 */
    uint8_t priority;  /**< 0 none, 1 (lowest) to 15 */
};

/** Where to listen */
struct tetraFollowTarget {
    uint32_t freq; /**< Carrier, Hz */
    uint8_t slots; /**< Timeslots to decode, bit 0 = TN1 */
    bool away;     /**< On a carrier other than the main one */
};

struct tetraFollowCallInfo {
    bool used;
    uint16_t callId;
    bool speech;
    bool encrypted;
    uint32_t freq; /**< Where to tune for its carrier, Hz; 0 if unknown */
    uint16_t carrier;
    uint8_t slots;
    bool joined; /**< Selected once: not joined late again */
    uint8_t priority;
    long long lastSeen; /**< Last event or allocation, ms */
};

struct tetraFollow {
    /* Settings */
    uint32_t home;        /**< Main carrier as the radio tunes it, Hz */
    uint32_t timeoutMs;   /**< Silence before returning home */
    bool followEncrypted; /**< Also follow calls that cannot be heard */
    /* Known calls */
    struct tetraFollowCallInfo calls[TETRA_FOLLOW_MAX_CALLS];
    uint8_t nextCall;
    /* Selected call */
    bool selected;
    uint16_t callId;
    long long lastActivity;
    uint8_t selectedPriority; /**< Priority of the selected call */
    uint8_t callSlots;     /**< Its timeslots if known, else 0 */
    bool preempted;        /**< A priority call took over: the caller
                                clears it */
    /* Priority calls */
    uint8_t holdPriority;   /**< Priority held for, 0 none */
    long long lastPriority; /**< When a call of that priority was active */
    /* Carrier being received */
    bool away;
    bool leave;       /**< Return home at the next tick */
    uint16_t carrier; /**< Carrier number while away */
    uint32_t freq;    /**< Frequency while away, Hz */
    uint8_t slots;    /**< Timeslots while away */
};

/**
 * Frequency to tune for a carrier of the cell, relative to the frequency
 * the radio uses for the main carrier, so that a tuning correction carries
 * over.
 *
 * @param home: frequency the radio is tuned to for the main carrier, Hz.
 * @param mainHz: main carrier frequency broadcast by the cell, Hz.
 * @param carrierHz: wanted carrier frequency from the cell's numbering, Hz.
 * @return frequency to tune, Hz; 0 if the radio is not tuned to the main
 * carrier (more than TETRA_FOLLOW_MAX_TUNING_HZ away) or a value is 0.
 */
uint32_t tetraFollow_carrierFreq(uint32_t home, uint32_t mainHz,
                                 uint32_t carrierHz);

/**
 * Join late a call going on on another carrier: when the radio is on the
 * main carrier with no call selected, and a call that can be played was
 * allocated another carrier (its channel allocation was seen while another
 * call was followed, so the receiver did not ask to retune), select it and
 * go there. A call already selected once is not joined, so that one whose
 * release was missed, or that went silent, does not bring the radio back
 * after each timeout; nor one not seen for TETRA_FOLLOW_JOIN_MAX_AGE_MS.
 *
 * After a join the caller tunes to tetraFollow_target() and restarts the
 * receiver's synchronisation, which then finds the call by its timeslots.
 *
 * @param f: follower.
 * @param allowed: whether a call may be joined (talkgroup filter), or NULL.
 * @param now: current time, ms.
 * @return true if a call was joined.
 */
bool tetraFollow_join(struct tetraFollow *f, bool (*allowed)(uint16_t callId),
                      long long now);

/**
 * Initialise the follower, at home with no call selected.
 *
 * @param f: follower.
 * @param home: frequency the radio is tuned to for the main carrier, Hz.
 */
void tetraFollow_init(struct tetraFollow *f, uint32_t home);

/**
 * Process a call control event.
 *
 * @param f: follower.
 * @param c: call event.
 * @param now: current time, ms.
 */
void tetraFollow_call(struct tetraFollow *f, const struct tetraFollowCall *c,
                      long long now);

/**
 * Process a traffic slot.
 *
 * @param f: follower.
 * @param callId: call the traffic belongs to, 0 if unknown.
 * @param speech: speech traffic.
 * @param encrypted: the traffic is encrypted.
 * @param now: current time, ms.
 */
void tetraFollow_traffic(struct tetraFollow *f, uint16_t callId, bool speech,
                         bool encrypted, long long now);

/**
 * Answer a retune request of the receiver. If accepted, the caller tunes
 * to tetraFollow_target() before returning to the receiver.
 *
 * @param f: follower.
 * @param r: request.
 * @param now: current time, ms.
 * @return true if the radio should retune.
 */
bool tetraFollow_retune(struct tetraFollow *f,
                        const struct tetraFollowRetune *r, long long now);

/**
 * Check the silence timeout and pending returns. Call periodically.
 *
 * @param f: follower.
 * @param now: current time, ms.
 * @return true if the follower went home on its own: the caller tunes to
 * the target and restarts the receiver's synchronisation.
 */
bool tetraFollow_tick(struct tetraFollow *f, long long now);

/**
 * Move home to another cell: the radio now receives a different main
 * carrier. Drops the call and goes home.
 *
 * @param f: follower.
 * @param home: frequency the radio is tuned to for the new main carrier, Hz.
 */
void tetraFollow_setHome(struct tetraFollow *f, uint32_t home);

/**
 * Drop the selected call, for example when a new filter excludes it. If it
 * is being followed, the follower goes home at the next tick.
 *
 * @param f: follower.
 */
void tetraFollow_drop(struct tetraFollow *f);

/**
 * Go home and drop the call, for example when the receiver is restarted.
 *
 * @param f: follower.
 */
void tetraFollow_home(struct tetraFollow *f);

/**
 * @param f: follower.
 * @return the carrier and timeslots to listen to.
 */
struct tetraFollowTarget tetraFollow_target(const struct tetraFollow *f);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_FOLLOW_H */
