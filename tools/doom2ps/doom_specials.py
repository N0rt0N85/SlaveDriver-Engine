#!/usr/bin/env python3
"""doom_specials.py -- spéciaux Doom (lignes et secteurs) -> géométrie mobile et objets .LEV.

Couvre SPEC_CONVERTER §5 et le contrat DOOM_ABI §6 :
  - `specials_of(M)`      : inventaire des lignes/secteurs spéciaux d'une carte (portes manuelles,
                            ascenseurs, sols, déclencheurs W, interrupteurs S, sorties, dégâts) ;
  - `mobile_bounds(...)`  : course d'un secteur mobile, par la RÈGLE DE DOOM (p_doors.c, p_plats.c,
                            p_floor.c) : porte = plus bas plafond voisin - 4 ; ascenseur = plus bas sol
                            (voisins + le sien) ; sol turbo (36) = plus HAUT sol voisin + 8 ; sol qui
                            monte = RAISE ;
  - `special_objects(...)`: objets OT_NORMALELEVATOR 49, OT_SECTORSWITCH 91 (2 shorts), OT_SW1..4
                            168-171, OT_DOOM_EXIT 176/177, OT_DOOM_DAMAGE 179, OT_DOOM_TELEPORT 181,
                            OT_DOOM_FLOOR 182 (sols qui montent ET qui descendent), OT_DOOM_DOOR 183,
                            avec leurs params gros-boutistes (OBJECT.C `suckShort`).

Ce que le moteur lit (relu à la ligne) :
  - porte      : plus celle du moteur (`constructDoor`, 3 shorts, OBJECT.C:269-272) -- OT_DOOM_DOOR,
                 6 shorts `pb, channel, doorHeight, appui, cle, tag` (DOOM_GAME.C) ;
  - ascenseur  : `constructElevator(pb, type, lower, upper)` puis `channel = suckShort()`
                 (OBJECT.C:255-262, AI.C:4686-4699) -> 4 shorts `pb, lower, upper, channel` ;
  - sector-switch : `sectorNm = suckShort(); channel = suckShort();` et `level_sector[sectorNm].object = this`
                 (AI2.C:665-676) -> 2 shorts, UNE feuille = UN objet au plus ;
  - interrupteur : `sectorNm, channel, ox, oy, oz` (AI2.C:582-596) ; la tuile OFF de la séquence
                 `level_sequenceMap[type]` doit exister UNE fois dans les murs de `sectorNm` (:606-632) ;
                 PowerSlave n'accepte le press qu'à < 40 u de l'orifice ; Doom le prend sur tout le
                 mur (`CFG_SWITCH_AIM` 0, SPRITE.H), l'orifice n'y place plus que le son.

Les objets Doom non-moteur (176-183, 204-226) sont lus par `game_placeObject` (SPEC_RUNTIME §2, §6).
Ne modifie ni assemble.py ni le moteur : tout est importable par make_e1m1.py.
"""
from __future__ import annotations

import math
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
# Types d'interrupteur. Un type est UNE apparence de mur : l'objet cherche la tuile OFF de SA sequence
# sur les murs de son secteur (constructSwitch, AI2.C:606-633), et le moteur n'en a que quatre
# (OBJECT.C:217). MESURE 2026-09-18 : E1M2 en voulait 10, E1M7 13 -- la meme texture posee a deux
# hauteurs fait deux tuiles, donc deux apparences. Doom n'a pas de poupees : les 23 types OT_DOLL1..23
# de PowerSlave (204-226, SLEVEL.H) portent les suivants, `game_placeObject` (DOOM_GAME.C) les
# envoie a constructSwitch avec les memes 5 params.
OT_DOLL1 = 204
OT_SWITCH_TYPES = tuple(range(OT_SW1, OT_SW4 + 1)) + tuple(range(OT_DOLL1, OT_DOLL1 + 23))
OT_DOOM_EXIT = 176
OT_DOOM_SECRETEXIT = 177
OT_DOOM_LIGHT = 178
OT_DOOM_DAMAGE = 179
OT_DOOM_SECRETWALL = 180      # param : mur DOORWALL d'une ligne ML_SECRET (les monstres ne la pressent pas)
# Teleporteur de ligne (EV_Teleport, p_telept.c) : 8 params = feuille qui declenche, feuille
# d'arrivee, x, z d'arrivee, angle (celui du depart : l'angle Doom, que DOOM_GAME.C convertit comme
# constructPlayer, - 90 degres), x, z du brouillard
# d'arrivee (20 u devant, dans l'angle d'arrivee), drapeaux (TELE_ONCE). DOOM_GAME.C.
OT_DOOM_TELEPORT = 181
TELE_ONCE = 1
# Sol qui MONTE ou DESCEND (EV_DoFloor, EV_DoPlat) : 6 params = pb, course (> 0 monte : emis en haut,
# descendu au chargement ; < 0 descend : emis a l'etat du WAD), canal, vitesse en 1/8 u par tic,
# face dont le flat remplace le sien au depart (-1 = aucun), degats du secteur ensuite (hp toutes
# les 32 tics ; -1 = inchanges). DOOM_GAME.C.
OT_DOOM_FLOOR = 182
OT_NMTYPES = 227
ML_SECRET = 0x20

CHANNEL_EXIT = 900            # DOOM_ABI §6 : tags Doom 1..999, sortie 900, secrète 901
CHANNEL_SECRETEXIT = 901
CHANNEL_NONE = -1

