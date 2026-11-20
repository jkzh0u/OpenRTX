/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_ROAM_H
#define OPENRTX_TETRA_ROAM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA cell tracking: keeps the receiver on the best cell of the network
 * as the radio moves, the way a TETRA radio reselects cells. A network is
 * many cells (base stations, or subway zones), each on its own main
 * carrier, and each broadcasts the carriers of its neighbours.
 *
 * - While locked, if the serving cell gets weak (its signal, averaged, or
 *   nothing decoded for a while), it measures the signal of each
 *   neighbour, a few milliseconds each, and moves to one that is clearly
 *   stronger, twice in a row, or at once when the cell is about to be lost.
 *   Not during a call, unless the cell is about to be lost.
 * - Locked on a carrier that is not its cell's main carrier (a search found
 *   a secondary carrier), it moves to the main carrier
 *   (tetraRoam_mainCarrier()).
 * - When the lock is lost, it measures every known carrier (neighbours
 *   first, then the frequencies of the built-in channel list), and tries
 *   the strongest ones in turn until one locks.
 * - On request (tetraRoam_search()), it does the same while still locked,
 *   and moves to the strongest carrier unless the serving cell is about as
 *   strong.
 *
 * The caller carries out the steps: a probe is a short retune to read the
 * signal strength, then back; a tune is a move to another cell, after which
 * the receiver synchronises again.
 *
 * Independent of the radio and of libtetra.
 */

/** Neighbour cells kept (a TTC cell names 11 or more) */
#define TETRA_ROAM_MAX_NEIGHBOURS 16
/** Other known carriers kept */
#define TETRA_ROAM_MAX_SCAN 48
/** Candidates of a search */
#define TETRA_ROAM_MAX_CANDIDATES \
    (TETRA_ROAM_MAX_NEIGHBOURS + TETRA_ROAM_MAX_SCAN + 1)

/** No lock for this long: search, ms */
#define TETRA_ROAM_LOST_MS 2500
/** Time between checks of the neighbours while the cell is weak, ms */
#define TETRA_ROAM_CHECK_MS 3000
/**
 * Serving cell weaker than this, averaged: check the neighbours, dBm. On a
 * C62 decoding starts to suffer at about -90 dBm and fails at about -94
 * (scripts/c62/README.md, section 9): move before that.
 */
#define TETRA_ROAM_WEAK_DBM -88
/**
 * Weaker than this, or nothing decoded: the cell is about to be lost.
 * Measure the neighbours even during a call (a measurement may click), and
 * move on the first measurement that finds a stronger one, dBm.
 */
#define TETRA_ROAM_CRITICAL_DBM -91
/** Time constant of the serving cell's signal average, ms */
#define TETRA_ROAM_AVERAGE_MS 2000
/**
 * Locked but nothing decoded for this long: as weak as a lost cell, ms (for
 * the caller, which tells tetraRoam_tick() whether blocks decode).
 */
#define TETRA_ROAM_DEAF_MS 1000
/** A neighbour must be this much stronger to move to it, dB */
#define TETRA_ROAM_MARGIN_DB 6
/** Weaker than this: no signal worth trying, dBm */
#define TETRA_ROAM_FLOOR_DBM -118
/** Time to wait for a lock on a carrier tried, ms */
#define TETRA_ROAM_TRY_MS 2500
/** Pause after a search found nothing, ms */
#define TETRA_ROAM_PAUSE_MS 2000
/** A requested search leaves a locked cell for one this much stronger, dB */
#define TETRA_ROAM_SEARCH_MARGIN_DB 3

enum tetraRoamAction {
    TETRA_ROAM_NONE,  /**< Nothing to do */
    TETRA_ROAM_PROBE, /**< Measure freq, then report with tetraRoam_probed() */
    TETRA_ROAM_TUNE,  /**< Move to freq and synchronise again */
};

struct tetraRoamStep {
    enum tetraRoamAction action;
    uint32_t freq; /**< Hz */
};

enum tetraRoamState {
    TETRA_ROAM_LOCKED, /**< On a cell, or waiting for the lock to return */
    TETRA_ROAM_CHECK,  /**< Measuring the neighbours of a weak cell */
    TETRA_ROAM_SWEEP,  /**< Lost or asked: measuring every known carrier */
    TETRA_ROAM_TRY,    /**< Lost or asked: trying candidates for a lock */
    TETRA_ROAM_PAUSE,  /**< Lost: nothing found, waiting to search again */
};

