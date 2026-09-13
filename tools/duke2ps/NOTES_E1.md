# E1 — import Build et nettoyage d'E1L1 (2026-09-10)

Code : `tools\duke2ps\build_import.py` (appuyé sur `tools\buildmap.py`). Sorties : `build\duke2ps\e1l1_import.json`
(860 088 o), `build\duke2ps\e1l1_import.png` (vue de dessus cadrée sur la partie gardée), `build\duke2ps\e1_run.log`.
Rien n'a été commité et aucun fichier suivi n'a été touché.

## Reproduire

```powershell
python tools\duke2ps\build_import.py                       # JSON + PNG ; code retour 0 si tous les critères passent
python tools\duke2ps\build_import.py --no-png
python tools\duke2ps\build_import.py --cap-all-ceilings    # variante : plafonne aussi les plafonds NON ciel à Y 1760
```

- Entrée : `refs\build\duke13\maps\E1L1.MAP`, sha1 `66dd095de213099a305544220b7d3e6c7633b9e9`, v7, 317 secteurs,
  1 937 murs, 639 sprites.
- Déterministe (MESURE) : deux exécutions donnent un JSON identique octet pour octet (sha1 `712FC0B7…`).
- Repère : X = x/8, Z = −y/8, Y = −z/128, sans décalage (décision owner).

## Critères d'acceptation (MESURE, sortie du script)

