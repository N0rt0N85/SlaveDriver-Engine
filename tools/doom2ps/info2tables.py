#!/usr/bin/env python3
"""info2tables.py -- T1 doom2ps : les tables de Doom (info.c) en C gnu89 pour le moteur + doom_ids.json.

Contrat : docs/doom/DOOM_ABI.md section 4 (structures DoomState 8 o / DoomMobjInfo 44 o, numerotation
OT section 1, sons statiques section 3) et SPEC_RUNTIME section 11 T1.

Lit dans Mimas/core (GPL-2+, memes numeros que le moteur Doom) :
  info.h      enums spritenum_t (SPR_*), statenum_t (S_*), mobjtype_t (MT_*)
  info.c      sprnames[], states[NUMSTATES], mobjinfo[NUMMOBJTYPES]
  sounds.h    enum sfxenum_t (sfx_*)          sounds.c   S_sfx[] (noms de lumps sans DS)
  d_items.c   weaponinfo[NUMWEAPONS]          m_random.c rndtable[256]
  p_mobj.h    valeurs MF_*                    doomdef.h  am_*

Ecrit (atomiquement, temp + os.replace) :
  game/doom/DOOM_TABLES.H   typedef DoomState / DoomMobjInfo / DoomWeaponInfo, #define NUMSTATES...,
                            enums S_* MT_* SPR_* sfx_* am_* DOOM_ACTION_*, extern des tables
  game/doom/DOOM_ACTIONS.H  DOOM_ACTIONS_LIST(M, P) : X-macro des verbes A_* distincts dans l'ordre
                            d'apparition (M = verbe d'acteur, P = verbe d'arme) -> DOOM_VERBS.C
                            instancie doomActions[] et definit les A_* ; prototypes via la meme macro
  game/doom/DOOM_TABLES.C   doomStates[], doomMobjInfo[], doomWeaponInfo[9], doomStaticSfx[20],
                            doomOtToMt[227], doomMtToOt[137], rndtable[256] + P_Random/M_Random,
                            doomSpriteNames[], doomSfxNames[], doomActionNames[]
  build/doom/doom_ids.json  ed->MT, MT->OT, OT->MT, spritename->spritenum, sfxname->sfx, statiques,
                            states et mobjinfo bruts, armes, actions (pour le convertisseur)

Usage : python tools\\doom2ps\\info2tables.py [INFO_C [SOUNDS_C]] [--out-dir game/doom]
                                              [--json build/doom/doom_ids.json]
        (les autres fichiers sont pris dans le dossier d'INFO_C ; defaut ../Mimas/core)
"""
from __future__ import annotations

import argparse
import ast
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_CORE = os.path.join(os.path.dirname(ROOT), "Mimas", "core")

FRACUNIT = 65536
OT_NMTYPES = 227
OT_PLAYER = 13
# contrat section 1 : les MT (sauf MT_PLAYER -> 13) prennent les valeurs croissantes de ces plages
OT_RANGES = [(14, 47), (64, 90), (92, 162), (172, 226)]
# contrat section 3 : les 20 sons statiques, ordre STATIC.DAT (groupe ST_JOHN, PUSHBLOCK = index 6)
STATIC_SFX = ["pistol", "shotgn", "punch", "swtchn", "swtchx", "noway", "doropn", "dorcls",
              "pstart", "pstop", "plpain", "pldeth", "oof", "itemup", "wpnup", "slop", "barexp",
              "telept", "stnmov", "rlaunc"]
# ordre des 23 champs de mobjinfo_t (info.h)
MOBJ_FIELDS = ["doomednum", "spawnstate", "spawnhealth", "seestate", "seesound", "reactiontime",
               "attacksound", "painstate", "painchance", "painsound", "meleestate", "missilestate",
               "deathstate", "xdeathstate", "deathsound", "speed", "radius", "height", "mass",
               "damage", "activesound", "flags", "raisestate"]
# ordre des champs de DoomMobjInfo (contrat section 4)
DMI_SHORTS = ["doomednum", "spawnstate", "seestate", "painstate", "meleestate", "missilestate",
              "deathstate", "xdeathstate", "raisestate", "spawnhealth", "speed", "radius", "height",
              "painchance", "damage"]
DMI_BYTES = ["seesound", "attacksound", "painsound", "deathsound", "activesound", "reactiontime"]
AMMO = ["am_clip", "am_shell", "am_cell", "am_misl", "NUMAMMO", "am_noammo"]     # doomdef.h


# ----------------------------------------------------------------------------- lecture C
def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def parse_enum(text, tag):
    """Enumerateurs de `typedef enum {...} tag;` dans l'ordre, valeurs explicites honorees."""
    m = re.search(r"typedef\s+enum\s*\{([^{}]*)\}\s*" + re.escape(tag) + r"\s*;", text, re.S)
    if not m:
        raise SystemExit("enum %s introuvable" % tag)
    names, value = [], 0
    for item in m.group(1).split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            name, expr = (s.strip() for s in item.split("=", 1))
            value = int(expr, 0)
        else:
            name = item
        names.append((name, value))
        value += 1
    return names


