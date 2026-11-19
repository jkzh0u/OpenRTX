#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Decode every TETRA carrier of a channel list found in a wideband SDR
recording, through an approximation of the C62's receive path, into a
folder:

  <out>/README.md                summary table
  <out>/<MHz>_<name>/stats.txt    c62_tetra_host figures
  <out>/<MHz>_<name>/trace.txt    tetra_trace: cell, calls, call following
  <out>/<MHz>_<name>/speech_tsN.wav  clear speech of timeslot N, 8 kHz
  <out>/<MHz>_<name>/baseband.wav what the C62 would hear, 48 kHz

The recording is 16-bit I/Q (airspy_rx -t 2), raw (give --rate) or WAV.
Needs numpy and scipy, a C compiler, and the west workspace's libtetra
and OpenACELP next to OpenRTX (modules/lib).

  c62_decode_capture.py ttc.raw decoded --rate 3000000 --centre 423.0 \\
      --channels OpenRTX/scripts/c62/channels/ttc_tetra.h
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MODULES = os.path.join(os.path.dirname(ROOT), "modules", "lib")
SURVEY_SECONDS = 10
CHANNEL_HALF_WIDTH = 9000  # Hz either side, for the survey


def build_tools(dest):
    """Build c62_tetra_host (with speech) and tetra_trace into dest."""
    lt = os.path.join(MODULES, "libtetra")
    oa = os.path.join(MODULES, "openacelp")
    for need in (lt, oa):
        if not os.path.isdir(need):
            sys.exit(f"{need} not found: run 'west update' in the workspace")
    srcs = []
    for d, _, files in os.walk(os.path.join(lt, "src")):
        if "crypto" in d:
            continue
        # Built without the optional decryption (TETRA_ENABLE_CRYPTO)
        srcs += [os.path.join(d, f) for f in files
                 if f.endswith(".c") and "crypto" not in f]
    inc = ["-I", os.path.join(ROOT, "openrtx/include"),
           "-I", os.path.join(lt, "include"), "-I", os.path.join(lt, "src")]
    demod = os.path.join(ROOT, "openrtx/src/protocols/TETRA/TetraDemod.c")
    follow = os.path.join(ROOT, "openrtx/src/protocols/TETRA/TetraFollow.c")
    acelp = [os.path.join(oa, "src", f) for f in
             ("oa_dsp.c", "oa_bits.c", "oa_dec.c", "oa_tables_tetra.c")]
    cc = ["cc", "-std=gnu17", "-O2", "-w"]
    subprocess.run(cc + ["-DHOST_SPEECH"] + inc
                   + ["-I", os.path.join(oa, "include"),
                      "-I", os.path.join(oa, "src"),
                      os.path.join(ROOT, "tests/platform/c62_tetra_host.c"),
                      demod] + acelp + srcs
                   + ["-lm", "-o", os.path.join(dest, "c62_tetra_host")],
                   check=True)
    subprocess.run(cc + inc
                   + [os.path.join(ROOT, "tests/platform/tetra_trace.c"),
                      demod, follow] + srcs
                   + ["-lm", "-o", os.path.join(dest, "tetra_trace")],
                   check=True)


def read_channels(path):
    """@return {Hz: [names]} of the TETRA channels of a channel list."""
    chans = {}
    pat = re.compile(r'CPS_CHANNEL\("([^"]+)",\s*(\d+),\s*OPMODE_TETRA\)')
    with open(path) as f:
        for line in f:
            m = pat.search(line)
            if m:
                chans.setdefault(int(m.group(2)), []).append(m.group(1))
    return chans


def read_priority(path):
    """@return SSIs of the priority and alarm talkgroups of a channel list."""
    pat = re.compile(r"TETRA_ID\(\s*(\d+)\s*,.*\b(TF_PRIORITY|TF_ALARM)\b")
    with open(path) as f:
        return [m.group(1) for m in map(pat.search, f) if m]


def read_start(path, rate, seconds):
    """First seconds of a 16-bit I/Q recording, raw or WAV."""
    with open(path, "rb") as f:
        head = f.read(12)
        if head[:4] == b"RIFF":
            with wave.open(path) as w:
                rate = w.getframerate()
                raw = w.readframes(int(seconds * rate))
        else:
            f.seek(0)
            raw = f.read(int(seconds * rate) * 4)
    x = np.frombuffer(raw, dtype="<i2").reshape(-1, 2).astype(np.float32)
    return (x[:, 0] + 1j * x[:, 1]) / 32768, rate


