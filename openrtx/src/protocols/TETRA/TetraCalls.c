/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/TETRA/TetraCalls.h"

void tetraCalls_init(struct tetraCalls *c)
{
    memset(c, 0, sizeof(*c));
}

void tetraCalls_note(struct tetraCalls *c, uint32_t dst, uint32_t src,
                     bool group, bool voice, bool encrypted, long long now)
{
    struct tetraCallEntry e;
    unsigned i;

    if (dst == 0 && src == 0)
        return;

    /* The entry for this called party, else a new one (or the oldest) */
    for (i = 0; i < c->count; i++) {
        const struct tetraCallEntry *o = &c->entry[i];
        if (dst != 0 ? o->dst == dst : (o->dst == 0 && o->src == src))
            break;
    }

    if (i < c->count) {
        e = c->entry[i];
    } else {
        memset(&e, 0, sizeof(e));
        e.dst = dst;
        e.group = group;
        e.first = now;
        if (c->count < TETRA_CALLS_MAX)
            c->count++;
        i = c->count - 1;
    }

    if (src != 0)
        e.src = src;
    if (voice && (e.heard == 0 || now - e.lastVoice >= TETRA_CALLS_PAUSE_MS))
        e.heard++;
    if (voice)
        e.lastVoice = now;
    e.voice |= voice;
    e.encrypted |= encrypted;
    e.time = now;

    /* Move it to the front */
    memmove(&c->entry[1], &c->entry[0], i * sizeof(c->entry[0]));
    c->entry[0] = e;
}
