# Disque Doom E1M1 (Aguzzino) — état au 2026-09-14

Premier disque Doom pour SlaveDriver : E1M1, skill UV, 1 joueur, overlay STATUSTEXT. **Il n'a jamais tourné** :
tout ce qui suit est vérifié sur PC (convertisseur, vérificateurs, build, lien).
Le premier boot est celui du propriétaire. Le protocole de comparaison avec Mimas est dans `COMPARE_MIMAS.md`.

## Reconstruire (depuis la racine du dépôt, Git Bash)

```sh
python tools/doom2ps/make_e1m1.py && powershell -ExecutionPolicy Bypass -File build.ps1 PARAMS=params/doom.cfg STATUSTEXT=1 CDDIR=cd_doom iso
```

- `make_e1m1.py` lit `../Mimas/cd/data/DOOM1.WAD` et écrit `cd_doom/` : `E1M1.LEV`, `STATIC.DAT`, plus des copies de
  `cd/INITLOAD.DAT` et `cd/INTRO.PCS`. Il écrit aussi `build/doom/doom_art.h`. Il sort `TOUT VERT` (rc 0) sinon rien ne va plus loin.
- **ISO : `build/stext/doom/slavedriver.iso`** (3 237 888 o, IP.BIN générique SRL). Pour une version sans overlay,
  retirer `STATUSTEXT=1` ; l'image est alors `build/doom/slavedriver.iso`.
- Contenu du disque (vérifié par `xorriso -lsl`) :

  | fichier | taille (o) |
  |---|---|
  | `0.BIN` (INIT) | 117 772 |
  | `MAIN.BIN` | 373 852 |
  | `STATIC.DAT` | 649 443 |
  | `E1M1.LEV` | 1 333 464 |
  | `INITLOAD.DAT` | 81 948 |
  | `INTRO.PCS` | 297 380 |

  Aucune piste audio : pas de musique.

## Pré-vol (arbre `build/stext/doom`)

- **Mémoire.** `_end = 0x060ab898`, donc 346 984 o de HWRAM libre. Pool total = LWRAM 1 Mo + 346 984 = 1 395 560 o.
  Résident = niveau 236 566 + palettes 514 + tuiles 781 168 + séquences 11 278 + STATIC (armes et wseq) 132 232,
  soit 1 161 764 o. **Marge : 233 796 o.** `make_e1m1` lit `build/doom/MAIN.map` (`_end` 0x060aaf28, arbre non reconstruit) :
  sa marge affichée (235 188) est trop haute de 1 392 o pour cette version.
- **Sons.** 20 statiques (176 970 o) + 16 dynamiques (168 848 o) = 36 sons, sous le plafond de 80. `soundTop` attendu en fin
  de chargement : **345 818** sur 524 288, soit 178 470 o libres. En debug, l'overlay affiche donc `extra:174`.
- **Tuiles.** Géométrie 143 + `tileBase` 95 = 238, plafond u8 255 (marge 17). `MAXNMPICS` : 95 + 347 = 442 sur 800.
- **Objets.** 140 = 1 joueur + 115 mobjs + 24 spéciaux, dont 2 `OT_DOOM_SECRETWALL` (180, murs 1513 et 1529 de la ligne 247).
  Params : 1 502 o, égalité vérifiée par `verif_doom`.
  `verif_doom` donne **49 OK, 0 échec** et 1 avertissement : 3 barils sur 6 sont à moins de 37 u d'un mur, donc on ne peut pas les contourner.

## Ce que tu vois → ce que ça veut dire

