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
import gridparts                                       # noqa: E402

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
# LUMIERES DE LIGNE (p_lights.c EV_LightTurnOn) : 3 params = feuille, canal, lumiere 0..16. Doom
# n'anime pas ces specialites, il AFFECTE un niveau aux secteurs du tag. Une par FEUILLE, comme
# les secteurs a degats. L'episode 1 n'en porte qu'une sorte -- `35`, trois lignes autour de la
# clef bleue d'E1M3 (tag 13) : la lumiere tombe quand on la prend. Les autres (12, 13, 80, 81,
# 104, 138, 139) n'existent nulle part dans l'episode et ne sont donc PAS emises : leur cible se
# lit sur les voisins au moment du declenchement, ce qu'on ne saurait figer sans mentir.
OT_DOOM_LIGHT = 178
LIGHT_LINES = {35: 35}        # special -> lightlevel Doom a poser sur les secteurs du tag
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
# LIGNES G (P_ShootSpecialLine) : la ligne s'ouvre quand une BALLE la touche, pas quand on la
# franchit. Une seule dans l'episode 1 -- E1M2 linedef 572, « GR open door » tag 6, le placard de
# la tronconneuse -- et elle n'etait pas convertie du tout : le secteur 188 n'etait enregistre que
# comme porte MANUELLE, sans canal, donc rien ne pouvait l'ouvrir. Meme objet que les lignes W,
# avec un drapeau : DOOM_GAME.C la saute au franchissement et la teste au tir.
WLINE_GUN = 2
DOOR_TAGGED_G = {46: None}    # genre rempli plus bas (PORTE_OUVERTE), les constantes suivent
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
DOOR_TAGGED_G[46] = PORTE_OUVERTE            # 46 = GR open door, au tir : reste ouverte
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
# LES SECRETS (special 9, P_PlayerInSpecialSector « secret_count++ ; sector->special = 0 »). Ils
# passent par le MEME enregistrement que les degats -- le moteur tient deja un octet par feuille --
# mais avec le bit DAMAGE_SECRET dans hp et, dessous, le NUMERO du secret dans l'ordre du WAD. Le
# moteur en tire le total (mpTotal[2]) sans que la carte ait a le lui dire, et efface d'un coup
# toutes les feuilles qui portent le meme numero : un secteur decoupe en dix feuilles compte UNE
# fois, comme le `sector->special = 0` de Doom. 128 secrets par carte au plus (E1M5 en a 9).
SECTOR_SECRET = 9
DAMAGE_SECRET = 0x200
SECRETS_MAX = 128
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
# LAMPES (ajout : Doom n'a aucune lumiere dynamique). Une source que la CARTE recoit, pas un
# special du WAD : elle eclaire ce que le WAD laisse dans le noir. Par carte, une liste de :
#   x, y       plan Doom (moteur : X = x, Z = y) ; la feuille qui les contient doit etre du `secteur`
#              Doom annonce -- verifie a l'emission, une coquille ne pose pas la lampe ailleurs ;
#   hauteur    Y absolu (celui des sols), strictement entre le sol et le plafond du secteur ;
#   canal      -1 = allumee au chargement ; un tag = allumee quand il sonne, et pour de bon ;
#   teinte     k 0..16 par canal, `rayon` en u, `intensite` 0..31 au centre (addLightEx, WALLS.C) ;
#   montee     tics jusqu'a l'intensite pleine une fois allumee (0 = d'un coup).
# 20 params : feuille, x, hauteur, y, canal, r, g, b, rayon, intensite, montee (DOOM_GAME.C), puis
# le nombre de feuilles VUES et ces feuilles (LAMPE_VUS_MAX, -1 au-dela) : une par secteur que la
# lampe eclaire (secteurs_eclaires). Le moteur eteint la lampe quand aucun joueur ne peut voir
# aucune d'elles (REJECT) -- quelle que soit la direction du regard : un test a l'image la
# rallumerait 1 a 2 images trop tard quand on se retourne (updateLights, en fin de drawWalls).
# E1M3 -- la salle de la clef bleue : prendre la clef franchit les lignes 1017-1019 (35, tag 13 ->
# secteurs 26, 27 et 28 a 35, soit light_of 0 : le noir) puis 733-735 (2, tag 11 -> la porte 30
# s'ouvre sur le placard aux imps, secteur 29 : lumiere 160 -> 14, plafond TLITE6_5 rouge). Le
# placard reste eclaire, la salle non. La lampe est DANS le placard, a 4 u de la porte (y -632),
# au milieu en x (-320..-208) et en hauteur (sol 96, plafond 176) ; allumee par le canal de la
# PORTE et montant avec elle : 172 - 97 = 75 u a 2 u/tic, 38 tics. Pas avant : une lumiere du
# moteur traverse les murs (buildLightList ne teste que le plan du mur), elle peindrait le sol
# devant une porte encore fermee, dans une salle a 14 qu'elle pousserait vers 31. Teinte : un
# orange a mi-teinte (30 degres) entre 16, 9, 7 -- le rouge du plafond TLITE6_5, trop rouge a
# l'ecran -- et 16, 14, 6, trop jaune (Ymir, 2026-09-22).
# MESURE (build/doom2ps/e1m3_geom3d.json, lightApply rejoue) : au sol devant la porte +20 en rouge,
# +13 a 76 u de la lampe, +7 a 108 u, +2 a 140 u ; ce qui passe les murs (le moteur n'occulte rien) :
# +11 au sol derriere le mur est du placard (feuille 69, la meme salle, noire), +5 au bord est de la
# salle 25 (a 120 u, derriere le mur ouest), +1 dans le couloir 26.
OT_DOOM_LAMP = 186
# ENREGISTREMENTS DE LONGUEUR VARIABLE (un par carte au plus, emis en DERNIER : effets puis lampes).
# Regle unique : leur 1er short est leur propre longueur en shorts, en-tete compris (nShorts) ;
# le moteur les lit en place (OBJECT.C suckParams), verif les relit sans extras.
# ANIMATIONS DE LUMIERE DES SECTEURS (p_lights.c) : UN SEUL enregistrement par carte, de longueur
# variable. Un objet par feuille etait le reflexe -- Doom donne un penseur a chaque secteur -- mais
# E1M6 a 161 feuilles animees pour 4 objets de marge dans sa reserve (544 slots pour les monstres,
# les ramassables et les speciaux d'une carte). Le moteur parcourt donc l'enregistrement une fois
# par tic et y ECRIT l'etat de chaque effet, la ou il est (level_objectParams).
#   [0] nShorts (l'enregistrement entier), [1] nmFx, puis par effet 9 shorts + ses feuilles :
#   0 genre (FX_*)  1 sombre  2 clair (0..16)  3 temps sombre du strobe   <- le convertisseur
#   4 compte (= la phase)  5 niveau (= clair)  6 sens (= -1)  7 graine     <- l'etat, ecrit par
#   8 nombre de feuilles, puis les feuilles                                   le moteur
# Les feuilles d'un meme secteur Doom sont dans la MEME entree : elles ne peuvent pas se
# desynchroniser. Niveaux : `clair` = la lumiere du secteur, `sombre` = P_FindMinSurroundingLight
# (le plus sombre des voisins, borne par la sienne) ; le strobe met sombre a 0 quand les deux sont
# egales, le feu (17) ajoute 16 au sombre. Tout passe par doom3d.light_of.
OT_DOOM_SECTORFX = 187
FX_ENTETE = 9                    # shorts d'une entree avant ses feuilles
# LES FLAQUES (OT_DOOM_LAMPS) : UN gestionnaire par carte, qui porte UNE lumiere verte suivant la
# flaque la plus proche que le joueur peut voir. Une lampe par bassin etait la premiere idee : E1M6,
# la carte qui a le plus de nukage, n'a que 3 objets de marge sur les 541 du moteur, et huit lampes
# l'ont fait deborder. Un gestionnaire coute UN objet et UN emplacement de lumiere, quel que soit le
# nombre de flaques.
#   [0] nShorts  [1] nmPools  [2..4] r, g, b  [5] intensite  [6] battement en tics
#   puis par flaque 6 shorts + ses feuilles vues :
#   0 x  1 hauteur  2 y  3 rayon  4 feuille  5 nombre de feuilles vues, puis les feuilles
OT_DOOM_LAMPS = 188
POOL_ENTETE = 10                 # shorts d'une flaque avant ses feuilles
POOL_TETE = 7                    # shorts d'en-tete de l'enregistrement (teinte comprise)
OT_LONGUEUR_VARIABLE = (OT_DOOM_SECTORFX, OT_DOOM_LAMPS)
FX_FLASH, FX_STROBE, FX_GLOW, FX_FLICKER = 1, 2, 3, 4
FX_SYNC = 0x80                   # strobe des speciaux 12/13 : premier compte 1 (P_SpawnStrobeFlash)
FASTDARK, SLOWDARK = 15, 35
# special de secteur Doom -> (genre | FX_SYNC, temps sombre du strobe) ; P_SpawnSpecials
# LES FLAQUES qui recoivent un glow (voir special_objects). Ce sont les flats ANIMES de liquide
# de p_spec.c animdefs[], et rien d'autre : l'eau (FWATER, SWATER) ne brille pas, la roche (RROCK)
# non plus, et ATTENTION -- SLIME01 a SLIME12 de Doom 2 sont bien des liquides mais SLIME13 a
# SLIME16 sont du metal rouille, qu'un simple prefixe ferait briller. D'ou la liste exacte. C'est
# ici qu'on ajoute ce qu'apportent Doom 2, TNT et Plutonia.
# LES SOLS SPECIAUX, DEDUITS. Le WAD dit "ce sol n'est pas ordinaire" de deux facons, et deux
# seulement : le flat appartient a une FAMILLE ANIMEE (p_spec.c P_InitPicAnims -- la table est
# celle de Doom, pas une liste de gout), ou le secteur qu'il couvre porte un SPECIAL A DEGATS
# (P_PlayerInSpecialSector). La couleur tranche ensuite : on mesure la saturation de la couleur
# MOYENNE du flat dans PLAYPAL, et sous SATURATION_MIN on laisse tomber -- un liquide qui ne se
# distingue pas par la couleur n'a rien a teinter, et un sol brun sature (FLOOR7_1 : 0.61) n'est
# pas un liquide pour autant, c'est l'animation ou les degats qui le disent.
# La carte prend la teinte de son sol special le PLUS ETENDU : une seule par niveau, parce que la
# rampe du monde n'a qu'une famille de bandes (UTIL.H).
SECTEURS_DEGATS = frozenset((4, 5, 7, 11, 16))
SATURATION_MIN = 0.35
TEINTE_ADOUCIT = 0.75   # part du chemin vers le blanc : la couleur BRUTE d'un flat est bien trop
                         # forte sur un mur (le nukage sort a k = 8, 16, 5 et peint la salle en
                         # vert pomme -- mesure a l'ecran). Adoucie, elle donne 13, 16, 12, ce
                         # qui est la teinte reglee a la main et acceptee le 2026-09-23.
