/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/TETRA/TetraFocus.h"

void tetraFocus_init(struct tetraFocus *f)
{
    memset(f, 0, sizeof(*f));
}

static bool same(const struct tetraFocus *f, uint16_t callId,
                 uint8_t usageMarker)
{
    if (callId != 0 && f->callId != 0)
        return callId == f->callId;
    return usageMarker != 0 && usageMarker == f->usageMarker;
}

static void take(struct tetraFocus *f, uint16_t callId, uint8_t usageMarker,
                 bool alarm, long long now)
{
    f->active = true;
    f->callId = callId;
    f->usageMarker = usageMarker;
    f->alarm = alarm;
    f->last = now;
}

bool tetraFocus_offer(struct tetraFocus *f, uint16_t callId,
                      uint8_t usageMarker, bool alarm, long long now)
{
    if (f->active && same(f, callId, usageMarker)) {
        /* Learn what was missing */
        if (callId != 0)
            f->callId = callId;
        if (usageMarker != 0)
            f->usageMarker = usageMarker;
        f->alarm |= alarm;
        f->last = now;
        return true;
    }

    if (!f->active || now - f->last >= TETRA_FOCUS_HANG_MS
        || (alarm && !f->alarm)) {
        take(f, callId, usageMarker, alarm, now);
        return true;
    }

    return false;
}

void tetraFocus_release(struct tetraFocus *f, uint16_t callId)
{
    if (f->active && callId != 0 && callId == f->callId)
        f->active = false;
}
