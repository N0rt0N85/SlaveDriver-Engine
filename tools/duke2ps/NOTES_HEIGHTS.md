# Hauteurs des plafonds et des ciels — règles générales (2026-09-11)

Module : `tools\duke2ps\height_rules.py` (fonctions pures). Mesures, tableaux et commandes :
`build\duke2ps\rules\heights_report.md` (généré) et les scripts `build\duke2ps\rules\*.py`. Rien n'est commité, aucun
fichier suivi n'est modifié. Étiquettes : **MESURE** (script cité), **SOURCE** (fichier:ligne), **HYPOTHÈSE**.

## 0. En bref

- **27 paires PC ↔ Saturn** pour E1-E3 (23 cartes PC, dont 4 scindées en 2 `.LEV`). Il reste 7 cartes PC sans paire
  (E1L7, E1L8, E2L10, E2L11, E3L9, E3L10, E3L11) et 3 `.LEV` sans carte (LORD2, STADIUM, UREA51). REDLIGHT est décalé
  de Z +320 u. MESURE `pairs*.py`.
- **Lobotomy n'applique pas une loi unique** (MESURE `heights_constat.py`). Il n'y a pas de plafond global : le Y max
  va de 64 à 2 048 selon le niveau. Il garde presque tous les plafonds non ciel jusqu'à 1 024 u. Il baisse les ciels
  très hauts vers une valeur par niveau, qui peut remonter les ciels bas, et garde souvent les ciels moyens. Il
  déplace des zones entières en Y (archipel SE7 recollé en 3D).
- **Règle retenue : `fov_bas`**. C'est H1 + H2 + H3 selon le principe du champ de vue, avec une portée de vue
  **D = 1 024 u** et un arrondi à 128 au-dessus du sol, ajustés sur E1L1 seul. Un ciel haut descend à la hauteur de sa
  région ; un ciel bas ne monte que jusqu'au besoin de ses propres façades.
  - **Choix** : parmi six familles ajustées chacune sur E1L1, c'est celle qui prédit le mieux Lobotomy **hors E1L1**
    (826-829 secteurs ciel + hauts) : **44,9 % à ±64 u, écart moyen 235 u**. Les autres : fov 44,4 %/237, fov_secteur
    44,5 %/235, global 960 43,9 %/244, voisins 41,3 %/243. La référence « garder Build » fait 46,9 % mais 250 u.
  - **Sur E1L1** : ciels 62 % à ±64 u (170 u, contre 2 038 en gardant Build) ; plafonds hauts 50 % (110 u, contre 284).
  - **Ciels seuls hors E1L1** (zones non déplacées, 228) : 56,1 %/247 u, contre 62,3 %/277 pour « garder Build ».
    **Aucune règle ne bat Build en part à ±64 u**, parce que Lobotomy garde tels quels la plupart des ciels modestes.
    La règle ne gagne que sur l'écart moyen. Voir §4.
