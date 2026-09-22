#!/usr/bin/env python3
"""ovlpack.py -- overlays: code linked against MAIN, kept on the disc, loaded by OVL.C ovl_run.

An overlay is a set of objects (game/doom/ovl/*.C) linked with MAIN's symbols as absolute
addresses (ld --just-symbols=MAIN.elf, saturn_ovl.ld) and packed into one file.  MAIN keeps a
small loader (OVL.C) and pays nothing else: the menu code is read into idle RAM when it is
needed, relocated, run and forgotten.  See docs/PORTING_NOTES.md "Overlays".

    ovlpack.py --roots NM OUT OBJ... [--title OBJ...]
        what the overlay objects use and do not define themselves, as `-Wl,-u,SYM` flags written
        to OUT (atomically) for MAIN's link: MAIN's --gc-sections then keeps every function and
        datum the overlay calls, and a symbol MAIN lacks is pulled into MAIN from its libraries --
        never into the overlay, which links no library (a private copy of a library's state would
        silently diverge from MAIN's).  COMMON symbols are rooted too (they must resolve to MAIN's).
        Fails on a DENIED symbol: the overlay runs while the game is frozen, from memory the
        renderer owns, so it must not call what draws, swaps the VDP1 or writes that memory.
        The objects after --title run only at the title, from the level pool (OVL_POOL), where
        the renderer's memory is not theirs: they may draw.  So that nothing frozen can reach
        them, only the object that holds the entry table (ovlEntries) may name their symbols.
    ovlpack.py --buildid OUT.c FILE...
        CRC-32 over FILE... (MAIN's objects, its linker script, the roots): writes
        `const unsigned int ovlBuildId=0x...;` to OUT.c (only when it changes).  Any change that can
        move a MAIN symbol changes the id; ovl_run refuses a file whose id is not MAIN's.
    ovlpack.py --pack NM OUT A.elf A.bin B.bin MAIN.elf ROOTS OBJ...
        A and B are the same overlay linked at 0x0A000000 and 0x0B000000.  Every byte that differs
        must sit in a 4-aligned big-endian word that differs by exactly 0x01000000: those words are
        the relocations (anything else -- a 16-bit absolute, a relaxed call -- fails the build).
        Refuses a COMMON symbol of the overlay objects that the link allocated in the overlay
        instead of resolving it to MAIN.  Writes OUT (atomically) and prints the sizes.
    ovlpack.py --verify OVL MAIN.elf
        what ovl_run will say of OVL next to that MAIN: accepted, or refused and why.

File format (big-endian, 32-byte header, then the image, then the relocation table):
    0  'OVL1'   4 buildId   8 linkBase   12 imageBytes   16 bssBytes   20 nRelocs   24 nEntries   28 0
    32 image: byte 0 = the entry table (int (*const ovlEntries[])(int,char *,char *))
    .. u16 relocation[nRelocs]: word offsets (byte offset / 4) into the image, padded to 4 bytes
"""
import os
import re
import struct
import subprocess
import sys
import zlib

MAGIC = b"OVL1"
BASE_A, BASE_B = 0x0A000000, 0x0B000000
HEADER = 32

# Functions the overlay must never call (K1 of the pause design): they draw with the VDP1, swap
# its framebuffers (the frozen image would be lost), or write doorwayCache -- where the overlay
# itself runs.  A name ending in '*' is a prefix.
DENY = ["dlg_*", "loadOverBase", "SCL_DisplayFrame", "EZ_*", "teleportEffect", "drawWalls*",
        "pic_nextFrame"]


