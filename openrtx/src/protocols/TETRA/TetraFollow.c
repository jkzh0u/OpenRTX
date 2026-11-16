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
    tetraFollow_home(f);
}

void tetraFollow_home(struct tetraFollow *f)
{
    f->selected = false;
    f->away = false;
    f->leave = false;
    f->carrier = 0xffff;
    f->freq = 0;
    f->slots = TETRA_FOLLOW_ALL_SLOTS;
}

static struct tetraFollowCallInfo *findCall(struct tetraFollow *f,
                                            uint16_t callId)
{
    for (unsigned i = 0; i < TETRA_FOLLOW_MAX_CALLS; i++) {
        if (f->calls[i].used && f->calls[i].callId == callId)
            return &f->calls[i];
    }

    return NULL;
}

/** Remember a call's type, replacing the oldest entry when full. */
static void noteCall(struct tetraFollow *f, uint16_t callId, bool speech,
                     bool encrypted)
{
    struct tetraFollowCallInfo *c = findCall(f, callId);

    if (c == NULL) {
        for (unsigned i = 0; i < TETRA_FOLLOW_MAX_CALLS && c == NULL; i++) {
            if (!f->calls[i].used)
                c = &f->calls[i];
        }
    }

    if (c == NULL) {
        c = &f->calls[f->nextCall];
        f->nextCall = (f->nextCall + 1) % TETRA_FOLLOW_MAX_CALLS;
    }

    c->used = true;
    c->callId = callId;
    c->speech = speech;
    c->encrypted = encrypted;
}

static bool playable(const struct tetraFollow *f, bool speech, bool encrypted)
{
    return speech && (!encrypted || f->followEncrypted);
}

static void selectCall(struct tetraFollow *f, uint16_t callId, long long now)
{
    f->selected = true;
    f->callId = callId;
    f->lastActivity = now;
}

/** Drop the selected call, and leave its carrier if away. */
static void drop(struct tetraFollow *f)
{
    f->selected = false;
    if (f->away)
        f->leave = true;
}

void tetraFollow_call(struct tetraFollow *f, const struct tetraFollowCall *c,
                      long long now)
{
    bool ok = playable(f, c->speech, c->encrypted);

    if (c->event == TETRA_FOLLOW_RELEASE) {
        struct tetraFollowCallInfo *info = findCall(f, c->callId);
        if (info != NULL)
            info->used = false;
    } else {
        noteCall(f, c->callId, c->speech, c->encrypted);
    }

    if (f->selected) {
        /* One call at a time: others wait until it ends */
        if (c->callId != f->callId)
            return;

        if (c->event == TETRA_FOLLOW_RELEASE || !ok) {
            drop(f);
            return;
        }

        /* Moved within the carrier: libtetra does not ask to retune */
        if (f->away && c->carrier == f->carrier && c->slots != 0)
            f->slots = c->slots & TETRA_FOLLOW_ALL_SLOTS;

        f->lastActivity = now;
        return;
    }

    /*
     * Select the call even when it stays on the main carrier, so that a call
     * starting elsewhere does not take the receiver away from it.
     */
    if (c->event == TETRA_FOLLOW_UPDATE && ok)
        selectCall(f, c->callId, now);
}

void tetraFollow_traffic(struct tetraFollow *f, uint16_t callId, bool speech,
                         bool encrypted, long long now)
{
    bool ok = playable(f, speech, encrypted);

    if (callId != 0 && encrypted) {
        struct tetraFollowCallInfo *info = findCall(f, callId);
        if (info != NULL)
            info->encrypted = true;
    }

    if (!f->selected) {
        /* At home, the call being heard is selected */
        if (!f->away && callId != 0 && ok)
            selectCall(f, callId, now);
        return;
    }

    /*
     * Away from home only the call's timeslots are decoded, so traffic there
     * is the call's even if the receiver could not tell (no usage marker).
     */
    if (callId != f->callId && !(f->away && callId == 0))
        return;

    if (!ok) {
        drop(f);
        return;
    }

    f->lastActivity = now;
}

bool tetraFollow_retune(struct tetraFollow *f,
                        const struct tetraFollowRetune *r, long long now)
{
    if (r->main) {
        tetraFollow_home(f);
        return true;
    }

    const struct tetraFollowCallInfo *info = findCall(f, r->callId);
    if (info != NULL && !playable(f, info->speech, info->encrypted))
        return false;

    if (f->selected && f->callId != r->callId)
        return false;

    uint32_t freq = tetraFollow_carrierFreq(f->home, r->mainFreq, r->freq);
    if (freq == 0)
        return false;

    selectCall(f, r->callId, now);
    f->leave = false;
    f->away = freq != f->home;
    if (f->away) {
        f->carrier = r->carrier;
        f->freq = freq;
        f->slots = r->slots & TETRA_FOLLOW_ALL_SLOTS;
        if (f->slots == 0)
            f->slots = TETRA_FOLLOW_ALL_SLOTS;
    } else {
        f->carrier = 0xffff;
        f->freq = 0;
        f->slots = TETRA_FOLLOW_ALL_SLOTS;
    }

    return true;
}

bool tetraFollow_tick(struct tetraFollow *f, long long now)
{
    if (f->selected && now - f->lastActivity >= (long long)f->timeoutMs)
        drop(f);

    if (f->away && f->leave) {
        tetraFollow_home(f);
        return true;
    }

    return false;
}

struct tetraFollowTarget tetraFollow_target(const struct tetraFollow *f)
{
    struct tetraFollowTarget t;

    t.away = f->away;
    t.freq = f->away ? f->freq : f->home;
    t.slots = f->away ? f->slots : TETRA_FOLLOW_ALL_SLOTS;
    return t;
}
