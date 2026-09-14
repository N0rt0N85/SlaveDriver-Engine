#!/usr/bin/env python3
"""wad2sprites.py -- S_* de Doom -> tuiles 0x6A + sequences SlaveDriver (contrat DOOM_ABI section 2).

Un etat Doom = une sequence par vue, les tics sont comptes par le runtime (pas de
spriteAdvanceFrame). Pour le sous-ensemble d'un niveau (MT presents + spawnables, hors MT_PLAYER qui
n'est jamais dessine en vue subjective) :

  famille (sprite)  occupe `(maxframe + 1) * stride` sequences contigues a partir de `base`,
                    `stride = 8` si un lump de la famille porte une rotation != 0 dans le WAD, 1 sinon
  sequence          `seq(spr, frame, vue) = (map[spr] & 0x7fff) + frame * stride + (stride == 8 ? vue : 0)`
                    avec la garde `map[spr] == -2` AVANT le test du bit 0x8000 (contrat section 2)
  vue moteur k      = rotation Doom k + 1 (lump `XXXXFk+1`) ; lump `XXXXF2F8` = vue 1 directe, vue 7 en
                    miroir (flag chunk 1, `chunkx' = -chunkx - 64 + (w - 2 lo)`, leftoffset garde comme Doom)
  frame sans rotation dans une famille a rotations (POSSH0..U0) : les 8 vues sont peuplees avec le meme
                    lump (le runtime demande toujours `seq(spr, frame, vue)` avec la vue courante et une
                    sequence vide dessinerait la suivante, WALLS.C:2777-2779) ; la tuile est partagee,
                    seuls les 8 o de frame + 8 o de chunk sont dupliques
  frame             1 par sequence, `flags 0`, `sound -1`, `pad 0` ; sequence non peuplee = vide
  carte             `sequenceMap[227] = -2` partout, `map[spritenum] = base | 0x8000` si rotations
                    (short signe), entrees 163-171 laissees au moteur, 172-203 < 0 (markAnimTiles)
  terminaux         `sequence[N] = nmFrames`, frame terminale `chunkIndex = nmChunks`, PAS de chunk
                    terminal (STATIC.C:547-549) -- inclus dans SpriteSet.sequence / .frames
  tuiles            0x6A, RLE de rle8.py, dedoublonnees par sha1 ; `chunk.tile` est relatif au debut
                    des tuiles de sprites : passer `tile_base = nombre de tuiles de geometrie` qui les
                    precedent dans le bloc tuiles du .LEV (le moteur ajoute ensuite `nmWeaponTiles`,
                    SEQUENCE.C:74-75)

API :
  rotations(wad, family) -> (dict[(frame, vue)] -> (lump, miroir), has_rotations)
  families_for(ids, mobj_types, wad) -> dict[sprite] -> dict(spritenum, maxframe, rotations,
                                                             reachable_frames, mts)
  build_sprites(wad, ids, mobj_types, *, remap=None, tile_base=0, close_spawns=True) -> SpriteSet
  seq(sequence_map, spr, frame, view) -> int          formule du contrat (identique au C)

Usage : python tools\\doom2ps\\wad2sprites.py [--wad W] [--ids build/doom/doom_ids.json] [--map E1M1]
        [--skill 3]   (test : comptes, formule seq() sur tous les etats atteignables x 8 vues ; n'ecrit rien)
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import namedtuple

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
import wad2snd                                         # noqa: E402

DEFAULT_WAD = wad2snd.DEFAULT_WAD
DEFAULT_IDS = wad2snd.DEFAULT_IDS

OT_NMTYPES = 227
ABSENT = -2                       # famille absente (contrat section 2)
ROT_FLAG = 0x8000                 # convention HB d'AI.C:2478 / AICOMMON.H:5
ANIM_RANGE = range(172, 204)      # markAnimTiles PIC.C:129-161 : doit rester < 0
MAXNMPICS = 800                   # PIC.C:32
SEQ_BLOCK_MAX = 1024 * 1024       # SEQUENCE.C:30
MT_PLAYER = 0                     # jamais dessine (vue subjective) : PLAY ne garde que ses cadavres

SpriteSet = namedtuple("SpriteSet", "tiles frames chunks sequence sequenceMap families budget warnings")
FrameRec = dict
SEQ_HEADER = 12                   # struct seqHeader (SLEVEL.H:199-203)


def frame_rec(chunk_index):
    """sFrameType (SLEVEL.H:214-219) : flags 0, sound -1 (SEQUENCE.C:76-78 ne decale que != -1)."""
    return dict(chunkIndex=chunk_index, flags=0, sound=-1, pad=[0, 0])


def chunk_rec(chunkx, chunky, tile, flags=0):
    """sChunkType (SLEVEL.H:221-226) ; flags bit 0 = miroir horizontal (WALLS.C:2789)."""
    return dict(chunkx=chunkx, chunky=chunky, tile=tile, flags=flags, pad=0)


def to_short(v):
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


def seq(sequence_map, spr, frame, view):
    """Contrat section 2 -- formule identique au C (garde -2 avant le bit 15)."""
    m = sequence_map[spr]
    if m == ABSENT:
        return ABSENT
    stride = 8 if (m & ROT_FLAG) else 1
    return (m & 0x7FFF) + frame * stride + (view if stride == 8 else 0)


# ----------------------------------------------------------------------------- lumps d'une famille
def rotations(wad, family):
    """(dict[(frame, vue)] -> (lump, miroir), has_rotations). Lump `FFFFXr` ou `FFFFXrYs` (r_segs :
    R_InstallSpriteLump) ; rotation 0 = toutes les vues, meme lump, sans miroir ; rotation r = vue
    r - 1 ; le second couple d'un lump de 8 caracteres est la vue miroir."""
    out = {}
    has_rot = False
    for nm in rle8.sprite_lumps(wad):
        if len(nm) < 6 or nm[:4] != family:
            continue
        pairs = [(nm[4], nm[5], False)]
        if len(nm) == 8:
            pairs.append((nm[6], nm[7], True))
        for letter, digit, mirror in pairs:
            f = ord(letter) - ord("A")
            r = int(digit)
            if r == 0:
                for v in range(8):
                    out.setdefault((f, v), (nm, False))
            else:
                has_rot = True
                out[(f, r - 1)] = (nm, mirror)
    return out, has_rot


