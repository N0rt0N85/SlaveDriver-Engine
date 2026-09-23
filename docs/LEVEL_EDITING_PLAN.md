# Rendre les niveaux éditables — plan (2026-09-23)

Question posée : *le convertisseur WAD / Build produit des `.LEV` ; comment un artiste
manipule-t-il ce résultat ? Un éditeur de `.LEV` ? En s'appuyant sur un logiciel existant, ou
directement sur le code du moteur ?*

Toute affirmation de cette note cite `FICHIER.C:ligne` de ce dépôt, ou porte la mesure qui la
fonde. Les chiffres viennent de trois campagnes du 2026-09-23 sur les 24 niveaux retail, les neuf
niveaux Doom du fork et l'E1M1 converti.

---

## 1. Le fait décisif : un `.LEV` est **compilé**, pas édité

Ce n'est pas un format d'auteur. Chaque mur y est **déjà tessellé** :

* un mur en `WALLFLAG_PARALLELOGRAM` porte une grille `tileLength × tileHeight` dont les sommets
  **n'existent pas dans le fichier** — ils se recalculent (WALLS.C:1832-1837) — et chaque cellule
  lit sa paire `[motif, tuile]` dans `level_texture` à `wall.textures + 2*(h*tileLength + w)`
  (WALLS.C:1893) ;
* un mur en maillage porte une liste de quads dont les sommets sont **relatifs** à
  `wall.firstVertex` (WALLS.C:2021-2040, :922) ;
* la lumière est **cuite** par sommet de grille, sur un pas différent de celui des cellules
  (`(tileHeight+1) × (tileLength+1)` à partir de `firstLight`) ;
* le plan de chaque mur (`normal`, `d`), `pixelLength`, `tileLength`, `tileHeight`, le
  centre des secteurs, `cutPlane`, les paires d'ordre et la table de rejet sont tous **dérivés**.

Déplacer un seul sommet invalide tout cela. Et l'ordre des murs d'un secteur est lui-même porteur
de sens : `findDoorways` (WALLS.C:2833-2835) sort de la boucle **au premier mur sans
`nextSector`**, donc un portail rangé derrière un mur plein n'est jamais franchi.

> **Un « éditeur de `.LEV` » au sens naïf reviendrait à éditer un binaire compilé.**

D'où le découpage qui commande tout le reste :

| couche | ce qu'on touche | rien de dérivé ne bouge ? |
|---|---|---|
| **A — habillage** | tuile par cellule, motif (miroirs), lumière par sommet, objets, drapeaux de mur et de secteur | **oui** → éditable directement dans le `.LEV` |
| **B — géométrie** | positions, découpage, topologie | non → passe obligatoirement par un compilateur |

Vérifié, et c'est ce qui rend la couche A exploitable : **tout ce qu'elle touche est relu à chaque
image et n'a besoin d'aucune invalidation.** `level_texture[]` et `level_face[].tile` sont lus par
cellule et par image ; le cache de tuiles du VDP1 est trié par index avec un LRU (PIC.C:442-487,
658-690), donc un index changé demande simplement une autre tuile à l'image suivante ;
`doorwayCache` et `sectorDraw0` sont des brouillons de parcours refaits chaque image
(WALLS.C:2166-2170, 2565-2567). Seule une modification de **géométrie** demande une barrière, et
elle existe déjà : `wallsPipeDiscard()` (WALLS.C:3787-3795).

---

## 2. Ce qui est construit

### 2.1 `tools/lev_report.py` — ce qu'un niveau coûte et ce qui le tuerait

Ne lit que le `.LEV`, donc juge aussi les 24 niveaux retail — et c'est tout l'intérêt, parce que le
retail est le seul étalon de ce qui tourne vraiment.

Trois familles :

1. **les `assert` du chargeur**, délégués à `duke2ps/lev_write.engine_problems` ;
2. **les défauts silencieux**, qu'aucun `assert` ne couvre — et `assert` est de toute façon un
   no-op sur un disque NDEBUG (UTIL.H:124 contre :129). Douze contrôles : portails en tête, octet
   de tuile après `tileBase`, classe de tuile sous une cellule, motif < 8, indexation relative des
   faces, un sol et un plafond par secteur, plafonds par mur (`MAXVPERWALL` 700,
   `MAXNMSLAVEPOLYS` 1300), `cutIndex`, degré entrant (`MAXFANIN` 20), curseur `firstParam`,
   grille de lumière, débordement des tableaux fixes ;
3. **le coût**, par la carte de `tools/cout.py`, plus la RAM résidente et la pression sur le cache
   de tuiles du VDP1.

**Calibration.** Les 24 niveaux retail passent **tous** les contrôles FATAL ; seule l'alerte
`fanin` sort, sur trois niveaux, avec les 36 portails entrants de CAVERN. Et inversement, un banc
de mutations (une faute injectée à la fois) montre que **12 contrôles sur 12 savent tirer**. Un
FATAL de cet outil est donc un vrai défaut, pas un cri de garde trop zélé.

**Le contrôle qui justifie l'outil à lui seul** est `portails_en_tete`. Un niveau qui l'enfreint
démarre, se dessine, et ne montre simplement jamais les salles derrière le portail mal rangé. Sans
message, sans ralentissement, sans trace.

### 2.2 `tools/blender/io_lev/` — voir un niveau

Extension Blender en lecture seule (4.2 minimum, développée sur 5.2.2 LTS) : géométrie exacte,
**vraies tuiles décodées du fichier**, lumière par sommet multipliée à la texture, portails en fil
de fer, objets nommés, attributs par face (`secteur`, `mur`, `genre`), option « sans les plafonds »
pour voir l'intérieur d'en haut, et une couche de couleur `cout` si on lui passe le JSON de
`lev_report.py`.

L'architecture est dictée par la vérifiabilité : **tout ce qui peut être faux est hors de Blender.**
`levdata.py` lit le fichier, `scene.py` en fait une description, et les deux se jugent sans Blender.

