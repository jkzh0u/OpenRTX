/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include "protocols/TETRA/TetraFilter.h"

/* Filters and identities of tests/unit/tetra_ids_list.h */
static const size_t ALL = 0, SUBWAY = 1, LINE1 = 2, CONTROL = 3;
static const uint32_t RED_ALARM = 60002, TRANSIT_CONTROL = 101011,
                      LINE1_CALLS = 100010, LINE2_CALLS = 100020,
                      RUN_112 = 100112, UNKNOWN = 9001, DISPATCHER = 500001;

TEST_CASE("TETRA filter lets everything through by default",
          "[tetra][filter]")
{
    tetraFilter f;
    tetraFilter_init(&f);

    REQUIRE(tetraFilter_call(&f, 1, UNKNOWN, 0, false));
    REQUIRE(tetraFilter_traffic(&f, 1, 0, false));
    REQUIRE(tetraFilter_traffic(&f, 0, 0, false));
    REQUIRE(tetraFilter_sds(&f, UNKNOWN, UNKNOWN));
    REQUIRE(tetraFilter_passes(&f, 99));
}

TEST_CASE("TETRA filter passes group calls of its talkgroups",
          "[tetra][filter]")
{
    tetraFilter f;
    tetraFilter_init(&f);
    tetraFilter_select(&f, LINE1);

    // A Line 1 group call, its traffic, and its release
    REQUIRE(tetraFilter_call(&f, 1, LINE1_CALLS, 0, false));
    REQUIRE(tetraFilter_traffic(&f, 1, UNKNOWN, false));
    REQUIRE(tetraFilter_passes(&f, 1));

    // A Line 2 group call does not get through, nor its traffic
    REQUIRE_FALSE(tetraFilter_call(&f, 2, LINE2_CALLS, 0, false));
    REQUIRE_FALSE(tetraFilter_traffic(&f, 2, UNKNOWN, false));
    REQUIRE_FALSE(tetraFilter_passes(&f, 2));

    // Released: the release gets through, then the call is forgotten
    REQUIRE(tetraFilter_call(&f, 1, 0, 0, true));
    REQUIRE_FALSE(tetraFilter_passes(&f, 1));
}

TEST_CASE("TETRA filter passes point to point calls by either radio",
          "[tetra][filter]")
{
    tetraFilter f;
    tetraFilter_init(&f);
    tetraFilter_select(&f, LINE1);

    // Train run 112 calls a dispatcher who is not in the list
    REQUIRE(tetraFilter_call(&f, 5, DISPATCHER, RUN_112, false));
    // The dispatcher answers: still the same call
    REQUIRE(tetraFilter_call(&f, 5, 0, DISPATCHER, false));
    REQUIRE(tetraFilter_traffic(&f, 5, DISPATCHER, false));

    // The dispatcher calls the train
    REQUIRE(tetraFilter_call(&f, 6, RUN_112, DISPATCHER, false));
}

TEST_CASE("TETRA filter holds back what it cannot place",
          "[tetra][filter]")
{
    tetraFilter f;
    tetraFilter_init(&f);
    tetraFilter_select(&f, SUBWAY);

    // Parties not known yet
    REQUIRE_FALSE(tetraFilter_call(&f, 7, 0, 0, false));
    REQUIRE_FALSE(tetraFilter_traffic(&f, 7, 0, false));
    REQUIRE_FALSE(tetraFilter_traffic(&f, 0, 0, false));

    // Traffic of an unknown call by a listed talker gets through
    REQUIRE(tetraFilter_traffic(&f, 0, RUN_112, false));

    // On a followed call's carrier, all traffic is that call's
    REQUIRE(tetraFilter_traffic(&f, 0, 0, true));

    // A later event names the group
    REQUIRE(tetraFilter_call(&f, 7, LINE2_CALLS, 0, false));
    REQUIRE(tetraFilter_traffic(&f, 7, 0, false));
}

TEST_CASE("TETRA filter checks known calls again on a new selection",
          "[tetra][filter]")
{
    tetraFilter f;
    tetraFilter_init(&f);

    tetraFilter_call(&f, 1, LINE1_CALLS, 0, false);
    tetraFilter_call(&f, 2, TRANSIT_CONTROL, 0, false);
    tetraFilter_call(&f, 3, RED_ALARM, 0, false);

    tetraFilter_select(&f, CONTROL);
    REQUIRE_FALSE(tetraFilter_passes(&f, 1));
    REQUIRE(tetraFilter_passes(&f, 2));
    REQUIRE(tetraFilter_passes(&f, 3)); // in every filter

    tetraFilter_select(&f, LINE1);
    REQUIRE(tetraFilter_passes(&f, 1));
    REQUIRE_FALSE(tetraFilter_passes(&f, 2));
    REQUIRE(tetraFilter_passes(&f, 3));

    tetraFilter_select(&f, ALL);
    REQUIRE(tetraFilter_passes(&f, 2));
}

TEST_CASE("TETRA filter passes short messages by either party",
          "[tetra][filter]")
{
    tetraFilter f;
    tetraFilter_init(&f);
    tetraFilter_select(&f, CONTROL);

    REQUIRE(tetraFilter_sds(&f, TRANSIT_CONTROL, UNKNOWN));
    REQUIRE(tetraFilter_sds(&f, UNKNOWN, TRANSIT_CONTROL));
    REQUIRE_FALSE(tetraFilter_sds(&f, RUN_112, LINE1_CALLS));
}
