# E2.5 — quantification sur la grille du `.LEV` + garde-fou d'arrondi (2026-09-11)

Module `tools\duke2ps\quantize.py` (non suivi, jamais commité). Sorties `build\duke2ps\e1l1_quant.json`.
Demande du propriétaire : « la conversion doit être la même des deux côtés d'une jonction ; s'assurer que
l'arrondi est le même si deux sections avaient un décalage de 2 u ou moins ».

## Ce que fait le module

- **Grille** : x,y Build → multiple de 8 (1 u) ; z Build → multiple de 128 (1 u). Après quantification,
  la conversion aval X = x/8, Z = −y/8, Y = −z/128 est **exacte** (plus aucun arrondi tardif).
- **Sommets partagés** : le (x,y) propre de chaque mur est arrondi une seule fois ; les murs rouges déjà
  symétriques le restent (même coordonnée brute → même coordonnée arrondie). Soudure des sommets distants
  de ≤ T (2 u) par union-find, avec coupure des chaînes qui s'étalent sur plus de 2·T.
- **Hauteurs** : arrondies, puis soudure des portails **plats** distants de ≤ 2 u.
- **Pentes** : `slope_ref` re-ancré sur la grille (le plan reste exact).
- **Aires** recalculées après quantification.

## Résultat sur E1L1 (MESURE, `python tools\duke2ps\quantize.py --no-convex`)

| critère (garde-fou) | résultat |
|---|---|
| coordonnées sur la grille | 0 hors grille (66 sommets hors grille arrondis, déplacement max 0,71 u) |
| hauteurs sur la grille | 0 hors grille (les 552 hauteurs de sol/plafond étaient déjà exactes) |
| tient dans un short | OK (|v| ≤ 16 383 u) |
| symétrie des murs rouges | 0 asymétrique (1 036 paires déjà exactes avant) |
| une composante connexe | 1 |
| aucun secteur de hauteur ≤ 0 | 0 |
| égalité conservée aux jonctions plates | 0 scindée par l'arrondi |
| départ dans son secteur | OK |

**Conclusion du garde-fou** : l'arrondi est cohérent des deux côtés de chaque jonction, et **aucune**
jonction plate n'a été scindée. Les soudures ≤ 2 u ne déclenchent pas sur E1L1 (les paires de murs rouges
étaient déjà exactes, et les hauteurs plates déjà entières). Le garde-fou est donc un filet de sécurité
qui ne change rien sur cette carte — c'est le résultat attendu.

## À déférer (hors garde-fou, relevés mais non corrigés ici)

1. **8 petits écarts aux jonctions de pente** (≤ 2 u, dont 6 ≤ 0,06 u et 2 exactement à 2 u). Ce n'est pas
   une fissure d'arrondi : un **plan incliné** a une hauteur *continue* face à la hauteur *entière* d'un
   secteur plat. La soudure demanderait de modifier le plan (floorz/heinum/slope_ref), donc relève d'**E3**
   (représentation des pentes en cut-planes), pas de la quantification.
2. **1 boucle auto-tangente** : le secteur 23 (build_id 23). Il porte une **encoche de 1 unité Build
   (0,125 u)** — une bouche de portail sub-résolution. L'arrondi de x=21951 → 21952 referme la bouche et
   rend la boucle auto-tangente (non triangulable). C'est une *feature sub-résolution* (la même famille
   que les « 8 éclats < 1 u » signalés par E2), qui demande une **fusion de mur mince** : E2.5b.
3. **8 murs de longueur nulle** (534, 536, 540, 542, 565, 573, 578, 586) : extrémités des éclats. `convex.py`
   les ignore déjà (`murs_longueur_nulle`), donc non bloquant, mais à nettoyer proprement en E2.5b.

## Blocage pour la suite — RÉSOLU par E2.5b (voir plus bas)