* `verif_io_lev.py` confronte la lecture embarquée à `tools/lev.py` champ par champ, et déroule le
  flux RLE de **chaque** tuile ;
* `verif_scene.py` confronte, secteur par secteur, le nombre de quads produits à ce que
  `tools/cout.py` compte par un tout autre chemin — l'un lit `tileLength`/`firstFace`, l'autre
  **fabrique** les quads un par un ;
* `test_headless.py` ne teste plus que la couche `bpy`, et sort un rendu.

**Mesure 2026-09-23 : les deux vérificateurs passent sur 34 fichiers, zéro écart**, et l'import
tourne dans Blender 5.2.2 sur du retail comme sur du converti.

Trois choses ont été apprises en le construisant, toutes contre-intuitives :

* **L'octet de lumière est empaqueté.** `worldGrey` fait `WORLDGREEN_NM*32 = 128` entrées et
  l'assembleur l'indexe avec l'octet **tel quel** : bits 0-4 le niveau, bits 5-6 la bande de
  couleur du brouillard (UTIL.H:74-84, UTIL.C:151). **16 est le neutre, pas 31** ; 17..31 est le
  plancher de la bande. Le retail n'utilise que la bande 0 ; les niveaux Doom du fork utilisent les
  quatre et montent à 112, ce qui est parfaitement licite.
* **Les palettes des tuiles 8 bpp sont mortes.** `load8BPPRLETile` lit `palNm` et le **jette** —
  `addPic` reçoit `NULL` (PIC.C:996, :1010) — et le VDP1 tire par la banque CRAM 0, c'est-à-dire la
  palette objet. Honorer `palNm` recolorie faux : sur TOMB, 237 des 414 tuiles 8 bpp annoncent la
  palette 13, qui diffère de la palette 0 sur ses 255 entrées.
* **Un indice de sommet répété n'est pas une cellule vide, c'est un triangle.** Le format range un
  triangle en quad dont un sommet se répète, comme le VDP1 le dessine. ⚠ Le premier jet de
  l'extension les écartait tous en les croyant plats. **Mesure du 24-09 sur 33 fichiers : 28 482
  cellules ont un indice répété, dont seulement 411 sont réellement plates** — sur E1M1, les 439
  écartées étaient 439 triangles et zéro cellule plate, soit **11 % des surfaces du niveau perdues
  en silence**. Corrigé : elles sont rendues comme triangles, et `verif_scene.py` juge l'invariant
  des coins sans Blender, sur 34 fichiers. Les vraies cellules plates (les deux faces d'un portail)
  restent écartées et comptées à part.

---

## 3. L'étalon retail, et pourquoi il vaut mieux que les paliers

`tools/cout.py` prévient déjà que les paliers 470 / 896 / 1321 cellules viennent d'un build ASSERT
et surestiment la pente d'environ 1,5× en NDEBUG. La mesure du 23-09 le confirme par l'autre bout,
en passant les 24 niveaux **commerciaux** à la même moulinette :

| grandeur (cône 53°, par position debout) | min | médiane | max |
|---|---|---|---|
| médiane des positions | 452 (TOMBEND) | 920 | 1441 (SETARENA) |
| pire position | 1335 | 2157 | 3636 (CAVERN) |
| RAM résidente | 496 Ko | 1 220 Ko | 1 342 Ko |
| secteurs / murs / tuiles | 145 / 1171 / 226 | 358 / 2917 / 484 | 570 / 4939 / 652 |

Autrement dit : **PowerSlave commercial serait déclaré majoritairement sous les 30 images par
seconde par ces paliers.** Ils ne sont donc pas un seuil de recette, ils situent. La question utile
pour un auteur n'est pas « suis-je au-dessus de 470 » mais « suis-je plus cher que le pire niveau
qui ait été pressé sur un CD » — d'où `--etalon`, qui donne le rang.

Repère : l'**E1M1 converti** sort à une médiane de 592 et un pire cas de 1574, soit **8 % et 12 %**
du retail. La conversion Doom est, pour l'instant, dans le bas de la fourchette.

⚠ Le corollaire est une dette : il faut refaire les cinq captures console de
`STEXT_BASELINE_2026-09-12.md` sous NDEBUG, en faisant **varier le point de vue**. Tant que ce n'est
pas fait, les millisecondes imprimées sont un majorant, et l'outil le dit à chaque ligne.

---

## 4. Le budget dans Ultimate Doom Builder — **fait**, sous forme de calque

`tools/doom2ps/udb_budget.py` produit un **calque de budget** : un PWAD jetable, posé *à côté* de
la carte de l'auteur, qu'il ouvre dans UDB pour voir ce que chaque salle coûte au peintre de la
Saturn. `tools/doom2ps/verif_udb.py` le juge en cinq familles et **prouve qu'il sait tirer**.

### 4.1 Pourquoi un calque à part, et pas la carte de l'auteur

Le seul canal qu'UDB colore sans une ligne de greffon est la **luminosité** des secteurs (mode
*Brightness* de la vue 2D, `Renderer2D.cs:1641`). Or la luminosité n'est pas un champ libre : c'est
une donnée de jeu que l'auteur règle à la main, et que `doom3d.py` convertit en lumière par sommet.
Y écrire le coût écraserait son travail d'éclairage — et il ne s'en apercevrait qu'après la
conversion suivante.

D'où le parti pris : **on ne touche jamais à la carte de l'auteur.** Le calque est une copie
jetable dont trois champs ne servent plus qu'à porter la mesure. L'auteur garde les deux fichiers
ouverts, édite l'un, relit l'autre.

Le pont existe déjà et il est exact : `geom3d.json` porte `doom_sector[secteur_lev] → secteur Doom`
(219 → 83 sur E1M1), et les deux repères sont **1:1, sans facteur d'échelle** (`doom3d.py:10`) —
vérifié en bout de chaîne : `cd_doom/E1M1.LEV` et `DOOM1.WAD/E1M1` couvrent exactement
`-768..3808` en X et `-4864..-2048` en Z.

