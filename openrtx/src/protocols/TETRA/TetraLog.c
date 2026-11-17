/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "protocols/TETRA/TetraLog.h"

#define barrier() __sync_synchronize()

/*
 * Positions are byte counts since the start, so that a full ring and an
 * empty one differ: the writer moves the heads, the reader the tails.
 */

/* Live queue */
static char live[TETRA_LOG_LIVE_SIZE];
static volatile uint32_t liveHead;
static volatile uint32_t liveTail;
static volatile uint32_t dropped;
static uint32_t droppedSent; /* Reader */

/* History ring */
static char *history;
static uint32_t historySize;
static volatile uint32_t historyHead;    /* Bytes complete */
static volatile uint32_t historyWriting; /* Bytes being written, >= head */

/* History requests */
static volatile uint8_t requests;
static uint8_t requestsDone; /* Reader */

/* Notice being sent, reader only */
static char note[64];
static size_t noteLen;
static size_t notePos;

/* History being sent, reader only */
static bool sending;
static uint32_t sendPos;
static uint32_t sendEnd;
static bool skipToLine; /* Started in the middle of a line */

void tetraLog_init(char *buf, uint32_t size)
{
    liveHead = liveTail = 0;
    dropped = droppedSent = 0;
    history = (size > 0) ? buf : NULL;
    historySize = (history != NULL) ? size : 0;
    historyHead = historyWriting = 0;
    requestsDone = requests;
    sending = false;
    noteLen = notePos = 0;
}

void tetraLog_printf(long long now, const char *fmt, ...)
{
    char line[TETRA_LOG_LINE_MAX];
    va_list ap;

    if (now < 0)
        now = 0;
    int n = snprintf(line, sizeof(line), "%6lu.%03u  ",
                     (unsigned long)(now / 1000), (unsigned)(now % 1000));
    if (n < 0)
        n = 0;

    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
    va_end(ap);
    if (m < 0)
        m = 0;

    /* Truncated lines keep their newline */
    uint32_t len = (uint32_t)(n + m);
    if (len > sizeof(line) - 2)
        len = sizeof(line) - 2;
    line[len++] = '\n';

    if (history != NULL) {
        uint32_t head = historyHead;
        historyWriting = head + len;
        barrier();
        for (uint32_t i = 0; i < len; i++)
            history[(head + i) % historySize] = line[i];
        barrier();
        historyHead = head + len;
    }

    uint32_t head = liveHead;
    if (TETRA_LOG_LIVE_SIZE - (head - liveTail) < len) {
        dropped = dropped + 1;
        return;
    }
    for (uint32_t i = 0; i < len; i++)
        live[(head + i) % TETRA_LOG_LIVE_SIZE] = line[i];
    barrier();
    liveHead = head + len;
}

void tetraLog_requestHistory(void)
{
    requests = requests + 1;
}

uint32_t tetraLog_dropped(void)
{
    return dropped;
}

/** Rest of the notice being sent */
static size_t noteNext(char *buf, size_t len)
{
    size_t n = noteLen - notePos;

    if (n > len)
        n = len;
    memcpy(buf, note + notePos, n);
    notePos += n;
    return n;
}

/** Send a notice, in as many pieces as the reader takes */
static size_t text(char *buf, size_t len, const char *fmt, unsigned long v)
{
    int n = snprintf(note, sizeof(note), fmt, v);

    if (n < 0)
        n = 0;
    noteLen = ((size_t)n < sizeof(note)) ? (size_t)n : sizeof(note) - 1;
    notePos = 0;
    return noteNext(buf, len);
}

/** Next bytes of the history being sent */
static size_t historyNext(char *buf, size_t len)
{
    for (;;) {
        /* Lines overwritten since: skip to the oldest one left */
        if (historyWriting - sendPos > historySize) {
            sendPos = historyWriting - historySize;
            skipToLine = true;
        }

        if ((int32_t)(sendEnd - sendPos) <= 0) {
            sending = false;
            return text(buf, len, "--- end of stored log ---\n", 0);
        }

        uint32_t n = sendEnd - sendPos;
        if (n > len)
            n = (uint32_t)len;
        for (uint32_t i = 0; i < n; i++)
            buf[i] = history[(sendPos + i) % historySize];
        barrier();

        /* Overwritten while copying: start again from the oldest */
        if (historyWriting - sendPos > historySize)
            continue;

        sendPos += n;
        if (!skipToLine)
            return n;

        char *nl = memchr(buf, '\n', n);
        if (nl == NULL)
            continue;
        skipToLine = false;
        size_t rest = n - (size_t)(nl + 1 - buf);
        memmove(buf, nl + 1, rest);
        if (rest > 0)
            return rest;
    }
}

size_t tetraLog_next(char *buf, size_t len)
{
    if (len == 0)
        return 0;

    if (notePos < noteLen)
        return noteNext(buf, len);

    /* Live lines first */
    uint32_t tail = liveTail;
    uint32_t used = liveHead - tail;
    if (used > 0) {
        barrier();
        uint32_t n = (used < len) ? used : (uint32_t)len;
        for (uint32_t i = 0; i < n; i++)
            buf[i] = live[(tail + i) % TETRA_LOG_LIVE_SIZE];
        barrier();
        liveTail = tail + n;
        return n;
    }

    /* Emptied after lines were dropped: say so where they were */
    uint32_t d = dropped;
    if (d != droppedSent) {
        unsigned long lost = d - droppedSent;
        droppedSent = d;
        return text(buf, len, "--- %lu lines not sent: line too slow ---\n",
                    lost);
    }

    if (!sending && requests != requestsDone) {
        requestsDone = requests;
        if (history == NULL)
            return text(buf, len, "--- no stored log ---\n", 0);

        sendEnd = historyHead;
        sendPos = (sendEnd > historySize) ? sendEnd - historySize : 0;
        skipToLine = sendPos > 0;
        sending = true;
        return text(buf, len, "--- stored log, %lu bytes ---\n",
                    (unsigned long)(sendEnd - sendPos));
    }

    if (sending)
        return historyNext(buf, len);

    return 0;
}
