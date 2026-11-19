/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include "protocols/TETRA/TetraIds.h"

/*
 * The list comes from the header TETRA_ID_LIST, built in, or with
 * CONFIG_CHLIST from the channel list image (core/chlist.h), read when
 * needed: an image opened after start is used straight away.
 */
#ifdef CONFIG_CHLIST
#include "core/chlist.h"
#undef TETRA_ID_LIST
#endif

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
#define TF_PRIORITY 0x80000000ul
#define TF_EVERY (0x7ffffffeul)

#ifndef CONFIG_CHLIST
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
#endif

struct tetraId {
    uint32_t ssi;
    const char *name;
    uint32_t filters;
};

#ifndef CONFIG_CHLIST
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
#endif

struct tetraLa {
    uint16_t la;
    const char *name;
};

#ifndef CONFIG_CHLIST
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
#endif

struct tetraIdRange {
    uint32_t first;
    uint32_t last;
    const char *format; /**< printf format of the SSI minus base */
    uint32_t base;
    uint32_t filters;
};

#ifndef CONFIG_CHLIST
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
#endif

#undef CPS_CHANNEL

#ifdef CONFIG_CHLIST

static size_t numFilters(void)
{
    return chlist_filterCount() + 1;
}

static const char *filterName(size_t i)
{
    return i == 0 ? "All" : chlist_filterName(i - 1);
}

static size_t numIds(void)
{
    return chlist_idCount();
}

static struct tetraId idAt(size_t i)
{
    struct chlistId c = { 0, NULL, 0 };
    chlist_id(i, &c);
    return (struct tetraId){ c.ssi, c.name, c.filters };
}

static size_t numLas(void)
{
    return chlist_laCount();
}

static struct tetraLa laAt(size_t i)
{
    struct chlistLa c = { 0, NULL };
    chlist_la(i, &c);
    return (struct tetraLa){ c.la, c.name };
}

static size_t numRanges(void)
{
    return chlist_rangeCount();
}

static struct tetraIdRange rangeAt(size_t i)
{
    struct chlistRange c = { 0, 0, NULL, 0, 0 };
    chlist_range(i, &c);
    return (struct tetraIdRange){ c.first, c.last, c.format, c.base,
                                  c.filters };
}

#else

static size_t numFilters(void)
{
    return TF_COUNT_;
}

static const char *filterName(size_t i)
{
    return filterNames[i];
}

static size_t numIds(void)
{
    return sizeof(ids) / sizeof(ids[0]) - 1;
}

static struct tetraId idAt(size_t i)
{
    return ids[i];
}

static size_t numLas(void)
{
    return sizeof(las) / sizeof(las[0]) - 1;
}

static struct tetraLa laAt(size_t i)
{
    return las[i];
}

static size_t numRanges(void)
{
    return sizeof(ranges) / sizeof(ranges[0]) - 1;
}

static struct tetraIdRange rangeAt(size_t i)
{
    return ranges[i];
}

#endif

/* The filter number of a held talkgroup: after the last filter */
#define HOLD numFilters()

/*
 * Written by the UI, read by the receiver. The selection number changes
 * last, after the values it announces.
 */
static volatile uint8_t selected;
static volatile uint8_t beforeHold;
static volatile uint32_t heldSsi;
static volatile uint8_t selection;

/** The entry of an identity: false if it has none */
static bool find(uint32_t ssi, struct tetraId *out)
{
    if (ssi == 0)
        return false;

    for (size_t i = 0; i < numIds(); i++) {
        *out = idAt(i);
        if (out->ssi == ssi)
            return true;
    }

    return false;
}

const char *tetraIds_name(uint32_t ssi)
{
    struct tetraId id;

    return find(ssi, &id) ? id.name : NULL;
}

/** The range of an identity: false if in none */
static bool findRange(uint32_t ssi, struct tetraIdRange *out)
{
    if (ssi == 0)
        return false;

    for (size_t i = 0; i < numRanges(); i++) {
        *out = rangeAt(i);
        if (ssi >= out->first && ssi <= out->last)
            return true;
    }

    return false;
}

bool tetraIds_nameOf(uint32_t ssi, char *buf, size_t len)
{
    struct tetraId id;
    struct tetraIdRange r;

    if (find(ssi, &id)) {
        snprintf(buf, len, "%s", id.name);
        return true;
    }

    if (findRange(ssi, &r)) {
        snprintf(buf, len, r.format, (unsigned)(ssi - r.base));
        return true;
    }

    return false;
}

/** Filters an identity belongs to, from its entry or its range */
static uint32_t filtersOf(uint32_t ssi)
{
    struct tetraId id;
    struct tetraIdRange r;

    if (find(ssi, &id))
        return id.filters;

    return findRange(ssi, &r) ? r.filters : 0;
}

size_t tetraIds_filterCount(void)
{
    return numFilters();
}

const char *tetraIds_filterName(size_t filter)
{
    if (filter == HOLD)
        return "Hold";
    return filter < numFilters() ? filterName(filter) : NULL;
}

bool tetraIds_inFilter(uint32_t ssi, size_t filter)
{
    if (filter == 0)
        return true;
    if (filter == HOLD)
        return ssi != 0 && ssi == heldSsi;
    if (filter > numFilters())
        return false;

    return (filtersOf(ssi) & (1ul << filter)) != 0;
}

bool tetraIds_isAlarm(uint32_t ssi)
{
    return (filtersOf(ssi) & TF_ALARM) != 0;
}

bool tetraIds_isPriority(uint32_t ssi)
{
    return (filtersOf(ssi) & (TF_PRIORITY | TF_ALARM)) != 0;
}

const char *tetraIds_laName(uint16_t la)
{
    for (size_t i = 0; i < numLas(); i++) {
        struct tetraLa l = laAt(i);
        if (l.la == la)
            return l.name;
    }

    return NULL;
}

void tetraIds_stepFilter(int step)
{
    int from = (selected == HOLD) ? beforeHold : selected;
    int count = (int)numFilters();
    int next = (from + step) % count;

    if (next < 0)
        next += count;
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