| critère | résultat |
|---|---|
| une seule composante (graphe final des portails) | **OK** : 1 |
| 0 secteur de hauteur ≤ 0 (au point de référence ET à chaque sommet, pentes via getzsofslope) | **OK** : 0 |
| 0 boucle auto-intersectée (entier exact : croisement, contact entre arêtes non adjacentes, pic, arête nulle) | **OK** : 0 (14 boucles défectueuses dans la carte d'origine, toutes dans la partie gardée) |
| orientation cohérente | **OK** : 302 boucles = 276 extérieures + 26 trous ; clockdir() et le signe des lacets concordent sur 302/302 ; tous les trous dans leur extérieure |
| symétrie des murs rouges | **OK** : 1 036/1 036 (100 %) |
| composante gardée = 293 / toit = 18 | OK / OK |
| départ dans un secteur vivant | OK |
| plafonds de ciel ≤ Y 1760 | OK |
| **secteurs vivants** | **276** (1 648 murs, 572 sprites) |

**Convention d'orientation (SOURCE `jfbuild/src/build.c:5732-5766`, `4401/4428/4470`).** Dans le repère Build
(y vers le bas), la boucle extérieure est CW au sens `clockdir() == 0`, soit une somme de lacets
Σ(xᵢ·yᵢ₊₁ − xᵢ₊₁·yᵢ) > 0. Les trous sont CCW (< 0). L'éditeur insère une nouvelle boucle intérieure à
`sector.wallptr` : **l'extérieure n'est donc pas forcément la première** (5 secteurs dans ce cas). Il faut la
trouver par son signe. En repère Saturn, le signe s'inverse : l'extérieure a Σ(Xᵢ·Zᵢ₊₁ − Xᵢ₊₁·Zᵢ) < 0.

Contrôles faits en plus, hors du script (MESURE) :
- **Bilan d'aire exact.** 2 × aire des 293 secteurs = 1 650 146 979. On retire les volumes pleins
  (38 862 848) et on ajoute les enveloppes R-25 (574 464) : on obtient 1 611 858 595, soit exactement la
  somme `area2_build` de la sortie (écart 0). En aire Saturn : 12 891 773 → 12 592 645 u².
- 1 619 murs non modifiés ont exactement les coordonnées et le picnum de la carte. 29 murs ont été modifiés.

## 1. Composantes (MESURE)

On trouve 6 composantes par `nextsector` : 293, 18, 3, 1, 1 et 1 secteurs. Seule la plus grande est gardée.

| retirée | secteurs | raison |
|---|---|---|
| toit (M1b) | 232-240, 254, 271-276, 309, 310 | contient `cursectnum` 309 ; seul lien vers le reste : SE7 552 ↔ 84 (hitag 252) |
| îlot | 126, 186, 187 (x 30720..31744, y 50176..50752) | aucun portail vers la partie gardée, aucun SE7/SE17/SE23 ; 8 sprites (3 MASTERSWITCH, 4 SE, 1 picnum 1247) : une « salle-machine » |
| îlots | 314, 315, 316 (x 64000..65408, y 64896..65408) | aucun portail, aucun sprite |

Les sprites de la partie gardée sont tous conservés (572). 35 d'entre eux sont dans un secteur supprimé
(SE11, SE20, MUSICANDSFX, etc.) : ils sortent avec `sector = null` et gardent `build_sector`.

## 2. Portes, rideaux, vantaux et volumes pleins

La partie gardée contient 18 secteurs fermés (plafond ≤ sol) : lotag 0 × 4, 20 × 4, 23 × 6 et 27 × 4. Les
deux autres lotag 0 fermés de la carte (237, 238) sont dans le toit.

La **dynamique** citée pour chaque règle vient du code (SOURCE). L'**état statique retenu** est à chaque fois
une HYPOTHESE à valider sur console.

**R-20 : porte de plafond (4 portes fermées).**
- SOURCE : à l'ouverture, `sector.c:805-819` fait monter `ceilingz` jusqu'au plafond du voisin donné par
  `nextsectorneighborz(sn, ceilingz, -1, -1)` (`engine.c:8534`), c'est-à-dire le plafond voisin le plus
  proche **au-dessus**, les murs étant parcourus dans leur ordre.
- Règle : on applique exactement cette formule ; le sol reste inchangé.
- Résultat :

| porte | voisin | plafond obtenu | hauteur |
|---|---|---|---|
| 85 | 213 | −71680 | 224 u |
| 101 | 102 | −37888 | **64 u** |
| 242 | 241 | −63488 | 128 u |
| 286 | 287 | −24576 | **64 u** |

- 101 et 286 ne s'ouvrent qu'à 64 u : c'est la valeur Duke. Elle est inférieure aux 94 u de la sphère PS
  (rayon 47), donc ces portes sont infranchissables avec le rayon par défaut.

**R-23 : vantaux de portes battantes (les 11 secteurs lotag 23, tous porteurs d'un SE11).**
- Plan : « vantaux supprimés ». Je l'ai appliqué aux **11** vantaux, pas seulement aux 6 fermés : les 5 autres
  sont aussi des vantaux fermés, simplement de hauteur non nulle.
  - 79 et 80 (dans 313) : sol relevé de 128 u.
  - 97 et 98 (dans 305) : sol relevé de 128 u.
  - 143 (dans 144) : plafond abaissé à 16 u du sol (jour sous la porte).
- MESURE : les 11 sont des îles, c'est-à-dire une boucle dont tous les murs sont rouges vers un même conteneur.
  - Dans 147 (145, 146), le vantail est un vrai trou.
  - Dans 93 (53, 54), 120 (86, 87), 313, 305 et 144, le vantail est une **sous-chaîne pincée** de la boucle
    extérieure : il touche le chambranle en un sommet visité deux fois. C'est la cause de 5 des 14 boucles
    auto-intersectées.
- Règle : on supprime le vantail, ainsi que sa boucle-trou ou sa sous-chaîne jumelle dans le conteneur. Aucune
  coordonnée ne bouge.
- HYPOTHESE : dans Duke, le vantail ouvert pivote de 90° autour du SE11 (`actors.c:5753-5810`) et reste un
  obstacle. Nous le retirons : on perd le visuel du vantail et on gagne un peu de place.

**R-27 : lotag 27 (6 secteurs).**
- MESURE : dans E1L1, ce ne sont **pas des ponts** mais des bandes-rideaux de 64 u Build (8 u) d'épaisseur et
  de 3 007 à 3 136 u Build de long.
  - 21 et 22 : y 58784..58848.
  - 150 et 151 : îles dans 152, y 58976..59040.
  - 1 et 2 : hauteur 16 u.
  - Chacun contient un SE20, un GPSPEED de lotag 2560 et un ACTIVATOR 135.
- SOURCE :
  - Le SE20 tire les deux sommets les plus proches de lui le long de son angle (`game.c:4755-4798`,
    `actors.c:6297-6361`).
  - Les angles sont 1024 et 0, pointés vers l'extérieur : l'ouverture **rétracte** donc la bande.
  - Course : GPSPEED donne `sector.extra` = 2560, puis `SE.yvel` (`premap.c:771`, `game.c:4604`). L'arrêt se fait
    à 2 552 u Build.
  - Il reste donc dans Duke un moignon de 455 à 584 u Build (57 à 73 u) pour 21, 22, 150 et 151.
  - Les sommets tirés sont partagés avec 19, 20 et 23, que Duke déforme en trapèzes.
- Règle :
  - a) Une île est supprimée avec son trou (150 et 151).
  - b) Sinon, « air » : plafond = le plus bas des voisins ouverts, sol = le plus haut. On copie les attributs de
    surface du voisin qui donne la valeur, et les surfaces sont plates. Pour 1, 2, 21 et 22, le sol est −8192
    et le plafond −53248, venu de 23 (hauteur 352 u).
