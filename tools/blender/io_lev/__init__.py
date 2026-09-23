"""io_lev -- importer un .LEV de SlaveDriver / Aguzzino dans Blender, en LECTURE SEULE.

CE QU'IL FAIT ET CE QU'IL NE FAIT PAS. Il montre un niveau tel que le moteur le porte : la
geometrie exacte (grilles de cellules et maillages), les vraies tuiles decodees depuis le fichier,
la lumiere par sommet, les portails, les objets. Il n'ECRIT rien : un `.LEV` est un format COMPILE
-- deplacer un sommet invaliderait la tessellation, les plans de mur, les longueurs en tuiles, les
plans de coupe, les paires d'ordre et la table de rejet, tous derives. Voir docs/LEVEL_EDITING_PLAN.md.

TOUT CE QUI PEUT ETRE FAUX EST AILLEURS : `levdata.py` lit le fichier, `scene.py` en fait une
description, et les deux sont juges sans Blender par `tools/blender/verif_io_lev.py` et
`verif_scene.py`. Ce fichier-ci ne fait que recopier des tableaux dans Blender.

Installation : Edition > Preferences > Extensions > Install from Disk, en pointant le dossier
io_lev zippe -- ou copier le dossier dans .../scripts/addons/.
"""
import array
import os
import sys

import bpy
from bpy.props import BoolProperty, EnumProperty, StringProperty
from bpy_extras.io_utils import ImportHelper

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levdata                                          # noqa: E402
import scene as scn                                     # noqa: E402

# 1 unite monde = 1 texel ; une tuile fait TILESIZE 64 unites (SLEVEL.H:126). A l'echelle 1 un
# niveau s'etend sur +/- 16 000 unites, au-dela du plan de clipping par defaut de Blender : le
# defaut ramene donc une tuile a 1 unite Blender.
ECHELLES = {"TUILE": 1.0 / 64.0, "UNITE": 1.0}


def _materiau(nom, image, teinte_lumiere):
    """Un materiau par tuile : la texture, multipliee par la lumiere par sommet quand on la veut.

    La lumiere du moteur n'est PAS un eclairage de scene, c'est une couleur cuite par sommet
    (worldGrey, UTIL.C:151) : la reproduire par un noeud de couleur est la seule facon de voir ce
    que la console affiche vraiment."""
    mat = bpy.data.materials.new(nom)
    if mat.node_tree is None:               # 4.2 le demande ; 5.x cree l'arbre tout seul et
        mat.use_nodes = True                # `use_nodes` y est deprecie (retrait annonce en 6.0)
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest"           # 64x64 : on veut voir les texels, pas les lisser
    tex.location = (-600, 300)
    if bsdf is None:
        return mat
    bsdf.inputs["Roughness"].default_value = 1.0
    if "Specular IOR Level" in bsdf.inputs:
        bsdf.inputs["Specular IOR Level"].default_value = 0.0
    src = tex.outputs["Color"]
    if teinte_lumiere:
        col = nt.nodes.new("ShaderNodeVertexColor")
        col.layer_name = "lumiere"
        col.location = (-600, 0)
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.location = (-350, 200)
        nt.links.new(mix.inputs[6], src)        # A (couleur)
        nt.links.new(mix.inputs[7], col.outputs["Color"])
        src = mix.outputs[2]
    nt.links.new(bsdf.inputs["Base Color"], src)
    nt.links.new(bsdf.inputs["Alpha"], tex.outputs["Alpha"])
    return mat


def _image_de_tuile(donnees, i):
    t = donnees["tiles"][i]
    w, h, px = levdata.rgba(t, donnees)
    if not w:
        return None
    img = bpy.data.images.new("tuile_%03d" % i, w, h, alpha=True)
    img.pixels.foreach_set(array.array("f", px))
    img.pack()
    return img


