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
static const uint16_t HOME_CARRIER = 3600;
static const uint16_t OTHER_CARRIER = 3620;

static tetraFollowCall makeCall(tetraFollowEvent event, uint16_t callId,
                                uint16_t carrier, uint8_t slots)
{
    tetraFollowCall c;
    c.event = event;
    c.callId = callId;
    c.carrier = carrier;
    c.slots = slots;
    c.speech = true;
    c.encrypted = false;
    return c;
}

static tetraFollowRetune makeRetune(uint16_t callId, uint16_t carrier,
                                    uint32_t freq, uint8_t slots)
{
    tetraFollowRetune r;
    r.main = false;
    r.callId = callId;
    r.carrier = carrier;
    r.freq = freq;
    r.mainFreq = HOME;
    r.slots = slots;
    return r;
}

static tetraFollowRetune makeMain()
{
    tetraFollowRetune r = makeRetune(0, HOME_CARRIER, HOME, 0);
    r.main = true;
    return r;
}

static void requireHome(const tetraFollow &f)
{
    tetraFollowTarget t = tetraFollow_target(&f);
    REQUIRE_FALSE(t.away);
    REQUIRE(t.freq == f.home);
    REQUIRE(t.slots == TETRA_FOLLOW_ALL_SLOTS);
}

static void requireAway(const tetraFollow &f, uint32_t freq, uint8_t slots)
{
    tetraFollowTarget t = tetraFollow_target(&f);
    REQUIRE(t.away);
    REQUIRE(t.freq == freq);
    REQUIRE(t.slots == slots);
}

/* Set-up of a call on the other carrier, then libtetra's retune request */
static void follow(tetraFollow &f, uint16_t callId, uint8_t slots,
                   long long now)
{
    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, callId, OTHER_CARRIER,
                                 slots);
    tetraFollow_call(&f, &c, now);
    tetraFollowRetune r = makeRetune(callId, OTHER_CARRIER, OTHER, slots);
    REQUIRE(tetraFollow_retune(&f, &r, now));
    requireAway(f, OTHER, slots);
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
    REQUIRE_FALSE(tetraFollow_tick(&f, 100000));
    requireHome(f);
}

TEST_CASE("TETRA follower follows a call to another carrier",
          "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);
    follow(f, 100, 0x2, 0);

    SECTION("the receiver brings it home")
    {
        tetraFollowRetune r = makeMain();
        REQUIRE(tetraFollow_retune(&f, &r, 10));
        requireHome(f);
    }

    SECTION("a release brings it home if the receiver does not")
    {
        tetraFollowCall c = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
        tetraFollow_call(&f, &c, 10);
        REQUIRE(tetraFollow_tick(&f, 20));
        requireHome(f);
        REQUIRE_FALSE(tetraFollow_tick(&f, 30));
    }

    SECTION("a new allocation on another carrier moves it")
    {
        tetraFollowRetune r = makeRetune(100, OTHER_CARRIER + 1, OTHER + 25000,
                                         0x8);
        REQUIRE(tetraFollow_retune(&f, &r, 10));
        requireAway(f, OTHER + 25000, 0x8);
    }

    SECTION("a new allocation on the same carrier changes the timeslots")
    {
        tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 100, OTHER_CARRIER,
                                     0x4);
        tetraFollow_call(&f, &c, 10);
        requireAway(f, OTHER, 0x4);
    }

    SECTION("an event without allocation keeps the channel")
    {
        tetraFollowCall c = makeCall(TETRA_FOLLOW_CEASED, 100, 0xffff, 0);
        tetraFollow_call(&f, &c, 10);
        requireAway(f, OTHER, 0x2);
    }

    SECTION("other calls are not followed meanwhile")
    {
        tetraFollowRetune r = makeRetune(101, OTHER_CARRIER + 1, OTHER + 25000,
                                         0x8);
        REQUIRE_FALSE(tetraFollow_retune(&f, &r, 10));
        requireAway(f, OTHER, 0x2);
    }

    SECTION("going home on request of the caller drops the call")
    {
        tetraFollow_home(&f);
        requireHome(f);
        tetraFollowRetune r = makeRetune(101, OTHER_CARRIER, OTHER, 0x8);
        REQUIRE(tetraFollow_retune(&f, &r, 10));
        requireAway(f, OTHER, 0x8);
    }
}

TEST_CASE("TETRA follower moves home with the cell", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);
    follow(f, 100, 0x2, 0);

    tetraFollow_setHome(&f, OTHER + 25000);
    tetraFollowTarget t = tetraFollow_target(&f);
    REQUIRE_FALSE(t.away);
    REQUIRE(t.freq == OTHER + 25000);
    REQUIRE_FALSE(f.selected);
}

TEST_CASE("TETRA follower keeps the radio's tuning offset", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME - 300);

    tetraFollowRetune r = makeRetune(5, OTHER_CARRIER, OTHER, 0x1);
    REQUIRE(tetraFollow_retune(&f, &r, 0));
    requireAway(f, OTHER - 300, 0x1);

    r = makeMain();
    REQUIRE(tetraFollow_retune(&f, &r, 10));
    requireHome(f);
    REQUIRE(tetraFollow_target(&f).freq == HOME - 300);

    // Too far from the main carrier, or the main carrier unknown: stay
    tetraFollow_init(&f, HOME + 25000);
    r = makeRetune(5, OTHER_CARRIER, OTHER, 0x1);
    REQUIRE_FALSE(tetraFollow_retune(&f, &r, 0));
    requireHome(f);

    tetraFollow_init(&f, HOME);
    r.mainFreq = 0;
    REQUIRE_FALSE(tetraFollow_retune(&f, &r, 0));
    requireHome(f);
}

