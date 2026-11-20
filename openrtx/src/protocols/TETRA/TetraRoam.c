/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/TETRA/TetraRoam.h"

#define NOT_MEASURED (-32768)

void tetraRoam_init(struct tetraRoam *r, uint32_t serving, long long now)
{
    memset(r, 0, sizeof(*r));
    r->serving = serving;
    r->state = TETRA_ROAM_LOCKED;
    r->since = now;
    r->lastLock = now;
    r->lastCheck = now;
}

static bool contains(const uint32_t *list, unsigned n, uint32_t freq)
{
    for (unsigned i = 0; i < n; i++) {
        if (list[i] == freq)
            return true;
    }

    return false;
}

void tetraRoam_addScan(struct tetraRoam *r, uint32_t freq)
{
    if (freq == 0 || r->nScan >= TETRA_ROAM_MAX_SCAN
        || contains(r->scan, r->nScan, freq))
        return;

    r->scan[r->nScan++] = freq;
}

void tetraRoam_setNeighbours(struct tetraRoam *r, const uint32_t *freq,
                             unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (freq[i] == 0 || freq[i] == r->serving
            || contains(r->neighbour, r->nNeighbours, freq[i]))
            continue;
        if (r->nNeighbours == TETRA_ROAM_MAX_NEIGHBOURS) {
            /* Full: the oldest makes room */
            memmove(&r->neighbour[0], &r->neighbour[1],
                    (TETRA_ROAM_MAX_NEIGHBOURS - 1) * sizeof(r->neighbour[0]));
            r->nNeighbours--;
        }
        r->neighbour[r->nNeighbours++] = freq[i];
    }
}

static void addCandidate(struct tetraRoam *r, uint32_t freq)
{
    for (unsigned i = 0; i < r->nCand; i++) {
        if (r->cand[i].freq == freq)
            return;
    }

    if (freq == 0 || r->nCand >= TETRA_ROAM_MAX_CANDIDATES)
        return;

    r->cand[r->nCand].freq = freq;
    r->cand[r->nCand].rssi = NOT_MEASURED;
    r->nCand++;
}

/** Lost: measure the neighbours, then the other known carriers. */
static void startSweep(struct tetraRoam *r, long long now)
{
    r->nCand = 0;
    for (unsigned i = 0; i < r->nNeighbours; i++)
        addCandidate(r, r->neighbour[i]);
    for (unsigned i = 0; i < r->nScan; i++)
        addCandidate(r, r->scan[i]);
    addCandidate(r, r->serving);

    r->next = 0;
    r->state = TETRA_ROAM_SWEEP;
    r->since = now;
    r->requested = false;
}

void tetraRoam_search(struct tetraRoam *r, long long now)
{
    if (r->requested)
        return;

    startSweep(r, now);
    r->requested = true;
}

static struct tetraRoamStep step(enum tetraRoamAction action, uint32_t freq)
{
    struct tetraRoamStep s;

    s.action = action;
    s.freq = freq;
    return s;
}

static struct tetraRoamStep tune(struct tetraRoam *r, uint32_t freq)
{
    r->serving = freq;
    r->averaged = false; /* Another cell: its own signal and neighbours */
    r->nNeighbours = 0;
    return step(TETRA_ROAM_TUNE, freq);
}

/** Average of the serving cell's signal, dBm */
static int average(struct tetraRoam *r, long long now, int rssi)
{
    if (!r->averaged) {
        r->average = rssi * 16;
        r->averaged = true;
    } else {
        long long dt = now - r->lastAverage;
        if (dt > TETRA_ROAM_AVERAGE_MS)
            dt = TETRA_ROAM_AVERAGE_MS;
        if (dt > 0)
            r->average += (int32_t)((rssi * 16 - r->average) * dt
                                    / TETRA_ROAM_AVERAGE_MS);
    }
    r->lastAverage = now;
    return r->average / 16;
}

/** Strongest candidates first */
static void sortCandidates(struct tetraRoam *r)
{
    for (unsigned i = 1; i < r->nCand; i++) {
        struct tetraRoamCandidate c = r->cand[i];
        unsigned j = i;
        while (j > 0 && r->cand[j - 1].rssi < c.rssi) {
            r->cand[j] = r->cand[j - 1];
            j--;
        }
        r->cand[j] = c;
    }
}