def importer(chemin, echelle=ECHELLES["TUILE"], portails=True, objets=True,
             textures=True, lumiere=True, plafonds=True, rapport=None):
    """-> dict de statistiques. Cree une collection nommee d'apres le fichier."""
    donnees = levdata.lire(chemin)
    sc = scn.construire(donnees, portails=portails, objets=objets)
    nom = os.path.splitext(os.path.basename(chemin))[0]

    racine = bpy.data.collections.new(nom)
    bpy.context.scene.collection.children.link(racine)

    # ---- le maillage du monde -----------------------------------------------------------
    # Repere : le .LEV a X et Z horizontaux et +Y vers le haut (wallasm_gnu.s:61, UTIL.C:41) ;
    # Blender a +Z vers le haut. On echange donc Y et Z.
    verts = [(p[0] * echelle, p[2] * echelle, p[1] * echelle) for p in sc.sommets]

    # ⚠ LES CELLULES D'AIRE NULLE. Tout niveau retail en porte (6 a 58 par fichier, toujours en
    # nombre PAIR : ce sont les deux faces d'un portail). Le moteur les garde dans ses tableaux et
    # `tools/cout.py` les compte, parce que le peintre les traverse ; Blender, lui, refuse une
    # face dont deux sommets sont confondus et `validate()` la retire -- ce qui desalignerait tous
    # les tableaux par face. On les ecarte donc ICI, et seulement ici : `scene.py` reste fidele au
    # moteur, cette couche montre ce qui est montrable, et l'ecart est dit a l'utilisateur.
    garde = [i for i, q in enumerate(sc.quads)
             if len(set(q)) == 4 and (plafonds or sc.genre[i] != 2)]
    ecartees = len(sc.quads) - len(garde)

    mesh = bpy.data.meshes.new(nom)
    mesh.from_pydata(verts, [], [sc.quads[i] for i in garde], shade_flat=True)
    mesh.validate(verbose=False)
    if len(mesh.polygons) != len(garde):
        raise RuntimeError("Blender a garde %d faces sur %d -- des cellules se recouvrent "
                           "exactement ; signalez le niveau" % (len(mesh.polygons), len(garde)))

    tuiles = sorted(set(sc.tuiles))
    slot = {t: k for k, t in enumerate(tuiles)}
    obj = bpy.data.objects.new(nom, mesh)
    racine.objects.link(obj)

    for t in tuiles:
        img = _image_de_tuile(donnees, t) if textures else None
        mesh.materials.append(_materiau("%s_t%03d" % (nom, t), img, lumiere and textures)
                              if img else bpy.data.materials.new("%s_t%03d" % (nom, t)))
    mesh.polygons.foreach_set("material_index",
                              array.array("i", [slot[sc.tuiles[i]] for i in garde]))

    uv = mesh.uv_layers.new(name="tuile")
    plat = []
    for i in garde:
        for u, v in sc.uv[i]:
            plat += [u, 1.0 - v]                # v du .LEV va vers le BAS, celui de Blender monte
    uv.data.foreach_set("uv", array.array("f", plat))

    if lumiere:
        col = mesh.color_attributes.new(name="lumiere", type="BYTE_COLOR", domain="CORNER")
        plat = []
        for i in garde:
            for o in sc.lum[i]:
                c = scn.clarte(o)
                plat += [c, c, c, 1.0]
        col.data.foreach_set("color", array.array("f", plat))

    att = mesh.attributes.new(name="secteur", type="INT", domain="FACE")
    att.data.foreach_set("value", array.array("i", [sc.secteur[i] for i in garde]))
    att = mesh.attributes.new(name="mur", type="INT", domain="FACE")
    att.data.foreach_set("value", array.array("i", [sc.mur[i] for i in garde]))
    att = mesh.attributes.new(name="genre", type="INT", domain="FACE")
    att.data.foreach_set("value", array.array("i", [sc.genre[i] for i in garde]))

    # ---- la carte de cout, si on a le JSON de tools/lev_report.py -------------------------
    peint = 0
    if rapport:
        cel = _cellules_du_rapport(rapport, os.path.basename(chemin))
        if cel:
            pire = max(cel) or 1
            col = mesh.color_attributes.new(name="cout", type="BYTE_COLOR", domain="CORNER")
            plat = []
            for i in garde:
                si = sc.secteur[i]
                c = scn.couleur_de_cout(cel[si] if si < len(cel) else 0, pire)
                plat += list(c) * 4
            col.data.foreach_set("color", array.array("f", plat))
            peint = len(cel)
    mesh.update()

    # ---- les portails, a part : ils n'ont pas de geometrie mais portent la collision -------
    n_port = 0
    quads_p = [q for q in sc.portails if len(set(q)) == 4]
    if portails and quads_p:
        pm = bpy.data.meshes.new(nom + "_portails")
        pm.from_pydata(verts, [], quads_p, shade_flat=True)
        pm.validate(verbose=False)
        pm.update()
        po = bpy.data.objects.new(nom + "_portails", pm)
        po.display_type = "WIRE"
        po.hide_render = True
        racine.objects.link(po)
        n_port = len(quads_p)

    # ---- les objets ------------------------------------------------------------------------
    if objets and sc.objets:
        coll = bpy.data.collections.new(nom + "_objets")
        racine.children.link(coll)
        for o in sc.objets:
            e = bpy.data.objects.new("%s_%d" % (o["nom"], o["type"]), None)
            e.empty_display_type = "ARROWS"
            e.empty_display_size = 32 * echelle
            e.location = (o["x"] * echelle, o["z"] * echelle, o["y"] * echelle)
            e.rotation_euler = (0.0, 0.0, o["angle"] * 3.141592653589793 / 180.0)
            e["ot_type"] = o["type"]
            e["secteur"] = o["secteur"]
            coll.objects.link(e)

    obj["fichier"] = os.path.basename(chemin)
    obj["secteurs"] = len(donnees["level"]["sectors"])
    obj["cellules"] = len(sc.quads)
    obj["tuiles_geometrie"] = len(tuiles)
    obj["cellules_ecartees"] = ecartees
    return dict(quads=len(garde), cellules=len(sc.quads), ecartees=ecartees,
                sommets=len(verts), tuiles=len(tuiles), portails=n_port,
                objets=len(sc.objets), muets=sc.objets_muets, secteurs_peints=peint)


