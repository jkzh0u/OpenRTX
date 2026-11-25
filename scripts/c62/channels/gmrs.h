/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * GMRS channel list: the 22 GMRS channels, the 8 repeater channels and a
 * home channel. Build and flash it next to the firmware (scripts/c62/
 * README.md, section 6):
 *
 *     python3 scripts/chlist.py scripts/c62/channels/gmrs.h -o channels.bin
 *     cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
 *         0x2c0000 channels.bin
 *
 * FM_CHANNEL: name, receive and transmit frequency in Hz, width, receive
 * and transmit tone (CTCSS in tenths of Hz, 0 for none).
 */

/*
 * Channels 1 to 7, shared with FRS, wide. Channels 8 to 14, shared with
 * FRS, narrow and low power only. Channels 15 to 22, wide.
 */
FM_CHANNEL("GMRS 1", 462562500, 462562500, 25000, 0, 0)
FM_CHANNEL("GMRS 2", 462587500, 462587500, 25000, 0, 0)
FM_CHANNEL("GMRS 3", 462612500, 462612500, 25000, 0, 0)
FM_CHANNEL("GMRS 4", 462637500, 462637500, 25000, 0, 0)
FM_CHANNEL("GMRS 5", 462662500, 462662500, 25000, 0, 0)
FM_CHANNEL("GMRS 6", 462687500, 462687500, 25000, 0, 0)
FM_CHANNEL("GMRS 7", 462712500, 462712500, 25000, 0, 0)
FM_CHANNEL("GMRS 8", 467562500, 467562500, 12500, 0, 0)
FM_CHANNEL("GMRS 9", 467587500, 467587500, 12500, 0, 0)
FM_CHANNEL("GMRS 10", 467612500, 467612500, 12500, 0, 0)
FM_CHANNEL("GMRS 11", 467637500, 467637500, 12500, 0, 0)
FM_CHANNEL("GMRS 12", 467662500, 467662500, 12500, 0, 0)
FM_CHANNEL("GMRS 13", 467687500, 467687500, 12500, 0, 0)
FM_CHANNEL("GMRS 14", 467712500, 467712500, 12500, 0, 0)
FM_CHANNEL("GMRS 15", 462550000, 462550000, 25000, 0, 0)
FM_CHANNEL("GMRS 16", 462575000, 462575000, 25000, 0, 0)
FM_CHANNEL("GMRS 17", 462600000, 462600000, 25000, 0, 0)
FM_CHANNEL("GMRS 18", 462625000, 462625000, 25000, 0, 0)
FM_CHANNEL("GMRS 19", 462650000, 462650000, 25000, 0, 0)
FM_CHANNEL("GMRS 20", 462675000, 462675000, 25000, 0, 0)
FM_CHANNEL("GMRS 21", 462700000, 462700000, 25000, 0, 0)
FM_CHANNEL("GMRS 22", 462725000, 462725000, 25000, 0, 0)

/*
 * Repeater channels: receive the repeater's output on 15 to 22, transmit
 * to its input 5 MHz up. Most repeaters need a tone: set it with
 * Settings > FM > TX tone, or put it in here (e.g. 1413 for 141.3 Hz).
 */
FM_CHANNEL("GMRS 15R", 462550000, 467550000, 25000, 0, 0)
FM_CHANNEL("GMRS 16R", 462575000, 467575000, 25000, 0, 0)
FM_CHANNEL("GMRS 17R", 462600000, 467600000, 25000, 0, 0)
FM_CHANNEL("GMRS 18R", 462625000, 467625000, 25000, 0, 0)
FM_CHANNEL("GMRS 19R", 462650000, 467650000, 25000, 0, 0)
FM_CHANNEL("GMRS 20R", 462675000, 467675000, 25000, 0, 0)
FM_CHANNEL("GMRS 21R", 462700000, 467700000, 25000, 0, 0)
FM_CHANNEL("GMRS 22R", 462725000, 467725000, 25000, 0, 0)

/* Home channel: simplex on 462.700 MHz (GMRS 21), 241.8 Hz both ways */
FM_CHANNEL("Home Channel", 462700000, 462700000, 25000, 2418, 2418)
