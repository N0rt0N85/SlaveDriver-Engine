#!/usr/bin/env python3
"""R3 -- STATIC.DAT (ordre SRUINS.C:1925-1931) et queue de KILENTRY/TOMB (sons, tuiles, sequences, palettes)."""
import os, struct, sys, math
from collections import Counter

SD = r"C:\Users\pcico\Projects\SlaveDriver-Engine"
sys.path.insert(0, os.path.join(SD, "tools"))
sys.path.insert(0, os.path.join(SD, "tools", "duke2ps"))
import lev_io


def hz(rate):
    """inverse de CONVERT.C writeSound : rate = ((octave<<11)&0x7800)|fns ; octave signe 4 bits."""
    octv = (rate >> 11) & 0xF
    if octv >= 8:
        octv -= 16
    fns = rate & 0x3FF
    return int(round(44100.0 * 2 ** (octv + fns / 1024.0)))


def static_dat(path):
    b = open(path, "rb").read()
    p = 0
    print(f"STATIC.DAT : {len(b):,} o")
    # loadLoadingScreen : 512 pal + int 320 + int 240 + 320*240
    pal = b[p:p + 512]; p += 512
    xs, ys = struct.unpack(">ii", b[p:p + 8]); p += 8
    p += xs * ys
    print(f"  1. ecran de chargement : palette 512 + {xs}x{ys} 8bpp = {512 + 8 + xs*ys:,} o")
    # loadVDP2Sprites : 1024*256 bytes
    p += 1024 * 256
    print(f"  2. feuille VDP2 (NBG0 512x512 8bpp) : 262 144 o -> VRAM+0x40000, fin @ {p:,}")
    # loadStaticSounds : loadSoundSet(map 8)
    n = struct.unpack(">i", b[p:p + 4])[0]; p += 4
    smap = struct.unpack(">%dh" % n, b[p:p + 2 * n]); p += 2 * n
    ns = struct.unpack(">i", b[p:p + 4])[0]; p += 4
    snd = []
    for i in range(ns):
        size, rate, bps, loop = struct.unpack(">iiii", b[p:p + 16]); p += 16
        snd.append((size, rate, bps, loop)); p += size
    print(f"  3. sons statiques : map {n} groupes {list(smap)} ; {ns} sons, PCM {sum(s[0] for s in snd):,} o, "
          f"bps {dict(Counter(s[2] for s in snd))}, Hz {dict(Counter(hz(s[1]) for s in snd).most_common(6))}, "
          f"boucles {sum(1 for s in snd if s[3] != -1)}, fin @ {p:,}")
    # loadWeaponTiles : loadTileSet
    nt = struct.unpack(">i", b[p:p + 4])[0]; p += 4
    kinds = Counter(); tbytes = 0; t0 = p
    for i in range(nt):
        f = struct.unpack(">h", b[p:p + 2])[0]; p += 2
        kinds[hex(f)] += 1
        if f in (0x32,):
            p += 2 + 4096
        elif f == 0x34:
            p += 2 + 1024
        elif f in (0x6A, 0x6C, 0x72):
            palnm, sz = struct.unpack(">hh", b[p:p + 4]); p += 4 + sz
        elif f == 0x01:
            p += 8
        else:
            raise SystemExit(f"flags inconnus {f:#x} @ {p}")
    print(f"  4. tuiles d'armes : {nt} tuiles = {dict(kinds)}, {p - t0:,} o (tileBase des .LEV = {nt})")
    # loadWeaponSequences
    size = struct.unpack(">i", b[p:p + 4])[0]; p += 4
    nseq, nfr, nch = struct.unpack(">3i", b[p:p + 12])
    print(f"  5. sequences d'armes : size {size:,} = header 12 + {nfr} frames x8 + {nch} chunks x8 + {nseq} shorts "
          f"(pas de carte objet -> {12 + 8*nfr + 8*nch + 2*nseq} attendu), fin @ {p + size:,} / {len(b):,}")
    p += size
    print(f"  reste : {len(b) - p} o")


