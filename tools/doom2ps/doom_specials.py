#!/usr/bin/env python3
"""doom_specials.py -- spéciaux Doom (lignes et secteurs) -> géométrie mobile et objets .LEV.

Couvre SPEC_CONVERTER §5 et le contrat DOOM_ABI §6 :
  - `specials_of(M)`      : inventaire des lignes/secteurs spéciaux d'une carte (portes manuelles,
                            ascenseurs, sols, déclencheurs W, interrupteurs S, sorties, dégâts) ;
  - `mobile_bounds(...)`  : course d'un secteur mobile, par la RÈGLE DE DOOM (p_doors.c, p_plats.c,
                            p_floor.c) : porte = plus bas plafond voisin - 4 ; ascenseur = plus bas sol
                            (voisins + le sien) ; sol turbo (36) = plus HAUT sol voisin + 8 ;
  - `special_objects(...)`: objets OT_NORMALDOOR 48, OT_NORMALELEVATOR 49, OT_STUCKDOWNELEVATOR 61,
                            OT_SECTORSWITCH 91 (2 shorts), OT_SW1..4 168-171, OT_DOOM_EXIT 176/177,
                            OT_DOOM_DAMAGE 179, avec leurs params gros-boutistes (OBJECT.C `suckShort`).

Ce que le moteur lit (relu à la ligne) :
  - porte      : `constructDoor(type, pb)` puis `channel = suckShort(); doorHeight = suckShort()`
                 (OBJECT.C:269-272, AI.C:4384-4398) -> 3 shorts `pb, channel, doorHeight` ;
  - ascenseur  : `constructElevator(pb, type, lower, upper)` puis `channel = suckShort()`
                 (OBJECT.C:255-262, AI.C:4686-4699) -> 4 shorts `pb, lower, upper, channel` ;
  - sector-switch : `sectorNm = suckShort(); channel = suckShort();` et `level_sector[sectorNm].object = this`
                 (AI2.C:665-676) -> 2 shorts, UNE feuille = UN objet au plus ;
  - interrupteur : `sectorNm, channel, ox, oy, oz` (AI2.C:582-596) ; la tuile OFF de la séquence
                 `level_sequenceMap[type]` doit exister UNE fois dans les murs de `sectorNm` (:606-632) ;
                 le press réussit à < 40 u de l'orifice, mesuré sur le POINT D'IMPACT du rayon
                 (SRUINS.C:843-853 passe `&collidePos`) -> orifice à hauteur d'oeil, pas au centre du mur.

Les objets Doom non-moteur (176-179) sont lus par `game_placeObject` (SPEC_RUNTIME §2, §6).
Ne modifie ni assemble.py ni le moteur : tout est importable par make_e1m1.py.
"""
from __future__ import annotations

import os
import struct
import sys
from collections import namedtuple, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import adjacency                                       # noqa: E402

# -- types d'objets (SLEVEL.H:22-81 ; 176-179 = DOOM_ABI §1, gravés dans game/doom/DOOM.H) -------
OT_PLAYER = 13
OT_NORMALDOOR = 48
OT_NORMALELEVATOR = 49
OT_STUCKDOWNELEVATOR = 61
OT_SECTORSWITCH = 91
OT_SW1 = 168
OT_SW4 = 171
OT_DOOM_EXIT = 176
OT_DOOM_SECRETEXIT = 177
OT_DOOM_LIGHT = 178
OT_DOOM_DAMAGE = 179
OT_DOOM_SECRETWALL = 180      # param : mur DOORWALL d'une ligne ML_SECRET (les monstres ne la pressent pas)
OT_NMTYPES = 227
ML_SECRET = 0x20

CHANNEL_EXIT = 900            # DOOM_ABI §6 : tags Doom 1..999, sortie 900, secrète 901
CHANNEL_SECRETEXIT = 901
CHANNEL_NONE = -1

DOOR_SLIT = 1                 # SPEC_CONVERTER E8 : plafond fermé = sol + 1 (un quad sans plan est rejeté)
PLAYER_EYE = 41               # params/doom.cfg : PLAYER_RADIUS 16 + PLAYER_EYE_HOVER 25 (DOOM_ABI §7)
SWITCH_PRESS_DIST = 40        # AI2.C:535-538

