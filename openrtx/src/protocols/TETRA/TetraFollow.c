/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/TETRA/TetraFollow.h"

uint32_t tetraFollow_carrierFreq(uint32_t home, uint32_t mainHz,
                                 uint32_t carrierHz)
{
    if (home == 0 || mainHz == 0 || carrierHz == 0)
        return 0;

    int32_t tuning = (int32_t)(home - mainHz);
    if (tuning > TETRA_FOLLOW_MAX_TUNING_HZ
        || tuning < -TETRA_FOLLOW_MAX_TUNING_HZ)
        return 0;

    return carrierHz + (uint32_t)tuning;
}

void tetraFollow_init(struct tetraFollow *f, uint32_t home)
{
    memset(f, 0, sizeof(*f));
    f->home = home;
    f->timeoutMs = TETRA_FOLLOW_TIMEOUT_MS;
}

static void drop(struct tetraFollow *f)
{
    f->selected = false;
    f->freq = 0;
    f->slots = 0;
}

/**
 * Take the channel allocation of an event for the selected call, if it has
 * one. An allocation on the main carrier, or to the control channel, brings
 * the receiver home while the call stays selected: with transmission
 * trunking each transmission gets a new allocation, sent on the control
 * channel.
 */
static void allocate(struct tetraFollow *f, const struct tetraFollowCall *c)
{
    if (c->freq == 0)
        return;

    if (c->freq == f->home || c->slots == 0) {
        f->freq = 0;
        f->slots = 0;
    } else {
        f->freq = c->freq;
        f->slots = c->slots & TETRA_FOLLOW_ALL_SLOTS;
    }
}

void tetraFollow_call(struct tetraFollow *f, const struct tetraFollowCall *c,
                      long long now)
{
    bool hidden = c->encrypted && !f->followEncrypted;

    if (f->selected) {
        /* One call at a time: others wait until it ends */
        if (c->callId != f->callId)
            return;

        if (c->event == TETRA_FOLLOW_RELEASE || hidden) {
            drop(f);
            return;
        }

        allocate(f, c);
        f->lastActivity = now;
        return;
    }

    if (c->event != TETRA_FOLLOW_UPDATE || !c->speech || hidden)
        return;

    /*
     * Select the call even when it stays on the main carrier, so that a call
     * starting elsewhere does not take the receiver away from it.
     */
    f->selected = true;
    f->callId = c->callId;
    f->freq = 0;
    f->slots = 0;
    f->lastActivity = now;
    allocate(f, c);
}

void tetraFollow_traffic(struct tetraFollow *f, uint16_t callId, bool encrypted,
                         long long now)
{
    if (!f->selected)
        return;

    /*
     * Away from home only the call's timeslots are decoded, so traffic there
     * is the call's even if the receiver could not tell (no usage marker).
     */
    if (callId != f->callId && !(f->freq != 0 && callId == 0))
        return;

    if (encrypted && !f->followEncrypted) {
        drop(f);
        return;
    }

    f->lastActivity = now;
}

void tetraFollow_lostSync(struct tetraFollow *f)
{
    drop(f);
}

void tetraFollow_tick(struct tetraFollow *f, long long now)
{
    if (f->selected && now - f->lastActivity >= (long long)f->timeoutMs)
        drop(f);
}

struct tetraFollowTarget tetraFollow_target(const struct tetraFollow *f)
{
    struct tetraFollowTarget t;

    t.away = f->selected && f->freq != 0;
    t.freq = t.away ? f->freq : f->home;
    t.slots = t.away ? f->slots : TETRA_FOLLOW_ALL_SLOTS;
    return t;
}