`convex.py` relancé sur `e1l1_quant.json` échouait sur le secteur 23 (« plus d'oreille »), à cause du
point 2. **E2.5b (`snapmap.py`) l'a résolu** : les features sub-résolution sont préservées au lieu d'être
écrasées, et la chaîne complète passe (440 morceaux, tous les critères OK). Les points 2 et 3 ci-dessus
sont donc caducs : 0 boucle dégénérée, 0 mur de longueur nulle. Seul le point 1 (écarts de pente) reste
déféré à E3.

## Reproduire

```
python tools\duke2ps\quantize.py            # E1L1 -> e1l1_quant.json + criteres
python tools\duke2ps\quantize.py --tol-u 1.0
python tools\duke2ps\convex.py --in build\duke2ps\e1l1_quant.json --out build\duke2ps\e1l1_quant_convex.json
```

## Reste HYPOTHÈSE

- La soudure des sommets XZ à 2 u (union-find) n'a déclenché **0 fois** sur E1L1 : son comportement sur
  des cartes aux jonctions réellement mal alignées (autres corpus, extensions) n'est pas validé.
- Le re-ancrage des pentes (2 secteurs re-ancrés) n'a pas été vérifié visuellement.
- Le seuil de 2 u est le choix du propriétaire ; les 2 écarts exactement à 2 u (81↔194, 185↔234, plafond)
  sont ambigus (marche volontaire ou pente à 2 u ?).

---

# E2.5b — préservation des features sub-résolution (2026-09-11)

Module `tools\duke2ps\snapmap.py`, branché dans `quantize.py` (`quantize_xz(W, T, S)`).

## Le problème

Sur E1L1, **27 groupes de coordonnées distinctes** tombent sur le même point de grille (étendue 1 à 5
unités Build, soit 0,125 à 0,625 u). La plupart sont des doublons inoffensifs, mais trois bornaient de
vraies features, que l'arrondi détruisait :

- **Secteur 23** : ce n'est pas une encoche, c'est un **mur-secteur d'UNE unité Build d'épaisseur**
  (x = 21951 contre 21952) qui sépare les secteurs 1/19/21 des secteurs 2/20/22. L'arrondi le faisait
  disparaître et collait les deux moitiés de la carte.
- **Secteur 122** : un **cadre filiforme** (1 à 4 unités Build) qui entoure les secteurs 120/121.
- 8 murs des secteurs 118/119/122 devenaient de longueur nulle.

## La règle

On arrondit normalement, **sauf** quand la collision crée une dégénérescence. Trois dégénérescences sont
détectées sur la carte snappée : arête de longueur nulle, sommet répété dans une boucle, et **sommet
tombant à l'intérieur d'une arête non adjacente** (jonction en T — c'est celle-ci qui faisait échouer la
triangulation du secteur 122). Les valeurs en cause sont alors écartées d'un cran de grille.

Trois propriétés que l'implémentation doit garantir, et qui ont chacune coûté un bug :
1. **La valeur déjà exactement sur la grille reste fixe** ; ce sont les intruses qui bougent.
2. **L'ordre est préservé.** Première version : 27389 et 27390 se retrouvaient à 27384 et 27376, donc
   *inversés* — ce qui retourne un polygone mince. Corrigé : les créneaux sont attribués du plus proche
   au plus loin de la valeur gardée.
3. **On n'écarte que les paires réellement contraintes.** Première version : un créneau par valeur, d'où
   une dérive de 2,5 u. Or 26370, 26371 et 26372 ne sont contraints que contre 26368, pas entre eux :
   ils partagent donc un créneau. Dérive ramenée à **0,875 u**.

## Résultat (MESURE)

| | E2 (non quantifié) | E2.5b (quantifié) |
|---|---|---|
| Boucles dégénérées | 1 (secteur 23) | **0** |
| Murs de longueur nulle | 8 | **0** |
| Morceaux convexes | 446 | **440** |
| Morceaux < 1 u de large | 7 | **0** |
| Morceau le plus mince | 0,250 u | **1,315 u** |
| Aire minimale | 7,50 u² | **32,00 u²** |
| Dérive max d'un sommet | — | 0,875 u |
| Paires de features préservées | — | 6 (1 itération) |

`convex.py` sur le modèle quantifié : **tous les critères OK** — 440/440 strictement convexes, aires
exactes (écart 0), 1 420 arêtes internes toutes jumelées, 0 jonction en T, départ dans le morceau 319.
Comparaison HOLYWOOD : 295 de nos morceaux contre 292 conteneurs sur les 170 secteurs appariés.

**La réserve d'E2 sur la virgule fixe est levée** : il n'y a plus un seul morceau sub-unitaire.

## Reproduire

```
python tools\duke2ps\quantize.py     # E1L1 -> e1l1_quant.json, puis convex.py -> e1l1_quant_convex.json
```

## Reste HYPOTHÈSE

- Testé sur E1L1 seulement. Les autres corpus (E2-E4, extensions) ont sans doute des features
  sub-résolution plus nombreuses ; la dérive et le nombre d'itérations y sont inconnus.
- L'élargissement du mur-secteur 23 (0,125 u → 1 u) et du cadre 122 mange dans les secteurs voisins.
  C'est géométriquement cohérent (les sommets partagés bougent ensemble) mais non validé visuellement.
- Les 8 petits écarts aux jonctions de pente restent déférés à E3.

---

# E2.5b — passage aux 194 cartes (2026-09-12)

Le propriétaire a demandé de valider la règle ailleurs que sur E1L1. Chaîne complète E1 → E2.5b → E2
lancée sur les 194 cartes des corpus `d13`, `atomic`, `dukedc`, `dz2`, `dz2pp`, `xtreme_sp`,
`xtreme_dm`, `build_goodies`, **en A/B** : découpe convexe sur le modèle brut *contre* le modèle
quantifié. C'est le seul test qui distingue « la carte est difficile » de « la quantification l'a
cassée ».

## Premier passage : la règle ne généralisait pas

| | résultat |
|---|---|
| convexe OK sur brut | 33 |
| convexe OK sur quantifié | 26 |
| **régressions** (brut OK → quantifié KO) | **7** |
| gains | 0 |
| plantages *avant* la quantification | 137 |

Les 7 régressions étaient de vrais défauts de la règle, pas des cartes difficiles. Trois causes,
toutes trouvées en regardant les secteurs incriminés :

1. **`spread` oscillait.** Une paire séparée au tour 1 se retrouvait réunie au tour 3, parce qu'un
   autre groupe avait choisi un autre pivot. `d13/E2L5` secteur 55 : 8 itérations sans converger,
   boucle de 0,5 u de large finalement écrasée à **0 u²** (15,97 avant). Corrigé : les contraintes
   sont désormais **accumulées** d'une itération à l'autre, donc une paire séparée ne peut plus être
   réunie.
2. **Aucune contrainte entre boucles d'un même secteur.** `find_constraints` ne regardait qu'une
   boucle à la fois : un trou venant toucher la boucle extérieure ne produisait aucune dégénérescence
   *interne* et passait (`d13/E2L2` secteur 80 : 19/19 sommets distincts, et pourtant « plus
   d'oreille »). Ajouté.
3. **Aucune détection d'effondrement d'aire.** Une boucle peut tomber à zéro ou se retourner sans
   sommet répété ni arête nulle. Ajouté : si l'aire signée s'annule ou change de signe, on contraint
   les extrêmes de l'axe qui s'est écrasé.

## Le piège de la correction : l'ordre, encore

Le premier correctif contre l'oscillation était un garde-fou « ne pas prendre un créneau déjà occupé
par un partenaire contraint ». Il **enjambait des valeurs** et inversait l'ordre : le secteur 55
passait de +15,97 à **−31,50 u²**, donc boucle retournée. C'est la troisième fois que la propriété
d'ordre coûte un bug sur ce module.

`spread` a donc été reformulé pour que la monotonie soit garantie **par construction** et non réparée
après coup : deux passes monotones (une vers le haut, une vers le bas), et sur chaque plage où elles
diffèrent on garde celle qui déplace le moins. Les deux passes coïncident aux bornes de ces plages,
donc le mélange reste monotone. Le secteur 55 donne maintenant +31,50 u² : l'éclat de 0,5 u est
élargi à 1 u, exactement comme le mur-secteur 23 d'E1L1.

Un quatrième défaut, sans rapport avec la règle : `quantize.py` plantait sur **111 cartes** parce que
`depart_m1.build_xy` est `None` dès qu'une carte n'a pas d'appariement SE7 (la logique de départ d'E1
est taillée pour E1L1). Ces 111 cartes n'avaient donc jamais été testées.

**Non-régression** : E1L1 ressort identique au sha1 près (`e1l1_quant_convex.json`, 299 995 o,
sha1 439cf56fe0ac), 440 morceaux, dérive 0,875 u, tous critères OK.
