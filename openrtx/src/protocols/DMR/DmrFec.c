/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "protocols/DMR/DmrFec.h"

/** Number of bits set */
static int weight(uint32_t v)
{
    int n = 0;

    while (v != 0) {
        v &= v - 1;
        n++;
    }

    return n;
}

/**
 * Systematic parity of a cyclic code extended with an overall parity bit:
 * the remainder of data(x) x^deg by the generator, then one bit making the
 * codeword's weight even.
 */
static uint32_t cyclicParity(uint32_t data, uint32_t gen, int deg)
{
    uint32_t rem = data << deg;

    for (int i = 31; i >= deg; i--) {
        if (rem & (1ul << i))
            rem ^= gen << (i - deg);
    }

    return (rem << 1) | (uint32_t)((weight(data) + weight(rem)) & 1);
}

/* Golay(20,8): the Golay(23,12) code, extended and shortened */
#define GOLAY_GEN 0xc75 /* x^11 + x^10 + x^6 + x^5 + x^4 + x^2 + 1 */

uint32_t dmrFec_golay2087Encode(uint8_t data)
{
    return ((uint32_t)data << 12) | cyclicParity(data, GOLAY_GEN, 11);
}

int dmrFec_golay2087Decode(uint32_t code, uint8_t *data)
{
    int best = 21;
    uint8_t bestData = 0;

    /* 256 codewords: the nearest one */
    for (unsigned d = 0; d < 256; d++) {
        int dist = weight((code ^ dmrFec_golay2087Encode((uint8_t)d))
                          & 0xfffff);
        if (dist < best) {
            best = dist;
            bestData = (uint8_t)d;
        }
    }

    *data = bestData;
    return (best <= 3) ? best : -1;
}

/* QR(16,7,6): a (15,7) cyclic code, extended */
#define QR_GEN 0x139 /* x^8 + x^5 + x^4 + x^3 + 1 */

uint16_t dmrFec_qr1676Encode(uint8_t data)
{
    data &= 0x7f;
    return (uint16_t)(((uint32_t)data << 9) | cyclicParity(data, QR_GEN, 8));
}

int dmrFec_qr1676Decode(uint16_t code, uint8_t *data)
{
    int best = 17;
    uint8_t bestData = 0;

    for (unsigned d = 0; d < 128; d++) {
        int dist = weight(code ^ dmrFec_qr1676Encode((uint8_t)d));
        if (dist < best) {
            best = dist;
            bestData = (uint8_t)d;
        }
    }

    *data = bestData;
    return (best <= 2) ? best : -1;
}

/*
 * Hamming codes of the product codes, parity equations as in ETSI TS
 * 102 361-1 annex B: each returns the syndrome, and corrects a single
 * error.
 */
static uint8_t hamming15113(uint8_t *d, bool fix)
{
    static const uint8_t err[16] = {
        0xff, 11, 12, 8, 13, 5, 9, 3, 14, 0, 6, 1, 10, 7, 4, 2,
    };
    uint8_t c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    uint8_t c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    uint8_t c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    uint8_t c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];
    uint8_t s = (uint8_t)((c0 ^ d[11]) | (c1 ^ d[12]) << 1 | (c2 ^ d[13]) << 2
                          | (c3 ^ d[14]) << 3);

    if (fix && s != 0)
        d[err[s]] ^= 1;
    return s;
}

static uint8_t hamming1393(uint8_t *d, bool fix)
{
    static const uint8_t err[16] = {
        0xff, 9, 10, 6, 11, 3, 7, 1, 12, 0xff, 4, 0xff, 8, 5, 2, 0,
    };
    uint8_t c0 = d[0] ^ d[1] ^ d[3] ^ d[5] ^ d[6];
    uint8_t c1 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7];
    uint8_t c2 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    uint8_t c3 = d[0] ^ d[2] ^ d[4] ^ d[5] ^ d[8];
    uint8_t s = (uint8_t)((c0 ^ d[9]) | (c1 ^ d[10]) << 1 | (c2 ^ d[11]) << 2
                          | (c3 ^ d[12]) << 3);

    if (fix && s != 0) {
        if (err[s] == 0xff)
            return s; /* Not a single error */
        d[err[s]] ^= 1;
    }
    return s;
}