Pad, disposé comme Mimas (`dg_saturn.cxx` pad_map) : A = tir ; B = utiliser (porte, interrupteur) ; C = courir
(maintenu) ; L / R = pas de côté ; Z = arme suivante ; Y = arme précédente ; croix = avancer ou tourner. Le menu
d'options de PowerSlave peut encore réaffecter (ses libellés restent ceux de PowerSlave : SAUT = courir, POUSSER =
utiliser). L+R+X bascule le vol debug (A ou C pour descendre, l'automap affiche x/y/secteur). L+R+Y (ou A+B+C)
bascule l'arbre de profil : 11 lignes à gauche puis une seconde colonne à droite (24 nœuds), avec les
étapes d'après les murs — `Post` (animations, push blocks), `Weapon`, `HUD`, `Overlay` ; ce que `root`
garde en propre est l'attente VDP1 / vblank.

| vu | sens / où regarder |
|---|---|
| Écran noir dès le boot, rien ne s'affiche | INIT ne démarre pas : IP.BIN, image ou 0.BIN. Rien de Doom n'est encore en jeu |
| Titre PowerSlave (INTRO.PCS) puis menu | **normal** : l'écran titre et le menu restent ceux de PowerSlave (J4). « Nouvelle partie » lance E1M1, sans carte du monde |
| Noir juste après le titre ou le menu, sans texte | `fs_open` asserte sur un fichier absent (FILE.C:155), `E1M1.LEV` ou `STATIC.DAT`. Relancer `make_e1m1` puis `iso` |
| Mots qui rebondissent (« Write This Down »), un nom de fichier et un nombre | assert : **noter le fichier et la ligne**. C'est la donnée dont on a besoin |
| Assert `DOOM_GAME.C` au chargement, juste après le fondu | `OT_DOOM_SECRETWALL` pointe un mur qui n'est pas DOORWALL : indices de murs du convertisseur périmés (`doom3d.py`) |
| Écran de chargement = TITLEPIC Doom, puis fondu | STATIC bloc 1 bien lu, le niveau se charge |
| Boot en boucle ou gel pendant le chargement | pool épuisé (marge 234 Ko) ou `soundTop` au-delà de 512 Ko. Noter la dernière image vue |
| Vue OK mais aucun monstre ni objet | `game_placeObject` (DOOM_GAME.C) n'a rien posé, ou tout est hors secteur |
| Sprites minuscules ou géants | `scale` : le runtime doit écrire 65536 (SPRITE.C:86 met 48000) |
| Sprites flottants ou enterrés | `y` = floorLevel + rayon (`shiftSprites`) ; décalage `to` des chunks |
| Monstres figés sur la 1re image | les tics 35 Hz ne tournent pas (CFG_TIC_*, SRUINS.C:2142) ou `A_Look` ne voit pas |
| Bonus (BON1/BON2) et barils qui clignotent tous en même temps | tic de départ aléatoire absent (`1 + P_Random() % tics`, `game_placeObject`) : **corrigé**, ils doivent être déphasés |
| Tous les monstres crient au même instant au chargement | verbe exécuté au spawn : **corrigé** (`doomSetSpawnState`, le 1er `A_Look` est au 1er tic, déphasé) |
| Monstres qui ne descendent jamais vers le sud / partent plein ouest | angle de `A_Chase` ≥ 180° non normalisé (SBL `MTH_Sin/Cos`) : **corrigé** (`normalizeAngle`) |
| Monstres qui marchent en « moonwalk » ou dans le mauvais sens | vue k ↔ rotation k+1 (DOOM_ABI §2) |
| Monstre qui glisse de quelques pixels de côté en tournant | vues miroir 5-7 : décalage `w − 2·lo` (`rle8.mirror_chunkx`). **Corrigé** ; s'il reste, c'est le sens du flag 1 (WALLS.C:2789) |
| On traverse les lampadaires, colonnes, candélabres | décor MF_SOLID : **corrigé** (sphère `doomSolidRadius`, 21 u pour COLU). Encore traversable ⇒ `NOSPRCOLLISION` quelque part |
| Arrêté trop loin (≈ 1 u) ou trop près d'une colonne | rayon de sphère calculé pour l'œil à 41 u ; les monstres s'y arrêtent ~13 u plus loin que Doom (écart assumé) |
| Boule d'imp qui explose sur une colonne | **normal** (Doom : missile contre MF_SOLID). La traverse ⇒ `DF_SOLID` absent |
| Le joueur recule quand il est touché, fort à côté d'un baril | **normal** : poussée de P_DamageMobj (dégâts/8 u/tic), corrigé |
| Couleurs fausses sur les sprites ou les murs | palette 0 / `objectPalette` 0, ou index 0 et 255 remappés |
| Teinte orange quand blessé + ramassage | **ne doit plus arriver** : le rouge a priorité sur le jaune (`doomFlashTic`) |
| Aucun son | `doom_sfxIndex` renvoie < 0 (carte 227), ou STATIC bloc 3 mal lu. `extra:` ≠ 174 ⇒ les sons ne sont pas chargés |
| Tir muet mais monstres audibles | index statique (PISTOL = 0) ou `ST_JOHN` |
| Déplacement lent, on « patine » | **corrigé** : la course était sur R, qui fait aussi le pas de côté droit, donc jamais tout droit (vitesse de marche 8,3 u/tic). Elle est sur C maintenu, et la vue suit le balancement de Doom (`doomViewBob`, ±8 u, période 20 tics) |
| Porte qui s'écrase au lieu de monter | **corrigé** : la face de porte est une dalle rigide (4 coins mobiles, la recette des portes retail), son haut caché par le plafond ; les rails DOORTRAK sont fixes à la hauteur ouverte |
| Parois de l'ascenseur étirées en descendant | **corrigé** : parois de cage fixes du bas de course au sol voisin, cachées par le sol de la plate-forme tant qu'elle est en haut |
| Un morceau de porte flotte au-dessus du plafond | le plafond ne recouvre pas le haut de la dalle : ordre des murs de la feuille (sol/plafond après les murs) ou face sous un ciel (`faces_porte_non_rigides`, 0 sur E1M1) |
| Rien ne se ramasse (armes, bonus, munitions) | **corrigé** : les sphères ne se touchaient jamais (caméra centrée sur l'œil à 41 u, objet de 8 u au sol) ; test de Doom maintenant (boîte rayon + 16, hauteur −8..56, joueur en mouvement) |
| Porte qui s'ouvre mais bloque le passage | `CFG_DOOR_FIT` ≠ 56 : SHORTOPENING 80 en vigueur |
| Linteau de porte ouverte de 3 u au lieu de 4 | `doorHeight` non déduit de la fente : **corrigé** (67 + fente 1 = 68) |
| Porte qui ne réagit pas au bouton C | `push()` rate le mur (portée < 120 u) ou le push block n'a pas de mur (`PBWall`) |
| Un monstre ouvre la porte secrète (ligne 247, près de la salle de départ) | `doom_wallIsSecret` : **corrigé** ; si ça arrive, le mur heurté n'est pas dans les 2 `OT_DOOM_SECRETWALL` |
| L'ascenseur de la cour ne fait qu'un cycle | `CFG_LIFT_RESET` : SIGNAL_SWITCHRESET absent (AI.C:4667) |
| L'interrupteur de sortie ne fait rien | `exit_func` (canal 900) ou orifice à plus de 40 u |
| Sortie ⇒ retour au titre | **normal** : un seul niveau (`DOOM_NMLEVELS` 1), le dernier renvoie au titre |
| Barre de statut absente ou décalée, vue de 200 lignes | cadre 224 non appliqué (CFG_TV_SIZE / CFG_SCL_LINES) ou chars 0-4 |
| Visage STFKILL quand on tient le tir 2 s | **normal** (rampage, ST_RAMPAGEDELAY 70 tics) |
| Chiffres HUD en carrés ou vides | polices Doom (masque 0x1E, chars dès 5) : ledger VRAM VDP1 (7 232 o libres calculés) |
| Ciel tourné de 90° et répété | **corrigé** : PowerSlave range son ciel transposé (horizontale = lignes du bitmap, 256 texels pour 90° comme Doom ; hauteur = colonnes, le haut vers la droite) |
| Montagnes du ciel trop hautes ou trop basses | horizon estimé colonne 260 (`SKY_HORIZON`, make_e1m1.py) depuis le bandeau des ciels retail ; noter de combien de lignes, c'est un seul nombre |
| Ciel en miroir par rapport à Mimas | Doom dessine son ciel en miroir, reproduit ici (colonne = −ligne) ; si Mimas ne le fait pas, inverser dans `sky_block` |
| Arme qui déborde sur la barre | clip arme 192 (CFG_WCLIP_BOTTOM) |
| Arme collée à gauche la moitié du temps en marchant | bob horizontal ≥ 180° : **corrigé** (`normalizeAngle`, DOOM_WEAPON.C) |
| Menu en jeu (Start) sans texte | connu : les polices 2/3 sont STTNUM/STYSNUM en jeu (TODO) |
| Fps : `fps:A B` en haut à gauche | lecture dans `COMPARE_MIMAS.md` §1. `mem:` sous 20k = danger |

## TODO restants (honnêtes)

**Rien n'a encore été vu sur console.** Chaque ligne du tableau ci-dessus est une hypothèse de panne, pas une panne observée.

- **Écran titre et menus.** INTRO.PCS, INITLOAD.DAT et les menus sont ceux de PowerSlave. L'option cachée DEATH TANK lierait
  `BONUS.BIN`, absent du disque ⇒ assert. `MAP.DAT`, `LOGOS.PCS` et `OPEN.MOV` sont sautés sous `GP_GAME_DOOM`.
- **Musique.** Pas de piste CDDA : `tools/mkcue.py` et la chaîne MUS→raw (PLAYER 10) n'existent pas.
- **Niveaux.** Un seul niveau (`doomLevelNames = {"+E1M1.LEV"}`). Pas de transport d'inventaire entre niveaux, pas d'écran
  d'intermission, pas de sauvegarde Doom.
- **Armes.** Poing, pistolet, fusil et chaingun ont leurs verbes. Les roquettes (`A_FireMissile` → `MT_ROCKET`) sont codées
  mais non testées (E1M3). Tronçonneuse, super-shotgun, plasma et BFG sont des corps vides (`STUB_P`, DOOM_VERBS.C:44-51).
- **Verbes monstres.** Seuls ceux d'E1M1 existent (POSS, SPOS, TROO, baril) ; les autres sont des `STUB_M` (Vile, Revenant, Mancubus…).
- **Ramassages.** Les pouvoirs (PINV, PSTR, PINS, SUIT, PMAP, PVIS) et la mégasphère sont refusés (DOOM_PLAYER.C:718).
- **Spéciaux de carte.** Ignorés : secret (special 9), lumières (1, 8, 12 ; `OT_DOOM_LIGHT` renvoie 0), scroll (ligne 48).
  Pas de porte à clé dans E1M1. **Ciel** : les fireballs explosent contre un mur/plafond de ciel et les balles y laissent un
  puff (Doom : retrait sans explosion, pas de puff) — demande un flag « ciel » du convertisseur, non fait.
- **RNG.** Corrigé : tirage `lastlook` de chaque spawn (joueur compris), tic de départ aléatoire des things placés, tirages
  paresseux de `newChaseDir` (un seul `movecount` par appel), tirage `tics` de la mort du joueur. **Reste désynchronisé
  de Doom** : les lumières (P_SpawnSpecials tire `P_Random` pour les secteurs special 1/2/3/4/13/17 après les things) ne
  sont pas émulées ; les sondes de `P_NewChaseDir` sont différées d'un tic par candidat (d'autres acteurs tirent entre-temps)
  et le `movecount` est tiré avant les tirages d'étage. T5/T7 (« même prndindex que Mimas ») restent donc impossibles.
- **HUD** :
  - pas de visages OUCH ni de regard latéral (le rampage est fait) ;
  - l'upload DMA du visage se fait pendant le rendu : déchirure possible d'une trame ;
  - les crânes affichent l'image de la carte ;
  - le message ne sort que sur les ramassages (clés et portes non branchées) ;
  - menu en jeu sans texte ;
  - ciel PLAX.C:47 non recalé sur 224.
  Le rendu PC de la barre est au pixel près, mais sur un modèle Python, pas sur le C exécuté.
- **Latents (sans effet sur E1M1)** : groupes de sons statiques PowerSlave à 0 (seul le plongeon SPRITE.C:565 est atteignable,
  pas d'eau) ; sol W1 sur 2 feuilles avec un seul `floorSector` (un ramassage posé sur la feuille 175 resterait en l'air ;
  seuls 2 monstres y sont).
- **Écarts de règles connus :**
  - attente des portes 3,7 s (Doom 4,3) ;
  - DORCLS partagé entre porte close et arrêt d'ascenseur ;
  - PAL non compensé (29 Hz) ;
  - corps du joueur 57 u (Doom 56) ;
  - décor solide : sphère moteur, les monstres s'arrêtent ~13 u plus loin que Doom.
- **Vérifications prévues mais absentes :** `sim_chase.py` (T5) n'existe pas ; le dédoublonnage des sons (PLAYER 11) n'est pas simulé.
- **Ledger VRAM VDP1.** 7 232 o libres calculés. Le passage des `assert(EZ_charNoToVram)` n'est prouvé que sur console.
