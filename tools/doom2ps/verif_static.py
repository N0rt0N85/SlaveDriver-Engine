#!/usr/bin/env python3
"""verif_static.py -- relit STATIC.DAT Doom et rejoue les 4 lecteurs du moteur (SRUINS.C runLevel).

Tests (SPEC_CONVERTER section 7, `verif_static.py`) :
  1. doom_loadingScreen (DOOM_TITLE.C) : le bloc logo (wad2title), relu comme verify_title le relit
     (magie, 37 niveaux, rampe, P_title, P_load[0] == 0 sans 255, logo dans la bitmap, fin exacte
     LB_PIXELS + w*h sans remplissage) ; logo 0x0 et masque vide admis (--loading black) ; sinon
     identique au bloc logo de DTITLE.DAT ; avec --wad, == wad2title.logo_block(WAD)
  2. (plus de bloc 2 : la feuille VDP2 de zeros n'est plus ecrite, CFG_VDP2_SHEET vide pour Doom)
  3. loadStaticSounds / loadSoundSet SOUND.C:231-241 : `int 8`, 8 shorts ([3] == 6), `int 20`, puis
     20 x (int size, rate 0x7000, bps 8, loop -1) + PCM, size pair, soundTop < 512 Ko (SOUND.C:218-219)
  4. loadWeaponTiles PIC.C:709 = loadTileSet : `int n`, n x (flags 0x6A, palNm, size > 0, RLE) ; chaque
     RLE decode exactement 4 096 pixels en consommant `size` octets (PIC.C:299-315) ; n == --tiles (96 : l'ombre + 95)
  5. loadWeaponSequences SEQUENCE.C:92-132 : `int size` == 12 + 8 f + 8 c + 2 s, 90 sequences,
     wSequence[0] == 0, croissante, terminale == nmFrames, frame terminale.chunkIndex == nmChunks,
     pads 0, sound -1, flags 0, chunk.tile < n ; fin de fichier exacte
  + (--ids, --wad) : pour chaque etat 1..89 dont le lump `sprite+lettre+0` est dans le WAD ET dans les
     familles decoupees, wseq(state) non vide ; les autres vides.
  + DTITLE.DAT a cote (ecran titre, wad2title.py ; --no-title pour s'en passer) : magies, bloc logo
     (37 niveaux, rampe PSX exacte, P_title identite, P_load[0] == 0 sans 255, logo dans la bitmap),
     polices relues comme initFonts (hauteurs 8 et 16, espace de 4 et 6), cranes 24x19 x 2, fin exacte ;
     avec --wad, chaque table recalculee depuis le WAD doit etre identique ; les lignes les plus
     larges des sous-ecrans du titre tiennent dans la grande police (TITLE_LINES) ; les #define LB_*,
     FIRE_MAX, FIRE_SLOTS, FIRE_W et MASK_H de game/doom/DOOM_TITLE.C valent ceux de wad2title ;
     le bloc 1 de STATIC.DAT == le bloc logo de DTITLE.DAT.

Usage : python tools\\doom2ps\\verif_static.py [STATIC.DAT] [--tiles 96] [--ids ...] [--wad ...]
Sortie 0 = tout vert ; sinon la premiere assertion qui tombe.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import rle8                                            # noqa: E402
import wad2font                                        # noqa: E402
import wad2static                                      # noqa: E402
import wad2title                                       # noqa: E402

DEFAULT_DAT = wad2static.DEFAULT_OUT


class Reader:
    def __init__(self, data):
        self.b, self.p = data, 0

    def take(self, n):
        assert self.p + n <= len(self.b), "fichier tronque a %d (+%d > %d)" % (self.p, n, len(self.b))
        d = self.b[self.p:self.p + n]
        self.p += n
        return d

    def i32(self):
        return struct.unpack(">i", self.take(4))[0]

    def i16(self):
        return struct.unpack(">h", self.take(2))[0]


def static_summary(data):
    """Relit les blocs 1-4 sans les verifier a fond : -> dict(tileBase, static_pcm, static_sounds,
    weapon_tiles_bytes, wseq_bytes). `tileBase` = `int n` du bloc 4 (loadWeaponTiles retourne n,
    SRUINS.C:1930) : c'est lui que verif_doom.py soustrait de 255 (LEVEL.C:69-72)."""
    r = Reader(data)
    head = r.take(wad2title.LOGO_BLOCK_HEAD)
    lo = title_offsets()["LOGO"]
    w, h = struct.unpack(">hh", head[lo:lo + 4])
    r.take(w * h)
    ng = r.i32()
    r.take(2 * ng)
    ns = r.i32()
    pcm = 0
    for _ in range(ns):
        size = struct.unpack(">iiii", r.take(16))[0]
        r.take(size)
        pcm += size
    n = r.i32()
    wb = 0                                   # residents : RLE par tuile, aligne 4 (PIC.C:575, UTIL.C:370)
    for _ in range(n):
        r.take(4)
        sz = r.i16()
        r.take(sz)
        wb += (sz + 3) & ~3
    size = r.i32()                           # bloc wseq : mem_malloc(0, size) SEQUENCE.C:100
    r.take(size)
    assert r.p == len(data)
    return dict(tileBase=n, static_pcm=pcm, static_sounds=ns, weapon_tiles_bytes=wb,
                wseq_bytes=(size + 3) & ~3)


