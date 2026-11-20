/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * TETRA replay and call following trace.
 *
 * Runs a 48 kHz recording of the radio's discriminator output (a WAV from
 * scripts/c62_bbdump.py) through the same chain as the radio's TETRA mode:
 * TetraDemod, libtetra and TetraFollow. Prints the cell, the call control
 * events with their channel allocations, libtetra's retune requests with
 * the follower's answers, and where the radio would tune.
 * A recording holds one carrier, so the trace shows the decisions the radio
 * would take, not what it would hear after a retune.
 *
 * Build, with libtetra from the west workspace (modules/lib/libtetra):
 *
 *     LIBTETRA=../modules/lib/libtetra
 *     cc -std=gnu17 -O2 -o tetra_trace -Iopenrtx/include \
 *        -I$LIBTETRA/include -I$LIBTETRA/src tests/platform/tetra_trace.c \
 *        openrtx/src/protocols/TETRA/TetraDemod.c \
 *        openrtx/src/protocols/TETRA/TetraFollow.c \
 *        openrtx/src/protocols/TETRA/TetraIds.c \
 *        openrtx/src/protocols/TETRA/TetraLogEvents.c \
 *        openrtx/src/core/chlist.c -DCONFIG_CHLIST \
 *        $(find $LIBTETRA/src -name '*.c' ! -path '*crypto*') -lm
 *
 * Run:
 *
 *     ./tetra_trace [-f MHz] [-n] [-v] [-L] [-c channels.bin]
 *                   [-p SSI[:N]]... recording.wav
 *
 *     -f  frequency the radio was tuned to, MHz (default: the main carrier
 *         frequency the cell broadcasts)
 *     -n  start the equalizer from scratch instead of the C62 taps: for
 *         recordings not made on a C62 (an SDR recording through
 *         scripts/c62_airspy_iq.py and scripts/c62_tetra_sim.py, synthetic
 *         signals), which the C62 taps make undecodable
 *     -v  also print every traffic slot
 *     -L  print what the radio's receive log would instead (TetraLogEvents
 *         and the follower's lines of OpMode_TETRA.cpp)
 *     -c  a channel list image (scripts/chlist.py) for the names and
 *         priorities of talkgroups, as on the radio
 *     -p  without -c, a talkgroup's priority N, 1 to 15 (TF_PRIORITY(N) in a channel
 *         list; 1 if not given); repeat for more
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tetra/tetra.h>
#include "protocols/TETRA/TetraDemod.h"
#include "protocols/TETRA/TetraFollow.h"
#include "protocols/TETRA/TetraIds.h"
#include "protocols/TETRA/TetraLog.h"
#include "protocols/TETRA/TetraLogEvents.h"
#include "core/chlist.h"

/* 10 ms blocks: the time printed is that of the block being decoded */
#define BLOCK 480

/* CONFIG_TETRA_EQ_INIT in platform/targets/c62/hwconfig.h */
static const float c62EqInit[TETRA_DEMOD_EQ_TAPS] = { 0.007f,  -0.004f, 0.036f,
                                                      -0.185f, 1.149f,  -0.187f,
                                                      0.033f,  -0.001f, 0.0f };

static struct tetraDemod demod;
static struct tetra_tmo_rx tmo;
static struct tetraFollow follower;
static struct tetraFollowTarget lastTarget;
static uint32_t tunedHz; /* -f, 0: the cell's main carrier */
static bool verbose;
static double now;       /* Seconds into the recording */
static unsigned trafficSlots;
static unsigned cellMcc, cellMnc, cellLa, cellMain;
static bool radioLog; /* -L: print what the radio's receive log would */
static bool haveList; /* -c: names and priorities from a channel list */

