#!/usr/bin/env python3
"""R3 -- mesures Duke shareware (DUKE3D.GRP) : ART, tuiles distinctes par carte, VOC, familles de monstres."""
import os, re, struct, sys
from collections import Counter, defaultdict

SD = r"C:\Users\pcico\Projects\SlaveDriver-Engine"
sys.path.insert(0, os.path.join(SD, "tools"))
import grp as grpmod
import buildmap

GRP = os.path.join(SD, "refs", "build", "duke13", "DUKE3D.GRP")
NAMES_H = os.path.join(SD, "refs", "build", "jfduke3d", "src", "names.h")
CELL = 64
TRANSP = 255


def load_art(g):
    out = {}
    per_file = []
    for name in sorted(n for n in g.names() if n.upper().startswith("TILES") and n.upper().endswith(".ART")):
        d = g.read(name)
        ver, numtiles, first, last = struct.unpack("<4i", d[:16])
        n = last - first + 1
        p = 16
        sx = struct.unpack("<%dh" % n, d[p:p + 2 * n]); p += 2 * n
        sy = struct.unpack("<%dh" % n, d[p:p + 2 * n]); p += 2 * n
        anm = struct.unpack("<%di" % n, d[p:p + 4 * n]); p += 4 * n
        cnt = 0; by = 0
        for i in range(n):
            sz = sx[i] * sy[i]
            if sz:
                out[first + i] = (sx[i], sy[i], d[p:p + sz], anm[i])
                cnt += 1; by += sz
            p += sz
        per_file.append((name, first, last, cnt, by))
    return out, per_file


def rle_bytes(entry):
    """RLE moteur (PIC.C map) d'une tuile ART decoupee en chunks 64x64 ; 255 = transparent -> 0."""
    sx, sy, px, _ = entry
    cx, cy = (sx + CELL - 1) // CELL, (sy + CELL - 1) // CELL
    total = 0
    for ty in range(cy):
        for tx in range(cx):
            vals = []
            for y in range(CELL):
                yy = ty * CELL + y
                for x in range(CELL):
                    xx = tx * CELL + x
                    if xx < sx and yy < sy:
                        v = px[xx * sy + yy]
                        vals.append(0 if v == TRANSP else (TRANSP if v == 0 else v))
                    else:
                        vals.append(0)
            i = 0; n = len(vals)
            while i < n:
                z = 0
                while i < n and vals[i] == 0 and z < 255:
                    z += 1; i += 1
                lit = 0; j = i
                while j < n and vals[j] != 0 and lit < 255:
                    lit += 1; j += 1
                total += 2 + lit
                i = j
    return total, cx * cy


