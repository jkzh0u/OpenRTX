/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Example channel list: copy this file, put in your own channels, then
 * build the image and flash it next to the firmware (scripts/c62/README.md,
 * section 6):
 *
 *     python3 scripts/chlist.py my_channels.h -o channels.bin
 *     cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
 *         0x2c0000 channels.bin
 *
 * One entry per line, C comments allowed. Names: ASCII; 12 characters show
 * on the main screen for channels, 16 for talkgroups.
 */

/*
 * Channels, in memory mode order: name, frequency in Hz, and OPMODE_FM,
 * OPMODE_M17 or OPMODE_TETRA. Receive only.
 */
CPS_CHANNEL("NOAA WX 1", 162550000, OPMODE_FM)
CPS_CHANNEL("M17 Simplex", 433475000, OPMODE_M17)
CPS_CHANNEL("TETRA Site 1", 390012500, OPMODE_TETRA)
CPS_CHANNEL("TETRA Site 2", 390037500, OPMODE_TETRA)

/*
 * TETRA talkgroup filters, chosen on the radio with # and *, in this
 * order. "All" is always there first. Up to 30.
 */
TETRA_FILTER(OPS, "Operations")
TETRA_FILTER(MAINT, "Maintenance")

/*
 * TETRA names: SSI (decimal), name, and the filters it belongs to: TF() of
 * each joined with |, 0 for none, TF_EVERY for all. Add TF_ALARM for an
 * alarm talkgroup: its calls turn the screen red and beep. Add
 * TF_PRIORITY(n), n from 1 (lowest) to 15, for a talkgroup that comes
 * first: its calls take over from calls of lower priority, and after one
 * the radio waits 10 s before following a lower priority call elsewhere.
 * Alarm talkgroups have priority 15 unless given another.
 */
TETRA_ID(1001, "Dispatch", TF(OPS) | TF_PRIORITY(5))
TETRA_ID(1002, "Ops Talk", TF(OPS))
TETRA_ID(2001, "Maint Crew", TF(MAINT))
TETRA_ID(9999, "Emergency", TF_EVERY | TF_ALARM)

/*
 * A range of radios named from a numbering plan: first SSI, last SSI, a
 * name with one %u that gets the SSI minus the base, the base, filters.
 * Here 500123 shows as "Unit 123". Entries of their own come first.
 */
TETRA_ID_RANGE(500001, 500999, "Unit %u", 500000, TF(OPS))

/* Location areas, shown as the place the radio is */
TETRA_LA(1, "Downtown")
TETRA_LA(2, "Airport")
