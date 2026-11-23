/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_DMR_LINK_H
#define OPENRTX_DMR_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "protocols/DMR/DmrDemod.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DMR data link layer receiver (ETSI TS 102 361-1, sections 6 to 9), on
 * top of the demodulator: which timeslot a burst is in, its slot type, and
 * the calls of each timeslot from their link control: the voice LC header
 * and terminator, and the link control embedded in the voice superframe,
 * which names a call joined late.
 *
 * Receive only, and no voice: DMR speech is AMBE+2, a proprietary codec.
 * What it gives is who calls whom, on which timeslot and colour code.
 */

/** Data types of the slot type (ETSI TS 102 361-1, 9.3.6) */
enum dmrDataType {
    DMR_DT_PI_HEADER = 0,
    DMR_DT_VOICE_LC_HEADER = 1,
    DMR_DT_TERMINATOR_LC = 2,
    DMR_DT_CSBK = 3,
    DMR_DT_MBC_HEADER = 4,
    DMR_DT_MBC_CONTINUATION = 5,
    DMR_DT_DATA_HEADER = 6,
    DMR_DT_RATE_12_DATA = 7,
    DMR_DT_RATE_34_DATA = 8,
    DMR_DT_IDLE = 9,
    DMR_DT_RATE_1_DATA = 10,
};

/** Full link control opcodes (FLCO) of voice calls */
#define DMR_FLCO_GROUP 0
#define DMR_FLCO_PRIVATE 3

/** Service options of a voice call's link control */
#define DMR_OPT_EMERGENCY 0x80
#define DMR_OPT_PRIVACY 0x40

/** Bursts since the last voice burst of a timeslot before its call ends */
#define DMR_LINK_CALL_TIMEOUT 12

struct dmrCall {
    uint8_t slot;       /**< Timeslot 1 or 2, 0 for a single transmitter */
    uint8_t colourCode; /**< 0 to 15 */
    bool ids;           /**< dst, src, group and options are known */
    bool group;         /**< Group call (else private, unit to unit) */
    uint8_t fid;        /**< Feature set ID of the link control */
    uint8_t options;    /**< Service options */
    uint32_t dst;       /**< Talkgroup or called radio */
    uint32_t src;       /**< Talking radio */
    bool encrypted;     /**< Privacy: PI header, EMB PI or options */
    bool lateEntry;     /**< Named by the embedded link control */
};

enum dmrEventType {
    DMR_EV_LOCK,  /**< Lock to a signal gained or lost */
    DMR_EV_CALL,  /**< A call started, was named, or ended */
    DMR_EV_DATA,  /**< A data burst (CSBK, data, idle...) */
};

enum dmrCallWhat {
    DMR_CALL_START,
    DMR_CALL_UPDATE, /**< Identities learned or changed, encryption seen */
    DMR_CALL_END,
};

struct dmrEvent {
    enum dmrEventType type;
    union {
        struct {
            bool locked;
            enum dmrSync sync; /**< Sync family locked to */
        } lock;
        struct {
            enum dmrCallWhat what;
            struct dmrCall call;
        } call;
        struct {
            uint8_t slot;
            uint8_t colourCode;
            uint8_t dataType;
            uint8_t csbko; /**< CSBK opcode, for DMR_DT_CSBK */
            bool crcOk;    /**< CSBK CRC passed */
        } data;
    } u;
};

typedef void (*dmrLinkEventFn)(void *user, const struct dmrEvent *ev);

struct dmrLinkSlot {
    bool voice;          /**< In a voice superframe */
    uint8_t pos;         /**< Superframe position of the last voice burst */
    uint32_t lastVoice;  /**< Burst index of the last voice burst */
    uint8_t emb[128];    /**< Embedded signalling fragments so far */
    uint8_t embCount;
    bool active;         /**< A call is on */
    uint32_t lastActive; /**< Burst index of the call's last burst */
    struct dmrCall call;
};

struct dmrLink {
    struct dmrDemod demod;
    struct dmrLinkSlot slot[2];
    bool locked;
    uint8_t slotPhase;   /**< Timeslot of burst index 0 */
    bool phaseKnown;
    uint8_t phaseMisses; /**< CACH timeslot bits disagreeing in a row */
    int8_t polarityVotes;
    uint8_t lastSlot;    /**< Timeslot of the last direct mode burst */
    uint32_t lastIndex;
    /* Statistics */
    uint32_t goodBursts; /**< Slot types or EMBs decoded */
    uint32_t badBursts;
    float error;         /**< Symbol error of the last burst, smoothed */
    float dc;            /**< Discriminator DC of the last sync */
    float level;         /**< Outer symbol level of the last sync */
    dmrLinkEventFn onEvent;
    void *user;
};

/**
 * Initialise.
 *
 * @param l: receiver.
 * @param onEvent: called for each event, from dmrLink_process().
 * @param user: passed to onEvent.
 */
void dmrLink_init(struct dmrLink *l, dmrLinkEventFn onEvent, void *user);

/**
 * Forget the signal (retune, gap in the samples): calls in progress end.
 */
void dmrLink_reset(struct dmrLink *l);

/**
 * Process discriminator samples.
 *
 * @param l: receiver.
 * @param samples: 48 kHz samples.
 * @param n: number of samples.
 */
void dmrLink_process(struct dmrLink *l, const int16_t *samples, unsigned n);

/**
 * Handle one burst: for tests, and called by the demodulator.
 */
void dmrLink_burst(void *link, const struct dmrBurst *b);

/**
 * @return true if the discriminator's polarity was found inverted.
 */
bool dmrLink_inverted(const struct dmrLink *l);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_DMR_LINK_H */