def parse_defines_enum(text, prefix):
    """`MF_X = 12,` ou `MF_X = 0x400,` d'un enum sans typedef nomme (p_mobj.h)."""
    out = {}
    for m in re.finditer(r"\b(" + prefix + r"\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)", text):
        out[m.group(1)] = int(m.group(2), 0)
    return out


def c_eval(expr, names):
    """Expression C entiere (+ - * | << ~, parentheses, identificateurs du dictionnaire)."""
    expr = expr.strip()
    tree = ast.parse(expr, mode="eval")

    def ev(n):
        if isinstance(n, ast.Expression):
            return ev(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return n.value
        if isinstance(n, ast.Name):
            if n.id not in names:
                raise SystemExit("identificateur inconnu dans '%s' : %s" % (expr, n.id))
            return names[n.id]
        if isinstance(n, ast.UnaryOp):
            v = ev(n.operand)
            if isinstance(n.op, ast.USub):
                return -v
            if isinstance(n.op, ast.UAdd):
                return v
            if isinstance(n.op, ast.Invert):
                return ~v
        if isinstance(n, ast.BinOp):
            a, b = ev(n.left), ev(n.right)
            if isinstance(n.op, ast.Add):
                return a + b
            if isinstance(n.op, ast.Sub):
                return a - b
            if isinstance(n.op, ast.Mult):
                return a * b
            if isinstance(n.op, ast.BitOr):
                return a | b
            if isinstance(n.op, ast.LShift):
                return a << b
            if isinstance(n.op, ast.FloorDiv):
                return a // b
        raise SystemExit("expression C non geree : %s" % expr)

    return ev(tree)


def parse_string_array(text, name):
    m = re.search(r"\b" + name + r"\s*\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("%s[] introuvable" % name)
    return re.findall(r'"([^"]*)"', m.group(1))


def parse_states(text, n):
    m = re.search(r"\bstates\s*\[NUMSTATES\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("states[] introuvable")
    rx = re.compile(r"\{\s*(SPR_\w+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*\{\s*(\w+)\s*\}\s*,"
                    r"\s*(S_\w+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}")
    out = []
    for e in rx.finditer(m.group(1)):
        out.append(dict(sprite=e.group(1), frame=int(e.group(2)), tics=int(e.group(3)),
                        action=e.group(4), nextstate=e.group(5), misc1=int(e.group(6)),
                        misc2=int(e.group(7))))
    if len(out) != n:
        raise SystemExit("states[] : %d entrees lues, NUMSTATES = %d" % (len(out), n))
    return out


def parse_mobjinfo(text, n, names):
    m = re.search(r"\bmobjinfo\s*\[NUMMOBJTYPES\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("mobjinfo[] introuvable")
    out = []
    for block in re.finditer(r"\{([^{}]*)\}", m.group(1)):
        fields = [f.strip() for f in block.group(1).split(",")]
        fields = [f for f in fields if f]
        if len(fields) != len(MOBJ_FIELDS):
            raise SystemExit("mobjinfo : %d champs au lieu de %d : %r" % (len(fields), len(MOBJ_FIELDS), fields))
        out.append({k: c_eval(v, names) for k, v in zip(MOBJ_FIELDS, fields)})
    if len(out) != n:
        raise SystemExit("mobjinfo[] : %d entrees lues, NUMMOBJTYPES = %d" % (len(out), n))
    return out


def parse_weapons(text, names):
    m = re.search(r"\bweaponinfo\s*\[NUMWEAPONS\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("weaponinfo[] introuvable")
    out = []
    for block in re.finditer(r"\{([^{}]*)\}", m.group(1)):
        fields = [f.strip() for f in block.group(1).split(",") if f.strip()]
        if len(fields) != 6:
            raise SystemExit("weaponinfo : 6 champs attendus : %r" % fields)
        out.append(dict(zip(["ammo", "upstate", "downstate", "readystate", "atkstate", "flashstate"],
                            [c_eval(f, names) for f in fields])))
    return out


def parse_rndtable(text):
    m = re.search(r"\brndtable\s*\[256\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("rndtable[] introuvable")
    vals = [int(x) for x in re.findall(r"\d+", m.group(1))]
    if len(vals) != 256:
        raise SystemExit("rndtable : %d valeurs" % len(vals))
    return vals


# ----------------------------------------------------------------------------- numerotation OT
def mt_to_ot_table(nmt, mt_names):
    """Contrat section 1 : MT_PLAYER -> 13, les autres MT croissants sur les plages OT_RANGES."""
    slots = [v for lo, hi in OT_RANGES for v in range(lo, hi + 1)]
    others = [i for i in range(nmt) if mt_names[i] != "MT_PLAYER"]
    if len(others) > len(slots):
        raise SystemExit("plus de MT (%d) que de valeurs OT libres (%d)" % (len(others), len(slots)))
    mt2ot = [0] * nmt
    mt2ot[mt_names.index("MT_PLAYER")] = OT_PLAYER
    for mt, ot in zip(others, slots):
        mt2ot[mt] = ot
    ot2mt = [-1] * OT_NMTYPES
    for mt, ot in enumerate(mt2ot):
        assert ot2mt[ot] == -1
        ot2mt[ot] = mt
    return mt2ot, ot2mt


# ----------------------------------------------------------------------------- emission
def write_atomic(path, text):
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="ascii", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def action_enum_name(a):
    return "DOOM_ACTION_NONE" if a == "NULL" else "DOOM_ACTION_" + a[2:].upper()


def c_enum(tag, names, comment):
    lines = ["/* %s */" % comment, "typedef enum {"]
    for name, value in names:
        lines.append("    %s = %d," % (name, value))
    lines.append("    %s_END_ = %d" % (tag.upper(), names[-1][1] + 1))
    lines.append("} %s;" % tag)
    return "\n".join(lines)


def rows(items, per_line, fmt="%d"):
    out = []
    for i in range(0, len(items), per_line):
        out.append("    " + ", ".join(fmt % v for v in items[i:i + per_line]) + ",")
    return "\n".join(out)


def emit_header(d):
    L = []
    L.append("/* DOOM_TABLES.H -- GENERE par tools/doom2ps/info2tables.py depuis Mimas/core (info.h, sounds.h) : NE PAS EDITER.")
    L.append(" * Contrat : docs/doom/DOOM_ABI.md section 4.  Doom est GPL-2+ (id Software 1993-1996, Simon Howard 2005-2014). */")
    L.append("#ifndef DOOM_TABLES_H")
    L.append("#define DOOM_TABLES_H")
    L.append("")
    L.append("#define NUMSPRITES   %d" % d["nspr"])
    L.append("#define NUMSTATES    %d" % d["nstates"])
    L.append("#define NUMMOBJTYPES %d" % d["nmt"])
    L.append("#define NUMSFX       %d" % d["nsfx"])
    L.append("#define NUMWEAPONS   %d" % len(d["weapons"]))
    L.append("#define DOOM_NUMACTIONS %d      /* doomActions[0] = NULL, puis les A_* distincts */" % len(d["actions"]))
    L.append("#define DOOM_NMSTATICSFX %d     /* contrat section 3 */" % len(STATIC_SFX))
    L.append("#define DOOM_OT_NMTYPES %d      /* OT_NMTYPES du moteur (SLEVEL.H), non redefini */" % OT_NMTYPES)
    L.append("")
    L.append("/* DoomState.flags */")
    L.append("#define DOOM_SF_FULLBRIGHT 1    /* bit 15 de states[].frame */")
    L.append("#define DOOM_SF_PSPRITE    2    /* etats %d..%d (S_LIGHTDONE..S_BFGFLASH2) : action = verbe d'arme, membre .psp */"
             % (d["psp_first"], d["psp_last"]))
    L.append("")
    L.append("/* Drapeaux mobjinfo (p_mobj.h) */")
    for name, value in sorted(d["mf"].items(), key=lambda kv: kv[1]):
        L.append("#define %-16s 0x%x" % (name, value))
    L.append("")
    L.append(c_enum("doom_spritenum_t", d["spr_enum"], "spritenum_t (info.h) -- SPR_HEAD/SPR_HOVER/SPR_STEP du moteur sont des macros"
                    " FONCTION (SPRITE.H:116-125), sans conflit avec un enumerateur nu"))
    L.append("")
    L.append(c_enum("doom_statenum_t", d["s_enum"], "statenum_t (info.h)"))
    L.append("")
    L.append(c_enum("doom_mobjtype_t", d["mt_enum"], "mobjtype_t (info.h)"))
    L.append("")
    L.append(c_enum("doom_sfxenum_t", d["sfx_enum"], "sfxenum_t (sounds.h)"))
    L.append("")
    L.append(c_enum("doom_ammotype_t", list(zip(AMMO, range(len(AMMO)))), "ammotype_t (doomdef.h) : am_noammo = 5"))
    L.append("")
    L.append(c_enum("doom_action_t", [(action_enum_name(a), i) for i, a in enumerate(d["actions"])],
                    "index dans doomActions[] (DOOM_VERBS.C), ordre d'apparition dans states[]"))
    L.append("")
    L.append("/* 8 o : 1+1+2+2+1+1, alignement max 2 -> pas de bourrage (contrat section 4) */")
    L.append("typedef struct {")
    L.append("    unsigned char sprite, frame;      /* frame = states[].frame & 0x7fff (lettre A = 0) */")
    L.append("    short tics;                       /* 35 Hz, tels quels ; -1 = infini */")
    L.append("    short nextstate;")
    L.append("    unsigned char action, flags;      /* action = doom_action_t ; flags = DOOM_SF_* */")
    L.append("} DoomState;")
    L.append("")
    L.append("/* 44 o : 15 shorts = 30, + 6 sons/reactiontime + ot + pad = 38, int aligne sur 4 -> 40, + 4 = 44.")
    L.append(" * mass omis (contrat section 4) ; radius/height en u (/FRACUNIT) ; speed en u/tic ; ot = contrat section 1 */")
    L.append("typedef struct {")
    L.append("    short doomednum, spawnstate, seestate, painstate, meleestate, missilestate,")
    L.append("          deathstate, xdeathstate, raisestate, spawnhealth, speed, radius, height,")
    L.append("          painchance, damage;          /* painchance en short : 256 (KEEN, BOSSBRAIN) ne tronque pas */")
    L.append("    unsigned char seesound, attacksound, painsound, deathsound, activesound, reactiontime, ot, pad;")
    L.append("    int flags;                        /* MF_* */")
    L.append("} DoomMobjInfo;")
    L.append("")
    L.append("/* 12 o : d_items.c weaponinfo[] (ammo = doom_ammotype_t, etats = doom_statenum_t) */")
    L.append("typedef struct {")
    L.append("    short ammo, upstate, downstate, readystate, atkstate, flashstate;")
    L.append("} DoomWeaponInfo;")
    L.append("")
    L.append("extern const DoomState      doomStates[NUMSTATES];")
    L.append("extern const DoomMobjInfo   doomMobjInfo[NUMMOBJTYPES];")
    L.append("extern const DoomWeaponInfo doomWeaponInfo[NUMWEAPONS];")
    L.append("extern const unsigned char  doomStaticSfx[DOOM_NMSTATICSFX];   /* sfx_* dans l'ordre STATIC.DAT, contrat section 3 */")
    L.append("extern const short          doomOtToMt[DOOM_OT_NMTYPES];      /* -1 si l'OT n'est pas un mobj Doom */")
    L.append("extern const unsigned char  doomMtToOt[NUMMOBJTYPES];")
    L.append("extern const char           doomSpriteNames[NUMSPRITES][5];   /* 4 lettres + NUL */")
    L.append("extern const char *const    doomSfxNames[NUMSFX];            /* nom du lump sans DS */")
    L.append("extern const char *const    doomActionNames[DOOM_NUMACTIONS]; /* \"NULL\", \"A_Light0\", ... */")
    L.append("")
    L.append("/* m_random.c : rndtable + P_Random (simulation) / M_Random (le reste) */")
    L.append("extern const unsigned char rndtable[256];")
    L.append("extern int prndindex, rndindex;")
    L.append("int  P_Random(void);")
    L.append("int  M_Random(void);")
    L.append("void M_ClearRandom(void);")
    L.append("")
    L.append("/* doomActions[] (contrat section 4) est definie dans DOOM_VERBS.C a partir de DOOM_ACTIONS.H ;")
    L.append(" * son type DoomAction (union {mobj, psp}) vit dans DOOM.H. */")
    L.append("")
    L.append("#endif /* DOOM_TABLES_H */")
    return "\n".join(L) + "\n"


def emit_actions_header(d):
    L = []
    L.append("/* DOOM_ACTIONS.H -- GENERE par tools/doom2ps/info2tables.py depuis Mimas/core/info.c : NE PAS EDITER.")
    L.append(" * Verbes A_* distincts de states[], ordre d'apparition = index doom_action_t (DOOM_TABLES.H).")
    L.append(" *   M(name) : verbe d'acteur   void name(DoomActor *this)")
    L.append(" *   P(name) : verbe d'arme     void name(DoomPlayer *p, int ps)   (etats psprite %d..%d)" % (d["psp_first"], d["psp_last"]))
    L.append(" * A inclure APRES DOOM.H (DoomActor, DoomPlayer, DoomAction).  DOOM_VERBS.C :")
    L.append(" *   DOOM_ACTIONS_PROTOTYPES")
    L.append(" *   #define M(n) { n },              (premier membre de l'union : .mobj)")
    L.append(" *   #define P(n) { .psp = n },       (designateur : extension GNU acceptee en gnu89)")
    L.append(" *   const DoomAction doomActions[DOOM_NUMACTIONS] = { { 0 }, DOOM_ACTIONS_LIST(M, P) };")
    L.append(" *   #undef M / #undef P ; un verbe non implemente se met a { 0 } (SPEC_RUNTIME section 3). */")
    L.append("#ifndef DOOM_ACTIONS_H")
    L.append("#define DOOM_ACTIONS_H")
    L.append("")
    L.append("#define DOOM_ACTIONS_LIST(M, P) \\")
    body = []
    for a in d["actions"][1:]:
        body.append("    %s(%s)" % ("P" if a in d["psp_actions"] else "M", a))
    L.append(" \\\n".join(body))
    L.append("")
    L.append("#define DOOM_ACTIONS_PROTO_M_(n) void n(DoomActor *this);")
    L.append("#define DOOM_ACTIONS_PROTO_P_(n) void n(DoomPlayer *p, int ps);")
    L.append("#define DOOM_ACTIONS_PROTOTYPES DOOM_ACTIONS_LIST(DOOM_ACTIONS_PROTO_M_, DOOM_ACTIONS_PROTO_P_)")
    L.append("")
    L.append("#endif /* DOOM_ACTIONS_H */")
    return "\n".join(L) + "\n"


def emit_c(d):
    L = []
    L.append("/* DOOM_TABLES.C -- GENERE par tools/doom2ps/info2tables.py depuis Mimas/core : NE PAS EDITER.")
    L.append(" * Sources : info.c (states, mobjinfo, sprnames), sounds.c, d_items.c, m_random.c -- Doom, GPL-2+")
    L.append(" * (Copyright 1993-1996 id Software, 2005-2014 Simon Howard).  Contrat : docs/doom/DOOM_ABI.md section 4. */")
    L.append("#include \"DOOM_TABLES.H\"")
    L.append("")
    L.append("/* Verification de taille a la compilation (C89 : un tableau de taille negative est une erreur) --")
    L.append(" * DoomState = 1+1+2+2+1+1 = 8 ; DoomMobjInfo = 15*2 + 8 = 38 -> 40 (int aligne 4) + 4 = 44. */")
    L.append("typedef char doom_sizeof_DoomState_is_8[(sizeof(DoomState) == 8) ? 1 : -1];")
    L.append("typedef char doom_sizeof_DoomMobjInfo_is_44[(sizeof(DoomMobjInfo) == 44) ? 1 : -1];")
    L.append("typedef char doom_sizeof_DoomWeaponInfo_is_12[(sizeof(DoomWeaponInfo) == 12) ? 1 : -1];")
    L.append("")
    L.append("/* {sprite, frame, tics, nextstate, action, flags} -- flags bit 0 = fullbright, bit 1 = psprite */")
    L.append("const DoomState doomStates[NUMSTATES] = {")
    for i, s in enumerate(d["states"]):
        L.append("    {%3d, %2d, %4d, %4d, %3d, %d},  /* %4d %s */"
                 % (s["sprite"], s["frame"], s["tics"], s["nextstate"], s["action"], s["flags"], i, d["s_names"][i]))
    L.append("};")
    L.append("")
    L.append("/* {doomednum, spawnstate, seestate, painstate, meleestate, missilestate, deathstate, xdeathstate, raisestate,")
    L.append(" *  spawnhealth, speed, radius, height, painchance, damage,")
    L.append(" *  seesound, attacksound, painsound, deathsound, activesound, reactiontime, ot, pad, flags} */")
    L.append("const DoomMobjInfo doomMobjInfo[NUMMOBJTYPES] = {")
    for i, m in enumerate(d["mobj"]):
        L.append("    /* %3d %s -> OT %d */" % (i, d["mt_names"][i], m["ot"]))
        L.append("    {%d, %d, %d, %d, %d, %d, %d, %d, %d," % tuple(m[k] for k in DMI_SHORTS[:9]))
        L.append("     %d, %d, %d, %d, %d, %d," % tuple(m[k] for k in DMI_SHORTS[9:]))
        L.append("     %d, %d, %d, %d, %d, %d, %d, 0, 0x%x},"
                 % (tuple(m[k] for k in DMI_BYTES) + (m["ot"], m["flags"])))
    L.append("};")
    L.append("")
    L.append("/* d_items.c : {ammo, upstate, downstate, readystate, atkstate, flashstate} */")
    L.append("const DoomWeaponInfo doomWeaponInfo[NUMWEAPONS] = {")
    wnames = ["fist", "pistol", "shotgun", "chaingun", "missile", "plasma", "bfg", "chainsaw", "supershotgun"]
    for i, w in enumerate(d["weapons"]):
        L.append("    {%d, %d, %d, %d, %d, %d},  /* wp_%s */" % (w["ammo"], w["upstate"], w["downstate"],
                 w["readystate"], w["atkstate"], w["flashstate"], wnames[i] if i < len(wnames) else str(i)))
    L.append("};")
    L.append("")
    L.append("/* contrat section 3 : sfx_* des 20 sons statiques, ordre STATIC.DAT (ST_JOHN, PUSHBLOCK = +6) */")
    L.append("const unsigned char doomStaticSfx[DOOM_NMSTATICSFX] = {")
    L.append("    " + ", ".join("%d" % v for v in d["static_sfx"]) + "")
    L.append("    /* " + " ".join(STATIC_SFX) + " */")
    L.append("};")
    L.append("")
    L.append("/* contrat section 1 : OT -> MT (-1 = type moteur ou libre) */")
    L.append("const short doomOtToMt[DOOM_OT_NMTYPES] = {")
    L.append(rows(d["ot2mt"], 16, "%3d"))
    L.append("};")
    L.append("")
    L.append("/* contrat section 1 : MT -> OT (MT_PLAYER = 13, puis [14..47] [64..90] [92..162] [172..226]) */")
    L.append("const unsigned char doomMtToOt[NUMMOBJTYPES] = {")
    L.append(rows(d["mt2ot"], 16, "%3d"))
    L.append("};")
    L.append("")
    L.append("const char doomSpriteNames[NUMSPRITES][5] = {")
    L.append(rows(d["sprnames"], 10, '"%s"'))
    L.append("};")
    L.append("")
    L.append("const char *const doomSfxNames[NUMSFX] = {")
    L.append(rows(d["sfxnames"], 8, '"%s"'))
    L.append("};")
    L.append("")
    L.append("const char *const doomActionNames[DOOM_NUMACTIONS] = {")
    L.append(rows(d["actions"], 4, '"%s"'))
    L.append("};")
    L.append("")
    L.append("/* m_random.c:24-60 */")
    L.append("const unsigned char rndtable[256] = {")
    L.append(rows(d["rndtable"], 14, "%3d"))
    L.append("};")
    L.append("")
    L.append("int prndindex = 0;")
    L.append("int rndindex = 0;")
    L.append("")
    L.append("int P_Random(void)")
    L.append("{")
    L.append("    prndindex = (prndindex + 1) & 0xff;")
    L.append("    return rndtable[prndindex];")
    L.append("}")
    L.append("")
    L.append("int M_Random(void)")
    L.append("{")
    L.append("    rndindex = (rndindex + 1) & 0xff;")
    L.append("    return rndtable[rndindex];")
    L.append("}")
    L.append("")
    L.append("void M_ClearRandom(void)")
    L.append("{")
    L.append("    rndindex = prndindex = 0;")
    L.append("}")
    return "\n".join(L) + "\n"


# ----------------------------------------------------------------------------- principal
def build(core, info_c, sounds_c):
    core_dir = os.path.dirname(os.path.abspath(info_c))
    src = lambda name: os.path.join(core_dir, name)     # noqa: E731
    info_h = strip_comments(read(src("info.h")))
    sounds_h = strip_comments(read(src("sounds.h")))
    info_c_t = strip_comments(read(info_c))
    sounds_c_t = strip_comments(read(sounds_c))
    d_items = strip_comments(read(src("d_items.c")))
    m_random = strip_comments(read(src("m_random.c")))
    p_mobj_h = strip_comments(read(src("p_mobj.h")))

    spr_enum = parse_enum(info_h, "spritenum_t")
    s_enum = parse_enum(info_h, "statenum_t")
    mt_enum = parse_enum(info_h, "mobjtype_t")
    sfx_enum = parse_enum(sounds_h, "sfxenum_t")
    for enum, terminal in ((spr_enum, "NUMSPRITES"), (s_enum, "NUMSTATES"),
                           (mt_enum, "NUMMOBJTYPES"), (sfx_enum, "NUMSFX")):
        if enum[-1][0] != terminal:
            raise SystemExit("enum : dernier enumerateur %s attendu, lu %s" % (terminal, enum[-1][0]))
        enum.pop()
    nspr, nstates, nmt, nsfx = len(spr_enum), len(s_enum), len(mt_enum), len(sfx_enum)
    spr_names = [n for n, _ in spr_enum]
    s_names = [n for n, _ in s_enum]
    mt_names = [n for n, _ in mt_enum]
    sfx_names = [n for n, _ in sfx_enum]

    sprnames = parse_string_array(info_c_t, "sprnames")
    if len(sprnames) != nspr or any("SPR_" + s != e for s, e in zip(sprnames, spr_names)):
        raise SystemExit("sprnames[] (%d) ne correspond pas a spritenum_t (%d)" % (len(sprnames), nspr))
    sfx_lumps = parse_string_array(sounds_c_t, "S_sfx")
    if len(sfx_lumps) != nsfx or any("sfx_" + s.lower() != e.lower() for s, e in zip(sfx_lumps, sfx_names)):
        raise SystemExit("S_sfx[] (%d) ne correspond pas a sfxenum_t (%d)" % (len(sfx_lumps), nsfx))

    names = {"FRACUNIT": FRACUNIT}
    names.update(dict(s_enum))
    names.update(dict(sfx_enum))
    names.update(dict(zip(AMMO, range(len(AMMO)))))
    mf = parse_defines_enum(p_mobj_h, "MF_")
    if "MF_SHOOTABLE" not in mf or mf["MF_SHOOTABLE"] != 4:
        raise SystemExit("MF_* de p_mobj.h non lus")
    names.update(mf)

    raw_states = parse_states(info_c_t, nstates)
    raw_mobj = parse_mobjinfo(info_c_t, nmt, names)
    weapons = parse_weapons(d_items, names)
    rndtable = parse_rndtable(m_random)
    if len(weapons) != 9:
        raise SystemExit("weaponinfo : %d armes" % len(weapons))

    psp_first, psp_last = names["S_LIGHTDONE"], names["S_BFGFLASH2"]
    actions = ["NULL"]
    psp_actions, mobj_actions = set(), set()
    states = []
    spr_index = dict(spr_enum)
    for i, s in enumerate(raw_states):
        if s["action"] not in actions:
            actions.append(s["action"])
        is_psp = psp_first <= i <= psp_last
        if s["action"] != "NULL":
            (psp_actions if is_psp else mobj_actions).add(s["action"])
        frame = s["frame"] & 0x7fff
        flags = (1 if s["frame"] & 0x8000 else 0) | (2 if is_psp else 0)
        if not (0 <= frame < 256 and -32768 <= s["tics"] < 32768):
            raise SystemExit("state %d hors gabarit" % i)
        states.append(dict(sprite=spr_index[s["sprite"]], frame=frame, tics=s["tics"],
                           nextstate=names[s["nextstate"]], action=actions.index(s["action"]),
                           flags=flags))
    both = psp_actions & mobj_actions
    if both:
        raise SystemExit("verbes a la fois d'arme et d'acteur (contrat section 4 : un membre par verbe) : %s" % sorted(both))
    if len(actions) > 255:
        raise SystemExit("plus de 255 actions : DoomState.action est un u8")

    mt2ot, ot2mt = mt_to_ot_table(nmt, mt_names)
    mobj = []
    for i, m in enumerate(raw_mobj):
        e = dict(m)
        for k in ("radius", "height"):
            if e[k] % FRACUNIT:
                raise SystemExit("%s.%s = %d n'est pas un multiple de FRACUNIT" % (mt_names[i], k, e[k]))
            e[k] //= FRACUNIT
        if e["speed"] >= FRACUNIT:
            if e["speed"] % FRACUNIT:
                raise SystemExit("%s.speed = %d" % (mt_names[i], e["speed"]))
            e["speed"] //= FRACUNIT
        e["ot"] = mt2ot[i]
        for k in DMI_SHORTS:
            if not -32768 <= e[k] < 32768:
                raise SystemExit("%s.%s = %d deborde un short" % (mt_names[i], k, e[k]))
        for k in DMI_BYTES:
            if not 0 <= e[k] < 256:
                raise SystemExit("%s.%s = %d deborde un u8" % (mt_names[i], k, e[k]))
        if not -2 ** 31 <= e["flags"] < 2 ** 31:
            raise SystemExit("%s.flags deborde un int" % mt_names[i])
        mobj.append(e)

    static_sfx = []
    for lump in STATIC_SFX:
        key = "sfx_" + lump
        if key not in names:
            raise SystemExit("son statique inconnu : %s" % key)
        static_sfx.append(names[key])

    return dict(nspr=nspr, nstates=nstates, nmt=nmt, nsfx=nsfx, spr_enum=spr_enum, s_enum=s_enum,
                mt_enum=mt_enum, sfx_enum=sfx_enum, s_names=s_names, mt_names=mt_names,
                sprnames=sprnames, sfxnames=sfx_lumps, mf=mf, states=states, raw_states=raw_states,
                mobj=mobj, raw_mobj=raw_mobj, weapons=weapons, rndtable=rndtable, actions=actions,
                psp_actions=psp_actions, psp_first=psp_first, psp_last=psp_last,
                static_sfx=static_sfx, mt2ot=mt2ot, ot2mt=ot2mt, names=names)


def make_json(d):
    ed2mt = {}
    for i, m in enumerate(d["raw_mobj"]):
        if m["doomednum"] >= 0:
            if m["doomednum"] in ed2mt:
                raise SystemExit("doomednum %d en double" % m["doomednum"])
            ed2mt[m["doomednum"]] = i
    return dict(
        source="Mimas/core info.c sounds.c d_items.c m_random.c (GPL-2+) ; contrat DOOM_ABI.md section 1, 3, 4",
        counts=dict(NUMSPRITES=d["nspr"], NUMSTATES=d["nstates"], NUMMOBJTYPES=d["nmt"], NUMSFX=d["nsfx"],
                    NUMWEAPONS=len(d["weapons"]), DOOM_NUMACTIONS=len(d["actions"]), OT_NMTYPES=OT_NMTYPES),
        ed_to_mt={str(k): v for k, v in sorted(ed2mt.items())},
        mt_to_ot=d["mt2ot"],
        ot_to_mt=d["ot2mt"],
        mt_names=d["mt_names"],
        state_names=d["s_names"],
        sprite_names=d["sprnames"],
        spritename_to_num={n: i for i, n in enumerate(d["sprnames"])},
        sfx_names=d["sfxnames"],
        sfxname_to_num={n: i for i, n in enumerate(d["sfxnames"])},
        static_sfx=[dict(index=i, lump=n, sfx=v) for i, (n, v) in enumerate(zip(STATIC_SFX, d["static_sfx"]))],
        psprite_states=[d["psp_first"], d["psp_last"]],
        actions=d["actions"],
        psp_actions=sorted(d["psp_actions"]),
        mf_flags=d["mf"],
        states=[dict(name=d["s_names"][i], sprite=s["sprite"], spritename=d["sprnames"][s["sprite"]],
                     frame=s["frame"], fullbright=bool(s["flags"] & 1), tics=s["tics"],
                     nextstate=s["nextstate"], action=d["actions"][s["action"]], flags=s["flags"],
                     raw_frame=r["frame"], misc1=r["misc1"], misc2=r["misc2"])
                for i, (s, r) in enumerate(zip(d["states"], d["raw_states"]))],
        mobjinfo=[dict(name=d["mt_names"][i], ot=d["mt2ot"][i], **{k: r[k] for k in MOBJ_FIELDS})
                  for i, r in enumerate(d["raw_mobj"])],
        mobjinfo_engine=[dict(name=d["mt_names"][i], **{k: m[k] for k in DMI_SHORTS + DMI_BYTES + ["ot", "flags"]})
                         for i, m in enumerate(d["mobj"])],
        weapons=d["weapons"],
        ammo_names=AMMO,
        rndtable=d["rndtable"],
    )


def verify(d):
    n = d["names"]
    checks = [
        ("NUMSTATES == 967", d["nstates"] == 967),
        ("NUMMOBJTYPES == 137", d["nmt"] == 137),
        ("NUMSPRITES == 138", d["nspr"] == 138),
        ("NUMSFX == 109", d["nsfx"] == 109),
        ("doomStates[S_POSS_RUN1].tics == 4", d["states"][n["S_POSS_RUN1"]]["tics"] == 4),
        ("doomStates[S_PISTOL1].flags & 2", bool(d["states"][n["S_PISTOL1"]]["flags"] & 2)),
        ("doomStates[S_POSS_STND].flags & 2 == 0", not (d["states"][n["S_POSS_STND"]]["flags"] & 2)),
        ("doomStates[S_PISTOLFLASH].flags & 1 (fullbright)", bool(d["states"][n["S_PISTOLFLASH"]]["flags"] & 1)),
        ("doomMtToOt[MT_PLAYER] == 13", d["mt2ot"][n_mt(d, "MT_PLAYER")] == 13),
        ("doomMtToOt[MT_POSSESSED] == 14", d["mt2ot"][n_mt(d, "MT_POSSESSED")] == 14),
        ("doomMtToOt[MT_TROOP] == 24", d["mt2ot"][n_mt(d, "MT_TROOP")] == 24),
        ("doomMtToOt[MT 35] == 64", d["mt2ot"][35] == 64),
        ("doomMtToOt[MT 62] == 92", d["mt2ot"][62] == 92),
        ("doomMtToOt[MT 133] == 172", d["mt2ot"][133] == 172),
        ("doomMtToOt[MT 136] == 175", d["mt2ot"][136] == 175),
        ("doomOtToMt[48] (OT_NORMALDOOR) == -1", d["ot2mt"][48] == -1),
        ("doomOtToMt[91] (OT_SECTORSWITCH) == -1", d["ot2mt"][91] == -1),
        ("doomOtToMt[163..171] == -1", all(v == -1 for v in d["ot2mt"][163:172])),
        ("doomOtToMt[176..179] (OT_DOOM_*) == -1", all(v == -1 for v in d["ot2mt"][176:180])),
        ("MT_TROOPSHOT speed == 10", d["mobj"][n_mt(d, "MT_TROOPSHOT")]["speed"] == 10),
        ("MT_POSSESSED speed == 8, radius 20, height 56",
         (d["mobj"][n_mt(d, "MT_POSSESSED")]["speed"], d["mobj"][n_mt(d, "MT_POSSESSED")]["radius"],
          d["mobj"][n_mt(d, "MT_POSSESSED")]["height"]) == (8, 20, 56)),
        ("doomStaticSfx[19] == sfx_rlaunc", d["static_sfx"][19] == n["sfx_rlaunc"]),
        ("doomStaticSfx[0] == sfx_pistol == 1", d["static_sfx"][0] == n["sfx_pistol"] == 1),
        ("rndtable[1] == 8, rndtable[255] == 249", (d["rndtable"][1], d["rndtable"][255]) == (8, 249)),
        ("weapon pistol atkstate == S_PISTOL1", d["weapons"][1]["atkstate"] == n["S_PISTOL1"]),
    ]
    ok = True
    for label, res in checks:
        print("  [%s] %s" % ("ok" if res else "ECHEC", label))
        ok = ok and res
    print("  actions distinctes : %d (dont NULL), verbes d'arme : %d" % (len(d["actions"]), len(d["psp_actions"])))
    return ok


def n_mt(d, name):
    return d["mt_names"].index(name)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("info_c", nargs="?", default=os.path.join(DEFAULT_CORE, "info.c"))
    ap.add_argument("sounds_c", nargs="?", default=None, help="defaut : sounds.c a cote d'info.c")
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "game", "doom"))
    ap.add_argument("--json", default=os.path.join(ROOT, "build", "doom", "doom_ids.json"))
    a = ap.parse_args(argv)
    sounds_c = a.sounds_c or os.path.join(os.path.dirname(os.path.abspath(a.info_c)), "sounds.c")

    d = build(None, a.info_c, sounds_c)
    if not verify(d):
        return 1
    write_atomic(os.path.join(a.out_dir, "DOOM_TABLES.H"), emit_header(d))
    write_atomic(os.path.join(a.out_dir, "DOOM_ACTIONS.H"), emit_actions_header(d))
    write_atomic(os.path.join(a.out_dir, "DOOM_TABLES.C"), emit_c(d))
    write_atomic(a.json, json.dumps(make_json(d), indent=1) + "\n")
    print("ecrit : %s/{DOOM_TABLES.H,DOOM_ACTIONS.H,DOOM_TABLES.C}, %s" % (a.out_dir, a.json))
    return 0


if __name__ == "__main__":
    sys.exit(main())
