#!/usr/bin/env python3
"""wad.py -- lecteur WAD (Doom) pour le convertisseur doom2ps.

Ne lit que ce dont le convertisseur a besoin : l'annuaire, les lumps d'une carte, la palette,
les flats et les textures composites (PNAMES + TEXTURE1/2 + patches).

Toutes les structures sont celles du format WAD original (little-endian) :
  LINEDEF 14 o : v1 v2 flags special tag sidenum[2]
  SIDEDEF 30 o : xoff yoff upper[8] lower[8] middle[8] sector
  VERTEX   4 o : x y
  SEG     12 o : v1 v2 angle linedef side offset
  SSECTOR  4 o : numsegs firstseg
  NODE    28 o : x y dx dy bbox[2][4] children[2]
  SECTOR  26 o : floorh ceilh floorpic[8] ceilpic[8] light special tag
  THING   10 o : x y angle type flags
"""
from __future__ import annotations

import struct
from collections import namedtuple

Linedef = namedtuple("Linedef", "v1 v2 flags special tag right left")
Sidedef = namedtuple("Sidedef", "xoff yoff upper lower middle sector")
Seg = namedtuple("Seg", "v1 v2 angle line side offset")
Subsector = namedtuple("Subsector", "count first")
Node = namedtuple("Node", "x y dx dy bbox children")
Sector = namedtuple("Sector", "floorh ceilh floorpic ceilpic light special tag")
Thing = namedtuple("Thing", "x y angle type flags")

MAP_LUMPS = ("THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
             "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP")


def _s(b):
    return b.rstrip(b"\0").decode("latin-1").upper()


class Wad:
    def __init__(self, path):
        self.path = path
        self.b = open(path, "rb").read()
        magic, n, off = struct.unpack("<4sii", self.b[:12])
        if magic not in (b"IWAD", b"PWAD"):
            raise SystemExit("%s n'est pas un WAD" % path)
        self.dir = []
        for i in range(n):
            fo, sz, nm = struct.unpack("<ii8s", self.b[off + 16 * i:off + 16 * i + 16])
            self.dir.append((_s(nm), fo, sz))
        self.index = {}
        for i, (nm, _, _) in enumerate(self.dir):
            self.index.setdefault(nm, i)

    # -- acces bruts -------------------------------------------------------------------
    def lump(self, name):
        i = self.index[name]
        _, fo, sz = self.dir[i]
        return self.b[fo:fo + sz]

    def lump_at(self, i):
        _, fo, sz = self.dir[i]
        return self.b[fo:fo + sz]

    def has(self, name):
        return name in self.index

    def between(self, start, end):
        """Les lumps strictement entre deux marqueurs (F_START/F_END, P_START/P_END...)."""
        a, b = self.index[start], self.index[end]
        return [(nm, self.lump_at(k)) for k, (nm, _, _) in enumerate(self.dir)
                if a < k < b]

    # -- palette -----------------------------------------------------------------------
    def playpal(self, which=0):
        d = self.lump("PLAYPAL")
        base = 768 * which
        return [tuple(d[base + 3 * i:base + 3 * i + 3]) for i in range(256)]

    # -- flats -------------------------------------------------------------------------
    def flats(self):
        """{nom: 4096 octets d'indices} -- un flat Doom fait 64x64 en 8 bits, exactement la
        taille d'une tuile de geometrie SlaveDriver."""
        out = {}
        for a, b in (("F_START", "F_END"), ("FF_START", "FF_END")):
            if self.has(a) and self.has(b):
                for nm, data in self.between(a, b):
                    if len(data) == 4096:
                        out[nm] = data
        return out

    # -- textures composites -----------------------------------------------------------
    def pnames(self):
        d = self.lump("PNAMES")
        n = struct.unpack("<i", d[:4])[0]
        return [_s(d[4 + 8 * i:12 + 8 * i]) for i in range(n)]

    def textures(self):
        """{nom: dict(width, height, patches=[(x, y, pname_index)])}"""
        out = {}
        for lname in ("TEXTURE1", "TEXTURE2"):
            if not self.has(lname):
                continue
            d = self.lump(lname)
            n = struct.unpack("<i", d[:4])[0]
            offs = struct.unpack("<%di" % n, d[4:4 + 4 * n])
            for o in offs:
                nm = _s(d[o:o + 8])
                w, h = struct.unpack("<hh", d[o + 12:o + 16])
                np_ = struct.unpack("<h", d[o + 20:o + 22])[0]
                pats = []
                for k in range(np_):
                    px, py, pi = struct.unpack("<hhh", d[o + 22 + 10 * k:o + 28 + 10 * k])
                    pats.append((px, py, pi))
                out[nm] = dict(width=w, height=h, patches=pats)
        return out

    def patch(self, name):
        """Decode un patch Doom (colonnes de posts) en (w, h, bytearray w*h, bytearray masque)."""
        d = self.lump(name)
        w, h, _lo, _to = struct.unpack("<hhhh", d[:8])
        colofs = struct.unpack("<%dI" % w, d[8:8 + 4 * w])
        pix = bytearray(w * h)
        msk = bytearray(w * h)
        for x in range(w):
            p = colofs[x]
            while p < len(d) and d[p] != 0xFF:
                top = d[p]
                cnt = d[p + 1]
                p += 3                                  # + 1 octet de garde
                for k in range(cnt):
                    y = top + k
                    if 0 <= y < h and p + k < len(d):
                        pix[y * w + x] = d[p + k]
                        msk[y * w + x] = 1
                p += cnt + 1                            # + 1 octet de garde
        return w, h, pix, msk

    # -- carte -------------------------------------------------------------------------
    def map_lumps(self, mapname):
        i = self.index[mapname]
        out = {}
        for j in range(i + 1, min(i + 12, len(self.dir))):
            nm, fo, sz = self.dir[j]
            if nm in MAP_LUMPS:
                out[nm] = self.b[fo:fo + sz]
            elif out:
                break
        return out


def read_map(wad, mapname):
    L = wad.map_lumps(mapname)

    def recs(key, fmt, size):
        d = L[key]
        return [struct.unpack_from(fmt, d, k * size) for k in range(len(d) // size)]

    verts = [(x, y) for x, y in recs("VERTEXES", "<hh", 4)]
    lines = [Linedef(*r) for r in recs("LINEDEFS", "<7h", 14)]
    sides = [Sidedef(r[0], r[1], _s(r[2]), _s(r[3]), _s(r[4]), r[5])
             for r in recs("SIDEDEFS", "<hh8s8s8sh", 30)]
    segs = [Seg(*r) for r in recs("SEGS", "<6h", 12)]
    subs = [Subsector(*r) for r in recs("SSECTORS", "<2h", 4)]
    nodes = []
    for r in recs("NODES", "<4h8h2H", 28):
        nodes.append(Node(r[0], r[1], r[2], r[3],
                          (r[4:8], r[8:12]), (r[12], r[13])))
    sects = [Sector(r[0], r[1], _s(r[2]), _s(r[3]), r[4], r[5], r[6])
             for r in recs("SECTORS", "<hh8s8shhh", 26)]
    things = [Thing(*r) for r in recs("THINGS", "<5h", 10)]
    return dict(vertices=verts, linedefs=lines, sidedefs=sides, segs=segs,
                subsectors=subs, nodes=nodes, sectors=sects, things=things)
