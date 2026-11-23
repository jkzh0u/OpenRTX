/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_DMR_STATUS_H
#define OPENRTX_DMR_STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include "protocols/DMR/DmrLink.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the DMR receive mode hears, for the UI: the signal, each timeslot's
 * call, and the recent calls. Written by the rtx thread, read with
 * dmr_getStatus(). The settings' timeslot and colour code filters apply
 * here: a call they exclude is not shown.
 */

/** Recent calls kept */
#define DMR_STATUS_RECENT 8
/** A call is shown this long after it ended, ms */
#define DMR_STATUS_HANG_MS 3000

/** Timeslot filter */
enum dmrSlotFilter {
    DMR_SLOTS_BOTH = 0,
    DMR_SLOTS_1 = 1,
    DMR_SLOTS_2 = 2,
};

struct dmrStatusCall {
    struct dmrCall call;
    long long start; /**< Tick the call started */
    long long last;  /**< Tick it was last heard, or ended */
    bool active;     /**< Still on */
};

struct dmrStatus {
    bool locked;            /**< Locked to a DMR signal */
    enum dmrSync sync;      /**< What sent it: base station, mobile... */
    bool ccKnown;
    uint8_t colourCode;     /**< Of the last burst decoded */
    long long lastData;     /**< Tick of the last data or idle burst */
    uint8_t lastDataType;   /**< Its data type */
    uint8_t lastDataSlot;
    /** Each timeslot's call (index 1 and 2; 0 for a single transmitter) */
    struct dmrStatusCall slot[3];
    struct dmrStatusCall recent[DMR_STATUS_RECENT]; /**< Newest first */
    uint8_t nRecent;
    uint16_t symbolError; /**< Symbol error, % of the outer level */
    bool inverted;        /**< Discriminator polarity found inverted */
    uint32_t goodBursts;
    uint32_t badBursts;
    uint32_t tunedHz;
};

/**
 * Copy the current DMR status.
 *
 * @param out: destination.
 */
void dmr_getStatus(struct dmrStatus *out);

/**
 * @return the call to show: the newest active one of a timeslot let
 * through, else the last one ended within DMR_STATUS_HANG_MS; NULL if none.
 * Points into status.
 */
const struct dmrStatusCall *dmr_shownCall(const struct dmrStatus *status,
                                          long long now);

/* For the DMR mode */

/**
 * Clear the status (new frequency, mode change).
 *
 * @param freq: frequency received, Hz.
 */
void dmrStatus_reset(uint32_t freq);

/**
 * Handle a receiver event.
 *
 * @param ev: event.
 * @param now: current tick, ms.
 * @return true if the filters let it through (a call they exclude is
 * false).
 */
bool dmrStatus_onEvent(const struct dmrEvent *ev, long long now);

/**
 * Update the demodulator figures.
 */
void dmrStatus_setDemod(float symbolError, bool inverted, uint32_t good,
                        uint32_t bad);

/**
 * Set the filters (from the settings).
 *
 * @param slots: enum dmrSlotFilter.
 * @param colourCode: 0 for any, else the colour code plus one.
 */
void dmrStatus_setFilter(uint8_t slots, uint8_t colourCode);

/**
 * @return true if the filters let a call through.
 */
bool dmrStatus_wanted(const struct dmrCall *c);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_DMR_STATUS_H */
