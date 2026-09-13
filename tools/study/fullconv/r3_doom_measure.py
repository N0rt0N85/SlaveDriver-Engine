#!/usr/bin/env python3
"""R3 -- mesures Doom pour le rapport pipeline (lecture seule).
Sorties : DS*, sprites (lumps/chunks/RLE moteur), sprites par niveau via core/info.c, textures, capacites Doom II."""
import os, re, struct, sys
from collections import Counter, defaultdict

MIMAS = r"C:\Users\pcico\Projects\Mimas"
WADS = {
    "DOOM1.WAD (cd/data, strippe)": os.path.join(MIMAS, "cd", "data", "DOOM1.WAD"),
    "Doom2.wad (temoin)": os.path.join(MIMAS, "wads_temoins", "Doom2.wad"),
    "Doom-ud.wad (Ultimate, temoin)": os.path.join(MIMAS, "wads_temoins", "Doom-ud.wad"),
}
INFO_C = os.path.join(MIMAS, "core", "info.c")
CELL = 64


def read_wad(path):
    b = open(path, "rb").read()
    magic, n, off = struct.unpack("<4sii", b[:12])
    d = []
    for i in range(n):
        fo, sz, nm = struct.unpack("<ii8s", b[off + 16 * i:off + 16 * i + 16])
        d.append((nm.rstrip(b"\0").decode("latin-1").upper(), fo, sz))
    return b, d


def between(d, a, b_):
    ia = next((i for i, x in enumerate(d) if x[0] == a), None)
    ib = next((i for i, x in enumerate(d) if x[0] == b_), None)
    if ia is None or ib is None:
        return []
    return d[ia + 1:ib]


def patch_decode(b, fo, sz):
    d = b[fo:fo + sz]
    if len(d) < 8:
        return None
    w, h, lo, to = struct.unpack("<hhhh", d[:8])
    if w <= 0 or h <= 0 or w > 2048 or h > 2048 or 8 + 4 * w > len(d):
        return None
    colofs = struct.unpack("<%dI" % w, d[8:8 + 4 * w])
    pix = bytearray(w * h)
    msk = bytearray(w * h)
    for x in range(w):
        p = colofs[x]
        while p < len(d) and d[p] != 0xFF:
            top = d[p]; cnt = d[p + 1]; p += 3
            for k in range(cnt):
                y = top + k
                if 0 <= y < h and p + k < len(d):
                    pix[y * w + x] = d[p + k]; msk[y * w + x] = 1
            p += cnt + 1
    return w, h, pix, msk


def rle_engine_bytes(pix, msk, w, h, sub0=1):
    """Octets RLE au format PIC.C map() : [zeroRun][litRun][lit...] par chunk 64x64, runs <= 255.
    Transparent = masque a 0 ; l'index 0 opaque est remplace par sub0 (index 0 = transparent chez SlaveDriver)."""
    total = 0
    cx, cy = (w + CELL - 1) // CELL, (h + CELL - 1) // CELL
    for ty in range(cy):
        for tx in range(cx):
            # chunk lineaire 64*64
            vals = []
            for y in range(CELL):
                sy = ty * CELL + y
                for x in range(CELL):
                    sx = tx * CELL + x
                    if sx < w and sy < h and msk[sy * w + sx]:
                        v = pix[sy * w + sx]
                        vals.append(v if v else sub0)
                    else:
                        vals.append(0)
            i = 0; n = len(vals); out = 0
            while i < n:
                z = 0
                while i < n and vals[i] == 0 and z < 255:
                    z += 1; i += 1
                lit = 0; j = i
                while j < n and vals[j] != 0 and lit < 255:
                    lit += 1; j += 1
                out += 2 + lit
                i = j
            total += out
    return total, cx * cy


