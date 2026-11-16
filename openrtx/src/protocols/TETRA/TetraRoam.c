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
    r->nNeighbours = 0;
    for (unsigned i = 0; i < n && r->nNeighbours < TETRA_ROAM_MAX_NEIGHBOURS;
         i++) {
        if (freq[i] == 0 || freq[i] == r->serving
            || contains(r->neighbour, r->nNeighbours, freq[i]))
            continue;
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
    return step(TETRA_ROAM_TUNE, freq);
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

    if (best == NULL || best->rssi < r->servingRssi + TETRA_ROAM_MARGIN_DB) {
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

    if (r->confirmed < 2)
        return step(TETRA_ROAM_NONE, 0);

    r->confirmed = 0;
    r->best = 0;
    r->lastLock = now;
    return tune(r, best->freq);
}

/** Measuring the neighbours of a weak cell: next probe, or the verdict */
static struct tetraRoamStep checkStep(struct tetraRoam *r, long long now,
                                      bool locked, bool busy)
{
    if (!locked || busy) {
        r->state = TETRA_ROAM_LOCKED;
        return step(TETRA_ROAM_NONE, 0);
    }
    if (r->next < r->nCand)
        return step(TETRA_ROAM_PROBE, r->cand[r->next].freq);
    return checkDone(r, now);
}

struct tetraRoamStep tetraRoam_tick(struct tetraRoam *r, long long now,
                                    bool locked, bool busy, int rssi)
{
    if (locked)
        r->lastLock = now;

    switch (r->state) {
        case TETRA_ROAM_LOCKED:
            if (!locked) {
                if (now - r->lastLock >= TETRA_ROAM_LOST_MS)
                    startSweep(r, now);
                break;
            }

            if (busy || r->nNeighbours == 0
                || now - r->lastCheck < TETRA_ROAM_CHECK_MS)
                break;

            r->lastCheck = now;
            if (rssi >= TETRA_ROAM_WEAK_DBM) {
                r->confirmed = 0;
                break;
            }

            /* Weak: measure the neighbours */
            r->nCand = 0;
            for (unsigned i = 0; i < r->nNeighbours; i++) {
                if (r->neighbour[i] != r->serving)
                    addCandidate(r, r->neighbour[i]);
            }
            r->servingRssi = (int16_t)rssi;
            r->next = 0;
            r->state = TETRA_ROAM_CHECK;
            return checkStep(r, now, locked, busy);

        case TETRA_ROAM_CHECK:
            return checkStep(r, now, locked, busy);

        case TETRA_ROAM_SWEEP: {
            if (locked) {
                r->state = TETRA_ROAM_LOCKED;
                break;
            }
            if (r->next < r->nCand)
                return step(TETRA_ROAM_PROBE, r->cand[r->next].freq);

            sortCandidates(r);
            unsigned usable = 0;
            while (usable < r->nCand
                   && r->cand[usable].rssi > TETRA_ROAM_FLOOR_DBM)
                usable++;

            if (usable == 0) {
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
                r->state = TETRA_ROAM_LOCKED;
                r->lastCheck = now;
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

bool tetraRoam_searching(const struct tetraRoam *r)
{
    return r->state == TETRA_ROAM_SWEEP || r->state == TETRA_ROAM_TRY
        || r->state == TETRA_ROAM_PAUSE;
}
