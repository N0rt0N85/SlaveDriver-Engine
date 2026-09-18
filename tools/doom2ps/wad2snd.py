#!/usr/bin/env python3
"""wad2snd.py -- sons DS* de Doom (DMX) -> SoundRec SlaveDriver (contrat DOOM_ABI section 3).

Format d'un son sur disque (SOUND.C:203-228) : `int size, int rate, int bps, int loopStart` + PCM,
copie a `soundTop` pair (`assert !(soundTop & 1)`, `soundTop + size < 512 Ko`). DS* Doom = DMX 8 bits
11 025 Hz : en-tete `u16 3, u16 rate, u32 len` ; `len` inclut 16 octets de garde de chaque cote du
PCM (Mimas src/i_sound_saturn.cxx:254-259 : `length -= 32; src = lump + 24`) :

  size      = len - 32, arrondi PAIR (un octet 0x80 = silence ajoute avant le ^0x80)
  PCM       = lump[24 : 24 + len - 32] ^ 0x80 (signe, STATIC.C:630-631)
  rate      = registre SCSP tel quel (SOUND.C:212, 290) : `scsp_pitch(11025) = 0x7000`
              (octave -2, fns 0 ; UTIL/MAKESND.C:32-40, STATIC.C:604-613)
  bps       = 8, loopStart = -1 (STATIC.C:614-615)

Statiques (STATIC.DAT bloc 3, `loadStaticSounds` SOUND.C:231-252) : `int 8`, 8 shorts
`[0, 0, 0, 6, 0, 0, 0, 0]` (ST_JOHN = 0 -> index 0 ; ST_PUSHBLOCK = 3 -> index 6 = DOROPN ; les groupes
inutilises valent 0, jamais -2 : `playStaticSound` indexe sans test, SOUND.H:38-39), `int 20`, puis les
20 sons dans l'ordre du contrat section 3 (`STATIC_SOUNDS`, = `doomStaticSfx[]` de DOOM_TABLES.C).

Dynamiques (.LEV) : `level_objectSoundMap[227]` indexee par `sfxenum_t` (0..108) : valeur = index dans
la liste du .LEV, **-1000** si absent (reste < 0 apres `+= nmStaticSounds`, SOUND.C:247-248). Liste
blanche par niveau = union des 5 champs son des MT presents/spawnables + sons emis par leurs verbes
A_* (variantes posit1-3 / bgsit1-2 d'A_Look, podth1-3 / bgdth1-2 d'A_Scream, claw d'A_TroopAttack...),
moins les statiques, moins les lumps absents du WAD. Plafonds : MAXNMSOUNDS 80 toutes sources
(SOUND.C:40), `soundTop` < 524 288.

API :
  load_dmx(lump) -> dict(pcm, rate, bps, loopStart, dmx_rate, dmx_len)      (= sound_rec)
  sound_rec(wad, name) -> dict           name = nom sfx sans DS ("pistol") ou lump ("DSPISTOL")
  scsp_pitch(rate_hz) -> int
  static_sounds(wad) -> list[dict]       20 sons, ordre STATIC_SOUNDS
  static_sound_block(wad) -> bytes       bloc 3 de STATIC.DAT (177 314 o sur DOOM1.WAD)
  dynamic_sounds(wad, ids, mobj_types) -> (map227, sounds, names)
  object_sound_map(ids, dyn) -> list[int]   227 shorts
  sound_names_for(ids, mobj_types) -> list[str]   liste blanche (hors statiques), ordre sfxenum_t

Usage : python tools\\doom2ps\\wad2snd.py [--wad W] [--ids build/doom/doom_ids.json] [--map E1M1]
        [--skill 3]   (test : tailles, relecture, plafonds ; n'ecrit rien)
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import wad as wadmod                                   # noqa: E402

DEFAULT_WAD = os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data", "DOOM1.WAD")
DEFAULT_IDS = os.path.join(ROOT, "build", "doom", "doom_ids.json")

OT_NMTYPES = 227
MAXNMSOUNDS = 80                  # SOUND.C:40
SOUND_RAM = 512 * 1024            # SOUND.C:218
ST_NMSTATICSOUNDGROUPS = 8        # SOUND.H:18-20
ST_JOHN, ST_PUSHBLOCK = 0, 3
PUSHBLOCK_INDEX = 6               # DOROPN : +0 ouverture, +1 DORCLS, +2 PSTART (AI.C:4336-4673)
ABSENT = -1000                    # contrat section 3
DMX_GUARD = 16                    # octets de garde de chaque cote du PCM
SOUNDHDR_FMT = ">iiii"            # SOUND.C:203-206 : size, rate, bps, loopStart

# contrat section 3, ordre STATIC.DAT (= doomStaticSfx[] de DOOM_TABLES.C, = ids["static_sfx"])
STATIC_SOUNDS = ["pistol", "shotgn", "punch", "swtchn", "swtchx", "noway", "doropn", "dorcls",
                 "pstart", "pstop", "plpain", "pldeth", "oof", "itemup", "wpnup", "slop", "barexp",
                 "telept", "stnmov", "rlaunc"]

# sons emis par les verbes A_* en plus des 5 champs de mobjinfo (Mimas/core/p_enemy.c ; SPEC_RUNTIME
# section 3) : verbe -> noms sfx. A_Look / A_Scream tirent les variantes au hasard (:611-624, :1541-1570)
ACTION_SOUNDS = {
    "A_PosAttack": ["pistol"], "A_SPosAttack": ["shotgn"], "A_CPosAttack": ["shotgn"],
    "A_TroopAttack": ["claw"], "A_BruisAttack": ["claw"], "A_XScream": ["slop"],
    "A_SkelWhoosh": ["skeswg"], "A_SkelFist": ["skepch"], "A_VileChase": ["slop"],
    "A_VileStart": ["vilatk"], "A_StartFire": ["flamst"], "A_FireCrackle": ["flame"],
    "A_Explode": [], "A_FatRaise": ["manatk"], "A_CyberAttack": ["rlaunc"],
    "A_Hoof": ["hoof"], "A_Metal": ["metal"], "A_BabyMetal": ["bspwlk"],
    "A_BrainAwake": ["bossit"], "A_BrainPain": ["bospn"], "A_BrainScream": ["bosdth"],
    "A_BrainSpit": ["bospit"], "A_SpawnFly": ["telept"], "A_SpawnSound": ["boscub"],
    "A_BFGSpray": [], "A_PainAttack": [], "A_SkullAttack": [],
}
# variantes tirees au hasard a partir du son de base (A_Look :611-624 ; A_Scream :1548-1560)
VARIANTS = {"posit1": ["posit1", "posit2", "posit3"], "posit2": ["posit1", "posit2", "posit3"],
            "posit3": ["posit1", "posit2", "posit3"], "bgsit1": ["bgsit1", "bgsit2"],
            "bgsit2": ["bgsit1", "bgsit2"], "podth1": ["podth1", "podth2", "podth3"],
            "podth2": ["podth1", "podth2", "podth3"], "podth3": ["podth1", "podth2", "podth3"],
            "bgdth1": ["bgdth1", "bgdth2"], "bgdth2": ["bgdth1", "bgdth2"]}
MOBJ_SOUND_FIELDS = ("seesound", "attacksound", "painsound", "deathsound", "activesound")
MOBJ_STATE_FIELDS = ("spawnstate", "seestate", "painstate", "meleestate", "missilestate",
                     "deathstate", "xdeathstate", "raisestate")


# ----------------------------------------------------------------------------- un son
def scsp_pitch(rate_hz):
    """UTIL/MAKESND.C:32-40 : octave = floor(log2(rate/44100)), fns = frac * 1024."""
    x = math.log(rate_hz) / math.log(2.0) - math.log(44100.0) / math.log(2.0)
    octave = int(math.floor(x))
    fns = int(abs((x - math.floor(x)) * 1024.0))
    assert (fns & 0x3FF) == fns
    return ((octave << 11) & 0x7800) | (fns & 0x3FF)


def load_dmx(lump):
    """Lump DS* (DMX format 3) -> SoundRec : pcm signe (^0x80), taille paire, pitch SCSP."""
    assert len(lump) >= 8 + 2 * DMX_GUARD, "lump DMX trop court (%d o)" % len(lump)
    fmt, rate, ln = struct.unpack("<HHI", lump[:8])
    assert fmt == 3, "format DMX %d != 3" % fmt
    assert 8 + ln <= len(lump), "len DMX %d deborde du lump (%d o)" % (ln, len(lump))
    size = ln - 2 * DMX_GUARD
    assert size > 0
    raw = bytearray(lump[8 + DMX_GUARD: 8 + DMX_GUARD + size])
    if size & 1:                                         # silence non signe = 0x80 -> 0 apres ^0x80
        raw.append(0x80)
        size += 1
    pcm = bytes(b ^ 0x80 for b in raw)
    assert len(pcm) == size and not (size & 1)
    return dict(pcm=pcm, rate=scsp_pitch(rate), bps=8, loopStart=-1, dmx_rate=rate, dmx_len=ln)


def lump_name(name):
    name = name.upper()
    return name if name.startswith("DS") else "DS" + name


def sound_rec(wad, name):
    return load_dmx(wad.lump(lump_name(name)))


def pack_sound(rec):
    """SOUND.C:203-207 : 4 ints puis le PCM."""
    assert not (len(rec["pcm"]) & 1)
    return struct.pack(SOUNDHDR_FMT, len(rec["pcm"]), rec["rate"], rec["bps"],
                       rec["loopStart"]) + rec["pcm"]


# ----------------------------------------------------------------------------- statiques
def static_sounds(wad):
    """Les 20 sons statiques (ordre STATIC_SOUNDS), tous obligatoires."""
    return [sound_rec(wad, n) for n in STATIC_SOUNDS]


def static_sound_map():
    m = [0] * ST_NMSTATICSOUNDGROUPS
    m[ST_JOHN] = 0
    m[ST_PUSHBLOCK] = PUSHBLOCK_INDEX
    return m


def static_sound_block(wad):
    """Bloc 3 de STATIC.DAT : int 8, 8 shorts, int 20, 20 x (16 o + PCM) -- loadSoundSet SOUND.C:231."""
    snds = static_sounds(wad)
    smap = static_sound_map()
    assert STATIC_SOUNDS[PUSHBLOCK_INDEX] == "doropn"
    out = [struct.pack(">i", ST_NMSTATICSOUNDGROUPS), struct.pack(">%dh" % ST_NMSTATICSOUNDGROUPS, *smap),
           struct.pack(">i", len(snds))]
    out += [pack_sound(s) for s in snds]
    return b"".join(out)


# ----------------------------------------------------------------------------- dynamiques
def reachable_states(ids, mt):
    """Etats atteints depuis les 8 champs d'etat de `mt` en suivant nextstate (contrat section 2)."""
    states = ids["states"]
    seen = set()
    stack = [ids["mobjinfo"][mt][f] for f in MOBJ_STATE_FIELDS]
    while stack:
        s = stack.pop()
        if s <= 0 or s in seen:
            continue
        seen.add(s)
        stack.append(states[s]["nextstate"])
    return seen