FLATS_ANIMES = frozenset(["NUKAGE1", "NUKAGE2", "NUKAGE3",       # Doom et Doom 2
                          "LAVA1", "LAVA2", "LAVA3", "LAVA4",    # Doom 2
                          "BLOOD1", "BLOOD2", "BLOOD3"]          # Doom 2
                         + ["SLIME%02d" % i for i in range(1, 13)]    # Doom 2 : 01-12 SEULEMENT
                         + ["FWATER%d" % i for i in range(1, 5)]
                         + ["SWATER%d" % i for i in range(1, 5)]
                         + ["RROCK%02d" % i for i in range(5, 9)])


def _saturation(W, flat):
    """(saturation, (r, g, b) en seiziemes) de la couleur moyenne d'un flat."""
    pal = W.playpal(0)
    acc = [0, 0, 0]
    try:
        d = W.lump(flat)
    except Exception:
        return 0.0, (16, 16, 16)       # la table animee est celle de DOOM 2 : le WAD peut ne pas
    for px in d:                       # avoir le flat, et alors il n'est pas special ici
        c = pal[px]
        for i in range(3):
            acc[i] += c[i]
    m = max(acc)
    if not m:
        return 0.0, (16, 16, 16)
    sat = (m - min(acc)) / float(m)
    k = [16.0 * v / m for v in acc]
    return sat, tuple(max(0, min(16, int(round(x + (16.0 - x) * TEINTE_ADOUCIT)))) for x in k)


