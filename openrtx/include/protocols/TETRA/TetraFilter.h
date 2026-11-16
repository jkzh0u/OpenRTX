/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_FILTER_H
#define OPENRTX_TETRA_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA talkgroup filter: decides which calls, traffic and short messages
 * get through to the status, the speaker and the call follower, from a
 * filter of the built-in identities (TetraIds.h).
 *
 * A call gets through when its called party or its calling party is in the
 * filter: a group call by its group, a point to point call by either radio.
 * Traffic follows the call it belongs to. Calls whose parties are not known
 * yet are held back until a call event names one.
 *
 * Independent of libtetra: the caller passes the fields of its events.
 */

/** Calls remembered */
#define TETRA_FILTER_MAX_CALLS 8

struct tetraFilterCall {
    bool used;
    bool pass;
    uint16_t callId;
    uint32_t dst;
    uint32_t src;
};

struct tetraFilter {
    size_t filter; /**< Selected filter, 0 lets everything through */
    struct tetraFilterCall calls[TETRA_FILTER_MAX_CALLS];
    uint8_t next;
};

/**
 * Initialise, letting everything through.
 *
 * @param f: filter state.
 */
void tetraFilter_init(struct tetraFilter *f);

/**
 * Select a filter. Calls already known are checked again.
 *
 * @param f: filter state.
 * @param filter: filter number, 0 for all.
 */
void tetraFilter_select(struct tetraFilter *f, size_t filter);

/**
 * A call control event.
 *
 * @param f: filter state.
 * @param callId: call identifier.
 * @param dst: called party, 0 if unknown.
 * @param src: calling or talking party, 0 if unknown.
 * @param release: the call ends.
 * @return true to let the event through.
 */
bool tetraFilter_call(struct tetraFilter *f, uint16_t callId, uint32_t dst,
                      uint32_t src, bool release);

/**
 * A traffic slot.
 *
 * @param f: filter state.
 * @param callId: call identifier, 0 if unknown.
 * @param talker: talking party, 0 if unknown.
 * @param followed: received on the carrier of a followed call, which only
 * carries that call's slots.
 * @return true to let the traffic through.
 */
bool tetraFilter_traffic(struct tetraFilter *f, uint16_t callId,
                         uint32_t talker, bool followed);

/**
 * A short data message.
 *
 * @param f: filter state.
 * @param src: sender.
 * @param dst: addressee.
 * @return true to let the message through.
 */
bool tetraFilter_sds(const struct tetraFilter *f, uint32_t src, uint32_t dst);

/**
 * @param f: filter state.
 * @param callId: call identifier.
 * @return true if the call gets through.
 */
bool tetraFilter_passes(const struct tetraFilter *f, uint16_t callId);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_FILTER_H */
