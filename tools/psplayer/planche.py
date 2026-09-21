#!/usr/bin/env python3
"""planche.py -- planches de sprites generees -> images du corps des autres joueurs (PowerSlave, multi).

Le corps que les autres joueurs voient (PSMULTI.C ps_playerBodySeq) n'existe dans aucun .LEV :
Lt. Curtis n'a pas de sprite (AI.C:45 playerSeqList = {-1}), d'ou le monstre porte en guise de peau.
On le fabrique a partir de deux planches generees (ChatGPT, 2026-09-21) d'apres le personnage de
l'ecran de chargement.  Elles restent HORS DEPOT (refs/psplayer/ et build/ sont dans .gitignore) :
ce sont des derives d'une image du jeu, comme les .LEV.

Entrees
  refs/psplayer/planche1.png  1536x1024, 8 colonnes x 6 rangees (1re generation, angles en rangees).
        On n'en garde que la MORT (rangee 6) et le FLASH (rangee 3, colonne 7) : ses tirs et ses
        douleurs sont tous de profil quelle que soit la rangee, et sa course n'alterne pas.
  refs/psplayer/planche2.png  1536x1024, 5 colonnes = angles (face, 3/4 face, profil, 3/4 dos, dos,
        tous tournes vers la DROITE) x 6 rangees = poses (repos, visee, douleur, course A, passage,
        course B).  Chaque rangee est un tour complet d'UNE pose : c'est ce qui a fait tourner le tir.
  refs/psplayer/planche3.png  (3 angles de cote x 5 phases de course, demandee en jambe proche /
        lointaine) : NON utilisee.  Sa foulee 2 repete sa foulee 1 et ses deux passages sont le meme,
        dans les 3 angles (lu a la poche de cuisse, qui marque la jambe proche) ; et ses rangees 3-5
        sont dessinees 5-13 % plus grandes que son repos.  Aucune des trois planches n'a de foulee
        jambe proche devant : de profil et de 3/4 la course reste A, passage, A, passage.

Sorties (build/psplayer/)
  img/*.png        une image par figure : RGBA, alpha binaire (l'index 0 des tuiles du moteur), rognee
  manifeste.json   les sequences dans l'ordre du moteur (animation x vue 0..7), pieces, miroirs, ancres
  apercu.png       planche de controle dans l'ordre du moteur
  course.gif, tir.gif, tour.gif, mort.gif   les animations, les 8 vues cote a cote

Conventions du moteur, relevees dans le code et les donnees retail
  vues    getFacingAngle (AICOMMON.C:73) : 0 = face, 4 = dos.  Anubis de KARNAK (marche, 1re image,
          rendu depuis le .LEV le 21-09) : les vues 5, 6, 7 sont dessinees tournees vers la DROITE,
          1, 2, 3 en sont les miroirs (drapeau de chunk 1) -- 7 = 3/4 face, 6 = profil, 5 = 3/4 dos.
          planche2 est dessinee vers la droite : ses colonnes vont telles quelles en 7/6/5, en miroir
          en 1/2/3.  Meme partage que le convertisseur Doom (wad2sprites.py, lumps XXXXF2F8).
  miroir  par CHUNK, pas par image (WALLS.C drawSprites : flags & 1 -> DIR_LRREV).  D'ou la course de
          face et de dos : l'autre jambe = le chunk des jambes retourne, torse et fusil intacts, ZERO
          tuile de plus.  Impossible de profil et de 3/4 : les pieds pointeraient vers l'arriere.
  echelle un pixel de sprite vaut o->scale = 48000/65536 = 0,732 u (SPRITE.C newSprite) ; les pieds
          sont au sol (WALLS.C:4066 feetPos.y = pos.y - radius) ; l'oeil du joueur PowerSlave est a
          PLAYER_RADIUS + PLAYER_EYE_HOVER = 47 + 8 = 55 u (gameparams.cfg, modele ball).  Deux joueurs
          face a face se regardent dans les yeux si ceux du sprite sont a 55 / 0,732 = 75 px du sol ;
          les yeux du repos de face sont a 90 % de sa hauteur (mesure sur l'image reduite : ligne
          8-9 sur 86) -> 84 px debout.
          (L'Anubis mesure 103-116 px, soit 76-85 u : il domine le joueur, c'est voulu.)
  cadence 1 image par tic (PSMULTI.C psBodyTic) ; la marche de l'Anubis tient chaque pose 3 tics
          (18 images = 6 poses x 3, mesure sur KARNAK) : TICS_PAR_POSE = 3.

Usage : python tools/psplayer/planche.py [--hauteur 84] [--sortie build/psplayer]
"""
from __future__ import annotations

