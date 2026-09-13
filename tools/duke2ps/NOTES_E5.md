# E5 + E6 disque — E1L1 testable sur console (2026-09-12)

> **E4 est fait** : le niveau utilise maintenant les vraies textures de Duke
> (`NOTES_E4.md`). La « boîte grise » décrite ci-dessous reste disponible avec
> `assemble.py --tuiles ""` et sert de repli si les tuiles posent problème.

`tools\duke2ps\assemble.py`. Non suivi, jamais commité.

## Pourquoi une boîte grise

E4 (extraction des tuiles Duke depuis les `.ART`, palette, ciel) n'est pas fait. Mais il n'est pas
nécessaire pour juger ce que le jalon M1 demande : **la géométrie d'E1L1 est-elle marchable dans le
moteur ?** On prend donc un niveau retail comme **donneur** et on ne remplace que son bloc géométrie ;
ses tuiles, sa palette, son ciel, ses sons et ses séquences sont gardés tels quels. Nos 64 picnums
Build sont répartis à tour de rôle sur les 48 tuiles de flags 0x32 du donneur (KILENTRY).

Le niveau n'a donc **pas** les textures de Duke — il aura l'air d'un patchwork égyptien. Il a en
revanche exactement la géométrie d'E1L1.

## Ce qui rend l'opération légitime (relu dans le moteur, pas supposé)

- `level_texture[i] += tileBase` pour `i` **impair** et `level_face[i].tile += tileBase`
  (`LEVEL.C:70-72`) : les indices de tuile sont **locaux au niveau**, donc réutiliser ceux du donneur
  est correct. Ce `i += 2` confirme au passage la disposition `[motif, tuile]` par cellule.
- Un objet `OT_PLAYER` (type **13**, `SLEVEL.H:22-27`) consomme 5 shorts gros-boutistes :
  secteur, x, y, z, angle (`OBJECT.C:201-204` puis `suckSpriteParams`, `OBJECT.C:183-194`).
- `assert(level_object[o].firstParam == objectPPos)` (`OBJECT.C:211`) : les `firstParam` doivent être
  les offsets cumulés dans `objectParams`. Avec un seul objet, 0.
- `level_cutPlane` n'est indexé que si les **deux** secteurs portent `SECFLAG_CUTSORT`
  (`WALLS.C:2181-2191`) : nos secteurs ont `flags = 0`, donc 0 secteur de découpe est sûr.
- Taille du bloc niveau < 900 000 (`LEVEL.C:40-41`) **et** demande mémoire totale sous la limite
  du tas : chaque part est allouée séparément (`LOADPART`) et `mem_malloc` fait `assert(r)`
  quand il ne reste rien (`UTIL.C:392`). C'est ce second budget qui a manqué au premier disque.

## Pourquoi le créneau TOMB

`BUP.C:194-197` : une nouvelle partie fait `currentState.currentLevel = 3` et ne déverrouille que le
niveau 3. Or `BIGMAP.C:48` donne `/* 3*/ "+TOMB.LEV"`. **TOMB est le premier niveau de PowerSlave** :
commencer une nouvelle partie tombe directement dans notre E1L1 converti, sans manipulation.

## Deux défauts du PREMIER disque, trouvés sur console

**1. Écran noir avec du texte doré = `assert` du moteur.** C'était `UTIL.C:392`, `assert(r)` dans
`mem_malloc` : **plus de mémoire**. Le plan listait pourtant le critère
(« demande ≤ 1 250 000 ») et je n'avais vérifié que `size < 900 000`.

La **demande** = bloc niveau + palettes + tuiles + séquences (MESURE sur les 24 niveaux retail) :

| | bloc niveau | tuiles | demande |
|---|---|---|---|
| TOMB retail | 295 907 | 826 447 | 1 143 922 |
| nous, donneur TOMB | 589 641 | 826 447 | **1 437 656** |
| SHRINE (le plus serré du retail) | 578 964 | 720 713 | 1 343 773 |
| **nous, donneur KILENTRY** | 589 641 | **306 251** | **915 746** |

Notre géométrie pèse 293 734 o de plus que celle de TOMB, et TOMB a le deuxième plus gros jeu de
tuiles du jeu : on dépassait de 93 883 o le niveau retail le plus serré. **Le donneur fournit les
tuiles, pas le créneau** — le fichier s'appelle `TOMB.LEV` quoi qu'il arrive. Le plan désignait
KILENTRY (le plus léger, demande 497 290) ; je ne l'avais pas suivi.

