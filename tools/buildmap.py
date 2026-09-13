"""buildmap.py -- lecteur de cartes Build .MAP versions 6 et 7 (little-endian).

Sources (chemins relatifs a ce depot) :
  * en-tete commun : refs/build/jfbuild/src/engine.c:7400-7416 (loadoldboard) --
      int32 mapversion ; int32 posx, posy, posz ; int16 ang ; int16 cursectnum ;
      puis (v>=5) uint16 numsectors, sectors[], uint16 numwalls, walls[], uint16 numsprites,
      sprites[]  (engine.c:7419-7420, 7441-7442, 7467-7468). Meme en-tete pour v7.
  * v6 : struct sectortypev6 / walltypev6 / spritetypev6 engine.c:6440-6476 ; ordre de
      serialisation confirme champ par champ par readv6sect 7021-7044, readv6wall 7149-7166,
      readv6sprite 7252-7274 (37 / 32 / 43 octets, sans padding) ; NBlood lit les memes
      structs "#pragma pack" d'un bloc (refs/build/NBlood/source/build/src/engine.cpp:11434-11440,
      11507-11511, 11582-11600 ; exhumed : refs/build/NBlood/source/exhumed/src/init.cpp:118-128
      Sprite_6, et init.cpp:379-381 = engineLoadBoard puis fallback engineLoadBoardV5V6).
  * v7 : refs/build/jfbuild/include/build.h:107-120 sectortype (40 o), :136-144 walltype (32 o),
      :162-173 spritetype (44 o).
  * conversion v6 -> v7 (engine.c) :
      convertv6sectv7 7092-7119 : heinum_v7 = clamp(heinum_v6 << 5, -32768, 32767) et = 0 si
        (stat & 2) == 0 ; ceilingstat/floorstat u8 -> i16 ; filler = 0 ; le reste copie.
      convertv6wallv7 7210-7229 : copie pure (xrepeat/yrepeat/xpanning/ypanning inchanges --
        le x2 des xrepeat est la regle v5->v6, convertv5wallv6 6857-6876, pas v6->v7).
      convertv6sprv7 7327-7352 : copie pure, filler = 0.
      apres chargement : (cstat & 48) == 48 -> cstat &= ~48 (engine.c:7513).
Les champs exposes sont ceux de v7 ; un .MAP v6 est converti avec les regles ci-dessus
(raw_*heinum garde la valeur v6 non decalee).
"""
from __future__ import annotations
import struct
import sys
from dataclasses import dataclass, field


@dataclass
class Sector:
    wallptr: int; wallnum: int; ceilingz: int; floorz: int
    ceilingstat: int; floorstat: int
    ceilingpicnum: int; ceilingheinum: int; ceilingshade: int; ceilingpal: int
    ceilingxpanning: int; ceilingypanning: int
    floorpicnum: int; floorheinum: int; floorshade: int; floorpal: int
    floorxpanning: int; floorypanning: int
    visibility: int; lotag: int; hitag: int; extra: int
    raw_ceilingheinum: int = 0   # valeur v6 avant <<5 (v7: = ceilingheinum)
    raw_floorheinum: int = 0


@dataclass
class Wall:
    x: int; y: int; point2: int; nextwall: int; nextsector: int; cstat: int
    picnum: int; overpicnum: int; shade: int; pal: int
    xrepeat: int; yrepeat: int; xpanning: int; ypanning: int
    lotag: int; hitag: int; extra: int


@dataclass
class Sprite:
    x: int; y: int; z: int; cstat: int; picnum: int; shade: int; pal: int; clipdist: int
    xrepeat: int; yrepeat: int; xoffset: int; yoffset: int
    sectnum: int; statnum: int; ang: int; owner: int; xvel: int; yvel: int; zvel: int
    lotag: int; hitag: int; extra: int


@dataclass
class BuildMap:
    version: int
    posx: int; posy: int; posz: int; ang: int; cursectnum: int
    sectors: list[Sector] = field(default_factory=list)
    walls: list[Wall] = field(default_factory=list)
    sprites: list[Sprite] = field(default_factory=list)
    trailing: int = 0      # octets non consommes apres les sprites


