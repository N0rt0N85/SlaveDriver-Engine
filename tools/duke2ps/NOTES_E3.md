# E3 — géométrie 3D génération 1 (2026-09-11)

Modules `tools\duke2ps\geom3d.py` (générateur) et `tools\duke2ps\verif_e3.py` (vérificateur
contradictoire, écrit sans importer le générateur). Sortie `build\duke2ps\e1l1_geom3d.json`.
Non suivis, jamais commités.

## Méthode

Aucune convention du format n'a été devinée. Chacune est soit **mesurée sur les 24 `.LEV` retail**,
soit relue dans l'émetteur de Lobotomy (`UTIL\CONVERT.C`) et dans le moteur (`WALLS.C`), et les deux
sources se recoupent à chaque fois.

| convention | source | vérifiée sur |
|---|---|---|
| `TILESIZE` = **64** unités | `SLEVEL.H:126`, `CONVERT.C:1802` | 66 881 murs retail : longueur/`tileLength` = 64 (34 293) |
| `v0,v1` = arête haute, `v1→v2` = hauteur | `CONVERT.C:1793-1810` | KILENTRY W0 |
| plan : normale de Newell sur `v[0..3]`, `d = −(centroïde·n)` | `CONVERT.C:1974-2001` | tous nos murs à < 1,5 u du plan |
| parallélogramme : 2 octets/cellule `[motif, tuile]` | `WALLS.C:1083`, `1141-1143` | KILENTRY W5 3×3 → W6 à 18 |
| lumières : `(L+1)(H+1)` octets | `CONVERT.C:1925-1931` | W6 20×3 → W8 à 100 |
| faces : indices de sommet **locaux au mur** | `WALLS.C:1223` `assert(v<maxV)` | 10 930 faces |
| sol/plafond = des **murs** du secteur, un de chaque | `CONVERT.C:2040-2056` | **8 408/8 408** secteurs retail |
| grille des sols = 64 u découpée par le polygone | — | 11 207 faces de 64×64 |
| `SHORTOPENING` si 1 < h < 90 ; `CLIFFBNDRY` si Δsol > 320 | `CONVERT.C:2541-2586` | 96 / 12 émis |

## Deux faits qui ont corrigé des conclusions antérieures

**1. La cellule fait 64 unités, pas 128.** La section budget corrigée du 2026-09-11
(`budget_correct.py`, NOTES_BUDGET.md) calculait `ceil(L/128)·ceil(H/128)` : elle **sous-estimait la
réservation du slave d'un facteur 4**. Corrigé (`CELL = 64`, arrondi au plus proche comme
`CONVERT.C:1802`). Conséquence mesurée sur 128 cartes : **421 murs** dépassent 1 250 cellules aux
hauteurs Build, **183** après plafond de ciel à 1 024 u — contre « 0 sur E1L1, 2 sur E1L5 » annoncé.
E1L1 lui-même en a 1 (mur 788, secteur Build 169, 1 350 cellules). **Le découpage des faces n'est donc
pas un raffinement, c'est le mécanisme principal** ; le plafond de ciel ne règle que 57 % des cas.

**2. Mon « max 748 cellules en retail » était une erreur de catégorie.** `tileLength·tileHeight` ne
compte les cellules que pour un mur **parallélogramme** ; pour un mur à faces le slave réserve
`lastFace−firstFace+1` (`WALLS.C:1555`), et `tileLength/tileHeight` n'y sont qu'une échelle de texture.
Les vraies bornes mesurées : **242 cellules** pour le plus gros parallélogramme retail, **376 faces**
pour le plus gros mur à faces. Et les bornes *dures* sont ailleurs : `drawRectWall` fait
`assert(tileLength*tileHeight < MAXVPERWALL)` (`WALLS.C:1017`) et `rectTransform` remplit
`(tileLength+1)(tileHeight+1)` entrées dans un `vCalc[700]`. Plafond retenu : **256 cellules**.

## Quatre défauts trouvés par le vérificateur

Le générateur passait ses propres critères du premier coup. Le vérificateur indépendant en a quand
même sorti quatre — c'est exactement ce pour quoi il est écrit séparément.

1. **Portail en nœud papillon.** Entre deux secteurs en pente, l'ouverture peut se refermer *en cours
   d'arête* (mesuré : 200 ↔ 342). Le quad se croisait et chevauchait la marche basse. Corrigé : le
   portail **s'effile** jusqu'à une arête de hauteur nulle à ce bout. Les bornes `lo = max(sol, sol')`
   et `hi = min(plafond, plafond')` sont des fonctions **symétriques** des deux secteurs, donc les
   deux faces décrivent le même quad.
2. **Pavage du sol qui écrase un éclat.** Le secteur 143 fait 0,66 u d'épaisseur : les deux points de
   croisement d'une coupe de grille tombaient sur le **même** entier et la moitié du sol disparaissait
   (16 u² pavés contre 32). Même règle qu'en E2.5b : on coupe sur la ligne de grille **sauf** quand la
   coupe n'est pas représentable. Le test est exact — les aires sont entières, donc on compare
   `aire(gauche) + aire(droite)` à `aire(entier)`. 260 coupes refusées, 217 polygones gardés entiers.
