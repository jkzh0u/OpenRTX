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
 * Build the C62 firmware with -DC62_CHANNELS=scripts/c62/channels/ttc_tetra.h
 * Names show 12 characters on the main screen.
 *
 * Format: CPS_CHANNEL("Name", frequency in Hz, operating mode), then
 * TETRA_ID(SSI, "Name", listen) for talkgroups and radios.
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
 * Talkgroups and radios, from RadioReference's TTC talkgroup list (all
 * listed as clear, mode T): TETRA_ID(SSI, "Name", listen). Names replace
 * the numbers on the call screen. Set listen to true on the ones to hear:
 * the radio then plays and follows only those. All false: everything.
 */
/* Systemwide */
TETRA_ID(60002, "Red Alarm", false)
TETRA_ID(60101, "911 Announce", false)
TETRA_ID(101011, "Transit Control", false)
TETRA_ID(101117, "Janitor", false)
TETRA_ID(102716, "Transit Enforce1", false)
TETRA_ID(102740, "SpecialConstable", false)
TETRA_ID(102742, "Events", false)
TETRA_ID(120106, "PRESTO Techs", false)
/* Surface: streetcar, bus, Wheel-Trans */
TETRA_ID(101012, "Wheel Trans", false)
TETRA_ID(101013, "Surface Ch. 3", false)
TETRA_ID(101014, "Surface Ch. 4", false)
TETRA_ID(101015, "Surface Ch. 5 SC", false)
TETRA_ID(102727, "StreetcarControl", false)
TETRA_ID(102728, "Streetcar Sprvsr", false)
TETRA_ID(101151, "Leslie", false)
TETRA_ID(101154, "Russell", false)
TETRA_ID(102724, "Roncy", false)
TETRA_ID(101148, "Route Mgmt", false)
TETRA_ID(102734, "Bus Operations", false)
TETRA_ID(131071, "Control/Bus PA", false)
TETRA_ID(131074, "Control/Bus PA", false)
TETRA_ID(101155, "Birchmount", false)
TETRA_ID(101158, "Wilson", false)
TETRA_ID(101159, "Mt. Dennis", false)
TETRA_ID(101161, "Eglinton", false)
TETRA_ID(101162, "Malvern", false)
TETRA_ID(102719, "Incidents 1 Sub", false)
TETRA_ID(102720, "Incidents 2 Oth", false)
TETRA_ID(102721, "Incidents 3 Shut", false)
TETRA_ID(102725, "Fire Prevention", false)
/* Rapid transit */
TETRA_ID(100010, "Line1 Line Calls", false)
TETRA_ID(100019, "Line1 PA Calls", false)
TETRA_ID(102723, "Line1 YUS Tower", false)
TETRA_ID(100020, "Line2 Line Calls", false)
TETRA_ID(100029, "Line2 PA Calls", false)
TETRA_ID(102732, "Line2 B-D Tower", false)
TETRA_ID(102733, "Line2Kipling Twr", false)
TETRA_ID(106001, "PSA Bloor Stn", false)
TETRA_ID(100040, "Line4 Sheppard", false)
TETRA_ID(100050, "Line 5 Calls", false)
TETRA_ID(115000, "Line5 EglintonCT", false)
TETRA_ID(100060, "Line 6 F W", false)
TETRA_ID(116000, "Line 6 F W", false)
TETRA_ID(100002, "Wilson Yard", false)
TETRA_ID(100003, "Greenwood", false)
TETRA_ID(100004, "Davisville Yard", false)
TETRA_ID(100005, "McCowan Yard", false)
TETRA_ID(101114, "Stations 1", false)
TETRA_ID(102045, "WaysideMaintainr", false)
TETRA_ID(101132, "Wayside", false)
TETRA_ID(101133, "Wayside", false)
TETRA_ID(101101, "Signals Ch.1", false)
TETRA_ID(101102, "Signals Ch. 2", false)
TETRA_ID(102401, "Signal Crew", false)
TETRA_ID(102402, "Wayside", false)
TETRA_ID(102404, "Signals Crew", false)
TETRA_ID(102405, "Structure Crews", false)
TETRA_ID(102410, "Lubricators Z 10", false)
TETRA_ID(101006, "Track Maint", false)
TETRA_ID(101007, "Track Maint", false)
TETRA_ID(101001, "RTC Test/Work", false)
/* Line 1 trains (point to point with Transit Control) */
TETRA_ID(100112, "Run 112", false)
TETRA_ID(100117, "Run 117", false)
TETRA_ID(100121, "Run 121", false)
TETRA_ID(100124, "Run 124", false)
TETRA_ID(100136, "Run 136", false)
TETRA_ID(100141, "Run 141", false)
TETRA_ID(100162, "Run 162", false)
TETRA_ID(100187, "Run 187", false)
/* Line 2 trains (point to point): runs 201-249 off-peak, 250+ weekdays */
TETRA_ID(100201, "Run 201", false)
TETRA_ID(100202, "Run 202", false)
TETRA_ID(100203, "Run 203", false)
TETRA_ID(100204, "Run 204", false)
TETRA_ID(100205, "Run 205", false)
TETRA_ID(100206, "Run 206", false)
TETRA_ID(100207, "Run 207", false)
TETRA_ID(100208, "Run 208", false)
TETRA_ID(100209, "Run 209", false)
TETRA_ID(100210, "Run 210", false)
TETRA_ID(100211, "Run 211", false)
TETRA_ID(100212, "Run 212", false)
TETRA_ID(100213, "Run 213", false)
TETRA_ID(100214, "Run 214", false)
TETRA_ID(100215, "Run 215", false)
TETRA_ID(100216, "Run 216", false)
TETRA_ID(100217, "Run 217", false)
TETRA_ID(100218, "Run 218", false)
TETRA_ID(100219, "Run 219", false)
TETRA_ID(100220, "Run 220", false)
TETRA_ID(100221, "Run 221", false)
TETRA_ID(100222, "Run 222", false)
TETRA_ID(100223, "Run 223", false)
TETRA_ID(100224, "Run 224", false)
TETRA_ID(100225, "Run 225", false)
TETRA_ID(100226, "Run 226", false)
TETRA_ID(100251, "Run 251", false)
TETRA_ID(100252, "Run 252", false)
TETRA_ID(100253, "Run 253", false)
TETRA_ID(100254, "Run 254", false)
TETRA_ID(100255, "Run 255", false)
TETRA_ID(100256, "Run 256", false)
TETRA_ID(100257, "Run 257", false)
TETRA_ID(100258, "Run 258", false)
TETRA_ID(100259, "Run 259", false)
TETRA_ID(100260, "Run 260", false)
TETRA_ID(100261, "Run 261", false)
TETRA_ID(100262, "Run 262", false)
TETRA_ID(100263, "Run 263", false)
TETRA_ID(100264, "Run 264", false)
TETRA_ID(100265, "Run 265", false)
TETRA_ID(100266, "Run 266", false)
TETRA_ID(100267, "Run 267", false)
TETRA_ID(100268, "Run 268", false)
TETRA_ID(100269, "Run 269", false)
TETRA_ID(100270, "Run 270", false)
TETRA_ID(100271, "Run 271", false)
TETRA_ID(100272, "Run 272", false)
TETRA_ID(100273, "Run 273", false)
TETRA_ID(100274, "Run 274", false)
TETRA_ID(100275, "Run 275", false)
TETRA_ID(100276, "Run 276", false)
TETRA_ID(100277, "Run 277", false)
TETRA_ID(100287, "Run 287", false)
TETRA_ID(100290, "Run 290", false)
TETRA_ID(100291, "Run 291", false)
TETRA_ID(100296, "Run 296", false)
TETRA_ID(100297, "Run 297 OTC", false)
TETRA_ID(100298, "Run 298 OTC", false)
TETRA_ID(100299, "Run 299", false)
/* Line 5 trains (point to point) */
TETRA_ID(100515, "Run 515", false)
/* Line 6 trains (point to point) */
TETRA_ID(100604, "Run 604", false)
TETRA_ID(100606, "Run 606", false)
TETRA_ID(100617, "Run 617", false)
/* Radio technicians */
TETRA_ID(102701, "Wayside Techs", false)
TETRA_ID(102702, "Wayside Techs", false)
TETRA_ID(102703, "Wayside Techs", false)
TETRA_ID(102704, "Wayside Techs", false)
