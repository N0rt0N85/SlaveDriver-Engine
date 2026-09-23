# `io_lev` — voir un `.LEV` dans Blender

Extension Blender **en lecture seule** : elle ouvre un niveau SlaveDriver / Aguzzino avec sa
géométrie exacte, ses vraies tuiles décodées depuis le fichier, sa lumière par sommet, ses
portails et ses objets. Elle n'écrit jamais de `.LEV` — le format est **compilé**, et la raison
est expliquée dans [`docs/LEVEL_EDITING_PLAN.md`](../../docs/LEVEL_EDITING_PLAN.md).

## Installer

Blender **4.2 minimum** (développée et testée sur 5.2.2 LTS).

1. zipper le dossier `io_lev/` ;
2. Blender → *Edit > Preferences > Get Extensions > Install from Disk…* → choisir le zip ;
3. l'entrée apparaît dans *File > Import > SlaveDriver (.LEV)*.

## Options de l'import

| option | défaut | ce qu'elle fait |
|---|---|---|
| Échelle | 1 tuile = 1 unité | une tuile fait 64 unités monde (`TILESIZE`, SLEVEL.H:126) ; à l'échelle 1 un niveau dépasse le plan de clipping par défaut de Blender |
| Textures | oui | décode les tuiles du fichier et les pose ; une image empaquetée par tuile de géométrie |
| Lumière par sommet | oui | la lumière **cuite** du moteur, multipliée à la texture — c'est elle qui fait ressembler la vue à l'écran de la console |
| Plafonds | **non** | les garder empêche de voir l'intérieur des pièces d'en haut |
| Portails | oui | les murs sans géométrie, en fil de fer, dans un objet à part : ils ne se dessinent pas mais portent la collision |
| Objets | oui | une cible nommée (`OT_…`) par enregistrement lisible |
| Rapport JSON | — | la sortie de `tools/lev_report.py --json` : ajoute une couche de couleur `cout` qui peint chaque pièce par ce qu'elle coûte |

## Ce que la scène porte

Un objet maillage par niveau, plus un objet de portails et une collection d'objets.

* **matériaux** : un par tuile de géométrie, texture en filtrage *Closest* (on veut voir les texels) ;
* **UV** `tuile` : le moteur n'a **aucun UV libre** — une cellule prend sa tuile entière, et le seul
  placage réglable est le miroir encodé dans l'octet de motif (`pattern[8][4]`, WALLS.C:1153) ;
* **couleur** `lumiere` : l'octet de lumière du moteur, borné au neutre (16) ;
* **couleur** `cout` : vert → rouge par secteur, si on a passé le rapport JSON ;
* **attributs par face** `secteur`, `mur`, `genre` (0 mur, 1 sol, 2 plafond) : de quoi isoler une
  pièce ou ne sélectionner que les sols.

## Vérifier

Trois niveaux, du plus autonome au plus complet :

```powershell
python tools\blender\verif_io_lev.py     # la lecture embarquée contre tools/lev.py, champ par champ
python tools\blender\verif_scene.py      # les quads produits contre tools/cout.py, secteur par secteur
blender --background --factory-startup --python-exit-code 1 ^
        --python tools\blender\test_headless.py -- refs\extract\PS\TOMB.LEV --rendu build\rendus
```

Les deux premiers ne demandent **pas** Blender : toute la logique qui peut être fausse (grille des
murs en parallélogramme, permutation de motif, indexation relative des sommets de face, pas propre
à la grille de lumière) vit dans `levdata.py` et `scene.py`, et se juge sans lui. Le troisième ne
teste plus que la couche `bpy`, et `--rendu` sort une image pour qu'on **regarde** au lieu de croire.

Mesure du 2026-09-23 : les deux vérificateurs passent sur 34 fichiers (24 retail, 9 Doom du fork,
1 converti), zéro écart, RLE de chaque tuile déroulé.

## Régénérer la table des types d'objets

```powershell
python tools\blender\gen_ot_names.py     # SLEVEL.H -> io_lev/ot_names.py (229 types)
```

## Deux pièges à connaître

* **Un indice de sommet répété n'est pas une cellule vide : c'est le plus souvent un triangle.**
  Le format range un triangle en quad dont un sommet se répète, exactement comme le VDP1 le
  dessine. ⚠ Le premier jet de l'extension écartait *toutes* ces cellules en les croyant d'aire
  nulle. **Mesure du 2026-09-24, sur les 24 niveaux retail plus les 9 cartes Doom converties :
  28 482 cellules ont un indice répété, et seules 411 sont réellement plates** — tout le reste a
  trois coins distincts et une surface visible, jusqu'à 2 408 unités². Sur E1M1 les 439 cellules
  concernées étaient 439 triangles et zéro cellule plate, soit **11 % des surfaces du niveau
  perdues en silence**. L'extension les rend donc comme triangles, et n'écarte que ce qui a moins
  de trois coins distincts — en disant combien, dans son rapport d'import comme dans
  `verif_scene.py`. Les vraies cellules plates sont rares et vont par paires : les deux faces d'un
  portail (20 sur TOMB, 0 sur les cartes Doom converties).
* **`tileBase` ne s'applique pas à l'import.** Le chargeur ajoute le nombre de tuiles d'arme aux
  index de géométrie (LEVEL.C:122-125) ; sur le disque ils sont relatifs au jeu de tuiles du
  fichier. L'extension ne l'ajoute pas — l'ajouter sur-indexerait chaque niveau.