/* The radio's receive log, to stdout: same line format as TetraLog.c */
void tetraLog_printf(long long t, const char *fmt, ...)
{
    va_list ap;

    if (!radioLog)
        return;
    printf("%6lu.%03u  ", (unsigned long)(t / 1000), (unsigned)(t % 1000));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* A frequency as the radio's log writes it */
static const char *mhz(uint32_t hz)
{
    static char buf[2][16];
    static unsigned next;

    next = (next + 1) % 2;
    return tetraLog_freq(buf[next], sizeof(buf[next]), hz);
}

static long long ms(void)
{
    return (long long)(now * 1000);
}

static const char *callWhat(enum tetra_call_what what)
{
    static const char *const names[] = {
        "setup",        "connect",     "tx-granted", "tx-ceased",  "late-entry",
        "info",         "release",     "alert",      "proceeding", "tx-wait",
        "tx-interrupt", "tx-continue", "restore",
    };

    if ((unsigned)what < sizeof(names) / sizeof(names[0]))
        return names[what];
    return "?";
}

static void printFreq(uint32_t hz)
{
    printf("%lu.%05lu MHz", (unsigned long)(hz / 1000000),
           (unsigned long)(hz % 1000000 / 10));
}

/* Frequency of a carrier from the cell's numbering, for printing */
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

static void onCell(const struct tetra_cell *c)
{
    if (!c->have_sysinfo)
        return;

    if (follower.home == 0) {
        follower.home = tunedHz ? tunedHz : c->dl_freq_hz;
        lastTarget = tetraFollow_target(&follower);
    }

    if (c->mcc == cellMcc && c->mnc == cellMnc && c->location_area == cellLa
        && c->main_carrier == cellMain)
        return;
    cellMcc = c->mcc;
    cellMnc = c->mnc;
    cellLa = c->location_area;
    cellMain = c->main_carrier;

    if (!radioLog) {
        printf("%8.3f  cell %u-%u LA %u, main carrier %u = ", now, c->mcc,
               c->mnc, c->location_area, c->main_carrier);
        printFreq(c->dl_freq_hz);
        printf(" (band %u, offset %u, duplex %u%s)%s\n", c->band, c->offset,
               c->duplex_spacing, c->reverse_operation ? ", reverse" : "",
               c->air_encryption ? ", air encryption" : "");
    }

    if (tetraFollow_carrierFreq(follower.home, c->dl_freq_hz, c->dl_freq_hz)
        == 0) {
        if (radioLog)
            return;
        printf("%8.3f  warning: tuned to ", now);
        printFreq(follower.home);
        printf(", not the main carrier: the radio would not follow calls\n");
    }
}

/* Priority talkgroups given with -p */
#define MAX_PRIORITY 16
static uint32_t prioritySsi[MAX_PRIORITY];
static uint8_t priorityOf[MAX_PRIORITY];
static unsigned priorityCount;

static uint8_t ssiPriority(uint32_t ssi)
{
    if (haveList)
        return tetraIds_priority(ssi);

    for (unsigned i = 0; i < priorityCount; i++) {
        if (ssi != 0 && prioritySsi[i] == ssi)
            return priorityOf[i];
    }
    return 0;
}

static uint8_t callPriority(uint32_t dst, uint32_t src, uint8_t net)
{
    uint8_t p = ssiPriority(dst), q = ssiPriority(src);
    if (q > p)
        p = q;
    return net >= 15 ? 15 : p;
}

static void reportTakeover(void)
{
    if (!follower.preempted)
        return;
    follower.preempted = false;
    if (radioLog)
        tetraLog_printf(ms(), "follow: priority %u call %u takes over",
                        follower.selectedPriority, follower.callId);
    else
        printf("%8.3f  priority %u call %u takes over\n", now,
               follower.selectedPriority, follower.callId);
}

static void onCall(const struct tetra_call *c)
{
    struct tetraFollowCall fc;
    uint32_t freq = carrierFreq(c->carrier);

    if (radioLog)
        goto follow;
    printf("%8.3f  call %u %s", now, c->call_id, callWhat(c->what));
    if (c->dst_ssi)
        printf(" to %s%lu", c->group ? "G " : "", (unsigned long)c->dst_ssi);
    if (c->src_ssi)
        printf(" from %lu", (unsigned long)c->src_ssi);
    if (c->carrier != 0xffff) {
        printf(", carrier %u", c->carrier);
        if (freq != 0) {
            printf(" = ");
            printFreq(freq);
        }
        printf(" slots %s%s%s%s%s", c->timeslots ? "" : "MCCH",
               (c->timeslots & 1) ? "1" : "", (c->timeslots & 2) ? "2" : "",
               (c->timeslots & 4) ? "3" : "", (c->timeslots & 8) ? "4" : "");
    }
    if (c->circuit_type != 0)
        printf(", data (type %u)", c->circuit_type);
    if (c->encrypted)
        printf(", encrypted");
    printf("\n");

follow:
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
        const struct tetra_cell *cell = tetra_tmo_cell(&tmo);
        if (cell != NULL && cell->have_sysinfo && cell->band != 0
            && c->carrier != 0xffff) {
            fc.freq = tetra_carrier_hz(cell->band, c->carrier, cell->offset);
            fc.mainFreq = cell->dl_freq_hz;
        }
    }
    fc.priority = callPriority(c->dst_ssi, c->src_ssi, c->priority);
    tetraFollow_call(&follower, &fc, (long long)(now * 1000));
    reportTakeover();
}