def flats_speciaux(M, W=None):
    """{nom de flat} : les sols que le WAD signale comme pas ordinaires, et dont la couleur vaut
    la peine d'etre montree. `W` absent (verif, tests) : la table animee seule."""
    out = set(FLATS_ANIMES)
    for s in M["sectors"]:
        if s.special in SECTEURS_DEGATS:
            out.add(s.floorpic)
    if W is None:
        return out
    return {f for f in out if _saturation(W, f)[0] >= SATURATION_MIN}


def teinte_de_carte(M, conv, W):
    """(r, g, b) en seiziemes : la teinte du sol special le plus etendu de la carte."""
    import doom3d
    best, teinte = 0.0, (WORLDTINT_DEFAUT)
    aires = defaultdict(float)
    for si, s in enumerate(M["sectors"]):
        if s.floorpic in flats_speciaux(M, W):
            aires[s.floorpic] += abs(doom3d._perimetre_aire(M, si)[1])
    for f, a in aires.items():
        if a > best:
            best, teinte = a, _saturation(W, f)[1]
    return teinte


WORLDTINT_DEFAUT = (13, 16, 11)   # UTIL.H : le vert du nukage, si la carte ne dit rien
# LE VERT N'EST PAS UNE LUMIERE. Il l'a ete deux disques de suite et c'etait faux les deux fois :
# une lumiere du moteur est une tache, elle prend un des quinze slots, et il fallait faire bouger
# son intensite pour que la flaque vive, ce qui se voit comme un scintillement (a l'ecran,
# 2026-09-23). On CUIT donc le vert dans le niveau :
#   - la SALLE de chaque flaque (salle_de_flaque : le liquide et ce qui le borde) porte
#     SECFLAG_NUKAGE, et le moteur fait lire a ses MURS et a ses PLAFONDS -- pas a ses sols -- la
#     rampe VERTE du monde au lieu de la neutre (WALLS.C getLight, UTIL.C worldGreen). C'est la
#     seule facon d'avoir une couleur : la lumiere d'un sommet est un scalaire, c'est la RAMPE
#     qu'il traverse qui en fait un mot de couleur. Cout a l'execution : nul.
#   - une SOURCE au centre de chaque flaque eclaircit la lumiere des sommets autour d'elle
#     (doom3d.cuire_nukage, NUKAGE_CIBLE crans) : le relief, cuit lui aussi.
#   - il ne reste au gestionnaire qu'a faire RESPIRER la salle : tous les NUKAGE_PULSE tics un pas
#     d'un cran vers une cible a +/- NUKAGE_AMP de la lumiere de la salle, et seulement tant qu'un
#     joueur est assez pres et peut voir une de ses feuilles (sinon la salle est rendue telle
#     quelle). Ca varie lentement, globalement, et ca ne scintille pas.
NUKAGE_PULSE = 24                # tics entre deux pas de la respiration
NUKAGE_AMP = 2                   # crans (0..16) au-dessus et au-dessous de la lumiere de la salle
NUKAGE_FEUILLES = 8              # feuilles au plus par flaque (DOOM_GAME.C DOOM_LAMP_SEEN)
NUKAGE_BORD = 96                 # u : un voisin du liquide fait partie de la SALLE si son sol
                                 # n'est pas plus haut que ca -- la berge, la passerelle, pas le
                                 # couloir qui remonte
NUKAGE_CIBLE = 3                 # crans ajoutes a la lumiere des sommets au centre de la flaque
NUKAGE_RAYON = (128, 320)        # rayon minimum et maximum
NUKAGE_AIRE_MIN = 4096           # u2 : sous une dalle de 64x64, pas de lumiere
NUKAGE_MAX = 8                   # flaques par carte, les plus grandes d'abord
# FUSION DES BASSINS. Un bassin, c'est des secteurs de liquide qui se TOUCHENT -- et un chemin qui
# traverse une salle coupe sa flaque en deux bassins qui ne se touchent plus. Le gestionnaire
# n'allumant que la plus proche, une moitie de la salle restait noire (vu a l'ecran, 2026-09-23).
# On fond donc les bassins dont les BORDS sont a moins de NUKAGE_FUSION l'un de l'autre : la salle
# redevient UNE flaque, la lumiere se pose sur son centre et son rayon couvre les deux.
NUKAGE_FUSION = 256             # u entre deux bords : au-dela ce sont deux salles
NUKAGE_RAYON_MAX = 448          # u : plafond du rayon d'une flaque fondue
SECTOR_FX = {1: (FX_FLASH, 0), 2: (FX_STROBE, FASTDARK), 3: (FX_STROBE, SLOWDARK),
             4: (FX_STROBE, FASTDARK), 8: (FX_GLOW, 0), 12: (FX_STROBE | FX_SYNC, SLOWDARK),
             13: (FX_STROBE | FX_SYNC, FASTDARK), 17: (FX_FLICKER, 0)}