3. **Erreur d'arrondi qui se compose.** Une coupe interpolait sur un bord *déjà* arrondi par la coupe
   précédente : dépassement de **1,12 u**, au-delà du demi-pas théorique. Corrigé en faisant porter
   chaque croisement par l'arête **d'origine** : dépassement ramené à **0,49 u**, et le même croisement
   donne toujours le même point entier, ce qui rend le pavage étanche.
4. **Découpe désynchronisée entre les deux faces d'un portail.** `tileHeight` se mesure sur `v1→v2`,
   donc au bout B pour une face et au bout A pour l'autre ; sur un portail en pente les deux hauteurs
   diffèrent et les deux côtés ne coupaient pas au même endroit (8 portails asymétriques entre 188,
   189 et 190). Corrigé : la **décision** de couper et la coupe elle-même se prennent sur l'orientation
   canonique (`v0 ≤ v1` lexicographiquement), identique des deux côtés.

Et un cinquième trouvé en relisant le moteur plutôt qu'en testant : **un trapèze ne peut pas être
marqué `PARALLELOGRAM`**. `drawRectWall` reconstruit sa grille avec deux vecteurs constants
(`WALLS.C:1036-1049`), donc il redresserait le mur à l'écran. Mesure confirmant la règle : les
**11 712** murs `PARALLELOGRAM` du retail vérifient `v0+v2 == v1+v3` avec un écart **exactement nul**.
Nos 192 murs trapézoïdaux (pentes) partent donc en liste de faces, par interpolation bilinéaire.

## Résultat sur E1L1 (MESURE)

440 morceaux convexes → **440 secteurs, 3 564 murs, 31 555 sommets, 10 930 faces**, 25 772 octets de
texture, 20 427 lumières, 64 tuiles distinctes. Bloc niveau **589 627 octets** (limite 900 000,
`LEVEL.C:40-41`).

Critères du générateur : 12/12 OK (secteurs ≤ 600, murs ≤ 5 500, coordonnées dans un `short`, un sol
et un plafond par secteur, normales vers l'intérieur, symétrie des portails, indices de face locaux,
sommets par mur < 700, budget slave ≤ 1 250, blocs de parallélogramme contigus, budget d'octets,
départ présent).

Vérificateur indépendant : **aucune mise en défaut** sur 11 propriétés — pavage sans arête répétée,
bord du sol à moins de 1 u, aire des sols (médiane 0,000 %, p95 0,496 %, max 1,027 %, dépassement max
0,49 u), étanchéité verticale, quads dans leur plan, normales unitaires, faces non dégénérées, tuiles
cohérentes, parallélogrammes réels, grille rect sous 700, portails réciproques.

### Étalonnage contre le retail

| niveau | secteurs | murs | sommets | faces | octets | murs/sect | somm/sect | faces/sect |
|---|---|---|---|---|---|---|---|---|
| médiane retail | 349 | 2 881 | 27 232 | 10 394 | 474 351 | 8,3 | 78,0 | 29,8 |
| **E1L1 (nous)** | **440** | **3 564** | **31 555** | **10 930** | **589 627** | **8,1** | **71,7** | **24,8** |
| max retail | 570 | 4 939 | 44 906 | 16 913 | 762 614 | 8,7 | — | — |

E1L1 tombe **à l'intérieur** de la distribution retail sur chaque métrique, et les ratios par secteur
sont quasi identiques à ceux de Lobotomy. C'est le signal le plus fort dont on dispose que la
génération suit bien les mêmes conventions et la même densité.

La tolérance du critère d'aire des sols est elle aussi **calibrée sur le retail** : sur 1 933 secteurs
retail comparés à leur emprise, l'écart médian est 0,00 %, le p95 0,12 % et le maximum 3,65 %.

## Reproduire

```
python tools\duke2ps\geom3d.py       # -> build\duke2ps\e1l1_geom3d.json
python tools\duke2ps\verif_e3.py
```

## Reste HYPOTHÈSE

- **Rien n'a encore été rendu.** Toutes les propriétés ci-dessus sont géométriques ou structurelles ;
  l'aspect à l'écran ne sera jugé qu'en E7, sur console.
- **Tuiles et lumières sont des bouchons.** `tiles` ne contient que les picnums Build dans l'ordre
  (64 distincts) ; les pixels, la palette et le ciel sont le travail d'E4. Le motif de coin est 0
  partout (pas de retournement) et la lumière vient d'une conversion linéaire de `shade` non validée.
- **Le picnum du bas et du haut d'un portail** est celui du mur, alors que Build distingue le bas
  (`picnum`/`overpicnum`, bit d'échange `cstat`) : à reprendre.
- Les **8 écarts aux jonctions de pente** hérités d'E2.5 sont absorbés par l'arrondi des hauteurs
  (369 hauteurs arrondies) mais n'ont pas été rejugés un par un.
- **Testé sur E1L1 seulement**, comme E2.5b.
