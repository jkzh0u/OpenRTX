/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hwconfig.h"
#include "core/bcast.h"
#include "interfaces/radio.h"

#ifdef CONFIG_BCAST_FM

/* Asked by the UI */
static volatile bool requested;
static volatile uint32_t wantHz;
static volatile int8_t seekDir;  /* 0: no seek asked */
static volatile uint8_t tuneSeq; /* Changes on each tune or seek asked */

/* The rtx thread's state */
static bool powered;
static bool muted;
static uint8_t doneSeq;
static uint32_t seekFrom;
static long long since; /* Last retune while seeking, last poll */
static struct bcastStatus status;

void bcast_start(uint32_t hz)
{
    wantHz = bcast_clamp(hz);
    seekDir = 0;
    tuneSeq++;
    requested = true;
}

void bcast_tune(uint32_t hz)
{
    wantHz = bcast_clamp(hz);
    seekDir = 0;
    tuneSeq++;
}

void bcast_seek(int dir)
{
    seekDir = (dir > 0) ? 1 : -1;
    tuneSeq++;
}

void bcast_stop(void)
{
    requested = false;
}

void bcast_halt(void)
{
    requested = false;
    if (powered) {
        radio_bcastPower(0);
        powered = false;
    }
    status.state = BCAST_OFF;
}

bool bcast_requested(void)
{
    return requested;
}

void bcast_get(struct bcastStatus *out)
{
    *out = status;
    if (!requested)
        out->state = BCAST_OFF;
}

static void setMute(bool mute)
{
    if (mute != muted) {
        radio_bcastMute(mute);
        muted = mute;
    }
}

/** A station here: strong enough, clean enough, and on its frequency */
static bool isStation(void)
{
    bool railed;
    radio_bcastSignal(&status.rssi, &status.snr, &railed);
    return !railed && status.rssi >= BCAST_SEEK_RSSI
        && status.snr >= BCAST_SEEK_SNR;
}

bool bcast_update(bool channelOpen, bool transmitting, long long now)
{
    if (!requested) {
        if (powered) {
            radio_bcastPower(0);
            powered = false;
            status.state = BCAST_OFF;
        }
        return false;
    }

    if (!powered) {
        /* About 0.3 s the first time: the chip's start-up */
        status.hz = wantHz;
        radio_bcastPower(status.hz);
        powered = true;
        muted = false;
        doneSeq = tuneSeq;
        status.state = BCAST_PLAYING;
        since = now;
    }

    /* A new tune or seek from the UI */
    if (doneSeq != tuneSeq) {
        doneSeq = tuneSeq;
        if (seekDir != 0) {
            status.state = BCAST_SEEKING;
            seekFrom = status.hz;
            status.hz = bcast_step(status.hz, seekDir);
        } else {
            status.state = BCAST_PLAYING;
            status.hz = wantHz;
        }
        radio_bcastTune(status.hz);
        since = now;
    }

    if (status.state == BCAST_SEEKING && now - since >= BCAST_SEEK_SETTLE_MS) {
        if (isStation() || status.hz == seekFrom) {
            /* Found, or round the whole band */
            status.state = BCAST_PLAYING;
            seekDir = 0;
            wantHz = status.hz;
        } else {
            status.hz = bcast_step(status.hz, seekDir);
            radio_bcastTune(status.hz);
        }
        since = now;
    } else if (status.state == BCAST_PLAYING && now - since >= BCAST_POLL_MS) {
        isStation();
        since = now;
    }

    /* The channel comes first; nothing while seeking */
    status.channelBusy = channelOpen || transmitting;
    setMute(status.channelBusy || status.state == BCAST_SEEKING);
    return !status.channelBusy;
}

#endif /* CONFIG_BCAST_FM */