TEST_CASE("TETRA follower stays with the call on the main carrier",
          "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);
    tetraFollowRetune other = makeRetune(6, OTHER_CARRIER, OTHER, 0x2);

    SECTION("selected by its set-up")
    {
        tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 5, HOME_CARRIER,
                                     0x2);
        tetraFollow_call(&f, &c, 0);
        REQUIRE_FALSE(tetraFollow_retune(&f, &other, 10));
        requireHome(f);

        // Once it ends, the next call is followed
        c.event = TETRA_FOLLOW_RELEASE;
        tetraFollow_call(&f, &c, 20);
        REQUIRE(tetraFollow_retune(&f, &other, 30));
        requireAway(f, OTHER, 0x2);
    }

    SECTION("selected by its traffic")
    {
        tetraFollow_traffic(&f, 5, true, false, 0);
        REQUIRE_FALSE(tetraFollow_retune(&f, &other, 10));

        // Once it is silent, the next call is followed
        tetraFollow_traffic(&f, 5, true, false, 1000);
        REQUIRE_FALSE(tetraFollow_tick(&f, 1000 + TETRA_FOLLOW_TIMEOUT_MS));
        REQUIRE(tetraFollow_retune(&f, &other, 1000 + TETRA_FOLLOW_TIMEOUT_MS));
        requireAway(f, OTHER, 0x2);
    }

    SECTION("an allocation of it on the main carrier keeps the radio home")
    {
        tetraFollowRetune r = makeRetune(5, HOME_CARRIER, HOME, 0x2);
        REQUIRE(tetraFollow_retune(&f, &r, 0));
        requireHome(f);
        REQUIRE_FALSE(tetraFollow_retune(&f, &other, 10));
    }
}

TEST_CASE("TETRA follower ignores calls it cannot play", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall c = makeCall(TETRA_FOLLOW_UPDATE, 1, OTHER_CARRIER, 0x2);
    tetraFollowRetune r = makeRetune(1, OTHER_CARRIER, OTHER, 0x2);

    SECTION("encrypted")
    {
        c.encrypted = true;
        tetraFollow_call(&f, &c, 0);
        REQUIRE_FALSE(tetraFollow_retune(&f, &r, 0));
        requireHome(f);

        f.followEncrypted = true;
        REQUIRE(tetraFollow_retune(&f, &r, 0));
        requireAway(f, OTHER, 0x2);
    }

    SECTION("data")
    {
        c.speech = false;
        tetraFollow_call(&f, &c, 0);
        REQUIRE_FALSE(tetraFollow_retune(&f, &r, 0));
        requireHome(f);
    }

    SECTION("not selected by events other than set-up or grant")
    {
        c.event = TETRA_FOLLOW_CEASED;
        tetraFollow_call(&f, &c, 0);
        REQUIRE_FALSE(f.selected);
        c.event = TETRA_FOLLOW_RELEASE;
        tetraFollow_call(&f, &c, 0);
        REQUIRE_FALSE(f.selected);
    }

    SECTION("a call never seen is followed")
    {
        r.callId = 2;
        REQUIRE(tetraFollow_retune(&f, &r, 0));
        requireAway(f, OTHER, 0x2);
    }

    SECTION("a followed call whose traffic turns out encrypted")
    {
        follow(f, 1, 0x2, 0);
        tetraFollow_traffic(&f, 1, true, true, 10);
        REQUIRE(tetraFollow_tick(&f, 20));
        requireHome(f);

        // Remembered: not followed again
        REQUIRE_FALSE(tetraFollow_retune(&f, &r, 30));
    }

    SECTION("a followed call that turns out encrypted in signalling")
    {
        follow(f, 1, 0x2, 0);
        c.encrypted = true;
        tetraFollow_call(&f, &c, 10);
        REQUIRE(tetraFollow_tick(&f, 20));
        requireHome(f);
    }
}

TEST_CASE("TETRA follower returns home after silence", "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);
    follow(f, 3, 0x1, 1000);

    // Traffic keeps it there, including traffic of an unknown call
    tetraFollow_traffic(&f, 3, true, false, 4000);
    tetraFollow_traffic(&f, 0, true, false, 8000);
    REQUIRE_FALSE(tetraFollow_tick(&f, 8000 + TETRA_FOLLOW_TIMEOUT_MS - 1));
    requireAway(f, OTHER, 0x1);

    // Traffic of another call does not
    tetraFollow_traffic(&f, 4, true, false, 9000);
    REQUIRE(tetraFollow_tick(&f, 8000 + TETRA_FOLLOW_TIMEOUT_MS));
    requireHome(f);
    REQUIRE_FALSE(f.selected);

    // At home, traffic of an unknown call selects nothing
    tetraFollow_traffic(&f, 0, true, false, 20000);
    REQUIRE_FALSE(f.selected);
}

TEST_CASE("TETRA follower remembers a limited number of calls",
          "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    // An encrypted call, then enough other calls to push it out
    tetraFollowCall c = makeCall(TETRA_FOLLOW_CEASED, 1, 0xffff, 0);
    c.encrypted = true;
    tetraFollow_call(&f, &c, 0);
    c.encrypted = false;
    for (uint16_t id = 2; id < 2 + TETRA_FOLLOW_MAX_CALLS; id++) {
        c.callId = id;
        tetraFollow_call(&f, &c, 0);
    }
    REQUIRE_FALSE(f.selected);

    // Forgotten: followed like a call never seen
    tetraFollowRetune r = makeRetune(1, OTHER_CARRIER, OTHER, 0x2);
    REQUIRE(tetraFollow_retune(&f, &r, 0));
    requireAway(f, OTHER, 0x2);
}
