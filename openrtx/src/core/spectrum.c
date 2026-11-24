/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "hwconfig.h"
#include "core/spectrum.h"
#include "interfaces/radio.h"

#ifdef CONFIG_SPECTRUM

/* Lowest and highest frequency the radio tunes to receive, Hz */
#define RX_MIN_HZ 136000000u
#define RX_MAX_HZ 600000000u

/* Asked by the UI */
static volatile bool requested;
static volatile uint32_t viewCenter;
static volatile uint32_t viewStep;

/* The rtx thread's state */
static bool running;
static unsigned pos;              /* Next point of the sweep */
static struct spectrumSweep work; /* Sweep being measured */
static struct spectrumSweep done; /* Last complete sweep */

void spectrum_start(uint32_t center, uint32_t step)
{
    spectrum_setView(center, step);
    requested = true;
}

void spectrum_setView(uint32_t center, uint32_t step)
{
    viewCenter = center;
    viewStep = step;
}

void spectrum_stop(void)
{
    requested = false;
}

bool spectrum_requested(void)
{
    return requested;
}

void spectrum_get(struct spectrumSweep *out)
{
    *out = done;
}

bool spectrum_run(void)
{
    if (!requested) {
        if (running) {
            radio_spectrumStop();
            running = false;
        }
        return false;
    }

    if (!running) {
        running = true;
        pos = 0;
        memset(&done, 0, sizeof(done));
        radio_spectrumStart();
    }

    for (unsigned n = 0; n < SPECTRUM_BINS_PER_RUN; n++) {
        /* Moved or zoomed: start again at once */
        if (viewCenter != work.center || viewStep != work.step)
            pos = 0;
        if (pos == 0) {
            /* A new sweep, of the view asked for now */
            work.center = viewCenter;
            work.step = viewStep;
        }

        uint32_t hz = spectrum_binHz(work.center, work.step, pos);
        if (hz >= RX_MIN_HZ && hz <= RX_MAX_HZ)
            work.rssi[pos] = radio_spectrumRssi(hz);
        else
            work.rssi[pos] = SPECTRUM_NONE;

        if (++pos == SPECTRUM_BINS) {
            pos = 0;
            work.count = done.count + 1;
            done = work;
        }
    }

    return true;
}

#endif /* CONFIG_SPECTRUM */
