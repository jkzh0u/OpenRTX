/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_LOG_H
#define OPENRTX_TETRA_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TETRA receive log: one text line per thing the receiver decoded or
 * decided (lock, cell, calls, retunes, cell moves), with the time since
 * power on, for watching call following and cell tracking on a network.
 *
 * Each line goes two ways:
 * - to a small live queue, which a sender drains to a serial line. If the
 *   line is too slow the queue fills: lines are then dropped, and the
 *   sender reports how many;
 * - to a large history ring, which keeps the most recent lines (oldest
 *   overwritten) to be sent on request, for a ride with nothing connected.
 *
 * One writer (the receiver) and one reader (the sender), without locks:
 * call tetraLog_printf() from one thread only, and tetraLog_next() from one
 * other thread only. tetraLog_requestHistory() can be called from any
 * thread.
 *
 * Independent of the radio and of libtetra.
 */

/** Longest line, with its time and newline */
#define TETRA_LOG_LINE_MAX 160
/** Live queue, bytes: a power of two */
#define TETRA_LOG_LIVE_SIZE 4096

/**
 * Start logging. Lines are lost until this is called.
 *
 * @param history: memory for the history ring, NULL for none.
 * @param size: its size in bytes.
 */
void tetraLog_init(char *history, uint32_t size);

/**
 * Log a line. A newline is added. Writer thread only.
 *
 * @param now: time since power on, ms.
 * @param fmt: printf format.
 */
void tetraLog_printf(long long now, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Ask for the history to be sent, after the lines already queued. Live
 * lines keep being queued meanwhile.
 */
void tetraLog_requestHistory(void);

/**
 * Next bytes to send. Reader thread only.
 *
 * @param buf: destination.
 * @param len: its size.
 * @return number of bytes written to buf, 0 if there is nothing to send.
 */
size_t tetraLog_next(char *buf, size_t len);

/**
 * @return the number of lines that did not fit in the live queue.
 */
uint32_t tetraLog_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_LOG_H */
