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
#include "interfaces/cps_io.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "interfaces/radio.h"
#include "protocols/TETRA/TetraDemod.h"
#include "protocols/TETRA/TetraFilter.h"
#include "protocols/TETRA/TetraFollow.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraLog.h"
#include "protocols/TETRA/TetraLogEvents.h"
#include "protocols/TETRA/TetraRoam.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"
#include "rtx/OpMode_TETRA.hpp"

/* 30 ms per half buffer: room for libtetra's per-slot decoding */
#define HALF_SAMPLES 1440

/* Time on another carrier to read its signal strength, ms */
#define ROAM_PROBE_MS 5

/* Signal summary in the log this often, ms */
#define LOG_SIGNAL_MS 10000

#ifdef CONFIG_TETRA_LOG
#define TLOG(...) tetraLog_printf(getTick(), __VA_ARGS__)
#else
#define TLOG(...) \
    do {          \
    } while (0)
#endif

/* Large state: static, there is only one TETRA mode */
static int16_t baseband[2 * HALF_SAMPLES];
static int8_t soft[HALF_SAMPLES * 3 / 4 + 8];
static struct tetraDemod demod;
static struct tetra_tmo_rx tmo;
static struct tetraFollow follower;
static struct tetraFilter filter;
static uint8_t filterSelection; // tetraIds_selection() applied to filter
static struct tetraRoam roam;
static uint8_t searchRequests; // tetraStatus_searchRequests() handled
static long long lastSignalLog;
static uint8_t slots;       // Timeslots libtetra decodes
static rtxStatus_t *rtxCfg; // Receiver configuration, set by update()

#ifdef CONFIG_TETRA_LOG
/** A frequency for a log line; two can be used in one line */
static const char *mhz(uint32_t hz)
{
    static char buf[2][16];
    static unsigned next;

    next = (next + 1) % 2;
    return tetraLog_freq(buf[next], sizeof(buf[next]), hz);
}
#endif

/**
 * Listen where the follower says, on the serving cell or a followed call's
 * carrier: retune and select the timeslots.
 */
static void tune()
{
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
}

/**
 * @return true while receiving a followed call's carrier, which carries
 * only that call's timeslots.
 */
static bool following()
{
#ifdef CONFIG_TETRA_FOLLOW
    return tetraFollow_target(&follower).away;
#else
    return false;
#endif
}

/**
 * Talkgroup filter: let an event through to the status, the speaker and
 * the call follower?
 */
static bool filterEvent(const struct tetra_event *ev)
{
    switch (ev->type) {
        case TETRA_EV_CALL: {
            const struct tetra_call *c = &ev->u.call;
            return tetraFilter_call(&filter, c->call_id, c->dst_ssi,
                                    c->src_ssi,
                                    c->what == TETRA_CALL_RELEASE);
        }

        case TETRA_EV_TRAFFIC:
            return tetraFilter_traffic(&filter, ev->u.traffic.call_id,
                                       ev->u.traffic.talker_ssi, following());

        case TETRA_EV_SDS:
            return tetraFilter_sds(&filter, ev->u.sds.src_ssi,
                                   ev->u.sds.dst_ssi);

        default:
            return true;
    }
}

#ifdef CONFIG_TETRA_ROAM
/**
 * The serving cell's neighbours, as the radio tunes them: their frequency
 * plus the radio's tuning offset from the serving cell.
 */
static void onNeighbours(const struct tetra_network *nw)
{
    const struct tetra_cell *cell = tetra_tmo_cell(&tmo);
    uint32_t freq[TETRA_ROAM_MAX_NEIGHBOURS];
    unsigned n = 0;

    if (cell == NULL || !cell->have_sysinfo)
        return;

    for (unsigned i = 0; i < nw->nneighbours && n < TETRA_ROAM_MAX_NEIGHBOURS;
         i++) {
        const struct tetra_neighbour *nb = &nw->neighbour[i];

        /* Same network only */
        if ((nb->mcc != 0xffff && nb->mcc != cell->mcc)
            || (nb->mnc != 0xffff && nb->mnc != cell->mnc))
            continue;
        freq[n++] = tetraFollow_carrierFreq(roam.serving, cell->dl_freq_hz,
                                            nb->dl_freq_hz);
    }

    tetraRoam_setNeighbours(&roam, freq, n);
}