static void onEvent(void *user, const struct tetra_event *ev)
{
    (void)user;

    /* As the radio, with every talkgroup let through */
    tetraLogEvents_event(ms(), ev, true);

    switch (ev->type) {
        case TETRA_EV_SYNC:
            if (!radioLog)
                printf("%8.3f  sync %s\n", now,
                       ev->u.sync.locked ? "locked" : "lost");
            break;

        case TETRA_EV_CELL:
            onCell(ev->u.cell);
            break;

        case TETRA_EV_CALL:
            onCall(&ev->u.call);
            break;

        case TETRA_EV_TRAFFIC: {
            const struct tetra_traffic *t = &ev->u.traffic;
            trafficSlots++;
            if (verbose && !radioLog)
                printf("%8.3f  traffic call %u TN%u um %u%s\n", now, t->call_id,
                       ev->time.tn, t->usage_marker,
                       t->encrypted ? " encrypted" : "");
            tetraFollow_traffic(&follower, t->call_id, t->circuit_type == 0,
                                t->encrypted, (long long)(now * 1000));
            break;
        }

        default:
            break;
    }
}

/* Same answer as the radio's TETRA mode (OpMode_TETRA.cpp) */
static bool onRetune(void *user, const struct tetra_retune *r)
{
    const struct tetra_cell *cell = tetra_tmo_cell(&tmo);
    struct tetraFollowRetune fr;
    bool ok;

    (void)user;
    fr.main = r->reason == TETRA_RETUNE_MAIN;
    fr.callId = r->call_id;
    fr.carrier = r->carrier;
    fr.freq = r->freq_hz;
    fr.mainFreq = (cell != NULL && cell->have_sysinfo) ? cell->dl_freq_hz : 0;
    fr.slots = r->timeslots;
    fr.priority = callPriority(r->dst_ssi, 0, 0);
    ok = tetraFollow_retune(&follower, &fr, (long long)(now * 1000));

    reportTakeover();
    if (radioLog) {
        /* As OpMode_TETRA.cpp logs it */
        static uint16_t lastDeclined = 0xffff;
        struct tetraFollowTarget t = tetraFollow_target(&follower);
        if (!ok && fr.main)
            tetraLog_printf(ms(), "retune: main carrier: declined");
        else if (!ok && r->call_id != lastDeclined)
            tetraLog_printf(ms(), "retune: call %u to carrier %u = %s slots "
                            "0x%x: declined", r->call_id, r->carrier,
                            mhz(r->freq_hz), r->timeslots);
        else if (ok && fr.main)
            tetraLog_printf(ms(), "retune: main carrier >> %s", mhz(t.freq));
        else if (ok)
            tetraLog_printf(ms(), "retune: call %u to carrier %u slots 0x%x "
                            ">> %s%s", r->call_id, r->carrier, t.slots,
                            mhz(t.freq), t.away ? "" : " (main)");
        lastDeclined = (!ok && !fr.main) ? r->call_id : 0xffff;
        return ok;
    }

    printf("%8.3f  retune request: ", now);
    if (fr.main)
        printf("main carrier %u", r->carrier);
    else
        printf("call %u to carrier %u slots 0x%x", r->call_id, r->carrier,
               r->timeslots);
    printf(" = ");
    printFreq(r->freq_hz);
    printf(", %s\n", ok ? "accepted" : "declined");
    return ok;
}