def verify(data, tiles_expected=96, ids=None, wad=None, families=None):
    r = Reader(data)
    rep = {}
    # 1 : le bloc logo, sans cadre (doom_loadingScreen lit LB_PIXELS puis w*h octets)
    blk = read_logo_block(r, empty_ok=True)
    rep["logo_block"] = data[:r.p]
    rep["screen"] = dict(logo=blk["logo"], mask_bits=blk["mask_bits"])
    if wad is not None:
        black = blk["logo"][2] == 0
        assert rep["logo_block"] == wad2title.logo_block(wad, "black" if black else "TITLEPIC"), \
            "bloc 1 != wad2title.logo_block(WAD)"
    # (pas de bloc 2)
    # 3
    ng = r.i32()
    assert ng == 8, "ST_NMSTATICSOUNDGROUPS %d != 8" % ng
    smap = [r.i16() for _ in range(8)]
    assert smap[3] == 6 and smap == [0, 0, 0, 6, 0, 0, 0, 0], smap
    ns = r.i32()
    assert ns == 20, "%d sons statiques != 20" % ns
    top = 0
    for i in range(ns):
        size, rate, bps, loop = struct.unpack(">iiii", r.take(16))
        assert size > 0 and not (size & 1), "son %d size %d" % (i, size)
        assert rate == 0x7000 and bps == 8 and loop == -1, (i, rate, bps, loop)
        r.take(size)
        assert top + size < 512 * 1024
        top += size
    rep["sounds"] = dict(n=ns, pcm=top)
    # 4
    n = r.i32()
    assert n == tiles_expected, "tuiles d'armes n = %d != %d" % (n, tiles_expected)
    rle_total, rle_max = 0, 0
    for i in range(n):
        flags, pal, size = r.i16(), r.i16(), r.i16()
        assert flags == 0x6A and pal == 0 and size > 0, (i, flags, pal, size)
        rle = r.take(size)
        pix, used = rle8.unrle8(rle)
        assert len(pix) == 4096 and used == size, "tuile %d : RLE decode %d px avec %d/%d o" % (i, len(pix), used, size)
        rle_total += size
        rle_max = max(rle_max, size)
    rep["tiles"] = dict(n=n, tileBase=n, rle=rle_total, rle_max=rle_max)
    # 5
    size = r.i32()
    assert 0 < size < 1024 * 1024
    nseq, nfr, nch = struct.unpack(">iii", r.take(12))
    assert size == 12 + 8 * nfr + 8 * nch + 2 * nseq, "size %d != formule (%d, %d, %d)" % (size, nseq, nfr, nch)
    assert nseq == wad2static.WSEQ_LAST - wad2static.WSEQ_FIRST + 2, "nmSequences %d != 90" % nseq
    frames = [struct.unpack(">hhhbb", r.take(8)) for _ in range(nfr)]
    chunks = [struct.unpack(">hhhbb", r.take(8)) for _ in range(nch)]
    seqs = [r.i16() for _ in range(nseq)]
    assert seqs[0] == 0, "wSequence[0] != 0"
    assert all(a <= b for a, b in zip(seqs, seqs[1:])), "wSequence non croissante"
    assert seqs[-1] == nfr - 1, "terminale %d != nmFrames-1 %d" % (seqs[-1], nfr - 1)
    assert frames[-1][0] == nch, "frame terminale.chunkIndex %d != nmChunks %d" % (frames[-1][0], nch)
    for i, (ci, fl, so, p0, p1) in enumerate(frames):
        assert fl == 0 and so == -1 and p0 == 0 and p1 == 0, ("frame", i, fl, so, p0, p1)
        assert 0 <= ci <= nch
    assert all(a[0] <= b[0] for a, b in zip(frames, frames[1:]))
    for i, (cx, cy, t, fl, pad) in enumerate(chunks):
        assert 0 <= t < n and fl == 0 and pad == 0, ("chunk", i, t, fl, pad)
    assert r.p == len(data), "%d octets en trop apres les sequences d'armes" % (len(data) - r.p)
    rep["wseq"] = dict(sequences=nseq - 1, frames=nfr - 1, chunks=nch, block=size,
                       populated=[s for s in range(nseq - 1) if seqs[s] < seqs[s + 1]])
    # croisement avec le WAD / les etats
    if ids is not None and wad is not None:
        cut = set(wad2static.weapon_lumps(wad, families or wad2static.WEAPON_FAMILIES))
        bad = []
        for s in range(wad2static.WSEQ_FIRST, wad2static.WSEQ_LAST + 1):
            k = wad2static.wseq(s)
            expect = wad2static.psprite_lump(ids, s) in cut
            if (seqs[k] < seqs[k + 1]) != expect:
                bad.append((ids["state_names"][s], expect))
        assert not bad, "wseq incoherentes avec le WAD : %s" % bad
        rep["wseq"]["states_checked"] = wad2static.WSEQ_LAST
    rep["blocks"] = [len(rep["logo_block"]), 4 + 16 + 4 + 20 * 16 + top,
                     4 + 6 * n + rle_total, 4 + size]
    rep["total"] = len(data)
    return rep