### 4.2 Trois canaux, la même mesure

| champ | ce qu'il porte | où ça se voit |
|---|---|---|
| `light` | dégradé continu 48..255 | la vue 2D en mode *Brightness* : tout le niveau en carte de chaleur d'un coup d'œil |
| `floorpic` | un aplat `BUDGET0..3`, livré **dans** le PWAD entre `F_START` et `F_END` | le mode visuel 3D : une carte de chaleur **qu'on parcourt à pied** |
| `tag` | le **nombre exact** de cellules | l'auteur clique un secteur et lit la valeur, au lieu de deviner une nuance de gris |

⚠ **Correction au plan précédent.** La voie « T1 — la couleur » annonçait ~1 jour de configuration
UDMF pour teinter les sols par `lightcolor`. C'est inutile : un **aplat de 4 096 octets** (un seul
index de palette, choisi par plus proche voisin dans le `PLAYPAL` du WAD source) donne la même
carte de chaleur parcourable à pied, en **format Doom binaire**, sans UDMF et sans configuration de
jeu. C'est possible précisément *parce que* c'est un calque : il a le droit de perdre ses vraies
textures. T1 est donc retirée de la liste des choses à faire.

Tout le reste — hauteurs, type de secteur, plafonds, et **tous les autres lumps** — est copié
**octet pour octet**. C'est ce qui rend le calque reconnaissable comme étant *sa* carte, et c'est
vérifié.

### 4.3 Les deux mesures, et pourquoi c'est la vue par défaut

* **`--metrique vue`** (défaut) : le pire cône de `cout.carte` parmi les positions debout du
  secteur. C'est la seule qui réponde à « est-ce que ça garde 30 images ? », parce que le coût
  d'une image est celui de tout ce qu'on **voit**, pas de la pièce où l'on est. Passe par
  `ordre.visibilite`, le calcul cher.
* **`--metrique propre`** : les cellules que le secteur **possède**. Elle ne chiffre pas une image,
  mais c'est la seule **actionnable** : elle désigne la pièce à dégraisser.

Les deux règles d'agrégation diffèrent, et ce n'est pas un détail : les cellules possédées
**s'additionnent** sur le secteur Doom (c'est bien la même pièce qui les paie), le pire cône se
prend au **maximum** (on ne se tient qu'à un endroit à la fois).

### 4.4 Les bornes de couleur sont celles du retail, pas celles de la loi

Peindre une carte en rouge à 1 321 cellules mentirait à l'auteur : §3 montre que les paliers de la
loi déclareraient PowerSlave commercial majoritairement sous les 30 images. Les bornes sont donc
**dérivées à l'exécution de `tools/lev_etalon.json`** — si on recalcule l'étalon, le calque suit.
Pour la vue : vert ≤ **920** < jaune ≤ **1 474** < orange ≤ **2 157** < rouge. Le rouge veut dire
« au-delà de ce que le niveau retail médian montre à son pire endroit », ce qui est une phrase
qu'on peut défendre devant un auteur. `--bandes paliers` rend les seuils de la loi à qui les veut.

### 4.5 Ce qui n'est pas mesuré est peint comme tel

Il n'y a pas de silence, et c'est délibéré : un secteur non mesuré peint en vert ferait croire à
l'auteur qu'il est bon marché.

* **`BUDGETNA` (bleu)** — aucune position debout. Les positions sont sur une maille de 64 u
  (`ordre.PAS`, à ne pas affiner), donc un secteur étroit peut n'en contenir aucune. **Mesure sur
  E1M1 : 17 secteurs Doom sur 83.** Ce n'est pas rien, et l'outil l'imprime.
* **`BUDGETXX` (magenta)** — le secteur Doom n'existe pas dans le `.LEV` : le convertisseur l'a
  supprimé. Sur E1M1, 2 sur 85.

**L'épisode 1 converti, une ligne par carte** (métrique `vue`, bornes retail) :

| carte | vert | jaune | orange | rouge | NA | XX | pire vue |
|---|---:|---:|---:|---:|---:|---:|---:|
| E1M1 | 43 | 19 | 4 | 0 | 17 | 2 | 1 602 |
| E1M2 | 148 | 8 | 2 | 0 | 42 | 0 | 1 710 |
| E1M3 | 123 | 3 | 0 | 0 | 51 | 0 | 1 273 |
| E1M4 | 105 | 3 | 0 | 0 | 31 | 0 | 1 055 |
| E1M5 | 107 | 3 | 0 | 0 | 33 | 0 | 945 |
| E1M6 | 163 | 4 | 0 | 0 | 83 | 0 | 1 061 |
| E1M7 | 90 | 23 | 0 | 0 | 57 | 0 | 1 304 |
| **E1M8** | 33 | 15 | 1 | **1** | 24 | 0 | **2 413** |
| E1M9 | 129 | 0 | 0 | 0 | 18 | 0 | 830 |

Deux choses sautent aux yeux, et c'est précisément ce que le calque est fait pour montrer.
**E1M8 est la seule carte à porter un secteur rouge** — 2 413 cellules à son pire point de vue,
au-delà de ce que le niveau retail médian montre à son plus mauvais endroit ; c'est là qu'il faut
aller regarder en premier. Et **la colonne NA n'est pas marginale** : rapportée au total des
secteurs convertis, elle va de **12 %** (E1M9, 18 sur 147) à **34 %** (E1M7, 57 sur 170), en
passant par 17 sur 83 (E1M1) et 83 sur 250 (E1M6). La maille de 64 u ne pose aucune position debout
dans les couloirs, les marches et les seuils de porte. Le calque `propre` les couvre, lui.

### 4.6 La marche à suivre

```
python tools\doom2ps\udb_budget.py --wad DOOM1.WAD --map E1M1 ^
       --lev cd_doom\E1M1.LEV --geom build\doom2ps\e1m1_geom3d.json
python tools\doom2ps\udb_budget.py ... --metrique propre --pire 0
```
Chaque appel écrit **quatre fichiers** : `E1M1_VUE.wad`, son `.csv` secteur par secteur, un
`.dbs` (les réglages qu'UDB range à côté d'un WAD : configuration de jeu + WAD source en
ressource) et un `.bat` d'un clic. Le nom par défaut porte la métrique, précisément pour que le
second calque n'écrase pas le premier.

**Il n'y a donc aucune étape de configuration** : double-clic sur le `.bat`, puis soit basculer la
vue 2D en mode *Brightness*, soit entrer en mode visuel 3D et marcher. Les huit pires positions
debout sont posées comme objets de type **32000** — un type inconnu, donc dessiné *et* signalé par
le vérificateur d'UDB, impossible à confondre avec le décor.

Le `.bat` existe **en plus** du `.dbs` pour une raison mesurée : UDB relit bien le `gameconfig` du
`.dbs`, mais sur le chemin d'ouverture *par ligne de commande* il n'applique pas sa liste de
ressources (§4.7). Le `.bat` passe donc `-RESOURCE WAD` explicitement. Il résout `Builder.exe` par
la variable d'environnement `UDB`, sinon par les emplacements usuels — aucun chemin de machine
n'est codé en dur dans l'outil.

Le calque est **en aval d'une conversion** : il se régénère après chaque passe du convertisseur,
et son seul coût propre est celui d'`ordre.visibilite` pour la métrique `vue` (la métrique `propre`
est quasi instantanée).

