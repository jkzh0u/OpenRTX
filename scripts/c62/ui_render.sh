#!/bin/sh
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Render the C62 main screen on the host with the firmware's own UI code,
# for a set of example states, to preview UI changes without a radio. See
# tests/platform/ui_render/ui_render.c.
#
# Usage: scripts/c62/ui_render.sh [-c channels.h] [-n MCC-MNC] [-s scale] [dir]
#
#   -c  also draw memory mode for each channel of a C62_CHANNELS list
#   -n  network code shown for the example cell (default 235-78)
#   -s  pixel scale of the PNG files (default 4)
#   dir output directory (default ui_render)
#
# Needs a C compiler and zlib.

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CHANNELS=
TOOL_ARGS=

while getopts "c:n:s:" opt; do
    case $opt in
        c) CHANNELS=$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG") ;;
        n) TOOL_ARGS="$TOOL_ARGS -n $OPTARG" ;;
        s) TOOL_ARGS="$TOOL_ARGS -s $OPTARG" ;;
        *) sed -n '9,18p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
OUT=${1:-ui_render}

BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

# The layout is a static function of ui.c, which needs most of the firmware:
# take just that function
awk '/^static void _ui_calculateLayout/,/^}/' \
    "$ROOT/openrtx/src/ui/default/ui.c" > "$BUILD/ui_layout.inc"
if [ ! -s "$BUILD/ui_layout.inc" ]; then
    echo "ui_calculateLayout not found in ui.c" >&2
    exit 1
fi

# newlib's sniprintf() is the firmware's; the host has snprintf()
set -- -std=gnu17 -O1 -w -o "$BUILD/ui_render" \
    -DPLATFORM_LINUX -DFONT_UBUNTU_REGULAR -D_GNU_SOURCE \
    -Dsniprintf=snprintf -Dvsniprintf=vsnprintf \
    -I"$ROOT/tests/platform/ui_render" -I"$BUILD" \
    -I"$ROOT/openrtx/include" -I"$ROOT/lib/QRCode/include" \
    "$ROOT/tests/platform/ui_render/ui_render.c" \
    "$ROOT/openrtx/src/core/graphics.c" \
    "$ROOT/openrtx/src/core/utils.c" \
    "$ROOT/openrtx/src/ui/default/ui_main.c" \
    "$ROOT/openrtx/src/ui/default/ui_strings.c" \
    "$ROOT/lib/QRCode/qrcode.c"
if [ -n "$CHANNELS" ]; then
    set -- "$@" "-DCPS_BUILTIN_LIST=\"$CHANNELS\"" \
        "$ROOT/platform/drivers/CPS/cps_io_builtin.c"
fi
cc "$@" -lz -lm

mkdir -p "$OUT"
# shellcheck disable=SC2086
"$BUILD/ui_render" $TOOL_ARGS "$OUT"
