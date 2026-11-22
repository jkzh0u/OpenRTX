/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Read-only codeplug compiled into the firmware, for radios without codeplug
 * storage. CPS_BUILTIN_LIST names a header listing the channels, one entry
 * per line:
 *
 *     CPS_CHANNEL("Name", frequency_hz, OPMODE_xxx)
 *
 * Channels are receive only, 25 kHz, with the same frequency for TX and RX.
 * There are no contacts and no banks, and every write fails. The TETRA_
 * entries in the same header are for TetraIds.c and are ignored here.
 *
 * With CONFIG_CHLIST the channels come instead from the channel list image
 * (core/chlist.h), kept apart from the firmware, with their own transmit
 * frequency, bandwidth and tones.
 */

#include <string.h>
#include "interfaces/cps_io.h"
#include "rtx/rtx.h"

#ifdef CONFIG_CHLIST
#include "core/chlist.h"
#elif !defined(CPS_BUILTIN_LIST)
#error "CPS_BUILTIN_LIST must name the channel list header"
#endif

#ifndef CONFIG_CHLIST

struct builtinChannel {
    const char *name;
    freq_t freq;
    uint8_t mode;
};

/* The sentinel keeps the array valid when the list is empty */
static const struct builtinChannel channels[] = {
#define CPS_CHANNEL(name, freq, mode) { (name), (freq), (mode) },
#define TETRA_FILTER(...)
#define TETRA_ID(...)
#define TETRA_LA(...)
#define TETRA_ID_RANGE(...)
#include CPS_BUILTIN_LIST
#undef TETRA_ID_RANGE
#undef TETRA_LA
#undef TETRA_ID
#undef TETRA_FILTER
#undef CPS_CHANNEL
    { NULL, 0, OPMODE_NONE },
};

#define NUM_CHANNELS (sizeof(channels) / sizeof(channels[0]) - 1)
#endif

int cps_open(char *cps_name)
{
    (void)cps_name;
    return 0;
}

void cps_close()
{
}

int cps_create(char *cps_name)
{
    (void)cps_name;
    return -1;
}

int cps_readContact(contact_t *contact, uint16_t pos)
{
    (void)contact;
    (void)pos;
    return -1;
}

/*
 * From the channel list image (CONFIG_CHLIST) or the list built in; one
 * definition, on the same line in both builds (coverage reports merge them)
 */
int cps_readChannel(channel_t *channel, uint16_t pos)
{
#ifdef CONFIG_CHLIST
    struct chlistChannel c;

    if (!chlist_channel(pos, &c))
        return -1;

    memset(channel, 0, sizeof(*channel));
    channel->mode = c.mode;
    channel->bandwidth = c.bandwidth & 1;
    channel->rx_only = (c.flags & CHLIST_RX_ONLY) ? 1 : 0;
    channel->rx_frequency = c.rxHz;
    channel->tx_frequency = c.txHz;
    if (c.mode == OPMODE_FM) {
        channel->fm.rxToneEn = (c.rxTone >> 7) & 1;
        channel->fm.rxTone = c.rxTone & 0x7f;
        channel->fm.txToneEn = (c.txTone >> 7) & 1;
        channel->fm.txTone = c.txTone & 0x7f;
    }
    strncpy(channel->name, c.name, sizeof(channel->name) - 1);
#else
    if (pos >= NUM_CHANNELS)
        return -1;

    const struct builtinChannel *c = &channels[pos];

    memset(channel, 0, sizeof(*channel));
    channel->mode = c->mode;
    channel->bandwidth = BW_25;
    channel->rx_only = 1;
    channel->rx_frequency = c->freq;
    channel->tx_frequency = c->freq;
    strncpy(channel->name, c->name, sizeof(channel->name) - 1);
#endif
    return 0;
}

int cps_readBankHeader(bankHdr_t *b_header, uint16_t pos)
{
    (void)b_header;
    (void)pos;
    return -1;
}

int cps_readBankData(uint16_t bank_pos, uint16_t pos)
{
    (void)bank_pos;
    (void)pos;
    return -1;
}

int cps_writeContact(contact_t contact, uint16_t pos)
{
    (void)contact;
    (void)pos;
    return -1;
}

int cps_writeChannel(channel_t channel, uint16_t pos)
{
    (void)channel;
    (void)pos;
    return -1;
}

int cps_writeBankHeader(bankHdr_t b_header, uint16_t pos)
{
    (void)b_header;
    (void)pos;
    return -1;
}

int cps_writeBankData(uint32_t ch, uint16_t bank_pos, uint16_t pos)
{
    (void)ch;
    (void)bank_pos;
    (void)pos;
    return -1;
}

int cps_insertContact(contact_t contact, uint16_t pos)
{
    (void)contact;
    (void)pos;
    return -1;
}

int cps_insertChannel(channel_t channel, uint16_t pos)
{
    (void)channel;
    (void)pos;
    return -1;
}

int cps_insertBankHeader(bankHdr_t b_header, uint16_t pos)
{
    (void)b_header;
    (void)pos;
    return -1;
}

int cps_insertBankData(uint32_t ch, uint16_t bank_pos, uint16_t pos)
{
    (void)ch;
    (void)bank_pos;
    (void)pos;
    return -1;
}

int cps_deleteContact(uint16_t pos)
{
    (void)pos;
    return -1;
}

int cps_deleteChannel(channel_t channel, uint16_t pos)
{
    (void)channel;
    (void)pos;
    return -1;
}

int cps_deleteBankHeader(uint16_t pos)
{
    (void)pos;
    return -1;
}

int cps_deleteBankData(uint16_t bank_pos, uint16_t pos)
{
    (void)bank_pos;
    (void)pos;
    return -1;
}
