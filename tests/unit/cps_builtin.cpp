/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "interfaces/cps_io.h"
#include "rtx/rtx.h"
#include "chlist_load.h"

TEST_CASE("Built-in codeplug reads its channel list", "[cps][builtin]")
{
    channel_t ch;

    REQUIRE(cps_open(nullptr) == 0);

    REQUIRE(cps_readChannel(&ch, 0) == 0);
    REQUIRE(std::strcmp(ch.name, "Main") == 0);
    REQUIRE(ch.mode == OPMODE_TETRA);
    REQUIRE(ch.rx_frequency == 422662500);
    REQUIRE(ch.tx_frequency == 422662500);
    REQUIRE(ch.bandwidth == BW_25);
    REQUIRE(ch.rx_only == 1);

    // Long names are cut to fit, with a terminator
    REQUIRE(cps_readChannel(&ch, 1) == 0);
    REQUIRE(ch.mode == OPMODE_FM);
    REQUIRE(std::strlen(ch.name) == sizeof(ch.name) - 1);
    REQUIRE(std::strncmp(ch.name, "A name longer", 13) == 0);

    REQUIRE(cps_readChannel(&ch, 2) == 0);
    REQUIRE(ch.mode == OPMODE_M17);

    // Past the end
    REQUIRE(cps_readChannel(&ch, 3) == -1);
    REQUIRE(cps_readChannel(&ch, 0xffff) == -1);
}

TEST_CASE("Built-in codeplug is read only", "[cps][builtin]")
{
    channel_t ch;
    contact_t ct;
    bankHdr_t bank;

    memset(&ch, 0, sizeof(ch));
    memset(&ct, 0, sizeof(ct));
    memset(&bank, 0, sizeof(bank));

    REQUIRE(cps_create(nullptr) == -1);
    REQUIRE(cps_writeChannel(ch, 0) == -1);
    REQUIRE(cps_insertChannel(ch, 0) == -1);
    REQUIRE(cps_deleteChannel(ch, 0) == -1);
    REQUIRE(cps_readContact(&ct, 0) == -1);
    REQUIRE(cps_readBankHeader(&bank, 0) == -1);
    REQUIRE(cps_readBankData(0, 0) == -1);
}
