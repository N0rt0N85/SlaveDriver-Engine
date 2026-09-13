# Correction de la section Budget de height_rules.py (2026-09-11)

La vérification contradictoire (workflow du 2026-09-11, lentille budget) a **réfuté** la section Budget
de l'étude des hauteurs. Ce fichier enregistre la correction et le modèle juste. Module de mesure :
`tools\duke2ps\budget_correct.py` (non suivi).

## Ce qui était faux (et pourquoi)

1. **« La hauteur du ciel ne coûte presque rien par image » — FAUX.**
   Le slave (deuxième SH-2) réserve son budget de polygones sur le nombre **total** de cellules de chaque
   mur, **avant** tout rejet à l'écran :

   ```c
   // WALLS.C:1374 (slave_drawRectWall)
   if (height*width + nmSlavePolys + 50 > MAXNMSLAVEPOLYS)   // MAXNMSLAVEPOLYS = 1300, WALLS.C:1336
       return;   // le mur est SAUTE silencieusement
   ```

   `height` = `tileHeight` et `width` = `tileLength` sont le nombre de **rangées** et de **colonnes** de
   cellules du mur entier — pas les cellules visibles. Chaque cellule de mur parallélogramme émet un
   polygone (`nmSlavePolys++`, WALLS.C:1494/1539), chaque face explicite aussi (WALLS.C:1582). Donc :
   **ciel plus haut → murs plus hauts → plus de cellules → des murs entiers ne sont jamais dessinés.**
   C'est un coût *par image* piloté par la hauteur du ciel, exactement l'inverse de la conclusion initiale.

2. **Le modèle d'octets était faux.** Il comptait 8 o/sommet pour tous les murs, alors qu'un mur
   parallélogramme ne stocke **aucun** sommet de grille (`firstVertex=lastVertex=0xffff`, CONVERT.C:1948-1949) :
   il coûte ~2 o/cellule (`level_texture`) + 1 o de lumière par sommet de grille. Les **faces explicites**
   coûtent 8 o/sommet + 10 o/cellule (`sFaceType`). Les **sols et plafonds** (14-27 % du bloc retail)
   étaient omis. Résultat : le classement des niveaux était faux.

3. **La « règle budget » ne faisait rien.** Sur 91 cartes, elle ne baissait aucun ciel (le seuil
   d'octets de 450 000 n'était jamais atteint, et il était mal justifié : les tuiles, sons et séquences
   sont **hors** du bloc dont la taille est contrôlée par `LEVEL.C:41`, donc le vrai plafond géométrie
   est ~900 000 o, pas 450 000).

## ⚠ Ce modèle était LUI AUSSI faux : la cellule fait 64 unités, pas 128 (corrigé le 2026-09-11, E3)

La première version de ce fichier calculait `ceil(L/128) · ceil(H/128)`. **C'est un facteur 4 de
sous-estimation.** `SLEVEL.H:126` définit `TILESIZE 64`, et `CONVERT.C:1802-1812` pose
`tileLength = (longueur + TILESIZE/2) / TILESIZE`, minimum 1 — donc un arrondi **au plus proche** sur
64, pas un `ceil` sur 128. MESURE confirmant : sur les 66 881 murs des 24 `.LEV` retail, le rapport
longueur/`tileLength` vaut 64 pour 34 293 murs et hauteur/`tileHeight` vaut 64 pour 40 991.

Les tableaux ci-dessous sont ceux du modèle **corrigé**.

## Le modèle juste (MESURE, `budget_correct.py`, `CELL = 64`)

- **Réservation du slave** : pour un mur **parallélogramme**, `c = round(longueur/64)`,
  `r = round(hauteur/64)` (minimum 1), réservation `c*r` (`WALLS.C:1374`). Pour un mur **à faces**, la
  réservation est `lastFace − firstFace + 1` (`WALLS.C:1555`) et `tileLength/tileHeight` n'y sont
  qu'une échelle de texture. Un mur dont la réservation dépasse 1 250 est **toujours** sauté.

| corpus (128 cartes) | murs > 1250 cellules (ciel Build) | après ciel ≤ 1024 u |
|---|---|---|
| total | **421** | **183** |
| dz2/E1L5 | 66 | 35 |
| xtreme_sp/HOTH | 18 | 18 |
| d13/E1L5 et atomic/E1L5 | 74 | 7 |
| **d13/E1L1** | **1** (mur 788, secteur Build 169, 1 350 cellules) | **1** |

- Le plafonnement du ciel ne règle que **57 %** des cas (421 → 183) : les murs restants sont de longs
  murs intérieurs, que la hauteur du ciel ne touche pas.
- **Conclusion révisée** : le découpage des faces n'est pas un raffinement mais **le** mécanisme ; la
  règle de hauteur n'est qu'un appoint. E3 (`geom3d.py`) l'implémente, avec un plafond de 256 cellules.
- Bornes **dures** supplémentaires trouvées en E3, plus serrées que le budget slave pour un
  parallélogramme : `drawRectWall` fait `assert(tileLength*tileHeight < MAXVPERWALL)` (`WALLS.C:1017`)
  et `rectTransform` remplit `(tileLength+1)(tileHeight+1)` entrées dans un `vCalc[700]`.
  MESURE retail : le plus gros parallélogramme fait **242** cellules, le plus gros mur à faces **376**
  faces.

## Ce que ça change pour la règle de budget

- La valeur par défaut de la hauteur du ciel **peut** être déduite du budget : partir de la valeur
  visuelle (H1), descendre par paliers de 128 tant qu'un mur de la carte dépasse 1 250 cellules (réservation
  du slave) ou que la géométrie approche 900 000 o (avec le bon modèle d'octets).
- Il y a **trois** budgets statiques/périodiques distincts à surveiller, pas un :
  1. **secteurs** ≤ 600 (`MAXNMSECTORS`) — DUKEDC4/DUKEDCSL ont ~1 020 secteurs Build : la découpe/fusion
     d'E2 doit les ramener sous 600, ou la carte est trop grande ;
  2. **murs** ≤ 5 500 (`MAXNMWALLS`) ;
  3. **octets** < 900 000 (géométrie du bloc `LEVEL.C:41`).
  Et un budget **par image** : 1 250 cellules de slave.

## Reste HYPOTHÈSE / à faire

- `budget_correct.py` estime `c*r` sur la pleine hauteur du secteur (borne supérieure) ; il ne fait pas
  le parcours de portails par image (somme des murs visibles). La partie « un mur > 1 250 » est exacte ;
  la partie « somme par image » reste à brancher sur le parcours de `frame_cost` (corrigé pour compter
  les cellules **totales**, pas visibles).
- Le modèle d'octets est une borne basse (parallélogramme seul). Il faut choisir, en E3, quels murs
  seront parallélogramme et lesquels en faces explicites, pour un décompte exact.
- Les sprites (drawSprites) et l'eau (slave_drawWater) ne sont pas comptés dans le budget slave.