⚠ **Le calque est jetable et ne se joue pas.** Son `tag` porte un nombre de cellules, pas un numéro
de tag ; le tester dans un port lui ferait faire n'importe quoi. Le lump texte `BUDGET` du PWAD le
redit à qui l'ouvrirait plus tard, et l'outil refuse d'écrire sur le WAD source.

### 4.7 Ce que la vérification garantit

`verif_udb.py` est écrit **sans importer** `udb_budget`, avec son propre lecteur de WAD et sa
propre écriture de la règle du peintre. Cinq familles : **A** le calque est une copie octet pour
octet (plus la conformité du fichier au format : répertoire dans le fichier, lumps qui ne se
chevauchent pas, noms légaux) ; **B** seuls trois champs bougent, et un `F_SKY1` n'est *jamais*
repeint ; **C** depuis le seul `tag`, l'aplat **et** la luminosité sont ré-dérivés et comparés ;
**D** la mesure est ré-dérivée et ré-agrégée ici, puis comparée secteur par secteur ; **E** les
marqueurs sont du bon type et tombent dans la boîte englobante du WAD — ce qui revérifie le
repère 1:1.

**Le calque est auto-descriptif.** Son lump `BUDGET` porte une ligne `PARAMS metrique=… bandes=…
fov=… pire=… bornes=…`, et le vérificateur la relit *au lieu* de recevoir des drapeaux : sinon un
drapeau oublié lui ferait accuser un calque correct, ce qui est le pire défaut possible pour un
outil de contrôle — celui qui apprend à l'auteur à l'ignorer. Les bornes déclarées sont en plus
**confrontées** à celles qu'on re-dérive de l'étalon, ce qui détecte un calque fabriqué contre un
étalon périmé.

**Mesure 2026-09-24, après les correctifs de §4.7 ter : 9 cartes (E1M1..E1M9) × 2 métriques, zéro
défaut ; banc de mutations 15 fautes sur 15 en `vue` — familles A B C D E — et 12 sur 12 en
`propre`, où la famille E n'a rien à éprouver et où le banc le dit** au lieu de laisser croire le
contraire. Les quatre combinaisons de `--garder-sols` / `--plafonds` sont exercées ; `--plafonds`
ajoute une quatorzième mutation, un plafond qui ment sur son tag, et elle tire. Le banc commence par
exiger qu'un aller-retour *sans* mutation repasse, sinon il mesurerait son propre écrivain.

### 4.7 ter Ce qu'une relecture contradictoire a trouvé *après* le premier commit

Quatre relecteurs indépendants ont été lancés sur les trois outils avant le `push`, chaque trouvaille
passant ensuite devant un réfutateur chargé de la démolir. Treize verdicts, **huit réfutations** — et
**cinq défauts réels**, dont deux dans le vérificateur lui-même, c'est-à-dire dans la pièce qui n'a
pas le droit de mentir. Aucun des cinq ne se voyait à la relecture ordinaire ; les deux plus graves
ont été reproduits en direct avant d'être crus.

1. ⚠ **Le vérificateur ne regardait pas le plafond.** Sous `--plafonds`, le plafond devient un
   quatrième canal de mesure, mais la famille B cesse de le contrôler dès que le drapeau est posé et
   la famille C ne lisait que le sol. Mesuré : repeindre **les 78 plafonds non-ciel d'E1M1 en vert**
   laissait imprimer « aucun défaut » et sortir avec le code 0, pendant qu'un auteur parcourant la
   vue 3D lisait sa pièce la plus chère comme bon marché. Les deux canaux sont maintenant re-dérivés.
2. ⚠ **La famille A ne vérifiait pas que les aplats étaient *entre* `F_START` et `F_END`** — or
   c'est le seul critère qui compte : un aplat rangé hors de la plage n'est pas un flat pour
   l'éditeur, il est ignoré, et tous les sols s'affichent comme textures manquantes.
3. **La légende des couleurs mentait dans deux modes sur trois.** Elle était écrite une fois pour
   toutes dans le vocabulaire des quantiles du *cône* (« entre médiane et p90 retail »), alors que
   `--metrique propre` tire ses bornes du plus gros *secteur* retail et `--bandes paliers` de la loi
   de coût. Le rapport imprimait donc « au-dessus de la médiane retail » pour des pièces qui étaient
   dessous. La légende se dérive désormais des mêmes trois nombres que la peinture.
