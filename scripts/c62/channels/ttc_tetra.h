/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Toronto Transit Commission TETRA network (MCC 302, MNC 2013): the
 * carriers RadioReference marks as control channels, three per site.
 * Source: https://www.radioreference.com/db/sid/9380 (updated 8 Sep 2026).
 *
 * Then the 13 underground zones of the subway, each a stretch of line
 * between two stations with its own control channel, from
 * https://adryd.com/pages/2025-03-11-ttc-radio-zones/ (updated 30 Nov 2025).
 * Zones reuse four frequencies; each also has a secondary carrier 0.1 MHz
 * above, which together match the eight frequencies RadioReference lists
 * for its underground site.
 *
 * An example of a full list. Build its image and flash it next to the
 * firmware (scripts/c62/README.md, section 6):
 *
 *     python3 scripts/chlist.py scripts/c62/channels/ttc_tetra.h \
 *         -o channels.bin
 *     cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
 *         0x2c0000 channels.bin
 *
 * Names show 12 characters on the main screen.
 *
 * Format: CPS_CHANNEL("Name", frequency in Hz, operating mode), then the
 * talkgroup filters and TETRA_ID(SSI, "Name", filters) for talkgroups and
 * radios, see openrtx/include/protocols/TETRA/TetraIds.h.
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
/* Zone 1: Kipling to Old Mill */
CPS_CHANNEL("Z1 Kipling", 422325000, OPMODE_TETRA)
/* Zone 2: Old Mill to Ossington */
CPS_CHANNEL("Z2 Keele", 422525000, OPMODE_TETRA)
/* Zone 3: Ossington to Chester */
CPS_CHANNEL("Z3 Bay", 422262500, OPMODE_TETRA)
/* Zone 4: Chester to Victoria Park */
CPS_CHANNEL("Z4 Greenwood", 422462500, OPMODE_TETRA)
/* Zone 5: Victoria Park to Kennedy */
CPS_CHANNEL("Z5 Kennedy", 422525000, OPMODE_TETRA)
/* Zone 6: York University to Lawrence West */
CPS_CHANNEL("Z6 Downsview", 422525000, OPMODE_TETRA)
/* Zone 7: Lawrence West to St. George */
CPS_CHANNEL("Z7 StClair W", 422462500, OPMODE_TETRA)
/* Zone 8: St. George to Bloor-Yonge */
CPS_CHANNEL("Z8 Union", 422325000, OPMODE_TETRA)
/* Zone 9: Bloor-Yonge to Lawrence */
CPS_CHANNEL("Z9 Davisvle", 422525000, OPMODE_TETRA)
/* Zone 10: Lawrence to Finch */
CPS_CHANNEL("Z10 Finch", 422262500, OPMODE_TETRA)
/* Zone 11: Sheppard-Yonge to Bessarion */
CPS_CHANNEL("Z11 Sheppard", 422462500, OPMODE_TETRA)
/* Zone 12: Bessarion to Don Mills */
CPS_CHANNEL("Z12 DonMills", 422325000, OPMODE_TETRA)
/* Zone 13: Vaughan Metropolitan Centre to York University */
CPS_CHANNEL("Z13 VMC", 422262500, OPMODE_TETRA)


/*
 * Talkgroup filters, in the order # steps through them on the radio (*
 * goes back). "All" comes first and is built in. "Riding": what to hear
 * on a train, the line calls of every line and Transit Control.
 */
TETRA_FILTER(RIDING, "Riding")
TETRA_FILTER(SUBWAY, "Subway")
TETRA_FILTER(LINE1, "Line 1")
TETRA_FILTER(LINE2, "Line 2")
TETRA_FILTER(LINE4, "Line 4")
TETRA_FILTER(LINE5, "Line 5")
TETRA_FILTER(LINE6, "Line 6")
TETRA_FILTER(CONTROL, "Control")
TETRA_FILTER(SURFACE, "Surface")
TETRA_FILTER(STREETCAR, "Streetcar")
TETRA_FILTER(BUS, "Bus")
TETRA_FILTER(MAINT, "Maintenance")
TETRA_FILTER(SECURITY, "Security")

/*
 * Talkgroups and radios, from RadioReference's TTC talkgroup list (all
 * listed as clear, mode T): TETRA_ID(SSI, "Name", filters). Names replace
 * the numbers on the radio's screen; filters are TF() of each filter the
 * identity belongs to, TF_EVERY for all of them.
 */
/*
 * Location areas, shown as the radio's location, once known: the cell
 * screen shows LA <n>. For example:
 *
 * TETRA_LA(39, "Eglinton east")
 */

