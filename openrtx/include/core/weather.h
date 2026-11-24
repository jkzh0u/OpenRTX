/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOAA weather radio (CONFIG_WEATHER): the seven weather channels, and the
 * weather alert watch. Before a warning, NOAA stations send a 1050 Hz tone
 * for 8 to 10 s. With the watch on, the FM mode listens on the chosen
 * weather channel for WX_PEEK_MS every WX_PEEK_PERIOD_MS while the radio is
 * idle, the audio driver measuring that tone (audio_setToneWatch()), and
 * raises an alert when it hears it twice in a row.
 */

#define WX_CHANNELS 7
/** The alarm tone, Hz */
#define WX_ALERT_TONE_HZ 1050
/** A look at the weather channel every this often, ms */
#define WX_PEEK_PERIOD_MS 5000
/** How long each look lasts, ms */
#define WX_PEEK_MS 300
/** Before the tone is measured, in each look, ms */
#define WX_PEEK_SETTLE_MS 100
/** Share of the audio in the tone, percent, for it to count */
#define WX_TONE_RATIO 50
/** Looks in a row with the tone before an alert */
#define WX_ALERT_HITS 2
/** No new alert this long after one is dismissed, ms */
#define WX_ALERT_QUIET_MS 120000

/**
 * @param ch: weather channel, 0 for WX1 to 6 for WX7.
 * @return its frequency, Hz.
 */
static inline uint32_t wx_hz(uint8_t ch)
{
    static const uint32_t hz[WX_CHANNELS] = {
        162550000, 162400000, 162475000, 162425000,
        162450000, 162500000, 162525000,
    };
    return hz[ch % WX_CHANNELS];
}

/**
 * The FM mode, on each update.
 *
 * @param watch: the channel watched, 1 for WX1 to 7 for WX7, 0 for none.
 * @param idle: nothing heard or transmitted, the radio not used for
 * anything else: a look may start. Ends a look under way when false.
 * @param now: current tick, ms.
 * @return true during a look: the FM mode leaves the radio alone.
 */
bool wx_update(uint8_t watch, bool idle, long long now);

/**
 * @return the weather channel an alert was heard on, 0 for WX1 to 6 for
 * WX7, or -1 for none (UI).
 */
int wx_alert(void);

/** The alert was seen: no new one for WX_ALERT_QUIET_MS (UI). */
void wx_alertDismiss(void);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_H */
