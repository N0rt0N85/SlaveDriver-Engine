#!/usr/bin/env python3
"""wad2static.py -- STATIC.DAT Doom : les 4 blocs lus par SRUINS.C runLevel, dans l'ordre.

Contrat docs/doom/DOOM_ABI.md section 5 (SPEC_CONVERTER section 3, tache j4) :

  1. bloc logo             doom_loadingScreen (game/doom/DOOM_TITLE.C, CFG_LOADING_SCREEN) : le bloc
                           logo de DTITLE.DAT, octet pour octet (wad2title.logo_block : PLAYPAL, rampe,
                           P_load, flux aleatoires du feu, masque LOADING, M_DOOM) -- l'ecran de
                           chargement est l'ecran titre qui continue ; `--loading black` = sans logo
                           ni masque. Plus d'image 320x240 (77 320 o) : ~9,5 Ko.
     (l'ancien bloc 2, la feuille VDP2 de 262 144 zeros de loadVDP2Sprites, n'est plus ecrit :
      CFG_VDP2_SHEET est vide pour Doom, dont l'arme est en tuiles VDP1 -- 330 Ko de moins lus a
      chaque chargement, et la VRAM B reste au logo et au feu)
  3. sons statiques        wad2snd.static_sound_block : int 8, 8 shorts ([3] = 6), int 20, 20 sons.
  4. tuiles d'armes        loadWeaponTiles PIC.C:709-711 = loadTileSet(fd, 0) : `int n` puis n tuiles
                           0x6A (`short flags, short palNm, short size, RLE`, PIC.C:671-700, 568-580) ;
                           decoupe LUMP PAR LUMP (rle8.cut_patch, grille complete, SANS dedoublonnage :
                           le contrat fixe n = 95 = somme des grilles), ordre = ordre des lumps dans le
                           WAD, chunks (c, r) ligne par ligne ; 10 familles par defaut, 5 avec
                           --e1m1-weapons (n = 48). `loadWeaponTiles` retourne n = tileBase.
  5. sequences d'armes     loadWeaponSequences SEQUENCE.C:92-132 : `int size` + seqHeader (3 ints) +
                           frames (sFrameType 8 o) + chunks (sChunkType 8 o) + nmSequences shorts,
                           SANS carte ; wseq(state) = state - 1 pour S_LIGHTDONE (1) .. S_BFGFLASH2 (89)
                           + entree terminale (STATIC.C:547-549) ; une frame par etat dont le lump
                           `sprite + lettre + '0'` est decoupe, flags 0, sound -1, chunks aux offsets
                           `(-lo + 64c, -to + 64r)` ; etats sans lump (SHTGE0, SHT2, PLSG/PLSF, BFGG/BFGF)
                           = sequence vide ; wSequence[0] == 0 (asserte :126).

Et a cote, DTITLE.DAT (l'ecran titre : logo, feu, polices, crane -- wad2title.py) : write_static
l'ecrit dans le meme repertoire, pour que make_e1m1 le pose sur le disque sans changer d'appel.

Usage : python tools\\doom2ps\\wad2static.py [--wad W] [--ids build/doom/doom_ids.json]
                                             [--out cd_doom/STATIC.DAT] [--loading TITLEPIC|black]
                                             [--e1m1-weapons]
Ecriture atomique (temp + os.replace). Ne modifie rien d'autre.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(ROOT, "tools", "duke2ps")):
    if p not in sys.path:
        sys.path.insert(0, p)
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
import wad2snd                                         # noqa: E402
import wad2title                                       # noqa: E402
from lev_write import atomic_write, serialize_tiles    # noqa: E402

DEFAULT_WAD = wad2snd.DEFAULT_WAD
DEFAULT_IDS = wad2snd.DEFAULT_IDS
DEFAULT_OUT = os.path.join(ROOT, "cd_doom", "STATIC.DAT")

WEAPON_FAMILIES = ["PISG", "PISF", "SHTG", "SHTF", "PUNG", "CHGG", "CHGF", "MISG", "MISF", "SAWG"]
E1M1_FAMILIES = WEAPON_FAMILIES[:5]                    # repli --e1m1-weapons (SPEC_CONVERTER E1)
WSEQ_FIRST, WSEQ_LAST = 1, 89                          # S_LIGHTDONE .. S_BFGFLASH2 (info.h)
SEQ_HEADER_FMT = ">iii"                                # struct seqHeader SLEVEL.H:199-203
FRAME_FMT = ">hhhbb"                                   # sFrameType SLEVEL.H:214-219
CHUNK_FMT = ">hhhbb"                                   # sChunkType SLEVEL.H:221-226
SEQ_BLOCK_MAX = 1024 * 1024                            # SEQUENCE.C:102


# ----------------------------------------------------------------------------- bloc 4
def weapon_lumps(wad, families):
    """Lumps S_START..S_END dont la famille est demandee, dans l'ordre du WAD."""
    fams = set(families)
    return [n for n in rle8.sprite_lumps(wad) if n[:4] in fams]


