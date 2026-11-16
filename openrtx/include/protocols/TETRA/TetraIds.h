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
 * firmware: names to show instead of numbers, and the identities to listen
 * to.
 *
 * TETRA_ID_LIST names a header with one entry per line:
 *
 *     TETRA_ID(101011, "Transit Control", true)
 *
 * the SSI, its name and whether to listen to it. When at least one entry
 * listens, the receiver reports only calls and messages to those
 * identities; otherwise it reports everything. The header may also hold
 * CPS_CHANNEL() entries for platform/drivers/CPS/cps_io_builtin.c, which
 * are ignored here: one file can describe a whole network.
 */

/** Most identities the receiver can filter on (libtetra's limit) */
#define TETRA_IDS_MAX_LISTEN 255

/**
 * @param ssi: talkgroup or radio identity.
 * @return its name, or NULL if it is not in the list.
 */
const char *tetraIds_name(uint32_t ssi);

/**
 * Identities to listen to, for the receiver's filter.
 *
 * @param count: set to the number of identities, 0 to listen to all.
 * @return the identities, valid for the life of the program.
 */
const uint32_t *tetraIds_listen(size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_IDS_H */
