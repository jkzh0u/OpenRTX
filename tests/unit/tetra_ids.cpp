/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "protocols/TETRA/TetraIds.h"

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
    REQUIRE(tetraIds_filterName(4) == nullptr);

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

    // No such filter
    REQUIRE_FALSE(tetraIds_inFilter(60002, 4));
}

TEST_CASE("TETRA identity filter selection wraps", "[tetra][ids]")
{
    REQUIRE(tetraIds_selectedFilter() == 0);
    tetraIds_stepFilter(1);
    REQUIRE(tetraIds_selectedFilter() == 1);
    tetraIds_stepFilter(-1);
    tetraIds_stepFilter(-1);
    REQUIRE(tetraIds_selectedFilter() == 3);
    tetraIds_stepFilter(1);
    REQUIRE(tetraIds_selectedFilter() == 0);
}
