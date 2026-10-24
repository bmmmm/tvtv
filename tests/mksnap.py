#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Snapshots of the bridge tvtv-social for the tests (tests/social.bats), written from its
# docs/snapshot.md and docs/dem0.md with struct and zlib, independently of the show's reader
# (src/social.c) and VM (src/dem0.c), which the tests check against it. Also a small dem0
# assembler and a dem0 interpreter in float32, a second oracle beside the bridge's conformance
# vectors (tests/dem0-vectors.txt, its testdata/dem0-vectors.txt), in the same format.
#   mksnap.py <out> [spec.json|-] [--break <rule>]   a snapshot; with --break, one rule broken
#   mksnap.py <out> --full                           the snapshot --break starts from, unbroken
#   mksnap.py breaks                                 the rules --break knows
#   mksnap.py object '<asm>'                         a dem0 object in hex
#   mksnap.py vga <index>                            a colour of the VGA palette: r g b, 8 bits each
#   mksnap.py vectors                                conformance vectors on stdout
# The spec is JSON, every key optional:
#   {"seq": 1, "flags": ["announce", "loop_pixel", "loop_dem0", "loop_text", "paused"],
#    "mode": "normal", "bot": "@tvtv@bonn.social", "fill": 6, "paint": [[x, y, colour], ...],
#    "pixels": [{"x": 1, "y": 2, "color": 3, "author": "@a@b", "event": 9}, ...],   newest first
#    "progs": [{"asm": "in r0 x; out r0", "source": "x", "secs": 5, "author": "@a@b", "event": 9}],
#    "slots": [{"label": "GRUSS", "text": "Hallo", "author": "@a@b", "event": 9, "set_at": 1}],
#    "announce": {"kind": "pixel|dem0|text", "item": 0, "event": 9},
#    "pixel_secs": 10, "text_secs": 10}
# Assembler: statements split by ; or newlines, "op dest operands", registers r0..r31, inputs by
# name (x y t u v r a), constants as numbers: "const r0 2.5; in r1 x; mul r2 r1 r0; out r2".
# The writer moves files into place with rename, as the bridge does.
import ctypes
import json
import math
import os
import struct
import sys
import zlib

SIZE = 19952
OBJ = 1292


def f32(x):
    return ctypes.c_float(x).value


# ---------- dem0 objects (docs/dem0.md, 7) ----------

OPS = {
    "const": (0x01, "K", 1), "in": (0x02, "I", 1), "mov": (0x03, "U", 1),
    "add": (0x10, "B", 1), "sub": (0x11, "B", 1), "mul": (0x12, "B", 1), "div": (0x13, "B", 2),
    "mod": (0x14, "B", 4), "min": (0x15, "B", 1), "max": (0x16, "B", 1), "pow": (0x17, "B", 8),
    "atan2": (0x18, "B", 8), "hypot": (0x19, "B", 3), "step": (0x1A, "B", 1), "eq": (0x1B, "B", 1),
    "ne": (0x1C, "B", 1), "lt": (0x1D, "B", 1), "le": (0x1E, "B", 1), "and": (0x1F, "B", 1),
    "or": (0x20, "B", 1), "band": (0x21, "B", 2), "bor": (0x22, "B", 2), "bxor": (0x23, "B", 2),
    "shl": (0x24, "B", 2), "shr": (0x25, "B", 2),
    "neg": (0x30, "U", 1), "abs": (0x31, "U", 1), "floor": (0x32, "U", 2), "ceil": (0x33, "U", 2),
    "round": (0x34, "U", 2), "fract": (0x35, "U", 2), "sqrt": (0x36, "U", 3), "sin": (0x37, "U", 3),
    "cos": (0x38, "U", 3), "exp": (0x39, "U", 8), "log": (0x3A, "U", 8), "sign": (0x3B, "U", 1),
    "not": (0x3C, "U", 1), "bnot": (0x3D, "U", 2), "rnd": (0x3E, "U", 3),
    "sel": (0x40, "S", 1),
}
NAMES = {v[0]: k for k, v in OPS.items()}
INPUTS = "xytuvra"


def reg(w):
    if not (w.startswith("r") and w[1:].isdigit() and int(w[1:]) < 32):
        raise SystemExit("mksnap: not a register: " + w)
    return int(w[1:])


