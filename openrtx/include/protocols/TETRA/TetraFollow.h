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
 * TETRA call following: chooses the carrier and the timeslots the TMO
 * receiver listens to.
 *
 * On a trunked cell the main carrier carries the control channel, and calls
 * are given traffic channels, often on another carrier of the same cell. The
 * follower selects one speech call and tracks its latest channel allocation.
 * It returns to the main carrier ("home") when the call is released, sent
 * back to the control channel, silent for too long, or when the receiver
 * loses synchronisation.
 *
 * All carriers of a cell share the TDMA timing, so the receiver keeps its
 * slot timing across a retune: the caller retunes without resetting it.
 *
 * Independent of the radio and of the protocol stack: the caller translates
 * its call events into tetraFollowCall records and applies the target.
 */

/** All four timeslots, bit 0 = TN1 */
#define TETRA_FOLLOW_ALL_SLOTS 0x0f

/** Default silence before returning to the main carrier, ms */
#define TETRA_FOLLOW_TIMEOUT_MS 5000

/** Largest tuning error accepted between the radio and the main carrier */
#define TETRA_FOLLOW_MAX_TUNING_HZ 12500

enum tetraFollowEvent {
    TETRA_FOLLOW_UPDATE,  /**< Set-up, connect, grant or other call update */
    TETRA_FOLLOW_CEASED,  /**< Transmission ceased, the call continues */
    TETRA_FOLLOW_RELEASE, /**< Call released or lost */
};

/** A call control event, as the follower needs it */
struct tetraFollowCall {
    enum tetraFollowEvent event;
    uint16_t callId;
    uint32_t freq;  /**< Allocated carrier downlink, Hz; 0 if none or unknown */
    uint8_t slots;  /**< Allocated timeslots, bit 0 = TN1; 0: control channel */
    bool speech;    /**< Speech call (not circuit mode data) */
    bool encrypted; /**< End-to-end or air interface encryption */
};

/** Where to listen */
struct tetraFollowTarget {
    uint32_t freq; /**< Carrier, Hz */
    uint8_t slots; /**< Timeslots to decode, bit 0 = TN1 */
    bool away;     /**< On a carrier other than the main one */
};

struct tetraFollow {
    /* Settings */
    uint32_t home;        /**< Main carrier, Hz */
    uint32_t timeoutMs;   /**< Silence before returning home */
    bool followEncrypted; /**< Also follow calls that cannot be heard */
    /* Selected call */
    bool selected;
    uint16_t callId;
    uint32_t freq; /**< Its carrier, 0 while it is on the main carrier */
    uint8_t slots; /**< Its timeslots, valid when freq != 0 */
    long long lastActivity;
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
 * Initialise the follower, at home with no call selected.
 *
 * @param f: follower.
 * @param home: main carrier downlink frequency, Hz.
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
 * @param encrypted: the traffic is encrypted.
 * @param now: current time, ms.
 */
void tetraFollow_traffic(struct tetraFollow *f, uint16_t callId, bool encrypted,
                         long long now);

/**
 * The receiver lost synchronisation: drop the call and go home.
 *
 * @param f: follower.
 */
void tetraFollow_lostSync(struct tetraFollow *f);

/**
 * Check the silence timeout. Call periodically.
 *
 * @param f: follower.
 * @param now: current time, ms.
 */
void tetraFollow_tick(struct tetraFollow *f, long long now);

/**
 * @param f: follower.
 * @return the carrier and timeslots to listen to.
 */
struct tetraFollowTarget tetraFollow_target(const struct tetraFollow *f);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_FOLLOW_H */