static void resync(void)
{
    struct tetra_rx_config cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.cb = onEvent;
    cfg.retune = onRetune;
    cfg.promiscuous = true;
    tetra_tmo_init(&tmo, &cfg);
    tetraFollow_home(&follower);
}

static void reportTarget(void)
{
    struct tetraFollowTarget t;

    if (tetraFollow_tick(&follower, (long long)(now * 1000))) {
        if (radioLog)
            tetraLog_printf(ms(), "follow: back to the main carrier >> %s",
                            mhz(tetraFollow_target(&follower).freq));
        else
            printf("%8.3f  follower left the call's carrier, resync\n", now);
        tetra_tmo_resync(&tmo);
    }
    /* As the radio: join a call going on elsewhere once idle at home */
    if (tetra_tmo_locked(&tmo)
        && tetraFollow_join(&follower, NULL, (long long)(now * 1000))) {
        struct tetraFollowTarget j = tetraFollow_target(&follower);
        if (radioLog)
            tetraLog_printf(ms(), "follow: join call %u on carrier %u >> %s "
                            "slots 0x%x", follower.callId, follower.carrier,
                            mhz(j.freq), j.slots);
        else
            printf("%8.3f  join call %u late, resync\n", now,
                   follower.callId);
        tetra_tmo_resync(&tmo);
    }
    t = tetraFollow_target(&follower);
    if (follower.home == 0 || radioLog
        || (t.freq == lastTarget.freq && t.slots == lastTarget.slots))
        return;
    lastTarget = t;

    printf("%8.3f  >> ", now);
    if (t.away) {
        printf("follow call %u to ", follower.callId);
        printFreq(t.freq);
        printf(", slots 0x%x\n", t.slots);
    } else {
        printf("main carrier ");
        printFreq(t.freq);
        printf("\n");
    }
}

/**
 * Open a WAV file and position it at the start of the samples.
 *
 * @return the file, or NULL if it is not 48 kHz mono 16-bit PCM.
 */
static FILE *openWav(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[12];
    bool fmtOk = false;

    if (f == NULL) {
        perror(path);
        return NULL;
    }
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0
        || memcmp(h + 8, "WAVE", 4) != 0)
        goto bad;

    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8)
            goto bad;
        uint32_t len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;

        if (memcmp(c, "data", 4) == 0) {
            if (!fmtOk)
                goto bad;
            return f;
        }
        if (memcmp(c, "fmt ", 4) == 0 && len >= 16) {
            uint8_t fmt[16];
            if (fread(fmt, 1, 16, f) != 16)
                goto bad;
            unsigned format = fmt[0] | fmt[1] << 8;
            unsigned channels = fmt[2] | fmt[3] << 8;
            uint32_t rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16
                          | (uint32_t)fmt[7] << 24;
            unsigned bits = fmt[14] | fmt[15] << 8;
            fmtOk = format == 1 && channels == 1 && bits == 16
                 && rate == TETRA_DEMOD_IN_RATE;
            len -= 16;
        }
        if (fseek(f, (long)(len + (len & 1)), SEEK_CUR) != 0)
            goto bad;
    }

bad:
    fprintf(stderr, "%s: not a 48 kHz mono 16-bit WAV file\n", path);
    fclose(f);
    return NULL;
}