DOOR_SLIT = 1                 # SPEC_CONVERTER E8 : plafond fermé = sol + 1 (un quad sans plan est rejeté)
PLAYER_EYE = 41               # params/doom.cfg : PLAYER_RADIUS 16 + PLAYER_EYE_HOVER 25 (DOOM_ABI §7)
SWITCH_PRESS_DIST = 40        # AI2.C:535-538

# -- spéciaux de ligne (p_spec.c / p_switch.c / p_doors.c / p_plats.c / p_floor.c, Mimas/core) ----
# PORTES (p_doors.c) : une porte Doom est un OT_DOOM_DOOR (DOOM_GAME.C), qui porte le GENRE de son
# appui (EV_VerticalDoor, ligne manuelle) ET celui de son tag (EV_DoDoor, declencheur W ou
# interrupteur S) : une meme porte peut avoir les deux. MESURE 2026-09-18 : la porte du moteur
# (door_func, AI.C:4313) n'en sait qu'un -- un canal y coupe l'appui -- et referme toujours apres
# 128 tics ; E1M2 secteur 97 (manuelle 31 + interrupteur 103 tag 7) avait perdu son canal, et le
# secteur 21 (la porte vers dehors, tag 5, interrupteur a ~1500 u) se refermait avant qu'on arrive.
# Genres = vldoor_e : 1 normal (monte, attend 150 tics, redescend), 2 open (reste ouverte),
# 3 blazeRaise, 4 blazeOpen (x4). Les portes qui FERMENT (3, 16, 42, 50, 75, 76, 107, 110, 113, 116)
# partent ouvertes dans le WAD : pas converties (ignorees, comptees).
OT_DOOM_DOOR = 183
# ASCENSEURS (EV_DoPlat downWaitUpStay / blazeDWUS, T_PlatRaise) : 5 params = pb, course (< 0),
# canal, vitesse en 1/8 u par tic (PLATSPEED*4 = 32, blaze *8 = 64), attente en bas en tics
# (PLATWAIT*TICRATE = 105). Un signal ne le part que s'il est au repos (EV_DoPlat saute un secteur
# qui a deja un specialdata). DOOM_GAME.C. L'ascenseur du moteur (OT_NORMALELEVATOR) repartait a
# CHAQUE signal, meme pendant son attente en bas -- il n'est plus emis que pour --lift-contact.
OT_DOOM_LIFT = 184
LIFT_SPEED, LIFT_SPEED_BLAZE, LIFT_WAIT = 32, 64, 105
LIFT_BLAZE = {120, 121, 122, 123}
# LIGNES W (P_CrossSpecialLine) : 6 params = x1, z1, x2, z2 (le linedef, coordonnees Doom = x, z
# du moteur), canal, drapeaux (WLINE_ONCE). DOOM_GAME.C teste a chaque tic que le CENTRE du
# joueur la franchit. Le declencheur du moteur (OT_SECTORSWITCH) partait en ENTRANT dans une
# FEUILLE, des deux cotes de la ligne -- MESURE 2026-09-18, E1M2 : l'ascenseur 180 (tag 10) etait
# appele avant qu'on l'atteigne et renvoye en haut, vide, quand on en descendait.
OT_DOOM_WLINE = 185
WLINE_ONCE = 1
# W1 = les cas de P_CrossSpecialLine avant « RETRIGGERS » (p_spec.c, Mimas/core) : `line->special
# = 0` apres le premier passage. Les autres (72-98, 105-107, 120, 126, 128, 129) repartent.
W_ONCE = {2, 3, 4, 5, 6, 8, 10, 12, 13, 16, 17, 19, 22, 25, 30, 35, 36, 37, 38, 39, 40, 44, 52,
          53, 54, 56, 57, 58, 59, 100, 104, 108, 109, 110, 119, 121, 124, 125, 130, 141}
# DONUT (EV_DoDonut, p_spec.c ; 9 = S1) : pour chaque secteur s1 du tag, s2 = l'autre secteur de
# la 1re ligne de s1, s3 = le secteur ARRIERE de la 1re ligne de s2 qui n'est pas s1 ; s2 (l'anneau)
# monte au sol de s3 en prenant son flat et en perdant ses degats, s1 (le trou) descend au sol de
# s3, tous deux a FLOORSPEED/2 (4 en 1/8 u). E1M2 : l'interrupteur de la tronconneuse, ligne 604.
DONUT = {9}
DONUT_SPEED = 4
PORTE_NORMALE, PORTE_OUVERTE, PORTE_BLAZERAISE, PORTE_BLAZEOPEN = 1, 2, 3, 4
CLE_BLEUE, CLE_JAUNE, CLE_ROUGE = 1, 2, 3        # bit carte = cle - 1, bit crane = cle + 2 (it_*)
# portes manuelles : le secteur de la porte est celui du sidedef GAUCHE (p_doors.c EV_VerticalDoor :
# `sec = sides[line->sidenum[1]].sector`). special -> (genre, cle).
DOOR_MANUAL_KIND = {1: (PORTE_NORMALE, 0), 26: (PORTE_NORMALE, CLE_BLEUE),
                    27: (PORTE_NORMALE, CLE_JAUNE), 28: (PORTE_NORMALE, CLE_ROUGE),
                    31: (PORTE_OUVERTE, 0), 32: (PORTE_OUVERTE, CLE_BLEUE),
                    33: (PORTE_OUVERTE, CLE_ROUGE), 34: (PORTE_OUVERTE, CLE_JAUNE),
                    117: (PORTE_BLAZERAISE, 0), 118: (PORTE_BLAZEOPEN, 0)}