- **Défauts selon le budget** : la hauteur du ciel pilote les coûts statiques (cellules, sommets, MAXVPERWALL, octets),
  presque pas le coût par image (les cellules au-dessus de l'écran sont rejetées une à une). Voir §5.

## 1. Constantes du moteur (SOURCE)

| grandeur | valeur | source |
|---|---|---|
| rayon de la boule du joueur | 47 u | `AI.C:46` `newSprite(sector,F(47),…)` |
| hauteur de l'œil au repos | **55 u** au-dessus du sol (47 + 8) | `SPRITE.C:642-655` : pour la caméra, `floorDistance += F(8)`, puis `pos.y += floorDistance` ; caméra = sprite du joueur `SRUINS.C:1998` ; vue = `camera->pos` `SRUINS.C:2097-2100`, décalage nul au repos `SRUINS.C:229-233` |
| focale | 160 px | `wallasm_gnu.s:40-45` (`project_point` : dividende 160<<32 / z), `WALLS.H:4` |
| fenêtre de clip | x −160..160, y −110..90 autour de (160, 120) | `WALLS.C:128-131`, `SRUINS.C:2109` |
| demi-champ vertical haut | atan(110/160) = **34,5°**, tan = 0,6875 (bas 29,4°, horizontal 45°) | déduit des deux lignes précédentes |
| plan lointain | aucun (`ENABLEFARCLIP 0` ; `FARCLIP F(1024)` défini mais inutilisé) | `WALLS.C:28`, `WALLS.C:642` |
| saut | vitesse 39<<13, gravité 6<<12, +3<<12 par image tant que le bouton est tenu ; physique à chaque image d'entrée | `SRUINS.C:101, 609-617`, `AICOMMON.H:4`, `SPRITE.C:459-467`, `SRUINS.C:866-1048` |
| apogée du saut | **56,25 u** bouton tenu, 29,25 u sur un appui bref (simulé dans `jump_apex()`) | idem |
| marche franchissable en sautant | 64 u (bas de la boule à l'apogée : 8 + 56) | idem |
| plus haut point atteint | 158 u au-dessus du sol (haut de la boule à l'apogée) | idem |
| vue libre | tangage ±90°, recentrage automatique | `SRUINS.C:450-463, 1028-1032` |
| rejet par cellule | chaque cellule hors écran est rejetée (`clip_visible`) ; les sommets du mur entier sont transformés avant | `WALLS.C:1071-1109`, `WALLS.C:1014-1065` |
| ciel | plan `WALLFLAG_PARALLAX` sans cellules : seule sa boîte à l'écran sert au fond VDP2 | `SLEVEL.H:134`, `WALLS.C:1667-1690`, `UTIL\CONVERT.C:1865-1867, 1894-1896` |
| limites | 600 secteurs, 5 500 murs, 700 sommets par mur, `.LEV` < 900 000 o | `UTIL.H:21-22`, `WALLS.C:975/1020/1207`, `LEVEL.C:41` |

Le jetpack de Duke n'existe pas dans PowerSlave ; les sandales (saut 60<<13, `SRUINS.C:100`) sont un objet
PowerSlave, exclu. Le tangage libre rend visible n'importe quelle hauteur : toutes les règles raisonnent **à tangage
nul**, c'est-à-dire en regardant droit devant.

## 2. Constat Lobotomy (MESURE, détail dans le rapport §2)

- **Les ciels se lisent directement.** Les plans horizontaux Duke Saturn de flag 0x40 sont des plafonds parallax :
  HOLYWOOD en a 39 à Y 960 et 27 à Y 1 760, plus 12 murs parallax entre ciels de hauteurs différentes. Deux ciels
  voisins **n'ont donc pas besoin** d'avoir la même hauteur : la marche entre eux est un mur parallax.
- **Zones non déplacées.** Lobotomy baisse surtout les ciels hauts (≥ 1 024 u au-dessus du sol). Il garde la plupart
  des ciels ≤ ~900 (RAWMEAT, FARNHEIT, HOTELHEL, WRPFACTR, TIBERIUS, DARK1). Il baisse parfois des ciels moyens :
  E3L2 896 → 704, E3L5 640 → 480, E3L4 1 208 → 120. Dans les niveaux où il baisse le ciel principal, il remonte les
  ciels bas à la même valeur : E1L1 448 → 960, E1L6 224 → 896.
- **Plafonds non ciel.** Ils sont gardés à l'identique dans leur grande majorité jusqu'à 1 024 u de hauteur. Seuls
  les très hauts sont baissés : E1L1 154 3 400 → 837, 163 → 960, 169 → 552.
- **Déplacements verticaux.** 831 secteurs rattachés ont bougé de plus de 16 u, dont 166 dans l'eau. Ce sont surtout
  des **zones entières** : l'archipel relié par SE7, recollé en vraie 3D (`docs\DUKE_PC_TO_SATURN.md` §1). Une seule
  pièce perchée isolée est visible : E1L1 306, descendue de 2 563 u avec sa hauteur de 120 u.
- **Aucun plafond global** : le Y max Lobotomy va de 64 (FLODZONE) à 2 048 (SPACPORT) selon le niveau.

## 3. Règles (principe → définition → paramètre)

### H1 — ciel

*Principe* : un plafond de ciel n'est pas de la géométrie. Il fixe seulement la hauteur des façades qui bordent le
ciel. Dans Duke, ces façades montent jusqu'au plafond Build (souvent 3 600 u) : leur haut n'est jamais à l'écran. Une
façade abaissée ne doit pas laisser voir son haut depuis un point où le joueur se tient et la regarde.

*Définition* (`rule_h1_sky`) :
- **Région de ciel** : composante de secteurs ciel reliés par des murs rouges.
- **Façades** : murs pleins de la région, et murs rouges vers un secteur hors région (partie au-dessus de
  l'ouverture).
- **Points de vue** : grille de 64 u sur les sols atteignables (à ≥ 47 u des murs pleins) de la région et de sa
  couronne non ciel, plus les arrivées de téléporteur SE7 en hauteur. Duke fait apparaître le joueur à l'altitude du
  SE partenaire (`refs\build\jfduke3d\src\actors.c:2712`) : c'est la chute du départ d'E1L1.
- **Hauteur vue au-dessus d'un point de façade** à distance horizontale d : œil + d · min(0,6875, pentes des
  ouvertures traversées). Les ouvertures ciel ↔ ciel ne limitent rien ; un mur plein coupe la vue.
- Un spectateur dont l'œil est **déjà au-dessus** du ciel Build du secteur de la façade la voit d'en haut : il ne
  la contraint pas. Sans cette condition, le couvercle de hauteur nulle d'E3L9 (Y −2 480) montait à 2 112.
- **H = max** des hauteurs vues sur toutes les façades de la région, pour d ≤ D (portée de vue), arrondi au multiple de
  128 au-dessus du sol le plus bas de la région, et borné au ciel Build le plus haut de la région.
- Trois façons de répartir H, testées comme familles distinctes :
  - `unify=True` (fov) : toute la région à H. Les ciels bas montent, comme 211 et 212 dans HOLYWOOD.
  - **`unify='lower'` (fov_bas, retenue)** : un secteur dont le ciel Build est ≥ H descend à H ; un ciel plus bas ne
    monte que jusqu'au besoin de ses propres façades (≤ H).
  - `unify=False` (fov_secteur) : chaque secteur prend son besoin propre.
  - Le mode « région » gonfle la géométrie sans rien montrer : SPACPORT passe de 6 010 à 7 453 cellules. La marche
    entre deux ciels est de toute façon un mur parallax (§2).
- Une région sans aucun point de vue garde **le ciel Build de chaque secteur**. Bogue corrigé : le repli donnait à
  tous le maximum de la région.

*Paramètre* : **D = 1 024 u** et arrondi `rel128`, choisis sur E1L1 parmi {768, 1 024, 1 280, 1 536, 2 048, ∞} ×
{rel128, abs128, aucun}. Tableau d'ajustement E1L1 (ciel+haut, part à ±64 u) :

| famille | meilleur paramètre | ciels E1L1 | hauts E1L1 | ciel+haut |
|---|---|---|---|---|
| build | — | 0 % / 2 038 u | 47 % / 284 u | 37 % |
| global | C = 960 | 88 % / 100 u | 47 % / 284 u | 55 % |
| voisins | M = 0 | 0 % / 2 340 u | 47 % / 284 u | 37 % |
| fov | D 1 024, rel128 | 88 % / 108 u | 50 % / 110 u | 58 % |
| **fov_bas** | D 1 024, rel128 | 62 % / 170 u | 50 % / 110 u | 53 % |
| fov_secteur | D 1 280, abs128 | 50 % / 180 u | 50 % / 114 u | 50 % |

Sur E1L1, `fov` bat `fov_bas` à cause de 211 et 212 : Lobotomy les monte à 960, `fov_bas` à 704, leur besoin propre.
La sélection entre familles se fait ensuite hors E1L1 (§4). Sans portée (D = ∞), la plus longue vue d'E1L1 fait
3 417 u et donne H = 2 360, contre 960 pour Lobotomy. Coïncidence notable
(**HYPOTHÈSE**) : 1 024 est exactement le `FARCLIP` de PowerSlave, défini mais désactivé (`WALLS.C:28, 642`). Le moteur
génération 2 de Duke Saturn l'active peut-être, ce qui ferait de D une vraie limite de rendu.

*Variantes testées* : plafond global C (C = 960 sur E1L1, aussi bon que fov sur les ciels d'E1L1 mais pas ailleurs) ;
max(sols et plafonds voisins) + M (mauvais, parce que les plafonds voisins d'E1L1 sont eux-mêmes des halls de
3 400 u) ; Build inchangé.

### H2 — air mort

*Principe* : l'espace vertical que personne ne peut atteindre ni voir est retiré.
- **Atteignable** (`reachable`) : depuis le départ, par marche, par saut (marche ≤ 64 u), par chute, par ouverture
  ≥ 32 u, par SE7/SE17 appariés par hitag, et par les secteurs ascenseurs (lotag 15-19) vers tous leurs voisins.
  Plus haut point atteint : sol + 158 u. Pas de jetpack.
- **Visible** : même calcul que H1 (œil + d · 0,6875, pour d ≤ D), sur les murs du secteur, vus depuis le secteur et
  ses voisins.

*H2 plafonds* (`rule_h2_ceilings`) : un plafond non ciel dont **toute** la surface (pente comprise) est au-dessus de la
hauteur vue descend à cette hauteur, arrondie à 128 au-dessus de son sol. Une pente ainsi coupée est aplatie.
Secteurs exclus : ceux qui bougent (portes, ascenseurs, lotag 9-32 ; SE 0, 1, 6, 11, 13, 14, 15, 19, 20, 21, 25, 26,
29, 30, 31, 32 — SOURCE `jfduke3d actors.c:4985-6881`, `game.c:4891-4903` ; 0, 13 et 29 restent HYPOTHÈSE).

*H2b pièce perchée* : quand un plafond abaissé couperait l'ouverture vers une pièce plus haute, la pièce descend en
bloc (sols, plafonds, sprites), avec l'ouverture, pour que celle-ci garde min(hauteur d'origine, 158 u) sous le
plafond abaissé. Conditions :
- la pièce fait ≤ 4 secteurs, qui ne rejoignent le reste de la carte que par ce puits ;
- le gain est d'au moins 128 u ;
- la descente s'arrête avant tout secteur superposé en XZ.

Ces deux seuils ont été posés **après** avoir vu des faux positifs sur SECRET1 (5 secteurs, −102), RAWMEAT (28
secteurs, −16) et HOTELHEL (−392 et −30) : ils ne sont **pas** calibrés sur E1L1 seul. Le rapport signale quand la
pièce reste inaccessible sans jetpack.

### H3 — ne jamais couper

Point fixe, qui ne fait que remonter : pour tout plafond abaissé ou tout ciel, les trois contraintes ci-dessous.
Violée → on remonte le secteur (la région entière pour un ciel unifié) et la raison est notée `recul H3 …`.
- **Sol** : plafond ≥ sol haut + min(hauteur d'origine, 158).
- **Ouverture** encore ouverte dans Build : min(plafonds des deux côtés) ≥ bas de l'ouverture + min(hauteur
  d'origine de l'ouverture, 158).
- **Sprite** visible (hors marqueurs picnum 1-10 et cstat & 32768) : plafond ≥ haut du sprite. Hauteur calculée
  depuis les tailles ART lues dans `DUKE3D.GRP` (sizy · yrepeat · 4 / 128). Un sprite dont le haut touche le plafond
  Build à 16 u près est **accroché** et suit le plafond (ex. E1L1 154, pic 2360 à Y 3 400).

### Résultats de la règle retenue (fov_bas) sur E1L1 (carte PC complète)

- 154 : 3 400 → 832 (Lobotomy 837) ; 163 : 3 384 → 720 (Lobotomy 960) ; 169 : 3 384 → 720 (Lobotomy 552).
- 203 : 3 400 → 624, et 306 descend de 2 712 u (Lobotomy : 203 à 504, 306 descendue de 2 563 u). La pièce reste
  inaccessible sans jetpack. C'est **la seule** pièce déplacée par H2b sur les 23 cartes appariées.
- Ciels de la ville : 3 640 → 952 (Lobotomy 960) ; 211 et 212 : 448 → 704 (Lobotomy 960).
- Puits de départ 258/308 : 952 (Lobotomy 1 760 ; écart : voir §4).
- Sur `e1l1_import.json` (toit retiré) : mêmes décisions, en 1,2 s.

## 4. Calibration et évaluation

Le protocole, les familles, les tableaux par niveau et les écarts secteur par secteur sont dans le rapport, §4.
Lecture :
- **Ciels.** Aucune famille à paramètre unique (réglé sur E1L1) ne reproduit Lobotomy niveau par niveau.
  - Hors E1L1 (228 ciels de zones non déplacées) : fov_bas 56,1 %/247 u, fov_secteur 54,8 %/246, fov 54,4 %/254,
    global 51,8 %/258, voisins 42,1 %/253, « garder Build » 62,3 %/277.
  - La règle égale Build là où Lobotomy garde les ciels modestes : E2L3 77 %, E2L6, E3L1, E3L7, E3L8 à 100 %.
  - Elle gagne là où il baisse un ciel géant (E1L2 : 171 u contre 1 824).
  - Elle perd là où il baisse sans qu'aucune visibilité ne l'exige : E3L4 1 208 → 120, E3L2 896 → 704, E3L5 640 → 480,
    E1L6 1 952 → 896 depuis un canyon à −1 400.
  - Explication plausible (HYPOTHÈSE) : un réglage à la main, par niveau, piloté par le budget de polygones du moteur
    gen2 plutôt que par une loi visuelle.
- **Écarts typiques à la règle** :
  1. Puits de départ d'E1L1 (1 760) : la chute depuis le toit (arrivée SE7 à ~1 700). Ses façades sont vues d'en haut
     et à moins de 1 024 u ; la règle le garde au niveau de la ville (952).
  2. Ciels bas que Lobotomy remonte au ciel commun (E1L1 211/212 à 960, E1L6 224 → 896) : `fov_bas` ne les monte
     qu'à leur besoin (704). En revanche, il ne gonfle plus SPACPORT −288, WRPFACTR 888 ou DARK1 384, que le mode
     « région » montait sans raison.
  3. Ciels baissés par Lobotomy bien sous la hauteur vue (LARUMBLE 1 208 → 120, BANKROLL 896 → 704).
  4. Zones recollées en Y par Lobotomy : ni leur altitude ni leur hauteur ne se comparent. Elles sont exclues de la
     note des ciels et comptées à part.
- **Plafonds non ciel.** H2 à D = 1 024 est prudente.
  - Hors E1L1, elle ne change presque rien, comme Lobotomy : non ciel 49,4 u / 87,5 %, contre 50,9 / 87,6 pour Build.
  - Sur les 844 non ciel que Lobotomy a changés ou déplacés : 163 u contre 170.
  - Sur E1L1, elle ramène l'écart moyen des plafonds hauts de 284 à 110 u.
  - Gains nets ailleurs : FUSION1 688 → 594, FUSION2 419 → 356, ABYSS1 313 → 289.
  - Pertes : HOTELHEL 117 → 131 u, LARUMBLE 335 → 370 u.
  - Mitigé : E3L6, où l'écart moyen baisse (165 → 158 u) mais la part à ±64 u aussi (46 → 40 %).

## 5. Défauts déduits du budget

- **Ce qui dépend de la hauteur du ciel.** Les coûts statiques : cellules et sommets des façades, MAXVPERWALL par face
  ((colonnes+1)·(rangées+1) ≤ 700), octets de géométrie.
- **Ce qui n'en dépend pas.** Le nombre de secteurs et de murs.
- **Le coût par image n'en dépend presque pas.** Le moteur rejette chaque cellule hors écran (`WALLS.C:1071-1109`),
  donc un ciel plus haut que le champ de vue n'ajoute rien au dessin. Il ajoute seulement des transformations de
  sommets (`WALLS.C:1014-1065`).
- **Estimation par image** (`frame_cost`) : 150 points de vue atteignables tirés au sort (graine fixe) × 8 caps.
  Parcours 2D des portails en fenêtre écran, comme `sectorDraw` xmin/xmax. Compte les cellules de murs dans le tronc
  vertical, plus les sols et plafonds non ciel. C'est une borne haute :
  - pas d'occultation à l'intérieur d'un secteur non convexe ;
  - surtout, le **sol entier** de chaque secteur vu est compté, sans découpe à la fenêtre (MESURE, E2L10 : un secteur
    ciel de 28 millions u² et des murs de 4 288 u donnent 4 626 cellules de sol et 4 428 de murs dans la pire vue).

  Les p95 très élevés d'E2L10 et de SPACPORT viennent de là. Ils ne dépendent pas de la hauteur du ciel.
- **Règle proposée** (`budget_levels.py`) : ciel = valeur visuelle H1, descendue par paliers de 128 tant qu'une borne
  statique est dépassée (une face > 700 sommets, ou géométrie > 450 000 o). Ce plafond de 450 000 o, la moitié du
  `.LEV`, est une HYPOTHÈSE à remplacer par la part réelle mesurée sur un `.LEV` écrit par E3/E5. H3 s'applique après
  le plafond de budget.
- Les niveaux qui déclenchent cette règle, la sensibilité par image (ciel H1 contre plafonné à 768) et le
  MAXNMSECTORS avant découpe sont dans le rapport, §5.
- **MAXVPERWALL n'est pas un problème de ciel** (MESURE, `build_import`-indépendant). Sur E1L5, les deux faces
  au-delà de 700 sommets sont les murs pleins du secteur ciel 372, du fond de l'abîme (Y −6 488) jusqu'au ciel
  (4 008) : 2 128 et 2 490 u de long, soit 1 494 et 1 743 sommets.
  - Même avec un ciel plafonné à 512, il reste 1 008 et 1 176 sommets. Aucun plafond de ciel ne corrige ce cas.
  - Le remède est de **découper la face** en plusieurs murs (convertisseur) ; H2 pourrait aussi retirer le fond si
    l'abîme n'est ni atteignable ni visible.
  - La règle budget rend « aucun » pour ces niveaux, et c'est le bon diagnostic.

## 6. API (branchement sur build_import plus tard)

```python
import height_rules as HR
M = HR.model_from_import(json.load(open('build/duke2ps/e1l1_import.json')), tiles)   # ou model_from_build(buildmap.load(...))
dec, rapport = HR.apply_rules(M, dict(view_cap=1024.0))                               # dict {secteur: Decision}
d = dec[sid]; d.ceil_old, d.ceil_new, d.floor_new, d.dy, d.reasons                    # Y Saturn ; raison de chaque changement
HR.static_cost(M, dec); HR.frame_cost(M, dec, HR.frame_views(M))                      # budgets
```

- Paramètres par défaut : `DEFAULTS`. Le dict `memo` évite de refaire la visibilité lors d'un balayage de `sky_cap`.
- Sur `e1l1_import.json` : 276 secteurs, départ trouvé (secteur 230 = Build 259), 1,2 s. Les décisions sont les mêmes
  que sur la carte complète, sauf ce qui dépend du toit retiré.
- `build_import.py` n'est pas modifié. Pour brancher : remplacer son plafonnement fixe à `SKY_CAP_Y` (1 760) par
  `ceilingz = -Decision.ceil_new * 128`, et `floorz`, `ceilingz` et les sprites décalés de `dy * 128` pour les pièces
  déplacées.

## 7. Ce qui reste HYPOTHÈSE

- Le rattachement conteneur → secteur (centroïde, puis ≥ 50 % des sommets). Le bruit est de l'ordre de 15 %
  d'écarts > 8 u sur des pièces basses que Lobotomy n'a sans doute pas touchées.
- Le sens de la portée D = 1 024 (FARCLIP gen2 ?). Le champ de vue de Duke Saturn n'a pas été lu, faute de source ;
  la règle utilise celui de PowerSlave.
- Les points de vue : sols atteignables, sans jetpack ni accroupissement, ouverture ≥ 32 u, plus les arrivées SE7.
  Pas de vue depuis l'intérieur à travers une fenêtre au-delà de la première couronne de secteurs.
- La liste des SE qui déforment un secteur (0, 13, 29).
- Le poids en octets d'un sommet et d'une cellule, et la part de géométrie du `.LEV` (450 000 o).
- La correspondance LORD2 ↔ E2L9 (partielle) ; STADIUM et UREA51 sans carte PC.
- Les seuils de H2b (≤ 4 secteurs, ≥ 128 u), posés après avoir vu d'autres niveaux que E1L1.
