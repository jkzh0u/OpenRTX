/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/APRS/Ax25.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* A frame holds two addresses, control and the check sequence at least */
#define MIN_FRAME (2 * 7 + 1)

uint16_t ax25_fcs(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffff;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return crc ^ 0xffff;
}

static void readAddress(const uint8_t *p, struct ax25Address *a)
{
    int n = 0;

    for (int i = 0; i < 6; i++) {
        char c = (char)(p[i] >> 1);
        if (c != ' ')
            a->call[n++] = c;
    }
    a->call[n] = '\0';
    a->ssid = (p[6] >> 1) & 0x0f;
    a->repeated = (p[6] & 0x80) != 0;
}

bool ax25_decode(const uint8_t *data, size_t len, struct ax25Frame *out)
{
    size_t pos = 0;
    unsigned n = 0;

    memset(out, 0, sizeof(*out));
    if (len < MIN_FRAME)
        return false;

    /* Addresses, the last with bit 0 of its SSID byte set */
    for (;;) {
        if (pos + 7 > len)
            return false;
        if (n == 0)
            readAddress(&data[pos], &out->dst);
        else if (n == 1)
            readAddress(&data[pos], &out->src);
        else if (n - 2 < AX25_MAX_DIGIS)
            readAddress(&data[pos], &out->digi[out->ndigi++]);
        else
            return false;
        bool last = data[pos + 6] & 1;
        pos += 7;
        n++;
        if (last)
            break;
    }
    if (n < 2 || pos >= len)
        return false;

    out->control = data[pos++];
    /* I and UI frames have a protocol identifier */
    if ((out->control & 0x01) == 0 || (out->control & 0xef) == 0x03) {
        if (pos >= len)
            return false;
        out->pid = data[pos++];
    }
    size_t info = len - pos;
    if (info > AX25_MAX_INFO)
        info = AX25_MAX_INFO;
    memcpy(out->info, &data[pos], info);
    out->infoLen = (uint16_t)info;
    return true;
}

static void writeAddress(uint8_t *p, const struct ax25Address *a, bool last,
                         bool command)
{
    size_t n = strlen(a->call);

    for (size_t i = 0; i < 6; i++)
        p[i] = (uint8_t)((i < n ? toupper((unsigned char)a->call[i]) : ' ')
                         << 1);
    p[6] = (uint8_t)(0x60 | ((a->ssid & 0x0f) << 1) | (last ? 1 : 0));
    if (command || a->repeated)
        p[6] |= 0x80;
}

size_t ax25_encode(const struct ax25Frame *f, uint8_t *out, size_t max)
{
    size_t need = 7 * (2 + (size_t)f->ndigi) + 2 + f->infoLen + 2;
    size_t pos = 0;

    if (need > max || f->ndigi > AX25_MAX_DIGIS)
        return 0;

    /* A command: C bit set in the destination, clear in the source */
    writeAddress(&out[pos], &f->dst, false, true);
    pos += 7;
    writeAddress(&out[pos], &f->src, f->ndigi == 0, false);
    pos += 7;
    for (uint8_t i = 0; i < f->ndigi; i++) {
        writeAddress(&out[pos], &f->digi[i], i + 1 == f->ndigi, false);
        pos += 7;
    }
    out[pos++] = f->control;
    out[pos++] = f->pid;
    memcpy(&out[pos], f->info, f->infoLen);
    pos += f->infoLen;

    uint16_t fcs = ax25_fcs(out, pos);
    out[pos++] = (uint8_t)(fcs & 0xff);
    out[pos++] = (uint8_t)(fcs >> 8);
    return pos;
}

bool ax25_parseCall(const char *text, struct ax25Address *out)
{
    size_t n = 0;

    memset(out, 0, sizeof(*out));
    while (text[n] != '\0' && text[n] != '-') {
        if (n >= 6 || !isalnum((unsigned char)text[n]))
            return false;
        out->call[n] = (char)toupper((unsigned char)text[n]);
        n++;
    }
    if (n == 0)
        return false;
    out->call[n] = '\0';

    if (text[n] == '-') {
        unsigned ssid = 0;
        const char *p = &text[n + 1];
        if (*p == '\0')
            return false;
        for (; *p != '\0'; p++) {
            if (!isdigit((unsigned char)*p))
                return false;
            ssid = ssid * 10 + (unsigned)(*p - '0');
            if (ssid > 15)
                return false;
        }
        out->ssid = (uint8_t)ssid;
    }
    return true;
}

void ax25_callText(const struct ax25Address *a, char *buf, size_t len)
{
    if (a->ssid != 0)
        snprintf(buf, len, "%s-%u", a->call, a->ssid);
    else
        snprintf(buf, len, "%s", a->call);
}

void hdlcRx_init(struct hdlcRx *h)
{
    memset(h, 0, sizeof(*h));
}

size_t hdlcRx_bit(struct hdlcRx *h, uint8_t bit)
{
    size_t done = 0;

    h->shift = (uint8_t)((h->shift << 1) | (bit ? 1 : 0));

    /* A flag ends a frame and starts the next */
    if (h->shift == 0x7e) {
        if (h->inFrame && h->len >= MIN_FRAME + 2
            && ax25_fcs(h->buf, h->len - 2)
                   == (uint16_t)(h->buf[h->len - 2]
                                 | (h->buf[h->len - 1] << 8)))
            done = h->len - 2;
        h->inFrame = true;
        h->len = 0;
        h->bitbuf = 0x80;
        return done;
    }

    /* Seven ones: an abort, or no signal */
    if ((h->shift & 0x7f) == 0x7f) {
        h->inFrame = false;
        return 0;
    }
    if (!h->inFrame)
        return 0;

    /* A zero after five ones was stuffed in */
    if ((h->shift & 0x3f) == 0x3e)
        return 0;

    /* Bytes are sent least significant bit first */
    if (h->shift & 1)
        h->bitbuf |= 0x100;
    if (h->bitbuf & 1) {
        if (h->len >= sizeof(h->buf)) {
            h->inFrame = false;
            return 0;
        }
        h->buf[h->len++] = (uint8_t)(h->bitbuf >> 1);
        h->bitbuf = 0x80;
        return 0;
    }
    h->bitbuf >>= 1;
    return 0;
}

void hdlcTx_init(struct hdlcTx *h, const uint8_t *data, size_t len,
                 uint16_t flagsBefore, uint16_t flagsAfter)
{
    memset(h, 0, sizeof(*h));
    h->data = data;
    h->len = len;
    h->flagsBefore = flagsBefore;
    h->flagsAfter = flagsAfter;
}

int hdlcTx_next(struct hdlcTx *h)
{
    for (;;) {
        switch (h->part) {
            case 0:
            case 2: {
                uint16_t flags = (h->part == 0) ? h->flagsBefore :
                                                  h->flagsAfter;
                if (h->pos < (size_t)flags * 8) {
                    int bit = (0x7e >> (h->pos % 8)) & 1;
                    h->pos++;
                    return bit;
                }
                h->part++;
                h->pos = 0;
                h->ones = 0;
                break;
            }
            case 1:
                if (h->stuffing) {
                    h->stuffing = false;
                    h->ones = 0;
                    return 0;
                }
                if (h->pos < h->len * 8) {
                    int bit = (h->data[h->pos / 8] >> (h->pos % 8)) & 1;
                    h->pos++;
                    if (bit) {
                        if (++h->ones == 5)
                            h->stuffing = true;
                    } else {
                        h->ones = 0;
                    }
                    return bit;
                }
                h->part++;
                h->pos = 0;
                break;
            default:
                return -1;
        }
    }
}