- HYPOTHESE : on ignore le moignon et la déformation de 19, 20 et 23. La règle du plan (« pont déployé, sol du
  voisin ») est sans objet ici : le sol valait déjà celui des voisins.

**R-25 : portes coulissantes SE15 (215 et 216, 219 et 220).**
- Ces secteurs ne sont pas « fermés » au sens de la hauteur, mais leur boucle est dégénérée : le panneau est
  replié en pics de largeur nulle, et deux pointes en V de 30 u² se touchent au milieu. Cela représente 4 des
  14 défauts. Sur la carte : le panneau lui-même est le vide plein entre les pics, et la longueur des pics est
  la poche dans laquelle il coulisse.
- Règle : panneau ouvert = enveloppe convexe de ses sommets.
  - Ses 3 portails tombent sur l'enveloppe et sont gardés.
  - Le bord de poche restant devient un mur plein neuf, texturé comme le plus long mur plein retiré.
  - On vérifie qu'aucun sommet ni aucun mur d'un autre secteur n'entre dans l'enveloppe.
- Résultat : 4 rectangles de 128 × 576 u Build (16 × 72 u, 1 152 u²) et 4 murs neufs.
  - Porte A : x 15424..15552, y 60352..61504.
  - Porte B : x 17216..18368, y 55872..56000.
  - Ouverte, chaque porte laisse un passage de 144 u de large dans un mur de 16 u d'épaisseur.
- HYPOTHESE : porte entièrement escamotée, la course étant prise égale à la longueur des pics. SOURCE lue
  partiellement : `actors.c:5944-5974` (le SE15 avance de `SP>>3` pas, et `ms()` déplace tout le secteur) ;
  la course exacte n'a pas été calculée.

**R-0 : volume plein (lotag 0 fermé sans rôle, 4 secteurs en 2 grappes).**
- Grappe [155, 211, 212] : bande à sol Y 448 et plafond de ciel, entre la rue (183, 210, 258 ; sol Y −64/−72) et
  les toits 153 et 164 (sol Y 448).
- Grappe [292] : bande au bord de 294, au même sol Y 576, avec plafond de ciel.
- **Comment on le traite** : le secteur est supprimé. Chaque mur rouge d'un voisin vers lui devient plein
  (`nextwall = nextsector = −1`), en gardant son cstat et son picnum ; `solidified_from` note l'id Build du
  volume. Au total, 6 murs.
- Dans Build, un voisin de hauteur nulle dessine un haut et un bas de mur qui se rejoignent : le rendu est donc
  identique, **sauf avec le ciel** (MESURE ci-dessous).
- Duke ne dessine ces 6 murs que jusqu'au sol du volume, avec le ciel au-dessus. Les murs de 153, 164 et 294
  sont même **invisibles** (sols égaux) : ce sont des bandes de collision invisibles.
  - Un mur plein monterait au plafond, c'est-à-dire Y 1760.
  - Les 6 murs mesurent 96, 181, 256, 352, 621 et 1 040 u.
