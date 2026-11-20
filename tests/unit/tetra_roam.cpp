/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <vector>
#include "protocols/TETRA/TetraRoam.h"

static const uint32_t A = 422325000, B = 422525000, C = 422262500,
                      D = 422462500, E = 423412500;

/*
 * A simulated radio: signal strength per frequency, the carriers that lock
 * (after a delay), and what the tracker asked for.
 */
struct Radio {
    std::map<uint32_t, int> rssi;
    std::set<uint32_t> lockable;
    uint32_t tuned;
    long long tunedAt = 0;
    long long now = 0;
    bool busy = false;
    bool deaf = false; // Locked, but nothing decodes
    std::vector<uint32_t> probes;
    std::vector<uint32_t> tunes;

    int level(uint32_t f) const
    {
        auto it = rssi.find(f);
        return it == rssi.end() ? -130 : it->second;
    }

    bool locked() const
    {
        return lockable.count(tuned) != 0 && now - tunedAt >= 300;
    }

    /* Run the tracker for some time, 30 ms per tick */
    void run(tetraRoam &r, long long ms)
    {
        for (long long end = now + ms; now < end; now += 30) {
            tetraRoamStep s = tetraRoam_tick(&r, now, locked(),
                                             locked() && !deaf, busy,
                                             level(tuned));
            if (s.action == TETRA_ROAM_PROBE) {
                probes.push_back(s.freq);
                tetraRoam_probed(&r, s.freq, level(s.freq));
            } else if (s.action == TETRA_ROAM_TUNE) {
                tunes.push_back(s.freq);
                tuned = s.freq;
                tunedAt = now;
            }
        }
    }
};

static Radio start(tetraRoam &r, uint32_t serving)
{
    Radio radio;
    radio.tuned = serving;
    radio.tunedAt = -1000;
    radio.lockable.insert(serving);
    tetraRoam_init(&r, serving, 0);
    return radio;
}

TEST_CASE("TETRA roaming stays on a strong cell", "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, A);
    uint32_t nb[] = { B, C };
    tetraRoam_setNeighbours(&r, nb, 2);
    radio.rssi = { { A, -70 }, { B, -60 }, { C, -65 } };

    radio.run(r, 20000);
    REQUIRE(radio.probes.empty());
    REQUIRE(radio.tunes.empty());
}

TEST_CASE("TETRA roaming moves to a clearly stronger neighbour",
          "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, A);
    uint32_t nb[] = { B, C, A, 0 }; // the serving cell and 0 are skipped
    tetraRoam_setNeighbours(&r, nb, 4);
    REQUIRE(r.nNeighbours == 2);
    radio.lockable.insert(B);

    SECTION("stronger twice in a row: move")
    {
        // Weak, not yet about to be lost: confirmed before moving
        radio.rssi = { { A, -89 }, { B, -80 }, { C, -98 } };
        radio.run(r, TETRA_ROAM_CHECK_MS * 2 + 300);
        REQUIRE(radio.probes == std::vector<uint32_t>({ B, C, B, C }));
        REQUIRE(radio.tunes == std::vector<uint32_t>({ B }));
        REQUIRE(r.serving == B);

        // Locks there and stays
        radio.run(r, 5000);
        REQUIRE(radio.locked());
        REQUIRE(radio.tunes.size() == 1);
    }

    SECTION("not stronger by the margin: stay")
    {
        radio.rssi = { { A, -89 }, { B, -85 }, { C, -98 } };
        radio.run(r, TETRA_ROAM_CHECK_MS * 4);
        REQUIRE_FALSE(radio.probes.empty());
        REQUIRE(radio.tunes.empty());
    }

    SECTION("not while a call is received")
    {
        radio.rssi = { { A, -89 }, { B, -80 } };
        radio.busy = true;
        radio.run(r, TETRA_ROAM_CHECK_MS * 4);
        REQUIRE(radio.probes.empty());
        REQUIRE(radio.tunes.empty());
    }

    SECTION("about to be lost: during a call too, at the first measurement")
    {
        radio.rssi = { { A, -93 }, { B, -85 } };
        radio.busy = true;
        radio.run(r, TETRA_ROAM_CHECK_MS + 300);
        REQUIRE(radio.probes == std::vector<uint32_t>({ B, C }));
        REQUIRE(radio.tunes == std::vector<uint32_t>({ B }));
    }

    SECTION("found stronger once, then about to be lost: moves at once")
    {
        radio.rssi = { { A, -89 }, { B, -80 }, { C, -98 } };
        radio.run(r, TETRA_ROAM_CHECK_MS + 300); // one check: B, once
        REQUIRE(radio.tunes.empty());
        radio.deaf = true; // nothing decodes any more
        radio.run(r, 60);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ B }));
    }

    SECTION("lost during a check: the strongest neighbour, no margin")
    {
        // Weak, and the neighbour not stronger by the margin
        radio.rssi = { { A, -89 }, { B, -86 }, { C, -98 } };
        radio.run(r, TETRA_ROAM_CHECK_MS + 30); // the check starts
        REQUIRE(radio.probes.size() == 1);
        radio.lockable.erase(A); // and the lock goes as it measures
        radio.run(r, 300);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ B }));
    }

    SECTION("nothing decoded, however strong: a neighbour as strong will do")
    {
        radio.rssi = { { A, -75 }, { B, -75 }, { C, -98 } };
        radio.deaf = true;
        radio.run(r, TETRA_ROAM_CHECK_MS + 300);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ B }));
    }

    SECTION("a short dip does not start a check: the signal is averaged")
    {
        radio.rssi = { { A, -75 }, { B, -60 } };
        radio.run(r, 5000);
        radio.rssi[A] = -100; // under a bridge for 300 ms
        radio.run(r, 300);
        radio.rssi[A] = -75;
        radio.run(r, TETRA_ROAM_CHECK_MS * 2);
        REQUIRE(radio.probes.empty());
    }
}