# -- spéciaux de ligne (p_spec.c / p_switch.c / p_doors.c / p_plats.c / p_floor.c, Mimas/core) ----
# portes manuelles : le secteur de la porte est celui du sidedef GAUCHE (p_doors.c EV_VerticalDoor :
# `sec = sides[line->sidenum[1]].sector`). 26-28/32-34 = à clé (aucune dans E1M1 : signalée),
# 31-34/118 = « ouvre et reste » (le moteur referme après 128 trames : écart accepté).
DOOR_MANUAL = {1, 26, 27, 28, 31, 32, 33, 34, 117, 118}
DOOR_KEYED = {26, 27, 28, 32, 33, 34}
# portes à tag (levées par déclencheur W ou interrupteur S) : OT_NORMALDOOR avec channel = tag
DOOR_TAGGED_W = {2, 4, 86, 90}
DOOR_TAGGED_S = {29, 61, 63, 103}
# ascenseurs (EV_DoPlat downWaitUpStay / blazeDWUS) : course = plus bas sol voisin (p_plats.c)
LIFT_W = {10, 88, 120, 121}
LIFT_S = {21, 62, 122, 123}
# sols qui DESCENDENT (EV_DoFloor) : un aller -> OT_STUCKDOWNELEVATOR
FLOOR_W = {36: "floor_turbo", 98: "floor_turbo", 38: "floor_lowest", 82: "floor_lowest",
           37: "floor_lowest", 84: "floor_lowest", 83: "floor_highest"}
FLOOR_S = {70: "floor_turbo", 71: "floor_turbo", 23: "floor_lowest", 60: "floor_lowest",
           45: "floor_highest", 102: "floor_highest"}
EXIT_S = {11: CHANNEL_EXIT, 51: CHANNEL_SECRETEXIT}
EXIT_W = {52: CHANNEL_EXIT, 124: CHANNEL_SECRETEXIT}
# secteurs à dégâts : P_PlayerInSpecialSector (p_spec.c:1037-1059), hp toutes les 32 tics
SECTOR_DAMAGE = {7: 5, 5: 10, 16: 20, 4: 20}

Specials = namedtuple("Specials", "doors lifts floors wswitch sswitch exits damage ignored")


def _neighbours(M):
    """{secteur: {secteurs voisins}} par les linedefs à deux faces (getNextSector)."""
    nb = defaultdict(set)
    sides = M["sidedefs"]
    for ld in M["linedefs"]:
        ss = [sides[sd].sector for sd in (ld.right, ld.left) if 0 <= sd < len(sides)]
        if len(ss) == 2 and ss[0] != ss[1]:
            nb[ss[0]].add(ss[1])
            nb[ss[1]].add(ss[0])
    return nb


def mobile_bounds(M, sector, kind):
    """(lower, upper) de la course d'un secteur mobile, règle de Doom, en unités monde.

    door          : (sol, min plafond voisin - 4)            EV_VerticalDoor / P_FindLowestCeilingSurrounding
    lift          : (min(sol, sols voisins), sol)            EV_DoPlat downWaitUpStay
    floor_turbo   : (max sol voisin + 8 si != sol, sol)      p_floor.c:298-305 turboLower (type 36/98/70/71)
    floor_lowest  : (min(sol, sols voisins), sol)            lowerFloorToLowest
    floor_highest : (min(max sol voisin, sol), sol)          lowerFloor"""
    S = M["sectors"]
    sec = S[sector]
    nbs = sorted(_neighbours(M).get(sector, ()))
    if kind == "door":
        top = min((S[n].ceilh for n in nbs), default=None)
        if top is None:
            return sec.floorh, sec.floorh
        return sec.floorh, top - 4
    if kind in ("lift", "floor_lowest"):
        return min([sec.floorh] + [S[n].floorh for n in nbs]), sec.floorh
    if kind == "floor_turbo":
        hi = max((S[n].floorh for n in nbs), default=-500)
        if hi != sec.floorh:
            hi += 8
        return min(hi, sec.floorh), sec.floorh
    if kind == "floor_highest":
        hi = max((S[n].floorh for n in nbs), default=-500)
        return min(hi, sec.floorh), sec.floorh
    raise ValueError("kind inconnu : %r" % (kind,))


