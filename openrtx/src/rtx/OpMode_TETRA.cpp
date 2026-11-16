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
#include "protocols/TETRA/TetraIds.h"
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
static uint8_t slots;       // Timeslots libtetra decodes
static rtxStatus_t *rtxCfg; // Receiver configuration, set by update()

/**
 * Listen where the follower says: retune and select the timeslots.
 */
static void tune()
{
#ifdef CONFIG_TETRA_FOLLOW
    struct tetraFollowTarget t = tetraFollow_target(&follower);

    if (t.freq == 0 || rtxCfg == NULL)
        return;

    if (t.slots != slots) {
        tetra_tmo_set_slots(&tmo, t.slots);
        slots = t.slots;
    }

    if (t.freq != rtxCfg->rxFrequency) {
        rtxCfg->rxFrequency = t.freq;
        radio_updateConfiguration();
        tetraStatus_setFollow(t.away ? t.freq : 0);
    }
#endif
}

#ifdef CONFIG_TETRA_FOLLOW
/**
 * libtetra event callback: the status, then the call follower.
 */
static void onEvent(void *user, const struct tetra_event *ev)
{
    tetraStatus_onEvent(user, ev);

    switch (ev->type) {
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
            fc.carrier = c->carrier;
            fc.slots = c->timeslots;
            fc.speech = c->circuit_type == 0;
            fc.encrypted = c->encrypted;
            tetraFollow_call(&follower, &fc, getTick());
            tune();
            break;
        }

        case TETRA_EV_TRAFFIC:
            tetraFollow_traffic(&follower, ev->u.traffic.call_id,
                                ev->u.traffic.circuit_type == 0,
                                ev->u.traffic.encrypted, getTick());
            break;

        default:
            break;
    }
}

/**
 * libtetra retune callback: a call moved to another carrier of the cell, or
 * back to the main carrier. The follower decides; the radio retunes before
 * returning, as libtetra keeps decoding with the same slot timing.
 */
static bool onRetune(void *user, const struct tetra_retune *r)
{
    const struct tetra_cell *cell = tetra_tmo_cell(&tmo);
    struct tetraFollowRetune fr;

    (void)user;
    fr.main = r->reason == TETRA_RETUNE_MAIN;
    fr.callId = r->call_id;
    fr.carrier = r->carrier;
    fr.freq = r->freq_hz;
    fr.mainFreq = (cell != NULL && cell->have_sysinfo) ? cell->dl_freq_hz : 0;
    fr.slots = r->timeslots;

    if (!tetraFollow_retune(&follower, &fr, getTick()))
        return false;

    tune();
    return true;
}
#endif

OpMode_TETRA::OpMode_TETRA()
    : startRx(false)
    , dataErrors(0)
    , demodJumps(0)
    , homeFreq(0)
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
    cfg.retune = onRetune;
#else
    cfg.cb = tetraStatus_onEvent;
#endif

    /*
     * Listen only to the identities selected in the built-in list, if any:
     * libtetra then reports, plays and follows only their calls
     */
    size_t nListen;
    cfg.groups = tetraIds_listen(&nListen);
    cfg.ngroups = (uint8_t)nListen;
    cfg.promiscuous = nListen == 0;
    tetra_tmo_init(&tmo, &cfg);
    slots = TETRA_FOLLOW_ALL_SLOTS;

    /* A new receiver knows nothing of the followed call: go home */
    tetraFollow_home(&follower);
    tetraStatus_lostSync(overrun);
    tune();
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

    rtxCfg = rtx;
    if (startRx) {
        startSampling();
        radio_enableRx();
        startRx = false;
    }

    /*
     * A new frequency is a new cell: start from scratch. Other settings keep
     * the receiver. While following a call the radio has just been tuned
     * back to the main carrier: tune() below retunes it.
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
     * it asks to return to the main carrier.
     */
    uint32_t jumps = tetraDemod_reacquisitions(&demod);
    if (jumps != demodJumps) {
        demodJumps = jumps;
        if (!tetraFollow_target(&follower).away)
            resync(false);
    }

    tetra_tmo_push_soft(&tmo, soft, n);

#ifdef CONFIG_TETRA_FOLLOW
    /*
     * The follower left the call's carrier on its own (silence, encryption):
     * libtetra still thinks it is there, so restart its synchronisation on
     * the main carrier. tune() selects all timeslots again.
     */
    if (tetraFollow_tick(&follower, getTick()))
        tetra_tmo_resync(&tmo);
#endif
    /* Also undoes a configuration update that tuned back home meanwhile */
    tune();

    tetraStatus_setDemod(tetraDemod_phaseError(&demod),
                         tetraDemod_carrierOffset(&demod));

    if (tetraStatus_sync())
        platform_ledOn(GREEN);
    else
        platform_ledOff(GREEN);
}
