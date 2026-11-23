/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <pthread.h>
#include <string.h>
#include "protocols/DMR/DmrStatus.h"

static pthread_mutex_t statusMutex = PTHREAD_MUTEX_INITIALIZER;
static struct dmrStatus status;
static volatile uint8_t slotFilter;
static volatile uint8_t ccFilter;

void dmr_getStatus(struct dmrStatus *out)
{
    pthread_mutex_lock(&statusMutex);
    *out = status;
    pthread_mutex_unlock(&statusMutex);
}

const struct dmrStatusCall *dmr_shownCall(const struct dmrStatus *s,
                                          long long now)
{
    const struct dmrStatusCall *best = NULL;

    for (int i = 0; i < 3; i++) {
        const struct dmrStatusCall *c = &s->slot[i];
        if (c->active && (best == NULL || c->start > best->start))
            best = c;
    }
    if (best != NULL)
        return best;

    if (s->nRecent > 0 && now - s->recent[0].last < DMR_STATUS_HANG_MS)
        return &s->recent[0];
    return NULL;
}

void dmrStatus_reset(uint32_t freq)
{
    pthread_mutex_lock(&statusMutex);
    struct dmrStatusCall recent[DMR_STATUS_RECENT];
    uint8_t nRecent = status.nRecent;
    memcpy(recent, status.recent, sizeof(recent));
    memset(&status, 0, sizeof(status));
    /* The recent calls stay: they are history */
    memcpy(status.recent, recent, sizeof(recent));
    status.nRecent = nRecent;
    status.tunedHz = freq;
    pthread_mutex_unlock(&statusMutex);
}

void dmrStatus_setFilter(uint8_t slots, uint8_t colourCode)
{
    slotFilter = (slots <= DMR_SLOTS_2) ? slots : DMR_SLOTS_BOTH;
    ccFilter = (colourCode <= 16) ? colourCode : 0;
}

bool dmrStatus_wanted(const struct dmrCall *c)
{
    uint8_t slots = slotFilter;
    uint8_t cc = ccFilter;

    /* A single transmitter has no timeslot to filter on */
    if (slots != DMR_SLOTS_BOTH && c->slot != 0 && c->slot != slots)
        return false;
    if (cc != 0 && c->colourCode != cc - 1)
        return false;
    return true;
}

/** Add a call that ended to the recent ones, newest first */
static void addRecent(const struct dmrStatusCall *c)
{
    struct dmrStatusCall e = *c;

    e.active = false;
    /* The same call again (talk and pause) replaces its entry */
    for (uint8_t i = 0; i < status.nRecent; i++) {
        const struct dmrCall *o = &status.recent[i].call;
        if (o->ids && e.call.ids && o->dst == e.call.dst
            && o->src == e.call.src && o->group == e.call.group) {
            memmove(&status.recent[i], &status.recent[i + 1],
                    (status.nRecent - i - 1) * sizeof(status.recent[0]));
            status.nRecent--;
            break;
        }
    }
    if (status.nRecent == DMR_STATUS_RECENT)
        status.nRecent--;
    memmove(&status.recent[1], &status.recent[0],
            status.nRecent * sizeof(status.recent[0]));
    status.recent[0] = e;
    status.nRecent++;
}

bool dmrStatus_onEvent(const struct dmrEvent *ev, long long now)
{
    bool wanted = true;

    pthread_mutex_lock(&statusMutex);
    switch (ev->type) {
        case DMR_EV_LOCK:
            status.locked = ev->u.lock.locked;
            status.sync = ev->u.lock.sync;
            if (!status.locked)
                status.ccKnown = false;
            break;

        case DMR_EV_CALL: {
            const struct dmrCall *c = &ev->u.call.call;
            struct dmrStatusCall *s = &status.slot[c->slot <= 2 ? c->slot : 0];
            status.ccKnown = true;
            status.colourCode = c->colourCode;
            wanted = dmrStatus_wanted(c);
            if (!wanted) {
                /* Hidden: forget it if it was shown before the filter */
                s->active = false;
                break;
            }
            switch (ev->u.call.what) {
                case DMR_CALL_START:
                    s->call = *c;
                    s->start = now;
                    s->last = now;
                    s->active = true;
                    break;
                case DMR_CALL_UPDATE:
                    if (!s->active)
                        s->start = now;
                    s->call = *c;
                    s->last = now;
                    s->active = true;
                    break;
                case DMR_CALL_END:
                    if (!s->active)
                        break;
                    s->call = *c;
                    s->last = now;
                    s->active = false;
                    addRecent(s);
                    break;
            }
            break;
        }

        case DMR_EV_DATA:
            status.ccKnown = true;
            status.colourCode = ev->u.data.colourCode;
            status.lastData = now;
            status.lastDataType = ev->u.data.dataType;
            status.lastDataSlot = ev->u.data.slot;
            break;
    }
    pthread_mutex_unlock(&statusMutex);

    return wanted;
}

void dmrStatus_setDemod(float symbolError, bool inverted, uint32_t good,
                        uint32_t bad)
{
    pthread_mutex_lock(&statusMutex);
    float pct = symbolError * 100.0f;
    status.symbolError = (uint16_t)((pct > 999.0f) ? 999.0f : pct);
    status.inverted = inverted;
    status.goodBursts = good;
    status.badBursts = bad;
    pthread_mutex_unlock(&statusMutex);
}