SHADOW_RADIUS = 21.3                  # texels ; 32 = le disque retail, jugé trop large (testeur, -1/3)


SHADOW_INDEX = 247                    # PLAYPAL : le SEUL autre noir pur que l'indice 0 (0,0,0),
                                      # et l'indice 0 est le transparent du VDP1. L'ombre Doom est
                                      # dessinee en MESH (un pixel sur deux) et non en COMPO_SHADOW
                                      # (WALLS.C drawSprites, CFG_SHADOW_MODE) : c'est la couleur
                                      # ECRITE qui fait l'ombre, il la faut noire dans la banque 0.


def shadow_disc(value=SHADOW_INDEX, r=SHADOW_RADIUS):
    """64 x 64 indices : disque plein centre de rayon `r`, 0 = transparent autour. drawSprites
    etire toujours la tuile entiere sur 48 u x l'echelle (WALLS.C:2751) : le rayon du disque dans
    la tuile regle donc la taille de l'ombre sans toucher au moteur."""
    return bytes(value if (x - 31.5) ** 2 + (y - 31.5) ** 2 <= r * r else 0
                 for y in range(64) for x in range(64))


def weapon_tiles(wad, families, remap):
    """-> (tiles, chunks_by_lump). Une tuile 0x6A par case de la grille de chaque lump, sans
    dedoublonnage (contrat : n = somme des grilles = 95) ; chunks_by_lump[lump] = [(chunkx, chunky,
    tile)] avec `tile` = index dans le jeu de tuiles d'armes (pas de tileBase : loadWeaponSequences
    n'en ajoute pas, les tuiles d'armes sont les premiers pics, addPic PIC.C:405-407)."""
    # Tuile 0 = l'OMBRE : drawSprites pose `mapPic(0)` sous chaque sprite, en COMPO_SHADOW
    # (WALLS.C:2762) -- tout pixel non transparent assombrit ce qu'il couvre. Le retail y a un
    # disque plein de 64 x 64 ; sans elle la 1re tuile d'arme servait d'ombre (forme bizarre).
    tiles, by_lump = [rle8.tile_record(rle8.rle8(shadow_disc()))], {}
    for name in weapon_lumps(wad, families):
        p = rle8.read_patch(wad, name)
        cut, _, _ = rle8.cut_patch(p, remap)
        lst = []
        for c in cut:
            tiles.append(rle8.tile_record(rle8.rle8(c.pixels)))
            lst.append((c.chunkx, c.chunky, len(tiles) - 1))
        by_lump[name] = lst
    return tiles, by_lump


# ----------------------------------------------------------------------------- bloc 5
def wseq(state):
    """Contrat section 5 : wseq(state) = state - 1 pour S_LIGHTDONE (1) .. S_BFGFLASH2 (89)."""
    assert WSEQ_FIRST <= state <= WSEQ_LAST
    return state - 1


def psprite_lump(ids, state):
    st = ids["states"][state]
    return "%s%s0" % (st["spritename"], chr(ord("A") + st["frame"]))


def weapon_sequences(ids, chunks_by_lump):
    """-> (frames, chunks, sequence) : listes de tuples FRAME_FMT/CHUNK_FMT et 90 shorts (89 + terminale).
    Une frame par etat dont le lump est decoupe ; sinon sequence vide (sequence[s] == sequence[s+1])."""
    frames, chunks, sequence = [], [], []
    populated = []
    for s in range(WSEQ_FIRST, WSEQ_LAST + 1):
        assert wseq(s) == len(sequence)
        sequence.append(len(frames))
        lst = chunks_by_lump.get(psprite_lump(ids, s))
        if lst is None:
            continue
        frames.append((len(chunks), 0, -1, 0, 0))            # chunkIndex, flags 0, sound -1, pad
        for cx, cy, t in lst:
            chunks.append((cx, cy, t, 0, 0))                  # chunkx, chunky, tile, flags 0, pad 0
        populated.append(s)
    sequence.append(len(frames))                              # entree terminale (STATIC.C:547)
    frames.append((len(chunks), 0, -1, 0, 0))                 # frame terminale (:548-549)
    assert sequence[0] == 0
    return frames, chunks, sequence, populated