import argparse
import json
import os

import numpy as np
from PIL import Image, ImageDraw

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(os.path.dirname(ICI))
PLANCHE1 = os.path.join(RACINE, 'refs', 'psplayer', 'planche1.png')
PLANCHE2 = os.path.join(RACINE, 'refs', 'psplayer', 'planche2.png')

HAUTEUR = 84            # repos de face, pieds -> sommet du bandana (voir l'en-tete : l'oeil a 55 u)
TICS_PAR_POSE = 3
MAGENTA = 128           # min(R,B) - G : ~245 sur le fond, < 60 dans la figure ; au-dela de la moitie
                        # du chemin le pixel est plus fond que figure et part a la transparence
BASSIN = (0.50, 0.60)   # bande des hanches (fraction de la hauteur depuis le haut) : ancre horizontale
CEINTURE = 0.52         # coupe torse / jambes de la course de face et de dos (sous le fusil, mesure
                        # sur planche2 : chargeur et ceinture finissent vers 46 %)

ANGLES = ('face', '34av', 'profil', '34ar', 'dos')
# vue du moteur -> (angle dessine, miroir de l'image entiere)
VUES = {0: ('face', False), 1: ('34av', True), 2: ('profil', True), 3: ('34ar', True),
        4: ('dos', False), 5: ('34ar', False), 6: ('profil', False), 7: ('34av', False)}


# --- detourage ------------------------------------------------------------------------------------

def detourer(chemin):
    """(rgb uint8 HxWx3, alpha bool HxW).  Le fond n'est pas un magenta pur (249,4,250 +-2, bruite) :
    on mesure la « magentitude » min(R,B) - G.  Les pixels gardes perdent leur part de magenta
    (despill : R et B abaisses de min(R,B) - G) -- sinon le liseré rose du bord et le gilet violet
    des cadavres de planche1 resteraient roses une fois reduits."""
    a = np.asarray(Image.open(chemin).convert('RGB')).astype(np.int32)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mag = np.minimum(r, b) - g
    alpha = mag < MAGENTA
    spill = np.clip(mag, 0, None) * alpha
    out = np.stack([r - spill, g, b - spill], axis=-1)
    return np.clip(out, 0, 255).astype(np.uint8), alpha


TROU = 6                # un vide plus etroit ne separe pas deux figures (un flash decolle du canon)


def cellules(alpha, ncol, nrow):
    """{(rangee, colonne) 1-based: (x0, y0, x1, y1)} -- les figures, separees par les bandes VIDES et
    non par la grille demandee : ChatGPT ne la tient pas (planche3 : 5 rangees tous les ~190 px au
    lieu de 205, des pieds passent sous la ligne theorique).  Une ligne ou une colonne ne compte qu'a
    partir de 2 pixels opaques (les poussieres du bruit ne font ni bande ni boite)."""
    def plages(profil, n, quoi):
        runs = []
        for v in np.nonzero(profil >= 2)[0]:
            if runs and v - runs[-1][1] <= TROU:
                runs[-1][1] = v
            else:
                runs.append([v, v])
        assert len(runs) == n, '%d %s trouvees, %d attendues' % (len(runs), quoi, n)
        return [(int(a), int(b) + 1) for a, b in runs]
    boites = {}
    for r, (y0, y1) in enumerate(plages(alpha.sum(1), nrow, 'rangees'), 1):
        bande = alpha[y0:y1]
        for c, (x0, x1) in enumerate(plages(bande.sum(0), ncol, 'colonnes'), 1):
            lig = np.nonzero(bande[:, x0:x1].sum(1) >= 2)[0]
            boites[(r, c)] = (x0, y0 + int(lig[0]), x1, y0 + int(lig[-1]) + 1)
    return boites


