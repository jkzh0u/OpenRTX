/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_CONFIG_H
#define OPENRTX_TETRA_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA receive settings chosen on the radio (Settings > TETRA), set by the
 * UI from the saved settings and read by the receiver. All on at start.
 */

/**
 * @param follow: follow calls to other carriers.
 * @param join: also join late a call going on on another carrier, when
 * back on the main carrier with nothing to play (needs follow).
 * @param roam: move between cells by itself; a search asked for on the
 * keypad still runs when off.
 * @param alarmBeep: beep at the start of an alarm call; the screen turns
 * red either way.
 */
void tetraConfig_set(bool follow, bool join, bool roam, bool alarmBeep);

bool tetraConfig_follow(void);
bool tetraConfig_join(void);
bool tetraConfig_roam(void);
bool tetraConfig_alarmBeep(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_CONFIG_H */
