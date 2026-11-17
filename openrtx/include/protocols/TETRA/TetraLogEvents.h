/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_LOG_EVENTS_H
#define OPENRTX_TETRA_LOG_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lines of the TETRA log (TetraLog.h) for what libtetra decodes: lock, cell,
 * neighbours, network time, calls, short data and the start of each
 * transmission heard, with the names of known identities (TetraIds.h).
 * Repeated broadcasts are logged only when they change.
 */

struct tetra_event;

/**
 * Forget what was logged: the next broadcasts are logged again. Call when
 * the receiver starts on a new frequency.
 */
void tetraLogEvents_reset(void);

/**
 * Log an event, if it is worth a line. Same thread as tetraLog_printf().
 *
 * @param now: time since power on, ms.
 * @param ev: libtetra event.
 * @param passed: the talkgroup filter lets it through.
 */
void tetraLogEvents_event(long long now, const struct tetra_event *ev,
                          bool passed);

/**
 * Format a frequency for the log, as MHz with five decimals.
 *
 * @param buf: destination, at least 16 bytes.
 * @param len: its size.
 * @param hz: frequency, Hz.
 * @return buf.
 */
const char *tetraLog_freq(char *buf, unsigned len, uint32_t hz);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_LOG_EVENTS_H */