def families_for(ids, mobj_types, wad):
    """sprite -> {spritenum, maxframe, rotations, reachable_frames, mts} pour les MT donnes (MT_PLAYER
    exclu : il n'a pas de vue a la 3e personne, ses cadavres MISC62/68/69 restent)."""
    states = ids["states"]
    names = ids["sprite_names"]
    fam = {}
    for mt in sorted(set(mobj_types)):
        if mt == MT_PLAYER:
            continue
        for s in wad2snd.reachable_states(ids, mt):
            st = states[s]
            spr = names[st["sprite"]]
            d = fam.setdefault(spr, dict(spritenum=st["sprite"], maxframe=0, reachable_frames=set(),
                                         mts=set(), rotations=False))
            d["reachable_frames"].add(st["frame"])
            d["maxframe"] = max(d["maxframe"], st["frame"])
            d["mts"].add(mt)
    for spr, d in fam.items():
        d["lumps"], d["rotations"] = rotations(wad, spr)
    return fam


# ----------------------------------------------------------------------------- construction
def build_sprites(wad, ids, mobj_types, remap=None, tile_base=0, close_spawns=True):
    """SpriteSet pour les MT donnes (fermes par les spawnables si close_spawns). `remap` = remap_index
    de rle8.object_palette (0/255 deplaces) ; defaut : calcule sur PLAYPAL[0]."""
    if remap is None:
        _, remap = rle8.object_palette(wad.playpal(0))
    mts = wad2snd.spawnable_mobj_types(ids, mobj_types) if close_spawns else set(mobj_types)
    fam = families_for(ids, mts, wad)
    bank = rle8.TileBank()
    frames, chunks, sequence = [], [], []
    smap = [ABSENT] * OT_NMTYPES
    warnings = []
    lump_chunks = {}                                     # lump -> [(chunkx, chunky, tile)]
    lump_dims = {}                                       # lump -> (w, lo) pour le miroir Doom

    def chunks_of(lump):
        if lump not in lump_chunks:
            p = rle8.read_patch(wad, lump)
            lump_dims[lump] = (p.w, p.lo)
            cut, _, _ = rle8.cut_patch(p, remap)
            lump_chunks[lump] = [(c.chunkx, c.chunky, bank.add(c.pixels)) for c in cut]
        return lump_chunks[lump]

    populated = 0
    for spr in sorted(fam, key=lambda n: fam[n]["spritenum"]):
        d = fam[spr]
        stride = 8 if d["rotations"] else 1
        base = len(sequence)
        d["base"], d["stride"], d["populated"] = base, stride, 0
        for f in range(d["maxframe"] + 1):
            for v in range(stride):
                sequence.append(len(frames))              # debut de la sequence (vide si rien n'est ajoute)
                if f not in d["reachable_frames"]:
                    continue
                lm = d["lumps"].get((f, v))
                if lm is None:
                    warnings.append("%s frame %s vue %d : lump absent du WAD -> sequence vide"
                                    % (spr, chr(ord("A") + f), v))
                    continue
                lump, mirror = lm
                frames.append(frame_rec(len(chunks)))
                for cx, cy, t in chunks_of(lump):
                    if mirror:
                        chunks.append(chunk_rec(rle8.mirror_chunkx(cx, *lump_dims[lump]), cy,
                                                t + tile_base, 1))
                    else:
                        chunks.append(chunk_rec(cx, cy, t + tile_base, 0))
                d["populated"] += 1
                populated += 1
        smap[d["spritenum"]] = to_short(base | (ROT_FLAG if stride == 8 else 0))
    n_seq = len(sequence)
    sequence.append(len(frames))                         # entree terminale (STATIC.C:547)
    frames.append(frame_rec(len(chunks)))                # frame terminale (:548-549), pas de chunk
    block = SEQ_HEADER + len(sequence) * 2 + len(frames) * 8 + len(chunks) * 8 + OT_NMTYPES * 2
    assert all(smap[i] < 0 for i in ANIM_RANGE), "sequenceMap[172..203] doit rester < 0"
    assert all(smap[i] == ABSENT for i in range(163, 172)), "163-171 reserves au moteur"
    budget = dict(tiles=len(bank.tiles), bytes_rle=bank.bytes_rle, sequences=n_seq,
                  frames=len(frames) - 1, frames_populated=populated, chunks=len(chunks),
                  lumps=len(lump_chunks), block_bytes=block, block_max=SEQ_BLOCK_MAX,
                  maxnmpics=MAXNMPICS, families=len(fam))
    assert block < SEQ_BLOCK_MAX
    return SpriteSet(bank.tiles, frames, chunks, sequence, smap, fam, budget, warnings)


