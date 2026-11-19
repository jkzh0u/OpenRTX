/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
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
#include "protocols/TETRA/TetraConfig.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraLog.h"
#include "protocols/TETRA/TetraLogEvents.h"
#include "protocols/TETRA/TetraRoam.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"
#include "rtx/OpMode_TETRA.hpp"
#ifdef CONFIG_TETRA_AFC
#include "core/state.h"
#endif

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

/* Samples dropped after starting or retuning: 0.5 s */
#define SETTLE_SAMPLES (TETRA_DEMOD_IN_RATE / 2)

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
static uint8_t slots;       // Timeslots libtetra decodes
static rtxStatus_t *rtxCfg; // Receiver configuration, set by update()

#ifdef CONFIG_TETRA_LOG
static long long lastSignalLog; // Last signal line of the log

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

/**
 * The strongest carriers of the last measurement.
 *
 * @param out: carriers, strongest first.
 * @param max: room in out.
 * @return number of carriers in out; *total: number measured.
 */
static unsigned roamStrongest(struct tetraStatusMeasured *out, unsigned max,
                              unsigned *total)
{
    bool taken[TETRA_ROAM_MAX_CANDIDATES] = { false };
    unsigned measured = 0, n = 0;

    for (unsigned i = 0; i < roam.nCand; i++) {
        if (roam.cand[i].rssi > -32768)
            measured++;
    }

    while (n < max && n < measured) {
        int best = -1;
        for (unsigned i = 0; i < roam.nCand; i++) {
            if (!taken[i] && roam.cand[i].rssi > -32768
                && (best < 0 || roam.cand[i].rssi > roam.cand[best].rssi))
                best = (int)i;
        }
        taken[best] = true;
        out[n].freq = roam.cand[best].freq;
        out[n].rssi = roam.cand[best].rssi;
        n++;
    }

    *total = measured;
    return n;
}

#ifdef CONFIG_TETRA_LOG
/** Shown in the log of a measurement */
#define LOG_MEASURED 4

