/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Identity list for the TETRA identities and filter tests
 * (tests/unit/tetra_ids.cpp, tests/unit/tetra_filter.cpp)
 */

CPS_CHANNEL("Ignored", 422662500, OPMODE_TETRA)

TETRA_FILTER(SUBWAY, "Subway")
TETRA_FILTER(LINE1, "Line 1")
TETRA_FILTER(CONTROL, "Control")

TETRA_ID(60002, "Red Alarm", TF_EVERY | TF_ALARM)
TETRA_ID(101011, "Transit Control", TF(CONTROL))
TETRA_ID(100010, "Line1 Line Calls", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100020, "Line2 Line Calls", TF(SUBWAY))
TETRA_ID(100112, "Run 112", TF(SUBWAY) | TF(LINE1))
TETRA_ID(120106, "PRESTO Techs", 0)

TETRA_ID_RANGE(100201, 100299, "Run %u", 100000, TF(SUBWAY))
TETRA_ID_RANGE(100101, 100199, "Run %u", 100000, TF(SUBWAY) | TF(LINE1))

TETRA_LA(39, "Line 5 east")
TETRA_LA(1, "CN Tower")