class Prog:
    def __init__(self, out, consts, code):
        self.out, self.consts, self.code = out, consts, code

    def cost(self):
        return sum(OPS[NAMES[c[0]]][2] for c in self.code)

    def obj(self):
        b = bytearray(OBJ)
        b[0] = 1
        b[1] = len(self.out)
        for i, r in enumerate(self.out):
            b[2 + i] = r
        b[5] = len(self.consts)
        struct.pack_into("<HH", b, 6, len(self.code), self.cost())
        for i, k in enumerate(self.consts):
            struct.pack_into("<f", b, 12 + 4 * i, k)
        for i, c in enumerate(self.code):
            b[268 + 4 * i:272 + 4 * i] = bytes(c)
        return bytes(b)


def assemble(src):
    consts, code, out = [], [], []
    for stmt in src.replace("\n", ";").split(";"):
        w = stmt.split()
        if not w:
            continue
        if w[0] == "out":
            out = [reg(r) for r in w[1:]]
            continue
        op, fmt, _ = OPS[w[0]]
        d = reg(w[1])
        if fmt == "K":
            k = f32(float(w[2]))
            bits = struct.pack("<f", k)
            known = [struct.pack("<f", c) for c in consts]
            if bits not in known:
                consts.append(k)
                known.append(bits)
            code.append((op, d, known.index(bits), 0))
        elif fmt == "I":
            code.append((op, d, INPUTS.index(w[2]), 0))
        elif fmt == "U":
            code.append((op, d, reg(w[2]), 0))
        else:
            code.append((op, d, reg(w[2]), reg(w[3])))
    return Prog(out, consts, code)


# ---------- the semantics in float32 (docs/dem0.md, 5) ----------

SINE = [f32(math.sin(2 * math.pi * k / 4096)) for k in range(4097)]
SINE[0] = SINE[4096] = 0.0
SCALE = f32(651.8986469044033)


def finite(a):
    return not (math.isnan(a) or math.isinf(a))


def I(a):
    if not finite(a):
        return 0
    v = int(a) & 0xFFFFFFFF
    return v - (1 << 32) if v >= 1 << 31 else v


def i2f(v):
    v &= 0xFFFFFFFF
    return f32(float(v - (1 << 32) if v >= 1 << 31 else v))


def floor(a):
    if not finite(a) or a == 0:
        return a
    return float(math.floor(a))


def ceil(a):
    if not finite(a) or a == 0:
        return a
    c = float(math.ceil(a))
    return -0.0 if c == 0 and a < 0 else c


def tsin(p):
    if not abs(p) < 2147483648.0:
        return 0.0
    f = floor(p)
    q = f32(p - f)
    k = int(f) & 4095
    return f32(SINE[k] + f32(f32(SINE[k + 1] - SINE[k]) * q))


def vpow(a, b):
    if (a < 0 and b != floor(b)) or (a == 0 and b < 0):
        return 0.0
    try:
        return f32(math.pow(a, b))
    except OverflowError:
        return -math.inf if a < 0 and b == floor(b) and int(b) % 2 else math.inf


def vexp(a):
    try:
        return f32(math.exp(a))
    except OverflowError:
        return math.inf


def vrnd(a):
    k = I(floor(a)) & 0xFFFFFFFF
    k ^= k >> 16
    k = (k * 0x7feb352d) & 0xFFFFFFFF
    k ^= k >> 15
    k = (k * 0x846ca68b) & 0xFFFFFFFF
    k ^= k >> 16
    return (k >> 8) / 16777216.0


def b2f(c):
    return 1.0 if c else 0.0


