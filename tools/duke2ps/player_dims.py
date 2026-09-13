"""player_dims.py -- dimensions du joueur Duke Nukem 3D (1.3D/1.5) en unites Saturn, par SIMULATION des
boucles de jfduke3d (lecture seule de refs/build/jfduke3d, GPL-2+ : aucun code n'est recopie, seules les
constantes et l'ordre des operations sont reproduits pour mesurer).

Usage :  python tools/duke2ps/player_dims.py            -> tableau + build/duke2ps/player/duke_dims.json

Repere (faits etablis du convertisseur) : X = x/8, Z = -y/8, Y = -z/128  (u).  1<<8 en z = 2 u.

SOURCES (chemins relatifs a refs/build/jfduke3d/src sauf mention) :
  player.c:2376       getzrange(..., 163L, ...)                  rayon de sonde sol/plafond
  player.c:2785       i = 40                                     oeil cible = sol - (40<<8)
  player.c:3026-3035  lissage au sol (i==40) : posz += ((fz-(i<<8))-posz)>>1 (|k|<256 -> 0), poszv -= 768
  player.c:3048-3052  accroupi (bit 1) : posz += 2048+768 par tic
  player.c:3057-3065  saut : autorise si fz-cz > 56<<8
  player.c:3071-3095  impulsion de saut : poszv -= sintable[(2048-128+jc)&2047]/12 ; jc += 180 tant que < 1280
  player.c:2963-2970  chute : poszv += gc+80 (gc = GRAVITATIONALCONSTANT = 176, maps/atomic/USER.CON:93)
  player.c:3099-3106  plafond : posz >= cz + (4<<8)
  player.c:3303-3304  flordist = 20<<8 (4<<8 dans l'eau / sur un pont)
  player.c:3318-3320  clipmove(..., 164L, 4L<<8 (ceildist), i (flordist), ...)  -> walldist 164, CONSTANT
  player.c:3322-3323  retreci (yrepeat < 32, player.c:2375) : posz += 32<<8 APRES clipmove
  player.c:3292-3298  retreci : vitesse amortie par dukefriction*(1-1/2+1/4)
  player.c:3368       pushmove(..., 164L, 4L<<8, 4L<<8, ...)
  player.c:3373       |floorz-ceilingz| < 48<<8 -> activatebysector (+ quickkill si pushmove echoue)
  game.c:3065-3073    rendu : cposz borne a [cz+(4<<8), fz-(4<<8)]
  duke3d.h:140        PHEIGHT = 38<<8 (oeil -> z du sprite)
  duke3d.h:100-101    TICRATE 120, TICSPERFRAME = 120/26 = 4 (entier) -> 30 tics/s
  premap.c:1189-1193  sprite APLAYER : xrepeat 42, yrepeat 36, clipdist 64 (collision des AUTRES contre lui)
  jfbuild engine.c:9194-9213  test mur rouge de clipmove : bloque si nextfloor plus haut de >1<<8 et
                      posz >= nextfloor-(flordist-1), ou nextceil plus bas de >1<<8 et posz <= nextceil+(ceildist-1)
  jfbuild engine.c:9765-9766  getzrange ignore un voisin si z <= ceil+(3<<8) ou z >= floor-(3<<8)
  jfbuild engine.c:9219-9228  clipmove : carres axes de demi-cote walldist aux extremites des murs bloquants
  jfbuild engine.c:4921       sintable[i] = (short)(16384*sin(i*pi/1024))
  maps/d13/GAME.CON:1648-1670, 1735-1741 ; maps/d13/USER.CON:79-80 (SHRUNKCOUNT 270, SHRUNKDONECOUNT 304)
  gamedef.c:2664-2666 (move remet count a 0), 1921 (count++ a chaque tic), 2365-2374 (sizeto = 1 cran/tic),
  gamedef.c:2973-2975 (ifgapzl : (floorz-ceilingz)>>8 < N)
PowerSlave (fork, fichiers compiles) :
  SRUINS.C:100-101    SANDALJUMPVEL 60<<13, NORMALJUMPVEL 39<<13 ; SRUINS.C:609-620 (+3<<12 tant que tenu)
  AICOMMON.H:4        GRAVITY 6<<12 ; SPRITE.C:459-467 internal_moveSprite ; SPRITE.C:643-644 (+F(8) camera)
"""
from __future__ import annotations
import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "duke2ps", "player")

ZU = 128.0          # z Build par u
XU = 8.0            # xy Build par u
SIN = [int(16384 * math.sin(i * 3.14159265358979 / 1024)) for i in range(2048)]   # engine.c:4921