TEST_CASE("TETRA roaming searches when the lock is lost", "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, A);
    uint32_t nb[] = { B };
    tetraRoam_setNeighbours(&r, nb, 1);
    tetraRoam_addScan(&r, C);
    tetraRoam_addScan(&r, D);
    tetraRoam_addScan(&r, C); // once only

    // The cell disappears
    radio.lockable.clear();
    radio.run(r, TETRA_ROAM_LOST_MS - 100);
    REQUIRE_FALSE(tetraRoam_searching(&r));

    SECTION("the strongest carrier that locks is found")
    {
        // D is strongest but does not lock, C locks
        radio.rssi = { { B, -110 }, { C, -90 }, { D, -80 } };
        radio.lockable = { C };
        radio.run(r, 500);
        REQUIRE(tetraRoam_searching(&r));
        // Neighbours first, then the known carriers, then the old cell
        REQUIRE(radio.probes == std::vector<uint32_t>({ B, C, D, A }));
        REQUIRE(radio.tunes == std::vector<uint32_t>({ D }));

        radio.run(r, TETRA_ROAM_TRY_MS + 1000);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ D, C }));
        REQUIRE(radio.locked());
        REQUIRE_FALSE(tetraRoam_searching(&r));
        REQUIRE(r.serving == C);
    }

    SECTION("nothing heard: pause, then search again")
    {
        radio.rssi = {};
        radio.run(r, 500);
        REQUIRE(r.state == TETRA_ROAM_PAUSE);
        REQUIRE(radio.tunes.empty());

        radio.rssi = { { E, -90 } };
        tetraRoam_addScan(&r, E);
        radio.lockable = { E };
        radio.run(r, TETRA_ROAM_PAUSE_MS + 1500);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ E }));
        REQUIRE(radio.locked());
    }

    SECTION("the lock returns by itself")
    {
        radio.rssi = { { A, -100 } };
        // Back before the lost timer runs out
        radio.lockable = { A };
        radio.tunedAt = radio.now - 300;
        radio.run(r, 1000);
        REQUIRE_FALSE(tetraRoam_searching(&r));
        REQUIRE(radio.tunes.empty());
    }
}

