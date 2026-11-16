/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/TETRA/TetraFilter.h"
#include "protocols/TETRA/TetraIds.h"

void tetraFilter_init(struct tetraFilter *f)
{
    memset(f, 0, sizeof(*f));
}

static bool partiesPass(const struct tetraFilter *f, uint32_t dst,
                        uint32_t src)
{
    return tetraIds_inFilter(dst, f->filter)
        || tetraIds_inFilter(src, f->filter);
}

static struct tetraFilterCall *find(struct tetraFilter *f, uint16_t callId)
{
    for (unsigned i = 0; i < TETRA_FILTER_MAX_CALLS; i++) {
        if (f->calls[i].used && f->calls[i].callId == callId)
            return &f->calls[i];
    }

    return NULL;
}

/** Entry for a call, a free or the oldest one if it is new */
static struct tetraFilterCall *get(struct tetraFilter *f, uint16_t callId)
{
    struct tetraFilterCall *c = find(f, callId);

    for (unsigned i = 0; i < TETRA_FILTER_MAX_CALLS && c == NULL; i++) {
        if (!f->calls[i].used)
            c = &f->calls[i];
    }

    if (c == NULL) {
        c = &f->calls[f->next];
        f->next = (f->next + 1) % TETRA_FILTER_MAX_CALLS;
    }

    if (!c->used || c->callId != callId) {
        memset(c, 0, sizeof(*c));
        c->used = true;
        c->callId = callId;
    }

    return c;
}

void tetraFilter_select(struct tetraFilter *f, size_t filter)
{
    f->filter = filter;

    for (unsigned i = 0; i < TETRA_FILTER_MAX_CALLS; i++) {
        struct tetraFilterCall *c = &f->calls[i];
        if (c->used)
            c->pass = partiesPass(f, c->dst, c->src);
    }
}

bool tetraFilter_call(struct tetraFilter *f, uint16_t callId, uint32_t dst,
                      uint32_t src, bool release)
{
    struct tetraFilterCall *c = get(f, callId);
    bool pass;

    /* Keep every party seen: the talker changes in a group call */
    if (dst != 0)
        c->dst = dst;
    if (src != 0 && !tetraIds_inFilter(c->src, f->filter))
        c->src = src;
    c->pass = c->pass || partiesPass(f, c->dst, c->src);
    pass = f->filter == 0 || c->pass;

    if (release)
        c->used = false;

    return pass;
}

bool tetraFilter_traffic(struct tetraFilter *f, uint16_t callId,
                         uint32_t talker, bool followed)
{
    if (f->filter == 0 || followed)
        return true;

    if (callId != 0) {
        struct tetraFilterCall *c = find(f, callId);
        if (c != NULL && c->pass)
            return true;
    }

    return tetraIds_inFilter(talker, f->filter);
}

bool tetraFilter_sds(const struct tetraFilter *f, uint32_t src, uint32_t dst)
{
    return partiesPass(f, dst, src);
}

bool tetraFilter_passes(const struct tetraFilter *f, uint16_t callId)
{
    if (f->filter == 0)
        return true;

    for (unsigned i = 0; i < TETRA_FILTER_MAX_CALLS; i++) {
        const struct tetraFilterCall *c = &f->calls[i];
        if (c->used && c->callId == callId)
            return c->pass;
    }

    return false;
}
