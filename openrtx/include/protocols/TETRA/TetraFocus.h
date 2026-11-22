/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_FOCUS_H
#define OPENRTX_TETRA_FOCUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The one TETRA call the radio plays and shows. A cell can carry several
 * calls at once, on different timeslots; their speech must not be mixed.
 * The first call heard keeps the speaker through the pauses between its
 * transmissions; once it is silent for a while another call can take it,
 * and an alarm call takes it at once.
 *
 * Traffic is matched to the call by its call identifier, or by its usage
 * marker when the identifier is not known yet.
 *
 * Independent of the radio and of libtetra.
 */

/** Silence after which another call can take over, ms */
#define TETRA_FOCUS_HANG_MS 2000

struct tetraFocus {
    bool active;
    uint16_t callId;     /**< 0 if not known */
    uint8_t usageMarker; /**< 0 if not known */
    bool alarm;
    long long last;      /**< Last activity, ms */
};

/**
 * @param f: focus.
 */
void tetraFocus_init(struct tetraFocus *f);

/**
 * Offer a call's activity: a call event or a traffic slot.
 *
 * @param f: focus.
 * @param callId: call identifier, 0 if unknown.
 * @param usageMarker: traffic usage marker, 0 if unknown.
 * @param alarm: the call is an alarm.
 * @param now: current time, ms.
 * @return true if this call has the focus: play and show it.
 */
bool tetraFocus_offer(struct tetraFocus *f, uint16_t callId,
                      uint8_t usageMarker, bool alarm, long long now);

/**
 * A call ended: free the focus if it had it.
 *
 * @param f: focus.
 * @param callId: call identifier.
 */
void tetraFocus_release(struct tetraFocus *f, uint16_t callId);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_FOCUS_H */
