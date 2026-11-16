/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Toronto Transit Commission TETRA network (MCC 302, MNC 2013): the
 * carriers RadioReference marks as control channels, three per site.
 * Source: https://www.radioreference.com/db/sid/9380 (updated 8 Sep 2026).
 * The underground site, with no control channel listed, is left out.
 *
 * Build the C62 firmware with -DC62_CHANNELS=scripts/c62/channels/ttc_tetra.h
 * Names show 12 characters on the main screen.
 *
 * Format: CPS_CHANNEL("Name", frequency in Hz, operating mode)
 */

/* Site 27 */
CPS_CHANNEL("CN Tower 1", 422662500, OPMODE_TETRA)
CPS_CHANNEL("CN Tower 2", 423412500, OPMODE_TETRA)
CPS_CHANNEL("CN Tower 3", 423512500, OPMODE_TETRA)
/* Site 28 */
CPS_CHANNEL("Finch 1", 422687500, OPMODE_TETRA)
CPS_CHANNEL("Finch 2", 423387500, OPMODE_TETRA)
CPS_CHANNEL("Finch 3", 423562500, OPMODE_TETRA)
/* Site 29 */
CPS_CHANNEL("Shipp 1", 412962500, OPMODE_TETRA)
CPS_CHANNEL("Shipp 2", 423612500, OPMODE_TETRA)
CPS_CHANNEL("Shipp 3", 423712500, OPMODE_TETRA)
/* Site 30 */
CPS_CHANNEL("Malvern 1", 423487500, OPMODE_TETRA)
CPS_CHANNEL("Malvern 2", 423587500, OPMODE_TETRA)
CPS_CHANNEL("Malvern 3", 423762500, OPMODE_TETRA)
/* Site 31 */
CPS_CHANNEL("Greenwood 1", 412062500, OPMODE_TETRA)
CPS_CHANNEL("Greenwood 2", 412212500, OPMODE_TETRA)
CPS_CHANNEL("Greenwood 3", 412612500, OPMODE_TETRA)
/* Site 32 */
CPS_CHANNEL("Hillcrest 1", 412112500, OPMODE_TETRA)
CPS_CHANNEL("Hillcrest 2", 423287500, OPMODE_TETRA)
CPS_CHANNEL("Hillcrest 3", 423687500, OPMODE_TETRA)
/* Site 33 */
CPS_CHANNEL("Wilson 1", 413187500, OPMODE_TETRA)
CPS_CHANNEL("Wilson 2", 423362500, OPMODE_TETRA)
CPS_CHANNEL("Wilson 3", 423462500, OPMODE_TETRA)
/* Site 34; 412.0375 is listed for site 35 as well */
CPS_CHANNEL("McBrien 1", 412037500, OPMODE_TETRA)
CPS_CHANNEL("McBrien 2", 423537500, OPMODE_TETRA)
CPS_CHANNEL("McBrien 3", 423662500, OPMODE_TETRA)
/* Site 35 */
CPS_CHANNEL("Arrow 1", 412037500, OPMODE_TETRA)
CPS_CHANNEL("Arrow 2", 423312500, OPMODE_TETRA)
CPS_CHANNEL("Arrow 3", 423737500, OPMODE_TETRA)
/* Site 36 */
CPS_CHANNEL("Birchmount 1", 423337500, OPMODE_TETRA)
CPS_CHANNEL("Birchmount 2", 423437500, OPMODE_TETRA)
CPS_CHANNEL("Birchmount 3", 423637500, OPMODE_TETRA)
