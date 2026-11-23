/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_DMR_FEC_H
#define OPENRTX_DMR_FEC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DMR forward error correction (ETSI TS 102 361-1, annex B), for a
 * receiver: the codes of the slot type, the EMB, the full link control
 * (BPTC(196,96) and RS(12,9)) and the embedded link control. Encoders are
 * included for tests.
 *
 * Bit arrays hold one bit per byte (0 or 1), first transmitted first.
 */

/**
 * Golay(20,8) codeword of the slot type: the 8 data bits, then 12 parity
 * bits.
 *
 * @param data: colour code (high nibble) and data type (low nibble).
 * @return the 20-bit codeword, data in bits 19..12.
 */
uint32_t dmrFec_golay2087Encode(uint8_t data);

/**
 * Decode a Golay(20,8) codeword (corrects up to 3 bit errors).
 *
 * @param code: 20-bit codeword as received.
 * @param data: decoded data.
 * @return number of bit errors corrected, or -1 if too many.
 */
int dmrFec_golay2087Decode(uint32_t code, uint8_t *data);

/**
 * Quadratic residue (16,7,6) codeword of the EMB.
 *
 * @param data: colour code, PI and LCSS in the 7 low bits.
 * @return the 16-bit codeword, data in bits 15..9.
 */
uint16_t dmrFec_qr1676Encode(uint8_t data);

/**
 * Decode a QR(16,7,6) codeword (corrects up to 2 bit errors).
 *
 * @param code: 16-bit codeword as received.
 * @param data: decoded 7 data bits.
 * @return number of bit errors corrected, or -1 if too many.
 */
int dmrFec_qr1676Decode(uint16_t code, uint8_t *data);

/**
 * Decode the BPTC(196,96) of a data burst.
 *
 * @param bits: the 196 interleaved bits (the two 98-bit halves of the
 * burst's information field).
 * @param out: the 96 data bits as 12 bytes, first bit in the MSB.
 * @return true if every row and column is a codeword after correction.
 */
bool dmrFec_bptc19696Decode(const uint8_t *bits, uint8_t out[12]);

/**
 * Encode 96 data bits with the BPTC(196,96).
 *
 * @param in: 12 bytes.
 * @param bits: the 196 interleaved bits.
 */
void dmrFec_bptc19696Encode(const uint8_t in[12], uint8_t *bits);

/**
 * Reed-Solomon (12,9) parity of a full link control, unmasked.
 *
 * @param lc: the 9 link control bytes.
 * @param parity: the 3 parity bytes in transmission order (bytes 9 to 11).
 */
void dmrFec_rs129Parity(const uint8_t lc[9], uint8_t parity[3]);

/**
 * Check a full link control and its parity.
 *
 * @param lc: 12 bytes: link control, then parity with the CRC mask of the
 * burst type removed.
 * @return true if the parity matches.
 */
bool dmrFec_rs129Check(const uint8_t lc[12]);

/**
 * Decode the embedded link control of a voice superframe.
 *
 * @param raw: the 128 bits of the four embedded signalling fragments, in
 * order (bursts B to E).
 * @param lc: the 9 link control bytes.
 * @return true if the Hamming codes, column parity and checksum pass.
 */
bool dmrFec_embeddedLcDecode(const uint8_t raw[128], uint8_t lc[9]);

/**
 * Encode a link control into the four embedded signalling fragments.
 *
 * @param lc: the 9 link control bytes.
 * @param raw: 128 bits, 32 for each of bursts B to E.
 */
void dmrFec_embeddedLcEncode(const uint8_t lc[9], uint8_t raw[128]);

/**
 * CRC-CCITT of a CSBK or data header (polynomial 0x1021, initial value 0,
 * inverted).
 *
 * @param data: bytes.
 * @param len: number of bytes.
 * @return CRC, before the mask of the burst type.
 */
uint16_t dmrFec_crcCcitt(const uint8_t *data, unsigned len);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_DMR_FEC_H */
