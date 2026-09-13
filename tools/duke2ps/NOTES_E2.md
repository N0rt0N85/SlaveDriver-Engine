# E2 — découpe convexe d'E1L1 (2026-09-10)

Code : `tools\duke2ps\convex.py` (autonome, lit la sortie d'E1). Sorties : `build\duke2ps\e1l1_convex.json`
(301 882 o, sha1 `0717bb1fca60…`), `build\duke2ps\e1l1_convex.png` (vue de dessus des morceaux),
`build\duke2ps\e2_run.log`. Rien n'a été commité et aucun fichier suivi n'a été touché.

## Reproduire

```powershell
python tools\duke2ps\convex.py                        # JSON + PNG ; code retour 0 si tous les critères passent (~16 s)
python tools\duke2ps\convex.py --selftest             # 5 cas synthétiques (trou, peigne, colinéaires, fusion, jonction en T)
python tools\duke2ps\convex.py --no-fusion-secteurs   # sans fusion entre secteurs Build identiques
python tools\duke2ps\convex.py --tol-u 0.5            # tolérance de convexité des fusions (défaut 0 = exact)
python tools\duke2ps\convex.py --random 400           # plus d'essais aléatoires par secteur (défaut 64)
python tools\duke2ps\convex.py --split-solid-t        # scinder aussi les recouvrements entre murs pleins dos à dos
python tools\duke2ps\convex.py --no-png --no-holywood
```

- Entrée : `build\duke2ps\e1l1_import.json` (sha1 `712fc0b7…`) : 276 secteurs, 1 648 murs, 572 sprites.
- Déterministe (MESURE) : deux exécutions donnent un JSON identique octet pour octet (sha1 `0717bb1f…`). Les essais
  aléatoires sont semés par `sid × 100003 + t`.

## Méthode (tout en entiers exacts, aucun point de Steiner)

1. **Pont vers les trous** : pour chaque trou (x max décroissant), on part de son sommet le plus à droite et on va au
   sommet visible le plus proche. Le pont est validé par des tests de cône stricts aux deux bouts et par l'absence de
   contact avec toute arête, hors extrémité commune. Il devient une diagonale interne, jumelée avec lui-même.
2. **Triangulation par oreilles** du polygone faiblement simple. Une oreille est valide si le coin est strictement
   convexe (orient > 0), si la diagonale est dans le cône intérieur à ses deux bouts et si elle ne touche aucune arête
   ni aucun sommet. Ce test tient compte des sommets dupliqués par les ponts.
3. **Hertel-Mehlhorn** : on retire une diagonale si l'union des deux morceaux reste convexe. L'union est calculée sur les
   arêtes : on supprime les jumelles, puis on rechaîne en un seul cycle ; un sommet répété ou deux cycles font refuser
   la fusion.
4. **Fusion gloutonne** jusqu'à stabilité : des paires (arête commune la plus longue d'abord), puis des triplets (A avec
   deux voisins, ou une chaîne A-B-C).
5. Par secteur, on essaie 6 stratégies d'oreilles (première à 3 départs, meilleur angle, diagonale la plus courte, la
   plus longue) × 2 ordres HM, plus 64 tirages aléatoires tant que la borne basse du secteur n'est pas atteinte. On
   garde le moins de morceaux, puis la plus grande aire minimale.
6. **Fusion entre secteurs Build voisins identiques**, avec la même union convexe. « Identique » veut dire :
   - plans de sol et de plafond égaux (pentes comprises) : `z` et `heinum` égaux, droite de référence parallèle, test
     exact au carré ;
   - `stat`, `picnum`, `shade`, `pal`, `xpanning` et `ypanning` égaux ;
   - `lotag` et `hitag` égaux.

   Le mur commun ne doit porter ni cstat 1, 16, 32 ou 64 (bloquant, masqué, sens unique, bloque les tirs), ni lotag ou
   hitag, des deux côtés. J'ai ajouté panning, stat et hitag à la liste demandée, par prudence.
7. **Jonctions en T** : on balaie, droite par droite (droite canonique entière), les recouvrements colinéaires de longueur
   non nulle entre arêtes de morceaux différents. Une paire opposée qui n'est pas une paire de jumelles exactes est une
   jonction en T : l'arête interne est coupée aux extrémités de l'autre. Les murs pleins dos à dos ne sont coupés
   qu'avec `--split-solid-t`.
8. **Jumelles** : une diagonale a pour jumelle l'arête de même id en sens inverse ; un portail, l'arête qui porte
   `nextwall` en sens inverse, aux mêmes extrémités. Les deux sont exactes.

Les **sommets colinéaires** (180°) sont admis dans les morceaux : sans eux, le voisin aurait un sommet au milieu d'une
arête (jonction en T). Il y en a 233, dans 100 morceaux. Les pics (aller-retour) sont refusés.

## Critères d'acceptation (MESURE, sortie du script)