def sound_names_for(ids, mobj_types):
    """Liste blanche (hors statiques), triee par sfxenum_t : 5 champs son + verbes + variantes."""
    sfx_names = ids["sfx_names"]
    states = ids["states"]
    wanted = set()
    for mt in sorted(set(mobj_types)):
        info = ids["mobjinfo"][mt]
        for f in MOBJ_SOUND_FIELDS:
            if info[f] > 0:
                wanted.add(sfx_names[info[f]])
        for s in reachable_states(ids, mt):
            for n in ACTION_SOUNDS.get(states[s]["action"], ()):
                wanted.add(n)
    for n in list(wanted):
        wanted.update(VARIANTS.get(n, ()))
    wanted -= set(STATIC_SOUNDS)
    wanted.discard("none")
    num = ids["sfxname_to_num"]
    return sorted(wanted, key=lambda n: num[n])


def object_sound_map(ids, dyn):
    """227 shorts indexes par sfxenum_t : index dans `dyn`, -1000 sinon (contrat section 3)."""
    m = [ABSENT] * OT_NMTYPES
    num = ids["sfxname_to_num"]
    for i, n in enumerate(dyn):
        m[num[n]] = i
    return m


def dynamic_sounds(wad, ids, mobj_types):
    """-> (map227, sounds, names) : liste blanche des MT donnes, lumps absents du WAD ignores."""
    names = [n for n in sound_names_for(ids, mobj_types) if wad.has(lump_name(n))]
    sounds = [sound_rec(wad, n) for n in names]
    assert len(sounds) + len(STATIC_SOUNDS) < MAXNMSOUNDS, "MAXNMSOUNDS 80 (SOUND.C:40)"
    return object_sound_map(ids, names), sounds, names


