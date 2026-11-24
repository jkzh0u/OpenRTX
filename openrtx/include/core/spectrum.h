/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Spectrum view (CONFIG_SPECTRUM): the signal strength across a band, as
 * the Quansheng UV-K5's spectrum mod draws it. The UI starts it, moves it
 * and stops it; the FM mode sweeps on the rtx thread, retuning the radio to
 * each point and reading its RSSI (radio.h, radio_spectrum...()). Nothing
 * is heard while it runs.
 */

/** Points across the screen */
#define SPECTRUM_BINS 80
/** Points measured on each update of the FM mode */
#define SPECTRUM_BINS_PER_RUN 40
/** Reading of a point not measured (outside the radio's range), dBm */
#define SPECTRUM_NONE (-160)

/**
 * Frequency of a point.
 *
 * @param center: frequency at the middle of the view, Hz.
 * @param step: distance between points, Hz.
 * @param bin: point, 0 to SPECTRUM_BINS - 1.
 * @return the frequency, Hz.
 */
static inline uint32_t spectrum_binHz(uint32_t center, uint32_t step,
                                      unsigned bin)
{
    return center + (uint32_t)((int32_t)bin - SPECTRUM_BINS / 2) * step;
}

/** A complete sweep */
struct spectrumSweep {
    uint32_t center;             /**< Of the view swept, Hz */
    uint32_t step;               /**< Of the view swept, Hz */
    uint32_t count;              /**< Sweeps done since the start */
    int16_t rssi[SPECTRUM_BINS]; /**< dBm, or SPECTRUM_NONE */
};

/**
 * Start sweeping (UI).
 *
 * @param center: frequency at the middle, Hz.
 * @param step: distance between points, Hz.
 */
void spectrum_start(uint32_t center, uint32_t step);

/** Move or zoom the view: the next sweep uses it (UI). */
void spectrum_setView(uint32_t center, uint32_t step);

/** Stop, and give the radio back to normal reception (UI). */
void spectrum_stop(void);

/** @return true between spectrum_start() and spectrum_stop(). */
bool spectrum_requested(void);

/** Copy the last complete sweep (UI). count is 0 before the first. */
void spectrum_get(struct spectrumSweep *out);

/**
 * For the FM mode, on each update: sweeps a part of the band while the
 * view is open.
 *
 * @return true while the spectrum view has the radio: the mode does
 * nothing else, and receives again normally once it returns false.
 */
bool spectrum_run(void);

#ifdef __cplusplus
}
#endif

#endif /* SPECTRUM_H */
