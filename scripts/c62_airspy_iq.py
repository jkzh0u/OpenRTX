#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Cut one TETRA carrier out of a wideband SDR recording, for
c62_tetra_sim.py: shift the carrier to 0 Hz, low-pass it and decimate to
250 kHz, written as a 16-bit stereo I/Q WAV.

The input is 16-bit interleaved I/Q, as airspy_rx writes by default:
either a raw file (give --rate) or a WAV, whose rate is read from it. The
input rate must be a multiple of the output rate (Airspy: 2.5, 3, 6 or
10 MHz). Large files are read in blocks.

  c62_airspy_iq.py ttc.raw ttc_250k.wav --rate 2500000 \\
      --centre 392.3 --carrier 392.5125
"""

import argparse
import struct
import sys
import wave

import numpy as np
from scipy import signal

BLOCK = 1 << 20  # Complex samples per block read


def open_input(path, rate):
    """
    @return the file positioned at the samples, and the sample rate.
    """
    f = open(path, "rb")
    head = f.read(12)
    if head[:4] == b"RIFF" and head[8:12] == b"WAVE":
        while True:
            chunk = f.read(8)
            if len(chunk) < 8:
                sys.exit(f"{path}: no data chunk")
            cid, size = struct.unpack("<4sI", chunk)
            if cid == b"fmt ":
                fmt = f.read(size)
                channels, wav_rate, bits = (struct.unpack("<HI", fmt[2:8])
                                            + struct.unpack("<H", fmt[14:16]))
                if channels != 2 or bits != 16:
                    sys.exit(f"{path}: need 16-bit stereo I/Q, found "
                             f"{channels} channels of {bits} bits")
                rate = wav_rate
            elif cid == b"data":
                break
            else:
                f.seek(size + (size & 1), 1)
    else:
        f.seek(0)
    if rate is None:
        sys.exit(f"{path}: a raw file needs --rate")
    return f, rate


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inp", help="16-bit I/Q, raw or WAV")
    ap.add_argument("out", help="output WAV, 16-bit stereo I/Q")
    ap.add_argument("--rate", type=int, help="input sample rate, raw files")
    ap.add_argument("--centre", type=float, required=True,
                    help="frequency the SDR was tuned to, MHz")
    ap.add_argument("--carrier", type=float, required=True,
                    help="TETRA carrier to keep, MHz")
    ap.add_argument("--out-rate", type=int, default=250000)
    ap.add_argument("--seconds", type=float, help="only the first seconds")
    a = ap.parse_args()

    f, rate = open_input(a.inp, a.rate)
    if rate % a.out_rate:
        sys.exit(f"input rate {rate} is not a multiple of {a.out_rate}")
    dec = rate // a.out_rate
    shift = (a.carrier - a.centre) * 1e6
    if abs(shift) > rate / 2 - 15000:
        sys.exit(f"carrier {shift / 1e3:+.1f} kHz from the centre is outside "
                 f"the {rate / 1e6:g} MHz recording")

    # Keep +-100 kHz: the channel and its neighbours' edges, for the C62's
    # own filter in c62_tetra_sim.py
    sos = signal.butter(10, 100e3, fs=rate, output="sos")
    zi = np.zeros((sos.shape[0], 2), dtype=np.complex128)
    total = None if a.seconds is None else int(a.seconds * rate)

    out = wave.open(a.out, "wb")
    out.setnchannels(2)
    out.setsampwidth(2)
    out.setframerate(a.out_rate)

    n = 0      # Input samples read
    phase = 0  # Of the shift, carried across blocks
    keep = 0   # Offset of the next kept sample in the block
    peak = 0.0
    while total is None or n < total:
        want = BLOCK if total is None else min(BLOCK, total - n)
        raw = np.frombuffer(f.read(want * 4), dtype="<i2")
        if raw.size < 2:
            break
        raw = raw[: raw.size // 2 * 2].reshape(-1, 2)
        x = (raw[:, 0] + 1j * raw[:, 1]) / 32768.0
        t = phase + np.arange(len(x))
        x = x * np.exp(-2j * np.pi * shift / rate * t)
        phase = (phase + len(x)) % rate
        y, zi = signal.sosfilt(sos, x, zi=zi)
        y = y[keep::dec]
        keep = (keep - len(x)) % dec
        n += len(x)
        peak = max(peak, float(np.max(np.abs(y))) if len(y) else 0.0)
        iq = np.empty((len(y), 2), dtype="<i2")
        iq[:, 0] = np.clip(y.real * 32768, -32768, 32767)
        iq[:, 1] = np.clip(y.imag * 32768, -32768, 32767)
        out.writeframes(iq.tobytes())
    out.close()

    print(f"{n / rate:.1f} s at {rate / 1e6:g} MHz, carrier "
          f"{shift / 1e3:+.1f} kHz from the centre: {a.out} at "
          f"{a.out_rate / 1e3:g} kHz, peak {20 * np.log10(peak + 1e-12):.1f} "
          f"dBFS")


if __name__ == "__main__":
    main()