static uint8_t hamming16114(uint8_t *d, bool fix)
{
    uint8_t c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    uint8_t c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    uint8_t c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    uint8_t c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];
    uint8_t c4 = d[0] ^ d[2] ^ d[5] ^ d[6] ^ d[8] ^ d[9] ^ d[10];
    uint8_t s = (uint8_t)((c0 ^ d[11]) | (c1 ^ d[12]) << 1 | (c2 ^ d[13]) << 2
                          | (c3 ^ d[14]) << 3 | (c4 ^ d[15]) << 4);

    if (fix && s != 0) {
        /* Syndromes of a single error in bits 0 to 10 */
        static const uint8_t col[11] = {
            0x19, 0x0b, 0x1f, 0x07, 0x0e, 0x15, 0x1a, 0x0d, 0x13, 0x16, 0x1c,
        };
        for (int i = 0; i < 11; i++) {
            if (col[i] == s) {
                d[i] ^= 1;
                return 0;
            }
        }
        for (int i = 0; i < 5; i++) {
            if (s == (1u << i)) {
                d[11 + i] ^= 1;
                return 0;
            }
        }
    }
    return s;
}

/* Fill the parity of a codeword whose data bits are set */
static void encodeHamming(uint8_t *d, unsigned n,
                          uint8_t (*code)(uint8_t *, bool), unsigned k)
{
    for (unsigned i = k; i < n; i++)
        d[i] = 0;
    uint8_t s = code(d, false);
    for (unsigned i = 0; i < n - k; i++)
        d[k + i] = (s >> i) & 1;
}

/*
 * BPTC(196,96): a 13 x 15 matrix after the first, reserved, bit. Rows 0 to
 * 8 are Hamming(15,11) codewords, columns Hamming(13,9) codewords; the
 * transmitted bit n is matrix bit n * 181 mod 196.
 */
#define BPTC_BITS 196

bool dmrFec_bptc19696Decode(const uint8_t *bits, uint8_t out[12])
{
    uint8_t m[BPTC_BITS];
    uint8_t col[13];
    bool ok = false;

    for (unsigned a = 0; a < BPTC_BITS; a++)
        m[a] = bits[(a * 181) % BPTC_BITS] & 1;

    for (int pass = 0; pass < 5 && !ok; pass++) {
        ok = true;
        for (unsigned c = 0; c < 15; c++) {
            for (unsigned r = 0; r < 13; r++)
                col[r] = m[1 + c + r * 15];
            if (hamming1393(col, true) != 0) {
                ok = false;
                for (unsigned r = 0; r < 13; r++)
                    m[1 + c + r * 15] = col[r];
            }
        }
        for (unsigned r = 0; r < 9; r++) {
            if (hamming15113(&m[1 + r * 15], true) != 0)
                ok = false;
        }
    }

    /* Check after the last correction */
    ok = true;
    for (unsigned r = 0; r < 9; r++) {
        if (hamming15113(&m[1 + r * 15], false) != 0)
            ok = false;
    }
    for (unsigned c = 0; c < 15; c++) {
        for (unsigned r = 0; r < 13; r++)
            col[r] = m[1 + c + r * 15];
        if (hamming1393(col, false) != 0)
            ok = false;
    }

    /* Data: row 0 bits 3 to 10 (0 to 2 are reserved), rows 1 to 8 bits 0
     * to 10 */
    memset(out, 0, 12);
    unsigned n = 0;
    for (unsigned r = 0; r < 9; r++) {
        for (unsigned c = (r == 0) ? 3 : 0; c < 11; c++, n++) {
            if (m[1 + r * 15 + c])
                out[n / 8] |= (uint8_t)(0x80 >> (n % 8));
        }
    }

    return ok;
}

void dmrFec_bptc19696Encode(const uint8_t in[12], uint8_t *bits)
{
    uint8_t m[BPTC_BITS];
    uint8_t col[13];
    unsigned n = 0;

    memset(m, 0, sizeof(m));
    for (unsigned r = 0; r < 9; r++) {
        for (unsigned c = (r == 0) ? 3 : 0; c < 11; c++, n++)
            m[1 + r * 15 + c] = (in[n / 8] >> (7 - n % 8)) & 1;
        encodeHamming(&m[1 + r * 15], 15, hamming15113, 11);
    }
    for (unsigned c = 0; c < 15; c++) {
        for (unsigned r = 0; r < 9; r++)
            col[r] = m[1 + c + r * 15];
        encodeHamming(col, 13, hamming1393, 9);
        for (unsigned r = 9; r < 13; r++)
            m[1 + c + r * 15] = col[r];
    }

    for (unsigned a = 0; a < BPTC_BITS; a++)
        bits[(a * 181) % BPTC_BITS] = m[a];
}

