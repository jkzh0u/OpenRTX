/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Channel list for the built-in codeplug test (tests/unit/cps_builtin.cpp) */

CPS_CHANNEL("Main", 422662500, OPMODE_TETRA)
CPS_CHANNEL("A name longer than the 31 characters kept", 145500000, OPMODE_FM)
CPS_CHANNEL("M17", 433475000, OPMODE_M17)
