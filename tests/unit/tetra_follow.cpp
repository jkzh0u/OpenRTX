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
    c.freq = 0;
    c.mainFreq = 0;
    c.priority = false;
    return c;
}

/** A call event with its carrier's frequency, as the receiver gives it */
static tetraFollowCall makeCallAt(tetraFollowEvent event, uint16_t callId,
                                  uint16_t carrier, uint32_t freq,
                                  uint8_t slots)
{
    tetraFollowCall c = makeCall(event, callId, carrier, slots);
    c.freq = freq;
    c.mainFreq = HOME;
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
    r.priority = false;
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

static bool refuseAll(uint16_t callId)
{
    (void)callId;
    return false;
}

TEST_CASE("TETRA follower joins late a call on another carrier",
          "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    // A call heard on the main carrier, then one allocated elsewhere
    tetraFollowCall a = makeCallAt(TETRA_FOLLOW_UPDATE, 100, HOME_CARRIER,
                                   HOME, 0x8);
    tetraFollow_call(&f, &a, 0);
    REQUIRE(f.selected);
    tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200, OTHER_CARRIER,
                                   OTHER, 0x1);
    tetraFollow_call(&f, &b, 10);
    REQUIRE(f.callId == 100);
    REQUIRE_FALSE(tetraFollow_join(&f, NULL, 20));
    requireHome(f);

    SECTION("when the first call ends")
    {
        tetraFollowCall rel = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
        tetraFollow_call(&f, &rel, 30);
        REQUIRE(tetraFollow_join(&f, NULL, 40));
        REQUIRE(f.callId == 200);
        requireAway(f, OTHER, 0x1);

        // Its release brings the radio home, and it is not joined again
        tetraFollowCall relB = makeCall(TETRA_FOLLOW_RELEASE, 200, 0xffff, 0);
        tetraFollow_call(&f, &relB, 50);
        REQUIRE(tetraFollow_tick(&f, 60));
        requireHome(f);
        REQUIRE_FALSE(tetraFollow_join(&f, NULL, 70));
    }

    SECTION("not when the talkgroup filter holds it back")
    {
        tetraFollowCall rel = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
        tetraFollow_call(&f, &rel, 30);
        REQUIRE_FALSE(tetraFollow_join(&f, refuseAll, 40));
        requireHome(f);
    }

    SECTION("not again after it went silent")
    {
        tetraFollowCall rel = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
        tetraFollow_call(&f, &rel, 30);
        REQUIRE(tetraFollow_join(&f, NULL, 40));
        REQUIRE(tetraFollow_tick(&f, 40 + TETRA_FOLLOW_TIMEOUT_MS));
        requireHome(f);
        REQUIRE_FALSE(tetraFollow_join(&f, NULL, 50 + TETRA_FOLLOW_TIMEOUT_MS));
    }

    SECTION("not once released")
    {
        tetraFollowCall relB = makeCall(TETRA_FOLLOW_RELEASE, 200, 0xffff, 0);
        tetraFollow_call(&f, &relB, 20);
        tetraFollowCall rel = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
        tetraFollow_call(&f, &rel, 30);
        REQUIRE_FALSE(tetraFollow_join(&f, NULL, 40));
    }

    SECTION("not when encrypted")
    {
        b.encrypted = true;
        tetraFollow_call(&f, &b, 20);
        tetraFollowCall rel = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
        tetraFollow_call(&f, &rel, 30);
        REQUIRE_FALSE(tetraFollow_join(&f, NULL, 40));
    }
}

TEST_CASE("TETRA follower does not join a call without a known carrier",
          "[tetra][follow]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall a = makeCallAt(TETRA_FOLLOW_UPDATE, 100, HOME_CARRIER,
                                   HOME, 0x8);
    tetraFollow_call(&f, &a, 0);

    // Allocation seen, but the cell's numbering not known yet
    tetraFollowCall b = makeCall(TETRA_FOLLOW_UPDATE, 200, OTHER_CARRIER, 0x1);
    b.mainFreq = HOME;
    tetraFollow_call(&f, &b, 10);

    tetraFollowCall rel = makeCall(TETRA_FOLLOW_RELEASE, 100, 0xffff, 0);
    tetraFollow_call(&f, &rel, 20);
    REQUIRE_FALSE(f.selected);
    REQUIRE_FALSE(tetraFollow_join(&f, NULL, 30));
    requireHome(f);
}

static tetraFollowCall release(uint16_t callId)
{
    return makeCall(TETRA_FOLLOW_RELEASE, callId, 0xffff, 0);
}

TEST_CASE("TETRA follower lets a priority call take over",
          "[tetra][follow][priority]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    // A call heard on the main carrier
    tetraFollowCall a = makeCallAt(TETRA_FOLLOW_UPDATE, 100, HOME_CARRIER,
                                   HOME, 0x8);
    tetraFollow_call(&f, &a, 0);
    REQUIRE(f.callId == 100);

    SECTION("a priority call on another carrier")
    {
        tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200,
                                       OTHER_CARRIER, OTHER, 0x1);
        b.priority = true;
        tetraFollow_call(&f, &b, 10);
        REQUIRE(f.callId == 200);
        REQUIRE(f.preempted);
        tetraFollowRetune r = makeRetune(200, OTHER_CARRIER, OTHER, 0x1);
        REQUIRE(tetraFollow_retune(&f, &r, 10));
        requireAway(f, OTHER, 0x1);
    }

    SECTION("a priority call on the main carrier, with its timeslot")
    {
        tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200, HOME_CARRIER,
                                       HOME, 0x2);
        b.priority = true;
        tetraFollow_call(&f, &b, 10);
        REQUIRE(f.callId == 200);
        REQUIRE(f.preempted);
        REQUIRE(f.callSlots == 0x2);
        requireHome(f);
    }

    SECTION("first asked for by the receiver")
    {
        tetraFollowRetune r = makeRetune(200, OTHER_CARRIER, OTHER, 0x1);
        r.priority = true;
        REQUIRE(tetraFollow_retune(&f, &r, 10));
        REQUIRE(f.callId == 200);
        REQUIRE(f.preempted);
        requireAway(f, OTHER, 0x1);
    }

    SECTION("not a call without priority")
    {
        tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200,
                                       OTHER_CARRIER, OTHER, 0x1);
        tetraFollow_call(&f, &b, 10);
        REQUIRE(f.callId == 100);
        REQUIRE_FALSE(f.preempted);
    }
}

