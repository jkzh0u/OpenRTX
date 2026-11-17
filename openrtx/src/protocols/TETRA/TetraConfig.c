/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/TETRA/TetraConfig.h"

/* Written by the UI, read by the receiver: single values, no lock needed */
static volatile bool follow = true;
static volatile bool roam = true;
static volatile bool alarmBeep = true;

void tetraConfig_set(bool f, bool r, bool a)
{
    follow = f;
    roam = r;
    alarmBeep = a;
}

bool tetraConfig_follow(void)
{
    return follow;
}

bool tetraConfig_roam(void)
{
    return roam;
}

bool tetraConfig_alarmBeep(void)
{
    return alarmBeep;
}