/* Systemwide: alarms come through every filter, with an alert */
TETRA_ID(60002, "Red Alarm", TF_EVERY | TF_ALARM)
TETRA_ID(60101, "911 Announce", TF_EVERY | TF_ALARM)
TETRA_ID(101011, "Transit Control", TF(RIDING) | TF(CONTROL) | TF_PRIORITY(5))
TETRA_ID(101117, "Janitor", TF(MAINT))
TETRA_ID(102716, "Transit Enforce1", TF(SECURITY))
TETRA_ID(102740, "SpecialConstable", TF(SECURITY))
TETRA_ID(102742, "Events", TF(CONTROL))
TETRA_ID(120106, "PRESTO Techs", TF(MAINT))
/* Surface: streetcar, bus, Wheel-Trans */
TETRA_ID(101012, "Wheel Trans", TF(SURFACE) | TF(BUS))
TETRA_ID(101013, "Surface Ch. 3", TF(SURFACE))
TETRA_ID(101014, "Surface Ch. 4", TF(SURFACE))
TETRA_ID(101015, "Surface Ch. 5 SC", TF(SURFACE) | TF(STREETCAR))
TETRA_ID(102727, "StreetcarControl", TF(SURFACE) | TF(STREETCAR))
TETRA_ID(102728, "Streetcar Sprvsr", TF(SURFACE) | TF(STREETCAR))
TETRA_ID(101151, "Leslie", TF(SURFACE) | TF(STREETCAR))
TETRA_ID(101154, "Russell", TF(SURFACE) | TF(STREETCAR))
TETRA_ID(102724, "Roncy", TF(SURFACE) | TF(STREETCAR))
TETRA_ID(101148, "Route Mgmt", TF(SURFACE))
TETRA_ID(102734, "Bus Operations", TF(SURFACE) | TF(BUS))
TETRA_ID(131071, "Control/Bus PA", TF(SURFACE) | TF(BUS))
TETRA_ID(131074, "Control/Bus PA", TF(SURFACE) | TF(BUS))
TETRA_ID(101155, "Birchmount", TF(SURFACE) | TF(BUS))
TETRA_ID(101158, "Wilson", TF(SURFACE) | TF(BUS))
TETRA_ID(101159, "Mt. Dennis", TF(SURFACE) | TF(BUS))
TETRA_ID(101161, "Eglinton", TF(SURFACE) | TF(BUS))
TETRA_ID(101162, "Malvern", TF(SURFACE) | TF(BUS))
TETRA_ID(102719, "Incidents 1 Sub", TF(RIDING) | TF(CONTROL) | TF(SUBWAY))
TETRA_ID(102720, "Incidents 2 Oth", TF(CONTROL) | TF(SURFACE))
TETRA_ID(102721, "Incidents 3 Shut", TF(CONTROL) | TF(SURFACE) | TF(BUS))
TETRA_ID(102725, "Fire Prevention", TF(MAINT))
/* Rapid transit: lines 1, 2 and 4 are subway, 5 and 6 light rail */
TETRA_ID(100010, "Line1 Line Calls", TF(RIDING) | TF(SUBWAY) | TF(LINE1))
TETRA_ID(100019, "Line1 PA Calls", TF(SUBWAY) | TF(LINE1))
TETRA_ID(102723, "Line1 YUS Tower", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100020, "Line2 Line Calls", TF(RIDING) | TF(SUBWAY) | TF(LINE2))
TETRA_ID(100029, "Line2 PA Calls", TF(SUBWAY) | TF(LINE2))
TETRA_ID(102732, "Line2 B-D Tower", TF(SUBWAY) | TF(LINE2))
TETRA_ID(102733, "Line2Kipling Twr", TF(SUBWAY) | TF(LINE2))
TETRA_ID(106001, "PSA Bloor Stn", TF(SUBWAY) | TF(LINE1) | TF(LINE2))
TETRA_ID(100040, "Line4 Sheppard", TF(RIDING) | TF(SUBWAY) | TF(LINE4))
TETRA_ID(100050, "Line 5 Calls", TF(RIDING) | TF(SUBWAY) | TF(LINE5))
TETRA_ID(115000, "Line5 EglintonCT", TF(RIDING) | TF(SUBWAY) | TF(LINE5))
TETRA_ID(100060, "Line 6 F W", TF(RIDING) | TF(SUBWAY) | TF(LINE6))
TETRA_ID(116000, "Line 6 F W", TF(RIDING) | TF(SUBWAY) | TF(LINE6))
TETRA_ID(100002, "Wilson Yard", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100003, "Greenwood", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100004, "Davisville Yard", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100005, "McCowan Yard", TF(SUBWAY))
TETRA_ID(101114, "Stations 1", TF(SUBWAY))
/* Rapid transit maintenance */
TETRA_ID(102045, "WaysideMaintainr", TF(MAINT))
TETRA_ID(101132, "Wayside", TF(MAINT))
TETRA_ID(101133, "Wayside", TF(MAINT))
TETRA_ID(101101, "Signals Ch.1", TF(MAINT))
TETRA_ID(101102, "Signals Ch. 2", TF(MAINT))
TETRA_ID(102401, "Signal Crew", TF(MAINT))
TETRA_ID(102402, "Wayside", TF(MAINT))
TETRA_ID(102404, "Signals Crew", TF(MAINT))
TETRA_ID(102405, "Structure Crews", TF(MAINT))
TETRA_ID(102410, "Lubricators Z 10", TF(MAINT))
TETRA_ID(101006, "Track Maint", TF(MAINT))
TETRA_ID(101007, "Track Maint", TF(MAINT))
TETRA_ID(101001, "RTC Test/Work", TF(MAINT))
/*
 * Every train run: its radio is 100000 plus the run number, the first
 * digit of which is the line. Runs listed below keep their own entries.
 * Line 4 is assumed to follow the pattern: no run of it is listed.
 */