def serialize_weapon_sequences(frames, chunks, sequence):
    """SEQUENCE.C:92-132 : `int size` + header + frames + chunks + shorts, sans carte."""
    buf = struct.pack(SEQ_HEADER_FMT, len(sequence), len(frames), len(chunks))
    buf += b"".join(struct.pack(FRAME_FMT, *f) for f in frames)
    buf += b"".join(struct.pack(CHUNK_FMT, *c) for c in chunks)
    buf += struct.pack(">%dh" % len(sequence), *sequence)
    assert len(buf) < SEQ_BLOCK_MAX
    assert (12 + 8 * len(frames) + 8 * len(chunks)) % 4 == 0   # level_wSequence & 3 == 0 (:129)
    return struct.pack(">i", len(buf)) + buf


# ----------------------------------------------------------------------------- assemblage
def build_static(wad, ids, loading="TITLEPIC", families=None):
    """-> (bytes, info). `families` : liste de familles d'armes (defaut : les 10)."""
    if families is None:
        families = WEAPON_FAMILIES
    _pal, remap = rle8.object_palette(wad.playpal(0))
    b1 = wad2title.logo_block(wad, loading)             # "black" : sans logo ni masque
    b3 = wad2snd.static_sound_block(wad)
    tiles, by_lump = weapon_tiles(wad, families, remap)
    b4 = serialize_tiles(tiles)
    frames, chunks, sequence, populated = weapon_sequences(ids, by_lump)
    b5 = serialize_weapon_sequences(frames, chunks, sequence)
    distinct = len(set(hashlib.sha1(t["rle"]).digest() for t in tiles))
    info = dict(blocks=[len(b1), len(b3), len(b4), len(b5)],     # pas de bloc 2 (feuille VDP2)
                total=len(b1) + len(b3) + len(b4) + len(b5),
                tileBase=len(tiles), tiles_distinct=distinct,
                bytes_rle=sum(len(t["rle"]) for t in tiles), rle_max=max(len(t["rle"]) for t in tiles),
                lumps=len(by_lump), families=list(families),
                sequences=len(sequence) - 1, frames=len(frames) - 1, chunks=len(chunks),
                populated_states=populated, seq_block=len(b5) - 4,
                # residents en RAM (PIC.C:575 mem_malloc(0,size) par tuile ; SEQUENCE.C:100 le bloc
                # sans son `int size`), alignes 4 comme mem_nocheck_malloc (UTIL.C:370)
                weapon_tiles_bytes=sum((len(t["rle"]) + 3) & ~3 for t in tiles),
                wseq_bytes=(len(b5) - 4 + 3) & ~3)
    return b"".join([b1, b3, b4, b5]), info


def write_static(path, wad, ids, *, loading="TITLEPIC", weapons=None):
    """STATIC.DAT, puis DTITLE.DAT dans le meme repertoire (info title_path, title_bytes)."""
    data, info = build_static(wad, ids, loading, weapons)
    atomic_write(path, data)
    info["path"] = path
    tinfo = wad2title.write_title(os.path.join(os.path.dirname(os.path.abspath(path)), "DTITLE.DAT"), wad)
    info["title_path"], info["title_bytes"] = tinfo["path"], tinfo["bytes"]
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    ap.add_argument("--ids", default=DEFAULT_IDS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--loading", default="TITLEPIC", help="lump 320x200 ou `black`")
    ap.add_argument("--e1m1-weapons", action="store_true", help="5 familles (48 tuiles) au lieu de 10")
    a = ap.parse_args(argv)
    w = wadmod.Wad(a.wad)
    ids = json.load(open(a.ids))
    fams = E1M1_FAMILIES if a.e1m1_weapons else WEAPON_FAMILIES
    info = write_static(a.out, w, ids, loading=a.loading, weapons=fams)
    print("%s : %d o = blocs %s" % (info["path"], info["total"], info["blocks"]))
    print("%s : %d o" % (info["title_path"], info["title_bytes"]))
    print("tuiles d'armes : n = tileBase = %d (%d lumps, %d distinctes par sha1), RLE %d o, max %d o"
          % (info["tileBase"], info["lumps"], info["tiles_distinct"], info["bytes_rle"], info["rle_max"]))
    print("sequences d'armes : %d (+1 terminale), frames %d (+1), chunks %d, bloc %d o ; etats peuples : %s"
          % (info["sequences"], info["frames"], info["chunks"], info["seq_block"],
             " ".join(ids["state_names"][s] for s in info["populated_states"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