#ifdef CONFIG_TETRA_LOG
/** The strongest carriers of the last measurement */
static void roamLogMeasured(void)
{
    char line[120];
    size_t len = 0;
    bool shown[TETRA_ROAM_MAX_CANDIDATES] = { false };
    unsigned measured = 0, nShown = 0;

    line[0] = '\0';
    for (unsigned i = 0; i < roam.nCand; i++) {
        if (roam.cand[i].rssi > -32768)
            measured++;
    }

    while (nShown < 4 && nShown < measured) {
        int best = -1;
        for (unsigned i = 0; i < roam.nCand; i++) {
            if (!shown[i] && roam.cand[i].rssi > -32768
                && (best < 0 || roam.cand[i].rssi > roam.cand[best].rssi))
                best = (int)i;
        }
        shown[best] = true;
        nShown++;
        int w = snprintf(line + len, sizeof(line) - len, "%s%s %d",
                         len ? ", " : "", mhz(roam.cand[best].freq),
                         roam.cand[best].rssi);
        if (w < 0 || (size_t)w >= sizeof(line) - len)
            break;
        len += (size_t)w;
    }

    TLOG("roam: measured %u: %s%s", measured, line,
         measured > nShown ? ", ..." : "");
}

/** Signal strength measured for a carrier, or 0 */
static int roamRssi(uint32_t freq)
{
    for (unsigned i = 0; i < roam.nCand; i++) {
        if (roam.cand[i].freq == freq && roam.cand[i].rssi > -32768)
            return roam.cand[i].rssi;
    }

    return 0;
}

/**
 * What cell tracking just did, from its state before and after a step.
 */
static void roamLog(enum tetraRoamState before, bool measuredAll,
                    const struct tetraRoamStep *s, int rssi)
{
    enum tetraRoamState after = roam.state;

    if ((before == TETRA_ROAM_CHECK || before == TETRA_ROAM_SWEEP)
        && after != before && !measuredAll)
        TLOG("roam: measuring stopped (%s)",
             before == TETRA_ROAM_SWEEP ? "lock back" : "call or no lock");

    if (after == TETRA_ROAM_CHECK && before != TETRA_ROAM_CHECK)
        TLOG("roam: weak cell (%d dBm), measuring %u neighbours", rssi,
             roam.nCand);
    if (after == TETRA_ROAM_SWEEP && before != TETRA_ROAM_SWEEP)
        TLOG("roam: no lock, measuring %u carriers", roam.nCand);

    if (s->action == TETRA_ROAM_TUNE) {
        int level = roamRssi(s->freq);
        if (before == TETRA_ROAM_CHECK)
            TLOG("roam: move to a stronger neighbour >> %s (%d dBm)",
                 mhz(s->freq), level);
        else
            TLOG("roam: try >> %s (%d dBm)", mhz(s->freq), level);
    }

    if (after == TETRA_ROAM_PAUSE && before != TETRA_ROAM_PAUSE)
        TLOG("roam: nothing above %d dBm, waiting", TETRA_ROAM_FLOOR_DBM);
    if (before == TETRA_ROAM_TRY && after == TETRA_ROAM_LOCKED)
        TLOG("roam: locked on %s", mhz(roam.serving));
    if (before == TETRA_ROAM_SWEEP && after == TETRA_ROAM_LOCKED
        && measuredAll)
        TLOG("roam: staying on %s", mhz(roam.serving));
}
#endif

/**
 * One step of cell tracking: measure another carrier for a few
 * milliseconds, or move to another cell and synchronise there.
 */
