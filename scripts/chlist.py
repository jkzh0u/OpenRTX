#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Build a channel list image, or show what one holds.

A channel list is a text file of entries, the format of
platform/drivers/CPS/cps_io_builtin.c and
openrtx/include/protocols/TETRA/TetraIds.h:

    CPS_CHANNEL("Name", frequency_hz, OPMODE_FM | OPMODE_M17 | OPMODE_TETRA
                                      | OPMODE_DMR)
    FM_CHANNEL("Name", rx_hz, tx_hz, width_hz, rx_tone, tx_tone)
    TETRA_FILTER(ID, "Name")
    TETRA_ID(ssi, "Name", TF(ID) | TF(OTHER))      # or 0, TF_EVERY; also
                                                   # | TF_ALARM, TF_PRIORITY(n)
    TETRA_LA(la, "Name")
    TETRA_ID_RANGE(first, last, "Run %u", base, TF(ID))

with C comments. CPS_CHANNEL channels are receive only, 25 kHz, without
tones. FM_CHANNEL is an FM channel with all its settings: tx_hz the
transmit frequency (0: receive only), width_hz 12500 or 25000, and the
CTCSS tones in tenths of Hz (885 for 88.5 Hz), 0 for none.
TF_PRIORITY(n), n from 1 (lowest) to 15, gives a
talkgroup's calls priority over others; alarm talkgroups have 15 unless
given another. The image (openrtx/include/core/chlist.h) is flashed apart
from the firmware, so one firmware serves everyone:

    python3 scripts/chlist.py my_channels.h -o channels.bin
    python3 scripts/chlist.py --show channels.bin
"""

import argparse
import ast
import re
import struct
import sys
import zlib

VERSION = 2
READ_VERSIONS = (1, 2)  # 1: no priorities
HEADER_SIZE = 32
MAX_FILTERS = 30
OPMODES = {"OPMODE_FM": 1, "OPMODE_DMR": 2, "OPMODE_M17": 3,
           "OPMODE_TETRA": 4}
MODE_NAMES = {v: k[7:] for k, v in OPMODES.items()}
BW_12_5 = 0
BW_25 = 1
TONE_ON = 0x80
# CTCSS tones, tenths of Hz: the index is what the image keeps
# (openrtx/src/core/cps.c)
CTCSS = (670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000,
         1035, 1072, 1109, 1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462,
         1514, 1567, 1598, 1622, 1655, 1679, 1713, 1738, 1773, 1799, 1835,
         1862, 1899, 1928, 1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291,
         2336, 2418, 2503, 2541)
RX_ONLY = 0x01
TF_ALARM = 1
MAX_PRIORITY = 15
SSI_MASK = 0xFFFFFF  # SSIs are 24-bit: priority in bits 24-31
TF_EVERY = 0x7FFFFFFE
MACROS = ("CPS_CHANNEL", "FM_CHANNEL", "TETRA_FILTER", "TETRA_ID",
          "TETRA_LA", "TETRA_ID_RANGE")
FORMAT_RE = re.compile(r"%(%|[0-]*\d{0,2}u|.?)")


class ListError(Exception):
    pass


def strip_comments(text):
    """Remove C comments and preprocessor lines, keeping string literals
    and line numbers."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(c)
            i += 1
    lines = "".join(out).split("\n")
    return "\n".join("" if l.lstrip().startswith("#") else l for l in lines)


def split_args(s):
    args, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    args.append("".join(cur).strip())
    return args


def entries(text):
    """(line, macro, [args]) for each entry, read left to right; anything
    else than entries and whitespace is an error."""
    text = strip_comments(text)
    i, n = 0, len(text)
    while i < n:
        if text[i].isspace() or text[i] == ";":
            i += 1
            continue
        line = text.count("\n", 0, i) + 1
        m = re.match(r"[A-Za-z_]\w*", text[i:])
        if not m:
            raise ListError(f"line {line}: unexpected {text[i]!r}")
        name = m.group(0)
        j = i + m.end()
        while j < n and text[j].isspace():
            j += 1
        if name not in MACROS or j >= n or text[j] != "(":
            raise ListError(f"line {line}: unknown entry {name}")
        k, depth = j + 1, 1
        while k < n and depth:
            if text[k] == '"':
                k += 1
                while k < n and text[k] != '"':
                    k += 2 if text[k] == "\\" else 1
            elif text[k] == "(":
                depth += 1
            elif text[k] == ")":
                depth -= 1
            k += 1
        if depth:
            raise ListError(f"line {line}: {name}( is not closed")
        yield line, name, split_args(text[j + 1:k - 1])
        i = k