/* GF(256), polynomial x^8 + x^4 + x^3 + x^2 + 1 */
static uint8_t gfMul(uint8_t a, uint8_t b)
{
    uint8_t p = 0;

    while (b != 0) {
        if (b & 1)
            p ^= a;
        b >>= 1;
        a = (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1d : 0));
    }

    return p;
}

void dmrFec_rs129Parity(const uint8_t lc[9], uint8_t parity[3])
{
    /* Generator (x + a)(x + a^2)(x + a^3), a = 2 */
    static const uint8_t gen[3] = { 64, 56, 14 };
    uint8_t reg[3] = { 0, 0, 0 };

    for (unsigned i = 0; i < 9; i++) {
        uint8_t f = lc[i] ^ reg[2];
        reg[2] = reg[1] ^ gfMul(gen[2], f);
        reg[1] = reg[0] ^ gfMul(gen[1], f);
        reg[0] = gfMul(gen[0], f);
    }

    parity[0] = reg[2];
    parity[1] = reg[1];
    parity[2] = reg[0];
}

bool dmrFec_rs129Check(const uint8_t lc[12])
{
    uint8_t p[3];

    dmrFec_rs129Parity(lc, p);
    return p[0] == lc[9] && p[1] == lc[10] && p[2] == lc[11];
}

/*
 * Embedded link control: an 8 x 16 matrix sent column by column. Rows 0 to
 * 6 are Hamming(16,11) codewords, row 7 the parity of each column. The 72
 * link control bits fill the data bits (11 of rows 0 and 1, 10 of rows 2
 * to 6), the 5-bit checksum the last data bit of rows 2 to 6.
 */
static const uint8_t embRowData[7] = { 11, 11, 10, 10, 10, 10, 10 };

/** Matrix bit of transmitted bit a */
static unsigned embIndex(unsigned a)
{
    return (a == 127) ? 127 : (a * 16) % 127;
}

/** The 5-bit checksum: sum of the 9 bytes, modulo 31 */
static uint8_t embChecksum(const uint8_t lc[9])
{
    unsigned total = 0;

    for (unsigned i = 0; i < 9; i++)
        total += lc[i];
    return (uint8_t)(total % 31);
}

bool dmrFec_embeddedLcDecode(const uint8_t raw[128], uint8_t lc[9])
{
    uint8_t m[128];
    unsigned n = 0;
    uint8_t cs = 0;

    for (unsigned a = 0; a < 128; a++)
        m[embIndex(a)] = raw[a] & 1;

    for (unsigned r = 0; r < 7; r++) {
        if (hamming16114(&m[r * 16], true) != 0)
            return false;
    }
    for (unsigned c = 0; c < 16; c++) {
        uint8_t p = 0;
        for (unsigned r = 0; r < 8; r++)
            p ^= m[r * 16 + c];
        if (p != 0)
            return false;
    }

    memset(lc, 0, 9);
    for (unsigned r = 0; r < 7; r++) {
        for (unsigned c = 0; c < embRowData[r]; c++, n++) {
            if (m[r * 16 + c])
                lc[n / 8] |= (uint8_t)(0x80 >> (n % 8));
        }
        if (r >= 2)
            cs = (uint8_t)((cs << 1) | m[r * 16 + 10]);
    }

    return cs == embChecksum(lc);
}

void dmrFec_embeddedLcEncode(const uint8_t lc[9], uint8_t raw[128])
{
    uint8_t m[128];
    unsigned n = 0;
    uint8_t cs = embChecksum(lc);

    memset(m, 0, sizeof(m));
    for (unsigned r = 0; r < 7; r++) {
        for (unsigned c = 0; c < embRowData[r]; c++, n++)
            m[r * 16 + c] = (lc[n / 8] >> (7 - n % 8)) & 1;
        if (r >= 2)
            m[r * 16 + 10] = (cs >> (6 - r)) & 1;
        encodeHamming(&m[r * 16], 16, hamming16114, 11);
    }
    for (unsigned c = 0; c < 16; c++) {
        uint8_t p = 0;
        for (unsigned r = 0; r < 7; r++)
            p ^= m[r * 16 + c];
        m[7 * 16 + c] = p;
    }

    for (unsigned a = 0; a < 128; a++)
        raw[a] = m[embIndex(a)];
}

uint16_t dmrFec_crcCcitt(const uint8_t *data, unsigned len)
{
    uint16_t crc = 0;

    for (unsigned i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }

    return (uint16_t)~crc;
}