static struct tetraRoamStep checkDone(struct tetraRoam *r, long long now)
{
    const struct tetraRoamCandidate *best = NULL;

    for (unsigned i = 0; i < r->nCand; i++) {
        if (best == NULL || r->cand[i].rssi > best->rssi)
            best = &r->cand[i];
    }

    r->state = TETRA_ROAM_LOCKED;
    r->lastCheck = now;

    if (best == NULL || best->rssi < r->servingRssi + TETRA_ROAM_MARGIN_DB
        || best->rssi <= TETRA_ROAM_FLOOR_DBM) {
        r->confirmed = 0;
        return step(TETRA_ROAM_NONE, 0);
    }

    /* Stronger twice in a row: move, and give the new cell time to lock */
    if (best->freq == r->best) {
        r->confirmed++;
    } else {
        r->best = best->freq;
        r->confirmed = 1;
    }

    /* About to lose the cell: no time to confirm */
    if (r->confirmed < (r->urgent ? 1 : 2))
        return step(TETRA_ROAM_NONE, 0);

    r->confirmed = 0;
    r->best = 0;
    r->lastLock = now;
    return tune(r, best->freq);
}

/**
 * A requested search while locked, candidates sorted: is the serving cell
 * about as strong as the best one?
 */
static bool keepServing(const struct tetraRoam *r)
{
    int serving = NOT_MEASURED;

    if (r->nCand == 0 || r->cand[0].freq == r->serving)
        return true;

    for (unsigned i = 0; i < r->nCand; i++) {
        if (r->cand[i].freq == r->serving)
            serving = r->cand[i].rssi;
    }

    return serving != NOT_MEASURED
        && r->cand[0].rssi < serving + TETRA_ROAM_SEARCH_MARGIN_DB;
}

/** Back on the cell, the search over */
static void settle(struct tetraRoam *r, long long now)
{
    r->state = TETRA_ROAM_LOCKED;
    r->requested = false;
    r->confirmed = 0;
    r->lastCheck = now;
}

/** Measuring the neighbours of a weak cell: next probe, or the verdict */
static struct tetraRoamStep checkStep(struct tetraRoam *r, long long now,
                                      bool locked, bool busy)
{
    /*
     * The lock lost during a check: the check goes on, as one of a cell
     * about to be lost, which is worth nothing now; its result moves the
     * radio sooner than the search after a lost lock would
     */
    if (!locked && !r->urgent) {
        r->urgent = true;
        r->servingRssi = TETRA_ROAM_FLOOR_DBM - TETRA_ROAM_MARGIN_DB;
    }
    if (busy && !r->urgent) {
        r->state = TETRA_ROAM_LOCKED;
        return step(TETRA_ROAM_NONE, 0);
    }
    if (r->next < r->nCand)
        return step(TETRA_ROAM_PROBE, r->cand[r->next].freq);
    return checkDone(r, now);
}