4. **`lev_report.py` annonçait une médiane des médianes de 928** que l'étalon ne peut produire
   d'aucune façon : la vraie vaut 907, la convention `t[n//2]` que suivent les outils donne 920, la
   moyenne 886. Corrigé en 920, avec la convention nommée.
5. **Le vérificateur plantait au lieu de diagnostiquer.** Passé un `geom3d.json` d'une autre carte,
   il enregistrait bien « ils ne viennent pas de la même conversion » puis continuait et levait une
   `IndexError` — la trace remplaçait le diagnostic. Il s'arrête maintenant proprement.

Deux durcissements sont venus par la même passe, sans qu'un défaut vivant ait été prouvé : la garde
« ne pas écrire sur le WAD source » compare désormais des chemins `normcase` (sous Windows
`doom1.wad` et `DOOM1.WAD` sont le même fichier), et le `.bat` ne préfère `%UDB%` que si cette
variable désigne un exécutable existant.

Enfin, **le banc ne coupe plus en silence** : une mutation sans objet est écartée *et nommée*, le
rapport imprime les familles réellement exercées et avertit de celles qui ne le sont pas. En
`--metrique propre` il n'y a pas de marqueur, donc la famille E n'est pas éprouvée — le score plein
ne la couvrait pas, et se lisait pourtant comme si.

**Trois défauts réels que le banc de mutations avait trouvés avant elle :**

1. Un contrôle de bandes **ordinal** — frontières relues dans le calque plutôt que re-dérivées —
   laisse passer un relabelage cohérent. C'est **E1M9** qui l'a montré : son pire cône vaut 830,
   tout le niveau tient sous la première borne, donc repeindre son secteur le plus cher ne casse
   aucun ordre. D'où la re-dérivation.
2. Une mutation **qui ne change rien** se lit comme un échec de la vérification. Celle qui repeint
   le pire secteur choisit maintenant un aplat différent du sien.
3. `--metrique propre` sans `--pire 0` produisait un calque **coupable à sa propre vérification** :
   la déclaration annonçait 8 marqueurs quand la métrique n'énumère aucune position debout. Trouvé
   *parce que* la déclaration existait ; elle annonce désormais le fait, pas l'intention.

### 4.7 bis Ce qu'UDB lui-même a confirmé (2026-09-23)