def title_font(r):
    """Une police de DTITLE.DAT : `i32 n` puis n octets (police + remplissage a 4) -> parse_font."""
    n = r.i32()
    data = r.take(n)
    height = struct.unpack(">h", data[:2])[0]
    widths = list(data[34:290])
    size = 290 + sum(height * ((w + 1) >> 1) for w in widths if w)
    assert n == (size + 3) & ~3 and not any(data[size:]), "police : %d o pour %d utiles" % (n, size)
    return wad2font.parse_font(data[:size])


# Les lignes les plus larges que les sous-ecrans du titre ecrivent en police 2 (la grande), centrees
# sur 320 px : MPRULES.C mpMenuLine (MODE : MPRULES.C mpModeName + SPRITE.H CFG_MP_MONSTERS_NAME ;
# SKILL : DOOM_MODES.C ; BOSS : DOOM_GAME.C doom_bossName, 2 barons en E1M8), DOOM_LIGHTS.C
# lightLine (lightFxName).
TITLE_LINES = (["MODE  " + m for m in ("COOPERATIVE", "DEATHMATCH", "TEAM DEATHMATCH", "DEMONS",
                                       "BOSS BATTLE", "HORDE")]
               + ["SKILL  " + k for k in ("TOO YOUNG TO DIE", "NOT TOO ROUGH", "HURT ME PLENTY",
                                          "ULTRA-VIOLENCE")]
               + ["BOSS  BARON OF HELL X2", "PLAYER 4  GREEN TEAM", "PLAYER 4  MARINE", "MAP  E1M9",
                  "FRAG LIMIT  NONE", "TIME LIMIT  20 MIN"]
               + ["EFFECT  " + e for e in ("ALL", "IMP FIREBALL", "CACODEMON SHOT", "BARON SHOT", "ROCKET",
                                           "PLASMA BOLT", "EXPLOSION", "MY MUZZLE FLASH", "MONSTER MUZZLE")])