# ---- layouts serialises -------------------------------------------------------------------
# v6 (engine.c:6440-6452 / readv6sect 7021-7044) : 37 octets
#   u16 wallptr,wallnum ; i16 ceilingpicnum,floorpicnum ; i16 ceilingheinum,floorheinum ;
#   i32 ceilingz,floorz ; i8 ceilingshade,floorshade ; u8 ceilingxpanning,floorxpanning ;
#   u8 ceilingypanning,floorypanning ; u8 ceilingstat,floorstat ; u8 ceilingpal,floorpal ;
#   u8 visibility ; i16 lotag,hitag,extra
V6_SECT = struct.Struct("<HHhhhhiibbBBBBBBBBBhhh")
# v6 (engine.c:6454-6463 / readv6wall 7149-7166) : 32 octets
#   i32 x,y ; i16 point2,nextsector,nextwall ; i16 picnum,overpicnum ; i8 shade ; u8 pal ;
#   i16 cstat ; u8 xrepeat,yrepeat,xpanning,ypanning ; i16 lotag,hitag,extra
V6_WALL = struct.Struct("<iihhhhhbBhBBBBhhh")
# v6 (engine.c:6465-6476 / readv6sprite 7252-7274) : 43 octets
#   i32 x,y,z ; i16 cstat ; i8 shade ; u8 pal,clipdist ; u8 xrepeat,yrepeat ; i8 xoffset,yoffset ;
#   i16 picnum,ang,xvel,yvel,zvel,owner ; i16 sectnum,statnum ; i16 lotag,hitag,extra
V6_SPR = struct.Struct("<iiihbBBBBbbhhhhhhhhhhh")
# v7 (build.h:107-120) : 40 octets
#   i16 wallptr,wallnum ; i32 ceilingz,floorz ; i16 ceilingstat,floorstat ;
#   i16 ceilingpicnum,ceilingheinum ; i8 ceilingshade ; u8 ceilingpal,ceilingxpanning,ceilingypanning ;
#   i16 floorpicnum,floorheinum ; i8 floorshade ; u8 floorpal,floorxpanning,floorypanning ;
#   u8 visibility,filler ; i16 lotag,hitag,extra
V7_SECT = struct.Struct("<hhiihhhhbBBBhhbBBBBBhhh")
# v7 (build.h:136-144) : 32 octets
#   i32 x,y ; i16 point2,nextwall,nextsector,cstat ; i16 picnum,overpicnum ; i8 shade ;
#   u8 pal,xrepeat,yrepeat,xpanning,ypanning ; i16 lotag,hitag,extra
V7_WALL = struct.Struct("<iihhhhhhbBBBBBhhh")
# v7 (build.h:162-173) : 44 octets
#   i32 x,y,z ; i16 cstat,picnum ; i8 shade ; u8 pal,clipdist,filler ; u8 xrepeat,yrepeat ;
#   i8 xoffset,yoffset ; i16 sectnum,statnum ; i16 ang,owner,xvel,yvel,zvel ; i16 lotag,hitag,extra
V7_SPR = struct.Struct("<iiihhbBBBBBbbhhhhhhhhhh")
assert (V6_SECT.size, V6_WALL.size, V6_SPR.size) == (37, 32, 43)
assert (V7_SECT.size, V7_WALL.size, V7_SPR.size) == (40, 32, 44)


def _clamp16(v: int) -> int:
    return max(min(v, 32767), -32768)


def _sect_v6(t) -> Sector:
    (wallptr, wallnum, cpic, fpic, chei, fhei, cz, fz, csh, fsh, cxp, fxp, cyp, fyp,
     cst, fst, cpal, fpal, vis, lo, hi, ex) = t
    # convertv6sectv7 engine.c:7092-7119
    chei7 = _clamp16(chei << 5) if (cst & 2) else 0
    fhei7 = _clamp16(fhei << 5) if (fst & 2) else 0
    return Sector(wallptr, wallnum, cz, fz, cst, fst, cpic, chei7, csh, cpal, cxp, cyp,
                  fpic, fhei7, fsh, fpal, fxp, fyp, vis, lo, hi, ex, chei, fhei)


