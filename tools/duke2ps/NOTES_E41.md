# E4.1 — textures plaquées, pas seulement présentes (2026-09-12)

> **E4.1 (sous-tuiles) a été RETIRÉ le même jour : il effondrait le framerate ET
> affichait de fausses textures.** La cause et le remède sont en tête de ce fichier ;
> ce qui suit le bandeau « E4.1b » reste valide (formules, mesures, miroirs).

E4 mettait la bonne texture Duke sur chaque surface, mais **écrasée dans chaque cellule de 64 u**.
E4.1 la découpe et la place. Rien n'est commité ; les textures restent de la recherche (LICENSE.TXT [4][A]).

## Le défaut, en une phrase

Dans Build, une répétition de texture ne couvre pas une cellule PowerSlave. **MESURE E1L1** : avec
`yrepeat = 8` — la moitié des murs — un texel Duke fait 2 u, donc une texture 64×64 « neutre »
couvre **128×128 u, soit 2×2 cellules**. On la répétait 4 fois au lieu de la découper en 4.
Verticalement c'est pire : **45,5 % des murs** ont une répétition de plus de 128 u.

## Les formules (SOURCE `refs/build/jfbuild`, aucune ligne recopiée)

```
MUR      u(d) = (d/L)·8·xrepeat + xpanning                    engine.c:1027, 1044-1045
         v(Y) = (Y_ref − Y)·yrepeat/16 + ypanning·2^ylog/256   engine.c:2512-2524
         répétition : tsx·L/(8·xrepeat) u  ×  16·tsy/yrepeat u
SOL      u = k·X + P_x·tsx/256,  v = k·Z + P_y·tsy/256, k = 1 si stat&8 sinon 1/2
         répétition : tsx/k u × tsy/k u                        engine.c:1412-1451
```

- `xpanning` est en **texels** (0..255, modulo `tilesizx`) ; `ypanning` en **1/256 de tuile**.
  Vérification indépendante : l'éditeur propage l'alignement avec `(xpanning + (xrepeat<<3)) % tilesizx`
  (`build.c:1726`), ce qui confirme les deux à la fois.