LAMPE_VUS_MAX = 8                 # DOOM_GAME.C DoomLampObject.seen
# LAMPES DERIVEES. Il n'y a plus de liste ecrite a la main : la lampe se DEDUIT de la carte, par
# une regle etroite -- un PLACARD LUMINEUX, c'est-a-dire un secteur dont le plafond porte une dalle
# lumineuse (doom3d.LIGHT_FLATS), dont la lumiere est haute, et dont la seule entree est une PORTE
# A CANAL. Pourquoi cette regle et pas une autre : une dalle lumineuse qu'on voit depuis toujours
# n'a pas besoin d'une lumiere du moteur, `cuire_lumieres` lui cuit son halo une fois pour toutes ;
# ce que la cuisson ne sait pas faire, c'est APPARAITRE quand la porte s'ouvre. La lampe ne sert
# donc qu'a ca, et le canal qui l'allume est celui de la porte -- pas avant, car une lumiere du
# moteur traverse les murs (buildLightList ne teste que le plan du mur) et peindrait le sol devant
# une porte encore fermee. La montee suit la porte : sa course a VDOORSPEED (2 u par tic).
# E1M3 est la carte de l'episode 1 qui rentre dans la regle : la salle de la clef bleue s'eteint
# (lignes 1017-1019, tag 13) pendant que le placard aux imps, secteur 29, garde sa lumiere 160 et
# son plafond TLITE6_5, et s'ouvre par la porte 30 sur le canal 11. C'est exactement la lampe qui
# etait ecrite ici a la main jusqu'au 2026-09-23 : (-264, -628), hauteur 136, teinte (16, 11, 6),
# rayon 176, intensite 24, montee 38 tics -- la derivation doit la retrouver.
LAMPE_LUM_MIN = 144               # lumiere Doom du placard : en-dessous, ce n'est pas une lampe
LAMPE_ADOUCIT = 0.4               # part du chemin vers le blanc : la dalle TLITE6_5 est un rouge
                                  # pur (k = 16, 7, 7), trop rouge a l'ecran (Ymir, 22-09)
LAMPE_RAYON = (128, 320)          # bornes du rayon deduit de la taille du secteur
LAMPE_INTENSITE = 24              # sur 31, au centre
LAMPE_MAX = 2                     # lampes par carte : on reste avare
VDOORSPEED = 2                    # u par tic (p_doors.c)


def _teinte_de_dalle(W, flat):
    """k 0..16 par canal pour une dalle de plafond : sa couleur moyenne, normalisee sur son canal
    le plus fort, puis tiree de LAMPE_ADOUCIT vers le blanc -- sauf son canal le plus FAIBLE, qui
    reste ou il est. Une lampe qu'on adoucit partout devient blanche ; c'est son canal froid qui
    lui garde sa couleur, et a egalite c'est le plus BLEU qui reste bas : une lampe est chaude.
    TLITE6_5 donne (16, 11, 7), la teinte reglee a la main etait (16, 11, 6)."""
    pal = W.playpal(0)
    d = W.lump(flat)
    acc = [0, 0, 0]
    for px in d:
        c = pal[px]
        for i in range(3):
            acc[i] += c[i]
    m = max(acc) or 1
    k = [max(0, min(16, int(round(16.0 * v / m)))) for v in acc]
    bas = 2 - k[::-1].index(min(k))       # a egalite, le plus bleu
    out = [max(0, min(16, int(round(v + (16 - v) * LAMPE_ADOUCIT)))) for v in k]
    out[bas] = k[bas]
    return tuple(out)


