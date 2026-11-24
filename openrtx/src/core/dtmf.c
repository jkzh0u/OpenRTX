/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hwconfig.h"
#include "core/dtmf.h"

#ifdef CONFIG_DTMF

static volatile char current;
static char sent[DTMF_SENT_MAX + 1];
static volatile unsigned count;

void dtmf_key(char key)
{
    uint16_t lo, hi;

    if (key != 0 && !dtmf_tones(key, &lo, &hi))
        return;
    if (key != 0 && key != current) {
        /* Keep the last DTMF_SENT_MAX keys */
        unsigned n = count;
        if (n == DTMF_SENT_MAX) {
            for (unsigned i = 1; i < DTMF_SENT_MAX; i++)
                sent[i - 1] = sent[i];
            n--;
        }
        sent[n] = key;
        sent[n + 1] = '\0';
        count = n + 1;
    }
    current = key;
}

char dtmf_current(void)
{
    return current;
}

void dtmf_clear(void)
{
    current = 0;
    count = 0;
    sent[0] = '\0';
}

const char *dtmf_sent(void)
{
    return sent;
}

#endif /* CONFIG_DTMF */
