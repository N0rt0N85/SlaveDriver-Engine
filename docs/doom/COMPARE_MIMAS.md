# Comparer Mimas et Aguzzino sur E1M1 — protocole (2026-09-14)

Deux disques, la même carte, les mêmes trois postes, les mêmes gestes. Tout se lit sur console (ou
sur l'émulateur du propriétaire). Les chiffres vont dans le tableau du §3,
le ressenti dans la liste du §4.

**La référence de perf n'est pas Mimas** (la branche psw-world tourne à ~12 fps, MST ~80 ms sur ces
trois postes) : c'est le **budget du moteur**, 33,3 ms par image pour 30 fps (plafond structurel), et
sa loi de coût mesurée, 14,9 ms + 39,2 µs par cellule (build ASSERT+STATUSTEXT) — soit 470 cellules
au plus à 30 fps. Mimas sert à comparer les **règles et la sensation**, pas les millisecondes.

## 1. Les deux overlays

| | Mimas (`dg_saturn.cxx`, ligne 1) | Aguzzino (STATUSTEXT, `SRUINS.C:2218-2261`) |
|---|---|---|
| cadence | `N.Nfps aN.N` = instantané, moyenne | `fps:A B` = `60/framesElapsed` (instantané, quantifié 30/20/15/12), cible du pacing |
| temps CPU | `MST` = maître (ms) | `time:a b:c` en **lignes de balayage** (1 = 63,56 µs) : `a` = calc moyenné sur 2 frames, `b` = attente VDP1 de la frame précédente, `c − b` = calc courant |
| charge | `Bp/em/re/pr` (ligne 4) | `polys:N` = cellules murs+sols+plafonds (sprites non comptés), `cx/cy` |
| mémoire | — | `mem:Ak+Bk=Ck` (LWRAM, HWRAM) |
| caches | — | `used[i]` / `vswaps[i]` (build non NDEBUG) |
| profil | `TIC th… ` | L+R+Y (ou A+B+C) : arbre `PROFILE.C` en ms |

Conversion Aguzzino : `calc_ms = (c − b) × 0,0636`, `draw_ms = b × 0,0636`. Le plancher structurel du
moteur en jeu est 30 fps (2 fields).

## 2. Les trois postes (E1M1, skill UV, 1 joueur)

| poste | où se placer | regarder vers | pourquoi |
|---|---|---|---|
| **P1 couloir** | salle de départ, dos au mur du fond, 2 pas en avant | la porte du zigzag | scène fermée : 200-300 cellules, aucun monstre |
| **P2 cour** | cour extérieure (nukage), au bord de la piscine | l'arche vers le bâtiment, ciel à l'écran | scène ouverte : ciel, sol, 400-600 cellules, 3-6 zombies |
| **P3 salle d'ordinateurs** | pièce aux ordinateurs (fin du zigzag), à l'entrée | le fond de la salle | 10+ sprites, colonnes, la scène la plus chère de la carte |

À chaque poste : rester immobile 3 s (relever), puis tourner 360° (relever le pire), puis tirer 3 balles
de pistolet (relever la réaction).

## 3. Tableau à remplir

| poste | Mimas fps (inst./moy.) | Mimas MST ms | Aguzzino fps A | Aguzzino calc ms | Aguzzino draw ms | Aguzzino polys | pire fps sur 360° (M / A) |
|---|---|---|---|---|---|---|---|
| P1 | | | | | | | |
| P2 | | | | | | | |
| P3 | | | | | | | |

Lecture : comparer `calc` à la loi du moteur (14,9 ms + 39,2 µs × `polys`) — un écart au-dessus est
du travail du runtime Doom, pas de la géométrie ; si `draw` dépasse `calc` sur un poste, le VDP1 est
le pôle long (fill des sprites proches) et ce n'est plus une question de CPU.