def specials_of(M):
    """Inventaire des spéciaux d'une carte (namedtuple Specials, listes de dicts).

    doors   : {sector, lines, keyed, channel}       (channel -1 = manuelle, tag pour les portes à tag)
    lifts   : {sector, tag, kind 'lift'}
    floors  : {sector, tag, kind 'floor_*'}
    wswitch : {line, channel}                       lignes W -> OT_SECTORSWITCH (par feuille bordante)
    sswitch : {line, channel, special}              lignes S -> OT_SW1..4
    exits   : {line, channel, secret}
    damage  : {sector, hp}
    ignored : Counter des spéciaux non traités (scroll 48, lumières, téléporteurs...)"""
    sides, sects = M["sidedefs"], M["sectors"]
    doors, lifts, floors, wsw, ssw, exits, damage = [], [], [], [], [], [], []
    ignored = defaultdict(int)
    by_tag = defaultdict(list)
    for si, s in enumerate(sects):
        if s.tag:
            by_tag[s.tag].append(si)
        if s.special in SECTOR_DAMAGE:
            damage.append(dict(sector=si, hp=SECTOR_DAMAGE[s.special], special=s.special))
        elif s.special:
            ignored["secteur %d" % s.special] += 1
    door_by_sector = {}
    seen_mobile = set()

    def add_mobile(lst, tag, kind):
        for si in by_tag.get(tag, ()):
            if si in seen_mobile:
                continue
            seen_mobile.add(si)
            lst.append(dict(sector=si, tag=tag, kind=kind))

    for li, ld in enumerate(M["linedefs"]):
        sp = ld.special
        if not sp:
            continue
        if sp in DOOR_MANUAL:
            if not (0 <= ld.left < len(sides)):
                ignored["porte %d sans face arrière" % sp] += 1
                continue
            si = sides[ld.left].sector
            d = door_by_sector.get(si)
            if d is None:
                d = dict(sector=si, lines=[], keyed=(sp in DOOR_KEYED), channel=CHANNEL_NONE,
                         kind="door")
                door_by_sector[si] = d
                doors.append(d)
                seen_mobile.add(si)
            d["lines"].append(li)
        elif sp in DOOR_TAGGED_W or sp in DOOR_TAGGED_S:
            for si in by_tag.get(ld.tag, ()):
                if si not in door_by_sector:
                    d = dict(sector=si, lines=[], keyed=False, channel=ld.tag, kind="door")
                    door_by_sector[si] = d
                    doors.append(d)
                    seen_mobile.add(si)
                door_by_sector[si]["lines"].append(li)
            (wsw if sp in DOOR_TAGGED_W else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in LIFT_W or sp in LIFT_S:
            add_mobile(lifts, ld.tag, "lift")
            (wsw if sp in LIFT_W else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in FLOOR_W or sp in FLOOR_S:
            add_mobile(floors, ld.tag, FLOOR_W.get(sp) or FLOOR_S[sp])
            (wsw if sp in FLOOR_W else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in EXIT_S or sp in EXIT_W:
            ch = EXIT_S.get(sp) or EXIT_W[sp]
            exits.append(dict(line=li, channel=ch, secret=(ch == CHANNEL_SECRETEXIT)))
            (ssw if sp in EXIT_S else wsw).append(dict(line=li, channel=ch, special=sp))
        else:
            ignored["ligne %d" % sp] += 1
    for d in doors:
        d["lower"], d["upper"] = mobile_bounds(M, d["sector"], "door")
    for l_ in lifts + floors:
        l_["lower"], l_["upper"] = mobile_bounds(M, l_["sector"], l_["kind"])
    return Specials(doors, lifts, floors, wsw, ssw, exits, damage, dict(ignored))


def mobile_sectors(sp):
    """{secteur Doom: dict du spécial} pour tout ce qui bouge (portes, ascenseurs, sols)."""
    out = {}
    for d in sp.doors + sp.lifts + sp.floors:
        out[d["sector"]] = d
    return out


# -- feuilles ------------------------------------------------------------------------------------
def leaves_of_sector(conv, sector):
    """Feuilles gardées (index .LEV) du secteur Doom `sector`."""
    return [conv.remap[li] for li in conv.keep if conv.leaf_sector[li] == sector]


def leaves_on_line(conv, line):
    """Feuilles (index BSP) dont une arête de frontière est portée par la droite du linedef et
    recouvre son segment -- des DEUX côtés. C'est l'étiquette exacte d'adjacency.py, pas un
    échantillonnage."""
    M = conv.M
    V = M["vertices"]
    ld = M["linedefs"][line]
    ax, ay = V[ld.v1]
    bx, by = V[ld.v2]
    if (ax, ay) == (bx, by):
        return []
    key = adjacency.line_key(ax, ay, bx - ax, by - ay)
    t0, t1 = sorted((adjacency.param(key, ax, ay), adjacency.param(key, bx, by)))
    out = []
    for leaf in conv.keep:
        for (P, Q, tag, ta, tb, nb, nother) in conv.boundary[leaf]:
            if tag != key:
                continue
            lo, hi = (ta, tb) if ta < tb else (tb, ta)
            if min(hi, t1) - max(lo, t0) > 1e-3:
                out.append(leaf)
                break
    return out


# -- objets --------------------------------------------------------------------------------------
def pack_params(*vals):
    """shorts gros-boutistes, l'ordre de `suckShort` (OBJECT.C:165-170)."""
    for v in vals:
        if not (-32768 <= int(v) <= 32767):
            raise ValueError("param hors short : %r dans %r" % (v, vals))
    return struct.pack(">%dh" % len(vals), *[int(v) for v in vals])


def concat_objects(*parts):
    """Concatène des (objects, params) en rebasant `firstParam` (OBJECT.C:211 exige les offsets
    cumulés dans objectParams)."""
    objects, params = [], bytearray()
    for objs, prm in parts:
        base = len(params)
        for o in objs:
            o2 = dict(o)
            o2["firstParam"] = o["firstParam"] + base
            objects.append(o2)
        params += bytes(prm)
    return objects, params


def special_objects(M, conv, ids, specials, pb_index, *, lift_contact=False, switches=None,
                    exit_channel=None, secret_walls=None):
    """Objets des spéciaux -> (objects [{type, firstParam, ...}], params bytearray).

    `pb_index`  : {secteur Doom: numéro de push block} (sortie `mobile.pb_index` de doom3d --mobile).
    `switches`  : liste `mobile.switches` de doom3d (interrupteurs S : feuille, orifice, type OT_SW1..4).
    `lift_contact` : ascenseurs WR en `channel = -1` (contact/press, AI.C:4593-4624), sans sector-switch.
    `secret_walls` : murs DOORWALL des lignes ML_SECRET (`mobile.secret_walls` de doom3d) -> un
                    OT_DOOM_SECRETWALL (1 short) chacun (p_switch.c : un monstre ne presse pas ML_SECRET).
    Ordre d'émission : portes, ascenseurs, sols, sector-switches, interrupteurs, sorties, dégâts,
    murs secrets."""
    objects, params = [], bytearray()
    notes = defaultdict(int)
    pb_index = {int(k): int(v) for k, v in (pb_index or {}).items()}   # clés str depuis le JSON

    def emit(ot, *vals, **extra):
        o = dict(type=ot, firstParam=len(params))
        o.update(extra)
        objects.append(o)
        params.extend(pack_params(*vals))

    # portes : pb, channel, doorHeight. Le fichier ferme la porte a sol + DOOR_SLIT (close_doors) et
    # door_func monte de doorHeight (AI.C:4345-4348) : doorHeight = course Doom - fente, pour que le
    # plafond ouvert tombe sur la regle de Doom (plus bas plafond voisin - 4), pas 1 u au-dessus.
    for d in specials.doors:
        pb = pb_index.get(d["sector"])
        if pb is None:
            notes["porte sans push block"] += 1
            continue
        emit(OT_NORMALDOOR, pb, d["channel"], d["upper"] - d["lower"] - DOOR_SLIT,
             sector_doom=d["sector"], kind="door")
    # ascenseurs : pb, lower, upper, channel
    for l_ in specials.lifts:
        pb = pb_index.get(l_["sector"])
        if pb is None:
            notes["ascenseur sans push block"] += 1
            continue
        ch = CHANNEL_NONE if lift_contact else l_["tag"]
        emit(OT_NORMALELEVATOR, pb, l_["lower"], l_["upper"], ch,
             sector_doom=l_["sector"], kind="lift")
    for f in specials.floors:
        pb = pb_index.get(f["sector"])
        if pb is None:
            notes["sol sans push block"] += 1
            continue
        emit(OT_STUCKDOWNELEVATOR, pb, f["lower"], f["upper"], f["tag"],
             sector_doom=f["sector"], kind=f["kind"])
    # déclencheurs W : une feuille = un OT_SECTORSWITCH (level_sector[s].object, unique par feuille)
    lift_tags = {l_["tag"] for l_ in specials.lifts}
    taken = {}
    for w in specials.wswitch:
        if lift_contact and w["channel"] in lift_tags and w["special"] in LIFT_W:
            continue
        for leaf in leaves_on_line(conv, w["line"]):
            s = conv.remap[leaf]
            if s in taken and taken[s] != w["channel"]:
                notes["feuille avec deux déclencheurs (canal %d perdu)" % w["channel"]] += 1
                continue
            if s in taken:
                continue
            taken[s] = w["channel"]
            emit(OT_SECTORSWITCH, s, w["channel"], line=w["line"], kind="wswitch")
    # interrupteurs S : sectorNm, channel, ox, oy, oz
    for sw in (switches or []):
        emit(sw["type"], sw["leaf_sector"], sw["channel"], *sw["orifice"],
             line=sw["line"], kind="switch")
    # sorties
    for e in specials.exits:
        ch = e["channel"] if exit_channel is None else exit_channel
        emit(OT_DOOM_SECRETEXIT if e["secret"] else OT_DOOM_EXIT, ch, line=e["line"], kind="exit")
    # secteurs à dégâts : une par feuille
    for dm in specials.damage:
        for s in leaves_of_sector(conv, dm["sector"]):
            emit(OT_DOOM_DAMAGE, s, dm["hp"], sector_doom=dm["sector"], kind="damage")
    # murs secrets : un par mur DOORWALL d'une ligne ML_SECRET
    for w in sorted(int(x) for x in (secret_walls or ())):
        emit(OT_DOOM_SECRETWALL, w, kind="secretwall")
    return objects, params, dict(notes)


def expected_param_bytes(objects):
    """Σ des params attendus par type (DOOM_ABI « Vérifications PC ») : 5 joueur, 6 mobj, 3 porte,
    4 ascenseur, 2 sector-switch, 5 interrupteur, 1 sortie, 2 dégâts -- en octets."""
    n = 0
    for o in objects:
        t = o["type"]
        if t == OT_PLAYER:
            n += 5
        elif t == OT_NORMALDOOR:
            n += 3
        elif t in (OT_NORMALELEVATOR, OT_STUCKDOWNELEVATOR):
            n += 4
        elif t == OT_SECTORSWITCH:
            n += 2
        elif OT_SW1 <= t <= OT_SW4:
            n += 5
        elif t in (OT_DOOM_EXIT, OT_DOOM_SECRETEXIT, OT_DOOM_SECRETWALL):
            n += 1
        elif t == OT_DOOM_DAMAGE:
            n += 2
        else:
            n += 6                      # mobj Doom : sector, x, y, z, angle, flags
    return 2 * n


def switch_sequences(switches):
    """Descripteurs de séquences pour l'assembleur : 4 séquences par type OT_SW (AI2.C:522-577) :
    base = OFF (1 frame), +1 = anim ON (1 frame, tuile ON), +2 = anim OFF (1 frame, tuile OFF),
    +3 = ON (1 frame). -> [{type, sequences: [[frame, ...], ...]}], frame = [chunk], chunk =
    {chunkx, chunky, tile, flags, pad} avec tile = index de TUILE DE GÉOMÉTRIE (0x32) ; l'assembleur
    pose `sequenceMap[type] = base`."""
    out = []
    seen = set()
    for sw in switches:
        if sw["type"] in seen:
            continue
        seen.add(sw["type"])
        off, on = sw["tile_off"], sw["tile_on"]

        def fr(t):
            return [dict(chunkx=0, chunky=0, tile=t, flags=0, pad=0)]
        out.append(dict(type=sw["type"], sequences=[[fr(off)], [fr(on)], [fr(off)], [fr(on)]]))
    return out