# ----------------------------------------------------------------------------- verification
def check_reachable(ss, ids, mobj_types, close_spawns=True, tile_base=0):
    """Rejoue le moteur : pour chaque (MT, etat atteignable, vue 0..7), seq() -> sequence non vide,
    1 frame, chunks -> tuile existante. Retourne (nb testes, liste des defauts).
    `tile_base` = celui passe a build_sprites : `chunk.tile - tile_base` indexe `ss.tiles` (les
    tuiles de sprites seules) ; 0 quand `ss.tiles` est le jeu complet relu dans un .LEV."""
    mts = wad2snd.spawnable_mobj_types(ids, mobj_types) if close_spawns else set(mobj_types)
    states = ids["states"]
    problems = []
    tested = 0
    for mt in sorted(mts):
        if mt == MT_PLAYER:
            continue
        for s in wad2snd.reachable_states(ids, mt):
            st = states[s]
            for v in range(8):
                tested += 1
                q = seq(ss.sequenceMap, st["sprite"], st["frame"], v)
                if q == ABSENT or not (0 <= q < len(ss.sequence) - 1):
                    problems.append((ids["mt_names"][mt], st["name"], v, "seq %d" % q))
                    continue
                a, b = ss.sequence[q], ss.sequence[q + 1]
                if b - a != 1:
                    problems.append((ids["mt_names"][mt], st["name"], v, "%d frames" % (b - a)))
                    continue
                for c in range(ss.frames[a]["chunkIndex"], ss.frames[a + 1]["chunkIndex"]):
                    ch = ss.chunks[c]
                    t = ch["tile"] - tile_base
                    if not (0 <= t < len(ss.tiles)) or ss.tiles[t]["flags"] != 0x6A:
                        problems.append((ids["mt_names"][mt], st["name"], v, "tile %d" % ch["tile"]))
    return tested, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    ap.add_argument("--ids", default=DEFAULT_IDS)
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--skill", type=int, default=3)
    a = ap.parse_args(argv)
    w = wadmod.Wad(a.wad)
    ids = json.load(open(a.ids, encoding="utf-8"))
    present = wad2snd.present_mobj_types(w, ids, a.map, a.skill)
    ss = build_sprites(w, ids, present)
    B = ss.budget
    print("%s skill %d : %d familles, %d lumps decoupes" % (a.map, a.skill, B["families"], B["lumps"]))
    print("  %-4s %4s %6s %6s %7s %7s  %s" % ("spr", "num", "base", "stride", "seqs", "frames", "atteignables"))
    for spr in sorted(ss.families, key=lambda n: ss.families[n]["spritenum"]):
        d = ss.families[spr]
        letters = "".join(chr(ord("A") + f) for f in sorted(d["reachable_frames"]))
        print("  %-4s %4d %6d %6d %7d %7d  %s" % (spr, d["spritenum"], d["base"], d["stride"],
                                               (d["maxframe"] + 1) * d["stride"], d["populated"], letters))
    print("sequences %d (+1 terminale), frames %d (+1 terminale), chunks %d, tuiles %d, RLE %d o, "
          "bloc sequences %d o < %d" % (B["sequences"], B["frames"], B["chunks"], B["tiles"],
                                        B["bytes_rle"], B["block_bytes"], B["block_max"]))
    print("  (contrat : ~458 sequences / 276 frames / 211 tuiles ; l'ecart en sequences/frames vient des "
          "8 vues peuplees par frame sans rotation des familles a rotations, cf. en-tete)")
    assert len(ss.sequence) == B["sequences"] + 1 and ss.sequence[0] == 0
    assert ss.sequence[-1] == B["frames"] and ss.frames[-1]["chunkIndex"] == B["chunks"]
    assert all(ss.sequence[i] <= ss.sequence[i + 1] for i in range(B["sequences"]))
    assert all(f["pad"] == [0, 0] and f["sound"] == -1 for f in ss.frames)
    assert all(c["pad"] == 0 for c in ss.chunks)
    assert B["block_bytes"] == 12 + (B["sequences"] + 1) * 2 + (B["frames"] + 1) * 8 + B["chunks"] * 8 + 454
    tested, problems = check_reachable(ss, ids, present)
    print("formule seq() : %d (MT, etat, vue) testes, %d defauts" % (tested, len(problems)))
    for p in problems[:10]:
        print("   ", p)
    for wmsg in ss.warnings[:10]:
        print("  avert :", wmsg)
    # carte
    n_fam = sum(1 for m in ss.sequenceMap if m != ABSENT)
    print("sequenceMap : %d entrees != -2, [172..203] tous < 0 : %s, [163..171] = -2 : %s"
          % (n_fam, all(ss.sequenceMap[i] < 0 for i in ANIM_RANGE),
             all(ss.sequenceMap[i] == ABSENT for i in range(163, 172))))
    for spr in ("POSS", "TROO", "PLAY", "BAL1", "ELEC"):
        if spr in ss.families:
            d = ss.families[spr]
            print("  map[%s=%d] = %d (0x%04x)" % (spr, d["spritenum"], ss.sequenceMap[d["spritenum"]],
                                                ss.sequenceMap[d["spritenum"]] & 0xFFFF))
    # relecture par le serialiseur du .LEV (lev_write) puis lev_io
    try:
        sys.path.insert(0, os.path.join(ROOT, "tools", "duke2ps"))
        import lev_write                                 # noqa: E402
        data = lev_write.serialize_sequences(dict(frames=ss.frames, chunks=ss.chunks, sequence=ss.sequence,
                                                  sequenceMap=ss.sequenceMap))
        tiles = lev_write.serialize_tiles(ss.tiles)
        print("  serialize_sequences %d o (= 4 + bloc), serialize_tiles %d o" % (len(data), len(tiles)))
        assert len(data) == 4 + B["block_bytes"]
    except ImportError as e:
        print("  (lev_write non importable : %s)" % e)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
