/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Display and UI settings of the Retevis C62 for the UI render tool, from
 * platform/targets/c62/hwconfig.h and c62.dts. Keep them in step.
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#define CONFIG_SCREEN_WIDTH 160
#define CONFIG_SCREEN_HEIGHT 128
#define CONFIG_PIX_FMT_RGB565
#define CONFIG_SCREEN_BRIGHTNESS
/* Shortcut menu drawn as a grid shaped like the keypad */
#define CONFIG_UI_MACRO_GRID
#define CONFIG_UI_FM_SCREEN
#define CONFIG_ROGER_BEEP
#define CONFIG_DCS
#define CONFIG_FAST_COPY
#define CONFIG_SPECTRUM
#define CONFIG_SCAN
#define CONFIG_VOX
#define CONFIG_NOISE_REDUCTION
#define CONFIG_BCAST_FM
#define CONFIG_WEATHER
#define CONFIG_MEMCH
#define CONFIG_TETRA_LOG
#define CONFIG_TETRA_AFC
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2
#define CONFIG_M17
#define CONFIG_TETRA

#endif /* HWCONFIG_H */
