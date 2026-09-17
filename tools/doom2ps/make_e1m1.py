#!/usr/bin/env python3
"""make_e1m1.py -- UNE commande : WAD Doom -> disque Doom pour SlaveDriver (SPEC_CONVERTER §6).

Produit, atomiquement (temp + os.replace), rien n'etant commite :
  <out-dir>/<name>          le niveau (.LEV, modele lev_io -> lev_write.write_file, strict)
  <out-dir>/STATIC.DAT      wad2static.write_static (5 blocs, SRUINS.C:1922-1932)
  build/doom/doom_art.h     wad2hud.write_doom_art (art HUD en tableau C)
  <out-dir>/INITLOAD.DAT, INTRO.PCS   copies des assets retail de cd/ (DOOM_ABI §9)
et verifie que chaque nom de `doomLevelNames[]` (game/doom/DOOM_GAME.C) existe dans <out-dir>.

Chaine (tout en memoire sauf les JSON intermediaires sous build/doom2ps/) :
  1. info2tables.py            si build/doom/doom_ids.json est absent (DOOM_TABLES.C, doom_ids.json)
  2. doom3d.py --mobile        geometrie fermee + push blocks + interrupteurs -> e1m1_geom3d.json
  3. doomtiles.py              tuiles de geometrie 0x32 (dont les tuiles OFF/ON d'interrupteur)
  4. wad2sprites / wad2snd / things2objects + doom_specials.special_objects
  5. assemble_doom()           modele lev_io : ciel (SKY1 256x128 -> 512x256, table K d'un retail),
                               niveau, sons (carte 227 + dynamiques), palettes (palette 0 = PLAYPAL
                               objet, entree 0 = 0x0000, objectPalette = 0), tuiles (geometrie 0x32
                               en tete puis chunks 0x6A), sequences (sprites + interrupteurs)
  6. write_static / write_doom_art / copies
  7. lev_write.engine_problems == [], tools/lev.py --stats, verif_doom.py (etendu), verif_static.py

Usage : python tools\\doom2ps\\make_e1m1.py [--wad W] [--map E1M1] [--skill 3] [--lift-contact]
            [--e1m1-weapons] [--loading TITLEPIC|black] [--static-doors] [--name E1M1.LEV]
            [--out-dir cd_doom] [--retail cd/KILENTRY.LEV] [--no-verify]
Code de retour : 0 si tout est vert (engine_problems vides, verificateurs a 0 echec), 1 sinon.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(ROOT, "tools"), os.path.join(ROOT, "tools", "duke2ps")):
    if p not in sys.path:
        sys.path.insert(0, p)

import lev as levmod                                   # noqa: E402
import lev_io                                          # noqa: E402
import lev_write                                       # noqa: E402
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
import doom3d                                          # noqa: E402
import doomtiles                                       # noqa: E402
import doom_specials as sp                             # noqa: E402
import things2objects as t2o                           # noqa: E402
import wad2snd                                         # noqa: E402
import wad2sprites                                     # noqa: E402
import wad2static                                      # noqa: E402
import wad2hud                                         # noqa: E402

DEFAULT_WAD = wad2snd.DEFAULT_WAD
DEFAULT_IDS = wad2snd.DEFAULT_IDS
DEFAULT_OUT_DIR = os.path.join(ROOT, "cd_doom")
DEFAULT_RETAIL = os.path.join(ROOT, "cd", "KILENTRY.LEV")     # table K du ciel (identique sur les 24)
BUILD_DIR = os.path.join(ROOT, "build", "doom2ps")
DOOM_GAME_C = os.path.join(ROOT, "game", "doom", "DOOM_GAME.C")
RETAIL_COPIES = ["INITLOAD.DAT", "INTRO.PCS"]                 # DOOM_ABI §9, jamais commites
TILE_GEOM = 0x32                                              # 64x64 | 16BPP | PALLETE
SKY_W, SKY_H = 512, 256                                       # PLAX.C:91-93
SKY_TABLE = 320                                               # PLAX.C:115
SKY_HORIZON = 260                                             # xb de la ligne d'horizon (sky_block)


def log(msg=""):
    print(msg, flush=True)


# ----------------------------------------------------------------------------- etapes 1-3
def ensure_ids(ids_path):
    """build/doom/doom_ids.json (+ DOOM_TABLES.C) via info2tables si absent."""
    if not os.path.exists(ids_path):
        import info2tables
        log("  doom_ids.json absent -> info2tables.py")
        rc = info2tables.main(["--json", ids_path])
        if rc:
            raise SystemExit("info2tables.py a echoue (%s)" % rc)
    return t2o.load_ids(ids_path)


def run_geometry(wad_path, mapname, geom_path, static_doors=False, partition=None, optim=None,
                 diag_fusion=False, cap_canal=None):
    argv = ["--wad", wad_path, "--map", mapname, "--out", geom_path]
    if partition:
        argv += ["--partition", partition]     # doom3d.PARTITION : build_objects decoupe pareil
    if optim:
        argv += ["--optim", optim]             # doom3d.OPTIM_ACTIFS : build_objects fond pareil
    if cap_canal is not None:
        argv += ["--cap-canal", str(cap_canal)]   # doom3d.CAP_CANAL : idem
    if diag_fusion:
        argv.append("--diag-fusion")           # disque de diagnostic, jamais un disque livre
    if static_doors:
        argv.append("--static-doors")
    else:
        argv.append("--mobile")
    rc = doom3d.main(argv)
    if rc:
        raise SystemExit("doom3d.py : criteres en echec (%s)" % rc)
    with open(geom_path, encoding="utf-8") as f:
        return json.load(f)


def run_tiles(wad_path, geom_path, tiles_path):
    rc = doomtiles.main(["--wad", wad_path, "--geom", geom_path, "--out", tiles_path])
    if rc:
        raise SystemExit("doomtiles.py a echoue (%s)" % rc)
    with open(tiles_path, encoding="utf-8") as f:
        return json.load(f)


# ----------------------------------------------------------------------------- etape 4
def build_objects(W, M, ids, G, *, skill, lift_contact):
    """Joueur + mobjs (things2objects) puis speciaux (doom_specials), firstParam cumules."""
    specials = sp.specials_of(M)
    mobile = G.get("mobile")
    if mobile:
        doom3d.close_doors(M, specials, mobile.get("door_slit", sp.DOOR_SLIT))
    sizes = {nm: (t["width"], t["height"]) for nm, t in W.textures().items()}
    conv = doom3d.DoomConverter(M, sizes)
    fl = [s["floorLevel"] for s in G["sectors"]]
    things, tparams, st = t2o.things_to_objects(M, conv, ids, skill=skill, floor_levels=fl)
    notes = {}
    specs, sparams = [], bytearray()
    if mobile:
        specs, sparams, notes = sp.special_objects(M, conv, ids, specials, mobile["pb_index"],
                                                   lift_contact=lift_contact,
                                                   switches=mobile.get("switches"),
                                                   secret_walls=mobile.get("secret_walls"))
    objects, params = sp.concat_objects((things, tparams), (specs, sparams))
    return objects, params, dict(things=st, specials=specs, notes=notes)


# ----------------------------------------------------------------------------- etape 5
def sky_block(W, retail_path, lump="SKY1", horizon=SKY_HORIZON):
    """Ciel PLAX.C:84-115 : 256 u16 BGR555 (PLAYPAL), 512x256 indices, table de rotation K (320 ints)
    recopiee d'un .LEV retail -- identique sur les 24 niveaux du disque (meme sha1). L'indice 0 reste
    (RBG0 sans transparence, PLAX.C:108).

    Le bitmap est TRANSPOSE (matrice b = -1, d = 1, PLAX.C:138-141) : sa ligne yb (0..255) est la
    position HORIZONTALE -- 256 texels pour 90 degres (PLAXPERSCREEN 128 pour 45, PLAX.C:24-28),
    exactement les 1024 colonnes par tour du ciel de Doom -- et sa colonne xb (0..511) la HAUTEUR,
    le haut vers les xb croissants (les plantes du ciel de COLONY y poussent ; les 24 ciels retail
    sont tous ranges ainsi). L'ecrire a l'endroit donnait un ciel tourne de 90 degres et repete.
    A tangage nul la ligne centrale d'ecran lit xb = Xp ; l'art retail remplit 128..376, les 240
    lignes autour de 252 (ligne 120 de PowerSlave) ; notre cadre de 224 lignes a son horizon ligne
    112, 8 texels plus haut (viewp.y = 20 de PLAX.C:48 est cale sur 240) -> la ligne 100 de SKY1
    (skytexturemid de Doom, r_sky.c) tombe en xb `horizon` = 260 (estime, a confirmer a l'ecran).
    Doom dessine son ciel en miroir (r_plane.c : colonne = (viewangle + xtoviewangle[x]) >> 22, qui
    DECROIT vers la droite) -> colonne = -yb. Au-dela de la texture, la derniere ligne se repete."""
    tm = doomtiles.TileMaker(W)
    w, h, px = tm.texture(lump)
    assert w == SKY_H, "SKY1 attendu 256 de large : un tour = 4 x 256 colonnes (PLAX.C:28)"
    bmp = bytearray(SKY_W * SKY_H)
    for yb in range(SKY_H):
        col = (-yb) % w
        orow = yb * SKY_W
        for xb in range(SKY_W):
            r = min(max(100 - (xb - horizon), 0), h - 1)
            bmp[orow + xb] = px[r * w + col]
    r = levmod.Reader(retail_path)
    s = levmod.parse_sky(r)
    assert (s["width"], s["height"]) == (SKY_W, SKY_H), "ciel retail attendu 512x256 (PLAX.C:91-93)"
    assert s["table_off"] == 512 + 8 + SKY_W * SKY_H, "table K attendue a 0x20208 (PLAX.C:115)"
    table = list(struct.unpack(">%di" % SKY_TABLE, r.b[s["table_off"]:s["table_off"] + 4 * SKY_TABLE]))
    pal = [doomtiles.bgr555(c) for c in W.playpal(0)]
    return dict(palette=pal, width=SKY_W, height=SKY_H, bitmap=bytes(bmp), table=table), (w, h)


def append_switch_sequences(sq, descs):
    """Ajoute les 4 sequences par type OT_SW (doom_specials.switch_sequences) APRES les sequences
    de sprites : on retire l'entree terminale et la frame terminale, on ajoute, on les remet
    (STATIC.C:547-549 : sequence[N] = nmFrames, frame terminale.chunkIndex = nmChunks, pas de chunk
    terminal). `sequenceMap[type] = base` (AI2.C:598)."""
    seq, frames, chunks, smap = sq["sequence"], sq["frames"], sq["chunks"], sq["sequenceMap"]
    assert seq[-1] == len(frames) - 1 and frames[-1]["chunkIndex"] == len(chunks)
    seq.pop()
    frames.pop()
    for d in descs:
        base = len(seq)
        for frs in d["sequences"]:
            seq.append(len(frames))
            for fr in frs:
                frames.append(dict(chunkIndex=len(chunks), flags=0, sound=-1, pad=[0, 0]))
                for ch in fr:
                    chunks.append(dict(chunkx=ch["chunkx"], chunky=ch["chunky"], tile=ch["tile"],
                                       flags=ch.get("flags", 0), pad=0))
        assert smap[d["type"]] == wad2sprites.ABSENT, "sequenceMap[%d] deja pris" % d["type"]
        smap[d["type"]] = base
    seq.append(len(frames))
    frames.append(dict(chunkIndex=len(chunks), flags=0, sound=-1, pad=[0, 0]))


OT_ANM1, OT_ANM12 = 192, 203          # SLEVEL.H : dans la liste de markAnimTiles (PIC.C:137-138)
ANIM_REPEAT = 4                       # advanceWallAnimations avance 1 tic sur 2 (SRUINS.C:2197,
                                      # PIC.C:120) ; Doom change d'image tous les 8 tics -> 4 chunks


def anim_sequences(anims):
    """Une sequence OT_ANM1+k par famille animee (doom3d.anim_flat_keys) : une frame par pas
    d'animation, un chunk = la tuile de GEOMETRIE de l'image (loadSequences lui ajoute le meme
    tileBase que loadLevel). markAnimTiles marque chacune de ces tuiles ; mapPic les redirige."""
    assert len(anims) <= OT_ANM12 - OT_ANM1 + 1, "plus de 12 familles animees"
    return [dict(type=OT_ANM1 + k,
                 sequences=[[[dict(chunkx=0, chunky=0, tile=t)] for t in fam for _ in range(ANIM_REPEAT)]])
            for k, fam in enumerate(anims)]


def assemble_doom(G, T, sprites, sounds, objects, params, sky, palette, switches=()):
    """-> modele lev_io (SPEC_CONVERTER §6 etape 5). `sprites` = SpriteSet construit avec
    tile_base = len(T['tiles']) ; `sounds` = bloc {map, sounds} ; `palette` = 256 u16 (entree 0 =
    0x0000) partagee par les tuiles de geometrie (palNm 0) et les objets (objectPalette 0)."""
    geo = [dict(flags=TILE_GEOM, palNm=0, pixels=bytes.fromhex(h)) for h in T["tiles"]]
    assert len(geo) == len(G["tiles"]), "tiles.json ne correspond pas a la geometrie"
    tiles = geo + [dict(t) for t in sprites.tiles]
    sq = dict(frames=[dict(f) for f in sprites.frames], chunks=[dict(c) for c in sprites.chunks],
              sequence=list(sprites.sequence), sequenceMap=list(sprites.sequenceMap))
    if switches:
        append_switch_sequences(sq, sp.switch_sequences(switches))
    if G.get("anims"):
        append_switch_sequences(sq, anim_sequences(G["anims"]))
    mobile = G.get("mobile") or {}
    walls = [dict(w) for w in G["walls"]]
    # Murs sans face ni cellule (portails invisibles, firstFace == -1) : l'Emitter y laisse
    # firstVertex = lastVertex = 0xffff, la convention des PARALLELOGRAM. MESURE retail (KILENTRY 634,
    # SHRINE 1432 murs INVISIBLE non-para) : toujours 0/0 -- et lev.validate exige
    # `firstVertex <= lastVertex < nmVerticies` hors PARALLELOGRAM. On applique la convention retail.
    n_fix = 0
    for w in walls:
        if w["firstFace"] == -1 and not (w["flags"] & 0x01) and w["firstVertex"] == 0xFFFF:
            w["firstVertex"] = w["lastVertex"] = 0
            n_fix += 1
    level = dict(
        sectors=[dict(s) for s in G["sectors"]],
        walls=walls,
        vertices=[dict(v) for v in G["vertices"]],
        faces=[dict(v=list(f["v"]), tile=f["tile"], pad=0) for f in G["faces"]],
        objects=[dict(type=o["type"], firstParam=o["firstParam"]) for o in objects],
        objectParams=list(bytes(params)),
        pushBlocks=[dict(p) for p in mobile.get("pushBlocks", [])],
        PBVert=[dict(p) for p in mobile.get("PBVert", [])],
        waveVert=[], waveFace=[],
        PBWall=list(mobile.get("PBWall", [])),
        texture=list(G["texture"]), vertexLight=list(G["vertexLight"]),
        # Plans de coupe : rendent au peintre l'ordre que la fusion des feuilles lui prend
        # (doom3d.plans_de_coupe). Liste vide quand aucune fusion n'a eu lieu.
        cutPlane=[list(r) for r in (G.get("cutPlane") or [])],
        # Paires d'ordre : bloc optionnel apres le cutPlane (tools/ordre.py).
        orderPairs=[tuple(p) for p in (G.get("orderPairs") or [])])
    snd = dict(map=list(sounds["map"]),
               sounds=[dict(rate=s["rate"], bps=s["bps"], loopStart=s["loopStart"], pcm=s["pcm"])
                       for s in sounds["sounds"]])
    model = dict(sky=sky, level=level, sounds=snd,
                 palettes=dict(objectPalette=0, palettes=[list(palette)]),
                 tiles=tiles, sequences=sq)
    model["_notes"] = dict(faceless_walls_normalized=n_fix)
    return model


# ----------------------------------------------------------------------------- etape 7
def level_names_from_c(path=DOOM_GAME_C):
    """Les chaines de `doomLevelNames[...] = {"+E1M1.LEV", ...};` (DOOM_ABI §9)."""
    with open(path, encoding="latin-1") as f:
        src = f.read()
    m = re.search(r"doomLevelNames\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", src, re.S)
    if not m:
        return []
    return re.findall(r'"([^"]+)"', m.group(1))


def align4(n):
    """mem_nocheck_malloc arrondit chaque allocation a 4 (UTIL.C:370)."""
    return (n + 3) & ~3


def resident_tiles(tiles):
    """Octets RESIDENTS du jeu de tuiles : loadTileSet garde chaque tuile en RAM (lock = 0,
    PIC.C:713-715) -- 4 096 o par 0x32 (COMPRESS16BPP, mem_malloc(1), PIC.C:513) et le RLE tel quel
    par 0x6A (mem_malloc(0), PIC.C:575) ; les en-tetes du fichier (int n + 4/6 o par tuile) ne
    sont pas conserves. Zones 0 (LWRAM 1 Mo) et 1 (HWRAM _end..0x06100000) se debordent l'une
    dans l'autre (UTIL.C:365-388), d'ou une seule somme contre le pool total."""
    return sum(align4(4096 if "pixels" in t else len(t["rle"])) for t in tiles)


def resident_pool():
    """Pool reel du chargeur (UTIL.C:352-359) : LWRAM 1 Mo + (0x06100000 - _end de MAIN.map)."""
    mp = os.path.join(ROOT, "build", "doom", "MAIN.map")
    high = 435176
    src = "defaut (_end = 0x06095c18)"
    if os.path.exists(mp):
        with open(mp, encoding="latin-1") as f:
            m = re.search(r"^\s*0x([0-9a-fA-F]+)\s+_end\s*=", f.read(), re.M)
        if m:
            high = 0x06100000 - int(m.group(1), 16)
            src = "build/doom/MAIN.map (_end = 0x%08x)" % int(m.group(1), 16)
    return 1024 * 1024 + high, src


def run_tool(args, cwd=ROOT):
    """Sous-processus Python (les verificateurs ont un etat de module) ; -> (rc, sortie)."""
    p = subprocess.run([sys.executable] + args, cwd=cwd, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def atomic_copy(src, dst):
    tmp = dst + ".tmp"
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


# ----------------------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--skill", type=int, default=3, help="index gameskill (3 = UV)")
    ap.add_argument("--ids", default=DEFAULT_IDS)
    ap.add_argument("--lift-contact", action="store_true",
                    help="ascenseurs WR en channel = -1 (contact/press), sans sector-switch")
    ap.add_argument("--e1m1-weapons", action="store_true", help="STATIC.DAT : 5 familles d'armes (48 tuiles)")
    ap.add_argument("--loading", default="TITLEPIC", help="ecran de chargement : lump 320x200 ou `black`")
    ap.add_argument("--static-doors", action="store_true",
                    help="geometrie de controle : portes ouvertes en dur, sans push blocks ni speciaux")
    ap.add_argument("--name", default="E1M1.LEV", help="nom du .LEV (celui de doomLevelNames[])")
    ap.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    ap.add_argument("--retail", default=DEFAULT_RETAIL, help=".LEV retail donnant la table K du ciel")
    ap.add_argument("--build-dir", default=BUILD_DIR, help="JSON intermediaires")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--partition", choices=("grid", "bsp"), default=None,
                    help="morceaux convexes (doom3d --partition) ; defaut bsp")
    ap.add_argument("--optim", default=None,
                    help="optimisations de niveau (doom3d --optim) : all, none, ou une liste")
    ap.add_argument("--cap-canal", type=int, default=None,
                    help="avec --optim fusion : secteurs par canal de plans de coupe "
                         "(doom3d --cap-canal ; defaut %d)" % doom3d.CAP_CANAL)
    ap.add_argument("--diag-fusion", action="store_true",
                    help="DIAGNOSTIC (doom3d --diag-fusion) : damier sur les feuilles fusionnees")
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    os.makedirs(a.out_dir, exist_ok=True)
    os.makedirs(a.build_dir, exist_ok=True)
    stem = a.map.lower()
    geom_path = os.path.join(a.build_dir, "%s_geom3d.json" % stem)
    tiles_path = os.path.join(a.build_dir, "%s_tiles.json" % stem)
    lev_path = os.path.join(a.out_dir, a.name)
    static_path = os.path.join(a.out_dir, "STATIC.DAT")
    art_path = os.path.join(ROOT, "build", "doom", "doom_art.h")
    fails = []

    log("== 1. tables (%s)" % os.path.relpath(a.ids, ROOT))
    ids = ensure_ids(a.ids)
    W = wadmod.Wad(a.wad)
    M = wadmod.read_map(W, a.map)
    doom3d.OPTIM_ACTIFS = doom3d.lire_optim(a.optim)   # AVANT de fondre : run_geometry le refera
    doom3d.dissoudre_penombres(M)          # MEME carte que run_geometry (doom3d.main les fond aussi)

    log("== 2. geometrie %s (doom3d.py %s)" % (a.map, "--static-doors" if a.static_doors else "--mobile"))
    G = run_geometry(a.wad, a.map, geom_path, a.static_doors, a.partition, a.optim, a.diag_fusion,
                     a.cap_canal)
    n_geo = len(G["tiles"])

    log("== 3. tuiles de geometrie (doomtiles.py)")
    T = run_tiles(a.wad, geom_path, tiles_path)
    assert len(T["tiles"]) == n_geo

    log("== 4. sprites, sons, objets")
    playpal = W.playpal(0)
    palette, remap = rle8.object_palette(playpal)
    present = wad2snd.present_mobj_types(W, ids, a.map, a.skill)
    spawn = wad2snd.spawnable_mobj_types(ids, present)
    sprites = wad2sprites.build_sprites(W, ids, present, remap=remap, tile_base=n_geo)
    B = sprites.budget
    log("  sprites : %d familles, %d sequences (+1), %d frames, %d chunks, %d tuiles 0x6A, RLE %d o"
        % (B["families"], B["sequences"], B["frames"], B["chunks"], B["tiles"], B["bytes_rle"]))
    for wmsg in sprites.warnings[:5]:
        log("    avert : " + wmsg)
    tested, problems = wad2sprites.check_reachable(sprites, ids, present, tile_base=n_geo)
    log("  formule seq() : %d (MT, etat, vue), %d defauts" % (tested, len(problems)))
    if problems:
        fails.append("sequences atteignables (%d)" % len(problems))
    sounds, snd_names = wad2snd.sound_model(W, ids, spawn)
    log("  sons dynamiques : %d (%s), %d o de PCM" % (len(sounds["sounds"]), " ".join(snd_names),
                                                      sum(len(s["pcm"]) for s in sounds["sounds"])))
    objects, params, oinfo = build_objects(W, M, ids, G, skill=a.skill, lift_contact=a.lift_contact)
    st = oinfo["things"]
    from collections import Counter
    c = Counter(o["type"] for o in oinfo["specials"])
    log("  objets : %d (1 joueur + %d mobjs + %d speciaux : %s), %d o de params, %d starts ignores, "
        "%d ambush" % (len(objects), st["kept"], len(oinfo["specials"]),
                       " ".join("OT%d x%d" % kv for kv in sorted(c.items())), len(params),
                       st["starts"], st["ambush"]))
    for k, v in oinfo["notes"].items():
        log("    NOTE : %s x%d" % (k, v))
    if sp.expected_param_bytes(objects) != len(params):
        fails.append("params : %d attendus vs %d" % (sp.expected_param_bytes(objects), len(params)))

    log("== 5. assemblage -> %s" % os.path.relpath(lev_path, ROOT))
    sky, (skw, skh) = sky_block(W, a.retail)
    switches = (G.get("mobile") or {}).get("switches") or []
    model = assemble_doom(G, T, sprites, sounds, objects, params, sky, palette, switches)
    notes = model.pop("_notes")
    log("  %d murs sans face (portails invisibles) : firstVertex/lastVertex 0xffff -> 0 (convention retail)"
        % notes["faceless_walls_normalized"])
    probs = lev_write.engine_problems(model)
    if probs:
        fails.append("engine_problems : " + "; ".join(probs))
        for p_ in probs:
            log("  PROBLEME chargeur : " + p_)
        data = lev_write.write_lev(model, strict=False)
        lev_write.atomic_write(lev_path, data)
    else:
        data = lev_write.write_file(model, lev_path, strict=True)
    back, lay = lev_io.model_from_bytes(data, a.name)
    d = lev_io.diff(model, back)
    if d:
        fails.append("relecture : %d differences" % len(d))
    lvl_size = lay["level"]["size"]
    pal_sz = lay["tiles"]["palette_size"]
    til_sz = lay["tiles"]["end"] - lay["tiles"]["tileset_off"]
    seq_sz = lay["sequences"]["size"]
    log("  %s : %d o ; ciel SKY1 %dx%d -> %dx%d ; niveau %d o (< 900 000) ; palettes %d o ; "
        "tuiles %d (%d geometrie 0x32 + %d sprites 0x6A) = %d o ; sequences %d (%d o) ; sons %d"
        % (a.name, len(data), skw, skh, SKY_W, SKY_H, lvl_size, pal_sz, len(model["tiles"]), n_geo,
           len(sprites.tiles), til_sz, len(model["sequences"]["sequence"]) - 1, seq_sz,
           len(sounds["sounds"])))
    log("  relecture lev_io : %s ; engine_problems : %s" % ("identique" if not d else d[:3],
                                                            probs or "aucun"))

    log("== 6. STATIC.DAT, doom_art.h, copies retail")
    fams = wad2static.E1M1_FAMILIES if a.e1m1_weapons else wad2static.WEAPON_FAMILIES
    sinfo = wad2static.write_static(static_path, W, ids, loading=a.loading, weapons=fams)
    tile_base = sinfo["tileBase"]
    log("  STATIC.DAT : %d o = blocs %s ; tuiles d'armes n = tileBase = %d (RLE %d o) ; wseq %d"
        % (sinfo["total"], sinfo["blocks"], tile_base, sinfo["bytes_rle"], sinfo["sequences"]))
    ainfo = wad2hud.write_doom_art(art_path, W)
    log("  doom_art.h : %d o -> %s" % (ainfo["bytes"], os.path.relpath(art_path, ROOT)))
    for nm in RETAIL_COPIES:
        src = os.path.join(ROOT, "cd", nm)
        if os.path.exists(src):
            atomic_copy(src, os.path.join(a.out_dir, nm))
            log("  copie cd/%s -> %s (%d o)" % (nm, os.path.relpath(a.out_dir, ROOT), os.path.getsize(src)))
        else:
            fails.append("asset retail absent : cd/%s" % nm)
            log("  ECHEC : cd/%s absent" % nm)
    names = level_names_from_c()
    if not names:
        log("  AVERT : doomLevelNames[] introuvable dans %s" % os.path.relpath(DOOM_GAME_C, ROOT))
    for n in names:
        fn = n.lstrip("+")
        ok = os.path.exists(os.path.join(a.out_dir, fn))
        log("  doomLevelNames %-14s -> %s/%s : %s" % (n, os.path.relpath(a.out_dir, ROOT), fn,
                                                     "present" if ok else "ABSENT"))
        if not ok:
            fails.append("doomLevelNames %s absent de %s" % (n, a.out_dir))
    if names and ("+" + a.name) not in names and a.name not in [n.lstrip("+") for n in names]:
        log("  AVERT : --name %s n'est pas dans doomLevelNames[] %s (disque de controle ?)" % (a.name, names))

    # budget geometrie u8 (LEVEL.C:69-72) et pool memoire (UTIL.C:352-359)
    if n_geo + tile_base > 255:
        fails.append("geometrie %d + tileBase %d > 255" % (n_geo, tile_base))
    log("  tuiles u8 : geometrie %d + tileBase %d = %d <= 255 (marge %d) ; MAXNMPICS : %d + %d = %d < 800"
        % (n_geo, tile_base, n_geo + tile_base, 255 - n_geo - tile_base, tile_base, len(model["tiles"]),
           tile_base + len(model["tiles"])))
    pool, psrc = resident_pool()
    til_res = resident_tiles(model["tiles"])
    resident = align4(lvl_size) + align4(pal_sz) + til_res + align4(seq_sz)
    static_res = sinfo["weapon_tiles_bytes"] + align4(sinfo["wseq_bytes"])
    log("  residents (en memoire, alignes 4) : niveau %d + palettes %d + tuiles %d (fichier %d) + sequences %d "
        "= %d ; + STATIC (tuiles d'armes + wseq) %d = %d ; pool %d (%s) -> %s, marge %d"
        % (lvl_size, pal_sz, til_res, til_sz, seq_sz, resident, static_res, resident + static_res, pool, psrc,
           "OK" if resident + static_res <= pool else "DEPASSEMENT", pool - resident - static_res))
    if resident + static_res > pool:
        fails.append("memoire residente %d > pool %d" % (resident + static_res, pool))

    if a.no_verify:
        log("\n%s" % ("OK (sans verificateurs)" if not fails else "ECHEC : " + " | ".join(fails)))
        return 1 if fails else 0

    log("== 7. verificateurs")
    stats_json = os.path.join(a.build_dir, "%s_stats.json" % stem)
    rc, out = run_tool(["tools/lev.py", "--stats", stats_json, "--dump", os.path.join(a.build_dir, "dump"),
                        lev_path])
    log("  tools/lev.py --stats : rc %d\n    " % rc + out.strip().replace("\n", "\n    "))
    if rc:
        fails.append("lev.py --stats rc %d" % rc)
    rc, out = run_tool(["tools/doom2ps/verif_doom.py", "--lev", lev_path, "--geom", geom_path, "--tiles",
                        tiles_path, "--wad", a.wad, "--map", a.map, "--ids", a.ids, "--static", static_path,
                        "--skill", str(a.skill)])
    log("  verif_doom.py : rc %d\n    " % rc + out.strip().replace("\n", "\n    "))
    if rc:
        fails.append("verif_doom.py rc %d" % rc)
    vargs = ["tools/doom2ps/verif_static.py", static_path, "--ids", a.ids, "--wad", a.wad]
    if a.e1m1_weapons:
        vargs.append("--e1m1-weapons")
    rc, out = run_tool(vargs)
    log("  verif_static.py : rc %d\n    " % rc + out.strip().replace("\n", "\n    "))
    if rc:
        fails.append("verif_static.py rc %d" % rc)

    log("\n%s" % ("TOUT VERT" if not fails else "ECHEC : " + " | ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