- Le JSON garde `solidified_info` (floorz, stats, `ciel_au_dessus`) pour que l'étape E3 puisse arrêter ces murs
  à Y 448/576 et laisser le ciel au-dessus. Que PS dessine le ciel derrière une géométrie absente reste une
  HYPOTHESE non vérifiée.

**Nettoyage topologique exact, après les règles.**
- T-pic : on replie les 9 pics de largeur nulle.
  - Pics dans 117 (1) et dans les bandes de poche 217, 218, 221 et 222 (2 chacune).
  - Quand le mur replié est un portail, son jumeau est coupé à la jonction en T : 16 coupes.
  - Les côtés extérieurs deviennent jumeaux, ou bien pleins avec la texture du mur plein qui était vu : 8 murs.
- T-pin : aucune boucle pincée ne restait après R-23.
- Traçabilité : chaque mur coupé ou raccourci garde `orig_seg`, le segment Build d'origine, parce que
  xrepeat/xpanning portent sur le mur entier. Chaque mur retexturé porte `tex_from`.

## 3. Ciel

- Règle : un plafond de ciel dont le Y à un sommet dépasse 1760 prend `ceilingz` = −225280 ; une éventuelle
  pente serait retirée.
- MESURE : 20 plafonds de ciel dans la partie gardée, dont 16 plafonnés (Y avant : 3 632 à 3 640). Aucune pente
  aplatie, aucun refus. Le plafonnement est fait avant R-20, qui lit donc les plafonds voisins finaux ; aucun
  voisin donneur n'est un ciel.
- **Reste au-dessus de 1760 (information)** : 6 plafonds **non ciel**, laissés tels quels puisque la consigne
  vise le ciel seul.
  - 154 (3 400), 163 (3 384), 169 (3 384), 203 (3 400), **259, le secteur de départ (3 640)**, et 306 (3 336).
  - HOLYWOOD n'a aucun sommet au-dessus de 1760 (§2.2 du plan) : Lobotomy les a donc traités aussi.
- Variante `--cap-all-ceilings` (MESURE) : 5 plafonds non ciel plafonnés, dont 2 pentes aplaties (163, 169).
  - **306 est refusé** : son sol est déjà à Y 3 216. Un premier essai le supprimait comme volume plein ; c'est
    corrigé, un plafonnement ne supprime jamais rien.
  - Résultat : 276 secteurs, tous les critères OK, Y max = 3 336. Il faudra décider ce qu'on fait de 306.

## 4. Départ M1

- SOURCE, appariement des SE7 (`game.c:4613-4628`) : SE7 552 (toit, secteur 272, (−25849, 14774, −35840)) est
  apparié à SE7 84 (secteur 259, (9206, 41590, −70656)), tous deux de hitag 252.
- `onfloorz` vaut 0 pour les deux : z du sprite ≠ sol du secteur. On est donc dans la branche « air »
  (`actors.c:2728-2748`), et c'est MESURE, pas une hypothèse. Quand |posz − z du SE| < 6144, on a
  xy += OW − SE et posz = OW.z + 6144 ; **l'angle n'est pas modifié**.
- **Départ** :
  - Secteur Build 259 (id 230 après nettoyage), position Build (9206, 41590).
  - Sol plat à z 8192 ; plafond −465920 (non ciel).
  - **Saturn** : X 1150,75, Z −5198,75, Y_sol −64,0. Le joueur PS serait à Y = sol + 48 = −16 (règle E5 du plan,
    HYPOTHESE).
- **Angle** : ang Build 422 (74,18°), celui du départ du niveau, que le transport conserve. Dire que le joueur
  n'a pas tourné avant de sauter est une HYPOTHESE. L'angle propre du SE7 84 (624) n'est pas lu par Duke dans
  cette branche. Direction dans le plan Saturn XZ : (0,272 ; −0,962). La conversion en unité d'angle PS relève
  d'E5.