# ---------------------------------------------------------------- info.c
def parse_info():
    src = open(INFO_C, encoding="latin-1").read()
    # sprnames
    m = re.search(r"sprnames\[\]\s*=\s*\{(.*?)\};", src, re.S)
    sprnames = re.findall(r'"([A-Z0-9]{4})"', m.group(1))
    # states: {SPR_X, frame, tics, {action}, S_NEXT, m1, m2},  // S_NAME
    st_block = src[src.index("state_t\tstates[NUMSTATES]"):]
    st_block = st_block[:st_block.index("\n};")]
    states = []
    for mm in re.finditer(r"\{\s*(SPR_\w+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*\{[^}]*\}\s*,\s*(S_\w+)\s*,[^}]*\}\s*,?\s*//\s*(S_\w+)", st_block):
        states.append((mm.group(5), mm.group(1)[4:], mm.group(4)))
    sidx = {name: i for i, (name, _, _) in enumerate(states)}
    # mobjinfo
    mb = src[src.index("mobjinfo[NUMMOBJTYPES]"):]
    mobjs = []
    for mm in re.finditer(r"\{\s*//\s*(MT_\w+)\s*(.*?)\n\s*\}", mb, re.S):
        name, body = mm.group(1), mm.group(2)
        fields = [l.split(",")[0].strip() for l in body.strip().split("\n") if l.strip()]
        try:
            doomed = int(fields[0])
        except ValueError:
            doomed = -1
        st = [f for f in fields if f.startswith("S_")]
        mobjs.append((name, doomed, st))
    return sprnames, states, sidx, mobjs


def sprites_of(start_states, states, sidx):
    seen = set(); out = set(); stack = [s for s in start_states if s in sidx]
    while stack:
        s = stack.pop()
        if s in seen or s == "S_NULL":
            continue
        seen.add(s)
        name, spr, nxt = states[sidx[s]]
        out.add(spr)
        if nxt in sidx:
            stack.append(nxt)
    return out


# projectiles / effets spawnes par le code (p_enemy.c / p_mobj.c), pas dans info.c
SPAWNS = {
    "MT_TROOP": ["MT_TROOPSHOT"], "MT_HEAD": ["MT_HEADSHOT"], "MT_BRUISER": ["MT_BRUISERSHOT"],
    "MT_KNIGHT": ["MT_BRUISERSHOT"], "MT_BABY": ["MT_ARACHPLAZ"], "MT_CYBORG": ["MT_ROCKET"],
    "MT_FATSO": ["MT_FATSHOT"], "MT_UNDEAD": ["MT_TRACER", "MT_SMOKE"], "MT_VILE": ["MT_FIRE"],
    "MT_PAIN": ["MT_SKULL"], "MT_BOSSBRAIN": ["MT_SPAWNSHOT", "MT_SPAWNFIRE"],
    "MT_BOSSSPIT": ["MT_SPAWNSHOT"], "MT_BARREL": [],
}
ALWAYS = ["MT_PLAYER", "MT_PUFF", "MT_BLOOD", "MT_TFOG", "MT_IFOG", "MT_ROCKET", "MT_PLASMA", "MT_BFG",
          "MT_EXTRABFG"]
PSPRITES = ["PUNG", "PISG", "PISF", "SHTG", "SHTF", "SHT2", "CHGG", "CHGF", "MISG", "MISF", "SAWG", "PLSG",
            "PLSF", "BFGG", "BFGF"]

ANIM_FLATS = [("NUKAGE1", "NUKAGE3"), ("FWATER1", "FWATER4"), ("SWATER1", "SWATER4"), ("LAVA1", "LAVA4"),
              ("BLOOD1", "BLOOD3"), ("RROCK05", "RROCK08"), ("SLIME01", "SLIME04"), ("SLIME05", "SLIME08"),
              ("SLIME09", "SLIME12")]
ANIM_TEX = [("BLODGR1", "BLODGR4"), ("SLADRIP1", "SLADRIP3"), ("BLODRIP1", "BLODRIP4"), ("FIREWALA", "FIREWALL"),
            ("GSTFONT1", "GSTFONT3"), ("FIRELAV3", "FIRELAVA"), ("FIREMAG1", "FIREMAG3"), ("FIREBLU1", "FIREBLU2"),
            ("ROCKRED1", "ROCKRED3"), ("BFALL1", "BFALL4"), ("SFALL1", "SFALL4"), ("WFALL1", "WFALL4"),
            ("DBRAIN1", "DBRAIN4")]


def map_lumps(d, i):
    out = {}
    for j in range(i + 1, min(i + 12, len(d))):
        nm, fo, sz = d[j]
        if nm in ("THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES", "SECTORS",
                  "REJECT", "BLOCKMAP"):
            out[nm] = (fo, sz)
        elif out:
            break
    return out


