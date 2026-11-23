/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_DMR_DEMOD_H
#define OPENRTX_DMR_DEMOD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DMR physical layer receiver (ETSI TS 102 361-1, section 10): 4FSK at 4800
 * symbols/s from the 48 kHz output of an FM discriminator (the radio's flat
 * RX audio), cut into bursts.
 *
 *  1. root raised cosine filter (alpha 0.2), 10 samples per symbol
 *  2. synchronisation: correlation with the 48-bit sync patterns, which use
 *     only the outer symbols (+3 and -3); the peak gives the symbol timing,
 *     the DC offset (carrier offset) and the deviation
 *  3. bursts: 132 symbols around the sync, with the 12 CACH symbols before
 *     them on a base station's continuous downlink; between syncs (voice
 *     bursts B to F) at the expected time, 30 ms apart (60 ms on a
 *     mobile's or direct mode transmission)
 *
 * A burst is handed over as bits, with what its sync field held. Which
 * sign the discriminator gives a positive deviation is not known: the link
 * layer finds it (a voice sync inverted is a data sync) and sets it with
 * dmrDemod_setInverted().
 */

#define DMR_DEMOD_IN_RATE 48000
#define DMR_DEMOD_SPS 10
#define DMR_DEMOD_RRC_TAPS 81 /* +-4 symbols */
#define DMR_DEMOD_RING 2048   /* Filtered samples kept, power of two */

#define DMR_BURST_BITS 264
#define DMR_CACH_BITS 24

/** What a sync field held */
enum dmrSync {
    DMR_SYNC_NONE = 0, /**< No sync: embedded signalling, or not found */
    DMR_SYNC_BS,       /**< Base station sourced */
    DMR_SYNC_MS,       /**< Mobile station sourced */
    DMR_SYNC_TDMA1,    /**< TDMA direct mode, timeslot 1 */
    DMR_SYNC_TDMA2,    /**< TDMA direct mode, timeslot 2 */
};

struct dmrBurst {
    uint8_t bits[DMR_BURST_BITS]; /**< One bit per byte */
    uint8_t cach[DMR_CACH_BITS];  /**< Before the burst (base station) */
    enum dmrSync sync;            /**< Sync family found, or none */
    bool voice;      /**< Voice sync (else data), polarity applied */
    bool cachValid;  /**< A base station burst: cach holds the CACH */
    float syncScore; /**< Sync correlation, 0 to 1 */
    float level;     /**< Outer symbol level, in input units */
    float dc;        /**< DC offset of the discriminator, input units */
    float error;     /**< RMS distance of the symbols from the ideal, as a
                          fraction of the outer level */
    uint32_t index;  /**< Burst count since the lock */
};

typedef void (*dmrDemodBurstFn)(void *user, const struct dmrBurst *b);

struct dmrDemod {
    /* Filter */
    float in[2 * DMR_DEMOD_RRC_TAPS];
    unsigned inPos;
    /* Filtered samples */
    float ring[DMR_DEMOD_RING];
    uint32_t n; /**< Samples filtered so far */

    /* Sync search */
    bool locked;
    bool candidate;        /**< A sync seen, looking for its peak */
    uint32_t candidateEnd; /**< Stop looking for a better peak */
    float peakScore;       /**< Best correlation of the current peak */
    uint32_t peakAt;       /**< Sample of the last sync symbol's centre */
    int peakSign;
    float peakLevel, peakDc;

    /* Burst timing, once locked */
    uint32_t syncAt; /**< Centre of the last sync symbol of the next burst */
    bool syncFound;  /**< syncAt came from a sync, not the flywheel */
    bool decided;    /**< Whether this burst has a sync is known */
    enum dmrSync family;
    int syncSign;
    float syncScore;
    uint32_t period; /**< Samples between bursts */
    unsigned missed; /**< Bursts without a sync in a row */
    uint32_t bursts;
    float level, dc;

    bool inverted;
    dmrDemodBurstFn onBurst;
    void *user;
};

/**
 * Initialise.
 *
 * @param d: demodulator.
 * @param onBurst: called for each burst received, from dmrDemod_process().
 * @param user: passed to onBurst.
 */
void dmrDemod_init(struct dmrDemod *d, dmrDemodBurstFn onBurst, void *user);

/**
 * Forget the signal (retune, gap in the samples).
 */
void dmrDemod_reset(struct dmrDemod *d);

/**
 * Process discriminator samples.
 *
 * @param d: demodulator.
 * @param samples: 48 kHz samples.
 * @param n: number of samples.
 */
void dmrDemod_process(struct dmrDemod *d, const int16_t *samples, unsigned n);

/**
 * @param inverted: the discriminator gives positive deviation as negative
 * samples.
 */
void dmrDemod_setInverted(struct dmrDemod *d, bool inverted);

/**
 * @return true while locked to a signal's bursts.
 */
bool dmrDemod_locked(const struct dmrDemod *d);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_DMR_DEMOD_H */