TETRA_ID_RANGE(100101, 100199, "Run %u", 100000, TF(SUBWAY) | TF(LINE1))
TETRA_ID_RANGE(100201, 100299, "Run %u", 100000, TF(SUBWAY) | TF(LINE2))
TETRA_ID_RANGE(100401, 100499, "Run %u", 100000, TF(SUBWAY) | TF(LINE4))
TETRA_ID_RANGE(100501, 100599, "Run %u", 100000, TF(SUBWAY) | TF(LINE5))
TETRA_ID_RANGE(100601, 100699, "Run %u", 100000, TF(SUBWAY) | TF(LINE6))

/* Line 1 trains: point to point with Transit Control */
TETRA_ID(100112, "Run 112", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100117, "Run 117", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100121, "Run 121", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100124, "Run 124", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100136, "Run 136", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100141, "Run 141", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100162, "Run 162", TF(SUBWAY) | TF(LINE1))
TETRA_ID(100187, "Run 187", TF(SUBWAY) | TF(LINE1))
/* Line 2 trains: point to point; runs 201-249 off-peak, 250+ weekdays */
TETRA_ID(100201, "Run 201", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100202, "Run 202", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100203, "Run 203", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100204, "Run 204", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100205, "Run 205", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100206, "Run 206", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100207, "Run 207", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100208, "Run 208", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100209, "Run 209", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100210, "Run 210", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100211, "Run 211", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100212, "Run 212", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100213, "Run 213", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100214, "Run 214", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100215, "Run 215", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100216, "Run 216", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100217, "Run 217", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100218, "Run 218", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100219, "Run 219", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100220, "Run 220", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100221, "Run 221", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100222, "Run 222", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100223, "Run 223", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100224, "Run 224", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100225, "Run 225", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100226, "Run 226", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100251, "Run 251", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100252, "Run 252", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100253, "Run 253", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100254, "Run 254", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100255, "Run 255", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100256, "Run 256", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100257, "Run 257", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100258, "Run 258", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100259, "Run 259", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100260, "Run 260", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100261, "Run 261", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100262, "Run 262", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100263, "Run 263", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100264, "Run 264", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100265, "Run 265", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100266, "Run 266", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100267, "Run 267", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100268, "Run 268", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100269, "Run 269", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100270, "Run 270", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100271, "Run 271", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100272, "Run 272", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100273, "Run 273", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100274, "Run 274", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100275, "Run 275", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100276, "Run 276", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100277, "Run 277", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100287, "Run 287", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100290, "Run 290", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100291, "Run 291", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100296, "Run 296", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100297, "Run 297 OTC", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100298, "Run 298 OTC", TF(SUBWAY) | TF(LINE2))
TETRA_ID(100299, "Run 299", TF(SUBWAY) | TF(LINE2))
/* Line 5 trains: point to point */
TETRA_ID(100515, "Run 515", TF(SUBWAY) | TF(LINE5))
/* Line 6 trains: point to point */
TETRA_ID(100604, "Run 604", TF(SUBWAY) | TF(LINE6))
TETRA_ID(100606, "Run 606", TF(SUBWAY) | TF(LINE6))
TETRA_ID(100617, "Run 617", TF(SUBWAY) | TF(LINE6))
/* Radio technicians */
TETRA_ID(102701, "Wayside Techs", TF(MAINT))
TETRA_ID(102702, "Wayside Techs", TF(MAINT))
TETRA_ID(102703, "Wayside Techs", TF(MAINT))
TETRA_ID(102704, "Wayside Techs", TF(MAINT))