def lampes_derivees(M, conv, specials, W):
    """Les placards lumineux de la carte -> les dict que l'emission attend (voir LAMPES_DERIVEES)."""
    import doom3d                                 # tardif : doom3d importe ce module
    S, SD = M["sectors"], M["sidedefs"]
    porte_de = {}
    for d in specials.doors:
        if d["channel"] >= 0:
            porte_de[d["sector"]] = d
    out = []
    for si, sec in enumerate(S):
        if sec.ceilpic not in doom3d.LIGHT_FLATS or sec.light < LAMPE_LUM_MIN:
            continue
        voisins = set()
        for ld in M["linedefs"]:
            if ld.right < 0 or ld.left < 0:
                continue
            a, b = SD[ld.right].sector, SD[ld.left].sector
            if a == si:
                voisins.add(b)
            elif b == si:
                voisins.add(a)
        if not voisins or not all(v in porte_de for v in voisins):
            continue                              # on n'y entre pas QUE par des portes a canal
        d = porte_de[min(voisins)]
        cr = doom3d._centre_rayon(M, si)
        if cr is None:
            continue
        _cx, _cy, r = cr
        # la lampe se pose au centre du plus grand MORCEAU du secteur, pas au centre du secteur :
        # un secteur en L a son centre dehors, et DOOM_GAME.C exige la feuille qu'on annonce
        # (E1M4 : (1733, 1661) tombait dans le secteur 80, pas dans le 78)
        best = None
        for lf in leaves_of_sector(conv, si):
            ring = conv.polys[lf]
            a_ = abs(gridparts.area2(ring)) / 2.0
            if best is None or a_ > best[0]:
                best = (a_, lf, sum(p[0] for p in ring) / len(ring),
                        sum(p[1] for p in ring) / len(ring))
        if best is None:
            continue
        cx, cy = best[2], best[3]
        if conv.leaf_at(cx, cy) != best[1]:
            continue                              # centre hors de son propre morceau : tant pis
        depart = max(S[d["sector"]].ceilh, d["lower"] + DOOR_SLIT)
        out.append(dict(x=int(round(cx)), y=int(round(cy)),
                        hauteur=(sec.floorh + sec.ceilh) // 2, secteur=si,
                        canal=d["channel"], teinte=_teinte_de_dalle(W, sec.ceilpic),
                        rayon=max(LAMPE_RAYON[0], min(LAMPE_RAYON[1], int(r) + 96)),
                        intensite=LAMPE_INTENSITE,
                        montee=max(0, (d["upper"] - depart) // VDOORSPEED),
                        aire=r * r))
    out.sort(key=lambda lp: -lp["aire"])
    return out[:LAMPE_MAX]

# Tous les objets emis ici qui ne sont PAS des mobjs positionnes (things2objects.object_positions,
# verif_doom) -- le joueur et les mobjs portent `sector, x, y, z`, eux non.
OT_SPECIAL_TYPES = frozenset((OT_NORMALDOOR, OT_NORMALELEVATOR, OT_STUCKDOWNELEVATOR, OT_SECTORSWITCH,
                              OT_DOOM_EXIT, OT_DOOM_SECRETEXIT, OT_DOOM_LIGHT, OT_DOOM_DAMAGE,
                              OT_DOOM_SECRETWALL, OT_DOOM_TELEPORT, OT_DOOM_FLOOR,
                              OT_DOOM_DOOR, OT_DOOM_LIFT, OT_DOOM_WLINE,
                              OT_DOOM_LAMP, OT_DOOM_SECTORFX, OT_DOOM_LAMPS)) | frozenset(OT_SWITCH_TYPES)

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
    damage  : {sector, hp}   (hp | DAMAGE_SECRET + numero = un secteur SECRET, special 9)
    ignored : Counter des spéciaux non traités (scroll 48, lumières, escaliers...)"""
    sides, sects = M["sidedefs"], M["sectors"]
    doors, lifts, floors, wsw, ssw, exits, damage = [], [], [], [], [], [], []
    raises, teleports = [], []
    nm_secrets = 0
    ignored = defaultdict(int)
    by_tag = defaultdict(list)
    for si, s in enumerate(sects):
        if s.tag:
            by_tag[s.tag].append(si)
        if s.special in SECTOR_DAMAGE:
            damage.append(dict(sector=si, hp=SECTOR_DAMAGE[s.special], special=s.special,
                               exit=s.special in SECTOR_EXIT))
        elif s.special == SECTOR_SECRET:
            assert nm_secrets < SECRETS_MAX, (si, nm_secrets)
            damage.append(dict(sector=si, hp=DAMAGE_SECRET | nm_secrets, special=s.special,
                               exit=False, secret=nm_secrets))
            nm_secrets += 1
        elif s.special in SECTOR_FX:
            pass                       # anime : emis par special_objects (OT_DOOM_SECTORFX)
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
        elif sp in DOOR_TAGGED_W or sp in DOOR_TAGGED_S or sp in DOOR_TAGGED_G:
            genre = (DOOR_TAGGED_W.get(sp) or DOOR_TAGGED_G.get(sp) or DOOR_TAGGED_S[sp])
            for si in by_tag.get(ld.tag, ()):
                door_tag(si, ld.tag, genre, li)
            # une ligne G part avec les lignes W : meme objet, distingue par WLINE_GUN
            (wsw if (sp in DOOR_TAGGED_W or sp in DOOR_TAGGED_G) else ssw).append(
                dict(line=li, channel=ld.tag, special=sp))
        elif sp in LIGHT_LINES:
            # rien de mobile : seule la ligne compte, les receveurs se posent par feuille
            wsw.append(dict(line=li, channel=ld.tag, special=sp))
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


def secteurs_animes(M, specials):
    """Secteurs Doom dont la lumiere change en jeu : un special de SECTOR_FX (p_lights.c), ou la
    cible d'une ligne de lumiere (LIGHT_LINES, secteurs du tag). Le moteur y reecrit la lumiere
    des feuilles : rien ne doit y etre cuit. -> frozenset d'index de secteurs Doom."""
    out = {si for si, s in enumerate(M["sectors"]) if s.special in SECTOR_FX}
    tags = {w["channel"] for w in specials.wswitch if w["special"] in LIGHT_LINES}
    out.update(si for si, s in enumerate(M["sectors"]) if s.tag and s.tag in tags)
    return frozenset(out)


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


def bassins_nukage(M, conv):
    """Les flaques en BASSINS : les secteurs de liquide qui partagent une ligne n'en font qu'un.

    Rend une liste de dict(aire, feuille, secteur_moteur, x, y, rayon, secteur_doom, liquide)
    du plus grand au plus petit ; `liquide` est l'ensemble des secteurs Doom de liquide du bassin.
    Le point est le centre du plus grand MORCEAU du bassin, pas celui du bassin : un morceau est
    convexe, donc son centre est dedans, et DOOM_GAME.C exige que la feuille qui contient le point
    soit bien celle qu'on annonce."""
    if getattr(conv, "_flaques", None) is not None:
        return conv._flaques
    import doom3d                                 # tardif : doom3d importe ce module
    S, L, SD = M["sectors"], M["linedefs"], M["sidedefs"]
    speciaux = flats_speciaux(M, M.get("wad"))
    liquide = {si for si, s in enumerate(S) if s.floorpic in speciaux}
    parent = {si: si for si in liquide}

    def trouve(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for ld in L:
        if ld.right < 0 or ld.left < 0:
            continue
        a, b = SD[ld.right].sector, SD[ld.left].sector
        if a in liquide and b in liquide:
            ra, rb = trouve(a), trouve(b)
            if ra != rb:
                parent[ra] = rb
    groupes = defaultdict(list)
    for si in liquide:
        groupes[trouve(si)].append(si)
    out = []
    for g in groupes.values():
        aire = sum(abs(doom3d._perimetre_aire(M, si)[1]) for si in g)
        if aire < NUKAGE_AIRE_MIN:
            continue
        # le plus grand morceau du bassin, parmi ceux que la conversion garde
        best = None
        for si in g:
            for li in conv.keep:
                if conv.leaf_sector[li] != si:
                    continue
                ring = conv.polys[li]
                a_ = abs(gridparts.area2(ring)) / 2.0
                if best is None or a_ > best[0]:
                    best = (a_, li, si)
        if best is None:
            continue
        _a, li, si = best
        ring = conv.polys[li]
        cx = sum(p[0] for p in ring) / len(ring)
        cy = sum(p[1] for p in ring) / len(ring)
        if conv.leaf_at(cx, cy) != li:
            continue                              # centre hors de son propre morceau : on passe
        rayon = max(NUKAGE_RAYON[0], min(NUKAGE_RAYON[1], int(math.sqrt(aire / math.pi)) + 96))
        out.append(dict(aire=aire, feuille=li, sect=conv.remap[li], x=int(round(cx)),
                        y=int(round(cy)), rayon=rayon, si=si, liquide=frozenset(g)))
    # FUSION : les bassins dont les bords se touchent presque ne font qu'une flaque
    fondu = list(range(len(out)))

    def trouve2(a):
        while fondu[a] != a:
            fondu[a] = fondu[fondu[a]]
            a = fondu[a]
        return a

    for i in range(len(out)):
        for j in range(i + 1, len(out)):
            d = math.dist((out[i]["x"], out[i]["y"]), (out[j]["x"], out[j]["y"]))
            if d - out[i]["rayon"] - out[j]["rayon"] <= NUKAGE_FUSION:
                a, b = trouve2(i), trouve2(j)
                if a != b:
                    fondu[a] = b
    paquets = defaultdict(list)
    for i in range(len(out)):
        paquets[trouve2(i)].append(out[i])
    fusion = []
    for g in paquets.values():
        if len(g) == 1:
            fusion.append(g[0])
            continue
        aire = sum(e["aire"] for e in g)
        cx = sum(e["aire"] * e["x"] for e in g) / aire   # centre pondere par les aires
        cy = sum(e["aire"] * e["y"] for e in g) / aire
        rayon = int(max(math.dist((cx, cy), (e["x"], e["y"])) + e["rayon"] for e in g))
        rayon = max(NUKAGE_RAYON[0], min(NUKAGE_RAYON_MAX, rayon))
        gros = max(g, key=lambda e: e["aire"])           # de qui la flaque tient sa hauteur
        lf = conv.leaf_at(cx, cy)
        if lf not in conv.remap:                         # le centre tombe hors de la carte gardee
            lf = gros["feuille"]
            cx, cy = gros["x"], gros["y"]
        # le secteur est celui de la FEUILLE ou la flaque se pose, pas celui du plus gros bassin :
        # le centre d'une salle fondue tombe souvent sur le chemin, dont le sol est plus haut
        fusion.append(dict(aire=aire, feuille=lf, sect=conv.remap[lf], x=int(round(cx)),
                           y=int(round(cy)), rayon=rayon, si=conv.leaf_sector[lf],
                           liquide=frozenset().union(*[e["liquide"] for e in g])))
    fusion.sort(key=lambda e: -e["aire"])
    conv._flaques = fusion[:NUKAGE_MAX]
    return conv._flaques


def salle_de_flaque(M, flaque):
    """Les secteurs Doom de la SALLE d'une flaque : son liquide, et tout ce qui le borde par une
    ligne a deux faces sans remonter de plus de NUKAGE_BORD -- la berge et la passerelle, pas le
    couloir qui s'en va. Ce sont eux qui portent SECFLAG_NUKAGE, donc eux dont les murs et les
    PLAFONDS liront la rampe verte (WALLS.C getLight)."""
    S, SD = M["sectors"], M["sidedefs"]
    liquide = flaque["liquide"]
    bas = min(S[si].floorh for si in liquide)
    salle = set(liquide)
    for ld in M["linedefs"]:
        if ld.right < 0 or ld.left < 0:
            continue
        a, b = SD[ld.right].sector, SD[ld.left].sector
        for x, y in ((a, b), (b, a)):
            if x in liquide and y not in liquide and S[y].floorh - bas <= NUKAGE_BORD:
                salle.add(y)
    return salle


def salles_nukage(M, conv):
    """{secteur Doom} de toutes les salles a nukage de la carte (doom3d.cuire_nukage)."""
    out = set()
    for fl in bassins_nukage(M, conv):
        out |= salle_de_flaque(M, fl)
    return out


def secteurs_eclaires(M, x, y, rayon, pas=16):
    """Secteurs Doom qu'une lampe en (x, y) eclaire : ceux dont un point de bord, dans le rayon, se
    voit depuis elle en plan. Seuls les murs pleins (lignes a une face) arretent le regard ; une
    ouverture laisse passer quelle que soit sa hauteur, une porte compte ouverte. On surestime donc,
    jamais l'inverse : le moteur eteint la lampe quand aucun joueur ne peut voir un de ces secteurs
    (REJECT, DOOM_GAME.C doomLampSeen) -- en oublier un l'eteindrait sous les yeux du joueur.
    Un point est pris tous les `pas` u sur chaque ligne, a 1 u de son cote."""
    V, L, SD = M["vertices"], M["linedefs"], M["sidedefs"]

    def pres(p, q):                   # la boite du segment pq, elargie du rayon, contient la lampe
        return (min(p[0], q[0]) - rayon <= x <= max(p[0], q[0]) + rayon
                and min(p[1], q[1]) - rayon <= y <= max(p[1], q[1]) + rayon)

    def orient(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def coupe(p, q, a, b):            # pq et ab se croisent strictement
        d1, d2, d3, d4 = orient(a, b, p), orient(a, b, q), orient(p, q, a), orient(p, q, b)
        return d1 * d2 < 0 and d3 * d4 < 0

    pleins = [(V[ld.v1], V[ld.v2]) for ld in L
              if not 0 <= ld.left < len(SD) and pres(V[ld.v1], V[ld.v2])]
    lampe, r2, vus = (x, y), rayon * rayon, set()
    for ld in L:
        p, q = V[ld.v1], V[ld.v2]
        dx, dy = q[0] - p[0], q[1] - p[1]
        n = math.hypot(dx, dy)
        if n == 0 or not pres(p, q):
            continue
        nx, ny = dy / n, -dx / n          # normale vers le cote droit de v1 -> v2
        k = max(1, int(n // pas))
        for sd, sens in ((ld.right, 1.0), (ld.left, -1.0)):
            if not 0 <= sd < len(SD) or SD[sd].sector in vus:
                continue
            for i in range(k + 1):
                pt = (p[0] + dx * i / k + sens * nx, p[1] + dy * i / k + sens * ny)
                if ((pt[0] - x) ** 2 + (pt[1] - y) ** 2 <= r2
                        and not any(coupe(lampe, pt, a, b) for a, b in pleins)):
                    vus.add(SD[sd].sector)
                    break
    return vus


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
    interrupteurs, sorties, dégâts, murs secrets, lampes (LAMPES)."""
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
             (WLINE_ONCE if w["special"] in W_ONCE else 0)
             | (WLINE_GUN if w["special"] in DOOR_TAGGED_G else 0),
             line=w["line"], kind="wswitch")
    # lumieres de ligne : un OT_DOOM_LIGHT par feuille des secteurs du tag. Plusieurs lignes
    # peuvent partager un tag (E1M3 en a trois autour de la clef) : un seul jeu de receveurs.
    import doom3d                                  # tardif : doom3d importe ce module
    vus = set()
    for w in specials.wswitch:
        if w["special"] not in LIGHT_LINES:
            continue
        cle = (w["channel"], LIGHT_LINES[w["special"]])
        if cle in vus:
            continue
        vus.add(cle)
        niveau = doom3d.light_of(LIGHT_LINES[w["special"]])
        for si, sec in enumerate(M["sectors"]):
            if sec.tag != w["channel"]:
                continue
            for s in leaves_of_sector(conv, si):
                emit(OT_DOOM_LIGHT, s, w["channel"], niveau,
                     sector_doom=si, line=w["line"], kind="light")
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
    # lumieres animees des secteurs (p_lights.c) : un seul enregistrement, une entree par secteur
    voisins = _neighbours(M)
    fx = []
    for si, sec in enumerate(M["sectors"]):
        if sec.special not in SECTOR_FX:
            continue
        feuilles = leaves_of_sector(conv, si)
        if not feuilles:
            notes["secteur anime sans feuille (%s)" % sec.special] += 1
            continue
        genre, noir = SECTOR_FX[sec.special]
        clair_d = sec.light
        sombre_d = min([M["sectors"][v].light for v in voisins.get(si, ())] + [clair_d])
        if (genre & ~FX_SYNC) == FX_STROBE and sombre_d == clair_d:
            sombre_d = 0                          # P_SpawnStrobeFlash : minlight == maxlight -> 0
        if (genre & ~FX_SYNC) == FX_FLICKER:
            sombre_d = min(255, sombre_d + 16)    # P_SpawnFireFlicker
        clair, sombre = doom3d.light_of(clair_d), doom3d.light_of(sombre_d)
        if sombre > clair:
            sombre = clair
        base = genre & ~FX_SYNC
        # Phase et graine : deterministes, par secteur (Doom tire P_Random au chargement). La
        # graine IDENTIFIE le secteur -- impaire et unique -- pour que la verif puisse exiger une
        # seule regle par graine ; le generateur du moteur (LCG) separe deux graines voisines des
        # le premier pas.
        phase = 1 if (genre & FX_SYNC) else ((si * 7 + 3) & 7) + 1
        graine = (si & 0x3fff) * 2 + 1
        fx.append([base, sombre, clair, noir, phase, clair, -1, graine, len(feuilles)]
                  + list(feuilles))
    if fx:
        vals = [2 + sum(len(e) for e in fx), len(fx)]
        for e in fx:
            vals.extend(e)
        emit(OT_DOOM_SECTORFX, *vals, nshorts=vals[0], kind="sectorfx")
    # murs secrets : un par mur DOORWALL d'une ligne ML_SECRET
    for w in sorted(int(x) for x in (secret_walls or ())):
        emit(OT_DOOM_SECRETWALL, w, kind="secretwall")
    # lampes DERIVEES (lampes_derivees : les placards lumineux a porte) : la feuille qui contient
    # (x, y), du secteur deduit, et une feuille par secteur qu'elle eclaire, la sienne d'abord ;
    # au-dela de LAMPE_VUS_MAX, aucune : la lampe reste alors candidate partout, comme sans le test
    for lp in (lampes_derivees(M, conv, specials, M["wad"]) if M.get("wad") else ()):
        lf = conv.leaf_at(lp["x"], lp["y"])
        si = conv.leaf_sector[lf] if lf in conv.remap else None
        if si != lp["secteur"]:
            raise SystemExit("lampe de %s en (%d, %d) : feuille %d, secteur Doom %s au lieu de %d"
                             % (M.get("name"), lp["x"], lp["y"], lf, si, lp["secteur"]))
        sec = M["sectors"][si]
        if not sec.floorh < lp["hauteur"] < sec.ceilh:
            raise SystemExit("lampe de %s : hauteur %d hors du secteur %d (sol %d, plafond %d)"
                             % (M.get("name"), lp["hauteur"], si, sec.floorh, sec.ceilh))
        vus = [conv.remap[lf]]
        for s in sorted(secteurs_eclaires(M, lp["x"], lp["y"], lp["rayon"]) - {si}):
            fs = leaves_of_sector(conv, s)
            if fs:
                vus.append(fs[0])
        if len(vus) > LAMPE_VUS_MAX:
            notes["lampe sans test de vue (%d secteurs eclaires)" % len(vus)] += 1
            vus = []
        emit(OT_DOOM_LAMP, conv.remap[lf], lp["x"], lp["hauteur"], lp["y"], lp["canal"],
             *lp["teinte"], lp["rayon"], lp["intensite"], lp["montee"],
             len(vus), *(vus + [-1] * (LAMPE_VUS_MAX - len(vus))),
             sector_doom=si, kind="lamp", vus=vus)
    # les flaques : UN gestionnaire, une entree par bassin.  Ses feuilles sont celles de la SALLE
    # qui sont au MEME niveau de lumiere que le liquide : respirer avec elles garde les alcoves
    # plus claires et les recoins plus sombres la ou le WAD les a mis.
    import doom3d as _d3
    flaques = []
    for fl in bassins_nukage(M, conv):
        si, x, y, rayon = fl["si"], fl["x"], fl["y"], fl["rayon"]
        sec = M["sectors"][si]
        h = sec.floorh + 16
        if not sec.floorh < h < sec.ceilh:
            notes["bassin trop plat (secteur %d)" % si] += 1
            continue
        liq = max(fl["liquide"], key=lambda s_: abs(_d3._perimetre_aire(M, s_)[1]))
        base = _d3.light_of(M["sectors"][liq].light)
        salle = salle_de_flaque(M, fl)
        feuilles = []
        for s_ in sorted(salle, key=lambda s_: (s_ not in fl["liquide"], s_)):
            if _d3.light_of(M["sectors"][s_].light) != base:
                continue
            for lf in leaves_of_sector(conv, s_):
                if lf not in feuilles:
                    feuilles.append(lf)
        feuilles = feuilles[:NUKAGE_FEUILLES]
        if not feuilles:
            notes["flaque sans feuille a son niveau (secteur %d)" % si] += 1
            continue
        graine = (si & 0x3fff) * 2 + 1
        flaques.append([x, h, y, rayon, fl["sect"], base, 0, 0, graine, len(feuilles)] + feuilles)
    if flaques:
        teinte = teinte_de_carte(M, conv, M["wad"]) if M.get("wad") else WORLDTINT_DEFAUT
        vals = [POOL_TETE + sum(len(f) for f in flaques), len(flaques), NUKAGE_PULSE, NUKAGE_AMP,
                teinte[0], teinte[1], teinte[2]]
        for f in flaques:
            vals.extend(f)
        emit(OT_DOOM_LAMPS, *vals, nshorts=vals[0], kind="pools", n=len(flaques))
    return objects, params, dict(notes)


def longueur_variable(o, params=None):
    """nShorts d'un enregistrement OT_LONGUEUR_VARIABLE : `o["nshorts"]` cote convertisseur (l'objet
    porte ses extras), sinon son 1er short relu dans `params` a `o["firstParam"]` (verif relit le
    .LEV, dont les objets n'ont pas d'extras)."""
    if "nshorts" in o:
        return o["nshorts"]
    if params is None or "firstParam" not in o:
        raise ValueError("objet %d de longueur variable sans nshorts ni params" % o["type"])
    fp = o["firstParam"]
    return int.from_bytes(bytes(params[fp:fp + 2]), "big", signed=True)


def expected_param_bytes(objects, params=None):
    """Σ des params attendus par type (DOOM_ABI « Vérifications PC ») : 5 joueur, 6 mobj, 6 porte
    Doom (3 porte du moteur), 4 ascenseur, 2 sector-switch, 5 interrupteur, 1 sortie, 2 dégâts,
    8 teleporteur, 6 sol, nShorts pour les enregistrements de longueur variable (187/188 :
    `longueur_variable`, d'ou `params` cote verif) -- en octets."""
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
        elif t == OT_DOOM_LIGHT:
            n += 3                      # feuille, canal, lumiere
        elif t in OT_LONGUEUR_VARIABLE:
            n += longueur_variable(o, params)
        elif t == OT_DOOM_LAMP:
            n += 11 + 1 + LAMPE_VUS_MAX  # feuille, x, hauteur, y, canal, r, g, b, rayon, intensite,
                                        # montee ; nombre de feuilles vues, les feuilles
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