# REMAP CONTROLS (INTRO.C remapMenu) : "%s- %s" (bouton, action) sur deux colonnes, x = -150 et 10 ;
# la droite finit au bord (160) -> 150 px au plus. Boutons INTRO.C buttonNames, actions anglaises de
# INITLOAD.DAT (LB_ACTIONNAMES), n'importe quel bouton sur n'importe quelle action.
REMAP_LINES = ["%s- %s" % (b, act) for b in ("A", "B", "C", "X", "Y", "Z", "TL", "TR")
               for act in ("FIRE", "JUMP", "PUSH", "PITCH", "WEP DN", "WEP UP", "STRAFE-L", "STRAFE-R")]
SCREEN_W, REMAP_COLUMN = 320, 150
TITLE_C = os.path.join(ROOT, "game", "doom", "DOOM_TITLE.C")


def string_width(widths, text):
    """drawString (PRINT.C:128-147) : largeur + 1 par code present, un code absent n'avance pas."""
    return sum(widths[ord(c)] + 1 for c in text if widths[ord(c)])


def title_offsets():
    """Les offsets du bloc logo selon wad2title (sa docstring, dans l'ordre d'ecriture de logo_block)."""
    t = wad2title
    lb = dict(LEVELS=4, PLAYPAL=8)
    lb["RAMP"] = lb["PLAYPAL"] + 512
    lb["PTITLE"] = lb["RAMP"] + 2 * t.RAMP_SLOTS
    lb["PLOAD"] = lb["PTITLE"] + t.RAMP_SLOTS
    lb["R"] = lb["PLOAD"] + t.RAMP_SLOTS
    lb["U"] = lb["R"] + 256
    lb["LOGO"] = lb["U"] + 256
    lb["MASK"] = lb["LOGO"] + 8
    lb["MASKG"] = lb["MASK"] + t.MASK_ROWS * t.MASK_BYTES
    lb["PCTX"] = lb["MASKG"] + t.MASK_ROWS * t.MASK_BYTES
    lb["DIGITS"] = lb["PCTX"] + 4
    lb["PIXELS"] = lb["DIGITS"] + len(t.PCT_GLYPHS) * 2 * t.MASK_ROWS * t.DIGIT_BYTES
    assert lb["PIXELS"] == t.LOGO_BLOCK_HEAD, lb
    return lb


def verify_title_c(path=TITLE_C):
    """Les #define de DOOM_TITLE.C == les offsets de wad2title (un changement d'un seul cote casserait
    le jeu en silence : le C ne relit que la magie, et seulement en debug)."""
    import re
    src = open(path, encoding="latin-1").read()
    defs = {m.group(1): int(m.group(2)) for m in re.finditer(
        r"^#define\s+(LB_\w+|FIRE_MAX|FIRE_SLOTS|FIRE_W|MASK_H)\s+(\d+)\b", src, re.M)}
    want = {"LB_" + k: v for k, v in title_offsets().items()}
    want.update(FIRE_MAX=wad2title.FIRE_LEVELS - 1, FIRE_SLOTS=wad2title.RAMP_SLOTS, FIRE_W=wad2title.FIRE_W,
                MASK_H=wad2title.MASK_ROWS)
    for k, v in want.items():
        assert defs.get(k) == v, "DOOM_TITLE.C %s = %s, wad2title = %d" % (k, defs.get(k), v)
    extra = set(k for k in defs if k.startswith("LB_")) - set(want)
    assert not extra, "DOOM_TITLE.C : LB_* inconnus de verif_static %s" % sorted(extra)
    return len(want)


