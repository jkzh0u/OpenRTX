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
 * There are no contacts and no banks, and every write fails.
 */

#include <string.h>
#include "interfaces/cps_io.h"
#include "rtx/rtx.h"

#ifndef CPS_BUILTIN_LIST
#error "CPS_BUILTIN_LIST must name the channel list header"
#endif

struct builtinChannel {
    const char *name;
    freq_t freq;
    uint8_t mode;
};

/* The sentinel keeps the array valid when the list is empty */
static const struct builtinChannel channels[] = {
#define CPS_CHANNEL(name, freq, mode) { (name), (freq), (mode) },
#include CPS_BUILTIN_LIST
#undef CPS_CHANNEL
    { NULL, 0, OPMODE_NONE },
};

#define NUM_CHANNELS (sizeof(channels) / sizeof(channels[0]) - 1)

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

int cps_readChannel(channel_t *channel, uint16_t pos)
{
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