static void roamStep()
{
    /* While following a call, the follower owns the tuning */
    if (tetraFollow_target(&follower).away || rtxCfg == NULL)
        return;

    struct tetraStatus ts;
    tetra_getStatus(&ts);
    bool busy = ts.call || follower.selected;
    enum tetraRoamState before = roam.state;

    /* Search for the best signal, asked for on the keypad */
    if (tetraStatus_searchRequests() != searchRequests) {
        searchRequests = tetraStatus_searchRequests();
        tetraRoam_search(&roam, getTick());
        TLOG("roam: search asked, measuring %u carriers", roam.nCand);
        before = roam.state;
    }

    int rssi = radio_getRssi();
    bool measuring = roam.state == TETRA_ROAM_CHECK
                  || roam.state == TETRA_ROAM_SWEEP;
    bool measuredAll = measuring && roam.next >= roam.nCand;
#ifdef CONFIG_TETRA_LOG
    /* This step decides on the measurement: show it, all of it */
    if (measuredAll)
        roamLogMeasured();
#endif
    struct tetraRoamStep s = tetraRoam_tick(&roam, getTick(), ts.sync, busy,
                                            rssi);

    switch (s.action) {
        case TETRA_ROAM_PROBE: {
            uint32_t back = rtxCfg->rxFrequency;
            rtxCfg->rxFrequency = s.freq;
            radio_updateConfiguration();
            delayMs(ROAM_PROBE_MS);
            int level = radio_getRssi();
            rtxCfg->rxFrequency = back;
            radio_updateConfiguration();
            tetraRoam_probed(&roam, s.freq, level);
            break;
        }

        case TETRA_ROAM_TUNE:
            tetraFollow_setHome(&follower, s.freq);
            tetra_tmo_resync(&tmo);
            tetraStatus_setTuned(s.freq);
            tune();
            break;

        default:
            break;
    }

#ifdef CONFIG_TETRA_LOG
    roamLog(before, measuredAll, &s, rssi);
#endif
    tetraStatus_setSearching(tetraRoam_searching(&roam));
}

/**
 * Start tracking from the configured frequency, with the TETRA channels of
 * the channel list as the carriers to search.
 */
static void roamInit(uint32_t home)
{
    channel_t ch;

    tetraRoam_init(&roam, home, getTick());
    searchRequests = tetraStatus_searchRequests();
    for (uint16_t i = 0; cps_readChannel(&ch, i) == 0; i++) {
        if (ch.mode == OPMODE_TETRA)
            tetraRoam_addScan(&roam, ch.rx_frequency);
    }
}
#endif

/**
 * libtetra event callback: through the talkgroup filter to the status, then
 * the call follower.
 */
static void onEvent(void *user, const struct tetra_event *ev)
{
    bool passed = filterEvent(ev);

#ifdef CONFIG_TETRA_LOG
    tetraLogEvents_event(getTick(), ev, passed);
#endif
    if (!passed)
        return;

    tetraStatus_onEvent(user, ev);

#ifdef CONFIG_TETRA_ROAM
    if (ev->type == TETRA_EV_NETWORK)
        onNeighbours(ev->u.network);
#endif

#ifdef CONFIG_TETRA_FOLLOW
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
#endif
}

#ifdef CONFIG_TETRA_FOLLOW
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

    /* Calls the talkgroup filter holds back are not followed */
    if (r->reason == TETRA_RETUNE_CALL
        && !tetraFilter_passes(&filter, r->call_id)) {
        TLOG("retune: call %u to carrier %u: filtered out", r->call_id,
             r->carrier);
        return false;
    }

    fr.main = r->reason == TETRA_RETUNE_MAIN;
    fr.callId = r->call_id;
    fr.carrier = r->carrier;
    fr.freq = r->freq_hz;
    fr.mainFreq = (cell != NULL && cell->have_sysinfo) ? cell->dl_freq_hz : 0;
    fr.slots = r->timeslots;

    if (!tetraFollow_retune(&follower, &fr, getTick())) {
        if (fr.main)
            TLOG("retune: main carrier: declined");
        else
            TLOG("retune: call %u to carrier %u = %s slots 0x%x: declined",
                 r->call_id, r->carrier, mhz(r->freq_hz), r->timeslots);
        return false;
    }

    tune();