TEST_CASE("TETRA follower keeps a priority call", "[tetra][follow][priority]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall a = makeCallAt(TETRA_FOLLOW_UPDATE, 100, HOME_CARRIER,
                                   HOME, 0x8);
    a.priority = true;
    tetraFollow_call(&f, &a, 0);

    SECTION("from another priority call")
    {
        tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200,
                                       OTHER_CARRIER, OTHER, 0x1);
        b.priority = true;
        tetraFollow_call(&f, &b, 10);
        REQUIRE(f.callId == 100);
    }

    SECTION("away on another carrier, where nothing else is seen")
    {
        tetraFollow_init(&f, HOME);
        follow(f, 300, 0x1, 0);
        tetraFollowRetune r = makeRetune(400, OTHER_CARRIER + 1, OTHER, 0x2);
        r.priority = true;
        REQUIRE_FALSE(tetraFollow_retune(&f, &r, 10));
        REQUIRE(f.callId == 300);
    }
}

TEST_CASE("TETRA follower stays home for a priority conversation",
          "[tetra][follow][priority]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);
    const long long hold = TETRA_FOLLOW_PRIORITY_HOLD_MS;

    // A priority call on the main carrier, and another call elsewhere
    tetraFollowCall a = makeCallAt(TETRA_FOLLOW_UPDATE, 100, HOME_CARRIER,
                                   HOME, 0x8);
    a.priority = true;
    tetraFollow_call(&f, &a, 0);
    tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200, OTHER_CARRIER,
                                   OTHER, 0x1);
    tetraFollow_call(&f, &b, 10);
    tetraFollowCall rel = release(100);
    rel.priority = true;
    tetraFollow_call(&f, &rel, 1000);

    SECTION("no late join during the hold, then it joins")
    {
        REQUIRE_FALSE(tetraFollow_join(&f, NULL, 1000 + hold - 1));
        requireHome(f);
        REQUIRE(tetraFollow_join(&f, NULL, 1000 + hold));
        requireAway(f, OTHER, 0x1);
    }

    SECTION("no retune for another call during the hold")
    {
        tetraFollowRetune r = makeRetune(200, OTHER_CARRIER, OTHER, 0x1);
        REQUIRE_FALSE(tetraFollow_retune(&f, &r, 2000));
        requireHome(f);
        REQUIRE(tetraFollow_retune(&f, &r, 1000 + hold));
        requireAway(f, OTHER, 0x1);
    }

    SECTION("the next priority call restarts the hold")
    {
        tetraFollowCall c = makeCallAt(TETRA_FOLLOW_UPDATE, 101, HOME_CARRIER,
                                       HOME, 0x8);
        c.priority = true;
        tetraFollow_call(&f, &c, 3000);
        tetraFollowCall rel2 = release(101);
        rel2.priority = true;
        tetraFollow_call(&f, &rel2, 5000);
        REQUIRE_FALSE(tetraFollow_join(&f, NULL, 1000 + hold));
        REQUIRE(tetraFollow_join(&f, NULL, 5000 + hold));
    }

    SECTION("a call on the main carrier is still heard meanwhile")
    {
        tetraFollowCall c = makeCallAt(TETRA_FOLLOW_UPDATE, 300, HOME_CARRIER,
                                       HOME, 0x4);
        tetraFollow_call(&f, &c, 2000);
        REQUIRE(f.selected);
        REQUIRE(f.callId == 300);
        requireHome(f);
    }
}

TEST_CASE("TETRA follower joins priority calls first",
          "[tetra][follow][priority]")
{
    tetraFollow f;
    tetraFollow_init(&f, HOME);

    tetraFollowCall a = makeCallAt(TETRA_FOLLOW_UPDATE, 100, HOME_CARRIER,
                                   HOME, 0x8);
    tetraFollow_call(&f, &a, 0);
    tetraFollowCall b = makeCallAt(TETRA_FOLLOW_UPDATE, 200, OTHER_CARRIER,
                                   OTHER, 0x1);
    tetraFollow_call(&f, &b, 10);
    // A priority call that the receiver could not join (selected elsewhere)
    tetraFollowCall c = makeCallAt(TETRA_FOLLOW_UPDATE, 300, OTHER_CARRIER,
                                   OTHER, 0x4);
    c.priority = true;
    f.away = true; // seen while away: it cannot take over
    tetraFollow_call(&f, &c, 20);
    f.away = false;
    REQUIRE(f.callId == 100);

    tetraFollowCall rel = release(100);
    tetraFollow_call(&f, &rel, 30);
    REQUIRE(tetraFollow_join(&f, NULL, 40));
    REQUIRE(f.callId == 300);
    requireAway(f, OTHER, 0x4);
}
