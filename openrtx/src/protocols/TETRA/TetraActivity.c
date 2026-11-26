/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/TETRA/TetraActivity.h"
#include <string.h>

#define HOUR_MS 3600000LL

void tetraActivity_init(struct tetraActivity *a, long long now)
{
    memset(a, 0, sizeof(*a));
    a->since = now;
}

/** The hour slot of a time, emptied if it held an older hour */
static unsigned hourSlot(struct tetraActivity *a, long long t)
{
    uint32_t hour = (uint32_t)(t / HOUR_MS);
    unsigned slot = hour % TETRA_ACTIVITY_HOURS;

    if (a->slotHour[slot] != hour + 1) {
        for (uint8_t i = 0; i < a->count; i++)
            a->entry[i].hourSec[slot] = 0;
        a->slotHour[slot] = hour + 1;
    }
    return slot;
}

/** The entry of a talkgroup or radio, made if new, NULL for dst 0 */
static struct tetraActivityEntry *entryOf(struct tetraActivity *a, uint32_t dst,
                                          bool group, long long now)
{
    struct tetraActivityEntry *e;

    if (dst == 0)
        return NULL;
    for (uint8_t i = 0; i < a->count; i++)
        if (a->entry[i].dst == dst)
            return &a->entry[i];

    if (a->count < TETRA_ACTIVITY_MAX) {
        e = &a->entry[a->count++];
    } else {
        /* Full: the least talk time goes, the oldest of those */
        e = &a->entry[0];
        for (uint8_t i = 1; i < a->count; i++) {
            struct tetraActivityEntry *c = &a->entry[i];
            if (c->talkMs < e->talkMs
                || (c->talkMs == e->talkMs && c->last < e->last))
                e = c;
        }
    }
    memset(e, 0, sizeof(*e));
    e->dst = dst;
    e->group = group;
    e->first = now;
    return e;
}

/** End the transmission of a call at a time, adding it up */
static void endTalk(struct tetraActivity *a, struct tetraActivityOpen *o,
                    long long end)
{
    if (!o->talking)
        return;
    o->talking = false;

    struct tetraActivityEntry *e = entryOf(a, o->dst, o->group, end);
    if (e == NULL || end <= o->start)
        return;

    uint32_t ms = (uint32_t)(end - o->start);
    e->talkMs += ms;
    unsigned slot = hourSlot(a, end);
    uint32_t sec = e->hourSec[slot] + (ms + 500) / 1000;
    e->hourSec[slot] = (sec > 3600) ? 3600 : (uint16_t)sec;
}

static void startTalk(struct tetraActivity *a, struct tetraActivityOpen *o,
                      long long now)
{
    struct tetraActivityEntry *e;

    if (o->talking)
        return;
    o->talking = true;
    o->start = now;
    e = entryOf(a, o->dst, o->group, now);
    if (e != NULL)
        e->talks++;
}

/** The call followed with this identifier, a free slot, or NULL */
static struct tetraActivityOpen *
openOf(struct tetraActivity *a, uint16_t callId, bool make, long long now)
{
    struct tetraActivityOpen *oldest = NULL;

    for (unsigned i = 0; i < TETRA_ACTIVITY_OPEN; i++)
        if (a->open[i].used && a->open[i].callId == callId)
            return &a->open[i];
    if (!make)
        return NULL;

    for (unsigned i = 0; i < TETRA_ACTIVITY_OPEN; i++) {
        struct tetraActivityOpen *o = &a->open[i];
        if (!o->used) {
            oldest = o;
            break;
        }
        if (oldest == NULL || o->seen < oldest->seen)
            oldest = o;
    }
    if (oldest->used)
        endTalk(a, oldest, oldest->seen);
    memset(oldest, 0, sizeof(*oldest));
    oldest->used = true;
    oldest->callId = callId;
    oldest->seen = now;
    return oldest;
}

void tetraActivity_update(struct tetraActivity *a, long long now)
{
    for (unsigned i = 0; i < TETRA_ACTIVITY_OPEN; i++) {
        struct tetraActivityOpen *o = &a->open[i];
        if (o->used && now - o->seen >= TETRA_ACTIVITY_IDLE_MS) {
            endTalk(a, o, o->seen);
            o->used = false;
        }
    }
}

void tetraActivity_note(struct tetraActivity *a, uint8_t what, uint16_t callId,
                        uint32_t dst, bool group, long long now)
{
    struct tetraActivityOpen *o;
    struct tetraActivityEntry *e;

    tetraActivity_update(a, now);

    o = openOf(a, callId, what != TETRA_ACTIVITY_RELEASE, now);
    if (o == NULL)
        return;

    /* The called party becomes known: a call for it */
    if (o->dst == 0 && dst != 0) {
        o->dst = dst;
        o->group = group;
        e = entryOf(a, dst, group, now);
        e->calls++;
        if (o->talking)
            e->talks++;
    }
    o->seen = now;
    e = entryOf(a, o->dst, o->group, now);
    if (e != NULL)
        e->last = now;

    switch (what) {
        case TETRA_ACTIVITY_SETUP:
        case TETRA_ACTIVITY_TALK:
            startTalk(a, o, now);
            break;
        case TETRA_ACTIVITY_CEASE:
            endTalk(a, o, now);
            break;
        case TETRA_ACTIVITY_RELEASE:
            endTalk(a, o, now);
            o->used = false;
            break;
        default:
            break;
    }
}

/** True if entry x comes before entry y */
static bool busier(const struct tetraActivityEntry *x,
                   const struct tetraActivityEntry *y)
{
    if (x->talkMs != y->talkMs)
        return x->talkMs > y->talkMs;
    return x->last > y->last;
}

bool tetraActivity_get(const struct tetraActivity *a, uint8_t n, long long now,
                       struct tetraActivityEntry *out)
{
    const struct tetraActivityEntry *pick = NULL;

    if (n >= a->count)
        return false;

    /* The entry with exactly n busier than it */
    for (uint8_t i = 0; i < a->count && pick == NULL; i++) {
        uint8_t ahead = 0;
        for (uint8_t j = 0; j < a->count; j++)
            if (j != i && busier(&a->entry[j], &a->entry[i]))
                ahead++;
        if (ahead == n)
            pick = &a->entry[i];
    }
    if (pick == NULL)
        return false;
    *out = *pick;

    /* The transmission going on */
    for (unsigned i = 0; i < TETRA_ACTIVITY_OPEN; i++) {
        const struct tetraActivityOpen *o = &a->open[i];
        if (o->used && o->talking && o->dst == out->dst
            && now - o->seen < TETRA_ACTIVITY_IDLE_MS) {
            out->talking = true;
            if (now > o->start)
                out->talkMs += (uint32_t)(now - o->start);
        }
    }

    /* Hours, the current one first; slots of hours gone are empty */
    uint32_t hour = (uint32_t)(now / HOUR_MS);
    for (unsigned k = 0; k < TETRA_ACTIVITY_HOURS; k++) {
        uint32_t h = hour - k;
        unsigned slot = h % TETRA_ACTIVITY_HOURS;
        out->hourSec[k] = (k <= hour && a->slotHour[slot] == h + 1) ?
                              pick->hourSec[slot] :
                              0;
    }
    return true;
}
