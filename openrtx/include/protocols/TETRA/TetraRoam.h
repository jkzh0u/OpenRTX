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
 * - While locked, if the serving cell gets weak, it measures the signal of
 *   each neighbour, a few milliseconds each, and moves to one that is
 *   clearly stronger, twice in a row.
 * - When the lock is lost, it measures every known carrier (neighbours
 *   first, then the frequencies of the built-in channel list), and tries
 *   the strongest ones in turn until one locks.
 *
 * The caller carries out the steps: a probe is a short retune to read the
 * signal strength, then back; a tune is a move to another cell, after which
 * the receiver synchronises again.
 *
 * Independent of the radio and of libtetra.
 */

/** Neighbour cells kept */
#define TETRA_ROAM_MAX_NEIGHBOURS 8
/** Other known carriers kept */
#define TETRA_ROAM_MAX_SCAN 48
/** Candidates of a search */
#define TETRA_ROAM_MAX_CANDIDATES \
    (TETRA_ROAM_MAX_NEIGHBOURS + TETRA_ROAM_MAX_SCAN + 1)

/** No lock for this long: search, ms */
#define TETRA_ROAM_LOST_MS 2500
/** Time between checks of the neighbours while the cell is weak, ms */
#define TETRA_ROAM_CHECK_MS 3000
/** Serving cell weaker than this: check the neighbours, dBm */
#define TETRA_ROAM_WEAK_DBM -95
/** A neighbour must be this much stronger to move to it, dB */
#define TETRA_ROAM_MARGIN_DB 6
/** Weaker than this: no signal worth trying, dBm */
#define TETRA_ROAM_FLOOR_DBM -118
/** Time to wait for a lock on a carrier tried, ms */
#define TETRA_ROAM_TRY_MS 2500
/** Pause after a search found nothing, ms */
#define TETRA_ROAM_PAUSE_MS 2000

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
    TETRA_ROAM_SWEEP,  /**< Lost: measuring every known carrier */
    TETRA_ROAM_TRY,    /**< Lost: trying candidates for a lock */
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

    struct tetraRoamCandidate cand[TETRA_ROAM_MAX_CANDIDATES];
    uint8_t nCand;
    uint8_t next;  /**< Next candidate to probe or try */
    uint32_t best; /**< Stronger neighbour of the last check */
    uint8_t confirmed;
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
 * Set the neighbours broadcast by the serving cell.
 *
 * @param r: tracker.
 * @param freq: their frequencies as the radio tunes them, Hz; zeros are
 * skipped.
 * @param n: number of frequencies.
 */
void tetraRoam_setNeighbours(struct tetraRoam *r, const uint32_t *freq,
                             unsigned n);

/**
 * Next step. Call periodically.
 *
 * @param r: tracker.
 * @param now: current time, ms.
 * @param locked: the receiver is locked to a cell.
 * @param busy: a call is being received: do not probe.
 * @param rssi: signal strength of the serving cell, dBm.
 * @return what to do.
 */
struct tetraRoamStep tetraRoam_tick(struct tetraRoam *r, long long now,
                                    bool locked, bool busy, int rssi);

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
 * @return true while searching for a cell after losing the lock.
 */
bool tetraRoam_searching(const struct tetraRoam *r);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_ROAM_H */