def sound_model(wad, ids, mobj_types):
    """Bloc `sounds` du modele lev_io (map + sounds) pret pour lev_write.serialize_sounds."""
    smap, sounds, names = dynamic_sounds(wad, ids, mobj_types)
    return dict(map=smap, sounds=sounds), names


# ----------------------------------------------------------------------------- outils de test
# L'arsenal du JOUEUR.  Ses projectiles ne se deduisent pas de la carte -- aucune chose posee ne
# les fait naitre, contrairement a ceux des monstres (SPAWNED_BY_ACTION) -- et le joueur peut
# tirer avec une arme que le niveau ne contient pas.  Seuls ceux dont le WAD a TOUTES les images
# entrent : le shareware n'a ni plasma ni BFG (ni canon, ni bille, ni impact), et une sequence
# atteignable sans image est un defaut que verif_doom refuse a raison.
PLAYER_MISSILES = ["MT_ROCKET", "MT_PLASMA", "MT_BFG", "MT_EXTRABFG"]


def _has_all_frames(wad, ids, mt):
    """Chaque etat atteignable de `mt` a son lump dans ce WAD : nom du sprite + lettre de frame."""
    states = ids["states"]
    for s in reachable_states(ids, mt):
        pre = states[s]["spritename"] + chr(ord("A") + states[s]["frame"])
        if not any(n.startswith(pre) for n in wad.index):
            return False
    return True


