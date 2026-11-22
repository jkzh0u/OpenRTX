/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_STATUS_H
#define OPENRTX_TETRA_STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include "protocols/TETRA/TetraCalls.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Neighbour cells kept for the field test screen */
#define TETRA_STATUS_NEIGHBOURS 7
/** Carriers of the last signal measurement kept, strongest first */
#define TETRA_STATUS_MEASURED 6
/** roamState without cell tracking */
#define TETRA_STATUS_NO_ROAM 0xff

/** A neighbour cell broadcast by the serving cell */
struct tetraStatusNeighbour {
    uint32_t freq;    /**< Downlink frequency, Hz, 0 if not known */
    uint16_t carrier; /**< Main carrier number */
    uint16_t la;      /**< Location area, 0xffff if the same as the cell's */
    uint8_t load;     /**< Cell load: 0 unknown, 1 low .. 3 high */
    bool other;       /**< Belongs to another network */
};

/** A carrier measured by cell tracking */
struct tetraStatusMeasured {
    uint32_t freq; /**< Hz */
    int16_t rssi;  /**< dBm */
};

/*
 * What the TETRA receive mode has decoded, for the UI. Written by the rtx
 * thread, read with tetra_getStatus().
 */
struct tetraStatus {
    bool sync;             /**< Locked to a TMO downlink */
    bool cell;             /**< Fields below up to dlFreq are valid */
    uint16_t mcc;          /**< Mobile country code */
    uint16_t mnc;          /**< Mobile network code */
    uint8_t colourCode;    /**< Colour code */
    bool sysinfo;          /**< la and dlFreq are valid */
    uint16_t la;           /**< Location area */
    uint32_t dlFreq;       /**< Main carrier downlink frequency, Hz */
    bool airEncryption;    /**< Cell uses air interface encryption */

    bool call;             /**< A call is set up or traffic is flowing */
    bool callGroup;        /**< Group call */
    bool callEncrypted;    /**< Call or its traffic is encrypted */
    bool voice;            /**< Traffic seen in the last second */
    uint16_t callId;       /**< Call identifier */
    uint32_t callDst;      /**< Called (group) SSI, 0 if unknown */
    uint32_t talker;       /**< Talking party SSI, 0 if unknown */
    uint32_t followHz;     /**< Followed traffic carrier, 0 on the main one */
    bool callAlarm;        /**< The call is to or from an alarm talkgroup */
    long long txStart;     /**< Tick the current transmission started */

    uint32_t tunedHz;      /**< Main carrier received, Hz, 0 if not set */
    long long cellChanged; /**< Tick the radio last moved to another cell */
    uint16_t prevLa;       /**< Location area before the last move */
    uint32_t prevDlFreq;   /**< Main carrier before the last move, Hz */
    bool searching;        /**< Looking for a cell on other frequencies */

    struct tetraCalls recent; /**< Recent calls, newest first */

    uint32_t sdsSrc;          /**< Sender of the last SDS, 0 if none */
    char sds[64];             /**< Last SDS text or status, ASCII */
    long long sdsTime;        /**< Tick of the last SDS */

    uint16_t phaseErr;        /**< Demodulator RMS phase error, % of pi/4 */
    uint16_t overruns;        /**< Sample blocks lost: the radio was too slow */
    uint16_t resyncs;         /**< Burst synchronisation restarts */
    int16_t carrierHz;        /**< Carrier offset estimate */

    /* Field test */
    uint8_t load;        /**< Serving cell load: 0 unknown, 1 low .. 3 high */
    uint8_t nNeighbours; /**< Neighbour cells broadcast */
    struct tetraStatusNeighbour neighbour[TETRA_STATUS_NEIGHBOURS];
    bool time;           /**< Network time known */
    bool timeLocal;      /**< Local time, else UTC */
    uint32_t timeOfDay;  /**< Network time at timeTick, s since midnight */
    long long timeTick;  /**< Tick the network time was received */
    uint8_t roamState;   /**< enum tetraRoamState, TETRA_STATUS_NO_ROAM */
    uint8_t nMeasured;   /**< Carriers of the last measurement */
    struct tetraStatusMeasured measured[TETRA_STATUS_MEASURED];
    long long measuredTime; /**< Tick of the last measurement */
};