def atomic_write(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def c_name(sym):
    """The ELF symbol of a C name carries a leading underscore on this target."""
    return sym[1:] if sym.startswith("_") else sym


def nm_lines(nm, path, *flags):
    out = subprocess.run([nm] + list(flags) + [path], capture_output=True, text=True,
                         check=True).stdout
    return [l.split() for l in out.splitlines() if l.strip()]


def object_symbols(nm, objs):
    """({defined}, {undefined}, {common}) over the objects."""
    defined, undefined, common = set(), set(), set()
    for o in objs:
        for f in nm_lines(nm, o):
            kind, sym = f[-2], f[-1]
            if kind == "U":
                undefined.add(sym)
            elif kind == "C":
                common.add(sym)
            else:
                defined.add(sym)
    return defined, undefined - defined, common - defined


def denied(syms):
    bad = []
    for s in sorted(syms):
        n = c_name(s)
        for d in DENY:
            if (d.endswith("*") and n.startswith(d[:-1])) or n == d:
                bad.append(n)
    return bad


def roots(argv):
    nm, out, objs = argv[0], argv[1], argv[2:]
    title = []
    if "--title" in objs:
        i = objs.index("--title")
        objs, title = objs[:i], objs[i + 1:]
    frozen = objs
    objs = objs + title
    defined, undefined, common = object_symbols(nm, objs)
    _d, frozen_undef, _c = object_symbols(nm, frozen)
    bad = denied(frozen_undef - defined)
    if bad:
        print("*** ovlpack: the overlay calls what it must not (draws, swaps the VDP1 or writes "
              "doorwayCache): %s" % ", ".join(bad))
        return 1
    title_defs = object_symbols(nm, title)[0] if title else set()
    for o in frozen:
        d, u, _c = object_symbols(nm, [o])
        reach = sorted(c_name(s) for s in (u & title_defs))
        if reach and "_ovlEntries" not in d:
            print("*** ovlpack: %s reaches the title-only code (%s): it may draw, and the pause "
                  "must not" % (o, ", ".join(reach)))
            return 1
    flags = " ".join("-Wl,-u," + s for s in sorted(undefined | common))
    atomic_write(out, (flags + "\n").encode("ascii"))
    print("ovlpack: %d MAIN symbol(s) rooted for the overlay (%s)" % (len(undefined | common), out))
    return 0


def buildid(argv):
    out, files = argv[0], argv[1:]
    crc = 0
    for f in sorted(files):
        with open(f, "rb") as h:
            crc = zlib.crc32(h.read(), crc)
    crc &= 0xffffffff
    text = ("/* generated by tools/ovlpack.py --buildid -- do not edit */\n"
            "const unsigned int ovlBuildId=0x%08xu;\n" % crc)
    if not os.path.exists(out) or open(out).read() != text:
        atomic_write(out, text.encode("ascii"))
    print("ovlpack: build id 0x%08x" % crc)
    return 0


def elf_read(path, addr, size):
    """`size` bytes at virtual address `addr` of a 32-bit big-endian ELF (a PROGBITS section)."""
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 2, path
    shoff, = struct.unpack(">I", d[32:36])
    shentsize, shnum = struct.unpack(">HH", d[46:50])
    for i in range(shnum):
        sh = d[shoff + i * shentsize: shoff + (i + 1) * shentsize]
        _n, typ, _fl, a, off, sz = struct.unpack(">IIIIII", sh[:24])
        if typ != 8 and a <= addr and addr + size <= a + sz:       # 8 = SHT_NOBITS
            return d[off + addr - a: off + addr - a + size]
    raise ValueError("%s: 0x%08x not in a loaded section" % (path, addr))


def symbol(nm, elf, name):
    """(address, size) of a symbol of an ELF, or None."""
    for f in nm_lines(nm, elf, "-S", "--defined-only"):
        if f[-1] == name:
            return int(f[0], 16), (int(f[1], 16) if len(f) == 4 else 0)
    return None


def main_build_id(nm, main_elf):
    s = symbol(nm, main_elf, "_ovlBuildId")
    if s is None:
        raise SystemExit("*** ovlpack: %s has no ovlBuildId" % main_elf)
    return struct.unpack(">I", elf_read(main_elf, s[0], 4))[0]


def rooted_symbols(roots_txt):
    return set(re.findall(r"-Wl,-u,(\S+)", open(roots_txt).read()))


def pack(argv):
    nm, out, a_elf, a_bin, b_bin, main_elf, roots_txt = argv[:7]
    objs = argv[7:]
    a, b = open(a_bin, "rb").read(), open(b_bin, "rb").read()
    delta = BASE_B - BASE_A
    if len(a) != len(b) or len(a) & 3:
        print("*** ovlpack: the two links differ in size (%d, %d)" % (len(a), len(b)))
        return 1
    relocs, bad = [], []
    for w in sorted({k & ~3 for k in range(len(a)) if a[k] != b[k]}):
        x, y = struct.unpack(">I", a[w:w + 4])[0], struct.unpack(">I", b[w:w + 4])[0]
        (relocs if (y - x) & 0xffffffff == delta else bad).append(w)
    if bad:
        print("*** ovlpack: %d word(s) differ between the two links by something else than the "
              "base: not relocatable (%s)" % (len(bad), ", ".join("+0x%x" % w for w in bad[:8])))
        return 1
    if len(a) >> 2 > 0xffff:
        print("*** ovlpack: image too big for 16-bit relocations (%d bytes)" % len(a))
        return 1
    img_end = symbol(nm, a_elf, "__ovl_image_end")
    bss_end = symbol(nm, a_elf, "__ovl_bss_end")
    entries = symbol(nm, a_elf, "_ovlEntries")
    if not img_end or not bss_end or not entries or entries[0] != BASE_A:
        print("*** ovlpack: %s: no ovlEntries at the image's first byte" % a_elf)
        return 1
    if img_end[0] - BASE_A != len(a):
        print("*** ovlpack: the image is %d bytes but its end symbol says %d"
              % (len(a), img_end[0] - BASE_A))
        return 1
    bss = bss_end[0] - img_end[0]
    # every relocated word must point into the image or its BSS (one past the end allowed): a
    # section the script did not place would be dropped by objcopy and pointed at past the image
    wild = [w for w in relocs
            if not BASE_A <= struct.unpack(">I", a[w:w + 4])[0] <= bss_end[0]]
    if wild:
        print("*** ovlpack: %d relocated word(s) point outside the image and its BSS (%s)"
              % (len(wild), ", ".join("+0x%x" % w for w in wild[:8])))
        return 1
    # COMMON: every tentative definition of the overlay objects must be MAIN's datum, not a copy
    _d, _u, common = object_symbols(nm, objs)
    private = []
    for s in sorted(common):
        v = symbol(nm, a_elf, s)
        if v is None or BASE_A <= v[0] < bss_end[0]:
            private.append(c_name(s))
    if private:
        print("*** ovlpack: COMMON symbol(s) MAIN does not have, allocated in the overlay (its own "
              "copy would silently diverge): %s" % ", ".join(private))
        return 1
    # a root neither MAIN nor a library of its link defines stayed undefined in MAIN (ld -u does
    # not fail on it): the overlay would then relocate against nothing
    rooted = rooted_symbols(roots_txt)
    missing = sorted(c_name(s) for s in rooted if symbol(nm, main_elf, s) is None)
    if missing:
        print("*** ovlpack: MAIN has no definition for symbol(s) the overlay uses, and no library "
              "gave it one -- they are undefined in MAIN: %s" % ", ".join(missing))
        return 1
    bid = main_build_id(nm, main_elf)
    table = struct.pack(">%dH" % len(relocs), *[w >> 2 for w in relocs])
    table += b"\0" * (-len(table) & 3)
    head = MAGIC + struct.pack(">7I", bid, BASE_A, len(a), bss, len(relocs), entries[1] // 4, 0)
    atomic_write(out, head + a + table)
    # what the roots keep in MAIN: at most the size of the rooted symbols (the ones MAIN reaches
    # anyway cost nothing -- the _end of the MAP tells the net)
    sizes = {f[-1]: int(f[1], 16) for f in nm_lines(nm, main_elf, "-S", "--defined-only")
             if len(f) == 4}
    kept = sum(sizes.get(s, 0) for s in rooted)
    print("ovlpack: %s: image %d B, BSS %d B, %d relocation(s) (%d B), %d entr%s, 0 COMMON of "
          "its own (%d resolved to MAIN), build id 0x%08x; file %d B; the %d MAIN symbols it "
          "uses total %d B" % (out, len(a), bss, len(relocs), len(table), entries[1] // 4,
                              "y" if entries[1] == 4 else "ies", len(common), bid,
                              HEADER + len(a) + len(table), len(rooted), kept))
    return 0


def verify(argv):
    ovl, main_elf = argv[0], argv[1]
    nm = argv[2] if len(argv) > 2 else "sh2eb-elf-nm"
    d = open(ovl, "rb").read()
    bid = main_build_id(nm, main_elf)
    if len(d) < HEADER or d[:4] != MAGIC:
        print("%s: REFUSED (not an overlay)" % ovl)
        return 1
    have = struct.unpack(">I", d[4:8])[0]
    if have != bid:
        print("%s: REFUSED (build id 0x%08x, this MAIN's is 0x%08x)" % (ovl, have, bid))
        return 1
    # the rest of ovl_run's test (OVL.C): entries, and the length the header announces
    image, n_rel, n_ent = struct.unpack(">I", d[12:16])[0], *struct.unpack(">2I", d[20:28])
    if n_ent == 0 or len(d) != HEADER + image + ((n_rel * 2 + 3) & ~3):
        print("%s: REFUSED (%d entries, file %d B for a %d B image and %d relocations)"
              % (ovl, n_ent, len(d), image, n_rel))
        return 1
    print("%s: accepted (build id 0x%08x)" % (ovl, bid))
    return 0


def main(argv):
    ops = {"--roots": (roots, 3), "--buildid": (buildid, 2), "--pack": (pack, 8),
           "--verify": (verify, 2)}
    if len(argv) >= 2 and argv[1] in ops and len(argv) - 2 >= ops[argv[1]][1]:
        return ops[argv[1]][0](argv[2:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
