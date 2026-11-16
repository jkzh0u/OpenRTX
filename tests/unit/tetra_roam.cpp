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
            tetraRoamStep s = tetraRoam_tick(&r, now, locked(), busy,
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
        radio.rssi = { { A, -100 }, { B, -85 }, { C, -98 } };
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
        radio.rssi = { { A, -100 }, { B, -96 }, { C, -98 } };
        radio.run(r, TETRA_ROAM_CHECK_MS * 4);
        REQUIRE_FALSE(radio.probes.empty());
        REQUIRE(radio.tunes.empty());
    }

    SECTION("not while a call is received")
    {
        radio.rssi = { { A, -100 }, { B, -85 } };
        radio.busy = true;
        radio.run(r, TETRA_ROAM_CHECK_MS * 4);
        REQUIRE(radio.probes.empty());
        REQUIRE(radio.tunes.empty());
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