def present_mobj_types(wad, ids, mapname="E1M1", skill=3):
    """MT des things du niveau au skill donne (p_mobj.c:953-958 : bit 1<<(skill-1), pas de bit 16),
    joueur compris (MT 0) AVEC son arsenal (PLAYER_MISSILES presents dans le WAD), starts coop/DM
    (2-4, 11) ignores."""
    bit = 1 << (skill - 1)
    M = wadmod.read_map(wad, mapname)
    out = set()
    for t in M["things"]:
        if t.type in (2, 3, 4, 11):
            continue
        if t.type == 1:                                  # MT_PLAYER : doomednum -1 dans info.c
            out.add(0)
            continue
        if not (t.flags & bit) or (t.flags & 16):
            continue
        out.add(ids["ed_to_mt"][str(t.type)])
    if 0 in out:
        idx = {n: i for i, n in enumerate(ids["mt_names"])}
        out |= {idx[n] for n in PLAYER_MISSILES if _has_all_frames(wad, ids, idx[n])}
    return out


# projectiles / effets spawnes par les verbes des MT presents (P_SpawnMissile, p_enemy.c) + les
# quatre effets universels : PUFF (P_SpawnPuff), BLOOD (P_SpawnBlood), TFOG, IFOG (p_mobj.c)
SPAWNED_BY_ACTION = {"A_TroopAttack": "MT_TROOPSHOT", "A_HeadAttack": "MT_HEADSHOT",
                     "A_BruisAttack": "MT_BRUISERSHOT", "A_CyberAttack": "MT_ROCKET",
                     "A_SkelMissile": "MT_TRACER", "A_FatAttack1": "MT_FATSHOT",
                     "A_FatAttack2": "MT_FATSHOT", "A_FatAttack3": "MT_FATSHOT",
                     "A_BspiAttack": "MT_ARACHPLAZ", "A_PainAttack": "MT_SKULL",
                     "A_PainDie": "MT_SKULL", "A_VileTarget": "MT_FIRE",
                     "A_BrainSpit": "MT_SPAWNSHOT"}
ALWAYS_SPAWNED = ["MT_PUFF", "MT_BLOOD", "MT_TFOG", "MT_IFOG"]


