/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_AX25_H
#define OPENRTX_AX25_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AX.25 UI frames, as APRS uses them (data link layer): addresses, the
 * frame check sequence, and HDLC framing with bit stuffing.
 */

/** Digipeaters in a path, at most */
#define AX25_MAX_DIGIS 8
/** Information field, at most */
#define AX25_MAX_INFO 256
/** A whole frame with its check sequence, at most */
#define AX25_MAX_FRAME (7 * (2 + AX25_MAX_DIGIS) + 2 + AX25_MAX_INFO + 2)

struct ax25Address {
    char call[7];  /**< Callsign, up to 6 characters */
    uint8_t ssid;  /**< 0 to 15 */
    bool repeated; /**< A digipeater that has repeated the frame */
};

struct ax25Frame {
    struct ax25Address dst;
    struct ax25Address src;
    struct ax25Address digi[AX25_MAX_DIGIS];
    uint8_t ndigi;
    uint8_t control; /**< 0x03 for a UI frame */
    uint8_t pid;     /**< 0xF0: no layer 3 */
    uint8_t info[AX25_MAX_INFO];
    uint16_t infoLen;
};

/**
 * @return the frame check sequence (CRC-16/X.25) of some bytes.
 */
uint16_t ax25_fcs(const uint8_t *data, size_t len);

/**
 * Read a frame.
 *
 * @param data: the frame, without its check sequence.
 * @param len: its length.
 * @param out: the frame read.
 * @return false if it is not a frame.
 */
bool ax25_decode(const uint8_t *data, size_t len, struct ax25Frame *out);

/**
 * Write a frame, with its check sequence.
 *
 * @param f: the frame.
 * @param out: where to write it.
 * @param max: room in out.
 * @return its length, 0 if it does not fit.
 */
size_t ax25_encode(const struct ax25Frame *f, uint8_t *out, size_t max);

/**
 * Read a callsign as written: "VA3ABC-7".
 *
 * @return false if it is not one.
 */
bool ax25_parseCall(const char *text, struct ax25Address *out);

/**
 * Write a callsign: "VA3ABC-7", "VA3ABC" for SSID 0.
 */
void ax25_callText(const struct ax25Address *a, char *buf, size_t len);

/*
 * HDLC receive: bits after NRZI decoding in, frames with a good check
 * sequence out.
 */
struct hdlcRx {
    uint8_t buf[AX25_MAX_FRAME];
    uint16_t len;
    uint16_t bitbuf;
    uint8_t shift;
    bool inFrame;
};

void hdlcRx_init(struct hdlcRx *h);

/**
 * @param h: receiver.
 * @param bit: the next bit.
 * @return the length of a frame that has just ended with a good check
 * sequence, without it (in h->buf), else 0.
 */
size_t hdlcRx_bit(struct hdlcRx *h, uint8_t bit);

/*
 * HDLC transmit: flags, the frame with its zeros stuffed in, flags. Bits
 * before NRZI encoding.
 */
struct hdlcTx {
    const uint8_t *data;
    size_t len;
    uint16_t flagsBefore;
    uint16_t flagsAfter;
    size_t pos;    /**< Bit position in the part being sent */
    uint8_t part;  /**< 0 flags before, 1 frame, 2 flags after, 3 done */
    uint8_t ones;  /**< Ones in a row in the frame */
    bool stuffing; /**< A stuffed zero comes next */
};

/**
 * @param data: the frame with its check sequence, kept until the end.
 */
void hdlcTx_init(struct hdlcTx *h, const uint8_t *data, size_t len,
                 uint16_t flagsBefore, uint16_t flagsAfter);

/** @return the next bit, or -1 at the end. */
int hdlcTx_next(struct hdlcTx *h);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_AX25_H */
