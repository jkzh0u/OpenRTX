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
    REQUIRE(std::strcmp(tetraIds_name(60002), "Red Alarm") == 0);

    // Not listed, or no identity
    REQUIRE(tetraIds_name(9001) == nullptr);
    REQUIRE(tetraIds_name(0) == nullptr);
}

TEST_CASE("TETRA identities to listen to", "[tetra][ids]")
{
    size_t n = 99;
    const uint32_t *ids = tetraIds_listen(&n);

    // In list order, true and 1 both listen
    REQUIRE(n == 2);
    REQUIRE(ids[0] == 101011);
    REQUIRE(ids[1] == 102727);
}
