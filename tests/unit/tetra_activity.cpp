/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include "protocols/TETRA/TetraActivity.h"

static const long long HOUR = 3600000LL;
static const long long T0 = 100 * HOUR; // not at the start of the clock

TEST_CASE("TETRA activity counts calls and talk time", "[tetra][activity]")
{
    static tetraActivity a;
    tetraActivityEntry e;
    tetraActivity_init(&a, T0);

    // Call 7 to 101162: 4 s, a pause, 6 s, released
    tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, 7, 101162, true, T0);
    tetraActivity_note(&a, TETRA_ACTIVITY_CEASE, 7, 0, false, T0 + 4000);
    tetraActivity_note(&a, TETRA_ACTIVITY_TALK, 7, 0, false, T0 + 5000);
    tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, 7, 0, false, T0 + 11000);

    // Call 8 to 101167: 2 s
    tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, 8, 101167, true, T0 + 12000);
    tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, 8, 0, false, T0 + 14000);

    REQUIRE(a.count == 2);
    REQUIRE(tetraActivity_get(&a, 0, T0 + 20000, &e));
    REQUIRE(e.dst == 101162);
    REQUIRE(e.calls == 1);
    REQUIRE(e.talks == 2);
    REQUIRE(e.talkMs == 10000);
    REQUIRE(e.hourSec[0] == 10);
    REQUIRE_FALSE(e.talking);
    REQUIRE(e.first == T0);
    REQUIRE(e.last == T0 + 11000);

    REQUIRE(tetraActivity_get(&a, 1, T0 + 20000, &e));
    REQUIRE(e.dst == 101167);
    REQUIRE(e.talkMs == 2000);
    REQUIRE_FALSE(tetraActivity_get(&a, 2, T0 + 20000, &e));
}

TEST_CASE("TETRA activity: a call whose group is learnt late",
          "[tetra][activity]")
{
    static tetraActivity a;
    tetraActivityEntry e;
    tetraActivity_init(&a, T0);

    // Talking before the called group is known
    tetraActivity_note(&a, TETRA_ACTIVITY_TALK, 3, 0, false, T0);
    REQUIRE(a.count == 0);
    tetraActivity_note(&a, TETRA_ACTIVITY_SEEN, 3, 102747, true, T0 + 1000);
    tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, 3, 0, false, T0 + 3000);

    REQUIRE(tetraActivity_get(&a, 0, T0 + 3000, &e));
    REQUIRE(e.dst == 102747);
    REQUIRE(e.calls == 1);
    REQUIRE(e.talks == 1);
    REQUIRE(e.talkMs == 3000);
}

TEST_CASE("TETRA activity: talking now, and a call that goes quiet",
          "[tetra][activity]")
{
    static tetraActivity a;
    tetraActivityEntry e;
    tetraActivity_init(&a, T0);

    tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, 5, 101152, true, T0);
    tetraActivity_note(&a, TETRA_ACTIVITY_SEEN, 5, 0, false, T0 + 3000);

    // Still going: counted up to now
    REQUIRE(tetraActivity_get(&a, 0, T0 + 5000, &e));
    REQUIRE(e.talking);
    REQUIRE(e.talkMs == 5000);

    // No release heard: it ends at the last news
    tetraActivity_update(&a, T0 + 3000 + TETRA_ACTIVITY_IDLE_MS);
    REQUIRE(tetraActivity_get(&a, 0, T0 + 60000, &e));
    REQUIRE_FALSE(e.talking);
    REQUIRE(e.talkMs == 3000);
}

TEST_CASE("TETRA activity: hours go by", "[tetra][activity]")
{
    static tetraActivity a;
    tetraActivityEntry e;
    tetraActivity_init(&a, T0);

    // Speech keeps coming: news every 15 s
    tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, 1, 101152, true, T0);
    tetraActivity_note(&a, TETRA_ACTIVITY_SEEN, 1, 0, false, T0 + 15000);
    tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, 1, 0, false, T0 + 30000);
    tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, 2, 101152, true,
                       T0 + 2 * HOUR);
    tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, 2, 0, false,
                       T0 + 2 * HOUR + 10000);

    REQUIRE(tetraActivity_get(&a, 0, T0 + 2 * HOUR + 20000, &e));
    REQUIRE(e.calls == 2);
    REQUIRE(e.hourSec[0] == 10);
    REQUIRE(e.hourSec[1] == 0);
    REQUIRE(e.hourSec[2] == 30);

    // A day later the first hour is gone, the total stays
    REQUIRE(tetraActivity_get(&a, 0, T0 + 24 * HOUR + 1000, &e));
    REQUIRE(e.hourSec[22] == 10);
    REQUIRE(e.hourSec[23] == 0);
    REQUIRE(e.talkMs == 40000);
}

TEST_CASE("TETRA activity: full, the least busy goes", "[tetra][activity]")
{
    static tetraActivity a;
    tetraActivityEntry e;
    tetraActivity_init(&a, T0);

    long long t = T0;
    for (uint32_t i = 0; i < TETRA_ACTIVITY_MAX; i++) {
        uint16_t id = (uint16_t)(i + 1);
        tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, id, 200000 + i, true, t);
        // Each talks a tenth of a second longer than the one before
        t += 100 * (i + 1);
        tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, id, 0, false, t);
    }
    REQUIRE(a.count == TETRA_ACTIVITY_MAX);

    tetraActivity_note(&a, TETRA_ACTIVITY_SETUP, 900, 300000, true, t);
    tetraActivity_note(&a, TETRA_ACTIVITY_RELEASE, 900, 0, false, t + 5000);
    REQUIRE(a.count == TETRA_ACTIVITY_MAX);

    bool first = false, newOne = false;
    for (uint8_t n = 0; tetraActivity_get(&a, n, t + 5000, &e); n++) {
        first |= e.dst == 200000;
        newOne |= e.dst == 300000;
    }
    REQUIRE_FALSE(first);
    REQUIRE(newOne);

    // Busiest first: the new one talked 5 s, then the longest of the others
    REQUIRE(tetraActivity_get(&a, 0, t + 5000, &e));
    REQUIRE(e.dst == 300000);
    REQUIRE(tetraActivity_get(&a, 1, t + 5000, &e));
    REQUIRE(e.dst == 200000 + TETRA_ACTIVITY_MAX - 1);
}