def lev_tail(path):
    model, lay = lev_io.read_model(path)
    nm = os.path.basename(path)
    snd = model["sounds"]
    mapped = sum(1 for x in snd["map"] if x >= 0)
    print(f"\n{nm} : {lay['fsize']:,} o")
    print(f"  sons dyn : {len(snd['sounds'])} ; carte 227 -> {mapped} types ont un son ; PCM {sum(len(s['pcm']) for s in snd['sounds']):,} o ; "
          f"bps {dict(Counter(s['bps'] for s in snd['sounds']))} ; Hz {dict(Counter(hz(s['rate']) for s in snd['sounds']).most_common(6))} ; "
          f"boucles {sum(1 for s in snd['sounds'] if s['loopStart'] != -1)}")
    pal = model["palettes"]
    print(f"  palettes : {len(pal['palettes'])} x 256 u16, palette objet = #{pal['objectPalette']} ; "
          f"entree 0 == 0 dans {sum(1 for p in pal['palettes'] if p[0] == 0)}/{len(pal['palettes'])} ; "
          f"bit15 pose sur {sum(1 for p in pal['palettes'] for c in p[1:] if c & 0x8000)}/{255*len(pal['palettes'])} entrees non nulles")
    tiles = model["tiles"]
    kinds = Counter(hex(t["flags"]) for t in tiles)
    by = Counter()
    for t in tiles:
        by[hex(t["flags"])] += 2 + (2 + len(t["pixels"]) if "pixels" in t else (4 + len(t["rle"]) if "rle" in t else 8))
    print(f"  tuiles : {len(tiles)} = {dict(kinds)} ; octets par classe {dict(by)}")
    rle64 = [len(t["rle"]) for t in tiles if t["flags"] == 0x6A]
    if rle64:
        print(f"    RLE 8bpp 64x64 : {len(rle64)} tuiles, moyenne {sum(rle64)/len(rle64):.0f} o, max {max(rle64)} (vs 4096 brut)")
    sq = model["sequences"]
    smap = sq["sequenceMap"]
    print(f"  sequences : {len(sq['sequence'])} sequences, {len(sq['frames'])} frames, {len(sq['chunks'])} chunks ; "
          f"carte 227 -> {sum(1 for x in smap if x >= 0)} types ont une sequence ; chunk flags {dict(Counter(c['flags'] for c in sq['chunks']))} ; "
          f"frames avec son {sum(1 for f in sq['frames'] if f['sound'] != -1)} ; frame flags {dict(Counter(hex(f['flags']) for f in sq['frames']).most_common(6))}")
    chpf = Counter()
    seqs = sq["sequence"] + [len(sq["frames"])]
    # chunks par frame = chunkIndex[i+1]-chunkIndex[i]
    ci = [f["chunkIndex"] for f in sq["frames"]] + [len(sq["chunks"])]
    for i in range(len(sq["frames"])):
        chpf[ci[i + 1] - ci[i]] += 1
    print(f"    chunks par frame : {dict(sorted(chpf.items()))}")
    lv = model["level"]
    obj = Counter(o["type"] for o in lv["objects"])
    print(f"  objets : {len(lv['objects'])} ({len(obj)} types), params {len(lv['objectParams'])} o ; PB {len(lv['pushBlocks'])}, cut {len(lv['cutPlane'])}")
    sky = model["sky"]
    tab = sky["table"]
    print(f"  ciel : palette 256, {sky['width']}x{sky['height']}, table K 320 : min {min(tab)} max {max(tab)} ; bitmap index 0 present : {sky['bitmap'].count(0)} px")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    static_dat(os.path.join(SD, "cd", "STATIC.DAT"))
    for f in ("KILENTRY.LEV", "TOMBEND.LEV", "SUNKEN.LEV"):
        lev_tail(os.path.join(SD, "cd", f))
    # notre TOMB.LEV converti (E1M1 ou E1L1 du jour)
    lev_tail(os.path.join(SD, "cd", "TOMB.LEV"))


if __name__ == "__main__":
    main()