def _cellules_du_rapport(chemin_json, nom_fichier):
    """Lit `cellules_secteur` du JSON de tools/lev_report.py pour le niveau demande."""
    import json
    try:
        with open(chemin_json, encoding="utf-8") as f:
            rap = json.load(f)
    except Exception:
        return None
    for r in rap if isinstance(rap, list) else [rap]:
        if r.get("fichier") == nom_fichier:
            return r.get("cellules_secteur")
    return rap[0].get("cellules_secteur") if isinstance(rap, list) and rap else None


class IMPORT_SCENE_OT_lev(bpy.types.Operator, ImportHelper):
    """Importer un niveau SlaveDriver / Aguzzino (.LEV), en lecture seule"""
    bl_idname = "import_scene.slavedriver_lev"
    bl_label = "SlaveDriver (.LEV)"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".LEV"
    filter_glob: StringProperty(default="*.lev;*.LEV", options={"HIDDEN"})

    echelle: EnumProperty(
        name="Echelle",
        items=[("TUILE", "1 tuile = 1 unite", "64 unites monde par unite Blender"),
               ("UNITE", "1 unite = 1 unite", "l'echelle du moteur ; pensez au plan de clipping")],
        default="TUILE")
    textures: BoolProperty(name="Textures", default=True,
                           description="decoder les tuiles du fichier et les poser")
    lumiere: BoolProperty(name="Lumiere par sommet", default=True,
                          description="la lumiere cuite du moteur, multipliee a la texture")
    portails: BoolProperty(name="Portails", default=True,
                           description="les murs sans geometrie, en fil de fer")
    plafonds: BoolProperty(name="Plafonds", default=False,
                           description="garder les plafonds ; sans eux on voit l'interieur "
                                       "des pieces d'en haut")
    objets: BoolProperty(name="Objets", default=True,
                         description="poser une cible par objet lisible")
    rapport: StringProperty(name="Rapport JSON", default="", subtype="FILE_PATH",
                            description="sortie de tools/lev_report.py --json : ajoute une couche "
                                        "de couleur 'cout' par secteur")

    def execute(self, context):
        st = importer(self.filepath, echelle=ECHELLES[self.echelle], portails=self.portails,
                      objets=self.objets, textures=self.textures, lumiere=self.lumiere,
                      plafonds=self.plafonds, rapport=(self.rapport or None))
        self.report({"INFO"},
                    "%d cellules sur %d (%d d'aire nulle ecartees), %d sommets, %d tuiles, "
                    "%d portails, %d objets (%d non lus)"
                    % (st["quads"], st["cellules"], st["ecartees"], st["sommets"], st["tuiles"],
                       st["portails"], st["objets"], st["muets"]))
        return {"FINISHED"}


def _menu(self, context):
    self.layout.operator(IMPORT_SCENE_OT_lev.bl_idname, text="SlaveDriver (.LEV)")


def register():
    bpy.utils.register_class(IMPORT_SCENE_OT_lev)
    bpy.types.TOPBAR_MT_file_import.append(_menu)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(_menu)
    bpy.utils.unregister_class(IMPORT_SCENE_OT_lev)
