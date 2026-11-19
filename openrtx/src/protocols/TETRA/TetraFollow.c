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

/**
 * Remember a call's type, replacing the oldest entry when full.
 *
 * @return the call's entry.
 */
static struct tetraFollowCallInfo *noteCall(struct tetraFollow *f,
                                            uint16_t callId, bool speech,
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

    if (!c->used || c->callId != callId) {
        memset(c, 0, sizeof(*c));
        c->used = true;
        c->callId = callId;
    }
    c->speech = speech;
    c->encrypted = encrypted;
    return c;
}

static bool playable(const struct tetraFollow *f, bool speech, bool encrypted)
{
    return speech && (!encrypted || f->followEncrypted);
}

/** A priority call was active: hold the radio on the main carrier */
static void notePriority(struct tetraFollow *f, long long now)
{
    f->priorityHeard = true;
    f->lastPriority = now;
}

/**
 * @return true while the radio should stay on the main carrier for
 * priority calls: one was active less than TETRA_FOLLOW_PRIORITY_HOLD_MS
 * ago.
 */
static bool holding(const struct tetraFollow *f, long long now)
{
    return f->priorityHeard
           && now - f->lastPriority < TETRA_FOLLOW_PRIORITY_HOLD_MS;
}

static void selectCall(struct tetraFollow *f, uint16_t callId, long long now)
{
    /* Heard once, by whatever way: not joined late afterwards */
    struct tetraFollowCallInfo *info = findCall(f, callId);
    if (info != NULL)
        info->joined = true;

    f->selected = true;
    f->callId = callId;
    f->lastActivity = now;
    f->selectedPriority = info != NULL && info->priority;
    f->callSlots = info != NULL ? info->slots : 0;
    if (f->selectedPriority)
        notePriority(f, now);
}

void tetraFollow_setHome(struct tetraFollow *f, uint32_t home)
{
    f->home = home;
    tetraFollow_home(f);
}

void tetraFollow_drop(struct tetraFollow *f)
{
    f->selected = false;
    if (f->away)
        f->leave = true;
}

void tetraFollow_call(struct tetraFollow *f, const struct tetraFollowCall *c,
                      long long now)
{
    bool ok = playable(f, c->speech, c->encrypted);

    if (c->priority)
        notePriority(f, now);

    if (c->event == TETRA_FOLLOW_RELEASE) {
        struct tetraFollowCallInfo *info = findCall(f, c->callId);
        if (info != NULL)
            info->used = false;
    } else {
        struct tetraFollowCallInfo *info =
            noteCall(f, c->callId, c->speech, c->encrypted);
        info->priority = info->priority || c->priority;

        /* Where the call is, for a late join */
        if (c->carrier != 0xffff && c->slots != 0) {
            info->carrier = c->carrier;
            info->slots = c->slots & TETRA_FOLLOW_ALL_SLOTS;
            info->freq = tetraFollow_carrierFreq(f->home, c->mainFreq,
                                                 c->freq);
        }
    }

    if (f->selected) {
        /*
         * One call at a time: others wait until it ends. Except that on the
         * main carrier a priority call takes over from another call.
         */
        if (c->callId != f->callId) {
            if (c->event == TETRA_FOLLOW_UPDATE && ok && c->priority
                && !f->selectedPriority && !f->away) {
                selectCall(f, c->callId, now);
                f->preempted = true;
            }
            return;
        }

        if (c->event == TETRA_FOLLOW_RELEASE || !ok) {
            tetraFollow_drop(f);
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
        tetraFollow_drop(f);
        return;
    }

    f->lastActivity = now;
    if (f->selectedPriority)
        notePriority(f, now);
}

bool tetraFollow_retune(struct tetraFollow *f,
                        const struct tetraFollowRetune *r, long long now)
{
    if (r->main) {
        tetraFollow_home(f);
        return true;
    }

    struct tetraFollowCallInfo *info = findCall(f, r->callId);
    if (info != NULL && !playable(f, info->speech, info->encrypted))
        return false;

    bool priority = r->priority || (info != NULL && info->priority);
    bool preempt = false;
    if (f->selected && f->callId != r->callId) {
        /* Only a priority call takes over, and only from the main carrier */
        if (!priority || f->selectedPriority || f->away)
            return false;
        preempt = true;
    }

    uint32_t freq = tetraFollow_carrierFreq(f->home, r->mainFreq, r->freq);
    if (freq == 0)
        return false;

    /*
     * Priority talkgroups were active just now: stay for their next call.
     * A call selected on the main carrier that leaves it is let go, and
     * may be joined late after the hold.
     */
    if (!priority && freq != f->home && holding(f, now)) {
        if (f->selected && f->callId == r->callId) {
            tetraFollow_drop(f);
            if (info != NULL)
                info->joined = false;
        }
        return false;
    }

    selectCall(f, r->callId, now);
    if (priority) {
        f->selectedPriority = true;
        notePriority(f, now);
    }
    f->preempted = f->preempted || preempt;
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
        tetraFollow_drop(f);

    if (f->away && f->leave) {
        tetraFollow_home(f);
        return true;
    }

    return false;
}

bool tetraFollow_join(struct tetraFollow *f, bool (*allowed)(uint16_t callId),
                      long long now)
{
    if (f->selected || f->away)
        return false;

    /* Priority calls first; others not while priority calls are held for */
    bool hold = holding(f, now);
    for (unsigned i = 0; i < 2 * TETRA_FOLLOW_MAX_CALLS; i++) {
        struct tetraFollowCallInfo *c = &f->calls[i % TETRA_FOLLOW_MAX_CALLS];
        bool firstPass = i < TETRA_FOLLOW_MAX_CALLS;

        if (c->priority != firstPass || (!c->priority && hold))
            continue;
        if (!c->used || c->joined || c->freq == 0 || c->freq == f->home
            || c->slots == 0 || !playable(f, c->speech, c->encrypted))
            continue;
        if (allowed != NULL && !allowed(c->callId))
            continue;

        c->joined = true;
        selectCall(f, c->callId, now);
        f->leave = false;
        f->away = true;
        f->carrier = c->carrier;
        f->freq = c->freq;
        f->slots = c->slots;
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