**2. Le « sol + 48 » du plan est faux.** MESURE : dans les **23** niveaux retail, le `y` de l'objet
`OT_PLAYER` vaut **exactement** le `floorLevel` de son secteur, écart 0 partout. C'est le moteur qui
relève la caméra (elle flotte à R+8 au-dessus du contact, `SPRITE.C:642-655`). Corrigé.

## Trois défauts du DEUXIÈME disque (monde noir, colonnes de sprites répétées)

Le jeu tournait, HUD et arme affichés, mais le monde était noir avec quatre colonnes de figures
répétées. Trois causes, toutes vérifiables hors console :

**1. Les portails doivent être EN TÊTE de la liste des murs de chaque secteur.** `findDoorways`
parcourt `firstWall..lastWall` et fait `if (theWall->nextSector == -1) return;`
(`WALLS.C:1740-1743`, commentaire « doorways are sorted to be first in the list »). J'émettais les
murs dans l'ordre des arêtes — plein, portail, plein, portail… — donc le moteur sortait au premier
mur et **ne trouvait aucun portail** : la visibilité ne quittait jamais le secteur de départ.
MESURE : **0 des 8 211 secteurs retail** viole cette règle ; **361 de nos 440** la violaient.
Cela explique le noir : au niveau des yeux, le secteur de départ n'a que des portails (invisibles) ;
les seuls murs pleins sont les linteaux, à 344 u au-dessus du sol.

**2. Les quatre colonnes de sprites étaient ces linteaux**, larges de 2 tuiles et hauts de 53, peints
avec de mauvaises tuiles. MESURE : la géométrie des 23 niveaux retail n'utilise que des tuiles de
flags **exactement 0x32** (64×64, 16 bpp, palette). Mon filtre « 64×64 et 16 bpp » laissait passer les
**0x72**, les mêmes en **RLE** — des sprites d'objets. D'où les figures répétées.

**3. Un ciel `INVISIBLE` n'est jamais dessiné.** Le test `flags & WALLFLAG_INVISIBLE -> continue`
(`WALLS.C:1616`) passe **avant** la branche parallax (`WALLS.C:1666`). MESURE : les **2 844** murs
`PARALLAX` du retail valent tous exactement **320** (`BLOCKED | PARALLAX`), sans face et sans
`INVISIBLE`. J'émettais 322.

Les trois sont maintenant des critères, côté générateur et côté vérificateur indépendant.

## Résultat (MESURE)

```
fichier          1 092 271 o
bloc niveau        589 641 o  (limite 900 000, LEVEL.C:41)
demande            915 746 o  (cible 1 250 000 ; plafond retail mesure 1 343 769 = SHRINE)
sha1             836fb09aae7292b8792d634f5934ddf67c2e56b5  (avec les textures Duke)
relecture        identique (lev_io.model_from_bytes -> diff vide)
lev_write        aucun problème signalé
départ           secteur 319, X 1151, Y −64, Z −5199 (= floorLevel)
ISO              build\slavedriver.iso, 168 665 088 o
                 TOMB.LEV extrait de l'ISO : même sha1
```

Le morceau 319 est bien celui que `convex.py` avait désigné comme contenant le point de départ.

## Reproduire

```
python tools\duke2ps\geom3d.py
python tools\duke2ps\assemble.py            # -> build\duke2ps\e5\TOMB.LEV
copy /Y build\duke2ps\e5\TOMB.LEV cd\TOMB.LEV
make iso SRL_DIR=<chemin vers un SaturnRingLib>
```