| critère | résultat |
|---|---|
| tous les morceaux convexes (0,5 u) | **OK** : dépassement max 0,000 u ; 446/446 strictement convexes en exact |
| somme des aires = aire Build par secteur (± 0,1 %) | **OK** : égalité exacte sur 276/276 secteurs |
| somme des aires = aire Build totale (± 0,1 %) | **OK** : 1 611 858 595 = 1 611 858 595 (2 × aire Build), écart 0 ; 12 592 645,27 u² |
| chaque arête interne a exactement une jumelle | **OK** : 1 428 arêtes internes, 0 sans jumelle, 0 asymétrique |
| 0 jonction en T restante | **OK** : 0 trouvée, 0 coupe ; recouvrements : 714 paires de jumelles exactes et rien d'autre |
| ≤ 520 morceaux (plafond dur 600) | **OK** : **446** |
| aucun morceau < 1 u² | **OK** : minimum 7,5 u² |
| (en plus) chaque mur couvert exactement une fois | **OK** : 1 648 murs ; 20 portails fondus dans des morceaux multi-secteurs |
| (en plus) départ M1 dans un morceau | **OK** : morceau 325 (secteur 230 = Build 259) |

**Contrôle de partition** : les pièces sont toutes orientées positivement, chaque mur est couvert une fois, chaque
arête interne a une jumelle exacte et les aires sont égales à l'unité près. On a donc une partition exacte, sans trou
ni recouvrement.

## Chiffres (MESURE)

- **446 morceaux** : 456 après la découpe intra-secteur, puis 10 fusions entre secteurs (6 morceaux multi-secteurs).
- 215 secteurs sont déjà convexes et sans trou, donc 1 morceau chacun. 61 secteurs sont découpés : 27 atteignent leur
  borne basse, 34 la dépassent, pour un excès total de 77.
- **Bornes recalculées sur les 276 secteurs gardés** : ⌈r/2⌉+1−h = **379** (somme par secteur, sans fusion entre
  secteurs) ; r+1−h = **481** (231 sommets rentrants, 26 trous).
  - Les bornes du plan, **471-618**, portaient sur les 317 secteurs de la carte, toit, îlots et portes compris.
  - Nos 446 sont sous le bas de l'ancienne fourchette, mais à +67 de la nouvelle borne basse (+77 avant fusion).
- **HOLYWOOD : 415 conteneurs au total.** Comparaison à périmètre égal (HYPOTHESE de méthode) : on prend le centre du
  conteneur (+4, +8 ; x = cx·8, y = −cz·8) et on le teste dans nos 276 secteurs, avec l'altitude (+10) à 8 u près du
  Y du sol.
  - **374/415** centres tombent dans l'emprise gardée ; **292** sont appariés en altitude, sur 170 secteurs.
  - Sur ces 170 secteurs, nous avons **298** morceaux : quasi à parité.
  - Les 82 conteneurs non appariés en altitude et les 41 hors emprise (le toit, entre autres) ne sont pas classés.
- **Arêtes** : 612 murs pleins, 1 016 arêtes de portail (508 paires), 412 arêtes de diagonale (206 paires) ;
  865 sommets distincts.
- **Morceaux** : 3 à 17 sommets (56 triangles, 260 quadrilatères, 53 pentagones…). Aire médiane 4 480 u², p10 384 u².
- **Fusions entre secteurs** : 17 paires de voisins identiques, 10 fusions. Morceaux multi-secteurs (ids Build) :
  [3, 15], [16, 17], [166, 167, 168, 226, 227, 228], [215, 216], [219, 220], [290, 291].
  - [215, 216] et [219, 220] sont les **deux moitiés de chaque porte coulissante R-25** d'E1.
  - Pour un état statique, ce n'est pas gênant. Si ces portes doivent redevenir mobiles plus tard :
    `--no-fusion-secteurs`, qui donne 456 morceaux.
- **Sprites** : les 537 sprites qui ont un secteur tombent tous dans un morceau de leur secteur (0 rattaché au plus
  proche). Les 35 sans secteur restent `null`.

### Secteurs Build les plus découpés (morceaux / borne basse / HOLYWOOD apparié)

| Build | morceaux | borne basse | r | trous | murs | HOLYWOOD |
|---|---|---|---|---|---|---|
| **295** (salle ronde, 6 trous) | **28** | 15 | 39 | 6 | 62 | 18 |
| 131 | 10 | 5 | 8 | 0 | 23 | 0 |
| 263 | 10 | 6 | 14 | 2 | 28 | 8 |
| 252 | 8 | 3 | 8 | 2 | 20 | 0 |
| 177 | 7 | 4 | 8 | 1 | 19 | 6 |
| 230 | 7 | 3 | 8 | 2 | 16 | 8 |
| 304 | 7 | 4 | 9 | 2 | 15 | 0 |
| 305 | 7 | 5 | 13 | 3 | 36 | 11 |
| 313 | 7 | 3 | 6 | 1 | 24 | 6 |
| 210 | 6 | 2 | 4 | 1 | 8 | 2 |

