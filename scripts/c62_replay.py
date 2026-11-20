#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Replay a wideband SDR recording of a TETRA network through the radio's own
TETRA mode (tests/platform/tetra_replay): call following, late joins,
priorities and cell tracking, with real retunes between the recorded
carriers, and fades to make the radio move between cells.

The carriers of the channel list strong enough in the recording are cut
out once into <work>/ as 48 kHz I/Q, with their level in C62 dBm estimated
from how far they stand above the noise (as the noise sweep calibrated:
dBm = -100 + dB above the noise). Then the replay runs and prints the
radio's receive log and a summary.

  c62_replay.py ttc.raw work --rate 3000000 --centre 423.0 \\
      --channels OpenRTX/scripts/c62/channels/ttc_tetra.h \\
      --start 423.4875 [--fade 423.4875:30:90:-30] [--level 422.6875:-80]
      [--seconds 60] [--quiet]

--fade MHz:t0:t1:dB weakens (dB < 0) a carrier linearly from t0 to t1 s;
--level MHz:dBm sets a carrier's level instead of the estimate. Needs numpy
and scipy, a C and C++ compiler, and libtetra in the west workspace.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import wave

import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MODULES = os.path.join(os.path.dirname(ROOT), "modules", "lib")
sys.path.insert(0, HERE)
import c62_decode_capture as dc  # noqa: E402  (survey, channel list)


def cut(raw, rate, centre, mhz, out):
    """One carrier as 48 kHz complex 16-bit I/Q."""
    with tempfile.TemporaryDirectory() as tmp:
        w250 = os.path.join(tmp, "iq.wav")
        cmd = [sys.executable, os.path.join(HERE, "c62_airspy_iq.py"), raw,
               w250, "--centre", str(centre), "--carrier", str(mhz)]
        if rate:
            cmd += ["--rate", str(rate)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        with wave.open(w250) as w:
            x = np.frombuffer(w.readframes(w.getnframes()), "<i2")
    z = (x[0::2] + 1j * x[1::2]).astype(np.complex64)
    z = signal.resample_poly(z, 24, 125)  # 250 kHz -> 48 kHz
    z *= 0.5 * 32767 / max(np.max(np.abs(z)), 1e-9)
    iq = np.empty(2 * len(z), "<i2")
    iq[0::2] = np.round(z.real)
    iq[1::2] = np.round(z.imag)
    iq.tofile(out)


def build(dest):
    lt = os.path.join(MODULES, "libtetra")
    if not os.path.isdir(lt):
        sys.exit(f"{lt} not found: run 'west update' in the workspace")
    defs = ["-DCONFIG_TETRA", "-DCONFIG_TETRA_FOLLOW", "-DCONFIG_TETRA_ROAM",
            "-DCONFIG_TETRA_LOG", "-DCONFIG_CHLIST"]
    inc = ["-I", os.path.join(ROOT, "tests/platform/tetra_replay"),
           "-I", os.path.join(ROOT, "openrtx/include"),
           "-I", os.path.join(lt, "include"), "-I", os.path.join(lt, "src")]
    tetra = os.path.join(ROOT, "openrtx/src/protocols/TETRA")
    csrc = [os.path.join(tetra, f) for f in (
        "TetraDemod.c", "TetraFollow.c", "TetraRoam.c", "TetraFilter.c",
        "TetraIds.c", "TetraStatus.c", "TetraConfig.c", "TetraLogEvents.c",
        "TetraCalls.c", "TetraFocus.c")]
    csrc += [os.path.join(ROOT, "openrtx/src/core/chlist.c"),
             os.path.join(ROOT, "platform/drivers/CPS/cps_io_builtin.c")]
    for d, _, files in os.walk(os.path.join(lt, "src")):
        if "crypto" not in os.path.relpath(d, lt):
            csrc += [os.path.join(d, f) for f in files
                     if f.endswith(".c") and "crypto" not in f]
    objs = []
    for i, f in enumerate(csrc):
        o = os.path.join(dest, f"{i}.o")
        subprocess.run(["cc", "-std=gnu17", "-O2", "-w"] + defs + inc
                       + ["-c", f, "-o", o], check=True)
        objs.append(o)
    for f in ("openrtx/src/rtx/OpMode_TETRA.cpp",
              "tests/platform/tetra_replay/tetra_replay.cpp"):
        o = os.path.join(dest, os.path.basename(f) + ".o")
        subprocess.run(["c++", "-std=c++14", "-O2", "-w"] + defs + inc
                       + ["-c", os.path.join(ROOT, f), "-o", o], check=True)
        objs.append(o)
    exe = os.path.join(dest, "tetra_replay")
    subprocess.run(["c++", "-o", exe] + objs + ["-lm", "-lpthread"],
                   check=True)
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inp", help="16-bit I/Q recording, raw or WAV")
    ap.add_argument("work", help="folder for the cut carriers")
    ap.add_argument("--rate", type=int, help="sample rate of a raw file")
    ap.add_argument("--centre", type=float, required=True)
    ap.add_argument("--channels", required=True)
    ap.add_argument("--start", type=float, required=True,
                    help="frequency the radio is set to, MHz")
    ap.add_argument("--fade", action="append", default=[],
                    metavar="MHz:t0:t1:dB")
    ap.add_argument("--level", action="append", default=[],
                    metavar="MHz:dBm")
    ap.add_argument("--min-snr", type=float, default=8)
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    os.makedirs(a.work, exist_ok=True)
    levels_file = os.path.join(a.work, "carriers.txt")
    if not os.path.exists(levels_file):
        chans = dc.read_channels(a.channels)
        z, rate = dc.read_start(a.inp, a.rate, dc.SURVEY_SECONDS)
        rows = []
        for snr, hz, names in dc.survey(z, rate, round(a.centre * 1e6),
                                        chans):
            if snr < a.min_snr:
                continue
            path = os.path.join(a.work, f"{hz / 1e6:.4f}.cs16")
            print(f"cutting {hz / 1e6:.4f} MHz {names[0]}: {snr:.1f} dB above "
                  f"the noise, about {-100 + snr:.0f} dBm", file=sys.stderr)
            cut(a.inp, a.rate, a.centre, hz / 1e6, path)
            rows.append(f"{hz / 1e6:.4f} {-100 + snr:.1f} {path}")
        with open(levels_file, "w") as f:
            f.write("\n".join(rows) + "\n")

    override = {}
    for spec in a.level:
        mhz, dbm = spec.split(":")
        override[round(float(mhz), 4)] = float(dbm)

    with tempfile.TemporaryDirectory() as tmp:
        exe = build(tmp)
        image = os.path.join(tmp, "channels.bin")
        subprocess.run([sys.executable, os.path.join(HERE, "chlist.py"),
                        a.channels, "-o", image], check=True,
                       stdout=subprocess.DEVNULL)
        cmd = [exe, "-c", image, "-f", str(a.start)]
        for line in open(levels_file):
            mhz, dbm, path = line.split()
            dbm = override.get(round(float(mhz), 4), float(dbm))
            cmd += ["-C", f"{mhz}:{dbm}:{path}"]
        for spec in a.fade:
            cmd += ["-F", spec]
        if a.seconds:
            cmd += ["-s", str(a.seconds)]
        if a.quiet:
            cmd += ["-q"]
        sys.exit(subprocess.run(cmd).returncode)


if __name__ == "__main__":
    main()