def voc_rate(d):
    """Premier bloc VOC de type 1 (8 bits) ou 9 (nouveau format) -> Hz."""
    p = struct.unpack("<H", d[20:22])[0]
    while p + 4 <= len(d):
        t = d[p]
        if t == 0:
            break
        sz = d[p + 1] | (d[p + 2] << 8) | (d[p + 3] << 16)
        if t == 1:
            return 1000000 // (256 - d[p + 4]), 8
        if t == 9:
            rate = struct.unpack("<I", d[p + 4:p + 8])[0]
            return rate, d[p + 8]
        p += 4 + sz
    return None, None


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    g = grpmod.GrpFile(GRP)
    art, per_file = load_art(g)
    print(f"DUKE3D.GRP : {len(g.entries)} entrees")
    print("ART : %d tuiles non vides, %s o de pixels" % (len(art), f"{sum(e[0]*e[1] for e in art.values()):,}"))
    for name, first, last, cnt, by in per_file:
        print(f"  {name:13s} {first:5d}-{last:5d} {cnt:4d} tuiles {by:9,} o")
    # VOC
    vocs = [e for e in g.entries if e.name.upper().endswith(".VOC")]
    rates = Counter(); tot = 0
    for e in vocs:
        d = g.read(e)
        r, bps = voc_rate(d)
        rates[(r, bps)] += 1; tot += e.size
    print(f"VOC : {len(vocs)} fichiers, {tot:,} o ; (rate, bps) -> n : {dict(rates.most_common(8))}")
    for ext in ("CON", "MID", "DAT", "BIN", "TMB"):
        es = [e for e in g.entries if e.name.upper().endswith("." + ext)]
        print(f"{ext} : {len(es)} fichiers, {sum(e.size for e in es):,} o : {', '.join(e.name for e in es)}")

    # familles de monstres depuis names.h
    defs = {}
    for m in re.finditer(r"#define\s+([A-Z0-9_]+)\s+(\d+)", open(NAMES_H, encoding="latin-1").read()):
        defs[m.group(1)] = int(m.group(2))
    FAM = ["APLAYER", "SHARK", "LIZTROOP", "OCTABRAIN", "DRONE", "COMMANDER", "RECON", "PIGCOP", "LIZMAN",
           "ROTATEGUN", "GREENSLIME", "ORGANTIC", "BOSS1", "BOSS2", "BOSS3", "NEWBEAST", "EGG"]
    bases = sorted((defs[f], f) for f in FAM if f in defs)
    # borne haute = prochain define (toute famille) strictement plus grand qui n'est pas de la meme famille
    allvals = sorted(set(defs.values()))
    fam_range = {}
    for k, (v, f) in enumerate(bases):
        # fin heuristique : prochaine base de famille, ou +200
        nxt = bases[k + 1][0] if k + 1 < len(bases) else v + 200
        # resserrer : dernier define < nxt dont le nom commence par le prefixe de la famille
        pre = f[:4]
        own = [defs[n] for n in defs if n.startswith(pre) and v <= defs[n] < nxt]
        hi = max(own) + 1 if own else v + 1
        # etendre jusqu'a la derniere tuile non vide contigue apres hi (frames non nommees)
        while hi < nxt and (hi in art):
            hi += 1
        fam_range[f] = (v, hi)
    print("\nFamilles (names.h) : plage [est] = base .. derniere tuile non vide contigue avant la famille suivante")
    print("  %-10s %5s %5s %5s %6s %8s %9s" % ("famille", "base", "fin", "tuiles", "chunks", "raw_o", "RLE_o"))
    fam_stats = {}
    for f, (lo, hi) in fam_range.items():
        tiles = [t for t in range(lo, hi) if t in art]
        ch = 0; rl = 0; raw = 0
        for t in tiles:
            r, c = rle_bytes(art[t]); ch += c; rl += r; raw += art[t][0] * art[t][1]
        fam_stats[f] = (lo, hi, len(tiles), ch, raw, rl)
        print("  %-10s %5d %5d %5d %6d %8d %9d" % (f, lo, hi - 1, len(tiles), ch, raw, rl))

    # cartes
    print("\nCartes E1L1-E1L6 (tuiles distinctes ; sprites hors effecteurs picnum 1..10)")
    print("  %-5s %4s %5s %5s | %5s %5s %5s | %5s %6s %7s | %s" % (
        "carte", "sect", "murs", "spr", "wallT", "flatT", "total", "sprT", "chunks", "RLE_o", "familles presentes (n sprites)"))
    for i in range(1, 7):
        nm = f"E1L{i}.MAP"
        m = buildmap.parse(g.read(nm))
        wall_t = set(); flat_t = set(); spr_t = set(); fam_present = Counter()
        for w in m.walls:
            wall_t.add(w.picnum)
            if w.overpicnum and (w.cstat & (16 | 32) or w.nextwall >= 0):
                wall_t.add(w.overpicnum)
        for s in m.sectors:
            flat_t.add(s.floorpicnum); flat_t.add(s.ceilingpicnum)
        for s in m.sprites:
            if 1 <= s.picnum <= 10:
                continue
            spr_t.add(s.picnum)
            for f, (lo, hi) in fam_range.items():
                if lo <= s.picnum < hi:
                    fam_present[f] += 1
        ch = 0; rl = 0
        for t in spr_t:
            if t in art:
                r, c = rle_bytes(art[t]); ch += c; rl += r
        print("  %-5s %4d %5d %5d | %5d %5d %5d | %5d %6d %7d | %s" % (
            nm[:4], len(m.sectors), len(m.walls), len(m.sprites), len(wall_t), len(flat_t), len(wall_t | flat_t),
            len(spr_t), ch, rl, " ".join(f"{k}:{v}" for k, v in sorted(fam_present.items()))))
        # cout des familles presentes (toutes leurs frames)
        fch = sum(fam_stats[f][3] for f in fam_present); frl = sum(fam_stats[f][5] for f in fam_present)
        ft = sum(fam_stats[f][2] for f in fam_present)
        print("        familles completes : %d tuiles, %d chunks, %s o RLE" % (ft, fch, f"{frl:,}"))
        # murs a panning/repeat, sprites muraux/plats, pentes
        pan = sum(1 for w in m.walls if w.xpanning or w.ypanning)
        rep = sum(1 for w in m.walls if w.xrepeat != 8 or w.yrepeat != 8)
        wallspr = sum(1 for s in m.sprites if (s.cstat & 48) == 16)
        floorspr = sum(1 for s in m.sprites if (s.cstat & 48) == 32)
        slopes = sum(1 for s in m.sectors if (s.floorstat & 2) or (s.ceilingstat & 2))
        masked = sum(1 for w in m.walls if w.cstat & 16)
        oneway = sum(1 for w in m.walls if w.cstat & 32)
        print("        murs panning %d, repeat!=8 %d, masques %d, sens unique %d ; sprites muraux %d, plats %d ; secteurs en pente %d" % (
            pan, rep, masked, oneway, wallspr, floorspr, slopes))


if __name__ == "__main__":
    main()
