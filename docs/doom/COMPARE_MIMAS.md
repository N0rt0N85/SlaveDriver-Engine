# Comparer Mimas et Aguzzino sur E1M1 — protocole (2026-09-14)

Deux disques, la même carte, les mêmes trois postes, les mêmes gestes. Tout se lit sur console (ou
sur l'émulateur du propriétaire). Les chiffres vont dans le tableau du §3,
le ressenti dans la liste du §4.

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

Lecture : à `polys` égal, Aguzzino doit coûter 1,6 à 2,5 fois moins que Mimas par commande
(39 contre 64 µs mesurés) ; si `draw` dépasse `calc` sur un poste, le VDP1 est le pôle long (fill des
sprites proches) et ce n'est plus une question de CPU.

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