def ancre_bassin(alpha, boite):
    """x (planche) de l'axe du corps : mediane des pixels opaques de la bande des hanches.  Ni le
    centre de la boite (le fusil tendu la decale) ni le milieu des pieds (ecartes en course)."""
    x0, y0, x1, y1 = boite
    h = y1 - y0
    bande = alpha[y0 + int(h * BASSIN[0]):y0 + int(h * BASSIN[1]), x0:x1]
    cols = np.nonzero(bande)[1]
    return x0 + float(np.median(cols))


def axe_ceinture(alpha, boite, axe):
    """x de l'axe de symetrie a la coupe torse/jambes : milieu de la taille sur la ligne de coupe
    (les deux bords doivent tomber juste une fois les jambes retournees)."""
    x0, y0, x1, y1 = boite
    y = y0 + int((y1 - y0) * CEINTURE)
    cols = np.nonzero(alpha[y, x0:x1])[0] + x0
    pres = cols[np.abs(cols - axe) < (y1 - y0) * 0.3]
    return (pres.min() + pres.max()) / 2.0 if len(pres) else axe


# --- reduction ------------------------------------------------------------------------------------

def reduire(rgb, alpha, boite, facteur, ax, pieds):
    """Figure de la planche -> (image RGBA reduite, x0, y0) ; (x0, y0) = coin haut-gauche relatif a
    l'origine du sprite (axe du corps, sol).  Moyenne de surface sur couleurs premultipliees : aucun
    magenta ne rentre par le bord ; puis alpha binaire a 50 % (le moteur n'a que l'index 0)."""
    x0, y0, x1, y1 = boite
    # la grille des pixels reduits passe par l'origine : (gx, gy) = coin haut-gauche, entier
    gx = int(np.floor((x0 - ax) * facteur)); gy = int(np.floor((y0 - pieds) * facteur))
    sx = ax + gx / facteur; sy = pieds + gy / facteur          # ce coin, dans la planche
    w = int(np.ceil((x1 - sx) * facteur)); h = int(np.ceil((y1 - sy) * facteur))
    cx0 = int(np.floor(sx)) - 1; cy0 = int(np.floor(sy)) - 1   # PIL veut une boite source positive
    cx1 = int(np.ceil(sx + w / facteur)) + 1; cy1 = int(np.ceil(sy + h / facteur)) + 1
    rgba = np.dstack([rgb[cy0:cy1, cx0:cx1], (alpha[cy0:cy1, cx0:cx1] * 255).astype(np.uint8)])
    im = Image.fromarray(np.ascontiguousarray(rgba), 'RGBA').convert('RGBa')
    box = (sx - cx0, sy - cy0, sx - cx0 + w / facteur, sy - cy0 + h / facteur)
    im = im.resize((w, h), Image.BOX, box=box).convert('RGBA')
    a = np.asarray(im).copy()
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    a[a[..., 3] == 0] = 0
    ys, xs = np.nonzero(a[..., 3])
    a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return Image.fromarray(a, 'RGBA'), gx + int(xs.min()), gy + int(ys.min())


# --- le flash du tir ------------------------------------------------------------------------------

def flash_planche1(rgb, alpha, boites, facteur):
    """Le flash de planche1 (profil, tir 2) : tout ce qui depasse le bout du canon.  Rend le cone
    (vers la droite, origine a la bouche) et l'etoile de face (cone + son miroir, origine au centre)."""
    x0, y0, x1, y1 = boites[(3, 7)]
    h = y1 - y0
    sombre = alpha[y0:y1, x0:x1] & (rgb[y0:y1, x0:x1].max(axis=2) < 110)
    bande = sombre[int(h * 0.10):int(h * 0.45)]                    # la hauteur du fusil
    bout = x0 + int(np.nonzero(bande.any(axis=0))[0].max()) + 1    # 1re colonne apres le canon
    fl = np.zeros_like(alpha)
    fl[y0:y1, bout:x1] = alpha[y0:y1, bout:x1]
    ys, xs = np.nonzero(fl)
    boite = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    cone, fx, fy = reduire(rgb, fl, boite, facteur, bout, float(ys.mean()))
    # de face le canon est vu en bout : le cone et son miroir, dos a dos autour de la bouche
    c = np.asarray(cone)
    cw = c.shape[1]
    demi = fx + cw                                                 # de la bouche au bout du cone
    e = np.zeros((c.shape[0], 2 * demi, 4), np.uint8)
    e[:, 0:cw] = c[:, ::-1]
    droite = e[:, demi + fx:demi + fx + cw]
    e[:, demi + fx:demi + fx + cw] = np.where(c[..., 3:] > 0, c, droite)
    return (cone, fx, fy), (Image.fromarray(e, 'RGBA'), -demi, fy)