**Première mesure (14-09, build debug + overlay, avant l'allègement des acteurs au repos)** :

| poste | polys | fps | calc (ms) | loi (ms) | écart | arbre L+R+Y |
|---|---|---|---|---|---|---|
| P1 | 321 | 30 | 24,7 | 27,5 | −2,8 | Walls 12,5 (FindDoorways 3,6) ; Motion 8,9 dont Run Objects > Collide Sprite 7,0 |
| P2 | 371 | 20 | 34,1 | 29,4 | **+4,7** | Walls 16,3 (FindDoorways 4,7) ; Motion 10,6 dont Collide Sprite 7,9 |
| P3 | 587 | 20 | 37,8 | 37,9 | 0 | Walls 20,7 (FindDoorways 5,7) ; Motion 10,0 dont Collide Sprite 7,3 |

`b` (attente VDP1) = 9 lignes partout : le CPU est le pôle long. P2 rate 30 fps de 0,8 ms (le runtime),
P3 de 4,5 ms (587 cellules > 470 : la géométrie). Mimas-psw au même endroit : 12,0 / 12,6 / 12,4 fps.
(Arbre de cette mesure lu avec l'ancienne constante du profileur : ses ms sont 6,6 % trop basses.)

**Deuxième mesure (14-09, disque NDEBUG + overlay, `bcee814` : acteurs au repos sans collision)** —
`calc` en lignes (`c − b` = frame N−1, `2a − (c − b)` = frame N−2), arbre converti à l'horloge du mode
320 (× 1,066) :

| poste | polys | fps | calc N−1 / N−2 | calc (ms) | loi ASSERT (ms) | Motion | Walls (Find Doorways) | Overlay |
|---|---|---|---|---|---|---|---|---|
| P1 | 323 | 30 | 332 / 328 | 21,1 | 27,6 | 0,9 | 15,7 (3,8) | 3,8 |
| P2 | 392 | **30** | 458 / 468 | 29,1 | 30,3 | 2,6 | 20,8 (5,4) | 3,8 |
| P3 | 560 | 20 | 562 / 506 | 35,7 / 32,2 | 36,9 | 5,6 | 22,3 (6,3) | 3,8 |

- **Motion −8 ms** à P1 et P2 : P2 passe de 20 à 30 fps.
- **Walls monte de 2 à 3 ms dans l'arbre sans allonger `calc`** : c'est le partage maître/esclave.
  `drawWalls` donne `slaveSize + 1` secteurs à l'esclave, dessine les autres, puis le maître fait
  Motion pendant que l'esclave travaille ; `drawWallsFinish` attend l'esclave et bouge `slaveSize` de
  ±1 par frame pour que les deux finissent ensemble (`WALLS.C:2417-2459`). Avec 9 ms de Motion,
  l'esclave prenait presque tout ; avec 1 ms, le maître reprend des secteurs à son compte.
- **P3 alterne 506 et 562 lignes** : à 20 fps une frame joue 1 ou 2 tics (21/12), et le moteur ne
  revient à 30 fps qu'après **10 frames consécutives sous 2 fields** (`smoothVTime`,
  `SRUINS.C:2277-2301`) — c'est la frame à 2 tics qui doit tenir, pas la moyenne.
- **L'overlay coûte 3,8 ms, dont ~2,8 pour l'arbre** (~12 µs par caractère, une commande VDP1
  chacun) : à P3 c'est la différence entre les deux paliers. Relever `fps`/`time` **arbre éteint**,
  l'allumer ensuite pour la répartition.

## 4. Ressenti, point par point (oui / non / différent)

- **Monstres** : un zombie voit le joueur et crie ; il avance à vitesse Doom (~70 u/s, un couloir de
  512 u en 7 s) ; il contourne un pilier ; il ouvre la porte du secteur 4 ; il tire et la balle fait
  un impact au mur ; il a une chance de douleur visible (~78 % pour POSS) ; il meurt en 2-3 balles de
  pistolet et **reste au sol** ; un imp lance une boule de feu qui explose ; un baril explose et blesse
  autour ; deux monstres se battent s'ils se touchent.
- **HUD** : barre STBAR collée en bas, chiffres de santé/munitions à la bonne place, visage qui change
  par palier de 20 hp et grimace à la douleur, armes possédées en jaune, message « picked up… ».
- **Armes** : le pistolet monte en ~0,5 s, tire en rafale à 2,5 coups/s, flash d'une frame ; le fusil
  ramassé donne 8 cartouches et 7 plombs par coup ; le poing ; changement d'arme par bouton ; munitions
  refusées au maximum.
- **Sons** : coup de feu, cri de vue, douleur, mort, porte qui s'ouvre et se ferme, ramassage, switch,
  baril ; un son ne coupe pas un autre son important (mort du joueur).
- **Portes et sortie** : les 4 portes s'ouvrent au bouton et se referment après ~4 s ; l'ascenseur de
  la cour redescend et remonte ; l'interrupteur de sortie termine le niveau.
- **Ce qui manque encore (attendu)** : voir `E1M1_DISC.md` (liste des TODO du disque courant).

## 5. Ce qu'on compare vraiment

Mimas dessine la carte avec le renderer Doom (colonnes, visplanes, sprites logiciels) et fait tourner le
playsim Doom bit-exact. Aguzzino dessine des cellules VDP1 cuites hors ligne et fait tourner les règles
Doom (tables exactes, verbes ré-hébergés) sur les services du moteur. Les écarts de **règles** vont
dans `E1M1_DISC.md` (TODO) ; les écarts de **sensation** se jugent ici, côte à côte.
