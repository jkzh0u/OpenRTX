/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include "protocols/TETRA/TetraFocus.h"

TEST_CASE("TETRA focus keeps the first call", "[tetra][focus]")
{
    tetraFocus f;
    tetraFocus_init(&f);

    // Call 8 starts, call 9 starts on another slot meanwhile
    REQUIRE(tetraFocus_offer(&f, 8, 3, false, 0));
    REQUIRE_FALSE(tetraFocus_offer(&f, 9, 4, false, 100));
    REQUIRE(tetraFocus_offer(&f, 8, 3, false, 200));

    // A pause shorter than the hang time keeps it
    REQUIRE_FALSE(
        tetraFocus_offer(&f, 9, 4, false, 200 + TETRA_FOCUS_HANG_MS - 1));
    REQUIRE(tetraFocus_offer(&f, 8, 3, false, 200 + TETRA_FOCUS_HANG_MS - 1));

    // Silent for the hang time: the other call takes over
    long long t = 200 + 2 * TETRA_FOCUS_HANG_MS;
    REQUIRE(tetraFocus_offer(&f, 9, 4, false, t));
    REQUIRE_FALSE(tetraFocus_offer(&f, 8, 3, false, t + 10));
}

TEST_CASE("TETRA focus matches traffic by usage marker", "[tetra][focus]")
{
    tetraFocus f;
    tetraFocus_init(&f);

    // The call event gives both, traffic only the usage marker
    REQUIRE(tetraFocus_offer(&f, 8, 3, false, 0));
    REQUIRE(tetraFocus_offer(&f, 0, 3, false, 10));
    REQUIRE_FALSE(tetraFocus_offer(&f, 0, 4, false, 20));

    // Traffic first, the call identifier learnt later
    tetraFocus_init(&f);
    REQUIRE(tetraFocus_offer(&f, 0, 5, false, 0));
    REQUIRE(tetraFocus_offer(&f, 12, 5, false, 10));
    REQUIRE(tetraFocus_offer(&f, 12, 0, false, 20));
    REQUIRE_FALSE(tetraFocus_offer(&f, 13, 6, false, 30));

    // Traffic that cannot be placed only takes a free focus
    REQUIRE_FALSE(tetraFocus_offer(&f, 0, 0, false, 40));
}

TEST_CASE("TETRA focus gives way to an alarm and on release", "[tetra][focus]")
{
    tetraFocus f;
    tetraFocus_init(&f);

    REQUIRE(tetraFocus_offer(&f, 8, 3, false, 0));
    REQUIRE(tetraFocus_offer(&f, 40, 7, true, 10));
    REQUIRE_FALSE(tetraFocus_offer(&f, 8, 3, false, 20));

    // Another alarm does not take over from an alarm
    REQUIRE_FALSE(tetraFocus_offer(&f, 41, 9, true, 30));

    // Released: the next call takes it at once
    tetraFocus_release(&f, 99);
    REQUIRE_FALSE(tetraFocus_offer(&f, 8, 3, false, 40));
    tetraFocus_release(&f, 40);
    REQUIRE(tetraFocus_offer(&f, 8, 3, false, 50));
}