UDB **R4327 x64** est désormais installé (portable, `%USERPROFILE%\Tools\UltimateDoomBuilder`,
téléchargé depuis le dépôt Pages de l'org elle-même). Il tient un journal, `UDBuilder.log`, et
`-PORTABLE` le place à côté de l'exécutable : on peut donc vérifier une ouverture **sans avoir à
juger un écran**. Quatre faits, chacun mesuré :

1. **Le calque s'ouvre.** `Opening map "E1M1" with configuration "Doom_DoomDoom.cfg"` →
   `Initializing map format interface DoomMapSetIO` → `Reading map data from file`.
2. **Les six aplats sont chargés.** UDB annonce `Loaded 124 textures, 60 flats` sur le calque
   contre **54 flats** sur la carte d'origine intacte : 54 + 6, exactement les nôtres. C'est la
   preuve que `F_START`/`F_END` dans un PWAD suffit — le pari de §4.2.
3. **Le `.dbs` écrit à la main est bien relu.** En y remplaçant `Doom_DoomDoom.cfg` par
   `Boom_DoomDoom.cfg`, l'éditeur ouvre en Boom. Le format et les clés viennent de la source
   d'UDB (`MapOptions.cs:342-430`, `DataLocationList.cs:94-115`, `DataLocation.cs:30-32`), pas
   d'une supposition. ⚠ En revanche, sur le chemin *ligne de commande* il n'applique **pas** la
   liste de ressources : le journal ne montre alors que le calque et se plaint de l'absence de
   palette. D'où le `.bat`.
4. **Les seules erreurs restantes ne viennent pas de nous.** 45 `Unable to find sprite lump`, tous
   pour des acteurs absents du WAD *shareware* (Cacodémon, Cyberdémon, plasma, BFG, clés-crânes),
   plus un `SW18_7 is double defined` interne à `DOOM1.WAD`. Une `NullReferenceException` en fin de
   journal apparaît **à l'identique sur la carte d'origine intacte** : c'est l'artefact du lancement
   sans fenêtre suivi d'un arrêt forcé, pas le calque. Expérience de contrôle, pas déduction.

Ce qui reste non vérifié est **uniquement visuel** : est-ce que le dégradé se lit bien, est-ce que
les bandes se distinguent à l'œil. C'est la seule chose qu'un journal ne peut pas dire.

Vu au passage : `VisplaneExplorer.dll` est **livré** avec UDB, et `Doom_common.cfg` porte une
section `visplaneexplorer` avec ses hauteurs de vue. Le patron de la voie T3 n'est donc pas
seulement dans le source — il est installé et il tourne. Il mesure les visplanes du renderer PC,
pas nos cellules Saturn, donc il ne remplace pas le calque ; mais c'est une référence de travail.

### 4.8 Ce qui reste ouvert côté UDB

* **UDBScript** (`.js`, Jint, livré dans UDB) recalculerait et réécrirait `sector.brightness` sur
  un raccourci, sans relancer l'outil. ⚠ son API n'a **aucune primitive de dessin** : elle écrit
  des valeurs, elle ne peint pas de calque.
* **Un vrai greffon C#** — le patron est dans l'arbre d'UDB : `Source/Plugins/VisplaneExplorer` est
  *exactement* cette fonctionnalité, un `[EditMode] : ClassicMode` qui peint un
  `DynamicBitmapImage` sur la vue 2D à partir d'une métrique externe. .NET Framework 4.7.2, GPL-3,
  et pas d'ABI publique stable : taxe de maintenance à chaque build d'UDB. Le calque rend ce
  greffon largement inutile.
* **La configuration de jeu + le `.bat` de *Test Map*** — c'est le point 4 de l'ordre de marche, et
  il n'est **pas** fait. `testparameters` permet de faire lancer n'importe quel exécutable par le
  bouton *Test Map* (un `.bat` qui convertit, mastérise l'ISO et démarre Ymir), et `Compilers/*.cfg`
  d'enregistrer notre convertisseur comme « nodebuilder ». Les tables de `doom_specials.py`
  engendreraient le `.cfg` qui masque ce qu'on ne convertit pas.

**Limite honnête, inchangée** : un format sur disque réellement propre n'est **pas** atteignable
par configuration — seules trois valeurs de `formatinterface` existent dans le source d'UDB. On
reste en Doom binaire ou en UDMF sur le disque, et on convertit à côté. Les deux précédents
vérifiés de format exotique (PSX Doom chez GEC, SRB2 avec Ultimate Zone Builder) sont des **forks**,
pas des configurations.

---

## 5. Ce que ferait l'étape 4 (export Blender)

C'était la ligne la plus obscure du message précédent, et à raison : elle ne disait pas *ce qu'on
éditerait*. Voici la réponse.

**L'export ne rouvrirait que la couche A.** Concrètement, dans Blender, l'artiste :

* change la **tuile** d'une cellule ou d'une face — c'est-à-dire réassigne un matériau ;
* change le **motif** — c'est-à-dire retourne la texture d'une cellule (le seul placage réglable du
  moteur : `pattern[8][4]`, WALLS.C:1153) ;
* repeint la **lumière** par sommet, au pinceau de couleurs de sommet ;
* **déplace un objet**, ou en change l'angle.

Puis l'export **compare la scène au fichier d'origine** et n'écrit que les octets qui diffèrent,
par `lev_io` + `lev_write` — dont l'identité octet-pour-octet est prouvée sur les 24 fichiers
retail. Il **verrouille la géométrie** par une empreinte sur la topologie : si le nombre de quads,
leur ordre ou leurs positions ont bougé, il refuse d'écrire et dit lesquels. Il est donc
**structurellement incapable de produire un `.LEV` invalide** : il ne touche à aucun champ dérivé.

C'est exactement le même périmètre que l'éditeur tiré du moteur (§7), et c'est voulu — les deux
écriraient le même genre de journal, sur la même couche.

Chiffré : les offsets sont tous calculables et ont été vérifiés sur trois fichiers — `face.tile` à
`face.off + 10*i + 8`, la paire de texture à `texture.off + 2k` et `+2k+1`, `vertexLight` à
`vertexLight.off + i`, la lumière d'un sommet à `vertex.off + 8*i + 6`, les paramètres d'objet à
`objectParams.off + firstParam`. Le seul piège de conversion est `tileBase`, que le chargeur ajoute
en RAM (LEVEL.C:122-125) et qu'un journal doit donc soustraire ou porter.

**Ce que ça n'achète pas** : rien de la couche B. Déplacer un mur reste le travail du compilateur.

---

## 6. Un éditeur Build ?

**Le lecteur existe déjà et il est meilleur que je ne le pensais — mais la chaîne est incomplète,
et pas là où on l'attend.**

Ce qui est vrai :

* `duke2ps` lit un `.MAP` Build **directement** (`build_import.py:350` → `buildmap.parse`), versions
  6 et 7, refus explicite au-delà (`buildmap.py:157`). Mapster32 écrit du v7 (du v8 si les limites
  v7 sont dépassées) et jamais du v9 : la cible est donc la bonne.
* C'est le **seul chemin qui exprime les pentes**, que le moteur sait faire nativement et que Doom
  ne sait pas dire : 99 murs inclinés émis sur E1L1, contre **0** sur l'E1M1 converti. Sur le
  retail, 1 548 murs de sol ou de plafond sont inclinés et 943 murs penchés.
* Il existe une **trousse de cartographie PowerSlave publique** (oasiz, *Return to Ruins*) : un
  Mapster32 modifié avec le correctif de palette de Hendricks256, le `backmap` de Ken Silverman
  pour repasser du v7 au Build v6, un script de conversion au playtest, un `names.h` réécrit et une
  feuille de tags documentée. Un artiste a donc déjà un éditeur et de la documentation communautaire.
  (`backmap` sert à rejouer dans le moteur **PC** ; notre convertisseur, lui, lit le v7 directement.)

⚠ **CORRECTION DU 23-09, EN COURS DE JOURNÉE.** Une première version de cette section disait « la
chaîne casse à la deuxième carte jamais essayée ». **C'est faux dans les deux sens**, et le dépôt
portait déjà la preuve : `build/duke2ps/batch/rapport.json` est un passage sur **194 fichiers**
(**129 cartes distinctes** après déduplication par géométrie — 61 groupes de doublons). Résultat :
**19 cartes distinctes passent (14,7 %)**, 102 échouent dans `convex.py`, 8 passent mais dépassent
un budget. Parmi les 19 : cinq niveaux solo de Duke (E1L1, E1L7, E2L9, E3L11, E4L8), cinq cartes
communautaires et neuf arènes. E1L7 se convertit de bout en bout en ~11 s.

Ce qui bloque, mesuré plutôt que supposé :

* **Les échecs se décomposent en trois causes, et la plus grosse n'est pas géométrique.**
  **46 % (71 des 155)** sont un `TypeError` à `convex.py:1128` parce que `build_import.py` ne tire
  le point de départ que d'un téléporteur SE7 situé dans la composante « toit » qu'il vient de
  jeter (`:931-968`) : `depart_m1` est nul sur **57 % des cartes**. Or l'en-tête Build porte
  `posx/posy/posz/ang/cursectnum` et `buildmap.py:155` les décode déjà. **Une vingtaine de lignes
  retirent près de la moitié des échecs** — en prenant garde que `m.cursectnum` sert aussi
  aujourd'hui à choisir la composante à jeter (`build_import.py:387`).
  30 % sont une orientation de boucle, 25 % une triangulation.
* **La quantification est la vraie coupable, et elle disparaît sur une carte originale.** Sur les
  194 fichiers, il n'existe **aucun secteur auto-sécant à la source** ; il y en a **22 après le
  collage à la grille**. Les secteurs défectueux passent de 77 à 297 (×3,9). Diagnostic refait sur
  E1L3 s250 : le collage aux multiples de 8 rend trois points colinéaires et fait tomber un sommet
  strictement sur une arête — le polygone devient non simple et **0 des 76 stratégies** de
  triangulation n'aboutit. Le test d'oreille (`convex.py:315-327`), lui, est **correct**.
  Le remède est en amont : la grille 8 de Mapster32 **est** l'unité Saturn (`build.cpp:8980-8984`,
  à sélectionner par SHIFT+G ; l'autogrid plafonne à 7, donc des multiples de 16, ce qui reste
  sûr), et `quantize --tol-u 0` existe déjà (`quantize.py:452`). Mesure sur 18 cartes réelles :
  **92,4 % des 27 315 murs sont déjà sur la grille 8**, et les z des secteurs à 100 % sur 128.
  ⚠ Nuance : écrire des secteurs convexes court-circuite le pontage et l'oreille, **pas** le
  validateur d'orientation des boucles (`convex.py:535`), qui reste le premier à crier.
* **La couche d'objets est petite, et son absence est une jointure débranchée, pas un trou.**
  `assemble.py:212` n'émet qu'un `OT_PLAYER`. Mais les sprites Build traversent l'import avec leurs
  24 champs (639 → 572 sur E1L1, 67 perdus avec les composantes jetées) et `convex.py:1136-1145`
  les affecte déjà à leur morceau convexe — puis **`geom3d.py` ne les lit jamais**. Or l'ABI moteur
  est triviale : des shorts gros-boutistes (`OBJECT.C:190`), et les 72 étiquettes de `placeObjects`
  couvrant 160 des 228 valeurs se ramènent à **deux formes** — 1 short (secteur) ou 5 shorts
  (secteur, x, y, z, angle). Et `doom_specials.py` n'est énorme que parce qu'il **émule les
  linedefs de Doom** : **96 de ses 1 003 lignes de fonction sont réutilisables** (~10 %), le reste
  est une sémantique qu'un jeu original ne réécrit pas, il la conçoit autrement.
  **Estimation : 150 à 250 lignes, 2 à 3 jours.**
* Mapster32 est du C sans système de greffons : **aucune carte de chaleur** n'y est possible sans
  livrer son propre `mapster32.exe`. L'astuce T0/T1 d'UDB n'a pas d'équivalent.

**Verdict révisé** : pour **convertir Duke**, le chemin Build reste inégal (14,7 % des cartes). Pour
**créer des niveaux originaux**, il est le bon choix — la classe d'échec dominante est un bogue de
vingt lignes, la deuxième s'évapore si on dessine sur la grille 8, et la couche d'objets est de
l'ordre de la semaine. C'est aussi le **seul** chemin qui exprime les pentes, les murs penchés et
la salle-sur-salle que le moteur sait faire et que Doom ne sait pas dire.

---

## 7. L'éditeur tiré du moteur, limité aux retouches

C'est la bonne façon de le cadrer, et c'est aussi ce que le code permet : **limité à la couche A,
il est bon marché ; étendu à la géométrie, il devient un deuxième compilateur.**

**Trois des quatre pièces existent déjà.**

* **La visée est écrite.** `push()` (SRUINS.C:854-877) construit déjà un rayon depuis
  `playerAngle.yaw/pitch`, appelle `hitScan` (HITSCAN.C:194-271) et reçoit `COLLIDE_WALL|w` — donc
  l'index du mur — avec le point d'impact. 24 lignes, exécutées à chaque appui sur USE. Ce qu'il ne
  rend pas, c'est la **cellule** : la résoudre (projeter le point sur les deux axes du mur, ou
  point-dans-quad pour un maillage) fait 150 à 250 lignes.