def _sect_v7(t) -> Sector:
    (wallptr, wallnum, cz, fz, cst, fst, cpic, chei, csh, cpal, cxp, cyp,
     fpic, fhei, fsh, fpal, fxp, fyp, vis, _filler, lo, hi, ex) = t
    return Sector(wallptr, wallnum, cz, fz, cst, fst, cpic, chei, csh, cpal, cxp, cyp,
                  fpic, fhei, fsh, fpal, fxp, fyp, vis, lo, hi, ex, chei, fhei)


def _wall_v6(t) -> Wall:
    (x, y, p2, ns, nw, pic, opic, sh, pal, cst, xr, yr, xp, yp, lo, hi, ex) = t
    # convertv6wallv7 engine.c:7210-7229 : copie pure
    return Wall(x, y, p2, nw, ns, cst, pic, opic, sh, pal, xr, yr, xp, yp, lo, hi, ex)


def _wall_v7(t) -> Wall:
    return Wall(*t)


def _spr_v6(t) -> Sprite:
    (x, y, z, cst, sh, pal, cd, xr, yr, xo, yo, pic, ang, xv, yv, zv, own, sec, st, lo, hi, ex) = t
    # convertv6sprv7 engine.c:7327-7352 : copie pure
    return Sprite(x, y, z, cst, pic, sh, pal, cd, xr, yr, xo, yo, sec, st, ang, own, xv, yv, zv, lo, hi, ex)


def _spr_v7(t) -> Sprite:
    (x, y, z, cst, pic, sh, pal, cd, _filler, xr, yr, xo, yo, sec, st, ang, own, xv, yv, zv, lo, hi, ex) = t
    return Sprite(x, y, z, cst, pic, sh, pal, cd, xr, yr, xo, yo, sec, st, ang, own, xv, yv, zv, lo, hi, ex)


LAYOUTS = {
    6: ((V6_SECT, _sect_v6), (V6_WALL, _wall_v6), (V6_SPR, _spr_v6)),
    7: ((V7_SECT, _sect_v7), (V7_WALL, _wall_v7), (V7_SPR, _spr_v7)),
}


def parse(data: bytes, apply_cstat48_fix: bool = True) -> BuildMap:
    if len(data) < 20:
        raise ValueError("too short for a Build map header")
    version, posx, posy, posz, ang, cursect = struct.unpack_from("<iiiihh", data, 0)
    if version not in LAYOUTS:
        raise ValueError(f"unsupported Build map version {version} (only 6 and 7)")
    (sst, sfn), (wst, wfn), (pst, pfn) = LAYOUTS[version]
    off = 20
    m = BuildMap(version, posx, posy, posz, ang, cursect)

    def block(st, fn, out):
        nonlocal off
        (n,) = struct.unpack_from("<H", data, off)
        off += 2
        need = n * st.size
        if off + need > len(data):
            raise ValueError(f"truncated: need {need} bytes at {off}, have {len(data) - off}")
        for t in st.iter_unpack(data[off:off + need]):
            out.append(fn(t))
        off += need

    block(sst, sfn, m.sectors)
    block(wst, wfn, m.walls)
    block(pst, pfn, m.sprites)
    m.trailing = len(data) - off
    if apply_cstat48_fix:               # engine.c:7513 (loadoldboard)
        for s in m.sprites:
            if (s.cstat & 48) == 48:
                s.cstat &= ~48
    return m


def load(path: str) -> BuildMap:
    with open(path, "rb") as f:
        return parse(f.read())


def main(argv):
    for p in argv[1:]:
        m = load(p)
        print(f"{p}: v{m.version} sectors={len(m.sectors)} walls={len(m.walls)} "
              f"sprites={len(m.sprites)} start=({m.posx},{m.posy},{m.posz}) ang={m.ang} "
              f"sect={m.cursectnum} trailing={m.trailing}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
