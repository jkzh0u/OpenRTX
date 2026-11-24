/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hwconfig.h"
#include "core/weather.h"
#include "interfaces/audio.h"
#include "interfaces/radio.h"

#ifdef CONFIG_WEATHER

static volatile int8_t alertCh = -1;
static volatile bool dismissed;

/* The rtx thread's state */
static bool peeking;
static uint8_t peekCh;
static long long lastPeek;
static long long peekStart;
static uint8_t best; /* Highest tone share in this look, percent */
static uint8_t hits;
static long long quietUntil;

static void endPeek(void)
{
    radio_peekStop();
    audio_setToneWatch(0);
    peeking = false;
}

bool wx_update(uint8_t watch, bool idle, long long now)
{
    if (dismissed) {
        dismissed = false;
        alertCh = -1;
        hits = 0;
        quietUntil = now + WX_ALERT_QUIET_MS;
    }

    if (peeking) {
        if (!idle || watch == 0) {
            /* Something to hear or say: the look can wait */
            endPeek();
            lastPeek = now;
            return false;
        }
        long long t = now - peekStart;
        if (t >= WX_PEEK_SETTLE_MS) {
            uint8_t r = audio_getToneRatio();
            if (r > best)
                best = r;
        }
        if (t < WX_PEEK_MS)
            return true;

        endPeek();
        lastPeek = now;
        hits = (best >= WX_TONE_RATIO) ? hits + 1 : 0;
        if (hits >= WX_ALERT_HITS && now >= quietUntil && alertCh < 0)
            alertCh = (int8_t)peekCh;
        return false;
    }

    if (watch == 0 || !idle || now - lastPeek < WX_PEEK_PERIOD_MS)
        return false;

    peekCh = (uint8_t)((watch - 1) % WX_CHANNELS);
    best = 0;
    peekStart = now;
    peeking = true;
    audio_setToneWatch(WX_ALERT_TONE_HZ);
    radio_peekStart(wx_hz(peekCh));
    return true;
}

int wx_alert(void)
{
    return alertCh;
}

void wx_alertDismiss(void)
{
    dismissed = true;
}

#endif /* CONFIG_WEATHER */
