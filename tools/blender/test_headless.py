#!/usr/bin/env python3
"""test_headless.py -- fait tourner l'extension DANS Blender, sans interface.

`verif_io_lev.py` et `verif_scene.py` jugent tout ce qui n'a pas besoin de Blender ; il reste la
couche bpy, qui ne se juge qu'en la faisant tourner. Celui-ci importe un ou plusieurs .LEV, verifie
que le maillage arrive entier (comptes, materiaux, UV, couches de couleur, attributs, images
empaquetees) et, si on le demande, sort un rendu pour qu'on REGARDE le resultat au lieu de le
croire.

Usage :
  blender --background --factory-startup --python-exit-code 1 \\
          --python tools/blender/test_headless.py -- FICHIER.LEV [...] [--rendu DOSSIER]
"""
import math
import os
import sys

import bpy

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
import io_lev                                           # noqa: E402


def _args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    rendu = None
    if "--rendu" in argv:
        i = argv.index("--rendu")
        rendu = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    return argv, rendu


def _vider():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def _cadrer(obj):
    """Une camera qui voit tout l'objet, en vue de trois quarts."""
    bb = [obj.matrix_world @ v.co for v in obj.data.vertices]
    if not bb:
        return None
    xs = [p.x for p in bb]
    ys = [p.y for p in bb]
    zs = [p.z for p in bb]
    cx, cy, cz = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2
    r = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)) or 1.0
    cam_d = bpy.data.cameras.new("cam")
    cam = bpy.data.objects.new("cam", cam_d)
    bpy.context.scene.collection.objects.link(cam)
    cam.location = (cx + r * 0.9, cy - r * 0.9, cz + r * 0.8)
    cam.rotation_euler = (math.radians(58), 0.0, math.radians(45))
    bpy.context.scene.camera = cam
    return cam


def tester(chemin, rendu=None):
    _vider()
    st = io_lev.importer(chemin, plafonds=False)
    nom = os.path.splitext(os.path.basename(chemin))[0]
    obj = bpy.data.objects.get(nom)
    pb = []
    if obj is None:
        return ["objet %s absent de la scene" % nom], st
    me = obj.data
    if len(me.polygons) != st["quads"]:
        pb.append("%d polygones pour %d cellules annoncees" % (len(me.polygons), st["quads"]))
    if any(len(p.vertices) != 4 for p in me.polygons):
        pb.append("des polygones ne sont pas des quads")
    if len(me.materials) != st["tuiles"]:
        pb.append("%d materiaux pour %d tuiles" % (len(me.materials), st["tuiles"]))
    if "tuile" not in me.uv_layers:
        pb.append("couche UV 'tuile' absente")
    for c in ("lumiere",):
        if c not in me.color_attributes:
            pb.append("couche de couleur '%s' absente" % c)
    for a in ("secteur", "mur", "genre"):
        if a not in me.attributes:
            pb.append("attribut '%s' absent" % a)
    vides = [m.name for m in me.materials if m.node_tree is None]
    if vides:
        pb.append("%d materiaux sans arbre de noeuds (%s)" % (len(vides), vides[0]))
    img = [i for i in bpy.data.images if i.name.startswith("tuile_")]
    if len(img) != st["tuiles"]:
        pb.append("%d images pour %d tuiles" % (len(img), st["tuiles"]))
    creuses = [i.name for i in img if not i.has_data or i.size[0] == 0]
    if creuses:
        pb.append("%d images vides (%s)" % (len(creuses), creuses[0]))
    # Les UV doivent couvrir la tuile entiere : le moteur n'a pas d'UV libre, chaque cellule prend
    # sa tuile en entier, donc chaque coordonnee vaut 0 ou 1.
    uv = me.uv_layers["tuile"].data
    hors = sum(1 for d in uv if not (-0.001 <= d.uv[0] <= 1.001 and -0.001 <= d.uv[1] <= 1.001))
    if hors:
        pb.append("%d coins d'UV hors de [0, 1]" % hors)
    # Les 8 motifs doivent tous etre representables : on verifie qu'on voit bien des quads
    # retournes (sinon la permutation de WALLS.C:1153 n'a jamais servi).
    coins = {(round(d.uv[0]), round(d.uv[1])) for d in uv}
    if len(coins) != 4:
        pb.append("les UV n'utilisent que %d coins de texture sur 4" % len(coins))

    if rendu:
        os.makedirs(rendu, exist_ok=True)
        if _cadrer(obj):
            s = bpy.context.scene
            s.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in \
                [i.identifier for i in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] \
                else "BLENDER_WORKBENCH"
            s.render.resolution_x, s.render.resolution_y = 960, 640
            s.render.filepath = os.path.join(rendu, nom + ".png")
            s.render.image_settings.file_format = "PNG"
            if s.render.engine == "BLENDER_WORKBENCH":
                s.display.shading.light = "FLAT"
                s.display.shading.color_type = "TEXTURE"
            bpy.ops.render.render(write_still=True)
    return pb, st


def main():
    fichiers, rendu = _args()
    if not fichiers:
        print("aucun .LEV donne apres --")
        return 1
    faux = 0
    for c in fichiers:
        try:
            pb, st = tester(c, rendu)
        except Exception as e:
            import traceback
            traceback.print_exc()
            print("  FAUX  %-20s exception : %s" % (os.path.basename(c), e))
            faux += 1
            continue
        if pb:
            faux += 1
            print("  FAUX  %-20s %s" % (os.path.basename(c), " | ".join(pb)))
        else:
            print("  ok    %-20s %d cellules, %d sommets, %d tuiles, %d portails, %d objets"
                  % (os.path.basename(c), st["quads"], st["sommets"], st["tuiles"],
                     st["portails"], st["objets"]))
    print("\n%d fichier(s), %d en defaut" % (len(fichiers), faux))
    return 1 if faux else 0


if __name__ == "__main__":
    sys.exit(main())
