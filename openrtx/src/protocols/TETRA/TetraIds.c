/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include "protocols/TETRA/TetraIds.h"

/*
 * The list is read five times: filter numbers, filter names, identities,
 * location areas and identity ranges
 */
#define CPS_CHANNEL(...)

enum {
    TF_ALL_ = 0,
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(id, name) TF_##id,
#define TETRA_ID(...)
#define TETRA_LA(...)
#define TETRA_ID_RANGE(...)
#include TETRA_ID_LIST
#undef TETRA_ID_RANGE
#undef TETRA_LA
#undef TETRA_ID
#undef TETRA_FILTER
#endif
    TF_COUNT_
};

_Static_assert(TF_COUNT_ - 1 <= TETRA_IDS_MAX_FILTERS, "Too many filters");

/*
 * For the list: filter masks, bit n for filter n. Bit 0, "All", is not
 * needed as a filter and marks alarms.
 */
#define TF(id) (1ul << TF_##id)
#define TF_ALARM 1ul
#define TF_EVERY (0x7ffffffeul)

/* The filter number of a held talkgroup */
#define HOLD TF_COUNT_

static const char *const filterNames[] = {
    "All",
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(id, name) (name),
#define TETRA_ID(...)
#define TETRA_LA(...)
#define TETRA_ID_RANGE(...)
#include TETRA_ID_LIST
#undef TETRA_ID_RANGE
#undef TETRA_LA
#undef TETRA_ID
#undef TETRA_FILTER
#endif
};

struct tetraId {
    uint32_t ssi;
    const char *name;
    uint32_t filters;
};

/* The sentinel keeps the array valid when the list is empty */
static const struct tetraId ids[] = {
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(...)
#define TETRA_ID(ssi, name, filters) { (ssi), (name), (uint32_t)(filters) },
#define TETRA_LA(...)
#define TETRA_ID_RANGE(...)
#include TETRA_ID_LIST
#undef TETRA_ID_RANGE
#undef TETRA_LA
#undef TETRA_ID
#undef TETRA_FILTER
#endif
    { 0, NULL, 0 },
};

struct tetraLa {
    uint16_t la;
    const char *name;
};

static const struct tetraLa las[] = {
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(...)
#define TETRA_ID(...)
#define TETRA_LA(la, name) { (la), (name) },
#define TETRA_ID_RANGE(...)
#include TETRA_ID_LIST
#undef TETRA_ID_RANGE
#undef TETRA_LA
#undef TETRA_ID
#undef TETRA_FILTER
#endif
    { 0, NULL },
};

struct tetraIdRange {
    uint32_t first;
    uint32_t last;
    const char *format; /**< printf format of the SSI minus base */
    uint32_t base;
    uint32_t filters;
};

static const struct tetraIdRange ranges[] = {
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(...)
#define TETRA_ID(...)
#define TETRA_LA(...)
#define TETRA_ID_RANGE(first, last, format, base, filters) \
    { (first), (last), (format), (base), (uint32_t)(filters) },
#include TETRA_ID_LIST
#undef TETRA_ID_RANGE
#undef TETRA_LA
#undef TETRA_ID
#undef TETRA_FILTER
#endif
    { 0, 0, NULL, 0, 0 },
};

#undef CPS_CHANNEL

#define NUM_IDS (sizeof(ids) / sizeof(ids[0]) - 1)
#define NUM_LAS (sizeof(las) / sizeof(las[0]) - 1)
#define NUM_RANGES (sizeof(ranges) / sizeof(ranges[0]) - 1)

/*
 * Written by the UI, read by the receiver. The selection number changes
 * last, after the values it announces.
 */
static volatile uint8_t selected;
static volatile uint8_t beforeHold;
static volatile uint32_t heldSsi;
static volatile uint8_t selection;

static const struct tetraId *find(uint32_t ssi)
{
    if (ssi == 0)
        return NULL;

    for (size_t i = 0; i < NUM_IDS; i++) {
        if (ids[i].ssi == ssi)
            return &ids[i];
    }

    return NULL;
}

const char *tetraIds_name(uint32_t ssi)
{
    const struct tetraId *id = find(ssi);

    return id != NULL ? id->name : NULL;
}

static const struct tetraIdRange *findRange(uint32_t ssi)
{
    if (ssi == 0)
        return NULL;

    for (size_t i = 0; i < NUM_RANGES; i++) {
        if (ssi >= ranges[i].first && ssi <= ranges[i].last)
            return &ranges[i];
    }

    return NULL;
}

bool tetraIds_nameOf(uint32_t ssi, char *buf, size_t len)
{
    const struct tetraId *id = find(ssi);

    if (id != NULL) {
        snprintf(buf, len, "%s", id->name);
        return true;
    }

    const struct tetraIdRange *r = findRange(ssi);
    if (r != NULL) {
        snprintf(buf, len, r->format, (unsigned)(ssi - r->base));
        return true;
    }

    return false;
}

/** Filters an identity belongs to, from its entry or its range */
static uint32_t filtersOf(uint32_t ssi)
{
    const struct tetraId *id = find(ssi);

    if (id != NULL)
        return id->filters;

    const struct tetraIdRange *r = findRange(ssi);
    return r != NULL ? r->filters : 0;
}

size_t tetraIds_filterCount(void)
{
    return TF_COUNT_;
}

const char *tetraIds_filterName(size_t filter)
{
    if (filter == HOLD)
        return "Hold";
    return filter < TF_COUNT_ ? filterNames[filter] : NULL;
}

bool tetraIds_inFilter(uint32_t ssi, size_t filter)
{
    if (filter == 0)
        return true;
    if (filter == HOLD)
        return ssi != 0 && ssi == heldSsi;
    if (filter > TF_COUNT_)
        return false;

    return (filtersOf(ssi) & (1ul << filter)) != 0;
}

bool tetraIds_isAlarm(uint32_t ssi)
{
    return (filtersOf(ssi) & TF_ALARM) != 0;
}

const char *tetraIds_laName(uint16_t la)
{
    for (size_t i = 0; i < NUM_LAS; i++) {
        if (las[i].la == la)
            return las[i].name;
    }

    return NULL;
}

void tetraIds_stepFilter(int step)
{
    int from = (selected == HOLD) ? beforeHold : selected;
    int next = (from + step) % (int)TF_COUNT_;

    if (next < 0)
        next += TF_COUNT_;
    heldSsi = 0;
    selected = (uint8_t)next;
    selection++;
}

void tetraIds_hold(uint32_t ssi)
{
    if (ssi == 0)
        return;
    if (selected != HOLD)
        beforeHold = selected;
    heldSsi = ssi;
    selected = HOLD;
    selection++;
}

void tetraIds_release(void)
{
    if (selected != HOLD)
        return;
    heldSsi = 0;
    selected = beforeHold;
    selection++;
}

uint32_t tetraIds_held(void)
{
    return heldSsi;
}

size_t tetraIds_selectedFilter(void)
{
    return selected;
}

uint8_t tetraIds_selection(void)
{
    return selection;
}
