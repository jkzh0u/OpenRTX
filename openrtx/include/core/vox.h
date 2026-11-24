/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef VOX_H
#define VOX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FM VOX (CONFIG_VOX): transmit while you talk, without PTT. The FM mode
 * keeps the microphone captured while VOX is on (audio_setMicMonitor()),
 * keys up when its level (platform_getMicLevel()) reaches the threshold of
 * the VOX level, and unkeys after the hang time without speech.
 */

/** VOX levels: 0 off, 1 (needs a loud voice) to VOX_LEVELS (sensitive) */
#define VOX_LEVELS 10
/** Hang time choices */
#define VOX_DELAYS 5
/** Not keying up for this long after a signal: the speaker would key it */
#define VOX_RX_GUARD_MS 800
/** Longest VOX transmission: then it waits for quiet before keying again */
#define VOX_MAX_MS 180000

/**
 * @param level: VOX level, 1 to VOX_LEVELS.
 * @return the microphone level that keys up, 0 to 255.
 */
static inline uint8_t vox_threshold(uint8_t level)
{
    static const uint8_t thresholds[VOX_LEVELS] = {
        160, 120, 90, 70, 55, 42, 32, 24, 18, 12,
    };
    if (level < 1)
        level = 1;
    if (level > VOX_LEVELS)
        level = VOX_LEVELS;
    return thresholds[level - 1];
}

/**
 * @param delay: hang time choice, 0 to VOX_DELAYS - 1.
 * @return the hang time, ms.
 */
static inline uint32_t vox_hangMs(uint8_t delay)
{
    static const uint16_t hang[VOX_DELAYS] = { 500, 1000, 1500, 2000, 3000 };
    return hang[(delay < VOX_DELAYS) ? delay : 1];
}

#ifdef __cplusplus
}
#endif

#endif /* VOX_H */
