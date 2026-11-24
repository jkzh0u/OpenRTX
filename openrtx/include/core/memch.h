/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MEMCH_H
#define MEMCH_H

#include <stdbool.h>
#include <stdint.h>
#include "core/cps.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FM memory channels saved on the radio (CONFIG_MEMCH), as on the Quansheng
 * UV-K5: numbered slots that the radio fills, renames and clears itself,
 * kept in flash. Memory mode shows them after the channels of the flashed
 * channel list, which stays read only. FM only: the frequency, transmit
 * frequency, width, power, tones and a name.
 *
 * Slots are numbered from 0 here, from 1 (M1) on the screen.
 */

/** Saved channel slots */
#define MEMCH_SLOTS 200

/** Characters of a saved channel's name */
#define MEMCH_NAME_LEN 15

/** @return true if the store could be read (or started empty). */
bool memch_ready(void);

/** @return true if the slot holds a channel. */
bool memch_used(uint16_t slot);

/**
 * Read a saved channel.
 *
 * @param slot: slot number.
 * @param ch: the channel, an FM channel.
 * @return false if the slot is empty.
 */
bool memch_read(uint16_t slot, channel_t *ch);

/**
 * Save an FM channel to a slot, replacing what was there. A channel without
 * a name is named after its frequency. Writes the flash: takes a fraction
 * of a second.
 *
 * @param slot: slot number.
 * @param ch: the channel.
 * @return 0 on success, a negative value if not FM or the write failed.
 */
int memch_save(uint16_t slot, const channel_t *ch);

/**
 * Rename a saved channel.
 *
 * @return 0 on success, a negative value if the slot is empty or the write
 * failed.
 */
int memch_rename(uint16_t slot, const char *name);

/**
 * Clear a slot.
 *
 * @return 0 on success, a negative value if the write failed.
 */
int memch_delete(uint16_t slot);

/** @return the number of slots in use. */
uint16_t memch_count(void);

/**
 * @param n: 0 for the first used slot, 1 for the next...
 * @return the slot of the n-th channel saved, in slot order, or -1.
 */
int memch_nth(uint16_t n);

#ifdef __cplusplus
}
#endif

#endif /* MEMCH_H */
