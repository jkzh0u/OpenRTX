/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "interfaces/audio.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "interfaces/radio.h"
#include "protocols/DMR/DmrLink.h"
#include "protocols/DMR/DmrStatus.h"
#include "rtx/OpMode_DMR.hpp"
#ifdef CONFIG_TETRA_LOG
#include "protocols/TETRA/TetraLog.h"
#endif

/* 30 ms per half buffer: one timeslot */
#define HALF_SAMPLES 1440

/* Samples dropped after starting: the receiver and audio path settle */
#define SETTLE_SAMPLES (DMR_DEMOD_IN_RATE / 10)

/* Signal summary in the log this often, ms */
#define LOG_SIGNAL_MS 10000

/*
 * The receive log is the TETRA one (same UART and history): DMR lines start
 * with "dmr".
 */
#ifdef CONFIG_TETRA_LOG
#define DLOG(...) tetraLog_printf(getTick(), __VA_ARGS__)
#else
#define DLOG(...) \
    do {          \
    } while (0)
#endif

/* Large state: static, there is only one DMR mode */
static int16_t baseband[2 * HALF_SAMPLES];
static struct dmrLink receiver;
#ifdef CONFIG_TETRA_LOG
static long long lastSignalLog;
#endif

static const char *syncName(enum dmrSync s)
{
    switch (s) {
        case DMR_SYNC_BS:
            return "base station";
        case DMR_SYNC_MS:
            return "mobile";
        case DMR_SYNC_TDMA1:
            return "direct TS1";
        case DMR_SYNC_TDMA2:
            return "direct TS2";
        default:
            return "none";
    }
}

/** Log line for a call event */
static void logCall(const struct dmrEvent *ev)
{
#ifdef CONFIG_TETRA_LOG
    const struct dmrCall *c = &ev->u.call.call;
    char slot[8], ids[48] = "";

    if (c->slot != 0)
        snprintf(slot, sizeof(slot), "TS%u", c->slot);
    else
        snprintf(slot, sizeof(slot), "simplex");
    if (c->ids)
        snprintf(ids, sizeof(ids), " %s %lu from %lu", c->group ? "TG" : "to",
                 (unsigned long)c->dst, (unsigned long)c->src);

    switch (ev->u.call.what) {
        case DMR_CALL_START:
            DLOG("dmr %s CC%u call%s%s%s", slot, c->colourCode,
                 c->ids ? ids : " (not named yet)",
                 c->encrypted ? ", encrypted" : "",
                 (c->options & DMR_OPT_EMERGENCY) ? ", emergency" : "");
            break;
        case DMR_CALL_UPDATE:
            DLOG("dmr %s CC%u call%s%s%s", slot, c->colourCode, ids,
                 c->lateEntry ? " (joined late)" : "",
                 c->encrypted ? ", encrypted" : "");
            break;
        case DMR_CALL_END:
            DLOG("dmr %s call end%s", slot, ids);
            break;
    }
#else
    (void)ev;
#endif
}

static void onEvent(void *user, const struct dmrEvent *ev)
{
    (void)user;
    bool wanted = dmrStatus_onEvent(ev, getTick());

    switch (ev->type) {
        case DMR_EV_LOCK:
            if (ev->u.lock.locked)
                DLOG("dmr lock: %s", syncName(ev->u.lock.sync));
            else
                DLOG("dmr lock lost");
            break;
        case DMR_EV_CALL:
            if (wanted)
                logCall(ev);
            break;
        case DMR_EV_DATA:
            if (ev->u.data.dataType == DMR_DT_CSBK && ev->u.data.crcOk)
                DLOG("dmr TS%u CC%u CSBK opcode %u", ev->u.data.slot,
                     ev->u.data.colourCode, ev->u.data.csbko);
            break;
    }
}

OpMode_DMR::OpMode_DMR()
    : startRx(false)
    , dataErrors(0)
    , settle(SETTLE_SAMPLES)
    , freq(0)
    , basebandPath(-1)
    , basebandId(-1)
{
}

OpMode_DMR::~OpMode_DMR()
{
    disable();
}

void OpMode_DMR::reset()
{
    settle = SETTLE_SAMPLES;
    dmrLink_reset(&receiver);
    dmrStatus_reset(freq);
#ifdef CONFIG_TETRA_LOG
    if (freq != 0)
        DLOG("dmr start on %lu.%05lu", (unsigned long)(freq / 1000000),
             (unsigned long)(freq % 1000000 / 10));
    lastSignalLog = getTick();
#endif
}

void OpMode_DMR::enable()
{
    static bool initialised;

    if (!initialised) {
        dmrLink_init(&receiver, onEvent, NULL);
        initialised = true;
    }
    freq = 0;
    reset();
    startRx = true;
}

void OpMode_DMR::disable()
{
    startRx = false;
    stopSampling();
    radio_disableRtx();
    platform_ledOff(GREEN);
    dmrLink_reset(&receiver);
    dmrStatus_reset(0);
}

void OpMode_DMR::startSampling()
{
    basebandPath = audioPath_request(SOURCE_RTX, SINK_MCU, PRIO_RX);
    basebandId = audioStream_start(basebandPath, baseband, 2 * HALF_SAMPLES,
                                   DMR_DEMOD_IN_RATE,
                                   STREAM_INPUT | BUF_CIRC_DOUBLE);
}

void OpMode_DMR::stopSampling()
{
    if (basebandId >= 0)
        audioStream_terminate(basebandId);
    if (basebandPath >= 0)
        audioPath_release(basebandPath);
    basebandId = -1;
    basebandPath = -1;
}

void OpMode_DMR::update(rtxStatus_t *const rtx, const bool newCfg)
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

    /* A new frequency: a new signal */
    if ((newCfg || freq == 0) && rtx->rxFrequency != freq) {
        freq = rtx->rxFrequency;
        reset();
    }

    if (audioPath_getStatus(basebandPath) != PATH_OPEN) {
        sleepFor(0, 10);
        return;
    }

    /* A failed read means samples were lost: the bursts' timing jumped */
    dataBlock_t block = inputStream_getData(basebandId);
    if (block.data == NULL) {
        if (dataErrors++ == 0) {
            dmrLink_reset(&receiver);
            DLOG("dmr samples lost: resync");
        } else {
            sleepFor(0, 10);
        }
        return;
    }
    dataErrors = 0;

    if (settle > 0) {
        settle -= (block.len < settle) ? block.len : settle;
        return;
    }

    dmrLink_process(&receiver, block.data, (unsigned)block.len);
    dmrStatus_setDemod(receiver.error, dmrLink_inverted(&receiver), receiver.goodBursts,
                       receiver.badBursts);

#ifdef CONFIG_TETRA_LOG
    if (getTick() - lastSignalLog >= LOG_SIGNAL_MS) {
        lastSignalLog = getTick();
        DLOG("dmr signal %d dBm, %s, symbol error %u%%, bursts %lu ok %lu "
             "bad%s",
             radio_getRssi(), receiver.locked ? "locked" : "no lock",
             (unsigned)(receiver.error * 100.0f),
             (unsigned long)receiver.goodBursts, (unsigned long)receiver.badBursts,
             dmrLink_inverted(&receiver) ? ", inverted" : "");
    }
#endif

    struct dmrStatus s;
    dmr_getStatus(&s);
    if (dmr_shownCall(&s, getTick()) != NULL)
        platform_ledOn(GREEN);
    else
        platform_ledOff(GREEN);
}