def is_map(nm):
    return (len(nm) == 4 and nm[0] == "E" and nm[2] == "M") or (len(nm) == 5 and nm.startswith("MAP"))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    sprnames, states, sidx, mobjs = parse_info()
    print(f"info.c : {len(sprnames)} sprnames, {len(states)} states, {len(mobjs)} mobjinfo")
    by_doomed = {}
    mobj_sprites = {}
    for name, doomed, st in mobjs:
        mobj_sprites[name] = sprites_of(st, states, sidx)
        if doomed >= 0:
            by_doomed[doomed] = name
    for name, extra in SPAWNS.items():
        for e in extra:
            mobj_sprites[name] |= mobj_sprites.get(e, set())
    always_spr = set()
    for a in ALWAYS:
        always_spr |= mobj_sprites.get(a, set())

    for label, path in WADS.items():
        if not os.path.exists(path):
            print(f"\n=== {label} : ABSENT ({path})"); continue
        b, d = read_wad(path)
        print(f"\n=== {label} : {len(b):,} o, {len(d)} lumps")
        # sons
        ds = [(nm, fo, sz) for nm, fo, sz in d if nm.startswith("DS") and sz > 8]
        rates = Counter(); samples = 0
        for nm, fo, sz in ds:
            fmt, rate, ns = struct.unpack("<HHI", b[fo:fo + 8])
            rates[rate] += 1; samples += ns
        dp = sum(1 for nm, _, _ in d if nm.startswith("DP"))
        print(f"  DS* : {len(ds)} lumps, {sum(x[2] for x in ds):,} o, {samples:,} echantillons 8 bits, "
              f"rates {dict(rates)} ; DP* : {dp} ; GENMIDI {'oui' if any(x[0]=='GENMIDI' for x in d) else 'non'}")
        dmus = [(nm, sz) for nm, fo, sz in d if nm.startswith("D_")]
        print(f"  D_* (MUS) : {len(dmus)} lumps, {sum(s for _, s in dmus):,} o")
        # sprites
        spr = between(d, "S_START", "S_END")
        info = {}
        tot_rle = tot_chunks = tot_raw = 0
        chunk_hist = Counter()
        for nm, fo, sz in spr:
            if sz < 8:
                continue
            r = patch_decode(b, fo, sz)
            if r is None:
                continue
            w, h, pix, msk = r
            rle, ch = rle_engine_bytes(pix, msk, w, h)
            info[nm] = (w, h, ch, rle, sz)
            tot_rle += rle; tot_chunks += ch; tot_raw += sz; chunk_hist[ch] += 1
        print(f"  sprites S_START..S_END : {len(info)} lumps, {tot_raw:,} o bruts, {tot_chunks} chunks 64x64 "
              f"({tot_chunks/max(1,len(info)):.2f}/lump), RLE moteur {tot_rle:,} o ; chunks/lump {dict(sorted(chunk_hist.items()))}")
        prefixes = Counter(nm[:4] for nm in info)
        print(f"  prefixes de sprites distincts : {len(prefixes)}")
        ps = [nm for nm in info if nm[:4] in PSPRITES]
        print(f"  psprites (armes 1re personne) presents : {sorted(set(nm[:4] for nm in ps))} = {len(ps)} lumps, "
              f"{sum(info[n][4] for n in ps):,} o bruts, {sum(info[n][3] for n in ps):,} o RLE, {sum(info[n][2] for n in ps)} chunks")
        # textures / flats / patches
        tex = 0
        for tl in ("TEXTURE1", "TEXTURE2"):
            e = next(((fo, sz) for nm, fo, sz in d if nm == tl), None)
            if e:
                tex += struct.unpack("<i", b[e[0]:e[0] + 4])[0]
        pn = next(((fo, sz) for nm, fo, sz in d if nm == "PNAMES"), None)
        npn = struct.unpack("<i", b[pn[0]:pn[0] + 4])[0] if pn else 0
        pat = between(d, "P_START", "P_END")
        pat = [x for x in pat if x[2] > 8]
        fl = [x for x in between(d, "F_START", "F_END") if x[2] == 4096]
        print(f"  textures {tex}, PNAMES {npn}, patches {len(pat)} ({sum(x[2] for x in pat):,} o), flats {len(fl)} ({len(fl)*4096:,} o)")
        # cartes
        maps = [(i, nm) for i, (nm, _, _) in enumerate(d) if is_map(nm)]
        print(f"  cartes : {len(maps)}")
        print("  %-6s %5s %5s %5s %5s %6s | %6s %5s %5s %6s | %4s %4s %3s %3s %4s | %s" % (
            "carte", "sect", "ssec", "segs", "thing", "lines", "sprLmp", "chunk", "RLEko", "rawKo", "tex", "flat", "anF", "anT", "sw", "monstres"))
        big = []
        for i, mn in maps:
            L = map_lumps(d, i)
            nsec = L["SECTORS"][1] // 26; nss = L["SSECTORS"][1] // 4; nsg = L["SEGS"][1] // 12
            nth = L["THINGS"][1] // 10; nld = L["LINEDEFS"][1] // 14
            fo, sz = L["THINGS"]
            types = Counter(struct.unpack("<h", b[fo + 10 * k + 6:fo + 10 * k + 8])[0] for k in range(nth))
            need = set(always_spr)
            monsters = Counter()
            for t, c in types.items():
                mt = by_doomed.get(t)
                if mt is None:
                    continue
                need |= mobj_sprites.get(mt, set())
                if mt in ("MT_POSSESSED", "MT_SHOTGUY", "MT_TROOP", "MT_SERGEANT", "MT_SHADOWS", "MT_HEAD",
                          "MT_BRUISER", "MT_SKULL", "MT_SPIDER", "MT_CYBORG", "MT_KNIGHT", "MT_BABY", "MT_PAIN",
                          "MT_UNDEAD", "MT_FATSO", "MT_VILE", "MT_CHAINGUY", "MT_WOLFSS"):
                    monsters[mt[3:]] += c
            lumps = [nm for nm in info if nm[:4] in need and nm[:4] not in PSPRITES]
            ch = sum(info[n][2] for n in lumps); rle = sum(info[n][3] for n in lumps); raw = sum(info[n][4] for n in lumps)
            # textures/flats distincts + animes + switches
            fo, sz = L["SIDEDEFS"]; tx = set()
            for k in range(sz // 30):
                r = b[fo + 30 * k:fo + 30 * k + 30]
                for s in (r[4:12], r[12:20], r[20:28]):
                    s = s.rstrip(b"\0").decode("latin-1").upper()
                    if s and s != "-":
                        tx.add(s)
            fo, sz = L["SECTORS"]; flt = set()
            for k in range(nsec):
                r = b[fo + 26 * k:fo + 26 * k + 26]
                for s in (r[4:12], r[12:20]):
                    flt.add(s.rstrip(b"\0").decode("latin-1").upper())
            def in_anim(name, table):
                for a, z in table:
                    pre = a.rstrip("0123456789")
                    if name.startswith(pre[:5]) and name[:len(pre)] == pre:
                        return True
                return False
            anf = sum(1 for f in flt if in_anim(f, ANIM_FLATS))
            ant = sum(1 for t in tx if in_anim(t, ANIM_TEX))
            sw = sum(1 for t in tx if t.startswith("SW1") or t.startswith("SW2"))
            print("  %-6s %5d %5d %5d %5d %6d | %6d %5d %5d %6d | %4d %4d %3d %3d %4d | %s" % (
                mn, nsec, nss, nsg, nth, nld, len(lumps), ch, rle // 1024, raw // 1024, len(tx), len(flt), anf, ant, sw,
                " ".join(f"{k}:{v}" for k, v in sorted(monsters.items()))))
            if nss > 600:
                big.append((mn, nss))
        print(f"  cartes > 600 feuilles BSP : {big if big else 'aucune'}")
        print(f"  sprites 'toujours' (joueur, puff, sang, fogs, projectiles arme) : {sorted(always_spr)} -> "
              f"{sum(1 for n in info if n[:4] in always_spr)} lumps, {sum(info[n][3] for n in info if n[:4] in always_spr):,} o RLE")


if __name__ == "__main__":
    main()
