#!/usr/bin/env python3
"""Prove the packed OPL4TAB.H means exactly what the unpacked one meant.

A digest of every value that reaches the chip - for all 129 programs and
all 128 notes, the regions the engine would select and the fields it would
write - computed from each layout and compared.  Storage is not part of
the digest, so it survives any repacking that preserves meaning.

    verifytab.py <old-header> <new-header>
"""
import re, sys, zlib

def arr(t, name):
    """The body of an array DEFINITION - the name also occurs in comments."""
    m = re.search(r"alsa_%s\[\d+\] = \{(.*?)\n\};" % name, t, re.S)
    assert m, name
    return m.group(1)

def parse_old(t):
    reg = [tuple(int(x, 0) for x in m) for m in re.findall(
        r"\{\s*(\d+),\s*(\d+),(0x[0-9a-f]+),\s*(-?\d+),\s*(\d+),\s*(-?\d+),"
        r"(0x[0-9a-f]+),(0x[0-9a-f]+),(0x[0-9a-f]+),(0x[0-9a-f]+),"
        r"(0x[0-9a-f]+),(0x[0-9a-f]+),(0x[0-9a-f]+)\}", arr(t, "reg"))]
    prog = [(int(a), int(b)) for a, b in re.findall(
        r"\{\s*(\d+),\s*(\d+)\},", arr(t, "prog"))]
    assert len(reg) == 610 and len(prog) == 129, (len(reg), len(prog))
    out = []
    for base, n in prog:
        out.append([reg[base + i] for i in range(n)])
    return out

def parse_new(t):
    body = lambda name, pat: [tuple(int(x, 0) for x in m)
                              for m in re.findall(pat, arr(t, name))]
    lvl = body("lvl", r"\{\s*(\d+),\s*(-?\d+),(0x[0-9a-f]+),(0x[0-9a-f]+)\}")
    env = body("env", r"\{(0x[0-9a-f]+),(0x[0-9a-f]+),(0x[0-9a-f]+),"
                      r"(0x[0-9a-f]+),(0x[0-9a-f]+)\}")
    reg = body("reg", r"\{\s*(\d+),\s*(\d+),(0x[0-9a-f]+),\s*(-?\d+),"
                      r"\s*(\d+),\s*(\d+)\}")
    starts = [int(x) for x in re.findall(r"-?\d+", arr(t, "prog"))]
    assert len(reg) == 610 and len(starts) == 130, (len(reg), len(starts))
    assert len(lvl) == 126, len(lvl)
    out = []
    for p in range(129):
        rows = []
        for i in range(starts[p], starts[p + 1]):
            lo, hi, tone, pofs, li, ei = reg[i]
            ksc, pan, att, vf = lvl[li]
            rows.append((lo, hi, tone, pofs, ksc, pan, att, vf) + env[ei])
        out.append(rows)
    return out

def digest(progs):
    """What the engine would send, for every program and every note."""
    h = zlib.crc32(b"")
    n = 0
    for p in range(129):
        for note in range(128):
            picked = 0
            for r in progs[p]:
                if r[0] <= note <= r[1]:            # lo..hi, as note_on does
                    h = zlib.crc32((",".join(map(str, r[2:])) + ";").encode(), h)
                    n += 1
                    picked += 1
                    if picked == 2:                 # engine keys at most 2
                        break
            h = zlib.crc32(b"|", h)
    return h, n

old, new = (open(a).read() for a in sys.argv[1:3])
a, na = digest(parse_old(old))
b, nb = digest(parse_new(new))
print("old: crc %08X over %d region hits" % (a, na))
print("new: crc %08X over %d region hits" % (b, nb))
print("IDENTICAL" if a == b and na == nb else "*** MISMATCH ***")
sys.exit(0 if a == b and na == nb else 1)