def read_logo_block(b, empty_ok=False):
    """Le bloc logo relu dans `b` (Reader) comme DOOM_TITLE.C le lit : LB_PIXELS octets, puis w*h de
    logo (w multiple de 4 : pas de remplissage). `empty_ok` : logo 0x0 et masque vide admis
    (STATIC.DAT --loading black). -> dict des tables."""
    t = wad2title
    assert b.take(4) == t.BLOCK_MAGIC, "bloc logo : magie"
    nlev, zero = b.i16(), b.i16()
    assert nlev == t.FIRE_LEVELS == 37 and zero == 0, (nlev, zero)
    pal = struct.unpack(">256H", b.take(512))
    assert all(c & 0x8000 for c in pal), "PLAYPAL sans bit 15"
    ramp = list(struct.unpack(">%dH" % t.RAMP_SLOTS, b.take(2 * t.RAMP_SLOTS)))
    assert ramp == t.fire_cram(), "rampe PSX != bgr555(FIRE_RGBS)"
    assert all(c & 0x8000 for c in ramp[:nlev]) and not any(ramp[nlev:])
    p_title = list(b.take(t.RAMP_SLOTS))
    p_load = list(b.take(t.RAMP_SLOTS))
    assert p_title == list(range(nlev)) + [0] * (t.RAMP_SLOTS - nlev), "P_title n'est pas l'identite"
    assert p_load[0] == 0 and 255 not in p_load[:nlev] and not any(p_load[nlev:]), p_load
    rr, uu = b.take(256), b.take(256)
    assert len(set(rr)) > 64 and len(set(uu)) > 64 and rr != uu, "R/U pas aleatoires"
    lw, lh, lx, ly = b.i16(), b.i16(), b.i16(), b.i16()
    mask = b.take(t.MASK_ROWS * t.MASK_BYTES)
    maskg = b.take(t.MASK_ROWS * t.MASK_BYTES)
    pctx, zero = b.i16(), b.i16()
    digits = b.take(len(t.PCT_GLYPHS) * 2 * t.MASK_ROWS * t.DIGIT_BYTES)
    assert zero == 0 and 0 <= pctx <= t.FIRE_W - t.PCT_CELLS * t.DIGIT_W and not (pctx & 7), pctx
    if empty_ok and lw == 0:
        assert (lh, lx, ly) == (0, 0, 0) and not any(mask) and not any(maskg), \
            "bloc logo vide : %s" % ((lh, lx, ly),)
    else:
        assert lw > 0 and lh > 0 and not (lw & 3) and not (lx & 3), (lw, lx)
        assert lx + lw <= t.BITMAP_W and ly + lh <= t.SCREEN_H // 2, \
            "logo hors bitmap (%d,%d %dx%d)" % (lx, ly, lw, lh)
        assert any(mask), "masque LOADING vide"
        # le CORPS de la lettre est strictement inclus dans le GLYPHE, et strictement plus petit :
        # c'est ce qui donne a "LOADING" son contour noir et le trou de ses O (DOOM_TITLE.C)
        assert all((a & ~b_) == 0 for a, b_ in zip(mask, maskg)), "corps hors du glyphe"
        assert sum(bin(v).count("1") for v in mask) < \
            sum(bin(v).count("1") for v in maskg), "corps == glyphe : lettres pleines"
        # les douze glyphes du pourcentage : l'espace vide, les onze autres pleins, corps inclus
        gl = t.MASK_ROWS * t.DIGIT_BYTES
        for k, ch in enumerate(t.PCT_GLYPHS):
            corps = digits[k * 2 * gl:k * 2 * gl + gl]
            gros = digits[k * 2 * gl + gl:(k + 1) * 2 * gl]
            if ch == " ":
                assert not any(corps) and not any(gros), "l'espace du pourcentage n'est pas vide"
            else:
                assert any(corps) and all((a & ~b_) == 0 for a, b_ in zip(corps, gros)), ch
    pixels = b.take(lw * lh)
    assert lw == 0 or any(pixels)
    assert b.p % 4 == 0 and b.p == t.LOGO_BLOCK_HEAD + lw * lh, "bloc logo : fin %d" % b.p
    return dict(levels=nlev, pal=pal, p_load=p_load, logo=(lx, ly, lw, lh), mask=mask,
                pctx=pctx, digits=digits,
                maskg=maskg, pixels=pixels,
                mask_bits=sum(bin(v).count("1") for v in mask),
                maskg_bits=sum(bin(v).count("1") for v in maskg))


