/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/TETRA/TetraIds.h"

/* The list is read three times: filter numbers, filter names, identities */
#define CPS_CHANNEL(...)

enum {
    TF_ALL_ = 0,
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(id, name) TF_##id,
#define TETRA_ID(...)
#include TETRA_ID_LIST
#undef TETRA_ID
#undef TETRA_FILTER
#endif
    TF_COUNT_
};

_Static_assert(TF_COUNT_ - 1 <= TETRA_IDS_MAX_FILTERS, "Too many filters");

/* For the list: filter masks, bit n for filter n */
#define TF(id) (1ul << TF_##id)
#define TF_EVERY (~1ul)

static const char *const filterNames[] = {
    "All",
#ifdef TETRA_ID_LIST
#define TETRA_FILTER(id, name) (name),
#define TETRA_ID(...)
#include TETRA_ID_LIST
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
#include TETRA_ID_LIST
#undef TETRA_ID
#undef TETRA_FILTER
#endif
    { 0, NULL, 0 },
};

#undef CPS_CHANNEL

#define NUM_IDS (sizeof(ids) / sizeof(ids[0]) - 1)

/* Written by the UI, read by the receiver: one byte, no lock needed */
static volatile uint8_t selected;

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

size_t tetraIds_filterCount(void)
{
    return TF_COUNT_;
}

const char *tetraIds_filterName(size_t filter)
{
    return filter < TF_COUNT_ ? filterNames[filter] : NULL;
}

bool tetraIds_inFilter(uint32_t ssi, size_t filter)
{
    if (filter == 0)
        return true;
    if (filter >= TF_COUNT_)
        return false;

    const struct tetraId *id = find(ssi);

    return id != NULL && (id->filters & (1ul << filter)) != 0;
}

void tetraIds_stepFilter(int step)
{
    int next = ((int)selected + step) % (int)TF_COUNT_;

    if (next < 0)
        next += TF_COUNT_;
    selected = (uint8_t)next;
}

size_t tetraIds_selectedFilter(void)
{
    return selected;
}