* **L'interface existe et ne coûte aucune RAM résidente.** `PAUSE.OVL` est un système de menus
  complet piloté à la manette — pages, curseur, édition de valeur avec répétition sur
  GAUCHE/DROITE, son propre rendu de police écrivant le bitmap NBG0 du VDP2 par le CPU sur une
  image VDP1 gelée. 9 316 octets d'image et 356 de BSS, et `MAIN` n'en paie que 39 lignes.
  ⚠ Contrainte explicite : une surcouche `OVL_SCRATCH` ne peut appeler ni `EZ_*` ni
  `SCL_DisplayFrame` (`tools/ovlpack.py:52`). Un éditeur **sur image gelée** est donc gratuit ; un
  surlignage **vivant** de la cellule visée devrait, lui, être résident.
* **Le vol libre existe** (`noClipCheat`, SPRITE.C:958-973) ; il n'y manque que le haut/bas.

**La quatrième pièce — faire ressortir les retouches — a une réponse vérifiée sur Ymir et une
réponse praticable sur console.** Ymir écrit `ymir_profile/state/bup-int.bin`, une image brute de
32 768 octets de la RAM de sauvegarde interne, formatée, sur le disque : on écrit un journal avec
`BUP_Write` (BUP.C:107-142 est le patron) et un script Python le relit. Sur console, le même
journal atterrit sur une carte mémoire. Et comme les tableaux du niveau sont à des adresses fixes
du `.map` du build, un vidage mémoire d'Ymir permet même de relire les tableaux édités sans aucun
journal.

