/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_IDS_H
#define OPENRTX_TETRA_IDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Known TETRA identities (SSIs of talkgroups or radios), built into the
 * firmware: names to show instead of numbers, and filters, sets of
 * identities to listen to, chosen on the radio.
 *
 * TETRA_ID_LIST names a header that first defines the filters, in the
 * order the radio steps through them, then lists the identities:
 *
 *     TETRA_FILTER(SUBWAY, "Subway")
 *     TETRA_FILTER(LINE1, "Line 1")
 *
 *     TETRA_ID(100010, "Line1 Line Calls", TF(SUBWAY) | TF(LINE1))
 *     TETRA_ID(101011, "Transit Control", 0)
 *     TETRA_ID(60002, "Red Alarm", TF_EVERY)
 *
 * with the SSI, a name and the filters it belongs to: TF() of each, 0 for
 * none, TF_EVERY for all of them. Filter 0, "All", is always there and
 * lets everything through. Up to 30 filters. Adding TF_ALARM marks an
 * alarm talkgroup: its calls sound an alert and turn the screen red.
 *
 *     TETRA_LA(39, "Line 5 east")
 *
 * names a location area, shown as the radio's location, and
 *
 *     TETRA_ID_RANGE(100201, 100299, "Run %u", 100000, TF(LINE2))
 *
 * names every identity of a range not listed on its own, from a printf
 * format given the SSI minus a base, with the filters of the range.
 *
 * The header may also hold CPS_CHANNEL() entries for
 * platform/drivers/CPS/cps_io_builtin.c, which are ignored here: one file
 * can describe a whole network.
 */

/** Most filters a list can define, besides "All" */
#define TETRA_IDS_MAX_FILTERS 30

/**
 * @param ssi: talkgroup or radio identity.
 * @return its name from its own entry, or NULL if it has none.
 */
const char *tetraIds_name(uint32_t ssi);

/**
 * Name of an identity from its entry or its range.
 *
 * @param ssi: talkgroup or radio identity.
 * @param buf: destination.
 * @param len: size of buf.
 * @return true if it has a name.
 */
bool tetraIds_nameOf(uint32_t ssi, char *buf, size_t len);

/**
 * @return the number of filters, including "All" (filter 0).
 */
size_t tetraIds_filterCount(void);

/**
 * @param filter: filter number, 0 for "All".
 * @return its name, or NULL if there is no such filter.
 */
const char *tetraIds_filterName(size_t filter);

/**
 * @param ssi: talkgroup or radio identity.
 * @param filter: filter number.
 * @return true if the filter lets the identity through: always for filter
 * 0, never for an unknown identity otherwise.
 */
bool tetraIds_inFilter(uint32_t ssi, size_t filter);

/**
 * @param ssi: talkgroup or radio identity.
 * @return true if it is an alarm talkgroup (TF_ALARM).
 */
bool tetraIds_isAlarm(uint32_t ssi);

/**
 * @param la: location area.
 * @return its name, or NULL if it is not in the list.
 */
const char *tetraIds_laName(uint16_t la);

/**
 * Select the next or previous filter, wrapping around. Releases a held
 * talkgroup first. Called from the UI.
 *
 * @param step: +1 for the next filter, -1 for the previous one.
 */
void tetraIds_stepFilter(int step);

/**
 * Listen only to one identity, until released: a temporary filter.
 *
 * @param ssi: talkgroup or radio to hold.
 */
void tetraIds_hold(uint32_t ssi);

/**
 * Go back to the filter selected before the hold.
 */
void tetraIds_release(void);

/**
 * @return the held identity, 0 if none.
 */
uint32_t tetraIds_held(void);

/**
 * @return the selected filter: "All" (0) at start, tetraIds_filterCount()
 * while holding a talkgroup.
 */
size_t tetraIds_selectedFilter(void);

/**
 * @return a number that changes with every new selection or hold, for the
 * receiver to notice it.
 */
uint8_t tetraIds_selection(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_IDS_H */
