/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include "protocols/TETRA/TetraCalls.h"

TEST_CASE("TETRA recent calls, newest first", "[tetra][calls]")
{
    tetraCalls c;
    tetraCalls_init(&c);
    REQUIRE(c.count == 0);

    tetraCalls_note(&c, 100010, 0, true, false, false, 1000);
    tetraCalls_note(&c, 101011, 320026, true, true, false, 2000);
    REQUIRE(c.count == 2);
    REQUIRE(c.entry[0].dst == 101011);
    REQUIRE(c.entry[0].src == 320026);
    REQUIRE(c.entry[0].voice);
    REQUIRE(c.entry[1].dst == 100010);

    // Heard again: back to the front, keeping what it learnt
    tetraCalls_note(&c, 100010, 100112, true, true, false, 3000);
    REQUIRE(c.count == 2);
    REQUIRE(c.entry[0].dst == 100010);
    REQUIRE(c.entry[0].src == 100112);
    REQUIRE(c.entry[0].time == 3000);
    REQUIRE(c.entry[0].first == 1000);
    tetraCalls_note(&c, 100010, 0, true, false, true, 4000);
    REQUIRE(c.entry[0].src == 100112);
    REQUIRE(c.entry[0].voice);
    REQUIRE(c.entry[0].encrypted);
    REQUIRE(c.entry[1].dst == 101011);
}

TEST_CASE("TETRA recent calls count transmissions", "[tetra][calls]")
{
    tetraCalls c;
    tetraCalls_init(&c);

    // Set-up without speech: none heard yet
    tetraCalls_note(&c, 100010, 0, true, false, false, 0);
    REQUIRE(c.entry[0].heard == 0);

    // A transmission: speech every 60 ms for a second
    for (long long t = 100; t < 1100; t += 60)
        tetraCalls_note(&c, 100010, 100112, true, true, false, t);
    REQUIRE(c.entry[0].heard == 1);

    // A short gap is the same transmission, a pause a new one
    tetraCalls_note(&c, 100010, 100112, true, true, false, 1500);
    REQUIRE(c.entry[0].heard == 1);
    tetraCalls_note(&c, 100010, 100253, true, true, false,
                    1500 + TETRA_CALLS_PAUSE_MS);
    REQUIRE(c.entry[0].heard == 2);
    REQUIRE(c.entry[0].src == 100253);
}

TEST_CASE("TETRA recent calls keep the newest ones", "[tetra][calls]")
{
    tetraCalls c;
    tetraCalls_init(&c);

    for (uint32_t i = 1; i <= TETRA_CALLS_MAX + 3; i++)
        tetraCalls_note(&c, 1000 + i, 0, true, true, false, i);

    REQUIRE(c.count == TETRA_CALLS_MAX);
    REQUIRE(c.entry[0].dst == 1000 + TETRA_CALLS_MAX + 3);
    REQUIRE(c.entry[TETRA_CALLS_MAX - 1].dst == 1004);
}

TEST_CASE("TETRA recent calls without a called party", "[tetra][calls]")
{
    tetraCalls c;
    tetraCalls_init(&c);

    // Nothing known: ignored
    tetraCalls_note(&c, 0, 0, false, true, false, 1);
    REQUIRE(c.count == 0);

    // Only the talker known: kept by talker
    tetraCalls_note(&c, 0, 100112, false, true, false, 2);
    tetraCalls_note(&c, 0, 100112, false, true, false, 3);
    REQUIRE(c.count == 1);
    REQUIRE(c.entry[0].src == 100112);
    REQUIRE(c.entry[0].time == 3);
}
