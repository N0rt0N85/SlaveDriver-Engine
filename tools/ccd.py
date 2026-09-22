#!/usr/bin/env python3
"""ccd.py -- a raw MODE1/2352 + CD-DA image -> CloneCD (.ccd / .img / .sub).

The Phoebe ODE (RMENU card) plays CD-DA only from CloneCD images: every commercial game on the
owner's card is one (529 folders), and a .cue is mounted as ONE data track -- four .cue writings
(Redump multi-file, one .bin with INDEX 00, PREGAP, INDEX 01 alone), our disc and the retail
Powerslave alike, never put track 2 in the console's TOC (HWPROBE toc line, 2026-09-22).

Laid out as the owner's working dumps are (`--check` rebuilds one byte for byte):
  .img  every sector from LBA 0 to the lead-out.  The 2 s before an audio track belong to the track
        before it: no INDEX 0 anywhere.
  .sub  96 bytes a sector, deinterleaved.  P (12) = FF on the first sector of each track, 0
        elsewhere.  Q (12) = mode-1 position: control/ADR, track, index 01, time in the track, 0,
        absolute time (BCD), CRC-16 CCITT inverted, big-endian.  R-W (72) = 0.
  .ccd  [CloneCD] Version=3, points A0/A1/A2 then one entry per track, [TRACK n] MODE / INDEX 1.

usage: python tools/ccd.py IMAGE OUT_BASE LBA:MODE [LBA:MODE ...]   (MODE 1 = data, 0 = audio)
         -> OUT_BASE.ccd, OUT_BASE.img (IMAGE copied), OUT_BASE.sub
       python tools/ccd.py --check DUMP.ccd    rebuild DUMP.ccd and DUMP.sub from their own tracks
"""
import os
import re
import shutil
import sys


def bcd(n):
    return ((n // 10) << 4) | (n % 10)


def msf(lba):
    """-> (m, s, f) of an absolute address: LBA 0 = 00:02:00"""
    a = lba + 150
    return a // 4500, (a // 75) % 60, a % 75


def crc16(buf):
    c = 0
    for b in buf:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
            c &= 0xFFFF
    return c ^ 0xFFFF


def control(mode):
    return 0x04 if mode else 0x00


def sub(tracks, total):
    """tracks = [(start LBA, mode)] in order; -> the .sub bytes"""
    out = bytearray(total * 96)
    bounds = [t[0] for t in tracks] + [total]
    for i, (start, mode) in enumerate(tracks):
        for lba in range(start, bounds[i + 1]):
            o = lba * 96
            if lba == start:
                out[o:o + 12] = b"\xff" * 12
            rel = lba - start
            q = bytearray((control(mode) << 4 | 1, bcd(i + 1), 1,
                           bcd(rel // 4500), bcd((rel // 75) % 60), bcd(rel % 75), 0))
            q += bytes(bcd(x) for x in msf(lba))
            q += crc16(q).to_bytes(2, "big")
            out[o + 12:o + 24] = q
    return bytes(out)


def ccd(tracks, total):
    """-> the .ccd text"""
    first, last = tracks[0][1], tracks[-1][1]

    def entry(k, point, ctl, pm, ps, pf, plba):
        return (f"[Entry {k}]\r\nSession=1\r\nPoint=0x{point:02x}\r\nADR=0x01\r\nControl=0x{ctl:02x}\r\n"
                f"TrackNo=0\r\nAMin=0\r\nASec=0\r\nAFrame=0\r\nALBA=-150\r\nZero=0\r\n"
                f"PMin={pm}\r\nPSec={ps}\r\nPFrame={pf}\r\nPLBA={plba}\r\n")
    t = (f"[CloneCD]\r\nVersion=3\r\n[Disc]\r\nTocEntries={3 + len(tracks)}\r\nSessions=1\r\n"
         f"DataTracksScrambled=0\r\nCDTextLength=0\r\n[Session 1]\r\nPreGapMode={first}\r\n"
         f"PreGapSubC=0\r\n")
    t += entry(0, 0xA0, control(first), 1, 0, 0, 1 * 4500 - 150)
    t += entry(1, 0xA1, control(last), len(tracks), 0, 0, len(tracks) * 4500 - 150)
    t += entry(2, 0xA2, control(last), *msf(total), total)
    for i, (start, mode) in enumerate(tracks):
        t += entry(3 + i, i + 1, control(mode), *msf(start), start)
    for i, (start, mode) in enumerate(tracks):
        t += f"[TRACK {i + 1}]\r\nMODE={mode}\r\nINDEX 1={start}\r\n"
    return t


def write(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def check(ccd_path):
    text = open(ccd_path, "rb").read().decode("latin-1")
    base = os.path.splitext(ccd_path)[0]
    tracks = [(int(i), int(m)) for m, i in re.findall(r"MODE=(\d+)\r\nINDEX 1=(\d+)", text)]
    total = int(re.search(r"Point=0xa2\r\n(?:.*\r\n)*?PLBA=(\d+)", text).group(1))
    ok_ccd = ccd(tracks, total).encode() == text.encode("latin-1")
    ok_sub = sub(tracks, total) == open(base + ".sub", "rb").read()
    print(f"{os.path.basename(ccd_path)}: {len(tracks)} tracks, {total} sectors: "
          f".ccd {'IDENTICAL' if ok_ccd else 'DIFFERENT'}, .sub {'IDENTICAL' if ok_sub else 'DIFFERENT'}")
    return ok_ccd and ok_sub


def main(argv):
    if argv[:1] == ["--check"]:
        return 0 if all([check(p) for p in argv[1:]]) else 1
    if len(argv) < 3:
        raise SystemExit(__doc__.strip().splitlines()[-3])
    image, base = argv[0], argv[1]
    size = os.path.getsize(image)
    if size % 2352:
        raise SystemExit(f"{image}: {size} bytes, not whole 2352-byte sectors")
    total = size // 2352
    tracks = [tuple(int(x) for x in a.split(":")) for a in argv[2:]]
    if tracks[0][0] != 0 or any(b[0] <= a[0] for a, b in zip(tracks, tracks[1:])):
        raise SystemExit("track starts must begin at 0 and rise")
    write(base + ".ccd", ccd(tracks, total).encode())
    write(base + ".sub", sub(tracks, total))
    if os.path.abspath(image) != os.path.abspath(base + ".img"):
        shutil.copyfile(image, base + ".img.tmp")
        os.replace(base + ".img.tmp", base + ".img")
    print(f"  CCD: {os.path.basename(base)} ({len(tracks)} tracks, {total} sectors)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
