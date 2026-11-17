#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Show and save the C62 TETRA log (C62_TETRA_LOG) from the programming cable.

Each line is printed and appended to a file, prefixed with the computer's
clock time; the radio's own time (seconds since power on) follows it. Press
side key + 0 on the radio to send the log it kept, between lines marked
"--- stored log". Stop with Ctrl+C.

    pip install pyserial
    python3 scripts/c62_log.py -p /dev/ttyUSB0 -o ride.log
"""

import argparse
import datetime
import sys

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: pip install pyserial")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-p", "--port", required=True,
                    help="serial port, e.g. /dev/ttyUSB0 or COM3")
    ap.add_argument("-b", "--baud", type=int, default=115200,
                    help="baud rate (default 115200)")
    ap.add_argument("-o", "--output",
                    help="file to append to (default c62_YYYYMMDD_HHMMSS.log)")
    args = ap.parse_args()

    out = args.output or datetime.datetime.now().strftime(
        "c62_%Y%m%d_%H%M%S.log")

    with serial.Serial(args.port, args.baud, timeout=1) as port, \
            open(out, "a", encoding="utf-8") as f:
        print(f"Logging {args.port} at {args.baud} baud to {out}",
              file=sys.stderr)
        partial = b""
        try:
            while True:
                partial += port.read(port.in_waiting or 1)
                *lines, partial = partial.split(b"\n")
                for raw in lines:
                    text = raw.rstrip(b"\r").decode("ascii", "replace")
                    stamp = datetime.datetime.now().strftime("%H:%M:%S")
                    line = f"{stamp}  {text}"
                    print(line, flush=True)
                    f.write(line + "\n")
                f.flush()
        except KeyboardInterrupt:
            print(f"\nSaved to {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