- **Ancre verticale** = bit 2 de `cstat` (valeur 4, 41 % des murs d'E1L1) : sans le bit on s'accroche
  au bord **mobile** (le secteur voisin), avec le bit au secteur courant (`engine.c:2521-2523,
  2627-2629, 2723-2724`).
- Miroirs : **bit 3 = horizontal**, **bit 8 = vertical** (`engine.c:1093`, `2525`).
- Validation croisée : `fixrepeats` (`build.c:5994-6003`) pose `xrepeat = dist·yrepeat >> 10`, ce qui
  rend les texels carrés exactement quand mes deux formules coïncident. **MESURE : 89,2 % des murs
  d'E1L1 vérifient l'égalité au bit près**, rapport largeur/hauteur d'un texel médian = 1,000.

## Pourquoi les sous-tuiles étaient un contresens (RETRAIT, testé console)

Le moteur ne garde en VRAM VDP1 qu'un **cache de 28 tuiles** 64×64 16 bpp :
`initPicSystem(i,{28,31,1,10,12,-1})` (`SRUINS.C:1903`) et nos tuiles, flags `0x32`
= `64x64|16BPP|PALLETE`, tombent en classe `TILE16BPP` (`PIC.C:678-680`, `PIC.C:84`).

Deux conséquences, qui expliquent **les deux symptômes à la fois** :

1. **Coût d'un miss.** `mapPic()` est appelé **par cellule** (`WALLS.C:1136`), et un miss exécute
   une expansion palette de 4 096 itérations (`PIC.C:352`) puis un DMA de 8 192 octets
   (`PIC.C:397`).
2. **Éviction dans la frame courante.** `map()` part de `oldTime = frameCount+1` et retient tout
   slot dont `lastUse < oldTime` (`PIC.C:271-275`) : une tuile **déjà utilisée dans cette frame**
   est éligible. Au-delà de 28 tuiles distinctes visibles, on évince une tuile dont la commande
   VDP1 est déjà en file — elle affiche alors les pixels d'une autre. La texture est donc fausse
   *en plus* d'être lente.

**MESURE retail (24 niveaux, 68 890 murs)** : 4 tuiles distinctes par secteur en médiane, 12 au
p99, **25 au maximum** — le cache de 28 est dimensionné exactement pour ça. Découper une texture
en 2×2 ou 4×4 multiplie par autant le nombre de tuiles distinctes **par vue**. Preuve causale :
E4 et E4.1 ont la **même géométrie** (mêmes cellules, mêmes sommets) et ne diffèrent que par
l'index de tuile de chaque cellule — donc la perte vient entièrement de ce chemin.

## E4.1b — l'échelle passe par la TAILLE DE LA CELLULE

`sWallType` porte `tileLength` / `tileHeight` (`SLEVEL.H:170-172`, « number of tiles along
v0→v1 ») : la taille d'une cellule en unités monde est **notre choix**, pas une constante. On la
prend égale à **une répétition de la texture** — l'échelle est juste avec **une seule tuile par
picnum**, donc exactement la pression de cache d'E4, celle qui tournait.

Les deux axes n'ont pas la même contrainte : un quad VDP1 est **affine**, sa déformation croît
avec l'écart de **profondeur** entre ses coins. Une cellule *longue* se déforme ; une cellule
*haute* non (ses deux bords horizontaux sont à la même distance). D'où des bornes dissymétriques,
calées sur ce que le retail exerce (longueur médiane 64 u, p99 90,5 ; hauteur médiane 62,9 mais
jusqu'à 544) :

```
CELL_MIN_U, CELL_MAX_U = 64, 256     # le long du mur
CELL_MIN_V, CELL_MAX_V = 64, 256     # en hauteur
```

256 u en hauteur n'est pas un réglage arbitraire : c'est **le mode dominant** d'E1L1
(`yrepeat = 8` + texture de 128 de haut → 742 murs).

| | E4 | E4.1 (retiré) | **E4.1b** |
|---|---|---|---|
| tuiles | 64 | 100 | **64** |
| cellules de mur (octets `texture`) | 25 772 | 25 772 | **8 342** |
| faces | 10 930 | 10 930 | **9 779** |
| aire à la bonne échelle (≤ 30 % d'erreur) | 6,4 % | — | **50,5 %** |
| demande mémoire | 1 178 658 | 1 236 058 | **1 033 770** |

Le gain de perf ne vient donc pas seulement du cache : il y a **3× moins de cellules de mur**,
donc 3× moins de sprites distordus VDP1 à émettre. E4.1b est plus rapide qu'E4.

Plafond mesuré à ~66 % d'aire correcte, quelles que soient les bornes : Build **rogne** la texture
au bout du mur, alors qu'une cellule affiche toujours la texture **entière**. Un mur dont la
longueur n'est pas un multiple de la répétition restera faux sans sous-tuile — et la sous-tuile
est justement ce que le cache interdit.

## L'axe horizontal est le vrai goulot (CELL_MAX_U : 128 -> 256)

Le vertical a « juste marche » avec une borne a 256 parce qu'un quad VDP1 est AFFINE et que sa
deformation croit avec l'ecart de PROFONDEUR entre ses coins : sur l'axe vertical les deux bords
sont a la meme profondeur (zero deformation), sur l'horizontal ils divergent sur un mur en biais.
C'est la SEULE raison d'un plafond horizontal plus bas, et c'est un vrai arbitrage, pas un reglage.

MESURE (aire ponderee) : repetition horizontale 64-128 u = 60,3 % de l'aire ; 256 u = 19,2 % ;
288-1664 u (longs murs) ~ 11 % ; < 64 u = 17,2 % (ETIRE, pas repete -- autre levier, CELL_MIN_U).

| CELL_MAX_U | aire repetee |
|---|---|
| 128 (E4.1b initial) | 31,6 % |
| **256 (livre)** | **11,2 %** |
| 384 | 2,5 % |
| 512 | 2,4 % |

256 colle la cellule a la repetition dominante (19 % de l'aire). Le 11 % restant, ce sont des
murs de 300-1664 u qu'aucune borne raisonnable ne corrige sans sous-tuiles (et la sous-tuile fait
exploser le cache de 28 tuiles). 384 = option agressive si le nage affine reste invisible.

## Ce que fait le convertisseur

1. **La cellule prend la taille d'une répétition** (`tile_counts(quad, cu, cv)`), bornée par
   `CELL_*`. `Emitter.cell_tile` retombe sur la clé `(pic, 0, 0, 1, 1)` — la texture entière.
2. **Les miroirs sont gratuits.** Le premier octet de chaque cellule indexe `pattern[8][4]`
   (`WALLS.C:982-992`), une permutation des 4 sommets : 5 = miroir H, 7 = miroir V, 2 = les deux.
   Une **face** n'a pas cet octet (`SLEVEL.H:120-124`) : on permute `v[0..3]` à la place.
3. **Plus aucun budget de tuiles à arbitrer** : une texture ne coûte qu'une tuile. `TexPlanner`
   ne fait plus que mesurer la répétition et rapporter l'échelle réellement obtenue.

## Ce qui reste vrai du budget

22 tuiles du donneur n'étaient référencées ni par notre géométrie ni par ses séquences :
**90 156 o récupérés** par élagage dans `assemble.py`. L'élagage reste utile, mais il ne finance
plus de sous-tuiles — il allège simplement le fichier.

Le plafond d'index n'est pas limitant : `tileBase = nmWeaponTiles = 40` (tileset trouvé à l'offset
656 336 de `cd/STATIC.DAT`) et `level_texture` étant un `unsigned char`, la géométrie peut aller
jusqu'à l'index 215. On est à 63. **La ressource rare n'est pas l'index ni les octets : c'est le
cache de 28 tuiles.**

```
python tools\duke2ps\geom3d.py
python tools\duke2ps\duketiles.py
python tools\duke2ps\assemble.py
```

## Reste HYPOTHÈSE / non fait

- **Alignement relatif des sols** (`floorstat` bit 6, 75 sols + 44 plafonds sur 317) : ces secteurs
  retombent sur la texture entière en une tuile. Un découpage mal orienté serait pire que pas de
  découpage — mais c'est 100 % de leur surface qui reste à l'ancienne échelle.
- **L'autre moitié de la surface.** 50,5 % de l'aire est à la bonne échelle ; le reste bute sur le
  fait qu'une cellule affiche la texture ENTIÈRE là où Build la rogne. Plafond théorique ~66 %.
- **Les sols ne sont pas traités** : ils gardent la grille fixe de 64 u (`_grid_cells`), dont la
  règle de préservation d'aire est délicate. 58 % d'entre eux portent `stat & 8` (double
  smooshiness) et sont **déjà** à la bonne échelle ; les 42 % restants voudraient 128 u.
- Le rééchantillonnage reste au plus proche voisin, et l'ombrage vient toujours du gouraud par sommet.
- **Levier non exploité, et il a changé de nature** : une tuile **32×32** (flags 0x34) occupe une
  classe de cache SÉPARÉE (`TILESMALL16BPP`, 10 slots, `SRUINS.C:1903`). Ce n'est donc pas
  seulement 4× moins d'octets, c'est **10 slots de plus**, sur un budget total de 28 — le seul
  moyen connu d'augmenter le nombre de textures distinctes visibles sans toucher au moteur. Rien dans `drawRectWall`/`EZ_specialDistSpr2`/`mapPic` ne suppose 64×64, la
  taille vient de `classType[]` (`PIC.C:112, 199, 217-220`). Il faut élargir 4 asserts
  (`WALLS.C:1171, 1235, 1322, 2083`) et agrandir le pool de slots small (10 en jeu, `SRUINS.C:1903` ;
  40 max structurel `PIC.C:77`). Le retail n'exerce jamais ce chemin : 0 cellule non-0x32 sur 23 niveaux.