def verify_title(data, wad=None):
    """DTITLE.DAT (wad2title.py) relu comme DOOM_TITLE.C le lit -> rapport."""
    t = wad2title
    r = Reader(data)
    assert r.take(4) == t.TITLE_MAGIC, "DTITLE.DAT : magie"
    bsize = r.i32()
    b = Reader(r.take(bsize))
    blk = read_logo_block(b)
    assert b.p == bsize, "bloc logo : %d octets en trop" % (bsize - b.p)
    nlev, pal, p_load, mask, pixels = blk["levels"], blk["pal"], blk["p_load"], blk["mask"], blk["pixels"]
    lx, ly, lw, lh = blk["logo"]
    small = title_font(r)
    big = title_font(r)
    for (h, _clut, widths, glyphs), hh, sp in ((small, 8, t.SMALL_SPACE), (big, 16, t.BIG_SPACE)):
        assert h == hh and widths[32] == sp and not any(q for row in glyphs[32] for q in row), (h, widths[32])
        assert all(widths[ord(c)] for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"), "glyphes manquants"
    bw = big[2]
    widest = max(TITLE_LINES, key=lambda x: string_width(bw, x))
    assert string_width(bw, widest) <= SCREEN_W, "grande police : %r = %d px > %d" % (
        widest, string_width(bw, widest), SCREEN_W)
    remap = max(REMAP_LINES, key=lambda x: string_width(bw, x))
    assert string_width(bw, remap) <= REMAP_COLUMN, "grande police : REMAP %r = %d px > %d" % (
        remap, string_width(bw, remap), REMAP_COLUMN)
    sw, sh = r.i32(), r.i32()
    assert (sw, sh) == (t.SKULL_W, t.SKULL_H), (sw, sh)
    sk = [r.take(2 * sw * sh), r.take(2 * sw * sh)]
    for s in sk:
        words = struct.unpack(">%dH" % (sw * sh), s)
        assert all(w == 0 or (w & 0x8000) for w in words) and any(words), "crane : mot sans bit 15"
    assert r.p == len(data), "DTITLE.DAT : %d octets en trop" % (len(data) - r.p)
    if wad is not None:
        pp = wad.playpal(0)
        from doomtiles import bgr555
        assert list(pal) == [bgr555(c) for c in pp], "PLAYPAL du bloc != WAD"
        assert p_load[:nlev] == t.fire_palette(pp), "P_load != boucle de m_fire.c"
        x, y, w, h, px = t.logo(wad)
        assert (x, y, w, h) == (lx, ly, lw, lh) and px == pixels, "logo != M_DOOM du WAD"
        mw, mh, _pix, msk = wad.patch("M_DOOM")
        x0 = (t.BITMAP_W - mw) // 2 - lx
        assert all(pixels[j * lw + x0 + i] for j in range(mh) for i in range(mw) if msk[j * mw + i]), \
            "logo : pixel opaque devenu transparent"
        assert sk == t.skulls(wad), "cranes != M_SKULL1/2 du WAD"
        assert data == t.title_file(wad)[0], "DTITLE.DAT != wad2title.title_file(WAD)"
    return dict(total=len(data), block=bsize, block_bytes=data[8:8 + bsize], levels=nlev,
                p_load=p_load[:nlev], logo=(lx, ly, lw, lh),
                fonts=(small[0], big[0]), glyphs=(len(small[3]), len(big[3])), skulls=(sw, sh),
                mask_bits=sum(bin(v).count("1") for v in mask), wad=wad is not None,
                widest=(widest, string_width(bw, widest)), remap=(remap, string_width(bw, remap)),
                c_defines=verify_title_c())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dat", nargs="?", default=DEFAULT_DAT)
    ap.add_argument("--tiles", type=int, default=96, help="n attendu (96 = ombre + 95 ; 49 avec --e1m1-weapons)")
    ap.add_argument("--ids", default=wad2static.DEFAULT_IDS)
    ap.add_argument("--wad", default=wad2static.DEFAULT_WAD)
    ap.add_argument("--e1m1-weapons", action="store_true")
    ap.add_argument("--no-cross", action="store_true", help="sans croisement WAD/etats")
    ap.add_argument("--no-title", action="store_true", help="sans DTITLE.DAT (ecran titre)")
    a = ap.parse_args(argv)
    data = open(a.dat, "rb").read()
    ids = wad = None
    if not a.no_cross:
        import wad as wadmod
        ids = json.load(open(a.ids))
        wad = wadmod.Wad(a.wad)
    fams = wad2static.E1M1_FAMILIES if a.e1m1_weapons else wad2static.WEAPON_FAMILIES
    rep = verify(data, 49 if a.e1m1_weapons else a.tiles, ids, wad, fams)
    assert sum(rep["blocks"]) == rep["total"]
    print("%s : %d o, blocs %s : OK" % (a.dat, rep["total"], rep["blocks"]))
    print("bloc logo %d o %s ; sons %s ; tuiles %s" % (len(rep["logo_block"]), rep["screen"], rep["sounds"],
                                                       rep["tiles"]))
    w = rep["wseq"]
    print("wseq : %d sequences, %d frames, %d chunks, bloc %d o, %d peuplees%s"
          % (w["sequences"], w["frames"], w["chunks"], w["block"], len(w["populated"]),
             ", croisement %d etats OK" % w["states_checked"] if "states_checked" in w else ""))
    if not a.no_title:
        tpath = os.path.join(os.path.dirname(os.path.abspath(a.dat)), "DTITLE.DAT")
        assert os.path.exists(tpath), "DTITLE.DAT absent a cote de %s" % a.dat
        t = verify_title(open(tpath, "rb").read(), wad)
        sb = rep["logo_block"]
        if rep["screen"]["logo"][2]:
            assert sb == t["block_bytes"], "STATIC.DAT bloc 1 != bloc logo de DTITLE.DAT"
        else:                                  # --loading black : memes tables, sans logo ni masque
            o = title_offsets()
            assert sb[:o["LOGO"]] == t["block_bytes"][:o["LOGO"]], "STATIC.DAT bloc 1 (noir) : tables != DTITLE.DAT"
        print("STATIC.DAT bloc 1 = bloc logo de DTITLE.DAT%s : OK"
              % ("" if rep["screen"]["logo"][2] else " (tables ; --loading black : sans logo ni masque)"))
        print("DTITLE.DAT : %d o, bloc logo %d o : %d niveaux de feu (rampe PSX exacte, P_title identite), "
              "P_load %s ; logo %dx%d en (%d,%d) ; polices h %d/%d avec espace (%d/%d glyphes) ; "
              "cranes %dx%d x 2 ; masque LOADING %d bits%s ; ligne la plus large %r %d px, REMAP %r %d px ; "
              "%d #define de DOOM_TITLE.C : OK"
              % (t["total"], t["block"], t["levels"], t["p_load"], t["logo"][2], t["logo"][3],
                 t["logo"][0], t["logo"][1], t["fonts"][0], t["fonts"][1], t["glyphs"][0], t["glyphs"][1],
                 t["skulls"][0], t["skulls"][1], t["mask_bits"],
                 ", identique au WAD" if t["wad"] else "", t["widest"][0], t["widest"][1],
                 t["remap"][0], t["remap"][1], t["c_defines"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
