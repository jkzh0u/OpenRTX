/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "protocols/TETRA/TetraIds.h"
#include "chlist_load.h"

/* The selection is global: start each test that uses it from "All" */
static void selectAll()
{
    tetraIds_release();
    while (tetraIds_selectedFilter() != 0)
        tetraIds_stepFilter(1);
}

TEST_CASE("TETRA identities have names", "[tetra][ids]")
{
    REQUIRE(std::strcmp(tetraIds_name(101011), "Transit Control") == 0);
    REQUIRE(std::strcmp(tetraIds_name(100112), "Run 112") == 0);
    REQUIRE(std::strcmp(tetraIds_name(120106), "PRESTO Techs") == 0);

    // Not listed, or no identity
    REQUIRE(tetraIds_name(9001) == nullptr);
    REQUIRE(tetraIds_name(0) == nullptr);
}

TEST_CASE("TETRA identity filters", "[tetra][ids]")
{
    REQUIRE(tetraIds_filterCount() == 4);
    REQUIRE(std::strcmp(tetraIds_filterName(0), "All") == 0);
    REQUIRE(std::strcmp(tetraIds_filterName(1), "Subway") == 0);
    REQUIRE(std::strcmp(tetraIds_filterName(3), "Control") == 0);
    REQUIRE(std::strcmp(tetraIds_filterName(4), "Hold") == 0);
    REQUIRE(tetraIds_filterName(5) == nullptr);

    // "All" lets anything through, even unknown identities
    REQUIRE(tetraIds_inFilter(9001, 0));

    // Membership
    REQUIRE(tetraIds_inFilter(100010, 1));
    REQUIRE(tetraIds_inFilter(100010, 2));
    REQUIRE_FALSE(tetraIds_inFilter(100010, 3));
    REQUIRE(tetraIds_inFilter(100020, 1));
    REQUIRE_FALSE(tetraIds_inFilter(100020, 2));
    REQUIRE(tetraIds_inFilter(101011, 3));

    // TF_EVERY: in every filter; 0: in none
    for (size_t i = 1; i < 4; i++) {
        REQUIRE(tetraIds_inFilter(60002, i));
        REQUIRE_FALSE(tetraIds_inFilter(120106, i));
        REQUIRE_FALSE(tetraIds_inFilter(9001, i));
    }

    // No such filter, or no held talkgroup
    REQUIRE_FALSE(tetraIds_inFilter(60002, 4));
    REQUIRE_FALSE(tetraIds_inFilter(60002, 5));
}

TEST_CASE("TETRA identity ranges", "[tetra][ids]")
{
    char buf[24];

    // Named by its range, with its filters
    REQUIRE(tetraIds_name(100253) == nullptr);
    REQUIRE(tetraIds_nameOf(100253, buf, sizeof(buf)));
    REQUIRE(std::strcmp(buf, "Run 253") == 0);
    REQUIRE(tetraIds_inFilter(100253, 1));
    REQUIRE_FALSE(tetraIds_inFilter(100253, 2));
    REQUIRE(tetraIds_inFilter(100150, 2));

    // Its own entry first
    REQUIRE(tetraIds_nameOf(100112, buf, sizeof(buf)));
    REQUIRE(std::strcmp(buf, "Run 112") == 0);
    REQUIRE(tetraIds_nameOf(101011, buf, sizeof(buf)));
    REQUIRE(std::strcmp(buf, "Transit Control") == 0);

    // Outside: no name
    REQUIRE_FALSE(tetraIds_nameOf(100300, buf, sizeof(buf)));
    REQUIRE_FALSE(tetraIds_nameOf(0, buf, sizeof(buf)));
    REQUIRE_FALSE(tetraIds_inFilter(100300, 1));
}

TEST_CASE("TETRA alarms and location areas", "[tetra][ids]")
{
    REQUIRE(tetraIds_isAlarm(60002));
    REQUIRE_FALSE(tetraIds_isAlarm(101011));
    REQUIRE_FALSE(tetraIds_isAlarm(9001));
    REQUIRE_FALSE(tetraIds_isAlarm(0));

    // The alarm bit is not a filter
    REQUIRE(tetraIds_inFilter(60002, 1));

    REQUIRE(std::strcmp(tetraIds_laName(39), "Line 5 east") == 0);
    REQUIRE(std::strcmp(tetraIds_laName(1), "CN Tower") == 0);
    REQUIRE(tetraIds_laName(2) == nullptr);
}

TEST_CASE("TETRA identity priorities", "[tetra][ids]")
{
    REQUIRE(tetraIds_priority(101011) == 5); // TF_PRIORITY(5)
    REQUIRE(tetraIds_priority(60002) == 15); // alarm, no number given
    REQUIRE(tetraIds_priority(100250) == 2); // in a range
    REQUIRE(tetraIds_priority(100150) == 0); // range without priority
    REQUIRE(tetraIds_priority(100010) == 0);
    REQUIRE(tetraIds_priority(9001) == 0);   // unknown
    REQUIRE(tetraIds_priority(0) == 0);

    // The priority does not change the identity or its filters
    REQUIRE(std::strcmp(tetraIds_name(101011), "Transit Control") == 0);
    REQUIRE(tetraIds_inFilter(101011, 3));
    REQUIRE_FALSE(tetraIds_inFilter(101011, 1));
}

TEST_CASE("TETRA identity filter selection wraps", "[tetra][ids]")
{
    selectAll();
    REQUIRE(tetraIds_selectedFilter() == 0);
    tetraIds_stepFilter(1);
    REQUIRE(tetraIds_selectedFilter() == 1);
    tetraIds_stepFilter(-1);
    tetraIds_stepFilter(-1);
    REQUIRE(tetraIds_selectedFilter() == 3);
    tetraIds_stepFilter(1);
    REQUIRE(tetraIds_selectedFilter() == 0);
}

TEST_CASE("TETRA talkgroup hold", "[tetra][ids]")
{
    const size_t HOLD = tetraIds_filterCount();

    selectAll();

    tetraIds_stepFilter(1); // Subway
    uint8_t sel = tetraIds_selection();

    // Holding any identity, listed or not
    tetraIds_hold(9001);
    REQUIRE(tetraIds_selection() != sel);
    REQUIRE(tetraIds_selectedFilter() == HOLD);
    REQUIRE(tetraIds_held() == 9001);
    REQUIRE(tetraIds_inFilter(9001, HOLD));
    REQUIRE_FALSE(tetraIds_inFilter(100010, HOLD));
    REQUIRE_FALSE(tetraIds_inFilter(0, HOLD));

    // Holding another one keeps the filter to return to
    tetraIds_hold(101011);
    REQUIRE(tetraIds_held() == 101011);

    // Release: back to the filter before
    tetraIds_release();
    REQUIRE(tetraIds_selectedFilter() == 1);
    REQUIRE(tetraIds_held() == 0);
    tetraIds_release();
    REQUIRE(tetraIds_selectedFilter() == 1);

    // Stepping releases too, from the filter before the hold
    tetraIds_hold(9001);
    tetraIds_stepFilter(1);
    REQUIRE(tetraIds_selectedFilter() == 2);
    REQUIRE(tetraIds_held() == 0);

    // Nothing to hold
    tetraIds_hold(0);
    REQUIRE(tetraIds_selectedFilter() == 2);
}
