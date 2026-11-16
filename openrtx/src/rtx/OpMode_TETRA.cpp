/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <hwconfig.h>
#include <tetra/tetra.h>
#include "interfaces/audio.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "interfaces/radio.h"
#include "protocols/TETRA/TetraDemod.h"
#include "protocols/TETRA/TetraFollow.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"
#include "rtx/OpMode_TETRA.hpp"

/* 30 ms per half buffer: room for libtetra's per-slot decoding */
#define HALF_SAMPLES 1440

/* Large state: static, there is only one TETRA mode */
static int16_t baseband[2 * HALF_SAMPLES];
static int8_t soft[HALF_SAMPLES * 3 / 4 + 8];
static struct tetraDemod demod;
static struct tetra_tmo_rx tmo;
static struct tetraFollow follower;

#ifdef CONFIG_TETRA_FOLLOW
/**
 * Frequency to tune for a carrier number of the cell, 0 if unknown.
 */
static uint32_t carrierFreq(uint16_t carrier)
{
    const struct tetra_cell *cell = tetra_tmo_cell(&tmo);

    if (carrier == 0xffff || cell == NULL || !cell->have_sysinfo
        || cell->band == 0)
        return 0;

    return tetraFollow_carrierFreq(follower.home, cell->dl_freq_hz,
                                   tetra_carrier_hz(cell->band, carrier,
                                                    cell->offset));
}

/**
 * libtetra event callback: the status, then the call follower.
 */
static void onEvent(void *user, const struct tetra_event *ev)
{
    tetraStatus_onEvent(user, ev);

    switch (ev->type) {
        case TETRA_EV_SYNC:
            if (!ev->u.sync.locked)
                tetraFollow_lostSync(&follower);
            break;

        case TETRA_EV_CALL: {
            const struct tetra_call *c = &ev->u.call;
            struct tetraFollowCall fc;

            switch (c->what) {
                case TETRA_CALL_RELEASE:
                    fc.event = TETRA_FOLLOW_RELEASE;
                    break;
                case TETRA_CALL_TX_CEASED:
                    fc.event = TETRA_FOLLOW_CEASED;
                    break;
                default:
                    fc.event = TETRA_FOLLOW_UPDATE;
                    break;
            }
            fc.callId = c->call_id;
            fc.freq = carrierFreq(c->carrier);
            fc.slots = c->timeslots;
            fc.speech = c->circuit_type == 0;
            fc.encrypted = c->encrypted;
            tetraFollow_call(&follower, &fc, getTick());
            break;
        }

        case TETRA_EV_TRAFFIC:
            tetraFollow_traffic(&follower, ev->u.traffic.call_id,
                                ev->u.traffic.encrypted, getTick());
            break;

        default:
            break;
    }
}
#endif

OpMode_TETRA::OpMode_TETRA()
    : startRx(false)
    , dataErrors(0)
    , demodJumps(0)
    , homeFreq(0)
    , slots(TETRA_FOLLOW_ALL_SLOTS)
    , basebandPath(-1)
    , basebandId(-1)
{
}

OpMode_TETRA::~OpMode_TETRA()
{
    disable();
}

void OpMode_TETRA::reset()
{
    tetraDemod_init(&demod);
#ifdef CONFIG_TETRA_EQ_INIT
    static const float eqInit[TETRA_DEMOD_EQ_TAPS] = CONFIG_TETRA_EQ_INIT;
    tetraDemod_setEqualizer(&demod, eqInit);
#endif

    demodJumps = 0;
    tetraFollow_init(&follower, homeFreq);
    resync(false);
    tetraStatus_reset();
}

/*
 * Restart libtetra's burst synchronisation. After a jump in the symbol
 * stream (samples lost, the demodulator re-acquiring) it would otherwise
 * keep decoding on the old slot grid for seconds before noticing.
 */
void OpMode_TETRA::resync(bool overrun)
{
    struct tetra_rx_config cfg;
    memset(&cfg, 0, sizeof(cfg));
#ifdef CONFIG_TETRA_FOLLOW
    cfg.cb = onEvent;
#else
    cfg.cb = tetraStatus_onEvent;
#endif
    cfg.promiscuous = true;
    tetra_tmo_init(&tmo, &cfg);
    slots = TETRA_FOLLOW_ALL_SLOTS;
    tetraFollow_lostSync(&follower);
    tetraStatus_lostSync(overrun);
}