DOOR_MANUAL = set(DOOR_MANUAL_KIND)
DOOR_KEYED = {s for s, (g, k) in DOOR_MANUAL_KIND.items() if k}
# portes à tag : special -> genre ; channel = tag
DOOR_TAGGED_W = {2: PORTE_OUVERTE, 4: PORTE_NORMALE, 86: PORTE_OUVERTE, 90: PORTE_NORMALE,
                 105: PORTE_BLAZERAISE, 106: PORTE_BLAZEOPEN, 108: PORTE_BLAZERAISE,
                 109: PORTE_BLAZEOPEN}
DOOR_TAGGED_S = {29: PORTE_NORMALE, 61: PORTE_OUVERTE, 63: PORTE_NORMALE, 103: PORTE_OUVERTE,
                 111: PORTE_BLAZERAISE, 112: PORTE_BLAZEOPEN, 114: PORTE_BLAZERAISE,
                 115: PORTE_BLAZEOPEN}
# ascenseurs (EV_DoPlat downWaitUpStay / blazeDWUS) : course = plus bas sol voisin (p_plats.c)
LIFT_W = {10, 88, 120, 121}
LIFT_S = {21, 62, 122, 123}
# sols qui DESCENDENT (EV_DoFloor) : un aller -> OT_DOOM_FLOOR de course negative, a FLOORSPEED
# (1 u/tic, vitesse 8 en 1/8 u) ou turbo x4 (32). L'ascenseur du moteur qu'ils prenaient avant
# (OT_STUCKDOWNELEVATOR) descendait a 5 u/tic sur le son de PowerSlave. 37/84 (lowerAndChange)
# descendent sans changer de flat.
FLOOR_W = {36: "floor_turbo", 98: "floor_turbo", 38: "floor_lowest", 82: "floor_lowest",
           37: "floor_lowest", 84: "floor_lowest", 83: "floor_highest"}
FLOOR_S = {70: "floor_turbo", 71: "floor_turbo", 23: "floor_lowest", 60: "floor_lowest",
           45: "floor_highest", 102: "floor_highest"}
FLOOR_SPEED = {"floor_turbo": 32, "floor_lowest": 8, "floor_highest": 8, "floor_donut": DONUT_SPEED}
# A_BossDeath (p_enemy.c) : la mort du dernier boss d'une carte agit sur un tag SANS ligne.
# carte -> [(tag, genre)] ; l'objet ecoute le canal = tag, DOOM_GAME.C (doom_bossDeath) le signale.
# E2M8 / E3M8 finissent la carte (pas de tag) ; MAP07 667 (raiseToTexture) n'y est pas.
BOSS_TAGS = {"E1M8": [(666, "floor_lowest")], "E4M8": [(666, "floor_lowest")],
             "E4M6": [(666, "door_blazeopen")], "MAP07": [(666, "floor_lowest")]}
EXIT_S = {11: CHANNEL_EXIT, 51: CHANNEL_SECRETEXIT}
EXIT_W = {52: CHANNEL_EXIT, 124: CHANNEL_SECRETEXIT}
# secteurs à dégâts : P_PlayerInSpecialSector (p_spec.c:1037-1059), hp toutes les 32 tics. 11 = fin
# d'episode (E1M8 : la salle ou l'on arrive par le teleporteur) : 20 hp et la carte se termine des
# que la sante passe a 10 ou moins -- le bit DAMAGE_EXIT du parametre hp de OT_DOOM_DAMAGE.
SECTOR_DAMAGE = {7: 5, 5: 10, 16: 20, 4: 20, 11: 20}
SECTOR_EXIT = {11}
DAMAGE_EXIT = 0x100
# Sols qui MONTENT, un aller (p_floor.c EV_DoFloor, p_plats.c EV_DoPlat ; declencheurs de
# P_CrossSpecialLine / P_UseSpecialLine). special -> (declencheur, destination, vitesse en 1/8 u
# par tic, prend le flat du secteur AVANT de la ligne, degats ensuite).
#   destination : "ceiling" = min(plus bas plafond voisin, son plafond) (raiseFloor) ; "nearest" =
#   plus bas sol voisin au-dessus du sien, sinon le sien (P_FindNextHighestFloor) ; "+24", "+32",
#   "+512" ; "texture" = + la plus petite texture basse de ses lignes a deux faces (raiseToTexture).
#   vitesse : 8 = FLOORSPEED (1 u/tic), 32 = turbo (x4), 4 = PLATSPEED/2 des plates-formes.
#   degats : None = inchanges ; 0 = effaces (raiseToNearestAndChange : `sec->special = 0`) ;
#   "front" = ceux du secteur avant (raiseFloor24AndChange : `sec->special = front->special`).
# Les ecraseurs (55, 56, 65, 94 : raiseFloorCrush) et les declencheurs au tir (24, 47) n'y sont pas.
RAISE = {
    5: ("W", "ceiling", 8, False, None), 91: ("W", "ceiling", 8, False, None),
    101: ("S", "ceiling", 8, False, None), 64: ("S", "ceiling", 8, False, None),
    119: ("W", "nearest", 8, False, None), 128: ("W", "nearest", 8, False, None),
    18: ("S", "nearest", 8, False, None), 69: ("S", "nearest", 8, False, None),
    130: ("W", "nearest", 32, False, None), 129: ("W", "nearest", 32, False, None),
    131: ("S", "nearest", 32, False, None), 132: ("S", "nearest", 32, False, None),
    22: ("W", "nearest", 4, True, 0), 95: ("W", "nearest", 4, True, 0),
    20: ("S", "nearest", 4, True, 0), 68: ("S", "nearest", 4, True, 0),
    58: ("W", "+24", 8, False, None), 92: ("W", "+24", 8, False, None),
    59: ("W", "+24", 8, True, "front"), 93: ("W", "+24", 8, True, "front"),
    15: ("S", "+24", 4, True, None), 66: ("S", "+24", 4, True, None),
    14: ("S", "+32", 4, True, None), 67: ("S", "+32", 4, True, None),
    140: ("S", "+512", 8, False, None),
}
# raiseToTexture (30, 96) n'y est pas : sa destination depend des HAUTEURS de texture (TEXTURE1),
# que specials_of ne voit pas -- et tous ses appelants doivent calculer la meme chose.
# ESCALIERS (EV_BuildStairs, p_floor.c) : des sols qui montent en chaine, chaque marche a SA
# destination (stair_steps). special -> (declencheur, hauteur de marche, vitesse en 1/8 u par tic) :
# build8 = FLOORSPEED/4 (2), turbo16 = FLOORSPEED*4 (32).
STAIRS = {7: ("S", 8, 2), 8: ("W", 8, 2), 127: ("S", 16, 32), 100: ("W", 16, 32)}
ML_TWOSIDED = 0x0004
# Teleporteurs (EV_Teleport) : W1 et WR. 125 / 126 ne prennent que les monstres (le moteur ne
# signale l'entree d'un secteur que pour la camera, SPRITE.C:731) : non convertis.
TELEPORT = {39: TELE_ONCE, 97: 0}