/** The strongest carriers of the last measurement */
static void roamLogMeasured(const struct tetraStatusMeasured *m, unsigned n,
                            unsigned measured)
{
    char line[120];
    size_t len = 0;
    unsigned shown = 0;

    line[0] = '\0';
    for (; shown < n && shown < LOG_MEASURED; shown++) {
        int w = snprintf(line + len, sizeof(line) - len, "%s%s %d",
                         len ? ", " : "", mhz(m[shown].freq), m[shown].rssi);
        if (w < 0 || (size_t)w >= sizeof(line) - len)
            break;
        len += (size_t)w;
    }

    TLOG("roam: measured %u: %s%s", measured, line,
         measured > shown ? ", ..." : "");
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
    /* This step decides on the measurement: show it, all of it */
    if (measuredAll) {
        struct tetraStatusMeasured m[TETRA_STATUS_MEASURED];
        unsigned measured;
        unsigned n = roamStrongest(m, TETRA_STATUS_MEASURED, &measured);
        tetraStatus_setMeasured(m, n);
#ifdef CONFIG_TETRA_LOG
        roamLogMeasured(m, n, measured);
#endif
    }
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
    tetraStatus_setRoamState((uint8_t)roam.state);
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
            fc.freq = 0;
            fc.mainFreq = 0;
            {
                /* The carrier's frequency, for a late join */
                const struct tetra_cell *cell = tetra_tmo_cell(&tmo);
                if (cell != NULL && cell->have_sysinfo && cell->band != 0
                    && c->carrier != 0xffff) {
                    fc.freq = tetra_carrier_hz(cell->band, c->carrier,
                                               cell->offset);
                    fc.mainFreq = cell->dl_freq_hz;
                }
            }
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
/** Late join of a call: the talkgroup filter must let it through */
static bool joinAllowed(uint16_t callId)
{
    return tetraFilter_passes(&filter, callId);
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

    /* Calls the talkgroup filter holds back are not followed */
    if (r->reason == TETRA_RETUNE_CALL
        && !tetraFilter_passes(&filter, r->call_id)) {
        TLOG("retune: call %u to carrier %u: filtered out", r->call_id,
             r->carrier);
        return false;
    }

    /* Following switched off in the settings */
    if (r->reason == TETRA_RETUNE_CALL && !tetraConfig_follow()) {
        TLOG("retune: call %u to carrier %u: following off", r->call_id,
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

/* Diagnostics and remote control (see TetraStatus.h) */
static uint32_t crcBase; /* CRC passes of earlier tmo contexts */
static uint32_t lastCrc; /* CRC passes the demodulator was told of */
#ifdef CONFIG_TETRA_AFC
/*
 * Automatic frequency correction: every AFC_PERIOD_MS, the mean carrier
 * offset measured by the demodulator while blocks decode moves the
 * receiver's fine tuning towards a zero reading. That reading is about
 * 0.15 of the real offset (the receive path removes most of the carrier's
 * DC), but it is monotonic and zero where the phase error is lowest; the
 * loop gain allows for it. The offset found is kept across retunes: it is
 * mostly the radio's own (crystal, filters).
 */
#define AFC_PERIOD_MS 2000
#define AFC_GAIN 3       /* Hz of correction per Hz of mean reading */
#define AFC_STEP_MAX 300 /* Hz per period */
#define AFC_DEADBAND 25  /* Hz of mean reading */
#define AFC_RANGE 4000   /* Hz, either way: the C62 decodes about 4 kHz off */
#define AFC_CRC_RECENT_MS 1000
/*
 * Readings are trusted below this phase error (% of pi/4): near the edges
 * of the band where the C62 decodes (above 30%), the offset reading can
 * have the wrong sign
 */
#define AFC_ERROR_MAX 35
/*
 * Nothing decoded this long: the correction may be wrong for this signal
 * (another cell, a jump). Go back to the stored one, or from the stored one
 * to none, and start again.
 */
#define AFC_LOST_MS 8000
/*
 * Only with a signal this strong (dBm): a wrong correction leaves most of a
 * strong signal in the receive filter, but a weak signal that does not
 * decode is just weak, and moving the tuning only makes it worse
 */
#define AFC_LOST_RSSI -90
/*
 * The correction found is stored in the settings, so that a radio turned on
 * with a signal too weak for the loop to steer (above AFC_ERROR_MAX) starts
 * corrected; only moves of this much are stored, to spare the flash.
 */
#define AFC_STORE_STEP 200
static int32_t afcOffset;
static float afcSum;
static uint32_t afcCount;
static long long afcStart;
static long long lastCrcTick;
static int32_t afcApplied;
static bool afcLoaded; /* Stored correction read, once per power on */

static int32_t afcStored(void)
{
    return state.settings.tetraAfcOffset;
}

/**
 * Store the correction in the settings when it has moved far enough; they
 * are saved to flash a few seconds later (CONFIG_NVM_SAVE_ON_CHANGE).
 */
static void afcStore(void)
{
    int32_t delta = afcOffset - afcStored();
    if (delta < AFC_STORE_STEP && delta > -AFC_STORE_STEP)
        return;
    pthread_mutex_lock(&state_mutex);
    state.settings.tetraAfcOffset = (int16_t)afcOffset;
    pthread_mutex_unlock(&state_mutex);
}

static bool afcEnabled(void)
{
    return state.settings.tetraAfcOff == 0;
}

/**
 * Apply an offset to the receiver, if it is not applied already.
 */
static void afcApply(int32_t offset)
{
    if (offset != afcApplied) {
        radio_setRxOffset(offset);
        afcApplied = offset;
    }
}

/**
 * Called after each block of samples.
 */
static void afcUpdate(void)
{
    if (!afcEnabled()) {
        afcApply(0);
        afcSum = 0;
        afcCount = 0;
        return;
    }
    afcApply(afcOffset); /* Turned on again */

    /* Measure only while blocks decode: never steer on noise */
    long long now = getTick();
    if (tetra_tmo_locked(&tmo) && (now - lastCrcTick) < AFC_CRC_RECENT_MS
        && tetraDemod_phaseError(&demod) < AFC_ERROR_MAX) {
        afcSum += tetraDemod_carrierOffset(&demod);
        afcCount++;
    }

    /*
     * Lost for long, maybe because the correction is wrong for this signal
     * (another cell, a jump): from no correction it can be found again
     */
    if ((now - lastCrcTick) > AFC_LOST_MS && rtx_getRssi() > AFC_LOST_RSSI) {
        int32_t stored = afcStored();
        int32_t next = (afcOffset != stored) ? stored : 0;
        if (next == afcOffset)
            return;
        afcOffset = next;
        afcApply(afcOffset);
        lastCrcTick = now; /* Give it the time to decode */
        afcSum = 0;
        afcCount = 0;
        afcStart = now;
        return;
    }
    if ((now - afcStart) < AFC_PERIOD_MS)
        return;
    afcStart = now;

    uint32_t count = afcCount;
    float mean = (count > 0) ? afcSum / count : 0;
    afcSum = 0;
    afcCount = 0;
    if (count < 10 || fabsf(mean) <= AFC_DEADBAND)
        return;

    float step = AFC_GAIN * mean;
    if (step > AFC_STEP_MAX)
        step = AFC_STEP_MAX;
    else if (step < -AFC_STEP_MAX)
        step = -AFC_STEP_MAX;
    int32_t next = afcOffset + (int32_t)lrintf(step);
    if (next > AFC_RANGE)
        next = AFC_RANGE;
    else if (next < -AFC_RANGE)
        next = -AFC_RANGE;
    afcOffset = (next / 10) * 10; /* The BK4819 tunes in 10 Hz steps */
    afcApply(afcOffset);
    afcStore();
}
#endif
static volatile bool resetRequest;
static volatile bool eqSeed = true; /* Start from CONFIG_TETRA_EQ_INIT */

OpMode_TETRA::OpMode_TETRA()
    : startRx(false)
    , dataErrors(0)
    , settle(SETTLE_SAMPLES)
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
    settle = SETTLE_SAMPLES;
    tetraDemod_init(&demod);
#ifdef CONFIG_TETRA_EQ_INIT
    static const float eqInit[TETRA_DEMOD_EQ_TAPS] = CONFIG_TETRA_EQ_INIT;
    if (eqSeed)
        tetraDemod_setEqualizer(&demod, eqInit);
#endif

    demodJumps = 0;
    tetraFilter_init(&filter);
    filterSelection = tetraIds_selection();
    tetraFilter_select(&filter, tetraIds_selectedFilter());
    tetraFollow_init(&follower, homeFreq);
    resync(false);
    crcBase = 0;
    lastCrc = 0;
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
    crcBase += tetra_tmo_crc_ok(&tmo);

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
#ifdef CONFIG_TETRA_AFC
    /* The stored correction, once per power on */
    if (!afcLoaded) {
        int32_t stored = afcStored();
        if (stored > AFC_RANGE || stored < -AFC_RANGE)
            stored = 0;
        afcOffset = (stored / 10) * 10;
        afcLoaded = true;
    }
    /* Before receiving starts: no retune once it has settled */
    afcApply(afcEnabled() ? afcOffset : 0);
    afcSum = 0;
    afcCount = 0;
    afcStart = getTick();
    lastCrcTick = afcStart; /* Not "lost" before trying */
#endif
    reset();
    tetraSpeech_start();
    startRx = true;
}

void OpMode_TETRA::disable()
{
    startRx = false;
    stopSampling();
    tetraSpeech_stop();
#ifdef CONFIG_TETRA_AFC
    afcApply(0); /* The other modes tune where they are told */
#endif
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
     * back to the main carrier: tune() below retunes it. The remote control
     * can also ask for a restart.
     */
    if ((newCfg || homeFreq == 0) && rtx->rxFrequency != homeFreq) {
        homeFreq = rtx->rxFrequency;
        resetRequest = false;
        reset();
    } else if (resetRequest) {
        resetRequest = false;
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

    /*
     * Drop what follows a start or a retune: the receiver and the audio
     * path settle, and the demodulator would take their transient for the
     * carrier offset and keep it.
     */
    if (settle > 0) {
        settle -= (block.len < settle) ? block.len : settle;
        return;
    }

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

    /* Blocks decode: the demodulator keeps its lock */
    uint32_t crc = crcBase + tetra_tmo_crc_ok(&tmo);
    if (crc != lastCrc) {
        lastCrc = crc;
        tetraDemod_signalOk(&demod);
#ifdef CONFIG_TETRA_AFC
        lastCrcTick = getTick();
#endif
    }
#ifdef CONFIG_TETRA_AFC
    afcUpdate();
#endif

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
    /* Following switched off while on a call's carrier: back home */
    if (!tetraConfig_follow() && follower.selected)
        tetraFollow_drop(&follower);

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

    /*
     * Back on the main carrier with nothing to play: join a call still
     * going on on another carrier, whose allocation was seen while another
     * call was followed. libtetra did not tune there itself, so restart its
     * synchronisation: it finds the call by its timeslots. Not during a
     * cell search, which owns the tuning.
     */
    bool searching = false;
#ifdef CONFIG_TETRA_ROAM
    searching = tetraRoam_searching(&roam);
#endif
    if (tetraConfig_follow() && tetraConfig_join() && !searching
        && tetra_tmo_locked(&tmo)
        && tetraFollow_join(&follower, joinAllowed, getTick())) {
        struct tetraFollowTarget t = tetraFollow_target(&follower);
        tetra_tmo_resync(&tmo);
        TLOG("follow: join call %u on carrier %u >> %s slots 0x%x",
             follower.callId, follower.carrier, mhz(t.freq), t.slots);
    }
#endif
    /* Also undoes a configuration update that tuned back home meanwhile */
    tune();

#ifdef CONFIG_TETRA_ROAM
    /*
     * With cell tracking off, only a search asked for on the keypad runs,
     * until it ends
     */
    if (tetraConfig_roam() || tetraRoam_searching(&roam)
        || tetraStatus_searchRequests() != searchRequests)
        roamStep();
#endif

#ifdef CONFIG_TETRA_LOG
    if (getTick() - lastSignalLog >= LOG_SIGNAL_MS) {
        lastSignalLog = getTick();
        struct tetraDiag d;
        tetra_getDiag(&d);
        int correction = 0;
#ifdef CONFIG_TETRA_AFC
        correction = afcApplied;
#endif
        TLOG("signal %d dBm on %s, %s, phase error %u%%, offset %+d Hz, "
             "correction %+d Hz, crc ok %lu, reacquired %lu, realigned %lu",
             radio_getRssi(), mhz(rtxCfg->rxFrequency),
             tetraStatus_sync() ? "locked" : "no lock", d.phaseErr,
             d.carrierHz, correction, (unsigned long)d.crcOk,
             (unsigned long)d.reacquired, (unsigned long)d.realigned);
    }
#endif

    tetraStatus_setDemod(tetraDemod_phaseError(&demod),
                         tetraDemod_carrierOffset(&demod));

    if (tetraStatus_sync())
        platform_ledOn(GREEN);
    else
        platform_ledOff(GREEN);
}

void tetra_getDiag(struct tetraDiag *out)
{
    out->crcOk = crcBase + tetra_tmo_crc_ok(&tmo);
    out->realigned = tetra_tmo_realigned(&tmo, NULL);
    out->locked = tetra_tmo_locked(&tmo);
    out->reacquired = demod.reacquired;
    out->slips = demod.slips;
    out->symbols = demod.symbols;
    out->phaseErr = tetraDemod_phaseError(&demod);
    out->carrierHz = tetraDemod_carrierOffset(&demod);
    out->clockPpm = tetraDemod_clockOffset(&demod);
    out->eqSeed = eqSeed;
    memcpy(out->eq, demod.eq, sizeof(out->eq));
}

void tetra_requestReset(bool seedEqualizer)
{
    eqSeed = seedEqualizer;
    resetRequest = true;
}