TEST_CASE("TETRA roaming searches for the best signal on request",
          "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, A);
    uint32_t nb[] = { B };
    tetraRoam_setNeighbours(&r, nb, 1);
    tetraRoam_addScan(&r, C);
    tetraRoam_addScan(&r, D);
    radio.lockable = { A, B, C, D };

    // Strong enough that cell tracking alone would stay
    radio.run(r, 1000);
    REQUIRE(radio.probes.empty());

    SECTION("a clearly stronger carrier: move to it")
    {
        radio.rssi = { { A, -80 }, { B, -90 }, { C, -70 }, { D, -100 } };
        tetraRoam_search(&r, radio.now);
        REQUIRE(tetraRoam_searching(&r));

        // Measures every carrier while still locked, then moves
        radio.run(r, 500);
        REQUIRE(radio.probes == std::vector<uint32_t>({ B, C, D, A }));
        REQUIRE(radio.tunes == std::vector<uint32_t>({ C }));

        radio.run(r, 1000);
        REQUIRE(radio.locked());
        REQUIRE_FALSE(tetraRoam_searching(&r));
        REQUIRE(r.serving == C);

        // And stays there
        radio.run(r, 10000);
        REQUIRE(radio.tunes.size() == 1);
    }

    SECTION("the serving cell is about as strong: stay")
    {
        radio.rssi = { { A, -80 }, { B, -90 },
                       { C, -80 + TETRA_ROAM_SEARCH_MARGIN_DB - 1 } };
        tetraRoam_search(&r, radio.now);
        radio.run(r, 1000);
        REQUIRE(radio.probes.size() == 4);
        REQUIRE(radio.tunes.empty());
        REQUIRE_FALSE(tetraRoam_searching(&r));
        REQUIRE(r.serving == A);
    }

    SECTION("the serving cell is the strongest: stay")
    {
        radio.rssi = { { A, -60 }, { B, -90 }, { C, -70 } };
        tetraRoam_search(&r, radio.now);
        radio.run(r, 1000);
        REQUIRE(radio.tunes.empty());
        REQUIRE_FALSE(tetraRoam_searching(&r));
    }

    SECTION("the strongest does not lock: the next one is tried")
    {
        radio.rssi = { { A, -90 }, { B, -75 }, { C, -70 } };
        radio.lockable = { A, B };
        tetraRoam_search(&r, radio.now);
        radio.run(r, 500);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ C }));

        radio.run(r, TETRA_ROAM_TRY_MS + 1000);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ C, B }));
        REQUIRE(radio.locked());
        REQUIRE(r.serving == B);
        REQUIRE_FALSE(tetraRoam_searching(&r));
    }

    SECTION("asked twice: one search")
    {
        radio.rssi = { { A, -80 }, { C, -70 } };
        tetraRoam_search(&r, radio.now);
        radio.run(r, 60);
        tetraRoam_search(&r, radio.now);
        radio.run(r, 500);
        REQUIRE(radio.probes == std::vector<uint32_t>({ B, C, D, A }));
    }

    SECTION("asked while lost: searches at once")
    {
        radio.lockable = { C };
        radio.rssi = { { C, -90 } };
        radio.run(r, 500);
        REQUIRE_FALSE(tetraRoam_searching(&r));

        tetraRoam_search(&r, radio.now);
        radio.run(r, 1000);
        REQUIRE(radio.tunes == std::vector<uint32_t>({ C }));
        REQUIRE(radio.locked());
    }
}

TEST_CASE("TETRA roaming moves from a secondary carrier to the main one",
          "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, E);

    tetraRoamStep s = tetraRoam_mainCarrier(&r, A, 1000);
    REQUIRE(s.action == TETRA_ROAM_TUNE);
    REQUIRE(s.freq == A);
    REQUIRE(r.serving == A);

    // Already there, or searching: nothing
    REQUIRE(tetraRoam_mainCarrier(&r, A, 2000).action == TETRA_ROAM_NONE);
    tetraRoam_search(&r, 3000);
    REQUIRE(tetraRoam_mainCarrier(&r, B, 3000).action == TETRA_ROAM_NONE);
}

TEST_CASE("TETRA roaming is not lost while following a call elsewhere",
          "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, A);
    radio.rssi = { { A, -70 } };
    radio.run(r, 1000);

    // Away 20 s: the tracker is not run, only held
    for (long long t = 1000; t < 21000; t += 30)
        tetraRoam_hold(&r, t);
    radio.now = 21000;

    // Back, the receiver resyncs for a moment: no search
    radio.lockable.clear();
    radio.run(r, 300);
    REQUIRE(r.state == TETRA_ROAM_LOCKED);
    REQUIRE(radio.probes.empty());
}

TEST_CASE("TETRA roaming adds up the neighbours a cell names in parts",
          "[tetra][roam]")
{
    tetraRoam r;
    Radio radio = start(r, A);

    uint32_t part1[] = { B, C };
    uint32_t part2[] = { C, D };
    tetraRoam_setNeighbours(&r, part1, 2);
    tetraRoam_setNeighbours(&r, part2, 2);
    REQUIRE(r.nNeighbours == 3);

    // Full: the oldest go
    for (uint32_t f = 1; f <= TETRA_ROAM_MAX_NEIGHBOURS; f++)
        tetraRoam_setNeighbours(&r, &f, 1);
    REQUIRE(r.nNeighbours == TETRA_ROAM_MAX_NEIGHBOURS);
    REQUIRE(r.neighbour[0] == 1);

    // Another cell: its own neighbours
    tetraRoam_mainCarrier(&r, E, 1000);
    REQUIRE(r.nNeighbours == 0);
}