struct tetraRoamCandidate {
    uint32_t freq;
    int16_t rssi;
};

struct tetraRoam {
    uint32_t serving; /**< Frequency of the serving cell, Hz */
    uint32_t neighbour[TETRA_ROAM_MAX_NEIGHBOURS];
    uint8_t nNeighbours;
    uint32_t scan[TETRA_ROAM_MAX_SCAN];
    uint8_t nScan;

    enum tetraRoamState state;
    long long since;     /**< Start of the state, or of the try */
    long long lastLock;  /**< Last time locked */
    long long lastCheck; /**< Last check of the neighbours */
    int16_t servingRssi; /**< At the start of the check */
    int32_t average;     /**< Serving cell signal, dBm x 16 */
    long long lastAverage;
    bool averaged;   /**< average holds a value */
    bool urgent;     /**< The check is of a cell about to be lost */

    struct tetraRoamCandidate cand[TETRA_ROAM_MAX_CANDIDATES];
    uint8_t nCand;
    uint8_t next;  /**< Next candidate to probe or try */
    uint32_t best; /**< Stronger neighbour of the last check */
    uint8_t confirmed;
    bool requested; /**< The search was asked for: not stopped by a lock */
};

/**
 * Start tracking from a cell.
 *
 * @param r: tracker.
 * @param serving: frequency of the cell, Hz.
 * @param now: current time, ms.
 */
void tetraRoam_init(struct tetraRoam *r, uint32_t serving, long long now);

/**
 * Add a known carrier of the network, tried when the lock is lost.
 *
 * @param r: tracker.
 * @param freq: frequency, Hz.
 */
void tetraRoam_addScan(struct tetraRoam *r, uint32_t freq);

/**
 * Add neighbours broadcast by the serving cell. A cell names its neighbours
 * a few at a time, so they add up; they are forgotten on a move to another
 * cell, when the oldest make room for new ones too.
 *
 * @param r: tracker.
 * @param freq: their frequencies as the radio tunes them, Hz; zeros are
 * skipped.
 * @param n: number of frequencies.
 */
void tetraRoam_setNeighbours(struct tetraRoam *r, const uint32_t *freq,
                             unsigned n);

/**
 * Search for the best signal now, even while locked: measure every known
 * carrier (neighbours, the channel list, the serving cell), then move to
 * the strongest one that locks. A locked serving cell is kept unless
 * another carrier is TETRA_ROAM_SEARCH_MARGIN_DB stronger. If none of the
 * carriers tried locks, the search goes on as for a lost lock.
 *
 * @param r: tracker.
 * @param now: current time, ms.
 */
void tetraRoam_search(struct tetraRoam *r, long long now);

/**
 * Next step. Call periodically.
 *
 * @param r: tracker.
 * @param now: current time, ms.
 * @param locked: the receiver is locked to a cell.
 * @param decoding: blocks decoded within TETRA_ROAM_DEAF_MS.
 * @param busy: a call is being received: do not probe, unless the cell is
 * about to be lost.
 * @param rssi: signal strength of the serving cell, dBm.
 * @return what to do.
 */
struct tetraRoamStep tetraRoam_tick(struct tetraRoam *r, long long now,
                                    bool locked, bool decoding, bool busy,
                                    int rssi);

/**
 * The receiver stays locked elsewhere meanwhile (following a call on
 * another carrier of the cell): not a lost lock. Call while away.
 *
 * @param r: tracker.
 * @param now: current time, ms.
 */
void tetraRoam_hold(struct tetraRoam *r, long long now);

/**
 * The cell received has its main carrier elsewhere: the receiver locked to
 * a secondary carrier, where calls are not set up. Move to the main
 * carrier, when locked and not searching.
 *
 * @param r: tracker.
 * @param main: the cell's main carrier as the radio tunes it, Hz.
 * @param now: current time, ms.
 * @return a tune to it, or nothing.
 */
struct tetraRoamStep tetraRoam_mainCarrier(struct tetraRoam *r, uint32_t main,
                                           long long now);

/**
 * Report the result of a probe.
 *
 * @param r: tracker.
 * @param freq: frequency probed, Hz.
 * @param rssi: signal strength measured, dBm.
 */
void tetraRoam_probed(struct tetraRoam *r, uint32_t freq, int rssi);

/**
 * @param r: tracker.
 * @return true while searching for a cell, after losing the lock or on
 * request.
 */
bool tetraRoam_searching(const struct tetraRoam *r);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_ROAM_H */