- MESURE : le point est dans le secteur 259 (test exact). Le mur le plus proche est à 55,4 u, et c'est un
  portail : le secteur 259 n'a aucun mur plein. La sphère PS de rayon 47 tient donc à l'horizontale.
- Dans Duke, l'œil arrive à z −64512, puis chute de 568 u.

## 5. Format du JSON (`duke2ps/e1-import v2`)

- `sectors[]`, dans l'ordre d'origine, une ligne par secteur :
  - identifiants : `id`, `build_id` ; `wallptr`/`wallnum` contigus ; `loops` (listes d'ids de murs) ;
  - tous les champs Build du secteur, après traitement ;
  - `slope_ref` : premier mur **d'origine** pour les pentes. Aucun secteur en pente n'a vu ce mur déplacé,
    mais il faut toujours utiliser ce champ ;
  - `area2_build`, `treatment`, `saturn`.
- `walls[]` :
  - identifiants et géométrie : `id`, `build_id` (null pour les 4 murs neufs), `sector`, `x`, `y`, `point2`,
    `nextwall`, `nextsector` (recalculés) ;
  - les attributs Build ;
  - traçabilité : `solidified_from`, `solidified_info`, `prov` (historique), `tex_from`, `orig_seg`.
- `sprites[]` : tous les sprites de la partie gardée, avec `sector` (nouvel id, ou null) et `build_sector`.
- Rapports :
  - `retrait_composantes`, `regles_appliquees`, `volumes_pleins_grappes`, `secteurs_supprimes` ;
  - `nettoyage_topologique`, `ciel_plafonne`, `se7`, `depart_m1`, `speciaux_non_traites`, `bbox_saturn`,
    `criteres`.
- bbox Saturn : X −256..4544, Z −8128..−3072, Y −96..3640 (Y 3336 avec `--cap-all-ceilings`).

## Limites connues et points à décider

1. **Toutes les règles R-* sont des HYPOTHESES d'état statique**, à valider sur console :
   - vantaux absents ;
   - moignons de rideaux ignorés ;
   - portes coulissantes entièrement escamotées ;
   - volumes de ciel changés en murs pleins (voir R-0 et `solidified_info`).
2. Le plan demandait d'appliquer R-23, R-27 et R-25 aux secteurs fermés. Je les ai **étendues** à 5 vantaux,
   2 rideaux et 4 panneaux de hauteur non nulle, qui sont eux aussi des portes fermées. Sans ces extensions, le
   critère « 0 auto-intersection » échoue (14 boucles).
3. **Plateformes lotag 17 (55 et 302) laissées dans leur état initial.** SOURCE : `sector.c:713-737`, la
   plateforme va jusqu'au sol voisin suivant. 55 est relevée : sol Y 184 contre Y −64 pour 109, soit une
   ouverture de −120 u. Cela peut couper un chemin ; à regarder.
4. Autres secteurs non traités : lotag 3 (171, 172, 177, 206, avec SE13/SE4) et 32767 (6 secrets).
5. Ouvertures (MESURE, hauteurs plates) :
   - 518 portails entre secteurs vivants ; 46 ont une ouverture verticale < 90 u, 7 < 47 u, 3 ≤ 0
     (5↔203, 55↔109, 174↔266 ; ce dernier est en pente, sa valeur plate n'a pas de sens) ;
   - 29 secteurs font moins de 94 u (minimum 40 u, secteur 205) ;
   - c'est l'entrée du bouton « rayon du joueur ».
6. Textures des murs coupés : les attributs d'origine sont recopiés tels quels. xrepeat/xpanning ne sont **pas**
   répartis ; E4 doit plaquer sur `orig_seg`.
7. Hauteurs aux sommets en pente : `math.isqrt` remplace `nsqrtasm`, qui est une table approchée. Cela ne
   touche que le contrôle des hauteurs aux sommets ; aucun cas limite n'est apparu.
8. Écarts au plan : « 20 secteurs fermés » y compte ceux du toit, ce qui donne 18 ici. Le départ « secteur 259,
   (9206, 41590), Y = −64 + 48 » est confirmé.
