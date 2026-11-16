/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/TETRA/TetraIds.h"

struct tetraId {
    uint32_t ssi;
    const char *name;
};

/* Each list ends with a sentinel, so that an empty list is valid */
#define CPS_CHANNEL(...)

static const struct tetraId ids[] = {
#ifdef TETRA_ID_LIST
#define TETRA_ID(ssi, name, listen) { (ssi), (name) },
#include TETRA_ID_LIST
#undef TETRA_ID
#endif
    { 0, NULL },
};

static const uint32_t listenIds[] = {
#ifdef TETRA_ID_LIST
#define TETRA_ID(ssi, name, listen) LISTEN_##listen(ssi)
#define LISTEN_true(ssi) (ssi),
#define LISTEN_1(ssi) (ssi),
#define LISTEN_false(ssi)
#define LISTEN_0(ssi)
#include TETRA_ID_LIST
#undef TETRA_ID
#endif
    0,
};

#undef CPS_CHANNEL

#define NUM_IDS (sizeof(ids) / sizeof(ids[0]) - 1)
#define NUM_LISTEN (sizeof(listenIds) / sizeof(listenIds[0]) - 1)

_Static_assert(NUM_LISTEN <= TETRA_IDS_MAX_LISTEN,
               "Too many identities to listen to");

const char *tetraIds_name(uint32_t ssi)
{
    if (ssi == 0)
        return NULL;

    for (size_t i = 0; i < NUM_IDS; i++) {
        if (ids[i].ssi == ssi)
            return ids[i].name;
    }

    return NULL;
}

const uint32_t *tetraIds_listen(size_t *count)
{
    *count = NUM_LISTEN;
    return listenIds;
}