def spawnable_mobj_types(ids, mobj_types):
    """Fermeture des MT presents par les spawns de leurs verbes + effets universels."""
    names = ids["mt_names"]
    idx = {n: i for i, n in enumerate(names)}
    out = set(mobj_types)
    todo = list(out) + [idx[n] for n in ALWAYS_SPAWNED]
    states = ids["states"]
    while todo:
        mt = todo.pop()
        out.add(mt)
        for s in reachable_states(ids, mt):
            n = SPAWNED_BY_ACTION.get(states[s]["action"])
            if n and idx[n] not in out:
                out.add(idx[n])
                todo.append(idx[n])
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    ap.add_argument("--ids", default=DEFAULT_IDS)
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--skill", type=int, default=3)
    a = ap.parse_args(argv)
    w = wadmod.Wad(a.wad)
    ids = json.load(open(a.ids, encoding="utf-8"))
    assert [s["lump"] for s in ids["static_sfx"]] == STATIC_SOUNDS, "ordre statique != doom_ids.json"
    assert scsp_pitch(11025) == 0x7000
    print("scsp_pitch(11025) = 0x%04x" % scsp_pitch(11025))

    st = static_sounds(w)
    st_bytes = sum(len(s["pcm"]) for s in st)
    blk = static_sound_block(w)
    print("statiques : %d sons, %d o de PCM, bloc %d o (attendu 176 970 / 177 314)"
          % (len(st), st_bytes, len(blk)))
    assert len(blk) == 4 + 16 + 4 + 20 * 16 + st_bytes
    # relecture du bloc a la maniere de loadSoundSet
    n8, = struct.unpack(">i", blk[:4])
    smap = struct.unpack(">8h", blk[4:20])
    n20, = struct.unpack(">i", blk[20:24])
    assert n8 == 8 and smap[3] == 6 and n20 == 20, (n8, smap, n20)
    p = 24
    for i in range(n20):
        size, rate, bps, loop = struct.unpack(SOUNDHDR_FMT, blk[p:p + 16])
        assert rate == 0x7000 and bps == 8 and loop == -1 and not (size & 1)
        assert size == len(st[i]["pcm"])
        p += 16 + size
    assert p == len(blk)
    for i, (n, s) in enumerate(zip(STATIC_SOUNDS, st)):
        print("  %2d %-7s len %5d -> size %5d" % (i, n, s["dmx_len"], len(s["pcm"])))

    present = present_mobj_types(w, ids, a.map, a.skill)
    spawn = spawnable_mobj_types(ids, present)
    names = ids["mt_names"]
    print("%s skill %d : %d MT presents, %d avec spawnables (%s)"
          % (a.map, a.skill, len(present), len(spawn),
             " ".join(sorted(names[m][3:] for m in spawn - present))))
    smap, dyn, dnames = dynamic_sounds(w, ids, spawn)
    dyn_bytes = sum(len(s["pcm"]) for s in dyn)
    print("dynamiques : %d sons, %d o de PCM (attendu 16 / 168 848)" % (len(dyn), dyn_bytes))
    print("  " + " ".join("%s=%d" % (n, ids["sfxname_to_num"][n]) for n in dnames))
    assert len(smap) == OT_NMTYPES and smap.count(ABSENT) == OT_NMTYPES - len(dyn)
    for i, n in enumerate(dnames):
        assert smap[ids["sfxname_to_num"][n]] == i
    # relecture via lev_write.serialize_sounds + lev_io (meme trajet que le .LEV)
    try:
        sys.path.insert(0, os.path.join(ROOT, "tools", "duke2ps"))
        import lev_write                                 # noqa: E402
        data = lev_write.serialize_sounds(dict(map=smap, sounds=dyn))
        n, = struct.unpack(">i", data[:4])
        m2 = struct.unpack(">%dh" % n, data[4:4 + 2 * n])
        assert n == OT_NMTYPES and list(m2) == smap
        print("  serialize_sounds : %d o, carte relue identique" % len(data))
    except ImportError as e:
        print("  (lev_write non importable : %s)" % e)
    total = st_bytes + dyn_bytes
    ns = len(st) + len(dyn)
    print("total : %d sons < %d, soundTop %d o < %d : %s"
          % (ns, MAXNMSOUNDS, total, SOUND_RAM, "ok" if ns < MAXNMSOUNDS and total < SOUND_RAM
             else "DEPASSEMENT"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