def bouche(rgb, alpha, boite, angle):
    """Position (planche) de la bouche du canon sur une figure de VISEE, ou None (dos : cachee)."""
    x0, y0, x1, y1 = boite
    h = y1 - y0
    if angle == 'face':
        # le canon vu en bout : un anneau clair au milieu de la poitrine
        z = rgb[y0:y0 + h // 2, x0:x1].astype(int)
        clair = alpha[y0:y0 + h // 2, x0:x1] & (z.min(axis=2) > 150) & (np.ptp(z, axis=2) < 70)
        ys, xs = np.nonzero(clair)
        return (x0 + float(xs.mean()), y0 + float(ys.mean())) if len(xs) else None
    if angle == 'dos':
        return None
    # tourne vers la droite : le point le plus a droite de la bande du fusil
    bande = alpha[y0 + int(h * 0.10):y0 + int(h * 0.45), x0:x1]
    ys, xs = np.nonzero(bande)
    xm = xs.max()
    return (x0 + float(xm) + 1, y0 + int(h * 0.10) + float(ys[xs >= xm - 1].mean()))


# --- assemblage -----------------------------------------------------------------------------------

def construire(hauteur, sortie):
    os.makedirs(os.path.join(sortie, 'img'), exist_ok=True)
    images, info = {}, {}

    def poser(nom, im, x0, y0, **extra):
        im.save(os.path.join(sortie, 'img', nom + '.png'))
        images[nom] = (im, x0, y0)
        info[nom] = dict(png='img/%s.png' % nom, x=x0, y=y0, l=im.width, h=im.height, **extra)

    # planche2 : les poses orientees ----------------------------------------------------------------
    rgb2, al2 = detourer(PLANCHE2)
    b2 = cellules(al2, 5, 6)
    f2 = hauteur / (b2[(1, 1)][3] - b2[(1, 1)][1])
    poses = ('repos', 'visee', 'douleur', 'courseA', 'passage', 'courseB')
    bouches = {}
    for r, pose in enumerate(poses, 1):
        for c, angle in enumerate(ANGLES, 1):
            boite = b2[(r, c)]
            ax = ancre_bassin(al2, boite)
            if angle in ('face', 'dos') and pose in ('courseA', 'passage'):
                ax = axe_ceinture(al2, boite, ax)       # l'axe du miroir des jambes
            pieds = boite[3]
            im, x0, y0 = reduire(rgb2, al2, boite, f2, ax, pieds)
            extra = {}
            if angle in ('face', 'dos') and pose in ('courseA', 'passage'):
                extra['coupe'] = int(round((boite[1] + (boite[3] - boite[1]) * CEINTURE - pieds) * f2)) - y0
            poser('%s_%s' % (pose, angle), im, x0, y0, **extra)
            if pose == 'visee':
                p = bouche(rgb2, al2, boite, angle)
                bouches[angle] = None if p is None else (int(round((p[0] - ax) * f2)), int(round((p[1] - pieds) * f2)))
    if bouches['face'] is None:                                  # anneau du canon introuvable
        bouches['face'] = (0, -int(hauteur * 0.62))
    # le dos tire devant lui : l'eclair a la hauteur de celui de face, sur l'axe, cache par le corps
    bouches['dos'] = (0, bouches['face'][1])

    # planche1 : la mort et le flash ----------------------------------------------------------------
    rgb1, al1 = detourer(PLANCHE1)
    b1 = cellules(al1, 8, 6)
    f1 = hauteur / (b1[(1, 1)][3] - b1[(1, 1)][1])            # son repos de face, meme calage
    mort = []
    for k, c in enumerate((2, 3, 4, 5, 8), 1):               # 1 : pistolet ; 6-7 : doublons de 5 et 8
        boite = b1[(6, c)]
        ax = (boite[0] + boite[2]) / 2.0                     # non orientee : le corps reste centre
        im, x0, y0 = reduire(rgb1, al1, boite, f1, ax, boite[3])
        poser('mort%d' % k, im, x0, y0)
        mort.append('mort%d' % k)
    (cone, cx0, cy0), (etoile, ex0, ey0) = flash_planche1(rgb1, al1, b1, f1)
    poser('flash_cote', cone, cx0, cy0)
    poser('flash_face', etoile, ex0, ey0)

    # sequences, dans l'ordre du moteur -------------------------------------------------------------
    def piece(nom, miroir=False, rangs=None, dx=0, dy=0):
        p = dict(img=nom, miroir=miroir)
        if rangs: p['rangs'] = rangs
        if dx or dy: p.update(dx=dx, dy=dy)
        return p

    def jambes_retournees(nom):
        """torse tel quel + jambes retournees autour de l'axe (x = 0) : l'autre pied, zero tuile."""
        k = info[nom]['coupe']
        return [piece(nom, rangs=[0, k]), piece(nom, miroir=True, rangs=[k, info[nom]['h']])]

    course = {
        # face : A leve la jambe droite (a gauche de l'image), le passage leve la gauche -- c'est le
        # passage APRES B ; retourne, il devient celui apres A
        'face': [[piece('courseA_face')], jambes_retournees('passage_face'),
                 jambes_retournees('courseA_face'), [piece('passage_face')]],
        # dos : A et passage levent tous deux la jambe droite (semelle visible) -- dans l'ordre
        'dos': [[piece('courseA_dos')], [piece('passage_dos')],
                jambes_retournees('courseA_dos'), jambes_retournees('passage_dos')],
    }
    for angle in ('34av', 'profil', '34ar'):
        # PROVISOIRE : sur planche2, B repete A (meme jambe derriere, lu a la poche de cuisse)
        course[angle] = [[piece('courseA_' + angle)], [piece('passage_' + angle)],
                         [piece('courseB_' + angle)], [piece('passage_' + angle)]]

    def tir(angle):
        vis = piece('visee_' + angle)
        bx, by = bouches[angle]
        if angle == 'dos':
            return [piece('flash_face', dx=bx, dy=by), vis]   # derriere le corps : dessine d'abord
        return [vis, piece('flash_face' if angle == 'face' else 'flash_cote', dx=bx, dy=by)]

    def par_vue(fn):
        out = []
        for v in range(8):
            angle, m = VUES[v]
            out.append([[dict(p, miroir=p['miroir'] != m) for p in image] for image in fn(angle)])
        return out

    anims = {
        'repos': par_vue(lambda a: [[piece('repos_' + a)]]),
        'course': par_vue(lambda a: course[a]),
        'visee': par_vue(lambda a: [[piece('visee_' + a)]]),
        'tir': par_vue(lambda a: [[piece('visee_' + a)], tir(a)]),
        'douleur': par_vue(lambda a: [[piece('douleur_' + a)]]),
        'mort': [[piece(n)] for n in mort],                   # non orientee : memes images pour les 8 vues
    }
    manif = dict(hauteur_repos=hauteur, u_par_pixel=48000 / 65536, tics_par_pose=TICS_PAR_POSE,
                 vues={str(v): dict(angle=a, miroir=m) for v, (a, m) in VUES.items()},
                 note_miroir='miroir = retourne autour de x = 0 (axe du corps) ; x, y = coin haut-gauche '
                             'relatif aux pieds ; rangs = lignes [debut, fin) de l\'image prises par la piece',
                 images=info, animations=anims)
    tmp = os.path.join(sortie, 'manifeste.json.tmp')
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(manif, f, indent=1, ensure_ascii=False)
    os.replace(tmp, os.path.join(sortie, 'manifeste.json'))
    return images, info, anims


# --- apercus --------------------------------------------------------------------------------------

FOND = (72, 60, 46)            # pierre sombre de PowerSlave : les bords se jugent sur un fond de jeu
TOILE = (132, 104)             # une vue ; origine (axe, sol) en (66, 98)
ORIGINE = (66, 98)


def rendre(images, pieces, zoom=2):
    toile = Image.new('RGBA', TOILE, FOND + (255,))
    for p in pieces:
        im, x0, y0 = images[p['img']]
        if 'rangs' in p:
            im = im.crop((0, p['rangs'][0], im.width, p['rangs'][1]))
            y0 += p['rangs'][0]
        x = x0 + p.get('dx', 0); y = y0 + p.get('dy', 0)
        if p['miroir']:
            im = im.transpose(Image.FLIP_LEFT_RIGHT)
            x = -(x + im.width)
        calque = Image.new('RGBA', TOILE, (0, 0, 0, 0))            # paste rogne, alpha_composite non
        calque.paste(im, (ORIGINE[0] + x, ORIGINE[1] + y), im)
        toile.alpha_composite(calque)
    return toile.resize((TOILE[0] * zoom, TOILE[1] * zoom), Image.NEAREST)


def apercus(images, anims, sortie):
    lignes = [('repos', 0), ('visee', 0), ('tir', 1), ('douleur', 0)] + [('course', k) for k in range(4)]
    z = 2
    w, h = TOILE[0] * z, TOILE[1] * z
    planche = Image.new('RGBA', (60 + 8 * w, 16 + (len(lignes) + 1) * h), (30, 30, 30, 255))
    d = ImageDraw.Draw(planche)
    for v in range(8):
        a, m = VUES[v]
        d.text((60 + v * w + 6, 2), 'vue %d  %s%s' % (v, a, ' (miroir)' if m else ''), fill=(220, 220, 220))
    for i, (anim, k) in enumerate(lignes):
        d.text((4, 16 + i * h + h // 2), '%s %d' % (anim, k + 1), fill=(220, 220, 220))
        for v in range(8):
            planche.alpha_composite(rendre(images, anims[anim][v][k], z), (60 + v * w, 16 + i * h))
    i = len(lignes)
    d.text((4, 16 + i * h + h // 2), 'mort', fill=(220, 220, 220))
    for k, pieces in enumerate(anims['mort']):
        planche.alpha_composite(rendre(images, pieces, z), (60 + k * w, 16 + i * h))
    planche.convert('RGB').save(os.path.join(sortie, 'apercu.png'))

    def gif(nom, images_par_pas, ms):
        pas = [im.convert('RGB') for im in images_par_pas]
        pas[0].save(os.path.join(sortie, nom), save_all=True, append_images=pas[1:], duration=ms, loop=0)

    def huit(anim, k):
        bande = Image.new('RGBA', (8 * w, h), FOND + (255,))
        for v in range(8):
            bande.alpha_composite(rendre(images, anims[anim][v][k], z), (v * w, 0))
        return bande
    # 3 tics par pose ; le GIF ne sait pas a quelle cadence tourne le jeu : 100 ms par pose
    gif('course.gif', [huit('course', k) for k in range(4)], 100)
    gif('tir.gif', [huit('tir', k) for k in (0, 1, 0, 1, 0, 0)], 70)
    gif('tour.gif', [rendre(images, anims['repos'][v][0], 3) for v in range(8)], 300)
    gif('mort.gif', [rendre(images, p, 3) for p in anims['mort']] + [rendre(images, anims['mort'][-1], 3)] * 4, 150)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--hauteur', type=int, default=HAUTEUR)
    ap.add_argument('--sortie', default=os.path.join(RACINE, 'build', 'psplayer'))
    arg = ap.parse_args()
    images, info, anims = construire(arg.hauteur, arg.sortie)
    apercus(images, anims, arg.sortie)
    utiles = set()
    for nom, a in anims.items():
        for vue in ([a] if nom == 'mort' else a):
            for image in vue:
                utiles.update(p['img'] for p in image)
    opaques = sum(int((np.asarray(images[k][0])[..., 3] > 0).sum()) for k in utiles)
    print('%d images ecrites, %d referencees par les sequences -> %s' % (len(info), len(utiles), arg.sortie))
    print('hauteurs :', ', '.join('%s %dx%d' % (k, info[k]['l'], info[k]['h']) for k in sorted(info)))
    print('pixels opaques des images referencees : %d (~ octets de tuiles RLE, borne basse)' % opaques)


if __name__ == '__main__':
    main()