/**
 * Copy the current TETRA status.
 *
 * @param out: destination.
 */
void tetra_getStatus(struct tetraStatus *out);

/* For the TETRA mode */
struct tetra_event;

/**
 * Clear the status (new frequency, mode change).
 */
void tetraStatus_reset(void);

/**
 * libtetra event callback: updates the status. Pass it in the libtetra
 * receiver configuration.
 */
void tetraStatus_onEvent(void *user, const struct tetra_event *ev);

/**
 * Mark the burst synchronisation as lost, keeping the cell information.
 *
 * @param overrun: because samples were lost.
 */
void tetraStatus_lostSync(bool overrun);

/**
 * Update the demodulator figures.
 */
void tetraStatus_setDemod(unsigned phaseErr, int carrierHz);

/**
 * Set the main carrier being received, after the radio moved to another
 * cell.
 *
 * @param freq: frequency in Hz.
 */
void tetraStatus_setTuned(uint32_t freq);

/**
 * @param searching: true while looking for a cell on other frequencies.
 */
void tetraStatus_setSearching(bool searching);

/**
 * @param state: cell tracking state, enum tetraRoamState.
 */
void tetraStatus_setRoamState(uint8_t state);

/**
 * Set the result of a signal measurement of cell tracking.
 *
 * @param m: carriers measured, strongest first.
 * @param n: number of carriers, at most TETRA_STATUS_MEASURED are kept.
 */
void tetraStatus_setMeasured(const struct tetraStatusMeasured *m, unsigned n);

/**
 * Stop showing the current call, for example when a new filter excludes it.
 */
void tetraStatus_endCall(void);

/**
 * Set the traffic carrier being followed.
 *
 * @param freq: carrier frequency in Hz, 0 when on the main carrier.
 */
void tetraStatus_setFollow(uint32_t freq);

/**
 * Ask the receiver to search for the best signal among the known carriers.
 * Called by the UI.
 */
void tetraStatus_requestSearch(void);

/**
 * @return a number that changes with every search request, for the
 * receiver to notice a new one.
 */
uint8_t tetraStatus_searchRequests(void);

/**
 * @return true while locked to a TMO downlink.
 */
bool tetraStatus_sync(void);

/*
 * Receiver diagnostics, for test and remote control. Read without locking:
 * each field is consistent, the set is approximately so.
 */
struct tetraDiag {
    uint32_t crcOk;      /**< Downlink blocks that passed the CRC, total */
    uint32_t realigned;  /**< libtetra slot grid moves */
    bool locked;         /**< libtetra lock */
    uint32_t reacquired; /**< Demodulator re-acquisitions */
    uint32_t slips;      /**< Demodulator timing jumps followed */
    uint32_t symbols;    /**< Symbols since the last (re)acquisition */
    unsigned phaseErr;   /**< RMS phase error, % of pi/4 */
    int carrierHz;       /**< Carrier offset */
    int clockPpm;        /**< Sample clock offset */
    bool eqSeed;         /**< Equalizer started from the measured taps */
    float eq[9];         /**< Equalizer taps */
};

/**
 * Copy the receiver diagnostics (TETRA mode only, defined by the opmode).
 */
void tetra_getDiag(struct tetraDiag *out);

/**
 * Restart the receiver from scratch at its next update.
 *
 * @param seedEqualizer: start the equalizer from the measured taps
 * (CONFIG_TETRA_EQ_INIT) rather than a pass-through.
 */
void tetra_requestReset(bool seedEqualizer);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_STATUS_H */