BINARY = {
    "add": lambda a, b: f32(a + b),
    "sub": lambda a, b: f32(a - b),
    "mul": lambda a, b: f32(a * b),
    "div": lambda a, b: 0.0 if b == 0 else f32(a / b),
    "mod": lambda a, b: 0.0 if b == 0 else f32(a - f32(b * floor(f32(a / b)))),
    "min": lambda a, b: a if a < b else b,
    "max": lambda a, b: a if a > b else b,
    "pow": vpow,
    "atan2": lambda a, b: f32(math.atan2(a, b)),
    "hypot": lambda a, b: f32(math.sqrt(f32(f32(a * a) + f32(b * b)))),
    "step": lambda a, b: 0.0 if b < a else 1.0,
    "eq": lambda a, b: b2f(a == b),
    "ne": lambda a, b: b2f(a != b),
    "lt": lambda a, b: b2f(a < b),
    "le": lambda a, b: b2f(a <= b),
    "and": lambda a, b: b2f(a != 0 and b != 0),
    "or": lambda a, b: b2f(a != 0 or b != 0),
    "band": lambda a, b: i2f(I(a) & I(b)),
    "bor": lambda a, b: i2f(I(a) | I(b)),
    "bxor": lambda a, b: i2f(I(a) ^ I(b)),
    "shl": lambda a, b: i2f((I(a) & 0xFFFFFFFF) << (I(b) & 31)),
    "shr": lambda a, b: i2f(I(a) >> (I(b) & 31)),
}
UNARY = {
    "mov": lambda a: a,
    "neg": lambda a: -a,
    "abs": abs,
    "floor": floor,
    "ceil": ceil,
    "round": lambda a: floor(f32(a + 0.5)),
    "fract": lambda a: f32(a - floor(a)),
    "sqrt": lambda a: 0.0 if a < 0 else f32(math.sqrt(a)),
    "sin": lambda a: tsin(f32(a * SCALE)),
    "cos": lambda a: tsin(f32(f32(a * SCALE) + 1024.0)),
    "exp": vexp,
    "log": lambda a: 0.0 if a <= 0 else f32(math.log(a)),
    "sign": lambda a: 1.0 if a > 0 else -1.0 if a < 0 else 0.0,
    "not": lambda a: b2f(a == 0),
    "bnot": lambda a: i2f(~I(a)),
    "rnd": vrnd,
}


def inputs(x, y, t):
    u = f32(f32(x - 180.0) / 144.0)
    v = f32(f32(y - 144.0) / 144.0)
    r = f32(math.sqrt(f32(f32(u * u) + f32(v * v))))
    return [x, y, t, u, v, r, f32(math.atan2(v, u))]


def run(p, x, y, t):
    x, y, t = f32(x), f32(y), f32(t)
    inp = inputs(x, y, t)
    R = [None] * 32
    for op, d, a, b in p.code:
        name = NAMES[op]
        if name == "const":
            R[d] = p.consts[a]
        elif name == "in":
            R[d] = inp[a]
        elif name == "sel":
            R[d] = R[a] if R[d] != 0 else R[b]
        elif name in UNARY:
            R[d] = UNARY[name](R[a])
        else:
            R[d] = BINARY[name](R[a], R[b])
    return [R[r] for r in p.out]