int main(int argc, char *argv[])
{
    bool eqInit = true;
    int opt;

    while ((opt = getopt(argc, argv, "f:nvp:Lc:")) != -1) {
        switch (opt) {
            case 'L':
                radioLog = true;
                break;
            case 'c': {
                FILE *img = fopen(optarg, "rb");
                static uint32_t image[0x90000 / 4];
                size_t len = img ? fread(image, 1, sizeof(image), img) : 0;
                if (img)
                    fclose(img);
                int ret = chlist_open(image, len);
                if (ret != 0) {
                    fprintf(stderr, "%s: not a usable channel list (%d)\n",
                            optarg, ret);
                    return 1;
                }
                haveList = true;
                break;
            }
            case 'p': {
                char *end;
                unsigned long n = 1;
                uint32_t ssi = strtoul(optarg, &end, 10);
                if (*end == ':')
                    n = strtoul(end + 1, NULL, 10);
                if (priorityCount < MAX_PRIORITY && n >= 1 && n <= 15) {
                    prioritySsi[priorityCount] = ssi;
                    priorityOf[priorityCount++] = (uint8_t)n;
                }
                break;
            }
            case 'f':
                tunedHz = (uint32_t)(atof(optarg) * 1e6 + 0.5);
                break;
            case 'n':
                eqInit = false;
                break;
            case 'v':
                verbose = true;
                break;
            default:
                fprintf(stderr, "usage: %s [-f MHz] [-n] [-v] [-L] [-c channels.bin] "
                        "[-p SSI[:N]]... recording.wav\n",
                        argv[0]);
                return 2;
        }
    }
    if (optind != argc - 1) {
        fprintf(stderr, "usage: %s [-f MHz] [-n] [-v] [-L] [-c channels.bin] "
                        "[-p SSI[:N]]... recording.wav\n",
                argv[0]);
        return 2;
    }

    FILE *f = openWav(argv[optind]);
    if (f == NULL)
        return 1;

    tetraDemod_init(&demod);
    if (eqInit)
        tetraDemod_setEqualizer(&demod, c62EqInit);
    tetraFollow_init(&follower, 0);
    resync();

    static int16_t in[BLOCK];
    static int8_t soft[BLOCK * 3 / 4 + 8];
    uint32_t jumps = 0;
    uint32_t crcBase = 0; /* CRC passes before the last resync */
    uint32_t lastCrc = 0;
    unsigned long samples = 0;
    size_t n;

    while ((n = fread(in, sizeof(in[0]), BLOCK, f)) > 0) {
        now = (double)samples / TETRA_DEMOD_IN_RATE;
        samples += n;

        size_t bits = tetraDemod_process(&demod, in, n, soft);

        /* As the radio on the main carrier: resync after a timing jump */
        if (tetraDemod_reacquisitions(&demod) != jumps) {
            jumps = tetraDemod_reacquisitions(&demod);
            if (!radioLog)
                printf("%8.3f  demodulator jump, resync\n", now);
            crcBase += tetra_tmo_crc_ok(&tmo);
            resync();
        }

        tetra_tmo_push_soft(&tmo, soft, bits);

        /* As the radio: blocks decode, so the demodulator keeps its lock */
        uint32_t crc = crcBase + tetra_tmo_crc_ok(&tmo);
        if (crc != lastCrc) {
            lastCrc = crc;
            tetraDemod_signalOk(&demod);
        }

        reportTarget();
    }
    fclose(f);

    if (radioLog)
        return 0;
    printf("%8.3f  end: %lu blocks passed the CRC, %u traffic slots, phase "
           "error %u%%, carrier offset %d Hz, clock %d ppm\n",
           (double)samples / TETRA_DEMOD_IN_RATE,
           (unsigned long)(crcBase + tetra_tmo_crc_ok(&tmo)), trafficSlots,
           tetraDemod_phaseError(&demod), tetraDemod_carrierOffset(&demod),
           tetraDemod_clockOffset(&demod));
    return 0;
}
