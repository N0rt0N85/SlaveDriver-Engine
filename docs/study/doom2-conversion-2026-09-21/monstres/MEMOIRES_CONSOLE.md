# Mémoires hors work RAM : ce que la console a prouvé (2026-09-22)

Suite de `M2_MEMOIRES_CACHEES.md`, qui les avait chiffrées **sur le papier**. Ici, rien que ce qu'une
Saturn a montré.

Conditions de mesure :
- disque sonde `HWPROBE.C` (worktree `../SlaveDriver-Engine-hwprobe`, branche locale `hwprobe`,
  `make HWPROBE=1 STATUSTEXT=1 NDEBUG=1 PARAMS=params/doom.cfg`) ;
- Doom E1M1 en solo, Saturn avec ODE ;
- deux sessions console d'environ 800 images chacune, plus Ymir pour comparer.

**La musique CD-DA joue depuis le disque CloneCD** (§4), et la dernière mesure de la série est faite :
**relire la réserve pendant la musique ne coûte pas plus cher et ne casse rien** (§2, « pendant le
CD-DA »).

## 1. Le bilan

| mémoire | octets prouvés | lecture sur console | a tenu | reste à prouver |
|---|---|---|---|---|
| **tampon du bloc CD**, partition 21 | **393 216** (192 × 2 048)<br>**451 584** en secteurs de 2 352 o, relus à l'identique | **≈ 0,87 ms par secteur de 2 Ko** : commande 80-93 µs (max 169), transfert 730-752 µs, fin 40 µs<br>écriture des 192 secteurs : 66-83 ms | lectures GFS (STATIC.DAT), chargement complet d'E1M1 (7 s), plus de 3 900 relectures en jeu dont 932 **pendant la musique**, **0 erreur** | menus, films, changement de niveau ; SCU-DMA au lieu du CPU ; le son à l'oreille à 16 relectures par image |
| **marge hors écran des 2 framebuffers VDP1**, colonnes 336-511, lignes 0-255 | **90 112 par framebuffer** : 90 112 o si les deux portent les mêmes données (miroir), **180 224** s'ils portent des données différentes (alterné) | **≈ 3,75 ms pour 2 816 o**, comparaison au motif comprise (≈ 0,75 Mo/s) | environ 800 images × 2 sessions, les deux framebuffers, 0 faute, jamais retamponnés | les lignes 224-255 des colonnes 0-335 (M2 comptait 107 904 o) ; menus, films, intermission, écran partagé ; ce que la lecture coûte au tracé du VDP1 |
| **VRAM VDP2 B0+B1** | **262 144** | **1,87-2,25 ms par 4 Ko** (≈ 2 Mo/s, pendant l'affichage)<br>remplissage des 256 Ko en **26 ms** | environ 800 blocs de 4 Ko relus (la VRAM entière environ 12 fois), 0 faute | **écran partagé : réécrite (MPSKY)** ; menus et autres écrans ; `loadVDP2Sprites` la remet à zéro à chaque niveau |
| RAM son | non sondée | — | — | Doom 1 : 49-126 Ko libres selon la carte (`sound_ram_budget.py`) ; Doom 2 : pleine |
| VRAM VDP2 A0, en V-blank | non sondée (126 976 o sur le papier) | — | — | tout |
| cartouche | non sondée | — | — | une amélioration, jamais une exigence |

**Prouvé sans cartouche** : 393 216 + 262 144 + 90 112 = **745 472 o (728 Ko)**, en plus de la
work RAM. Plafond de ce qui est prouvé : 451 584 + 262 144 + 180 224 = **893 952 o (873 Ko)**,
mais seulement :
- en secteurs de 2 352 o, qui imposent de changer la longueur de secteur autour de chaque accès :
  elle vaut pour tout le bloc CD, et GFS lit en 2 048 ;
- et avec les framebuffers en alterné, où chaque moitié n'est lisible qu'une image sur deux.

**En écran partagé, la VRAM B N'EST PAS libre** : photo console du 2026-09-22 en 2 joueurs, `vb:ok:18
ko:55`, soit 55 blocs sur 73 réécrits depuis le chargement, et 2 866 µs par 4 Ko au lieu de 2 000 µs.
Le ciel du mode partagé (MPSKY : RBG0, NBG0, NBG1) s'y installe, et la rotation prend des cycles au
CPU. Dans la même partie, la marge FB (`ok:71 ko:0`) et le bloc CD (`ok:73 ko:0`, transfert 810 µs)
ont tenu. La VRAM B n'est donc disponible **qu'en solo**, ou en écran partagé sans ciel partagé.

**En solo, la VRAM B est libre** parce que l'arme reste en tuiles VDP1 : le propriétaire a refusé la D1 (arme
sur la feuille VDP2) le 2026-09-21.

Coût d'une lecture, ramené à l'octet : **bloc CD ≈ 0,42 µs, VRAM B ≈ 0,5 µs, marge FB ≈ 1,3 µs**
(comparaison comprise). Le bloc CD est le plus rapide des trois, mais il se sert par commandes.

## 2. Les règles d'usage, apprises par les sondes

**Tampon du bloc CD**
- **Partition 21, pas 23** : GFS prête le sélecteur 23 au système de fichiers du bloc CD
  (GFCD_GetFileInfo à GFS_Init). Il faut aussi marquer la partition et le filtre comme pris dans
  le `GfsMng` (`fs_reserveCdPart`, FILE.C du worktree), sinon GFS les réutilise.
- **Attendre le bit HIRQ avant ET après chaque commande** : ESEL pour les commandes de sélecteur,
  EHST pour Get, Put et Del. Sans ce bit, `doCmdRsp` de la SBL rend -1 **sans rien envoyer**.
  Ymir répond sans délai et ne montre jamais ce refus : le premier disque sonde, faux, donnait sous
  Ymir le même écran que le bon.
- Get **ne détruit pas** le secteur. La réserve ne se remplit qu'une fois, au démarrage.
- GFS a travaillé avec **8 secteurs libres** seulement : STATIC.DAT se lit en 2 166 ms, réserve
  vide ou pleine.
- **Pendant le CD-DA, tout tient** (mesuré le 2026-09-22, disque CloneCD) : 932 relectures à 1, 4 et
  16 par image, **0 ratée, 0 erreur** ; commande ~85 µs, transfert 730-740 µs, fin 40 µs, soit le
  **même prix que sans musique** ; le lecteur reste en lecture sur la piste 2 et sa position avance ;
  `libre` = 8 et le bit BFUL retombe. Reste à juger à l'oreille à 16 relectures par image.
- **Sans musique, le tampon est plein** : `libre:0` et BFUL levé, à cause de la lecture anticipée de
  GFS, pas de la musique. Ma crainte d'une réserve incompatible avec le CD-DA (ST-38 p.10 : un tampon
  plein met le lecteur en pause) est **réfutée par la mesure**.
- Budget par image : 4 relectures (≈ 3,5 ms) ont tenu 15 fps dans la scène de test ; 16 (≈ 14 ms)
  l'ont fait tomber à 12 fps. **La banque sert quelques secteurs par image, pas du vrac.**

**Marge des framebuffers VDP1**
- Le CPU ne voit que le framebuffer **en cours de tracé**, A et B en alternance. La sonde reconnaît
  chacun à un identifiant posé dans la marge.
- Le VDP1 ne dessine pas au-delà de la colonne 319 (clip système), et l'effacement s'arrête à 319.
  La sonde commence à 336 pour garder 16 colonnes de marge à l'arrondi de l'effacement.
- La lecture est lente : **à réserver aux données froides**.

**VRAM VDP2 B**
- Lecture CPU seulement : le SCU-DMA ne lit pas le VDP2.
- À **remplir après `loadVDP2Sprites`** à chaque chargement de niveau (26 ms).
- NBG0 à la priorité 0 pour que le contenu ne s'affiche jamais.

## 3. Ymir contre console

| mesure | Ymir | console |
|---|---|---|
| Get : commande | 38-46 µs | 80-93 µs |
| Get : transfert 2 Ko | 920-934 µs | 730-752 µs |
| Get : fin | 30-39 µs | 40-42 µs |
| Put des 192 secteurs | 183 ms | 66-83 ms |
| STATIC.DAT, réserve vide / pleine | 2 116-2 133 ms | 2 150-2 166 ms |
| secteurs libres en jeu | 8 | **0**, BFUL levé |
| refus silencieux de la SBL | jamais visible | réel (premier disque) |

## 4. Trouvé en chemin : les disques avec musique

- **Démarrage** : le premier disque avec piste audio a été refusé (« CD non reconnu »). Les
  suivants démarrent, avec les 2 s de secteurs MODE1 vides en fin de piste de données que porte
  la piste 01 du disque retail de Powerslave. `tools/iso2bin.py` du worktree les écrit
  maintenant. Qu'elles aient été la cause n'est **pas établi** : un fichier de piste manquait
  peut-être.
- **Musique muette sur console** : la table des pistes lue par la console ne contient **que la
  piste 1**.
  - Console : `toc 41000096 ffffffff lo01002066` ; la fin du disque est au dernier secteur du .bin
    de données.
  - Ymir, même disque : `41000096 010020fc lo010070dd`.
  - La console ne voit pas le second fichier du .cue, et la lecture de la piste 2 ne va nulle part
    (le lecteur reste en pause sur la piste 1, FAD 150). **Le moteur n'est pas en cause.**
  - Phoebe (l'ODE du joueur), qui est lancé par le .cue et joue la musique des jeux du commerce en
    « un .cue + un .bin », n'a vu la piste 2 dans aucune de nos quatre écritures : Redump en plusieurs
    fichiers, un seul .bin avec `INDEX 00`, `PREGAP`, `INDEX 01` seul. La fin du disque suit
    toujours la taille du .bin, sans tenir compte du `PREGAP`.
  - Test suivant : le disque retail de Powerslave rangé exactement comme les nôtres
    (`materiel/PowerslaveUn`). S'il joue sa musique, c'est notre piste de données qui gêne Phoebe ;
    sinon, c'est notre écriture du .cue.
  - Résultat : **PowerslaveUn est muet lui aussi** : nos données sont hors de cause, c'est la forme du
    .cue et du .bin que Phoebe refuse.
  - **RÉSOLU le 2026-09-22 : il faut du CloneCD.** La carte du joueur est une carte RMENU, et ses 529
    jeux du commerce sont tous en `.ccd`/`.img`/`.sub` (le `.cue` posé à côté désigne un `.bin`
    absent : il ne sert à rien). Avec un `.cue`, Phoebe ne monte que la piste de données, quelle que
    soit l'écriture. Le disque sonde en CloneCD — `tools/ccd.py` du worktree, validé en reconstruisant
    le `.ccd` et le `.sub` de 3D Lemmings (EU) **octet pour octet** — **joue sa musique sur console**.
  - Conséquence : tout disque Aguzzino avec musique doit sortir en CloneCD.

## 5. Les photos, une ligne par photo

Session 1 (disques AguzzinoHwProbeMono et Pg, sans musique ; « relectures/image » = réglage L+R+C) :

| # | relectures/image | relectures bonnes/ratées | fps | commande min/moy/max · transfert · fin (µs) | FB bonnes/ratées · µs | VRAM B bonnes/ratées · µs/4 Ko |
|---|---|---|---|---|---|---|
| 2 | 1 | 109/0 | 15 | 91/90/103 · 730 · 40 | 107/0 · 3751 | 109/0 · 1941 |
| 3 | 4 | 400/0 | 15 | 86/80/108 · 750 · 40 | 252/0 · 3776 | 254/0 · 2172 |
| 4 | 16 | 1024/0 | 12 | 86/80/167 · 740 · 40 | 348/0 · 3761 | 350/0 · 2249 |
| 5 | 0 | 0/0 | 15 | — | 453/0 · 3671 | 455/0 · 2115 |
| 6 | 1 | 112/0 | 15 | 90/90/113 · 730 · 40 | 603/0 · 3764 | 605/0 · 1867 |
| 7 | 4 | 356/0 | 15 | 86/80/129 · 740 · 40 | 725/0 · 3759 | 727/0 · 2170 |
| 8 | 16 | 848/0 | 12 | 86/80/122 · 740 · 40 | 805/0 · 3748 | 807/0 · 1929 |

La moyenne sous le minimum (80 < 86) est un artefact de la sonde : elle somme en dizaines de µs
tronquées.

Session 2 (disque AguzzinoHwProbe3, dans l'ordre de prise d'après le compteur vb) :

| vb | m | réponse CdPlay / nombre | con | hq | état du lecteur | libre | fps |
|---|---|---|---|---|---|---|---|
| 83 | 1 | 0 / 2 | 0 | 0fdd | pas encore lu | 0 | 15 |
| 196 | 2 | 0 / 2 | 0 | 0fdd | 21 (pause), piste 1.1, FAD 158 | 0 | 15 |
| 275 | 3 | 0 / 2 | 0 | 0fdd | idem, non relu | 0 | 12 |
| 401 | 4 | 0 / 3 | 0 | 0fdd | pause, piste 1.1, FAD 150 | 0 | 15 |
| 485 | 5 | 0 / 4 | 255 | 0fdd | pause, piste 1.1, FAD 150 | 0 | 15 |
| 577 | 0 | 0 / 4 | 255 | 0fdd | pause, piste 1.1, FAD 150 | 0 | 15 |
| 660 | 1 | 0 / 4 | 255 | 0fdd | idem | 0 | 15 |
| 725 | 2 | 0 / 4 | 255 | 0fdd | idem | 0 | 15 |
| 794 | 3 | 0 / 4 | 255 | 0fdd | idem | 0 | 12 |

Sur toutes les lignes : `toc 41000096 ffffffff lo01002066`, réserve 192/192, 0 relecture ratée,
FB et VRAM B sans faute.
