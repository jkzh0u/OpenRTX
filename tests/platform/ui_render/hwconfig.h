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
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2
#define CONFIG_M17
#define CONFIG_TETRA

#endif /* HWCONFIG_H */