def string(arg, line):
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg)
    if not parts or re.sub(r'"(?:[^"\\]|\\.)*"', "", arg).strip():
        raise ListError(f"line {line}: expected a string, got {arg}")
    s = re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(
        m.group(1), m.group(1)), "".join(parts))
    try:
        return s.encode("ascii")
    except UnicodeEncodeError:
        raise ListError(f"line {line}: {s!r}: use ASCII characters only")


def number(arg, line, filters=None):
    """An integer expression: numbers, OPMODE_x, TF(x), TF_EVERY,
    TF_ALARM, TF_PRIORITY(n), with | + - * << and parentheses.
    TF_PRIORITY(n) gives n << 32, above the 32-bit filter mask."""
    def tf(m):
        if filters is None or m.group(1) not in filters:
            raise ListError(f"line {line}: unknown filter {m.group(1)}")
        return str(1 << filters[m.group(1)])

    def prio(m):
        n = int(m.group(1), 0)
        if not 1 <= n <= MAX_PRIORITY:
            raise ListError(f"line {line}: TF_PRIORITY({m.group(1)}): "
                            f"priority 1 to {MAX_PRIORITY}")
        return str(n << 32)

    expr = re.sub(r"\bTF_PRIORITY\s*\(\s*(\w+)\s*\)", prio, arg)
    if re.search(r"\bTF_PRIORITY\b", expr):
        raise ListError(f"line {line}: TF_PRIORITY needs a number, "
                        f"TF_PRIORITY(1) to TF_PRIORITY({MAX_PRIORITY})")
    expr = re.sub(r"\bTF\s*\(\s*([A-Za-z_]\w*)\s*\)", tf, expr)
    names = dict(OPMODES, TF_EVERY=TF_EVERY, TF_ALARM=TF_ALARM)
    expr = re.sub(r"\b(\d+|0[xX][0-9a-fA-F]+)[uUlL]*\b", r"\1", expr)
    try:
        tree = ast.parse(expr, mode="eval")
    except SyntaxError:
        raise ListError(f"line {line}: cannot read {arg}")

    def ev(n):
        if isinstance(n, ast.Expression):
            return ev(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return n.value
        if isinstance(n, ast.Name) and n.id in names:
            return names[n.id]
        if isinstance(n, ast.BinOp):
            ops = {ast.BitOr: lambda a, b: a | b, ast.Add: lambda a, b: a + b,
                   ast.Sub: lambda a, b: a - b, ast.Mult: lambda a, b: a * b,
                   ast.LShift: lambda a, b: a << b}
            if type(n.op) in ops:
                return ops[type(n.op)](ev(n.left), ev(n.right))
        raise ListError(f"line {line}: cannot read {arg}")

    return ev(tree)


def tone(arg, line):
    """A CTCSS tone in tenths of Hz, 0 for none, as the image keeps it."""
    v = number(arg, line)
    if v == 0:
        return 0
    if v not in CTCSS:
        near = min(CTCSS, key=lambda t: abs(t - v))
        raise ListError(f"line {line}: {arg}: not a CTCSS tone (tenths of "
                        f"Hz; nearest {near // 10}.{near % 10} Hz is {near})")
    return TONE_ON | CTCSS.index(v)


def ssi(arg, line):
    """An SSI: 24 bits, the image keeps a priority above them."""
    v = number(arg, line)
    if not 0 <= v <= SSI_MASK:
        raise ListError(f"line {line}: {arg}: an SSI is 0 to {SSI_MASK}")
    return v


def parse(text):
    channels, filters, ids, las, ranges = [], {}, [], [], []
    filter_names = []
    items = list(entries(text))

    for line, name, args in items:
        if name == "TETRA_FILTER":
            if len(args) != 2:
                raise ListError(f"line {line}: TETRA_FILTER(ID, \"Name\")")
            if args[0] in filters:
                raise ListError(f"line {line}: filter {args[0]} twice")
            filters[args[0]] = len(filters) + 1
            filter_names.append(string(args[1], line))
    if len(filters) > MAX_FILTERS:
        raise ListError(f"{len(filters)} filters: at most {MAX_FILTERS}")

    for line, name, args in items:
        want = {"CPS_CHANNEL": 3, "FM_CHANNEL": 6, "TETRA_FILTER": 2,
                "TETRA_ID": 3, "TETRA_LA": 2, "TETRA_ID_RANGE": 5}[name]
        if len(args) != want:
            raise ListError(f"line {line}: {name} takes {want} arguments")
        if name == "CPS_CHANNEL":
            freq = number(args[1], line)
            mode = number(args[2], line)
            if mode not in MODE_NAMES:
                raise ListError(f"line {line}: unknown mode {args[2]}")
            if not 1 <= freq < 1 << 32:
                raise ListError(f"line {line}: bad frequency {args[1]}")
            channels.append((freq, freq, string(args[0], line), mode, BW_25,
                             RX_ONLY, 0, 0))
        elif name == "FM_CHANNEL":
            rx = number(args[1], line)
            tx = number(args[2], line)
            width = number(args[3], line)
            if not 1 <= rx < 1 << 32:
                raise ListError(f"line {line}: bad frequency {args[1]}")
            if not 0 <= tx < 1 << 32:
                raise ListError(f"line {line}: bad frequency {args[2]}")
            if width not in (12500, 25000):
                raise ListError(f"line {line}: width {args[3]}: 12500 or "
                                f"25000")
            channels.append((rx, tx if tx else rx, string(args[0], line),
                             OPMODES["OPMODE_FM"],
                             BW_12_5 if width == 12500 else BW_25,
                             0 if tx else RX_ONLY, tone(args[4], line),
                             tone(args[5], line)))
        elif name == "TETRA_ID":
            v = number(args[2], line, filters)
            ids.append((ssi(args[0], line), string(args[1], line),
                        v & 0xFFFFFFFF, v >> 32))
        elif name == "TETRA_LA":
            las.append((number(args[0], line) & 0xFFFF,
                        string(args[1], line)))
        elif name == "TETRA_ID_RANGE":
            fmt = string(args[2], line)
            conv = FORMAT_RE.findall(fmt.decode())
            if [c for c in conv if c != "%"] != [c for c in conv
                                                  if c.endswith("u")] \
                    or sum(c.endswith("u") for c in conv) != 1:
                raise ListError(f"line {line}: the name needs exactly one "
                                "%u, e.g. \"Run %u\"")
            v = number(args[4], line, filters)
            ranges.append((ssi(args[0], line), ssi(args[1], line), fmt,
                           number(args[3], line), v & 0xFFFFFFFF, v >> 32))

    return channels, filter_names, ids, las, ranges


def build(lists):
    channels, filter_names, ids, las, ranges = lists
    strings, offsets = bytearray(), {}

    def ref(s):
        if s not in offsets:
            offsets[s] = len(strings)
            strings.extend(s + b"\0")
        return offsets[s]

    body = bytearray()
    for rx, tx, name, mode, bw, flags, rxt, txt in channels:
        body += struct.pack("<IIIBBBBB3x", rx, tx, ref(name), mode, bw, flags,
                            rxt, txt)
    for name in filter_names:
        body += struct.pack("<I", ref(name))
    for ident, name, mask, prio in ids:
        body += struct.pack("<III", ident | prio << 24, ref(name), mask)
    for la, name in las:
        body += struct.pack("<HxxI", la, ref(name))
    for first, last, fmt, base, mask, prio in ranges:
        body += struct.pack("<IIIII", first | prio << 24, last, ref(fmt),
                            base, mask)
    if not strings:
        strings += b"\0"

    str_at = HEADER_SIZE + len(body)
    rest = bytes(body) + bytes(strings)
    size = HEADER_SIZE + len(rest)
    header = struct.pack("<4sHHII6HI", b"OCHL", VERSION, HEADER_SIZE, size,
                         zlib.crc32(rest) & 0xFFFFFFFF, len(channels),
                         len(filter_names), len(ids), len(las), len(ranges),
                         0, str_at)
    return header + rest


def show(data):
    if len(data) < HEADER_SIZE or data[:4] != b"OCHL":
        raise ListError("not a channel list image (no OCHL at the start)")
    (_, ver, hsize, size, crc, nc, nf, ni, nl, nr, _,
     str_at) = struct.unpack_from("<4sHHII6HI", data)
    if ver not in READ_VERSIONS or hsize != HEADER_SIZE:
        raise ListError(f"version {ver}, this script reads "
                        f"{', '.join(map(str, READ_VERSIONS))}")
    if zlib.crc32(data[HEADER_SIZE:size]) & 0xFFFFFFFF != crc:
        raise ListError("bad CRC: the image is damaged")

    def s(ref):
        end = data.index(b"\0", str_at + ref)
        return data[str_at + ref:end].decode("ascii")

    at = HEADER_SIZE
    print(f"{size} bytes: {nc} channels, {nf} filters, {ni} identities, "
          f"{nl} location areas, {nr} ranges")
    fnames = ["All"]
    for i in range(nc):
        rx, tx, name, mode, bw, flags, rxt, txt = struct.unpack_from(
            "<IIIBBBBB3x", data, at)
        at += 20
        extra = ""
        if mode == OPMODES["OPMODE_FM"]:
            def t(v):
                if not v & TONE_ON or (v & 0x7F) >= len(CTCSS):
                    return "-"
                c = CTCSS[v & 0x7F]
                return f"{c // 10}.{c % 10}"
            extra = (f" {'12.5' if bw == BW_12_5 else '25'} kHz"
                     f" tones {t(rxt)}/{t(txt)}")
            if flags & RX_ONLY:
                extra += " RX only"
            elif tx != rx:
                extra += f" TX {tx / 1e6:.5f}"
        print(f"  {i + 1:3}. {s(name):16} {rx / 1e6:11.5f} "
              f"{MODE_NAMES.get(mode, mode)}{extra}")
    for i in range(nf):
        fnames.append(s(struct.unpack_from("<I", data, at)[0]))
        at += 4
    print("  filters: " + ", ".join(fnames))

    def masks(m, prio):
        names = [fnames[b] for b in range(1, len(fnames)) if m >> b & 1]
        return ", ".join(names + (["alarm"] if m & 1 else [])
                         + ([f"priority {prio}"] if prio else []))

    for _ in range(ni):
        ident, name, mask = struct.unpack_from("<III", data, at)
        at += 12
        print(f"  {ident & SSI_MASK:>9} {s(name):24} "
              f"{masks(mask, ident >> 24)}")
    for _ in range(nl):
        la, name = struct.unpack_from("<HxxI", data, at)
        at += 8
        print(f"  LA {la:<6} {s(name)}")
    for _ in range(nr):
        first, last, fmt, base, mask = struct.unpack_from("<IIIII", data, at)
        at += 20
        print(f"  {first & SSI_MASK}-{last} {s(fmt)!r} - {base}  "
              f"{masks(mask, first >> 24)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 epilog=__doc__.split("\n\n", 1)[1],
                                 formatter_class=argparse.
                                 RawDescriptionHelpFormatter)
    ap.add_argument("list", nargs="?", help="channel list to build")
    ap.add_argument("-o", "--output", help="image to write")
    ap.add_argument("--show", metavar="IMAGE", help="show an image")
    ap.add_argument("--max-size", type=lambda v: int(v, 0), default=0x90000,
                    help="largest image that fits (default 0x90000, C62)")
    args = ap.parse_args()

    try:
        if args.show:
            with open(args.show, "rb") as f:
                show(f.read())
            return
        if not args.list or not args.output:
            ap.error("give a list and -o IMAGE, or --show IMAGE")
        with open(args.list, encoding="utf-8") as f:
            image = build(parse(f.read()))
        if len(image) > args.max_size:
            raise ListError(f"image of {len(image)} bytes: at most "
                            f"{args.max_size}")
        with open(args.output, "wb") as f:
            f.write(image)
        print(f"{args.output}: {len(image)} bytes", file=sys.stderr)
    except (ListError, OSError) as e:
        sys.exit(f"chlist: {e}")


if __name__ == "__main__":
    main()
