/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BCAST_H
#define BCAST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Broadcast FM radio (CONFIG_BCAST_FM), 87.5 to 108 MHz, on a receiver chip
 * of its own (radio.h, radio_bcast...()), as on the Quansheng UV-K5. The UI
 * starts, tunes and stops it; the FM mode runs it on the rtx thread. The FM
 * channel keeps being received meanwhile: when its squelch opens the
 * broadcast is muted and the channel heard, and PTT transmits on it.
 */

#define BCAST_MIN_HZ 87500000u
#define BCAST_MAX_HZ 108000000u
#define BCAST_STEP_HZ 100000u
/** Stations saved on the number keys */
#define BCAST_PRESETS 9

/** Seek stops on a station at least this strong and clean */
#define BCAST_SEEK_RSSI 20
#define BCAST_SEEK_SNR 3
/** Time on each frequency while seeking, for the signal to be measured */
#define BCAST_SEEK_SETTLE_MS 60
/** Signal read every this often while playing */
#define BCAST_POLL_MS 500

enum bcastState {
    BCAST_OFF,     /**< Not running */
    BCAST_PLAYING, /**< On a station */
    BCAST_SEEKING, /**< Looking for the next station */
};

struct bcastStatus {
    enum bcastState state;
    uint32_t hz;      /**< Frequency tuned, Hz */
    uint8_t rssi;     /**< 0 to 255 */
    uint8_t snr;      /**< 0 to 15 */
    bool channelBusy; /**< Muted: the FM channel is heard, or transmitting */
};

/**
 * A frequency in the band, on the 100 kHz grid.
 *
 * @param hz: frequency, Hz.
 * @return the nearest valid one.
 */
static inline uint32_t bcast_clamp(uint32_t hz)
{
    if (hz < BCAST_MIN_HZ)
        return BCAST_MIN_HZ;
    if (hz > BCAST_MAX_HZ)
        return BCAST_MAX_HZ;
    return BCAST_MIN_HZ
         + (hz - BCAST_MIN_HZ + BCAST_STEP_HZ / 2) / BCAST_STEP_HZ
               * BCAST_STEP_HZ;
}

/**
 * One step up or down, round from one end of the band to the other.
 *
 * @param hz: frequency, Hz.
 * @param dir: +1 or -1.
 * @return the next frequency.
 */
static inline uint32_t bcast_step(uint32_t hz, int dir)
{
    if (dir > 0)
        return (hz + BCAST_STEP_HZ > BCAST_MAX_HZ) ? BCAST_MIN_HZ :
                                                     hz + BCAST_STEP_HZ;
    return (hz < BCAST_MIN_HZ + BCAST_STEP_HZ) ? BCAST_MAX_HZ :
                                                 hz - BCAST_STEP_HZ;
}

/** Start listening (UI). @param hz: station, Hz. */
void bcast_start(uint32_t hz);

/** Tune another station (UI). @param hz: station, Hz. */
void bcast_tune(uint32_t hz);

/** Look for the next station up (+1) or down (-1) (UI). */
void bcast_seek(int dir);

/** Stop, and power the receiver off (UI). */
void bcast_stop(void);

/** Stop and power the receiver off now (FM mode, when it is disabled). */
void bcast_halt(void);

/** @return true between bcast_start() and bcast_stop(). */
bool bcast_requested(void);

/** Copy the state (UI). */
void bcast_get(struct bcastStatus *out);

/**
 * For the FM mode, on each update: powers, tunes, seeks and mutes the
 * broadcast receiver.
 *
 * @param channelOpen: the FM channel's squelch is open.
 * @param transmitting: the radio transmits.
 * @param now: current tick, ms.
 * @return true while the broadcast is to be heard: the FM mode keeps the
 * receive audio path open, with the transceiver's own audio off.
 */
bool bcast_update(bool channelOpen, bool transmitting, long long now);

#ifdef __cplusplus
}
#endif

#endif /* BCAST_H */
