#!/usr/bin/env python3
"""things2objects.py -- lump THINGS d'une carte Doom -> objets .LEV (joueur + mobjs), SPEC_CONVERTER §4.

Règles (DOOM_ABI §1, OBJECT.C:165-206) :
  - filtre de difficulté = P_SpawnMapThing (p_mobj.c:953-958) : bit 1 pour sk_baby/sk_easy, 2 pour
    sk_medium, 4 pour sk_hard/sk_nightmare ; `flags & 16` (MTF_NOTSINGLE) = multi seulement, ignoré ;
    le départ joueur (DoomEd 1) est pris AVANT ce filtre, comme dans Doom ; 2-4/11 (starts coop / DM)
    ignorés ;
  - type .LEV = `mt_to_ot[ed_to_mt[doomed]]` de build/doom/doom_ids.json (info2tables.py), joueur 13 ;
  - joueur : 5 shorts `sector, x, y, z, angle` (suckSpriteParams OBJECT.C:183-194), angle +90°
    (constructPlayer retire F(90), AI.C:51) ;
  - mobj : 6 shorts `sector, x, y, z, angle, flags` ; `y = floorLevel` du secteur .LEV (le moteur
    relève le sprite de son rayon, SPRITE.C:60-66), `z = y_doom` (repère X = x_doom, Z = y_doom, 1:1),
    `angle = round(deg * 4096 / 360)` (short * 5760 = 360/4096°, OBJECT.C:194), `flags` = bits THINGS
    (bit 3 = ambush) ;
  - secteur = feuille du BSP contenant (x, y) remappée (`doom3d.DoomConverter.bsp.leaf_at`, `.remap`) ;
    une thing sur une feuille écartée est une erreur.

Usage : python tools\\doom2ps\\things2objects.py [--wad W] [--map E1M1] [--skill 3] [--ids J]
                                                 [--geom J] [--out J] [--lift-contact]
`--geom` (sortie de doom3d.py --mobile) apporte `floorLevel` par secteur, les push blocks et les
interrupteurs : les objets des spéciaux (doom_specials.special_objects) sont alors ajoutés à la suite.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "duke2ps"))

import wad as wadmod                                   # noqa: E402
import doom3d                                          # noqa: E402
import doom_specials as sp                             # noqa: E402

IDS_DEFAULT = os.path.join(ROOT, "build", "doom", "doom_ids.json")
MTF_AMBUSH = 8
MTF_NOTSINGLE = 16
START_TYPES = {1, 2, 3, 4, 11}          # DoomEd : joueurs 1-4, deathmatch


def skill_bit(skill):
    """p_mobj.c:953-958 : `if (gameskill == sk_baby) bit = 1; else if (gameskill == sk_nightmare)
    bit = 4; else bit = 1 << (gameskill - 1);` -- skill = index gameskill 0..4 (3 = sk_hard = UV)."""
    if skill <= 0:
        return 1
    if skill >= 4:
        return 4
    return 1 << (skill - 1)


def lev_angle(deg):
    return int(round((deg % 360) * 4096.0 / 360.0)) % 4096


def load_ids(path=IDS_DEFAULT):
    with open(path, encoding="utf-8") as f:
        ids = json.load(f)
    ids["_ed_to_mt"] = {int(k): int(v) for k, v in ids["ed_to_mt"].items()}
    return ids


def things_to_objects(M, conv, ids, *, skill=3, floor_levels=None):
    """-> (objects [{type, firstParam, ...}], params bytearray, stats). Le joueur en tête.

    `floor_levels[s]` = floorLevel du secteur .LEV `s` (sortie de doom3d) ; à défaut le sol Doom de
    la feuille, identique pour des sols plats (MESURE : écart 0 sur E1M1, vérifié par le CLI)."""
    ed_to_mt = ids.get("_ed_to_mt") or {int(k): int(v) for k, v in ids["ed_to_mt"].items()}
    mt_to_ot = ids["mt_to_ot"]
    mt_names = ids.get("mt_names") or []
    bit = skill_bit(skill)
    S = M["sectors"]
    objects, params = [], bytearray()
    stats = dict(things=len(M["things"]), skill_bit=bit, kept=0, starts=0, skill_out=0,
                 notsingle=0, ambush=0, unknown=[])

    def sector_of(t):
        lf = conv.bsp.leaf_at(t.x, t.y)
        if lf not in conv.remap:
            raise SystemExit("thing type %d en (%d, %d) tombe sur la feuille %d, écartée de la "
                             "carte" % (t.type, t.x, t.y, lf))
        s = conv.remap[lf]
        if floor_levels is not None and s < len(floor_levels):
            fl = int(floor_levels[s])
        else:
            fl = S[conv.leaf_sector[lf]].floorh
        return s, fl

    player = next((t for t in M["things"] if t.type == 1), None)
    if player is None:
        raise SystemExit("pas de départ joueur (thing type 1)")
    s, fl = sector_of(player)
    objects.append(dict(type=sp.OT_PLAYER, firstParam=0, ed=1, sector=s, x=player.x,
                        y=fl, z=player.y, angle_doom=player.angle, kind="player"))
    params += sp.pack_params(s, player.x, fl, player.y, lev_angle(player.angle + 90))

    for t in M["things"]:
        if t.type in START_TYPES:
            stats["starts"] += 1
            continue
        if not (t.flags & bit):
            stats["skill_out"] += 1
            continue
        if t.flags & MTF_NOTSINGLE:
            stats["notsingle"] += 1
            continue
        mt = ed_to_mt.get(t.type)
        if mt is None:
            stats["unknown"].append(t.type)
            continue
        ot = mt_to_ot[mt]
        if ot < 0:
            stats["unknown"].append(t.type)
            continue
        s, fl = sector_of(t)
        objects.append(dict(type=ot, firstParam=len(params), ed=t.type, mt=mt,
                            mt_name=mt_names[mt] if mt < len(mt_names) else None,
                            sector=s, x=t.x, y=fl, z=t.y, angle_doom=t.angle,
                            flags=t.flags, kind="mobj"))
        params += sp.pack_params(s, t.x, fl, t.y, lev_angle(t.angle), t.flags)
        stats["kept"] += 1
        if t.flags & MTF_AMBUSH:
            stats["ambush"] += 1
    return objects, params, stats


# -- vérification PC : pointInSectorP rejoué sur le modèle géométrique -------------------------------
def object_positions(objects, params):
    """[(index objet, type, sector, x, y, z)] pour les objets qui portent une position (joueur, mobjs)."""
    out = []
    p = bytes(params)
    for i, o in enumerate(objects):
        t = o["type"]
        if t == sp.OT_PLAYER or o.get("kind") == "mobj" or (
                t not in (sp.OT_NORMALDOOR, sp.OT_NORMALELEVATOR, sp.OT_STUCKDOWNELEVATOR,
                          sp.OT_SECTORSWITCH, sp.OT_DOOM_EXIT, sp.OT_DOOM_SECRETEXIT,
                          sp.OT_DOOM_DAMAGE, sp.OT_DOOM_SECRETWALL) and not (sp.OT_SW1 <= t <= sp.OT_SW4)):
            fp = o["firstParam"]
            s, x, y, z = struct.unpack(">4h", p[fp:fp + 8])
            out.append((i, t, s, x, y, z))
    return out


def check_objects(objects, params, sectors, walls, vertices):
    """Rejoue pointInSectorP (SPRITE.C:940-960, marge -F(1)/4 sur les murs VERTICAUX) et
    `y == floorLevel` pour chaque objet positionné. -> dict(n, hors_secteur, y_faux)."""
    hors, yfaux = [], []
    pos = object_positions(objects, params)
    for (i, t, s, x, y, z) in pos:
        sec = sectors[s]
        if y != sec["floorLevel"]:
            yfaux.append((i, t, y, sec["floorLevel"]))
        for wi in range(sec["firstWall"], sec["lastWall"] + 1):
            w = walls[wi]
            if w["normal"][1] != 0:
                continue
            v0 = vertices[w["v"][0]]
            d = ((x - v0["x"]) * w["normal"][0] + (y - v0["y"]) * w["normal"][1]
                 + (z - v0["z"]) * w["normal"][2])
            if d < -(1 << 16) // 4:
                hors.append((i, t, s, wi))
                break
    return dict(n=len(pos), hors_secteur=hors, y_faux=yfaux)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--skill", type=int, default=3, help="index gameskill (3 = UV)")
    ap.add_argument("--ids", default=IDS_DEFAULT)
    ap.add_argument("--geom", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_geom3d.json"),
                    help="sortie de doom3d.py (--mobile pour les spéciaux) ; '' = sans")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_objects.json"))
    ap.add_argument("--lift-contact", action="store_true")
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    W = wadmod.Wad(a.wad)
    M = wadmod.read_map(W, a.map)
    ids = load_ids(a.ids)
    G = None
    if a.geom and os.path.exists(a.geom):
        with open(a.geom, encoding="utf-8") as f:
            G = json.load(f)
    mobile = (G or {}).get("mobile")
    specials = sp.specials_of(M)
    if mobile:
        # même géométrie que doom3d --mobile : portes fermées (fente) avant tout calcul de secteur
        doom3d.close_doors(M, specials, mobile.get("door_slit", sp.DOOR_SLIT))
    sizes = {nm: (t["width"], t["height"]) for nm, t in W.textures().items()}
    conv = doom3d.DoomConverter(M, sizes)
    fl = [s["floorLevel"] for s in G["sectors"]] if G else None

    objects, params, st = things_to_objects(M, conv, ids, skill=a.skill, floor_levels=fl)
    print(f"things2objects : {a.map} -- {st['things']} things, bit de difficulté {st['skill_bit']} : "
          f"{len(objects)} objets (1 joueur + {st['kept']} mobjs), {st['starts']} starts ignorés, "
          f"{st['skill_out']} hors difficulté, {st['notsingle']} multi seulement, "
          f"{st['ambush']} ambush")
    if st["unknown"]:
        print(f"  DoomEd inconnus ignorés : {sorted(set(st['unknown']))}")
    notes = {}
    if mobile:
        so, sprm, notes = sp.special_objects(M, conv, ids, specials, mobile["pb_index"],
                                             lift_contact=a.lift_contact,
                                             switches=mobile.get("switches"))
        objects, params = sp.concat_objects((objects, params), (so, sprm))
        from collections import Counter
        c = Counter(o["type"] for o in so)
        print(f"  spéciaux : {len(so)} objets " + ", ".join(f"OT {t} x{n}" for t, n in sorted(c.items())))
        for k, v in notes.items():
            print(f"  NOTE : {k} x{v}")
    elif G is not None:
        print("  (géométrie sans clé `mobile` : aucun objet de spécial)")

    ok = True
    exp = sp.expected_param_bytes(objects)
    put = lambda k, b, d="": (print(f"  [{'OK' if b else 'ECHEC'}] {k}{(' : ' + d) if d else ''}"), b)[1]
    ok &= put("firstParam cumulés", all(objects[i + 1]["firstParam"] - objects[i]["firstParam"] > 0
                                        for i in range(len(objects) - 1))
              and objects[0]["firstParam"] == 0, f"{len(objects)} objets")
    ok &= put("Σ params attendus == nmObjectParams", exp == len(params), f"{exp} vs {len(params)}")
    if G is not None:
        chk = check_objects(objects, params, G["sectors"], G["walls"], G["vertices"])
        ok &= put("objets dans leur secteur (pointInSectorP)", not chk["hors_secteur"],
                  f"{chk['n']} positionnés, {len(chk['hors_secteur'])} hors secteur "
                  f"{chk['hors_secteur'][:3]}")
        ok &= put("y == floorLevel", not chk["y_faux"], str(chk["y_faux"][:3]))

    out = dict(format="doom2ps/objects v1", source=dict(wad=os.path.basename(a.wad), map=a.map,
                                                         skill=a.skill, lift_contact=a.lift_contact),
               stats=st, notes=notes, objects=objects, params=bytes(params).hex(),
               nmObjectParams=len(params))
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    os.replace(tmp, a.out)
    print(f"  -> {a.out}  ({len(objects)} objets, {len(params)} o de params)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
