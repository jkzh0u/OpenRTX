/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "hwconfig.h"
#include "core/cps.h"
#include "core/fast_copy.h"
#include "interfaces/radio.h"

#ifdef CONFIG_FAST_COPY

/* Asked by the UI: a new session number each start */
static volatile bool requested;
static volatile uint8_t session;

/* The rtx thread's state */
static uint8_t running; /* Session being run, 0 for none */
static long long since; /* Start of the current stage */
static uint32_t lastHz;
static uint8_t hits;
static uint16_t lastTone;
static uint8_t toneHits;
static struct fastCopyResult result;

void fastCopy_start(void)
{
    uint8_t s = (uint8_t)(session + 1);
    session = (s == 0) ? 1 : s;
    requested = true;
}

void fastCopy_stop(void)
{
    requested = false;
}

bool fastCopy_requested(void)
{
    return requested;
}

void fastCopy_get(struct fastCopyResult *out)
{
    *out = result;
    if (!requested)
        out->state = FAST_COPY_OFF;
}

static uint32_t absDiff(uint32_t a, uint32_t b)
{
    return (a > b) ? a - b : b - a;
}

bool fastCopy_run(long long now)
{
    if (!requested) {
        if (running != 0) {
            radio_fastCopyStop();
            running = 0;
            result.state = FAST_COPY_OFF;
        }
        return false;
    }

    if (running != session) {
        running = session;
        memset(&result, 0, sizeof(result));
        result.state = FAST_COPY_FREQ;
        hits = 0;
        lastHz = 0;
        since = now;
        radio_fastCopyStart();
        return true;
    }

    switch (result.state) {
        case FAST_COPY_FREQ: {
            uint32_t hz;
            if (!radio_fastCopyFreq(&hz)) {
                result.timedOut = (now - since) > FAST_COPY_FREQ_TIMEOUT_MS;
                break;
            }
            /* The same frequency several times in a row: not noise */
            if (hz != 0 && absDiff(hz, lastHz) < FAST_COPY_SAME_HZ)
                hits++;
            else
                hits = 0;
            lastHz = hz;
            if (hits + 1 < FAST_COPY_FREQ_HITS) {
                radio_fastCopyStart();
                break;
            }
            result.hz = hz;
            result.state = FAST_COPY_TONE;
            since = now;
            toneHits = 0;
            lastTone = 0;
            radio_fastCopyTone(hz);
            break;
        }

        case FAST_COPY_TONE: {
            uint16_t tone;
            if (radio_fastCopyToneResult(&tone)) {
                /* A DCS codeword passed its check; a CTCSS tone must read
                 * the same twice */
                bool dcs = (tone & RTX_TONE_DCS) != 0;
                uint16_t d = (tone > lastTone) ? tone - lastTone :
                                                 lastTone - tone;
                if (!dcs && lastTone != 0 && d <= 20)
                    toneHits++;
                lastTone = tone;
                if (dcs || toneHits >= 1) {
                    result.toneFound = true;
                    result.tone = tone;
                    result.state = FAST_COPY_FOUND;
                    radio_fastCopyStop();
                    break;
                }
                radio_fastCopyTone(result.hz);
            }
            if (now - since > FAST_COPY_TONE_MS) {
                /* No tone: a plain carrier */
                result.state = FAST_COPY_FOUND;
                radio_fastCopyStop();
            }
            break;
        }

        default:
            break;
    }

    return true;
}

#endif /* CONFIG_FAST_COPY */