# docs/dem0.md, 6.1: the palette of the VGA BIOS in mode 13h, 6 bits per channel.
def vga(i):
    ega = [0x000000, 0x00002A, 0x002A00, 0x002A2A, 0x2A0000, 0x2A002A, 0x2A1500, 0x2A2A2A,
           0x151515, 0x15153F, 0x153F15, 0x153F3F, 0x3F1515, 0x3F153F, 0x3F3F15, 0x3F3F3F]
    grey = [0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63]
    rings = [[0, 16, 31, 47, 63], [31, 39, 47, 55, 63], [45, 49, 54, 58, 63], [0, 7, 14, 21, 28],
             [14, 17, 21, 24, 28], [20, 22, 24, 26, 28], [0, 4, 8, 12, 16], [8, 10, 12, 14, 16],
             [11, 12, 13, 15, 16]]
    hues = ["004", "104", "204", "304", "404", "403", "402", "401", "400", "410", "420", "430",
            "440", "340", "240", "140", "040", "041", "042", "043", "044", "034", "024", "014"]
    if i < 16:
        c = [ega[i] >> 16, (ega[i] >> 8) & 255, ega[i] & 255]
    elif i < 32:
        c = [grey[i - 16]] * 3
    elif i < 248:
        ring, hue = rings[(i - 32) // 24], hues[(i - 32) % 24]
        c = [ring[int(h)] for h in hue]
    else:
        c = [0, 0, 0]
    return [(v << 2) | (v >> 4) for v in c]


# ---------- snapshots (docs/snapshot.md) ----------

FLAGS = {"paused": 1, "announce": 2, "loop_pixel": 4, "loop_dem0": 8, "loop_text": 16}
KINDS = {"pixel": 1, "dem0": 2, "text": 3}


def put_str(b, off, size, s):
    if len(s) >= size:
        raise SystemExit("mksnap: %r does not fit in %d bytes" % (s, size))
    b[off:off + size] = s + bytes(size - len(s))


def snapshot(spec):
    b = bytearray(SIZE)
    b[0:8] = b"DEM0SNAP"
    struct.pack_into("<IIQ", b, 8, 1, SIZE, spec.get("seq", 1))
    flags = spec.get("flags", ["announce", "loop_pixel", "loop_dem0", "loop_text"])
    struct.pack_into("<Iq", b, 28, sum(FLAGS[f] for f in flags), spec.get("written_at", 1761300000))
    put_str(b, 40, 16, spec.get("mode", "normal").encode())
    put_str(b, 56, 64, spec.get("bot", "@tvtv@bonn.social").encode())
    ann = spec.get("announce")
    if ann:
        struct.pack_into("<IBB", b, 120, ann["event"], KINDS[ann["kind"]], ann.get("item", 0))
    b[126] = spec.get("pixel_secs", 10)
    b[127] = spec.get("text_secs", 10)
    canvas = bytearray([spec.get("fill", 6)]) * 3072
    pixels = spec.get("pixels", [])
    for x, y, c in spec.get("paint", []):
        canvas[y * 64 + x] = c
    for p in reversed(pixels):
        canvas[p["y"] * 64 + p["x"]] = p["color"]
    b[128:3200] = canvas
    struct.pack_into("<I", b, 3200, spec.get("pixel_count", len(pixels)))
    for i, p in enumerate(pixels):
        o = 3208 + 64 * i
        b[o:o + 4] = bytes((p["x"], p["y"], p["color"], 1))
        struct.pack_into("<I", b, o + 4, p.get("event", 1000 - i))
        put_str(b, o + 8, 56, p.get("author", "@alice@bonn.social").encode())
    progs = spec.get("progs", [])
    b[3720] = len(progs)
    for i, p in enumerate(progs):
        o = 3724 + 1868 * i
        struct.pack_into("<IB", b, o, p.get("event", 2000 - i), p.get("secs", 5))
        put_str(b, o + 8, 56, p.get("author", "@bob@chaos.social").encode())
        put_str(b, o + 64, 512, p.get("source", " ".join(p["asm"].split())).encode())
        b[o + 576:o + 576 + OBJ] = assemble(p["asm"]).obj()
    slots = spec.get("slots", [])
    b[18668] = len(slots)
    for i, s in enumerate(slots):
        o = 18672 + 160 * i
        text = s.get("text", "")
        put_str(b, o, 24, s["label"].encode("latin-1"))
        put_str(b, o + 24, 64, text.encode("latin-1"))
        put_str(b, o + 88, 56, s.get("author", "@carol@bonn.social" if text else "").encode())
        struct.pack_into("<I", b, o + 144, s.get("event", 3000 + i if text else 0))
        struct.pack_into("<q", b, o + 152, s.get("set_at", 1761300000 if text else 0))
    return b


def seal(b):
    b[24:28] = bytes(4)
    struct.pack_into("<I", b, 24, zlib.crc32(bytes(b)) & 0xFFFFFFFF)


# The spec --break expects: three pixels, three programs (the first "const r0 2; in r1 x;
# mul r2 r1 r0; floor r3 r2; out r3"), three lines of which the second is free.
P0, OBJ0, S0, R0 = 3724, 3724 + 576, 18672, 3208


def pk(fmt, off, *v):
    return lambda b: struct.pack_into(fmt, b, off, *v)


def setb(off, v):
    return lambda b: b.__setitem__(off, v)


def fill(off, data):
    def f(b):
        b[off:off + len(data)] = data
    return f


def code(i, op, d, a, bb):
    return fill(OBJ0 + 268 + 4 * i, bytes((op, d, a, bb)))


def big_object(b):
    b[OBJ0:OBJ0 + OBJ] = assemble("in r0 x;" + "pow r1 r0 r0;" * 51 + "out r1").obj()


BREAKS = {
    "magic": setb(0, ord("X")),
    "version": pk("<I", 8, 2),
    "size-field": pk("<I", 12, SIZE + 1),
    "seq": pk("<Q", 16, 0),
    "flags": pk("<I", 28, 32),
    "mode-empty": fill(40, bytes(16)),
    "mode-char": setb(40, ord("N")),
    "mode-nul": fill(40, b"a" * 16),
    "mode-tail": setb(55, ord("x")),
    "bot-at": setb(56, ord("x")),
    "bot-char": setb(57, ord("T")),
    "bot-long": fill(56, b"@" + b"a" * 55),
    "bot-tail": setb(119, ord("a")),
    "announce-kind": fill(120, struct.pack("<IBB", 5, 4, 0)),
    "announce-none": fill(120, struct.pack("<IBB", 5, 0, 0)),
    "announce-event": fill(120, struct.pack("<IBB", 0, 1, 0)),
    "announce-pixel-item": fill(120, struct.pack("<IBB", 5, 1, 1)),
    "announce-pixel-none": lambda b: (fill(R0, bytes(64 * 8))(b), fill(120, struct.pack("<IBB", 5, 1, 0))(b)),
    "announce-prog-item": fill(120, struct.pack("<IBB", 5, 2, 3)),
    "announce-slot-item": fill(120, struct.pack("<IBB", 5, 3, 3)),
    "announce-slot-free": fill(120, struct.pack("<IBB", 5, 3, 1)),
    "pixel-secs": setb(126, 4),
    "text-secs": setb(127, 61),
    "canvas": setb(128 + 64 * 7 + 5, 16),
    "reserved-pixels": setb(3204, 1),
    "recent-used": setb(R0 + 3, 2),
    "recent-gap": fill(R0, bytes(64)),
    "recent-dirty": setb(R0 + 64 * 3 + 10, 1),
    "recent-x": setb(R0, 64),
    "recent-y": setb(R0 + 1, 48),
    "recent-color": setb(R0 + 2, 16),
    "recent-event": pk("<I", R0 + 4, 0),
    "recent-author": setb(R0 + 8, ord("x")),
    "nprog": setb(3720, 9),
    "reserved-progs": setb(3721, 1),
    "prog-dirty": setb(P0 + 1868 * 3 + 100, 1),
    "prog-event": pk("<I", P0, 0),
    "prog-secs": setb(P0 + 4, 61),
    "prog-reserved": setb(P0 + 5, 1),
    "prog-author": setb(P0 + 9, ord("B")),
    "prog-source-empty": fill(P0 + 64, bytes(512)),
    "prog-source-char": setb(P0 + 64, 0x7F),
    "prog-source-long": fill(P0 + 64, b"x" * 481),
    "prog-source-511": fill(P0 + 64, b"x" * 511),
    "nslots": setb(18668, 9),
    "reserved-slots": setb(18669, 1),
    "slot-dirty": setb(S0 + 160 * 3 + 30, 1),
    "slot-label-empty": fill(S0, bytes(24)),
    "slot-label-char": setb(S0, 0xAD),
    "slot-text-char": setb(S0 + 24, 0x80),
    "slot-text-nul": fill(S0 + 24, b"ab\0c"),
    "slot-author-free": fill(S0 + 160 + 88, b"@x"),
    "slot-author-missing": fill(S0 + 88, bytes(56)),
    "slot-event-free": pk("<I", S0 + 160 + 144, 1),
    "slot-event-missing": pk("<I", S0 + 144, 0),
    "slot-reserved": setb(S0 + 148, 1),
    "slot-setat-free": pk("<q", S0 + 160 + 152, 5),
    "obj-version": setb(OBJ0, 2),
    "obj-nout": setb(OBJ0 + 1, 2),
    "obj-out-unused": setb(OBJ0 + 3, 1),
    "obj-nconst": setb(OBJ0 + 5, 65),
    "obj-nins-zero": pk("<H", OBJ0 + 6, 0),
    "obj-nins-big": pk("<H", OBJ0 + 6, 257),
    "obj-reserved": setb(OBJ0 + 10, 1),
    "obj-const-nan": pk("<I", OBJ0 + 12, 0x7FC00000),
    "obj-const-inf": pk("<I", OBJ0 + 12, 0x7F800000),
    "obj-const-subnormal": pk("<I", OBJ0 + 12, 0x80400000),
    "obj-const-unused": pk("<I", OBJ0 + 16, 1),
    "obj-op-unknown": code(0, 0x05, 0, 0, 0),
    "obj-op-zero": code(0, 0x00, 0, 0, 0),
    "obj-k-range": code(0, 0x01, 0, 1, 0),
    "obj-k-b": code(0, 0x01, 0, 0, 1),
    "obj-in-range": code(1, 0x02, 1, 7, 0),
    "obj-in-b": code(1, 0x02, 1, 0, 1),
    "obj-u-b": code(3, 0x32, 3, 2, 1),
    "obj-reg": code(2, 0x12, 32, 1, 0),
    "obj-read": code(2, 0x12, 2, 1, 5),
    "obj-sel": code(3, 0x40, 6, 1, 2),
    "obj-out": setb(OBJ0 + 2, 9),
    "obj-ins-unused": code(200, 0x01, 0, 0, 0),
    "obj-cost": pk("<H", OBJ0 + 8, 7),
    "obj-cost-big": big_object,
}
UNSEALED = {"crc", "size-long", "size-short"}

BREAK_SPEC = {
    "pixels": [{"x": 1, "y": 2, "color": 2}, {"x": 3, "y": 4, "color": 5}, {"x": 5, "y": 6, "color": 7}],
    "progs": [{"asm": "const r0 2; in r1 x; mul r2 r1 r0; floor r3 r2; out r3"},
              {"asm": "in r0 y; out r0"}, {"asm": "in r0 x; in r1 y; bxor r2 r0 r1; out r2"}],
    "slots": [{"label": "GRUSS", "text": "Hallo"}, {"label": "SUCHE"}, {"label": "BIETE", "text": "Nix"}],
    "announce": {"kind": "dem0", "item": 0, "event": 2000},
}


def write(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


# ---------- conformance vectors (docs/dem0.md, 10) ----------

def num(v):
    return "%.9g" % v


VECTORS = [
    # name, program, points (x, y, t); the comment says what a reader can check by hand
    ("palette-x", "in r0 x; out r0", [(0, 0, 0), (17, 3, 0), (359, 287, 9.5)]),  # w = x
    ("rgb-xy", "in r0 x; in r1 y; in r2 t; out r0 r1 r2", [(1, 2, 3.5), (359, 287, 0)]),  # w = x, y, t
    ("inputs-uv", "in r0 u; in r1 v; in r2 r; out r0 r1 r2", [(180, 144, 0), (0, 0, 0), (359, 287, 0), (36, 288, 0)]),
    ("input-a", "in r0 a; out r0", [(180, 144, 0), (0, 144, 0), (180, 0, 0), (324, 0, 0), (0, 287, 0)]),
    ("xor", "in r0 x; in r1 y; bxor r2 r0 r1; out r2", [(3, 5, 0), (255, 1, 0), (300, 200, 0)]),  # 3^5 = 6
    ("arith", "in r0 x; in r1 y; add r2 r0 r1; sub r3 r0 r1; mul r4 r2 r3; out r2 r3 r4", [(7, 3, 0), (0.5, 0.25, 0)]),
    ("div", "in r0 x; in r1 y; div r2 r0 r1; out r2", [(1, 3, 0), (5, 0, 0), (-5, 0, 0), (0, 0, 0)]),  # /0 is 0
    ("mod", "in r0 x; in r1 y; mod r2 r0 r1; out r2", [(7, 3, 0), (-7, 3, 0), (7, -3, 0), (5.5, 2, 0), (1, 0, 0)]),
    ("minmax", "in r0 x; in r1 y; min r2 r0 r1; max r3 r0 r1; out r2 r3 r2", [(2, 9, 0), (9, 2, 0)]),
    ("pow", "in r0 x; in r1 y; pow r2 r0 r1; out r2", [(2, 10, 0), (-2, 3, 0), (-2, 0.5, 0), (0, -1, 0), (0, 0, 0), (9, 0.5, 0)]),
    ("atan2", "in r0 x; in r1 y; atan2 r2 r0 r1; out r2", [(1, 1, 0), (0, -1, 0), (-1, 0, 0), (3, 4, 0)]),
    ("hypot", "in r0 x; in r1 y; hypot r2 r0 r1; out r2", [(3, 4, 0), (5, 12, 0), (0, 0, 0)]),
    ("step", "in r0 x; in r1 y; step r2 r0 r1; out r2", [(5, 4, 0), (5, 5, 0), (5, 6, 0)]),  # y < x ? 0 : 1
    ("compare", "in r0 x; in r1 y; eq r2 r0 r1; ne r3 r0 r1; lt r4 r0 r1; le r5 r0 r1; add r6 r2 r3;"
     " add r7 r4 r5; out r2 r6 r7", [(1, 2, 0), (2, 2, 0), (3, 2, 0)]),
    ("logic", "in r0 x; in r1 y; and r2 r0 r1; or r3 r0 r1; not r4 r0; out r2 r3 r4", [(0, 0, 0), (0, 5, 0), (3, 5, 0)]),
    ("bits", "in r0 x; in r1 y; band r2 r0 r1; bor r3 r0 r1; bnot r4 r0; out r2 r3 r4", [(12, 10, 0), (255, 256, 0), (0, 0, 0)]),
    ("shift", "in r0 x; in r1 y; shl r2 r0 r1; shr r3 r0 r1; out r2 r3 r3", [(1, 4, 0), (256, 4, 0), (1, 31, 0), (-16, 2, 0), (1, 33, 0)]),
    ("shift-neg", "in r0 x; neg r1 r0; in r2 y; shr r3 r1 r2; shl r4 r1 r2; out r3 r4 r1", [(16, 2, 0), (1, 31, 0), (7, 0, 0)]),
    ("I-big", "const r0 3000000000; const r1 -2147483648; const r2 2147483648; bor r3 r0 r0; bor r4 r1 r1;"
     " bor r5 r2 r2; out r3 r4 r5", [(0, 0, 0)]),  # 3e9 - 2^32, -2^31, -2^31
    ("I-huge", "const r0 4294967808; const r1 -4294967552; const r2 1e30; bor r3 r0 r0; bor r4 r1 r1;"
     " bor r5 r2 r2; out r3 r4 r5", [(0, 0, 0)]),
    ("I-trunc", "in r0 x; neg r1 r0; bor r2 r0 r0; bor r3 r1 r1; out r2 r3 r3", [(1.7, 0, 0), (0.5, 0, 0), (255.9, 0, 0)]),
    ("neg-abs", "in r0 x; neg r1 r0; abs r2 r1; out r1 r2 r2", [(3, 0, 0), (0, 0, 0)]),
    ("floor-ceil", "in r0 x; floor r1 r0; ceil r2 r0; round r3 r0; out r1 r2 r3",
     [(1.5, 0, 0), (-1.5, 0, 0), (2.5, 0, 0), (-0.5, 0, 0), (0.49999997, 0, 0), (7, 0, 0)]),
    ("fract", "in r0 x; fract r1 r0; out r1", [(1.25, 0, 0), (-1.25, 0, 0), (3, 0, 0)]),
    ("sqrt", "in r0 x; sqrt r1 r0; out r1", [(16, 0, 0), (2, 0, 0), (-4, 0, 0), (0, 0, 0)]),
    ("sin", "in r0 t; sin r1 r0; out r1", [(0, 0, 0), (0, 0, 0.5), (0, 0, 1.5707964), (0, 0, 3.1415927), (0, 0, 100),
                                            (0, 0, -2.25), (0, 0, 3290000), (0, 0, 3300000), (0, 0, 4000000)]),
    ("cos", "in r0 t; cos r1 r0; out r1", [(0, 0, 0), (0, 0, 1), (0, 0, 3.1415927), (0, 0, -7.5), (0, 0, 3300000)]),
    ("exp-log", "in r0 x; exp r1 r0; log r2 r0; out r1 r2 r2", [(0, 0, 0), (1, 0, 0), (-1, 0, 0), (10, 0, 0), (100, 0, 0)]),
    ("inf-nan", "const r0 99; exp r1 r0; sub r2 r1 r1; neg r3 r1; out r1 r2 r3", [(0, 0, 0)]),  # inf, nan, -inf
    ("nan-ops", "const r0 99; exp r1 r0; sub r2 r1 r1; sin r3 r1; cos r4 r2; bor r5 r2 r2; sign r6 r2;"
     " add r7 r3 r4; add r7 r7 r5; add r7 r7 r6; sqrt r8 r2; log r9 r2; out r7 r8 r9", [(0, 0, 0)]),
    ("nan-compare", "const r0 99; exp r1 r0; sub r2 r1 r1; eq r3 r2 r2; ne r4 r2 r2; and r5 r2 r2;"
     " min r6 r2 r1; out r3 r4 r5", [(0, 0, 0)]),
    ("sign", "in r0 x; sign r1 r0; out r1", [(5, 0, 0), (-5, 0, 0), (0, 0, 0)]),
    ("rnd", "in r0 x; rnd r1 r0; out r1", [(0, 0, 0), (1, 0, 0), (1.9, 0, 0), (-1, 0, 0), (12345, 0, 0)]),
    ("sel", "in r0 x; in r1 y; in r2 t; sel r0 r1 r2; out r0", [(1, 7, 9), (0, 7, 9)]),  # x ? y : t
    ("mov", "in r0 x; mov r1 r0; mov r1 r1; out r1", [(42, 0, 0)]),
    ("plasma", "in r0 x; const r1 16; div r2 r0 r1; in r3 t; add r4 r2 r3; sin r5 r4; const r6 40; mul r7 r5 r6;"
     " in r8 y; const r9 8; div r10 r8 r9; sub r11 r10 r3; sin r12 r11; mul r13 r12 r6; add r14 r7 r13;"
     " in r15 r; const r16 90; mul r17 r15 r16; add r18 r14 r17; out r18",
     [(0, 0, 0), (100, 50, 1.25), (359, 287, 17.5)]),  # sin(x/16+t)*40+sin(y/8-t)*40+r*90
    ("tunnel", "in r0 r; const r1 0.1; add r2 r0 r1; const r3 1; div r4 r3 r2; in r5 a; const r6 4;"
     " mul r7 r5 r6; add r8 r7 r4; in r9 t; const r10 3; mul r11 r9 r10; add r12 r8 r11; sin r13 r12;"
     " const r14 99; mul r15 r13 r14; const r16 9; mul r17 r4 r16; add r18 r15 r17; out r18",
     [(180, 144, 0), (10, 20, 2.5), (300, 100, 7)]),  # d=1/(r+.1); s=a*4+d+t*3; sin(s)*99+d*9
]


def vectors():
    print("# dem0 conformance vectors (docs/dem0.md, section 10), written by tests/mksnap.py vectors")
    print("# from the document's semantics in float32, independently of the bridge's Go code.")
    for name, asm, points in VECTORS:
        p = assemble(asm)
        print("# " + " ".join(asm.split()))
        print("prog %s %s" % (name, p.obj().hex()))
        for x, y, t in points:
            w = run(p, x, y, t)
            print("at %s %s %s %s" % (num(f32(x)), num(f32(y)), num(f32(t)), " ".join(num(v) for v in w)))


def main(argv):
    if argv[:1] == ["vectors"]:
        vectors()
        return 0
    if argv[:1] == ["breaks"]:
        print("\n".join(sorted(list(BREAKS) + list(UNSEALED))))
        return 0
    if argv[:1] == ["object"]:
        print(assemble(argv[1]).obj().hex())
        return 0
    if argv[:1] == ["vga"]:
        print(*vga(int(argv[1])))
        return 0
    full = "--full" in argv
    if full:
        argv.remove("--full")
    rule = None
    if "--break" in argv:
        i = argv.index("--break")
        rule = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if not argv:
        print(__doc__ or "usage: mksnap.py <out> [spec.json|-] [--break <rule>]", file=sys.stderr)
        return 2
    out = argv[0]
    if len(argv) > 1:
        spec = json.load(sys.stdin if argv[1] == "-" else open(argv[1]))
    else:
        spec = BREAK_SPEC if rule or full else {}
    b = snapshot(spec)
    if rule in BREAKS:
        BREAKS[rule](b)
    elif rule and rule not in UNSEALED:
        raise SystemExit("mksnap: no such rule: " + rule)
    seal(b)
    if rule == "crc":
        b[128 + 100] ^= 1
    elif rule == "size-long":
        b += b"\0"
    elif rule == "size-short":
        b = b[:-1]
    write(out, bytes(b))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