def cdiv(a: int, b: int) -> int:
    """division entiere C (troncature vers 0)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def sim_posture(crouch: bool, shrunk: bool, tics: int = 400, gap: int | None = None):
    """Boucle au sol de processinput (hors eau/jetpack), sol plat fz = 0 (z vers le bas).
    Renvoie (oeil au moment de clipmove, oeil fin de tic, oeil rendu) en z Build, en regime etabli."""
    fz = 0
    cz = -(10 ** 9) if gap is None else -gap
    posz = fz - (40 << 8)
    poszv = 0
    zc = ze = zv = posz
    for _ in range(tics):
        i = 40                                            # player.c:2785
        if posz < fz - (i << 8):                          # player.c:2963 chute
            poszv = min(poszv + 176 + 80, 4096 + 2048)
        else:                                             # player.c:3012-3069 au sol
            k = ((fz - (i << 8)) - posz) >> 1             # decalage arithmetique (x86)
            if abs(k) < 256:
                k = 0
            posz += k
            poszv -= 768
            if poszv < 0:
                poszv = 0
            if crouch:
                posz += 2048 + 768                        # player.c:3050
        posz += poszv                                     # player.c:3097
        if posz < cz + (4 << 8):                          # player.c:3099
            posz = cz + (4 << 8)
            poszv = 128
        zc = posz                                         # clipmove voit cette valeur (player.c:3318)
        if shrunk:
            posz += 32 << 8                               # player.c:3322-3323
        ze = posz
        zv = min(max(ze, cz + (4 << 8)), fz - (4 << 8))   # game.c:3065-3073
    return zc, ze, zv


def sim_jump(hold: bool = True):
    """Saut depuis le sol plat, bouton tenu. Renvoie (montee max de l'oeil en z, tics jusqu'au sommet,
    tics en l'air)."""
    fz, cz = 0, -(10 ** 9)
    posz = fz - (40 << 8)
    poszv = 0
    jc = 0
    toggle = 0
    top = posz
    t_top = 0
    airborne = 0
    for t in range(400):
        pressed = hold
        if posz < fz - (40 << 8):                          # chute
            poszv = min(poszv + 176 + 80, 4096 + 2048)
            airborne += 1
        else:
            k = ((fz - (40 << 8)) - posz) >> 1
            if abs(k) < 256:
                k = 0
            posz += k
            poszv -= 768
            if poszv < 0:
                poszv = 0
            if not pressed and toggle == 1:
                toggle = 0
            elif pressed and toggle == 0:
                if jc == 0 and (fz - cz) > (56 << 8):
                    jc = 1
                    toggle = 1
            if jc and not pressed:
                toggle = 0
            if t > 5 and jc == 0 and airborne:
                break
        if jc:
            if not pressed and toggle == 1:
                toggle = 0
            if jc < 1024 + 256:
                poszv -= cdiv(SIN[(2048 - 128 + jc) & 2047], 12)
                jc += 180
            else:
                jc = 0
                poszv = 0
        posz += poszv
        if posz < top:
            top = posz
            t_top = t
    return (fz - (40 << 8)) - top, t_top, airborne


def shrink_timeline():
    """GAME.CON d13:1648-1670 : count remis a 0 par `move PSHRINKING` (gamedef.c:2665), +1 par tic
    (gamedef.c:1921).  count<32 : sizeto 8 9 ; 32..269 : rien ; 270..303 : sizeto 42 36 + ifgapzl 24 ;
    >=304 : move 0.  sizeto : 1 cran par tic (gamedef.c:2365-2374 ; l'APLAYER a yrepeat<36 grandit toujours)."""
    x, y = 42, 36
    first_shrunk = last_shrunk = None
    for count in range(0, 305):
        if count < 32:
            tx, ty = 8, 9
        elif count >= 270:
            tx, ty = 42, 36
        else:
            tx, ty = x, y
        x += (tx > x) - (tx < x)
        y += (ty > y) - (ty < y)
        if y < 32:
            if first_shrunk is None:
                first_shrunk = count
            last_shrunk = count
    return first_shrunk, last_shrunk


def ps_jump(vel0_shift13: int, hold: bool = True):
    """PowerSlave : SRUINS.C:609-620 + SPRITE.C:459-467, en Fixed32 (F(1)=65536), 1 pas = 1 trame
    d'entree (HYPOTHESE : 60 Hz NTSC)."""
    g = 6 << 12
    v = vel0_shift13 << 13
    y = 0
    top = 0
    frames = 0
    first = True
    while True:
        if hold and not first:
            v += 3 << 12
        first = False
        v -= g
        y += v
        frames += 1
        top = max(top, y)
        if y <= 0 and frames > 1:
            break
    return top / 65536.0, frames


def main():
    os.makedirs(OUT, exist_ok=True)
    u = lambda z: z / ZU  # noqa: E731
    stand = sim_posture(False, False)
    crouch = sim_posture(True, False)
    shrunk = sim_posture(False, True)
    shrunk_crouch = sim_posture(True, True)
    rise, t_top, air = sim_jump()
    f_sh, l_sh = shrink_timeline()
    ps_rise, ps_frames = ps_jump(39)
    ps_rise_s, _ = ps_jump(60)
    CEIL = 4 << 8
    FLOR = 20 << 8
    res = {
        "conversion": "Y = -z/128 ; XY = x/8 (u)",
        "walldist_build": 164, "walldist_u": 164 / XU,
        "largeur_min_axe_u": 2 * 164 / XU,
        "largeur_min_45deg_u": 2 * 164 * math.sqrt(2) / XU,
        "getzrange_rayon_u": 163 / XU,
        "sprite_clipdist": 64, "sprite_clipdist_u": 64 * 4 / XU,
        "PHEIGHT_u": u(38 << 8),
        "ceildist_u": u(CEIL), "flordist_u": u(FLOR),
        "debout": {
            "oeil_clip_u": -u(stand[0]), "oeil_rendu_u": -u(stand[2]),
            "hauteur_passage_u": -u(stand[0]) + u(CEIL),
            "marche_max_u": -u(stand[0]) - u(FLOR),
        },
        "accroupi": {
            "oeil_clip_u": -u(crouch[0]), "oeil_rendu_u": -u(crouch[2]),
            "hauteur_passage_u": -u(crouch[0]) + u(CEIL),
            "marche_max_u": max(0.0, -u(crouch[0]) - u(FLOR)),
        },
        "retreci": {
            "oeil_clip_u": -u(shrunk[0]), "oeil_fin_tic_u": -u(shrunk[1]), "oeil_rendu_u": -u(shrunk[2]),
            "hauteur_passage_u": -u(shrunk[0]) + u(CEIL),
            "marche_max_u": max(0.0, -u(shrunk[0]) - u(FLOR)),
        },
        "retreci_accroupi": {"oeil_clip_u": -u(shrunk_crouch[0]), "oeil_rendu_u": -u(shrunk_crouch[2])},
        "saut": {"montee_oeil_u": u(rise), "tic_sommet": t_top, "tics_en_l_air": air,
                 "rebord_max_u": u(rise) + u(FLOR), "hauteur_libre_requise_u": u(56 << 8)},
        "retrecissement": {
            "SHRUNKCOUNT": 270, "SHRUNKDONECOUNT": 304, "tics_par_s": 30,
            "tic_premier_retreci": f_sh, "tic_dernier_retreci": l_sh,
            "duree_retreci_s": (l_sh - f_sh + 1) / 30.0,
            "debut_regrandir_s": 270 / 30.0, "fin_s": 304 / 30.0,
            "ecrase_si_hauteur_libre_lt_u": u(24 << 8),
            "taille_sprite": "42x36 -> 8x9 (xrepeat x yrepeat)",
        },
        "powerslave": {"rayon_u": 47, "oeil_au_dessus_sol_u": 47 + 8, "hauteur_passage_u": 2 * 47 + 8,
                       "stepheight_u": 32, "saut_montee_u_tenu": ps_rise, "saut_trames": ps_frames,
                       "saut_sandales_montee_u": ps_rise_s},
    }
    path = os.path.join(OUT, "duke_dims.json")
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1, ensure_ascii=False)
    os.replace(path + ".tmp", path)
    d = res
    print(f"walldist 164 -> rayon {d['walldist_u']:.1f} u ; largeur mini {d['largeur_min_axe_u']:.1f} u (axe) "
          f"/ {d['largeur_min_45deg_u']:.1f} u (45 deg)")
    for k in ("debout", "accroupi", "retreci"):
        p = d[k]
        print(f"{k:9s} oeil(clip) {p['oeil_clip_u']:6.1f}  oeil(rendu) {p['oeil_rendu_u']:6.1f}  "
              f"passage >= {p['hauteur_passage_u']:6.1f}  marche <= {p['marche_max_u']:5.1f}")
    print(f"retreci+accroupi oeil(clip) {d['retreci_accroupi']['oeil_clip_u']:.1f} (sous le sol : degenere)")
    s = d["saut"]
    print(f"saut : montee oeil {s['montee_oeil_u']:.1f} u (sommet tic {s['tic_sommet']}, {s['tics_en_l_air']} tics"
          f" en l'air) ; rebord max {s['rebord_max_u']:.1f} u ; exige hauteur libre > {s['hauteur_libre_requise_u']:.0f} u")
    r = d["retrecissement"]
    print(f"retreci du tic {r['tic_premier_retreci']} au tic {r['tic_dernier_retreci']} "
          f"({r['duree_retreci_s']:.2f} s) ; regrandit a {r['debut_regrandir_s']:.2f} s ; fin {r['fin_s']:.2f} s ; "
          f"ecrase si hauteur libre < {r['ecrase_si_hauteur_libre_lt_u']:.0f} u")
    p = d["powerslave"]
    print(f"PowerSlave : oeil {p['oeil_au_dessus_sol_u']} u, passage >= {p['hauteur_passage_u']} u, "
          f"saut tenu {p['saut_montee_u_tenu']:.1f} u ({p['saut_trames']} trames), sandales {p['saut_sandales_montee_u']:.1f} u")
    print(f"-> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