**Le prix, et il faut le dire à l'avance.** Environ 400 à 500 lignes de C — 250 résidentes, 250
dans la surcouche en réutilisant `PAUSE.C` — plus 150 lignes de Python. Soit ~7 à 9 Ko en surcouche
(0 résident) et 2,6 à 6,6 Ko résidents, c'est-à-dire **0,6 à 1,6 tuile de contenu** à la règle du
projet (4 Ko = 1 tuile). La parade est l'idiome du `Makefile` lui-même : `STATUSTEXT=1` et `WALK=1`
construisent chacun leur propre arbre ; un `EDIT=1` ne ferait payer les tuiles que sur le disque de
l'auteur.

⚠ **Le piège de cette parade**, et il est sérieux : `doom2ps` dérive le budget de tuiles du `_end`
de `MAIN`. Un arbre EDIT a un `MAIN` plus gros, donc son niveau converti reçoit **moins** de tuiles
et `doomtiles.reduire` choisit un **jeu de tuiles différent** — les index du journal ne voudraient
plus dire la même chose sur le disque de série. Le build d'édition doit être **forcé** au budget de
tuiles du build de série, et cela doit être asserté, pas supposé.

### L'alternative écartée : un rendu PC

Plus cher qu'il n'en a l'air. Le chemin de rendu, c'est 4 872 lignes de `WALLS.C` — avec seulement
45 sites d'appel `EZ_*`, donc une frontière fine, ce qui est bon signe — mais la projection et
l'éclairage par sommet sont **450 lignes d'assembleur SH-2 sans repli en C** (WALLASM.H:1-16). Et
ce qui fait que l'image est celle de la Saturn, c'est le rastériseur de sprites distordus du VDP1
plus le cache de 32 slots de `PIC.C` et sa barre `PIC_LOD_PX` — précisément le mécanisme qui décide
quelles cellules passent en aplat, c'est-à-dire ce que le budget de tuiles achète. `refs/reyeme-viewer`
(MIT, C#/SharpGL, 1 130 lignes) charge déjà un `.LEV` et dessine la géométrie sans textures ; son
propre README liste « charger les textures » et « charger les entités » comme non faits.

Mais le vrai obstacle n'est pas le portage, c'est la **synchronisation** : `WALLS.C` a changé à
chacun des cinq derniers commits de cette branche. Un deuxième peintre serait à revalider contre la
console après chaque changement. L'argument décisif en faveur de l'éditeur dans le moteur est
exactement là : **Ymir fait tourner le vrai `MAIN.BIN`**, donc il est synchronisé par construction,
et il montre le cache de tuiles et le coût d'image réels qu'un aperçu PC devrait feindre.

---

## 8. Ordre de marche

1. ~~`lev_report.py`~~ — **fait**, calibré sur le retail, 12 contrôles sur 12 prouvés capables de tirer.
2. ~~Importeur Blender en lecture~~ — **fait**, vérifié sur 34 fichiers, rendu à l'appui.
3. ~~Le calque de budget UDB~~ — **fait** (§4) : `udb_budget.py` + `verif_udb.py`, 9 cartes × 2
   métriques sans défaut, 15 mutations sur 15 rattrapées, aucune ligne dans UDB. UDB R4327 est
   installé et son journal confirme l'ouverture, les six aplats et la relecture du `.dbs` (§4.7 bis).
   Ne reste que le jugement **visuel**.
4. **La configuration de jeu UDB + le `.bat` de *Test Map*** — la boucle d'auteur complète sur le
   compilateur qui existe déjà. Les tables de `doom_specials.py` et `doom_ids.json` engendrent
   le `.cfg` qui masque ce qu'on ne convertit pas. ⚠ Le `.bat` livré par `udb_budget.py` **n'est pas
   celui-là** : il ouvre un calque, il ne convertit rien et ne démarre pas Ymir.
5. **L'éditeur de retouches dans le moteur** (`EDIT=1`), couche A seulement, journal par `BUP_Write`.
6. **Export Blender de la couche A** — même périmètre, même journal, pour ceux qui préfèrent la
   souris à la manette. L'un des deux suffit ; faire les deux n'a de sens que si le format de
   journal est commun.
7. **Le chemin Build**, dans cet ordre, et c'est le chemin des niveaux **originaux** :
   a. le point de départ depuis l'en-tête Build (~20 lignes, ~46 % des échecs) ;
   b. dessiner sur la grille 8 + `quantize --tol-u 0` (aucun code : une consigne d'auteur, qui
      supprime la classe des dégâts de collage) ;
   c. rebrancher les sprites : `convex.py` les affecte déjà à leur morceau, `geom3d.py` ne les lit
      pas — 150 à 250 lignes pour la couche d'objets ;
   d. seulement ensuite, le pontage de trous sur les cartes Duke héritées.

## 9. Ce qui reste ouvert

* **Refaire les captures console sous NDEBUG**, point de vue variable. Toute la colonne « coût »
  repose dessus et la loi actuelle majore de ~1,5×.
* **L'origine verticale des tuiles.** Le rangement ligne par ligne, x le plus rapide, pas de 64, est
  prouvé deux fois (PIC.C:556-565, :828) ; que la première ligne soit celle du **haut** est la
  convention de caractère du VDP1 et n'a pas été recoupée avec les manuels de
  `../saturn-refs/manuals/`. Si une texture sort à l'envers dans Blender, c'est là qu'il faut
  regarder.
* **L'espace colorimétrique** des images produites par l'extension : les valeurs sont écrites
  telles quelles en sRGB. Si le rendu paraît délavé, basculer l'image en *Non-Color* le corrige ;
  ce n'est pas tranché.
* **Les objets non interprétés** : l'extension ne pose que les enregistrements d'au moins cinq
  shorts dont le point tombe dans la boîte du niveau, et **compte** les autres (jusqu'à 196 sur
  E1M6). Une table de tailles par type, reconstructible depuis les écarts de `firstParam`, les
  récupérerait tous.