#ifdef CONFIG_TETRA_LOG
    struct tetraFollowTarget t = tetraFollow_target(&follower);
    if (fr.main)
        TLOG("retune: main carrier >> %s", mhz(t.freq));
    else
        TLOG("retune: call %u to carrier %u slots 0x%x >> %s%s", r->call_id,
             r->carrier, t.slots, mhz(t.freq), t.away ? "" : " (main)");
#endif
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
    tetraFilter_init(&filter);
    filterSelection = tetraIds_selection();
    tetraFilter_select(&filter, tetraIds_selectedFilter());
    tetraFollow_init(&follower, homeFreq);
    resync(false);
    tetraStatus_reset();
    tetraStatus_setTuned(homeFreq);
#ifdef CONFIG_TETRA_ROAM
    roamInit(homeFreq);
#endif
#ifdef CONFIG_TETRA_LOG
    tetraLogEvents_reset();
    if (homeFreq != 0)
        TLOG("start on %s, filter %s", mhz(homeFreq),
             tetraIds_filterName(tetraIds_selectedFilter()));
    lastSignalLog = getTick();
#endif
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
    cfg.cb = onEvent;
#ifdef CONFIG_TETRA_FOLLOW
    cfg.retune = onRetune;
#endif
    /* Every call: the talkgroup filter is applied to the events */
    cfg.promiscuous = true;
    tetra_tmo_init(&tmo, &cfg);
    slots = TETRA_FOLLOW_ALL_SLOTS;

    /* A new receiver knows nothing of the followed call: go home */
    tetraFollow_home(&follower);
    tetraStatus_lostSync(overrun);
    if (overrun)
        TLOG("samples lost: resync");
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

    /*
     * A new talkgroup filter chosen on the keypad: stop showing, and
     * following, a call it excludes
     */
    if (tetraIds_selection() != filterSelection) {
        struct tetraStatus ts;

        filterSelection = tetraIds_selection();
        tetraFilter_select(&filter, tetraIds_selectedFilter());
        if (tetraIds_held() != 0)
            TLOG("filter: hold %lu", (unsigned long)tetraIds_held());
        else
            TLOG("filter: %s", tetraIds_filterName(tetraIds_selectedFilter()));
        tetra_getStatus(&ts);
        if (ts.call && !tetraFilter_passes(&filter, ts.callId))
            tetraStatus_endCall();
#ifdef CONFIG_TETRA_FOLLOW
        if (follower.selected && !tetraFilter_passes(&filter, follower.callId))
            tetraFollow_drop(&follower);
#endif
    }

#ifdef CONFIG_TETRA_FOLLOW
    /*
     * The follower left the call's carrier on its own (silence, encryption):
     * libtetra still thinks it is there, so restart its synchronisation on
     * the main carrier. tune() selects all timeslots again.
     */
    if (tetraFollow_tick(&follower, getTick())) {
        tetra_tmo_resync(&tmo);
        TLOG("follow: back to the main carrier >> %s",
             mhz(tetraFollow_target(&follower).freq));
    }
#endif
    /* Also undoes a configuration update that tuned back home meanwhile */
    tune();

#ifdef CONFIG_TETRA_ROAM
    roamStep();
#endif

#ifdef CONFIG_TETRA_LOG
    if (getTick() - lastSignalLog >= LOG_SIGNAL_MS) {
        lastSignalLog = getTick();
        TLOG("signal %d dBm on %s, %s, phase error %u%%, offset %+d Hz",
             radio_getRssi(), mhz(rtxCfg->rxFrequency),
             tetraStatus_sync() ? "locked" : "no lock",
             tetraDemod_phaseError(&demod), tetraDemod_carrierOffset(&demod));
    }
#endif

    tetraStatus_setDemod(tetraDemod_phaseError(&demod),
                         tetraDemod_carrierOffset(&demod));

    if (tetraStatus_sync())
        platform_ledOn(GREEN);
    else
        platform_ledOff(GREEN);
}