struct tetraRoamStep tetraRoam_tick(struct tetraRoam *r, long long now,
                                    bool locked, bool decoding, bool busy,
                                    int rssi)
{
    /* The serving cell's signal, and whether it is about to be lost */
    int level = 0;
    bool urgent = false;
    if (locked) {
        r->lastLock = now;
        level = average(r, now, rssi);
        urgent = level < TETRA_ROAM_CRITICAL_DBM || !decoding;
    }

    switch (r->state) {
        case TETRA_ROAM_LOCKED: {
            if (!locked) {
                if (now - r->lastLock >= TETRA_ROAM_LOST_MS)
                    startSweep(r, now);
                break;
            }

            /*
             * About to be lost, and the last check found a stronger
             * neighbour: no time to measure again
             */
            if (urgent && r->confirmed > 0 && r->best != 0) {
                uint32_t to = r->best;
                r->confirmed = 0;
                r->best = 0;
                r->lastCheck = now;
                return tune(r, to);
            }

            /* Checked more often when the cell is about to be lost */
            long long every = urgent ? TETRA_ROAM_CHECK_MS / 3
                                     : TETRA_ROAM_CHECK_MS;
            if ((busy && !urgent) || r->nNeighbours == 0
                || now - r->lastCheck < every)
                break;

            r->lastCheck = now;
            if (level >= TETRA_ROAM_WEAK_DBM && decoding) {
                r->confirmed = 0;
                break;
            }

            /* Weak: measure the neighbours */
            r->nCand = 0;
            for (unsigned i = 0; i < r->nNeighbours; i++) {
                if (r->neighbour[i] != r->serving)
                    addCandidate(r, r->neighbour[i]);
            }
            /*
             * A cell that does not decode, however strong: a neighbour as
             * strong will do, no margin needed
             */
            r->servingRssi = (int16_t)(decoding ? level
                                                : level - TETRA_ROAM_MARGIN_DB);
            r->urgent = urgent;
            r->next = 0;
            r->state = TETRA_ROAM_CHECK;
            return checkStep(r, now, locked, busy);
        }

        case TETRA_ROAM_CHECK:
            /* Worse during the check: decide as for a cell about to go */
            if (urgent && !r->urgent) {
                r->urgent = true;
                if (!decoding && r->servingRssi > level - TETRA_ROAM_MARGIN_DB)
                    r->servingRssi = (int16_t)(level - TETRA_ROAM_MARGIN_DB);
            }
            return checkStep(r, now, locked, busy);

        case TETRA_ROAM_SWEEP: {
            /* A lost lock that returns ends the search, a requested one not */
            if (locked && !r->requested) {
                r->state = TETRA_ROAM_LOCKED;
                break;
            }
            if (r->next < r->nCand)
                return step(TETRA_ROAM_PROBE, r->cand[r->next].freq);

            sortCandidates(r);
            if (locked && keepServing(r)) {
                settle(r, now);
                break;
            }

            unsigned usable = 0;
            while (usable < r->nCand
                   && r->cand[usable].rssi > TETRA_ROAM_FLOOR_DBM)
                usable++;

            if (usable == 0) {
                if (locked) {
                    settle(r, now);
                    break;
                }
                r->state = TETRA_ROAM_PAUSE;
                r->since = now;
                break;
            }

            r->nCand = (uint8_t)usable;
            r->next = 0;
            r->state = TETRA_ROAM_TRY;
            r->since = now;
            return tune(r, r->cand[0].freq);
        }

        case TETRA_ROAM_TRY:
            if (locked) {
                settle(r, now);
                break;
            }
            if (now - r->since < TETRA_ROAM_TRY_MS)
                break;

            r->next++;
            if (r->next >= r->nCand) {
                startSweep(r, now);
                break;
            }
            r->since = now;
            return tune(r, r->cand[r->next].freq);

        case TETRA_ROAM_PAUSE:
            if (locked)
                r->state = TETRA_ROAM_LOCKED;
            else if (now - r->since >= TETRA_ROAM_PAUSE_MS)
                startSweep(r, now);
            break;
    }

    return step(TETRA_ROAM_NONE, 0);
}

void tetraRoam_probed(struct tetraRoam *r, uint32_t freq, int rssi)
{
    if (r->state != TETRA_ROAM_CHECK && r->state != TETRA_ROAM_SWEEP)
        return;
    if (r->next >= r->nCand || r->cand[r->next].freq != freq)
        return;

    r->cand[r->next].rssi = (int16_t)rssi;
    r->next++;
}

void tetraRoam_hold(struct tetraRoam *r, long long now)
{
    r->lastLock = now;
    r->lastCheck = now;
}

struct tetraRoamStep tetraRoam_mainCarrier(struct tetraRoam *r, uint32_t main,
                                           long long now)
{
    if (r->state != TETRA_ROAM_LOCKED || main == 0 || main == r->serving)
        return step(TETRA_ROAM_NONE, 0);

    r->lastLock = now;
    r->lastCheck = now;
    r->confirmed = 0;
    return tune(r, main);
}

bool tetraRoam_searching(const struct tetraRoam *r)
{
    return r->state == TETRA_ROAM_SWEEP || r->state == TETRA_ROAM_TRY
        || r->state == TETRA_ROAM_PAUSE;
}
