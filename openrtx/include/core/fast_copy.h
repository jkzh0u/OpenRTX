/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FAST_COPY_H
#define FAST_COPY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fast copy (CONFIG_FAST_COPY): read the frequency, and the CTCSS tone or
 * DCS code, of another radio transmitting next to this one, to copy them
 * into the VFO, as the Quansheng UV-K5's frequency scan does. The UI starts
 * and stops it; the FM mode runs it on the rtx thread with the radio's
 * frequency and tone scanners (radio.h, radio_fastCopy...()).
 */

/** No stable frequency within this time: given up, ms */
#define FAST_COPY_FREQ_TIMEOUT_MS 30000
/** Time to look for a tone once the frequency is known, ms */
#define FAST_COPY_TONE_MS 3000
/** Frequency readings within this of the last one count as the same, Hz */
#define FAST_COPY_SAME_HZ 1000
/** Readings in a row that agree before the frequency is taken */
#define FAST_COPY_FREQ_HITS 3

enum fastCopyState {
    FAST_COPY_OFF,   /**< Not running */
    FAST_COPY_FREQ,  /**< Measuring the frequency */
    FAST_COPY_TONE,  /**< Frequency known, looking for a tone */
    FAST_COPY_FOUND, /**< Done: hz, and the tone if one was found */
};

struct fastCopyResult {
    enum fastCopyState state;
    uint32_t hz;    /**< Frequency measured, Hz */
    bool toneFound; /**< A tone was found */
    uint16_t tone;  /**< In the rtx encoding: CTCSS in tenths of Hz, or a
                          DCS code with RTX_TONE_DCS (core/cps.h) */
    bool timedOut;  /**< FAST_COPY_FREQ: nothing found for a long time */
};

/**
 * A measured frequency on the nearest channel: of the 2.5 kHz and 6.25 kHz
 * grids, the one it is closer to (446.00625 is on the 6.25 kHz grid,
 * 146.520 on both).
 *
 * @param hz: frequency measured, Hz.
 * @return the channel frequency, Hz.
 */
static inline uint32_t fastCopy_channel(uint32_t hz)
{
    uint32_t a = ((hz + 1250) / 2500) * 2500;
    uint32_t b = ((hz + 3125) / 6250) * 6250;
    uint32_t ea = (a > hz) ? a - hz : hz - a;
    uint32_t eb = (b > hz) ? b - hz : hz - b;
    return (eb < ea) ? b : a;
}

/** Start reading a nearby radio (UI). */
void fastCopy_start(void);

/** Stop, and give the radio back to normal reception (UI). */
void fastCopy_stop(void);

/** @return true between fastCopy_start() and fastCopy_stop(). */
bool fastCopy_requested(void);

/** Copy the progress and result (UI). */
void fastCopy_get(struct fastCopyResult *out);

/**
 * For the FM mode, on each update: runs the measurement while one is
 * asked for.
 *
 * @param now: current tick, ms.
 * @return true while fast copy has the radio: the mode does nothing else,
 * and receives again normally once it returns false.
 */
bool fastCopy_run(long long now);

#ifdef __cplusplus
}
#endif

#endif /* FAST_COPY_H */