« 0 » signifie qu'aucun conteneur n'a été apparié en altitude à ce secteur. Ça ne veut pas dire que Lobotomy n'y a
rien mis (voir limite 5). Sur 230 et 305, HOLYWOOD a **plus** de conteneurs que nous (8 contre 7, 11 contre 7). La
seule vraie économie de Lobotomy est donc le 295.

Plus grands écarts « nous − HOLYWOOD » (secteurs appariés) : 295 (+10), 210 (+4), 200 (+4 : 5 contre 1), 163 (+3),
puis 301, 263, 223, 182, 128 et 63 (+2).

### Variantes mesurées

| variante | morceaux | coût |
|---|---|---|
| défaut (exact, 64 tirages) | **446** | 16 s |
| `--tol-u 0.5` (fusions tolérantes, pire dépassement 0,088 u) | 445 | 17 s |
| `--random 400` | 444 (295 reste à 28) | 80 s |
| `--no-fusion-secteurs` | 456 | 13 s |

La tolérance et la recherche aléatoire plafonnent. Le gain restant est structurel : il faudrait des points de Steiner,
ou simplifier le secteur 295.

## Format du JSON (`duke2ps/e2-convex v1`)

- `pieces[]`, une ligne par morceau :
  - `id`, `sectors` (ids E1), `sectors_build`, `area2_build`, `area2_par_secteur`, `aire_u2` ;
  - `verts` : coordonnées Build entières, dans le sens des boucles **extérieures** Build. La somme de lacets est > 0
    (`clockdir() == 0`) ; en Saturn (X = x/8, Z = −y/8), Σ(Xᵢ·Zᵢ₊₁ − Xᵢ₊₁·Zᵢ) < 0. Le morceau est à gauche de chaque
    arête dans le plan (x, y) cartésien. Rotation : premier sommet = plus petit (x, y) ;
  - `edges[k]` va de `verts[k]` à `verts[k+1]` :
    - `type` : `mur`, `portail` ou `diagonale` ;
    - `wall` et `build_wall` pour un mur ou un portail ; `nextsector` pour un portail ;
    - `diag` et `sector` pour une diagonale ;
    - `twin` = [morceau, k] pour les arêtes internes.
  - Un mur Build peut apparaître en plusieurs arêtes s'il a été coupé par une jonction en T (aucun cas ici).
- `secteurs[]` : pour chaque secteur E1, les réflexes, les trous, la borne basse, `morceaux_intra`, `morceaux` (ids),
  la stratégie retenue et le résultat de chaque essai.
- Rapports :
  - `bornes`, `fusion_inter_secteurs`, `aretes`, `sommets_distincts`, `holywood` ;
  - `depart_m1` (morceau 325) ;
  - `sprites_morceau.table` (indexée par le rang du sprite dans e1l1_import.json) ;
  - `plus_decoupes`, `criteres`.
- Pour E3 :
  - une `diagonale` devient un portail INVISIBLE des deux côtés ;
  - un `portail` garde son mur Build : textures haut et bas, et `orig_seg` via `wall` dans e1l1_import.json ;
  - les hauteurs et surfaces d'un morceau sont celles de n'importe lequel de ses `sectors`, identiques par
    construction.

## Hypothèses et limites

1. **Optimalité non prouvée.** On est à +67 au-dessus de la borne basse ⌈r/2⌉+1−h. Cette borne n'est pas forcément
   atteignable sans points de Steiner, et la découpe convexe minimale avec trous est NP-difficile.
   - Le secteur 295 à lui seul pèse +13 (28 contre 15 ; Lobotomy en a 18).
   - Leviers possibles, non implémentés : points de Steiner (couper un mur plein, ce qui ajoute des murs à E3),
     simplification manuelle du 295, découpe optimale par programmation dynamique pour les secteurs sans trou.
2. **Clé d'identité plus stricte que la consigne** (panning, stat, hitag en plus ; murs à drapeaux jamais fondus). Elle
   laisse 7 des 17 paires identiques non fusionnées, faute d'union convexe.
3. **L'égalité des plans en pente** suit la formule réelle de `getzsofslope`, sans les troncatures de Build ni la table
   `nsqrtasm`. Ce qui compte pour Saturn, c'est le plan réel.
4. **La jonction en T n'a jamais servi sur E1L1** (0 cas ; E1 avait déjà fait les 16 coupes). La coupe n'est donc
   testée que sur le cas synthétique de `--selftest`.
5. **La comparaison HOLYWOOD** attribue un conteneur à un secteur par son centre et la loi alt = −floorz/128 : c'est une
   HYPOTHESE de méthode, et 123 conteneurs ne sont pas classés. Le script `compare_holywood.py` du plan (§7) reste à
   écrire, avec les vrais polygones des conteneurs.
6. **Les portes coulissantes R-25 sont fusionnées par paires** (deux moitiés identiques) : c'est un choix d'état
   statique, qu'on inverse avec `--no-fusion-secteurs`.