/**
 * @return true while following a call on a carrier other than the main one.
 */
bool OpMode_TETRA::away()
{
    return tetraFollow_target(&follower).away;
}

/**
 * Listen where the follower says: retune and select the timeslots. libtetra
 * is not reset, all carriers of the cell share its slot timing.
 */
void OpMode_TETRA::applyFollow(rtxStatus_t *const rtx)
{
#ifdef CONFIG_TETRA_FOLLOW
    tetraFollow_tick(&follower, getTick());
    struct tetraFollowTarget t = tetraFollow_target(&follower);

    if (t.freq == 0)
        return;

    if (t.slots != slots) {
        tetra_tmo_set_slots(&tmo, t.slots);
        slots = t.slots;
    }

    if (t.freq != rtx->rxFrequency) {
        rtx->rxFrequency = t.freq;
        radio_updateConfiguration();
        tetraStatus_setFollow(t.away ? t.freq : 0);
    }
#else
    (void)rtx;
#endif
}

void OpMode_TETRA::enable()
{
    homeFreq = 0;
    reset();
    tetraSpeech_start();
    startRx = true;
}

void OpMode_TETRA::disable()
{
    startRx = false;
    stopSampling();
    tetraSpeech_stop();
    radio_disableRtx();
    platform_ledOff(GREEN);
    tetraStatus_reset();
}

void OpMode_TETRA::startSampling()
{
    basebandPath = audioPath_request(SOURCE_RTX, SINK_MCU, PRIO_RX);
    basebandId = audioStream_start(basebandPath, baseband, 2 * HALF_SAMPLES,
                                   TETRA_DEMOD_IN_RATE,
                                   STREAM_INPUT | BUF_CIRC_DOUBLE);
}

void OpMode_TETRA::stopSampling()
{
    if (basebandId >= 0)
        audioStream_terminate(basebandId);
    if (basebandPath >= 0)
        audioPath_release(basebandPath);
    basebandId = -1;
    basebandPath = -1;
}

void OpMode_TETRA::update(rtxStatus_t *const rtx, const bool newCfg)
{
    switch (rtx->opStatus) {
        case OFF:
            radio_disableRtx();
            if (startRx) {
                rtx->opStatus = RX;
                return;
            }
            sleepFor(0, 30);
            return;

        case RX:
            break;

        default:
            /* Receive only */
            rtx->opStatus = OFF;
            startRx = true;
            return;
    }

    if (startRx) {
        startSampling();
        radio_enableRx();
        startRx = false;
    }

    /*
     * A new frequency is a new cell: start from scratch. Other settings keep
     * the receiver. While following a call the radio has just been tuned
     * back to the main carrier: applyFollow() retunes it.
     */
    if ((newCfg || homeFreq == 0) && rtx->rxFrequency != homeFreq) {
        homeFreq = rtx->rxFrequency;
        reset();
    }

    if (audioPath_getStatus(basebandPath) != PATH_OPEN) {
        sleepFor(0, 10);
        return;
    }

    /*
     * Wait for the next half buffer of samples. A failed read means we were
     * late and samples were lost: the symbol stream jumped, so resync, and
     * do not lose more by sleeping (unless the stream keeps failing).
     */
    dataBlock_t block = inputStream_getData(basebandId);
    if (block.data == NULL) {
        if (dataErrors++ == 0)
            resync(true);
        else
            sleepFor(0, 10);
        return;
    }
    dataErrors = 0;

    size_t n = tetraDemod_process(&demod, block.data, block.len, soft);

    /*
     * The demodulator followed a timing jump or re-acquired: resync too.
     * Except away from the main carrier, which may have no synchronisation
     * bursts to lock to again: there libtetra keeps its slot timing,
     * searching a few bits either side of it, and if it does lose the lock
     * the follower returns to the main carrier.
     */
    uint32_t jumps = tetraDemod_reacquisitions(&demod);
    if (jumps != demodJumps) {
        demodJumps = jumps;
        if (!away())
            resync(false);
    }

    tetra_tmo_push_soft(&tmo, soft, n);
    applyFollow(rtx);

    tetraStatus_setDemod(tetraDemod_phaseError(&demod),
                         tetraDemod_carrierOffset(&demod));

    if (tetraStatus_sync())
        platform_ledOn(GREEN);
    else
        platform_ledOff(GREEN);
}
