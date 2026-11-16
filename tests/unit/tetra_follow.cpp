/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include "protocols/TETRA/TetraFollow.h"

static const uint32_t HOME = 390012500;
static const uint32_t OTHER = 390512500;

static tetraFollowCall makeCall(tetraFollowEvent event, uint16_t callId,
                                uint32_t freq, uint8_t slots)
{
    tetraFollowCall c;
    c.event = event;
    c.callId = callId;
    c.freq = freq;
    c.slots = slots;
    c.speech = true;
    c.encrypted = false;
    return c;
}

static void requireHome(const tetraFollow &f)
{
    tetraFollowTarget t = tetraFollow_target(&f);
    REQUIRE_FALSE(t.away);
    REQUIRE(t.freq == HOME);
    REQUIRE(t.slots == TETRA_FOLLOW_ALL_SLOTS);
}

static void requireAway(const tetraFollow &f, uint32_t freq, uint8_t slots)
{
    tetraFollowTarget t = tetraFollow_target(&f);
    REQUIRE(t.away);
    REQUIRE(t.freq == freq);
    REQUIRE(t.slots == slots);
}

TEST_CASE("TETRA carrier frequencies follow the radio's tuning",
          "[tetra][follow]")
{
    // Tuned exactly: the cell's numbering as is, the main carrier is home
    REQUIRE(tetraFollow_carrierFreq(HOME, HOME, OTHER) == OTHER);
    REQUIRE(tetraFollow_carrierFreq(HOME, HOME, HOME) == HOME);

    // A tuning correction carries over, in both directions
    REQUIRE(tetraFollow_carrierFreq(HOME + 300, HOME, OTHER) == OTHER + 300);
    REQUIRE(tetraFollow_carrierFreq(HOME - 300, HOME, OTHER) == OTHER - 300);
    REQUIRE(tetraFollow_carrierFreq(HOME - 300, HOME, HOME) == HOME - 300);

    // Not on the main carrier, or unknown values: do not follow
    REQUIRE(tetraFollow_carrierFreq(HOME + 25000, HOME, OTHER) == 0);
    REQUIRE(tetraFollow_carrierFreq(HOME - 12501, HOME, OTHER) == 0);
    REQUIRE(tetraFollow_carrierFreq(HOME + 12500, HOME, OTHER)
            == OTHER + 12500);
    REQUIRE(tetraFollow_carrierFreq(0, HOME, OTHER) == 0);
    REQUIRE(tetraFollow_carrierFreq(HOME, 0, OTHER) == 0);
    REQUIRE(tetraFollow_carrierFreq(HOME, HOME, 0) == 0);
}

TEST_CASE("TETRA follower starts on the main carrier", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);
    requireHome(f);
}

TEST_CASE("TETRA follower follows a grant on another carrier",
          "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 100, OTHER, 0x2);
    tetraFollow_call(&f, &c, 0);
    requireAway(f, OTHER, 0x2);

    SECTION("release brings it home")
    {
        c.event = TETRA_FOLLOW_RELEASE;
        tetraFollow_call(&f, &c, 10);
        requireHome(f);
    }

    SECTION("loss of synchronisation brings it home")
    {
        tetraFollow_lostSync(&f);
        requireHome(f);
    }

    SECTION("a new allocation moves it")
    {
        c = makeCall(TETRA_FOLLOW_UPDATE, 100, OTHER + 25000, 0x8);
        tetraFollow_call(&f, &c, 10);
        requireAway(f, OTHER + 25000, 0x8);
    }

    SECTION("an event without allocation keeps the channel")
    {
        c = makeCall(TETRA_FOLLOW_CEASED, 100, 0, 0);
        tetraFollow_call(&f, &c, 10);
        requireAway(f, OTHER, 0x2);
    }
}

TEST_CASE("TETRA follower handles transmission trunking", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 7, OTHER, 0x4);
    tetraFollow_call(&f, &c, 0);
    requireAway(f, OTHER, 0x4);

    // Transmission ceased: back to the control channel
    c = makeCall(TETRA_FOLLOW_CEASED, 7, OTHER, 0);
    tetraFollow_call(&f, &c, 100);
    requireHome(f);

    // The call stays selected: another call does not take over
    tetraFollowCall other = makeCall(TETRA_FOLLOW_UPDATE, 8, OTHER, 0x8);
    tetraFollow_call(&f, &other, 200);
    requireHome(f);

    // Next transmission of the same call, on a different channel
    c = makeCall(TETRA_FOLLOW_UPDATE, 7, OTHER - 25000, 0x2);
    tetraFollow_call(&f, &c, 300);
    requireAway(f, OTHER - 25000, 0x2);
}

TEST_CASE("TETRA follower treats the main carrier as home", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 5, HOME, 0x2);
    tetraFollow_call(&f, &c, 0);
    requireHome(f);

    // The call on the main carrier is selected: others wait
    tetraFollowCall other = makeCall(TETRA_FOLLOW_UPDATE, 6, OTHER, 0x2);
    tetraFollow_call(&f, &other, 10);
    requireHome(f);

    // Once it ends, the next call is followed
    c.event = TETRA_FOLLOW_RELEASE;
    tetraFollow_call(&f, &c, 20);
    tetraFollow_call(&f, &other, 30);
    requireAway(f, OTHER, 0x2);
}

TEST_CASE("TETRA follower ignores calls it cannot play", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 1, OTHER, 0x2);

    SECTION("encrypted")
    {
        c.encrypted = true;
        tetraFollow_call(&f, &c, 0);
        requireHome(f);

        f.followEncrypted = true;
        tetraFollow_call(&f, &c, 0);
        requireAway(f, OTHER, 0x2);
    }

    SECTION("data")
    {
        c.speech = false;
        tetraFollow_call(&f, &c, 0);
        requireHome(f);
    }

    SECTION("events other than set-up or grant")
    {
        c.event = TETRA_FOLLOW_CEASED;
        tetraFollow_call(&f, &c, 0);
        requireHome(f);
        c.event = TETRA_FOLLOW_RELEASE;
        tetraFollow_call(&f, &c, 0);
        requireHome(f);
    }

    SECTION("a followed call that turns out encrypted")
    {
        tetraFollow_call(&f, &c, 0);
        requireAway(f, OTHER, 0x2);
        tetraFollow_traffic(&f, 1, true, 10);
        requireHome(f);
    }
}

TEST_CASE("TETRA follower returns home after silence", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 3, OTHER, 0x1);
    tetraFollow_call(&f, &c, 1000);

    // Traffic keeps it there, including traffic of an unknown call
    tetraFollow_traffic(&f, 3, false, 4000);
    tetraFollow_traffic(&f, 0, false, 8000);
    tetraFollow_tick(&f, 8000 + TETRA_FOLLOW_TIMEOUT_MS - 1);
    requireAway(f, OTHER, 0x1);

    // Traffic of another call does not
    tetraFollow_traffic(&f, 4, false, 9000);
    tetraFollow_tick(&f, 8000 + TETRA_FOLLOW_TIMEOUT_MS);
    requireHome(f);

    // Neither does traffic of an unknown call at home
    tetraFollow_init(&f, HOME);
    c = makeCall(TETRA_FOLLOW_UPDATE, 3, HOME, 0x1);
    tetraFollow_call(&f, &c, 0);
    tetraFollow_traffic(&f, 0, false, 4000);
    tetraFollow_tick(&f, TETRA_FOLLOW_TIMEOUT_MS);
    REQUIRE_FALSE(f.selected);
}
