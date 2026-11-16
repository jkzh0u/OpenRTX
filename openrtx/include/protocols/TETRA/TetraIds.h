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
 * lets everything through. Up to 31 filters.
 *
 * The header may also hold CPS_CHANNEL() entries for
 * platform/drivers/CPS/cps_io_builtin.c, which are ignored here: one file
 * can describe a whole network.
 */

/** Most filters a list can define, besides "All" */
#define TETRA_IDS_MAX_FILTERS 31

/**
 * @param ssi: talkgroup or radio identity.
 * @return its name, or NULL if it is not in the list.
 */
const char *tetraIds_name(uint32_t ssi);

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
 * Select the next or previous filter, wrapping around. Called from the UI.
 *
 * @param step: +1 for the next filter, -1 for the previous one.
 */
void tetraIds_stepFilter(int step);

/**
 * @return the selected filter, "All" (0) at start.
 */
size_t tetraIds_selectedFilter(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_IDS_H */