# Tous les objets emis ici qui ne sont PAS des mobjs positionnes (things2objects.object_positions,
# verif_doom) -- le joueur et les mobjs portent `sector, x, y, z`, eux non.
OT_SPECIAL_TYPES = frozenset((OT_NORMALDOOR, OT_NORMALELEVATOR, OT_STUCKDOWNELEVATOR, OT_SECTORSWITCH,
                              OT_DOOM_EXIT, OT_DOOM_SECRETEXIT, OT_DOOM_LIGHT, OT_DOOM_DAMAGE,
                              OT_DOOM_SECRETWALL, OT_DOOM_TELEPORT, OT_DOOM_FLOOR,
                              OT_DOOM_DOOR, OT_DOOM_LIFT, OT_DOOM_WLINE)) | frozenset(OT_SWITCH_TYPES)

Specials = namedtuple("Specials", "doors lifts floors raises teleports wswitch sswitch exits damage ignored")


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
    floor_highest : (min(max sol voisin, sol), sol)          lowerFloor
    raise_ceiling : (sol, min(plus bas plafond voisin, plafond))   raiseFloor
    raise_nearest : (sol, plus bas sol voisin au-dessus du sien, sinon le sien)   P_FindNextHighestFloor
    raise_+N      : (sol, sol + N)                           raiseFloor24 / 512, plates-formes +24 / +32"""
    S = M["sectors"]
    sec = S[sector]
    nbs = sorted(_neighbours(M).get(sector, ()))
    if kind.startswith("raise_"):
        regle = kind[len("raise_"):]
        if regle == "ceiling":
            return sec.floorh, min(min((S[n].ceilh for n in nbs), default=sec.ceilh), sec.ceilh)
        if regle == "nearest":
            return sec.floorh, min((S[n].floorh for n in nbs if S[n].floorh > sec.floorh),
                                   default=sec.floorh)
        return sec.floorh, sec.floorh + int(regle)
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


def stair_steps(M, tag, pas, occupes):
    """EV_BuildStairs rejoue a la lettre -> [(secteur, destination)].

    Pour chaque secteur du tag (P_FindSectorFromLineTag, par numero croissant) qui ne bouge pas deja :
    1re marche = son sol + `pas` ; puis, tant qu'il y en a une, la marche suivante est le secteur
    ARRIERE de la 1re ligne a deux faces (ordre des linedefs, P_GroupLines) dont le secteur AVANT est
    la marche courante et le flat de sol celui de la 1re marche -- destination + `pas` a chaque
    ligne qui passe ce test, MEME si son secteur arriere bouge deja (vanilla incremente avant de
    tester). La recherche des secteurs du tag reprend APRES la derniere marche (`secnum` y est
    reaffecte), comme dans Doom. `occupes` : secteurs deja mobiles (portes, ascenseurs, sols)."""
    L, SD, S = M["linedefs"], M["sidedefs"], M["sectors"]
    lignes = defaultdict(list)                 # sector->lines de P_GroupLines : ordre des linedefs
    for li, ld in enumerate(L):
        for sd in (ld.right, ld.left):
            if 0 <= sd < len(SD):
                s = SD[sd].sector
                if not lignes[s] or lignes[s][-1] != li:
                    lignes[s].append(li)
    tagged = sorted(i for i, s in enumerate(S) if s.tag == tag)
    pris = set(occupes)
    out = []
    secnum = -1
    while True:
        nxt = [i for i in tagged if i > secnum]
        if not nxt:
            break
        secnum = nxt[0]
        if secnum in pris:
            continue
        pris.add(secnum)
        hauteur = S[secnum].floorh + pas
        out.append((secnum, hauteur))
        flat = S[secnum].floorpic
        cur = secnum
        suite = True
        while suite:
            suite = False
            for li in lignes[cur]:
                ld = L[li]
                if not (ld.flags & ML_TWOSIDED) or not (0 <= ld.left < len(SD)):
                    continue
                if SD[ld.right].sector != cur:
                    continue
                t = SD[ld.left].sector
                if S[t].floorpic != flat:
                    continue
                hauteur += pas
                if t in pris:
                    continue
                pris.add(t)
                cur = secnum = t
                out.append((t, hauteur))
                suite = True
                break
    return out


def donut_sectors(M, tag):
    """EV_DoDonut rejoue -> [(s1, s2, s3)] pour les secteurs du tag, par numero croissant.

    s1->lines[0] et s2->lines[] sont dans l'ordre des linedefs (P_GroupLines) ; getNextSector rend
    None sur une ligne a une face (Doom s'arrete). LINE_BACKSECTOR est le secteur du sidedef
    GAUCHE de la ligne, meme quand c'est s2 lui-meme (vanilla le prend tel quel). Une ligne sans
    face arriere depasserait la memoire dans Doom (DonutOverrun) : non rejoue, le secteur saute."""
    L, SD = M["linedefs"], M["sidedefs"]
    lignes = defaultdict(list)
    for li, ld in enumerate(L):
        for sd in (ld.right, ld.left):
            if 0 <= sd < len(SD):
                sct = SD[sd].sector
                if not lignes[sct] or lignes[sct][-1] != li:
                    lignes[sct].append(li)
    out = []
    for s1 in sorted(i for i, s in enumerate(M["sectors"]) if s.tag == tag):
        if not lignes[s1]:
            continue
        ld = L[lignes[s1][0]]
        if not (ld.flags & ML_TWOSIDED) or not (0 <= ld.left < len(SD)):
            break                                 # getNextSector == NULL : Doom s'arrete
        a, b = SD[ld.right].sector, SD[ld.left].sector
        s2 = b if a == s1 else a
        for li in lignes[s2]:
            l2 = L[li]
            s3 = SD[l2.left].sector if 0 <= l2.left < len(SD) else None
            if s3 == s1:
                continue
            if s3 is not None:
                out.append((s1, s2, s3))
            break
    return out


def specials_of(M):
    """Inventaire des spéciaux d'une carte (namedtuple Specials, listes de dicts).

    doors   : {sector, lines, keyed, channel}       (channel -1 = manuelle, tag pour les portes à tag)
    lifts   : {sector, tag, kind 'lift'}
    floors  : {sector, tag, kind 'floor_*'}
    raises  : {sector, tag, kind 'raise_*', special, line, speed, flat, damage, lower, upper}
              (sols qui montent, RAISE ; `line` = la 1re ligne qui les vise, son secteur AVANT donne
              le flat et les degats des variantes « AndChange »)
    teleports : {line, tag, special, flags}         (TELEPORT ; destination et feuilles : special_objects)
    wswitch : {line, channel}                       lignes W -> OT_SECTORSWITCH (par feuille bordante)
    sswitch : {line, channel, special}              lignes S -> OT_SW1..4
    exits   : {line, channel, secret}
    damage  : {sector, hp}
    ignored : Counter des spéciaux non traités (scroll 48, lumières, escaliers...)"""
    sides, sects = M["sidedefs"], M["sectors"]
    doors, lifts, floors, wsw, ssw, exits, damage = [], [], [], [], [], [], []
    raises, teleports = [], []
    ignored = defaultdict(int)
    by_tag = defaultdict(list)
    for si, s in enumerate(sects):
        if s.tag:
            by_tag[s.tag].append(si)
        if s.special in SECTOR_DAMAGE:
            damage.append(dict(sector=si, hp=SECTOR_DAMAGE[s.special], special=s.special,
                               exit=s.special in SECTOR_EXIT))
        elif s.special:
            ignored["secteur %d" % s.special] += 1
    door_by_sector = {}
    seen_mobile = set()

    def add_mobile(lst, tag, kind, **extra):
        for si in by_tag.get(tag, ()):
            if si in seen_mobile:
                continue
            seen_mobile.add(si)
            lst.append(dict(sector=si, tag=tag, kind=kind, **extra))

    def door_of(si):
        d = door_by_sector.get(si)
        if d is None:
            d = dict(sector=si, lines=[], keyed=False, channel=CHANNEL_NONE, kind="door",
                     manual=0, key=0, tagged=0)
            door_by_sector[si] = d
            doors.append(d)
            seen_mobile.add(si)
        return d

    def door_tag(si, tag, genre, li):
        # un tag s'ajoute a l'appui : la porte manuelle ne perd pas son appui, et prend le canal
        d = door_of(si)
        if d["channel"] == CHANNEL_NONE:
            d["channel"] = tag
        if not d["tagged"]:
            d["tagged"] = genre
        elif d["tagged"] != genre:
            ignored["porte a tag de deux genres (secteur %d : le 1er garde)" % si] += 1
        if li is not None:
            d["lines"].append(li)

    for li, ld in enumerate(M["linedefs"]):
        sp = ld.special
        if not sp:
            continue
        if sp in DOOR_MANUAL:
            if not (0 <= ld.left < len(sides)):
                ignored["porte %d sans face arrière" % sp] += 1
                continue
            si = sides[ld.left].sector
            d = door_of(si)
            genre, cle = DOOR_MANUAL_KIND[sp]
            if not d["manual"]:
                d["manual"], d["key"], d["keyed"] = genre, cle, bool(cle)
            elif (d["manual"], d["key"]) != (genre, cle):
                ignored["porte manuelle de deux genres (secteur %d : le 1er garde)" % si] += 1
            d["lines"].append(li)
        elif sp in DOOR_TAGGED_W or sp in DOOR_TAGGED_S:
            genre = DOOR_TAGGED_W.get(sp) or DOOR_TAGGED_S[sp]
            for si in by_tag.get(ld.tag, ()):
                door_tag(si, ld.tag, genre, li)
            (wsw if sp in DOOR_TAGGED_W else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in LIFT_W or sp in LIFT_S:
            add_mobile(lifts, ld.tag, "lift",
                       speed=LIFT_SPEED_BLAZE if sp in LIFT_BLAZE else LIFT_SPEED)
            (wsw if sp in LIFT_W else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in FLOOR_W or sp in FLOOR_S:
            add_mobile(floors, ld.tag, FLOOR_W.get(sp) or FLOOR_S[sp])
            (wsw if sp in FLOOR_W else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in EXIT_S or sp in EXIT_W:
            ch = EXIT_S.get(sp) or EXIT_W[sp]
            exits.append(dict(line=li, channel=ch, secret=(ch == CHANNEL_SECRETEXIT)))
            (ssw if sp in EXIT_S else wsw).append(dict(line=li, channel=ch, special=sp))
        elif sp in RAISE:
            trig, regle, vit, flat, degats = RAISE[sp]
            for si in by_tag.get(ld.tag, ()):
                if si in seen_mobile:
                    continue
                seen_mobile.add(si)
                raises.append(dict(sector=si, tag=ld.tag, kind="raise_" + regle, special=sp,
                                   line=li, speed=vit, flat=flat, damage=degats))
            (wsw if trig == "W" else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in STAIRS:
            trig, pas, vit = STAIRS[sp]
            for si, dest in stair_steps(M, ld.tag, pas, seen_mobile):
                seen_mobile.add(si)
                raises.append(dict(sector=si, tag=ld.tag, kind="raise_stair", special=sp,
                                   line=li, speed=vit, flat=False, damage=None, dest=dest))
            (wsw if trig == "W" else ssw).append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in DONUT:
            SS = M["sectors"]
            for s1, s2, s3 in donut_sectors(M, ld.tag):
                if s1 in seen_mobile or s2 in seen_mobile:
                    ignored["donut sur un secteur deja mobile (%d / %d)" % (s1, s2)] += 1
                    continue
                seen_mobile |= {s1, s2}
                floors.append(dict(sector=s1, tag=ld.tag, kind="floor_donut", dest=SS[s3].floorh))
                raises.append(dict(sector=s2, tag=ld.tag, kind="raise_donut", special=sp, line=li,
                                   speed=DONUT_SPEED, flat=True, donor=s3, damage=0,
                                   dest=SS[s3].floorh))
            ssw.append(dict(line=li, channel=ld.tag, special=sp))
        elif sp in TELEPORT:
            teleports.append(dict(line=li, tag=ld.tag, special=sp, flags=TELEPORT[sp]))
        else:
            ignored["ligne %d" % sp] += 1
    # A_BossDeath : un tag qu'aucune ligne ne vise, que la mort du dernier boss declenche
    for tag, genre in BOSS_TAGS.get(M.get("name"), ()):
        if genre == "floor_lowest":
            add_mobile(floors, tag, "floor_lowest")
        elif genre == "door_blazeopen":
            for si in by_tag.get(tag, ()):
                door_tag(si, tag, PORTE_BLAZEOPEN, None)
    for d in doors:
        d["lower"], d["upper"] = mobile_bounds(M, d["sector"], "door")
    for l_ in lifts + floors:
        if "dest" in l_:                      # le trou du donut : au sol de s3
            l_["lower"] = min(l_["dest"], sects[l_["sector"]].floorh)
            l_["upper"] = sects[l_["sector"]].floorh
        else:
            l_["lower"], l_["upper"] = mobile_bounds(M, l_["sector"], l_["kind"])
    # Un sol qui monte jusqu'a son PLAFOND fermerait son secteur : le quad de chaque mur serait
    # plat, donc rejete (Emitter.add_wall) -- il s'arrete a la fente, comme une porte fermee.
    # Un sol deja a destination (E1M5 et E1M7 : 91 sur un sol colle au plafond voisin, Doom ne le
    # bouge pas non plus) n'a pas d'objet, et ses declencheurs non plus.
    immobiles = set()
    for r in raises:
        if "dest" in r:                       # marche d'escalier, anneau du donut : destination donnee
            lo, hi = sects[r["sector"]].floorh, r["dest"]
        else:
            lo, hi = mobile_bounds(M, r["sector"], r["kind"])
        r["lower"], r["upper"] = lo, min(hi, sects[r["sector"]].ceilh - DOOR_SLIT)
        r["fente"] = r["upper"] != hi
        if r["upper"] <= r["lower"]:
            immobiles.add(r["sector"])
            ignored["sol qui monte deja a destination (%d)" % r["special"]] += 1
    if immobiles:
        raises = [r for r in raises if r["sector"] not in immobiles]
        vivantes = {r["tag"] for r in raises}
        monte = set(RAISE) | set(STAIRS)
        wsw = [w for w in wsw if w["special"] not in monte or w["channel"] in vivantes]
        ssw = [s for s in ssw if s["special"] not in monte or s["channel"] in vivantes]
    # de meme un sol qui descend deja au plus bas : ni objet, ni declencheur
    bas = {f["sector"] for f in floors if f["upper"] <= f["lower"]}
    if bas:
        for f in floors:
            if f["sector"] in bas:
                ignored["sol qui descend deja en bas (secteur %d)" % f["sector"]] += 1
        floors = [f for f in floors if f["sector"] not in bas]
        vivantes = {f["tag"] for f in floors}
        descend = set(FLOOR_W) | set(FLOOR_S)
        wsw = [w for w in wsw if w["special"] not in descend or w["channel"] in vivantes]
        ssw = [s for s in ssw if s["special"] not in descend or s["channel"] in vivantes]
    return Specials(doors, lifts, floors, raises, teleports, wsw, ssw, exits, damage, dict(ignored))


def mobile_sectors(sp):
    """{secteur Doom: dict du spécial} pour tout ce qui bouge (portes, ascenseurs, sols)."""
    out = {}
    for d in sp.doors + sp.lifts + sp.floors + sp.raises:
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


def teleport_targets(M, conv, tp):
    """-> (feuilles qui declenchent, destination) d'un teleporteur de ligne, ou (feuilles, None).

    DECLENCHE : EV_Teleport ne part que si l'on franchit la ligne depuis sa face AVANT (`if (side
    == 1) return` -- on ressort d'un teleporteur sans repartir), donc on arrive dans le secteur
    ARRIERE. Le moteur signale l'entree d'une FEUILLE (SIGNAL_ENTER, SPRITE.C:731) : ce sont les
    morceaux du secteur arriere qui bordent la ligne.
    DESTINATION : pour chaque secteur du tag, par numero croissant (P_FindSectorFromLineTag), le
    premier MT_TELEPORTMAN (DoomEd 14) de la liste des things qui s'y trouve (p_telept.c). Arrivee
    au sol du secteur, angle du thing ; le brouillard d'arrivee est 20 u devant."""
    L, SD, S = M["linedefs"], M["sidedefs"], M["sectors"]
    ld = L[tp["line"]]
    if not (0 <= ld.left < len(SD)):
        return [], None
    back = SD[ld.left].sector
    feuilles = sorted(conv.remap[l_] for l_ in leaves_on_line(conv, tp["line"])
                      if conv.leaf_sector[l_] == back)
    for si in sorted(i for i, s in enumerate(S) if s.tag == tp["tag"]):
        for t in M["things"]:
            if t.type != 14:
                continue
            lf = conv.leaf_at(t.x, t.y)
            if lf in conv.remap and conv.leaf_sector[lf] == si:
                a = math.radians(t.angle)
                return feuilles, dict(sector=conv.remap[lf], doom_sector=si, x=t.x, z=t.y,
                                      angle=int(round((t.angle % 360) * 4096.0 / 360.0)) % 4096,
                                      fog=(int(round(t.x + 20 * math.cos(a))),
                                           int(round(t.y + 20 * math.sin(a)))))
    return feuilles, None


def floor_donor_face(M, geom, line, sector, front=None):
    """Une face de SOL du secteur avant de `line` dans la geometrie emise (G) : le flat qu'un sol
    « AndChange » prend au depart (`sec->floorpic = front->floorpic`) ; `front` impose le secteur
    donneur (s3 du donut). -1 si ce secteur EST le sol qui monte (rien a changer), -2 si aucune face
    n'a ete trouvee."""
    if front is None:
        front = M["sidedefs"][M["linedefs"][line].right].sector
    if front == sector:
        return -1
    W, S = geom["walls"], geom["sectors"]
    for s, ds in enumerate(geom["doom_sector"]):
        if ds != front:
            continue
        for wi in range(S[s]["firstWall"], S[s]["lastWall"] + 1):
            if W[wi]["normal"][1] > 0 and W[wi]["firstFace"] >= 0:
                return W[wi]["firstFace"]
    return -2


def special_objects(M, conv, ids, specials, pb_index, *, lift_contact=False, switches=None,
                    exit_channel=None, secret_walls=None, geom=None):
    """Objets des spéciaux -> (objects [{type, firstParam, ...}], params bytearray).

    `pb_index`  : {secteur Doom: numéro de push block} (sortie `mobile.pb_index` de doom3d --mobile).
    `switches`  : liste `mobile.switches` de doom3d (interrupteurs S : feuille, orifice, type OT_SW1..4).
    `lift_contact` : ascenseurs WR en `channel = -1` (contact/press, AI.C:4593-4624), sans sector-switch.
    `secret_walls` : murs DOORWALL des lignes ML_SECRET (`mobile.secret_walls` de doom3d) -> un
                    OT_DOOM_SECRETWALL (1 short) chacun (p_switch.c : un monstre ne presse pas ML_SECRET).
    `geom`      : la geometrie emise (doom3d) -- faces des flats que prennent les sols « AndChange ».
    Ordre d'émission : portes, ascenseurs, sols, sols qui montent, teleporteurs, lignes W,
    interrupteurs, sorties, dégâts, murs secrets."""
    objects, params = [], bytearray()
    notes = defaultdict(int)
    pb_index = {int(k): int(v) for k, v in (pb_index or {}).items()}   # clés str depuis le JSON

    def emit(ot, *vals, **extra):
        o = dict(type=ot, firstParam=len(params))
        o.update(extra)
        objects.append(o)
        params.extend(pack_params(*vals))

    # portes : pb, channel, doorHeight, genre de l'appui, cle, genre du tag (OT_DOOM_DOOR). Le
    # fichier ferme la porte a sol + DOOR_SLIT (close_doors) et l'objet monte de doorHeight depuis
    # l'etat du FICHIER : doorHeight = regle de Doom (plus bas plafond voisin - 4) - plafond de
    # depart. Une porte que le WAD laisse ENTROUVERTE n'est pas refermee (close_doors) : elle part de
    # son plafond, pas de sol + fente -- MESURE 2026-09-18, E1M5 secteur 121 (plafond a sol + 8)
    # s'ouvrait 7 u trop haut.
    for d in specials.doors:
        pb = pb_index.get(d["sector"])
        if pb is None:
            notes["porte sans push block"] += 1
            continue
        depart = max(M["sectors"][d["sector"]].ceilh, d["lower"] + DOOR_SLIT)
        emit(OT_DOOM_DOOR, pb, d["channel"], d["upper"] - depart, d["manual"], d["key"],
             d["tagged"], sector_doom=d["sector"], kind="door")
    # ascenseurs : OT_DOOM_LIFT pb, course, canal, vitesse, attente (--lift-contact : l'ascenseur
    # du moteur, pb, lower, upper, canal -1)
    for l_ in specials.lifts:
        pb = pb_index.get(l_["sector"])
        if pb is None:
            notes["ascenseur sans push block"] += 1
            continue
        if lift_contact:
            emit(OT_NORMALELEVATOR, pb, l_["lower"], l_["upper"], CHANNEL_NONE,
                 sector_doom=l_["sector"], kind="lift")
        else:
            emit(OT_DOOM_LIFT, pb, l_["lower"] - l_["upper"], l_["tag"], l_["speed"], LIFT_WAIT,
                 sector_doom=l_["sector"], kind="lift")
    # sols qui descendent : OT_DOOM_FLOOR de course NEGATIVE (emis a l'etat du WAD, en haut ; l'objet
    # descend de la course quand son canal sonne) -- vitesse et sons de Doom (T_MoveFloor)
    for f in specials.floors:
        pb = pb_index.get(f["sector"])
        if pb is None:
            notes["sol sans push block"] += 1
            continue
        emit(OT_DOOM_FLOOR, pb, f["lower"] - f["upper"], f["tag"], FLOOR_SPEED[f["kind"]], -1, -1,
             sector_doom=f["sector"], kind=f["kind"])
    # sols qui montent : pb, course, canal, vitesse, face du nouveau flat, degats ensuite. La
    # geometrie est emise EN HAUT (doom3d.raise_floors) et l'objet la descend de la course au
    # chargement : le niveau demarre dans l'etat du WAD.
    S = M["sectors"]
    for r in specials.raises:
        pb = pb_index.get(r["sector"])
        if pb is None:
            notes["sol qui monte sans push block"] += 1
            continue
        donor = -1
        if r["flat"]:
            donor = floor_donor_face(M, geom, r["line"], r["sector"], r.get("donor")) if geom else -2
            if donor == -2:
                notes["sol qui monte : flat du secteur avant introuvable (garde le sien)"] += 1
                donor = -1
        degats = r["damage"]
        if degats == "front":
            degats = SECTOR_DAMAGE.get(S[M["sidedefs"][M["linedefs"][r["line"]].right].sector].special, 0)
        emit(OT_DOOM_FLOOR, pb, r["upper"] - r["lower"], r["tag"], r["speed"], donor,
             -1 if degats is None else degats, sector_doom=r["sector"], kind=r["kind"],
             line=r["line"])
    # teleporteurs : une feuille qui declenche = un objet (level_sector[s].object, SIGNAL_ENTER)
    taken = {}
    for tp in specials.teleports:
        feuilles, dest = teleport_targets(M, conv, tp)
        if dest is None:
            notes["teleporteur sans MT_TELEPORTMAN dans son tag (ligne %d)" % tp["line"]] += 1
            continue
        if not feuilles:
            notes["teleporteur sans feuille arriere (ligne %d)" % tp["line"]] += 1
            continue
        for s in feuilles:
            if s in taken:
                continue
            taken[s] = ("teleport", tp["line"])
            emit(OT_DOOM_TELEPORT, s, dest["sector"], dest["x"], dest["z"], dest["angle"],
                 dest["fog"][0], dest["fog"][1], tp["flags"], line=tp["line"], kind="teleport",
                 dest_doom=dest["doom_sector"])
    # declencheurs W : un OT_DOOM_WLINE par ligne, ses deux bouts (le franchissement, pas la feuille)
    lift_tags = {l_["tag"] for l_ in specials.lifts}
    V, L = M["vertices"], M["linedefs"]
    for w in specials.wswitch:
        if lift_contact and w["channel"] in lift_tags and w["special"] in LIFT_W:
            continue
        ld = L[w["line"]]
        (x1, z1), (x2, z2) = V[ld.v1], V[ld.v2]
        emit(OT_DOOM_WLINE, x1, z1, x2, z2, w["channel"],
             WLINE_ONCE if w["special"] in W_ONCE else 0, line=w["line"], kind="wswitch")
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
            emit(OT_DOOM_DAMAGE, s, dm["hp"] | (DAMAGE_EXIT if dm.get("exit") else 0),
                 sector_doom=dm["sector"], kind="damage")
    # murs secrets : un par mur DOORWALL d'une ligne ML_SECRET
    for w in sorted(int(x) for x in (secret_walls or ())):
        emit(OT_DOOM_SECRETWALL, w, kind="secretwall")
    return objects, params, dict(notes)


def expected_param_bytes(objects):
    """Σ des params attendus par type (DOOM_ABI « Vérifications PC ») : 5 joueur, 6 mobj, 6 porte
    Doom (3 porte du moteur), 4 ascenseur, 2 sector-switch, 5 interrupteur, 1 sortie, 2 dégâts,
    8 teleporteur, 6 sol -- en octets."""
    n = 0
    for o in objects:
        t = o["type"]
        if t == OT_PLAYER:
            n += 5
        elif t == OT_DOOM_TELEPORT:
            n += 8
        elif t == OT_DOOM_FLOOR:
            n += 6
        elif t == OT_DOOM_DOOR:
            n += 6
        elif t == OT_NORMALDOOR:
            n += 3
        elif t in (OT_NORMALELEVATOR, OT_STUCKDOWNELEVATOR):
            n += 4
        elif t == OT_DOOM_LIFT:
            n += 5
        elif t == OT_DOOM_WLINE:
            n += 6
        elif t == OT_SECTORSWITCH:
            n += 2
        elif t in OT_SWITCH_TYPES:
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