def survey(z, rate, centre_hz, chans):
    """@return [(snr dB, Hz, names)] of the channels inside the recording."""
    n = 8192
    z = z[rate // 10:]  # Skip the SDR's start
    segs = z[: len(z) // n * n].reshape(-1, n) * np.hanning(n)
    p = np.mean(np.abs(np.fft.fftshift(np.fft.fft(segs, axis=1),
                                       axes=1)) ** 2, axis=0)
    f = (np.arange(n) - n / 2) * rate / n
    db = 10 * np.log10(p + 1e-20)
    floor = np.median(db)
    out = []
    for hz, names in chans.items():
        off = hz - centre_hz
        if abs(off) > rate / 2 - 20000:
            continue
        sel = np.abs(f - off) < CHANNEL_HALF_WIDTH
        out.append((float(db[sel].mean() - floor), hz, names))
    return sorted(out, reverse=True)


def stat(stats, key):
    m = re.search(rf"\b{key} (-?[\d.]+)", stats)
    return m.group(1) if m else "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inp", help="16-bit I/Q recording, raw or WAV")
    ap.add_argument("out", help="output folder")
    ap.add_argument("--rate", type=int, help="sample rate of a raw file")
    ap.add_argument("--centre", type=float, required=True,
                    help="frequency the SDR was tuned to, MHz")
    ap.add_argument("--channels", required=True,
                    help="channel list (.h) naming the carriers")
    ap.add_argument("--min-snr", type=float, default=8,
                    help="decode carriers at least this far above the "
                         "noise, dB (default 8)")
    a = ap.parse_args()

    centre_hz = round(a.centre * 1e6)
    chans = read_channels(a.channels)
    priority = []
    for ssi in read_priority(a.channels):
        priority += ["-p", ssi]
    z, rate = read_start(a.inp, a.rate, SURVEY_SECONDS)
    found = survey(z, rate, centre_hz, chans)
    os.makedirs(a.out, exist_ok=True)

    py = sys.executable
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        print("building the decoders")
        build_tools(tmp)
        for snr, hz, names in found:
            if snr < a.min_snr:
                continue
            name = re.sub(r"[^A-Za-z0-9]+", "-", names[0]).strip("-")
            d = os.path.join(a.out, f"{hz / 1e6:.4f}_{name}")
            os.makedirs(d, exist_ok=True)
            print(f"{hz / 1e6:.4f} MHz {', '.join(names)}: {snr:.1f} dB")

            iq = os.path.join(tmp, "iq.wav")
            s16 = os.path.join(tmp, "c62.s16")
            cmd = [py, os.path.join(HERE, "c62_airspy_iq.py"), a.inp, iq,
                   "--centre", str(a.centre), "--carrier", str(hz / 1e6)]
            if a.rate:
                cmd += ["--rate", str(a.rate)]
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
            subprocess.run([py, os.path.join(HERE, "c62_tetra_sim.py"), iq,
                            s16], check=True)
            base = os.path.join(d, "baseband.wav")
            with open(s16, "rb") as f, wave.open(base, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(48000)
                w.writeframes(f.read())

            stats = subprocess.run(
                [os.path.join(tmp, "c62_tetra_host"), "-e", "-a",
                 os.path.join(d, "speech"), s16],
                check=True, capture_output=True, text=True).stdout
            with open(os.path.join(d, "stats.txt"), "w") as f:
                f.write(stats)
            trace = subprocess.run(
                [os.path.join(tmp, "tetra_trace"), "-n", "-f",
                 str(hz / 1e6)] + priority[:32] + [base],
                check=True, capture_output=True, text=True).stdout
            with open(os.path.join(d, "trace.txt"), "w") as f:
                f.write(trace)

            cell = re.search(r"cell (\d+-\d+ LA \d+)", trace)
            speech = re.findall(r"speech TN(\d): ([\d.]+) s", stats)
            rows.append((hz, names, snr, stats,
                         cell.group(1) if cell else "-",
                         ", ".join(f"TN{t}" for t, _ in speech) or "-"))

    with open(os.path.join(a.out, "README.md"), "w") as f:
        f.write("# Decoded carriers\n\n"
                f"From `{os.path.basename(a.inp)}`, {rate / 1e6:g} MHz "
                f"around {a.centre} MHz, through an approximation of the "
                "C62's receive path (`scripts/c62_tetra_sim.py`) and the "
                "C62's demodulator, libtetra and call follower. The "
                "equalizer starts from scratch (`-e`, `-n`): the C62's "
                "measured taps do not fit the approximation.\n\n"
                "| Carrier | Names | Above noise | Cell | Locked | "
                "Blocks/s | Phase error | Calls | SDS | Speech frames "
                "(bad) | Speech |\n"
                "|---|---|---|---|---|---|---|---|---|---|---|\n")
        for hz, names, snr, s, cell, sp in rows:
            f.write(f"| {hz / 1e6:.4f} | {', '.join(names)} | {snr:.1f} dB "
                    f"| {cell} | {stat(s, 'sync')}% | {stat(s, 'crc/s')} | "
                    f"{stat(s, 'err')}% | {stat(s, 'calls')} | "
                    f"{stat(s, 'sds')} | {stat(s, 'frames')} "
                    f"({stat(s, 'bad')}) | {sp} |\n")
        weak = [(snr, hz, n) for snr, hz, n in found if snr < a.min_snr]
        if weak:
            f.write(f"\nNot decoded, below {a.min_snr:g} dB: "
                    + ", ".join(f"{hz / 1e6:.4f} {n[0]} ({snr:.1f} dB)"
                                for snr, hz, n in weak) + "\n")
    print(f"done: {os.path.join(a.out, 'README.md')}")


if __name__ == "__main__":
    main()