Sauvegardes : `refs\build\duke2ps\TOMB_retail.LEV` (l'original, sha1 `247ea8e8e48d…`) et
`refs\build\duke2ps\TOMB_e1l1.LEV` (le nôtre). Pour revenir au jeu normal :
`copy /Y refs\build\duke2ps\TOMB_retail.LEV cd\TOMB.LEV` puis refaire l'ISO.

## Ce qu'il faut lancer et regarder

Lancer `build\slavedriver.iso`, **Nouvelle partie**.

| ce que vous voyez | ce que ça veut dire |
|---|---|
| Le jeu démarre et vous êtes debout dans une salle, pas dans la tombe égyptienne | Le niveau converti est chargé. E5/E6 sont bons. |
| Écran noir avec des mots dorés qui bougent (`UTIL.C`, un numéro de ligne) | C'est l'écran d'`assert` du moteur. Me donner le fichier et la ligne : `UTIL.C:392` = plus de mémoire, autre chose = autre cause. |
| Écran noir ou retour au menu au chargement | Le chargeur a refusé le fichier. Me le dire, c'est E5. |
| Vous tombez sans fin / vous êtes dans le noir dès le départ | Le point de départ est hors du secteur, ou son sol est mal placé. C'est le calcul de départ d'E5. |
| Les murs forment des pièces fermées, on peut marcher de salle en salle | **C'est le résultat attendu de M1.** La géométrie d'E1L1 tient debout dans le moteur. |
| Des trous dans le sol ou le plafond, par où on voit « à travers » le niveau | Le pavage des sols laisse passer. Ce serait un défaut qu'aucun de mes 12 tests n'aurait vu. |
| Des murs qui manquent, on voit à travers une paroi | Mur sauté par le budget du slave, ou mur émis invisible à tort. |
| Des murs « tordus » / la texture glisse en diagonale sur un mur en pente | Un trapèze marqué parallélogramme. Je pensais les avoir tous attrapés (192 passés en faces). |
| On traverse un mur en marchant | Drapeau `BLOCKED` manquant sur un mur plein. |
| On reste bloqué dans une porte basse | Passage sous 90 u : connu et accepté pour M1 (7 passages < 47 u, 46 < 90 u). |
| Les textures sont celles de Duke mais à la mauvaise échelle / étirées | Attendu : `xrepeat`/`yrepeat`/`panning` de Build sont ignorés, une texture couvre exactement une cellule de 64 u. |
| Une texture est un damier de couleurs fausses | La palette ou l'échange 0↔255 est en cause. C'est E4. |
| Des trous transparents dans un mur | L'index 0 est la transparence : une texture Duke qui utilise l'index 255 se troue. C'est E4. |
| Aucun ennemi, aucune arme au sol, aucune sortie | **Normal** : le niveau ne contient qu'un seul objet, le joueur. On ne peut pas le finir. |
| Le puits de départ est vertigineusement haut | Attendu mais perfectible : le plafond y est à 3 640 u (la valeur de Duke). Lobotomy avait descendu ce puits à 1 824 u. Voir « reste à faire ». |
| Ralentissements marqués en regardant une grande salle | Budget par image. Le plafond de découpe est à 256 cellules, contre 242 pour le plus gros mur retail. |

## Débug du moteur : ce qui existe

`cheatsEnabled` (`BUP.C:78-85`) s'allume si la RAM de sauvegarde contient un fichier nommé
**`POWERCHEAT!`** (`BUP.C:25`). Quand il est allumé :

| où | quoi |
|---|---|
| `MAP.C:256` | la carte automatique affiche `x:… y:… sector:…` — **position et secteur de la caméra** |
| `SRUINS.C:1022` | `camera->vel.y = F(3)` en permanence sauf si A ou C est tenu ⇒ **on vole** |
| `BIGMAP.C:495` | X sur la carte du monde : tous les niveaux déverrouillés + inventaire complet |
| `MENU.C:1371` | X / Y dans le menu : vie, munitions, clés, objets |

`debugFlag` (basculé par l'action PUSH, `SRUINS.C:993`) n'alimente que `dumpProfileData()` et
`dPrint`, tous deux compilés hors de ce build (`UTIL.H:147-153` les réserve à `PSYQ`, et
`PROFILE.H:15` fait de `pushProfile` un no-op). Il ne sert donc à rien ici.

Voler + l'affichage `x/y/sector` est exactement l'outil qu'il faut pour juger la géométrie : on peut
monter dans le puits, survoler la ville et vérifier chaque salle.

## Reste à faire / HYPOTHÈSE

- **Le puits de départ n'est pas plafonné.** Le secteur Build 259 n'a pas le bit parallax
  (`ceilingstat` bit 0 = 0), donc la règle de ciel d'E1 ne l'a pas touché : plafond à Y 3 640, soit
  3 704 u de hauteur, là où HOLYWOOD mesure 1 824. Le Y maximal du niveau est 3 897.
- Motifs de coin tous à 0 (aucun retournement de tuile) et lumière issue d'une conversion linéaire
  de `shade` non validée.
- Aucun objet : ni sortie, ni clé, ni ennemi, ni arme. Le niveau ne se termine pas.
- Les sons et séquences sont ceux du donneur ; rien ne les référence dans notre géométrie.
