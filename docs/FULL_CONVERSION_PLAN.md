# Conversion totale WAD / Build sur le moteur — plan v2 (2026-09-14)

Révision de la synthèse du 13 (archivée : `docs/study/full-conversion-2026-09-13/FULL_CONVERSION_PLAN_v1.md`)
après les réserves du propriétaire, et après quatre lectures supplémentaires du moteur (R7 acteurs/IA,
R8 physique/monde/sauvegarde, R9 2D/VDP2, R10 coût par frame — même dossier). Les faits du moteur déjà
vérifiés en v1 (§1.2 et §1.3 de la v1 : boucle, marionnettes, caches, tuiles, palettes, allocateur, son,
CD, sondes) restent vrais et ne sont pas recopiés ici ; les manques « convertisseur » de la v1 (§3.2-3.3,
items 15-27 et 36-42) restent la liste de référence pour les outils.

Marquage : `[src]` lu dans le code du fork ce jour ; `[mesuré]` calculé ce jour (24 `.LEV` retail,
`tools/lev.py` ; état GitHub par `gh`) ; `[HW]` mesuré sur console (STEXT_BASELINE) ; `[est]`
estimation ; `[web]` source en ligne citée en fin de document.

---

## 0. Réponse courte — les réserves, une par une

1. **Son : on part du moteur, rien de Mimas.** `SOUND.C` (32 slots pokés par le SH-2, sons par niveau
   dans le `.LEV`, `posMakeSound` volume/pan, sons de frame automatiques) et la musique CD du moteur
   (`playCDTrack`, `FILE.C:275` ; `trackMap[]`, `SOUND.C:387`) sont shippés. La v1 proposait d'importer
   le backend SFX de Mimas (~300 l.) : **retiré**. Les deux écarts avec Doom (un one-shot par frame
   toutes sources confondues ; pas de priorité de voix) sont des retouches de 20 lignes dans `SOUND.C`,
   à faire seulement si on les entend. Les MUS deviennent des pistes CDDA rendues hors ligne (chaîne
   PC), jouées par le lecteur CD du moteur, pas par celui de Mimas.
2. **Performance : la loi de la v1 mesure un build ASSERT + STATUSTEXT sur du Duke converti, et le
   moteur plafonne structurellement à 30 fps en jeu** (`SCL_DisplayFrame` attend le VBlank-OUT
   suivant : 2 fields minimum, R10 §1). PowerSlave est cité à 30 fps `[web]` ; Duke et Quake Saturn
   sont dans la même classe. « 15-30 fps selon la scène » est donc **la classe des jeux shippés, lue sur
   un build de debug**, pas une estimation basse — mais la constante de 14,9 ms n'est pas décomposée
   et le retail est NDEBUG (deux `assert` par cellule dont un appel de fonction, 586 sites, 15
   `drawStringf` d'overlay, IRQ HBlank à 15,7 kHz du STATUSTEXT ≈ 2-3 ms/frame `[est]` R10 §4). La
   piste moteur (§3) commence par là : NDEBUG, puis 10 timers d'étage sur le profileur cycle-exact
   **déjà dans le moteur et déjà actif dans tous les builds** (`PROFILE.C`, FRT φ/32, 1,117 µs/tick,
   R10 §3) — l'arbre s'affiche déjà à l'écran dans l'arbre de travail non commité (L+R+Y).
3. **« La 2D de Doom » = 9 601 lignes** (`m_menu`, `st_stuff` avec le visage, `wi_stuff` avec la carte
   animée, `hu_stuff`, `f_finale`, `am_map`) qui dessinent toutes dans un framebuffer 8 bpp. Le moteur
   a **sa** 2D, entièrement VDP1 : table de menus `dlgItem`, texte 1 commande par caractère (polices 4 bpp
   CLUT, zéro CRAM), barre d'état = un char 320×42 + polygones, automap `MAP.C` par-dessus la 3D, carte
   du monde (R9 §1-5). NBG0 dans la v1 n'était pas un choix : c'est la feuille 512×512 **qui existe déjà**
   pour la grenade et la manacle (banc CRAM 6). Ce qu'une feuille bitmap achète : la 2D de Doom **sans
   modification, au pixel près** ; ce qu'elle coûte : 128 Ko de VRAM VDP2 (un banc entier, B1, une fois
   la feuille NBG0 rendue conditionnelle au jeu), 72 Ko de HWRAM de framebuffer, **0,9-1,7 ms/frame**
   pour les 32 lignes du HUD (pas de DMA vers VDP2 dans le moteur, copie CPU), 6-12 ms plein écran
   (R9 conclusion B). **Décision v2 : en jeu, la 2D du moteur habillée en Doom** (barre STBAR en char,
   chiffres par `drawStringf`, clés et visage = 1 char chacun, police STCFN convertie au format 4 bpp
   du moteur, automap = `MAP.C`) = 0 ms, 0 Ko ; **hors jeu (titre, menus, intermission, finale), option
   NBG1 pour faire tourner les écrans de Doom tels quels** — 6-12 ms de copie n'ont aucune importance
   là. Le menu Doom par-dessus la vue 3D et le melt sont sacrifiés (le modal du moteur est sur fond noir).
4. **Sol dominant VDP2** : levier moteur, indépendant de la conversion, déjà étudié
   (`VDP2_DOMINANT_FLOOR.md`) : 50-70 cellules = 2,0-2,7 ms ≈ 1/6 d'un palier, **meilleur client avec du
   contenu Doom** (un flat par secteur, 64×64 = une tuile). Il vient **après** le profileur (on ne sait pas
   encore si les sols sont le bon étage) et il est exclusif avec le ciel HW du split (W0). En §3.4.
5. **`psw-world`** : la réserve « fork vs psw-world » de la v1 est **retirée**. On n'en reprend aucun
   code d'exécution (ni le peintre, ni le son, ni le CDDA, ni les gouverneurs, ni le framebuffer 2D en
   jeu). On garde : les faits matériels consignés dans `saturn-refs/knowledge` (capacité VDP1, CEF, DMA),
   la chaîne `/ship`, les outils PC (strip WAD, rendu MUS→CDDA), et le core Doom **comme référence** :
   source des tables (`info.c`) et oracle pour comparer côte à côte.
6. **Optimiser le moteur** : oui, c'est une piste à part entière (§3), avec un budget par étage et une
   comptabilité en cellules par palier, pas un gouverneur.
7. **Comportements apparentables** : c'est le changement d'architecture de cette v2. La v1 gardait le
   playsim Doom intact à côté du moteur (architecture C, deux mondes en RAM, passe de synchro). La v2
   pose **un moteur, des jeux en données + verbes** (architecture D, §2) : le moteur fournit les services
   (mouvement, collision, hitscan, ligne de vue, dégâts de zone, projectiles, secteurs mobiles,
   déclencheurs, son, sauvegarde, 2D), un runtime commun exécute une **table d'états à durée + verbes
   d'action**, et chaque jeu apporte ses tables (états, `mobjinfo`, spéciaux, ramassages, armes), ses
   constantes (`gameparams.cfg` existe déjà dans l'arbre de travail avec des clés `GP_*`, R8 §1) et sa
   bibliothèque de verbes (Doom : ~50 `A_*` ; Duke : les mots-clés CON, transpilés). Le lecteur R7 a
   vérifié que **tous les services d'un `A_*` existent déjà** ; ce qui manque est l'ossature (états à
   durée, callback) et trois comportements (alerte par le bruit, cadavres, monstres qui ouvrent les
   portes) — ~1 600 lignes, 25-30 j pour les monstres.
8. **Supprimé parce que le moteur l'a déjà** : le renderer (déjà), la 2D, le son, la boucle, le WAD et
   l'allocateur, la collision/le mouvement/la visée/la ligne de vue, les portes/ascenseurs/lumières comme
   mécanismes, l'automap, la sauvegarde, l'entrée, les tables trigonométriques. Reste de Doom : **les
   tables** (4 662 lignes de `info.c`, générées) et **~5-6 000 lignes de règles** ré-hébergées sur les
   services (verbes, armes, dégâts/ramassages, spéciaux, joueur). Table complète en §2.2.
9. **320×224** : oui. Cadre 3D 192 lignes (au lieu des 200 du moteur, −4 % de fill) + 32 lignes de
   barre, `SCL_224LINE` existe, ~1 j (clip `XMIN…`, FOOTCLIP, bbox ciel, clip arme). Aspect ×1,12 : reste
   une décision owner (§7).
10. **Sauvegardes : `SaveRec` du moteur + 8 octets** (armure, type d'armure, skill, sac à dos), 6 slots =
    11 blocs BUP au lieu de 10 ; le modèle du moteur (sauvegarde à la sortie de niveau, niveau rechargé à
    neuf, inventaire transporté) **est** le modèle Doom entre niveaux (R8 §7 et conclusion c). La
    sauvegarde en cours de niveau est sacrifiée (§9).
11. **Duke** : ce qu'il fait que ni Doom ni PowerSlave ne font, et le chemin moins cher, en §5. Résumé :
    le jeu PC n'est pas séparé de son moteur (1 302 appels + 3 950 accès directs), il déplace et fait
    tourner des secteurs, il a des miroirs, des caméras, un langage de script interprété par acteur et
    par tic, et 700 sites de 2D. Le chemin moins cher est **celui de Lobotomy** : Duke comme jeu du
    moteur — CON **transpilé** hors ligne en verbes du runtime de §2, physique du moteur (plus de
    clean-room BUILDLIC, plus de question juridique sur les structs), sous-ensemble vertical des effets
    de secteur. **45-60 j au-dessus de Doom au lieu de 90-120**, fidélité « Duke Saturn », pas « Duke PC ».
12. **Renommer** : le dépôt est un fork GitHub public de `Lobotomy-Software/SlaveDriver-Engine`, 0 fork
    enfant, ~1,4 Mo `[mesuré]` ⇒ éligible à « Leave fork network » (Settings → Danger Zone) `[web]`.
    Renommer d'abord (redirections conservées), détacher quand le dépôt cesse d'être « la source
    PowerSlave + des patchs » — au premier jalon de §2. **`aguzzino`** est libre sur GitHub (0 dépôt,
    0 utilisateur `[mesuré]`), et le moteur contient déjà `ramsesLid_func` (`AI2.C:34`). Détail en §8.
13. **RAM : la v1 se trompait de problème.** Les 24 niveaux retail font **1,16-1,60 Mo** de fichier
    (géométrie 245-754 Ko, tuiles 417-830 Ko, sons 42-196 Ko vers la RAM son, séquences 10-72 Ko), le
    moteur les charge en bloc ; E1M1 converti fait **1,20 Mo** (géométrie 228, tuiles 776, sons 43, séq.
    8) `[mesuré]` — **dans l'enveloppe, sous la médiane**. Ce qui débordait en v1, c'est ce que
    l'architecture C ajoutait : le code Doom (+336 Ko), la zone (256 Ko), la carte Doom en double, les
    mini-WAD, le framebuffer. L'architecture D n'ajoute que des tables et des verbes (~80-100 Ko `[est]`).
    Le convertisseur travaille déjà à l'enveloppe (E4.1b/c, cache de 28 tuiles) ; Mimas, lui, ne « stocke
    pas tout » : il streame textures, sprites et flats depuis le CD. La cartouche redevient une issue de
    secours pour les grandes cartes Doom II, pas une condition du shareware. Chiffres en §4.
14. **Gouverneur : retiré.** Les « gouverneurs Mimas » de la v1 disparaissent ; le pacing par vblank
    du moteur (`smoothVTime`, `framesElapsed`) est son horloge, on n'y touche pas. Ce qui les remplace :
    les 10 timers d'étage et le budget par étage de §3.

**Effort Doom en D : 60-70 j d'écriture, 70-85 j avec les allers-retours console `[est]`** — la même
classe que la v1 (70 / 70-90). D n'achète pas du temps ; il achète **un seul monde en RAM, un seul
tic, un seul moteur à optimiser, et Duke à moitié prix**. Ce qu'il coûte : Doom n'est plus bit-exact
(pas de relecture des démos, sensation approchée par des constantes, monstres sphériques) — réserve
posée en §2.4 avec les mesures qui la bornent.

---

## 1. Ce qui change entre v1 et v2

| sujet | v1 (13-09) | v2 (14-09) | pourquoi | preuve |
|---|---|---|---|---|
| architecture | C : playsim Doom intact + `.LEV` dérivé + synchro par tic | **D : services moteur + runtime états/verbes + jeux en données** | performance, RAM, un moteur pour Doom/Duke/PowerSlave | R7, R8 |
| son | backend SFX Mimas porté en C, CDDA du fork | **`SOUND.C` et `playCDTrack` tels quels**, 2 retouches optionnelles | le moteur a shippé ; Mimas balbutie | `SOUND.C:320-329, 58-76`, `FILE.C:275` |
| 2D | feuille NBG0 = écran Doom (en jeu et hors jeu) | **2D VDP1 du moteur habillée en jeu ; NBG1 optionnel hors jeu** | 0 ms/0 Ko en jeu ; la feuille NBG0 est la grenade ; VRAM B pleine | R9 |
| sauvegarde | différentielle 2-4 Ko, nom automatique | **`SaveRec` +8 o, checkpoint à la sortie de niveau** | le modèle du moteur = Doom entre niveaux ; 0 code nouveau sinon 3 sites | R8 §7 |
| RAM | plans A/B/C, « B non acquis », cartouche confortable | **enveloppe retail 1,16-1,60 Mo ; E1M1 1,20 Mo dedans ; cart = secours** | la v1 comptait les additions de C | §4, `tools/lev.py` |
| CPU | loi 14,9 + 39,2 µs prise telle quelle ; gouverneurs Mimas | **piste moteur : NDEBUG, 10 timers, budget par étage ; pas de gouverneur** | la constante n'est pas décomposée ; le profileur existe | R10 |
| Duke | clean-room BUILDLIC + CON interprété + 90-120 j | **chemin Lobotomy : CON transpilé, physique moteur, 45-60 j** | même runtime que Doom ; plus de question juridique | §5 |
| réserve psw-world | « J0a décide fork vs psw-world » | **retirée** | owner : psw-world = impasse | — |
| premier jalon | J0a mesures gratuites (dont T de l'overlay Mimas) | **J0 = NDEBUG + 10 timers + sonde de jointure esclave** | décomposer avant de ranger | R10 §5 |
| dépôt | non traité | **renommage + détachement, `aguzzino`** | demande owner | §8 |

---

## 2. Architecture D — un moteur, des jeux en données + verbes

```
                       ┌──────────────── moteur (services) ────────────────┐
  .LEV (géométrie,     │ moveSprite / collideSprite   hitScan / canSee      │
   tuiles, séquences,  │ constructGenproj / radialDamage / SIGNAL_HURT       │
   sons, objets) ────► │ push blocks (dy) + door/elevator   SIGNAL_PRESS/    │
                       │ ENTER/SWITCH(channel)   setSectorBrightness         │
                       │ posMakeSound   SaveRec/BUP   dlg_*/drawString/MAP.C │
                       └───────────────────────┬────────────────────────────┘
                                               │ appelés par
                       ┌───────────────────────▼────────────────────────────┐
                       │ runtime commun : Object + table d'états à durée     │
                       │ (sprite, frame, tics, verbe, suivant) + verbes      │
                       └──────┬──────────────────────┬──────────────────────┘
                              │                      │
            ┌─────────────────▼──────┐   ┌───────────▼───────────────────┐
            │ Doom : states[]/mobjinfo│   │ Duke : CON → tables action/    │
            │ générés d'info.c, ~50   │   │ move/ai + verbes transpilés,   │
            │ A_*, spéciaux, pickups, │   │ SE/ST vertical, 11 armes       │
            │ armes, gameparams doom  │   │ gameparams duke                │
            └────────────────────────┘   └───────────────────────────────┘
                       PowerSlave : les 17 xxx_func de AI.C, inchangés
```

### 2.1 Le contrat

- **Le moteur** garde ses structures et sa boucle (`runLevel`, 30 Hz objets, 60 Hz joueur, monde mobile
  par push blocks, caches, son, CD, BUP, 2D VDP1). PowerSlave **reste compilable et jouable à chaque
  jalon** (`GAME=powerslave|doom|duke` au build) : c'est le test de non-régression du moteur, et la
  condition pour continuer à cueillir l'amont.
- **Le runtime commun** (nouveau, ~700 l. `[est]` R7 conclusion c) : un `Object` générique
  `game_actor_func` qui, sur `SIGNAL_MOVE`, décrémente `tics`, applique le verbe de l'état, passe à l'état
  suivant, et route `SIGNAL_HURT/VIEW/OBJECTDESTROYED` vers les verbes de douleur/mort. Il porte aussi ce
  que le moteur n'a pas : alerte par propagation de secteurs (~60 l., patron `radialDamage`/`ROUTE.C`),
  cadavres (objet gardé sans collision, ~50 l.), portes ouvertes par les monstres (~60 l., les routes
  évitent `DOORWALL` aujourd'hui `ROUTE.C:97-100`), attaque hitscan de monstre (~40 l. ; `hitScan`
  n'est appelé que par le joueur et le laser), `painchance`/`reactiontime`/`threshold` (~120 l.),
  test LOS et règle « même espèce » dans `radialDamage`/`SIGNAL_HURT` (~20 l.).
- **Un jeu** = tables générées (états, types, sons, spéciaux, ramassages, armes, progression) + un
  fichier de constantes `gameparams` (rayon/œil/marche du joueur, friction, gravité, vitesses, cadence
  des tics) + une bibliothèque de verbes en C + les données converties (`.LEV`, STATIC, pistes CD,
  habillage 2D).
- **Cadence** : le moteur fait ses objets à 30 Hz ; Doom pense à 35 Hz. Deux choix, à trancher par J2 :
  changer le pas (`SRUINS.C:2142`, `mmc -= 2` → accumulateur 35/60) ou rescaler les `tics` ×6/7 à la
  génération des tables. Le premier est exact, le second est gratuit.

### 2.2 Doom sur D — ce qui disparaît, ce qui reste

Base : `Mimas/core`, 72 594 lignes `[mesuré v1]`.

| bloc Doom | lignes | sort en D | par quoi (moteur) |
|---|---|---|---|
| renderer `r_*` | 17 877 | supprimé | `WALLS.C`, `SPR.C`, caches |
| UI 2D `m_menu hu_* st_* wi_* f_* am_map` | 9 601 | supprimé ; **données** (STBAR, STCFN, M_*, WI*) converties | `dlg_*` (MENU.C), `drawString*` (PRINT.C), char de barre (SRUINS.C:1206-1339), `MAP.C` ; option NBG1 hors jeu |
| boucle/jeu/sauvegarde `d_main d_loop d_net g_game p_saveg` | 7 789 | supprimé ; progression = table | `runLevel`, `levelGraph`→liste, `BUP.C` |
| son `s_sound i_sound` | 2 360 | supprimé ; table `S_sfx` → liste blanche par niveau | `SOUND.C`, `playCDTrack` |
| `w_wad z_zone i_* m_misc m_argv` | ~4 000 | supprimé | `FILE.C`, `mem_malloc`, `V_BLANK.C` |
| `p_setup` (chargement de carte) | ~1 400 | supprimé | `LEVEL.C` (le `.LEV` est la carte) |
| `p_maputl p_map p_sight p_mobj` (collision, mouvement, LOS) | ~3 500 | supprimé | `moveSprite`, `hitScan`, `canSee`, `newSprite` |
| `p_ceilng p_floor p_plats p_doors p_lights` (mécanismes) | ~2 300 | supprimé ; **paramètres** (vitesse 2 u/tic, attente 150 tics, course) | push blocks + `door_func`/`elevator` (AI.C:4313, 4590), automate lumière (~50 l., `setSectorBrightness`) |
| `tables m_fixed m_bbox m_random` | ~1 200 | supprimé ; **`rndtable[]` gardée** (les dégâts Doom en dépendent) | `MTH_*`, `RNDTAB.H` |
| `info.c` (138 sprites, 967 états, 137 types) | 4 662 | **gardé, généré** en tables du runtime | script `info2tables.py` |
| `p_enemy` (verbes `A_*`) | ~2 300 | **gardé, ré-hébergé** : ~900 l. de verbes | services R7 (a) |
| `p_pspr` (armes) | ~900 | **gardé, ré-hébergé** : verbes `A_Fire*`, `A_Lower/Raise` | `queueWeaponSequence` + `FRAMEFLAG_FIRE` (WEAPON.C:920), `getAutoAimRay` |
| `p_inter` (dégâts, ramassages) | ~1 000 | **gardé** : table type→effet + règles d'armure | `playerGetObject` (SRUINS.C:1583-1799), `playerHurt` |
| `p_spec p_switch p_telept` (règles des spéciaux) | ~2 500 | **gardé en table** (type de ligne → action, tag = `channel`) + adaptateurs | `SIGNAL_PRESS/ENTER/SWITCH`, interrupteurs (AI2.C:518-676) ; **G1/GR à ajouter** (`SIGNAL_HURT` sur `wall->object`, WEAPON.C:582) |
| `p_user` (joueur) | ~400 | **gardé** : intégrateur 35 Hz, `×0,90625`, sans saut ni tangage | `movePlayer` (SRUINS.C:857-1048) paramétré, `PLRCYL` |
| `p_tick` (thinkers) | ~300 | supprimé | `runObjects` |
| `d_englsh`, `dstrings` | ~800 | gardé (chaînes) | — |

Ordre de grandeur : **~5-6 000 lignes de logique** ré-hébergées + 4 662 lignes de tables générées ; le
reste (≈ 60 000 lignes) est le moteur.

### 2.3 Services ↔ Doom — ce qui existe, ce qui manque (R7, R8)

| besoin Doom | service moteur `[src]` | écart |
|---|---|---|
| `P_SpawnMobj` | `getFreeObject` + `newSprite(sector, radius, friction, gravity, seq, flags, owner)` SPRITE.C:68 | pools 350 objets / 450 sprites / 20 gibs → constantes (E1M6 : 439 mobjs) |
| `P_TryMove` + glissement | `moveSprite` → `COLLIDE_*` SPRITE.C:908 ; sphère de `radius`, marche `GP_PLAYER_STEP` | sphère ≠ cylindre pour les monstres (chevauchement en hauteur possible) ; joueur = `PLRCYL` (cylindre, non commité) |
| `P_SpawnMissile`, `P_ExplodeMissile` | `initProjectile` + `constructGenproj` (AI.C:119-161) | — |
| `P_RadiusAttack` | `radialDamage` OBJECT.C:377-449 | + LOS, + « même espèce » (~20 l.) |
| `P_LineAttack`, autoaim | `hitScan` HITSCAN.C:185 + `autoTarget` élu au dessin (WALLS.C:2724) | autoaim Doom = cône vertical ; à paramétrer |
| `P_CheckSight` | `canSee` HITSCAN.C:342 | — |
| `P_NoiseAlert` | **absent** | flood de secteurs ~60 l. |
| `P_DamageMobj`, infighting | `SIGNAL_HURT` + `monsterObject_signalHurt` (retarget) AICOMMON.C:184-203 | painchance absent (stun 20 fixe) ; ~120 l. avec reaction/threshold |
| `A_Look`, `A_Chase`, `A_FaceTarget` | `normalMonster_idle/walking`, `decideWhatToDo`, `PlotCourseToObject` AICOMMON.C:305-423 | `A_Chase` Doom = 8 directions pas-à-pas ; ré-écrit en vélocité (~150 l.) |
| `A_Fall`, cadavres | **absent** (`delayKill` à la mort) | ~50 l. |
| rotations 0..7 | `getFacingAngle` + 8 séquences par état | le convertisseur duplique les 5 vues + miroir en 8 (flag bit 0) |
| `S_StartSound(mo, …)` | `spriteObject_makeSound` → `posMakeSound` SOUND.C:392 | — |
| portes / ascenseurs / sols | push block par secteur tagué (faces de sol + bas des murs adjacents, **à émettre par le convertisseur**) + `door_func` (2 u/tic, 128 tics) / `elevator` (5 u/tic) | vitesses et attentes Doom en paramètres ; `keyMask` = clés ; monstres n'ouvrent pas les portes (~60 l.) |
| interrupteurs, lignes W1/WR/S1/SR | `SIGNAL_PRESS` (hitscan < 120 u → `wall->object`), `SIGNAL_ENTER` (`sector->object`), `SIGNAL_SWITCH(channel)` diffusé | G1/GR (tirer) absent : 1 `SIGNAL_HURT` sur `wall->object` |
| lumières (flash, strobe, glow) | `setSectorBrightness(s, level)` AICOMMON.C:50-70 (lumière **par sommet**, réécrite par secteur) | automate ~50 l. ; interdit de partager un sommet entre secteurs animés |
| ramassages | `playerGetObject(type)` + table SRUINS.C:1583-1799 | réécrire la table (Doom : 40 types) |
| armes, cadence, munitions | `queueWeaponSequence`, `FRAMEFLAG_FIRE` → `weaponFire`, `weaponAmmo[8]`, `weaponUp/Down` | 7 armes Doom + poing/tronçonneuse ; flash fullbright = séquence |
| joueur | `movePlayer` 60 Hz par échantillon, `vel = (15v+f)/16` puis ×0,9 ; saut, tangage, roulis, dégâts de chute | Doom : 35 Hz, `vel += thrust`, ×0,90625 ; tourner sans inertie ; sans saut/tangage/roulis/chute — **à paramétrer ou à cadencer** (R8 conclusion b) |
| sauvegarde | `SaveRec` 100 o × 6, BUP, sauvegarde à la sortie de niveau, niveau rechargé à neuf | +8 o (armure ×2, skill, sac à dos) ; 3 sites d'appel |

### 2.4 Physique et sensation — la réserve, et ce qui la borne

La v1 (et `DOOM_ON_SLAVEDRIVER.md` §3-A) rejetait « Doom devient un jeu du moteur » parce que « le
comportement de Doom est un fait, pas une intention ». **La réserve tient** : en D, Doom n'est plus
bit-exact — pas de relecture des démos, collisions sphériques pour les monstres, 30 Hz ou 35 Hz
rescalé, glissement le long des murs du moteur. Le propriétaire a placé la performance et l'unité du
moteur au-dessus ; on le note, et on borne la réserve par des mesures :

- **Tout ce qui est une table reste exact** : durées d'états, dégâts, vitesses, santés, `painchance`,
  sons, `rndtable[]` (les dégâts Doom tirent dedans), chaînes d'états. Elles sont générées d'`info.c`,
  jamais recopiées à la main.
- **Tout ce qui est une constante physique est une clé `gameparams`** (le mécanisme existe déjà :
  `GP_PLAYER_STEP` etc., `gameparams.cfg:19-40` `[src]`) : rayon 16, œil 41, hauteur 56, marche 24,
  friction 0,90625, thrust 50/40, vitesse de rotation 3,5°/7°, gravité 1 u/tic², vitesses des portes.
- **Proxies mesurables sur console** (table « vu → sens » de J3) : temps de traversée d'E1M1 du départ à
  la porte de sortie en courant (référence Chocolate Doom PC, même chemin) ; temps d'ouverture d'une
  porte ; temps de vol d'un imp fireball sur 512 u ; latence de réaction d'un zombie à la vue ; nombre
  de coups de pistolet pour tuer un imp (exact par table). Écart > 10 % sur l'un d'eux = défaut à
  corriger, pas à accepter.
- **Ce qui n'est pas mesurable** (le « feel » du glissement, des coins) se juge en côte à côte, et
  c'est le propriétaire qui juge.

### 2.5 La 2D — moteur en jeu, option NBG1 hors jeu

Faits R9 : tout le 2D du moteur est VDP1 ; **VRAM VDP1 en jeu : 256 octets libres** (`EZ_initSprSystem(1448,4,1224)`
+ 5 caches = 524 032 / 524 288) ; feuille NBG0 512×512 en B0+B1 pour la grenade et la manacle (banc 6),
lignes 0-489 occupées ; CRAM 8/8 ; NBG1 est le seul plan bitmap libre et sa configuration existe déjà
(titre : INTRO.C:37-63).

| écran | v2 | coût | ce qui change pour le joueur |
|---|---|---|---|
| barre d'état en jeu | char 320×32 = STBAR (palette banc 0 = PLAYPAL, comme les things) ; santé/munitions/armure par `drawStringf` avec STTNUM converti en police 4 bpp CLUT ; clés = 3 chars ; **visage** = 1 char 32×29 re-uploadé au changement (928 o, `EZ_setChar`) ⇒ **libérer un slot 8 bpp** (4 Ko) du cache | ~60 l. ; 0 ms mesurable ; −1 slot de cache | identique à Doom, au pixel près pour les patches, sauf le fond du visage |
| messages HU, « picked up… » | `drawString` avec STCFN convertie | données | police Doom |
| automap | `MAP.C` : 1 `EZ_line` par mur du `.LEV`, couleurs par Δ hauteur → **table de couleurs Doom** (mur, porte, sol différent, secret) | ~30 l. | automap « Doom » sans `am_map.c` ; pas de grille ni de marques |
| menus | table `dlgItem` avec les entrées Doom (nouvelle partie/épisode/skill/charger/sauver/options), M_DOOM et le crâne en over-pics 16 bpp, police bigFont → **police Doom convertie** | ~50 l. par sous-menu, 6 sous-menus | modal sur fond noir (le moteur coupe le ciel, `enablePlax(0)`) — **pas par-dessus la vue 3D** |
| intermission | écran de stats par `drawString` + WIMAP0 en 20 tuiles 64×64 16 bpp (160 Ko VDP1, contexte propre comme BIGMAP) ; animations comme l'œil de BIGMAP | ~150 l. | carte et chiffres Doom ; percentages animés |
| titre, HELP, finale | **option NBG1** : `I_VideoBuffer` 72 Ko HWRAM copié dans NBG1 512×256 en B1 (une fois la feuille NBG0 conditionnelle au jeu), palette banc 0, index 0 → 247, priorité 5 ; le code `f_finale`/`m_menu` de Doom tourne **tel quel** pour ces écrans | 128 Ko VRAM VDP2, 6-12 ms/copie hors jeu, ~2 j | pixel-exact, cast de fin compris (`sprites[]` = séquences) |
| flashs PLAYPAL | `SCL_SetColOffset` (14 teintes) ; invulnérabilité = réécriture des bancs 0-4 (5 × 512 o) | 0,3 j | teinte uniforme, inversion approchée |
| wipe | fondu du moteur | 0 | pas de melt |

Le choix « tout NBG1, y compris en jeu » reste possible (0,9-1,7 ms/frame pour 32 lignes, R9 B) si le
propriétaire veut le visage et la barre au pixel près dès J4 ; il coûte 72 Ko de HWRAM en permanence.

### 2.6 Le son — le boîtier du moteur

- SFX : `SOUND.C` tel quel. Les DS* de Doom (DMX 8 bits 11 025 Hz) passent au format `{size, pitch, 8,
  −1}` + PCM `^0x80` du `.LEV` (v1 §3.3 #26, `wad2snd.py`) ; **liste blanche par niveau** (55 DS* =
  535 Ko > 512 Ko de RAM son ; les niveaux retail embarquent 42-196 Ko `[mesuré]`), ~30 sons statiques
  dans STATIC. `SoundRec.size` u16 : le plus long son Doom fait 18,6 Ko.
- Deux écarts, à retoucher **si audibles** : un one-shot par frame toutes sources confondues
  (`SOUND.C:320-329`) et `silenceVoice` sans priorité (`:58-76`) — 20 lignes chacun.
- Musique : `playCDTrack(track, 1)` du moteur (`FILE.C:275`) ; `trackMap[]` régénéré par le
  convertisseur ; 13 MUS du shareware rendus hors ligne (chaîne PC : MUS → MIDI → SF2 → WAV → 2352) ;
  Doom II 32 morceaux ≤ 74 min. `idmus` = `playCDTrack(n)`.

### 2.7 La sauvegarde — `SaveRec` +8 octets

`SaveState` (96 o) + `valid` = 100 o, 6 slots = 10 blocs (`BUP.C:18-42`). Doom entre niveaux transporte
`health`, `armorpoints`, `armortype`, `ammo[4]`, `weaponowned`, `backpack`, `skill`, épisode·carte —
tout existe déjà dans `SaveState` sauf armure ×2, skill, sac à dos : **+8 o ⇒ 108 o, 11 blocs**. Trois
sites d'appel (`bup_saveGame` à la sortie de niveau) + `bup_initCurrentGame`. Sauvegarde en cours de
niveau : sacrifiée (§9) ; « recommencer le niveau » = snapshot `levStart` du moteur, gratuit.

### 2.8 Le cadre — 320×224

`SCL_224LINE` (`sega_scl.h:350`) ; vue 3D lignes 0-191 (`XMIN/YMIN/XMAX/YMAX` `WALLS.C:128-131`,
FOOTCLIP, bbox ciel, clip arme `SEQUENCE.C:256-262`), barre 192-223. Le moteur dessine 200 lignes de
3D aujourd'hui : **−4 % de fill**. Aspect vertical ×1,12 : décision owner (§7), 1-2 j, après J4.

---

## 3. Performance — la piste moteur

### 3.1 Ce que la loi mesure vraiment

`calc ≈ 14,9 ms + 39,2 µs × cellules` `[HW]` : 5 captures, **build ASSERT + STATUSTEXT**, Duke E1L1
converti, 190-824 cellules/frame. Paliers : 470 cellules = 30 fps, 896 = 20, 1 321 = 15. Ce que la
lecture R10 ajoute :

- **30 fps est le plafond structurel en jeu** : `SCL_DisplayFrame` attend le VBlank-OUT suivant
  (`SCL_VBLV.C:69-76`) ⇒ période = max(`smoothVTime`+1, ⌈travail/16,7 ms⌉) fields, minimum 2. Le
  « 60 » de l'overlay n'est jamais atteint en jeu.
- `calc` **inclut** l'overlay (15 `drawStringf` = sprintf + 1 commande VDP1 par glyphe) et **le spin de
  jointure esclave** (`WALLS.C:2452-2455`) ; `draw` = attente du tracé de la frame **précédente**.
- Le retail est NDEBUG : par cellule maître, 2 `assert` + `getPicClass()` (appel) + 5 asserts de
  `mapPic` + 4 `validPtr` ≈ 3-4 µs des 39 ; overlay ≈ 0,6-0,9 ms ; IRQ HBlank-IN du `htimer`
  (15,7 kHz) ≈ 2-3 ms sur une frame de 47 ms `[est]`. **Prédiction NDEBUG : ~14 ms + ~35 µs/cellule** —
  à mesurer, pas à croire.
- La capture 5 (+24 ms hors modèle à 498 cellules) n'est toujours pas expliquée ; candidat n° 1 = le
  spin de jointure (`slaveSize` ne bouge que de ±1 par frame).

### 3.2 La boucle, décomposée (R10 §1)

| étage | ∝ | attend |
|---|---|---|
| `findDoorways` + boucle `NEEDTOPROCESS` (4 transformations + `clipZ` + 4 projections par portail ; cache invalidé chaque frame) | secteurs visités × portails | — |
| `buildTree` + `sortLeafList` (2 tris O(n²)) | secteurs² | — |
| kick esclave ; maître : `drawSector` + `drawSprites` des secteurs lointains | cellules, sprites | — |
| `movePlayer` (1 pas par vblank écoulé) ; `runObjects` (30 Hz, ≤ 4 tics/frame) | frames ; objets actifs | — |
| `drawWallsFinish` : **spin jointure**, `drawSlaveWalls` (copie 32 o + gouraud par cellule esclave) | cellules esclave | **esclave** |
| anims, push blocks, `runWeapon`, HUD, overlay | const | — |
| `EZ_closeCommand` ; `SPR_WaitDrawEnd` (CEF de la frame précédente) | — | DMA, **VDP1** |
| `SCL_DisplayFrame` | — | **VBlank ×2** |

L'esclave : lancé **avant** les secteurs du maître, les sprites et le tic ; il ne fait attendre le
maître que si sa part (les secteurs les plus proches, caméra comprise, `slaveSize` secteurs) dépasse
`drawSector(loin) + drawSprites + Motion`. Budget esclave : `height*width + nmSlavePolys + 50 > 1 300`
⇒ mur **sauté par personne** (`WALLS.C:1374`). Rien ne chronomètre le côté esclave.

### 3.3 Le profileur existe — 10 timers d'étage

`PROFILE.C` : FRT du maître, φ/32 = **1,117 µs/tick**, 16 bits ⇒ wrap 73 ms ; arbre de 60 nœuds ;
`NPROFILE` n'est défini nulle part ⇒ **actif dans tous les builds, NDEBUG compris** ; sortie
`debugPrint` = no-op hors PSYQ, mais l'arbre de travail non commité ajoute `drawProfileData` à l'écran
(bascule L+R+Y). Nœuds posés : `Walls` ⊃ `Find Visible` ⊃ `Find Doorways` ; `2nd Half` (boucle
cellules de `drawRectWall` seulement) ; `Motion` ⊃ `Run Objects` ⊃ `Collide Sprite`. Le spin de
jointure est **fondu dans `Walls`** (même littéral, `SRUINS.C:2192`).

Instrumentation J0 (R10 §5) : `getTimer()` exporté, `st[10]` cumulés par frontière, 2 lignes
STATUSTEXT en dixièmes de ms :

| n | étage | insertion | attendu à 700 cellules `[est]` |
|---|---|---|---|
| 0 | doorways | `WALLS.C:2278-2290` | 2-4 ms |
| 1 | tree + sort | `:2307-2413` | 0,3-1 ms |
| 2 | murs maître (transfo + clip + émission) | autour de `:2428` | 10-20 ms (la pente) |
| 3 | sprites | `:2429`, `:2436` | 0,5-3 ms |
| 4 | joueur | `SRUINS.C:2137` | 0,5-1,5 ms |
| 5 | objets | `:2150` | 1-4 ms **par tic** (le jitter ±2,2 ms de la régression) |
| 6 | **jointure esclave** + `i`, `slaveSize`, `updateListSize` | `WALLS.C:2451-2455` | **0-10 ms** |
| 7 | émission esclave | `:2460` | 3-5 ms (8-12 µs/cellule esclave) |
| 8 | arme + HUD | `SRUINS.C:2207-2214` | 0,3-0,6 ms |
| 9 | overlay | `:2217-2242` | 0,8-1,5 ms |
| + | VDP1 wait, VB wait, plax | `:2273-2318` | hors calc, contrôle |

`st[2]/nmPolys` et `st[7]/nmSlavePolys` donnent **les deux pentes séparément** — la régression sur
`polys` (maître + esclave confondus) ne le peut pas.

### 3.4 Leviers, classés en cellules par palier

Règle (mémoire `slavedriver-cost-law-measured`) : un levier se chiffre en **ms retirées à
constante fixée** ou en **cellules retirées**, et se juge contre 470/896/1 321.

| levier | où | gain `[est]` | quand |
|---|---|---|---|
| **NDEBUG** (2 asserts + appel par cellule, 586 sites, overlay, IRQ HBlank) | flag de build | −1 ms constante, −4 µs/cellule ; jusqu'à −3 ms si l'IRQ HBlank pèse ce qu'on croit | **J0, gratuit** |
| jointure esclave : politique `slaveSize` (±1/frame → proportionnelle à la mesure), ou déplacer le tic **après** la jointure quand le maître est en avance | `WALLS.C:2452-2459`, `SRUINS.C:2137-2191` | 0-10 ms, la capture 5 | J1, après le timer 6 |
| `findDoorways` : cache de portails entre frames (invalidé sur mouvement de caméra > seuil) ; `sortLeafList` O(n²) → tri par insertion sur liste presque triée | `WALLS.C:2266, 2307-2413` | 1-3 ms | J1 |
| densité du convertisseur : E1M1 = 35 cellules/secteur (Duke E1L1 : 19), murs de 64 u ; portes en rangées de 64 plutôt que 32 ; fusion de murs coplanaires | doom2ps | −20..−40 % de cellules/frame | J2-J3 |
| **sol dominant VDP2** (Doom : 1 flat/secteur = 1 tuile) | `drawSector` PARALLAX pattern, RPMD=3 (`VDP2_DOMINANT_FLOOR.md`) | 50-70 cellules = 2-2,7 ms ; plus avec du Doom | après J1, si les sols sont un étage mesuré |
| chemin lumière (A+B+C déjà shippés, `slavedriver-vertex-transform-perf`) : `rectTransform` pipeliné | `wallasm_gnu.s` | ~15 cyc/sommet sur 25 % des sommets | après mesure du timer 2 |
| fill sprites proches (chunk zoomé 200 px = 40 k px) | budget de sprites par distance | `draw` 8-20 ms sur 2 captures/5 | J2 (timer + `draw`) |
| overlay : `drawStringf` → chaîne pré-formatée, 1 commande par ligne | `PRINT.C` | 0,5-1 ms en build STATUSTEXT seulement | quand ça gêne la mesure |

### 3.5 Objectif chiffré

**Tenir 30 fps** ⇔ `calc + draw ≤ 33,4 ms` ⇔ cellules ≤ (33,4 − c)/s. Aujourd'hui (ASSERT) : 472.
NDEBUG prédit : ~550. Si J0-J1 ramènent **c ≤ 8 ms et s ≤ 30 µs**, le budget passe à ~850 cellules :
**toutes les scènes d'E1M1** (200-900 `[est]`) et la plupart des scènes Doom shareware. C'est
l'objectif de la piste moteur ; il se vérifie sur console à chaque jalon, et il **ne dépend pas** de la
conversion.

---

## 4. RAM — l'enveloppe retail

### 4.1 Ce que le moteur charge par niveau `[mesuré]` (24 `.LEV` retail, `tools/lev.py`)

| section | destination | min | médiane | max |
|---|---|---|---|---|
| ciel (palette + bitmap + table K) | VDP2 A0/A1, CRAM 7 | 133 Ko | 133 Ko | 133 Ko |
| géométrie (`loadLevel`) | aire 1 (HWRAM) | 245 Ko | ~470 Ko | 754 Ko |
| sons (`loadDynamicSounds`) | **RAM son** (SCSP, 512 Ko) | 42 Ko | ~155 Ko | 196 Ko |
| palettes + tuiles (`loadTiles`) | aires 0/1 (HWRAM puis LWRAM) | 417 Ko | ~636 Ko | 830 Ko |
| séquences (`loadSequences`) | aire 1 | 10 Ko | ~25 Ko | 72 Ko |
| **fichier entier** | | **1,16 Mo** | **~1,43 Mo** | **1,60 Mo** |
| **résident en RAM de travail** (géométrie + tuiles + séquences) | 2 Mo moins le moteur | ~0,85 Mo | **~1,14 Mo** | ~1,32 Mo |

(KILENTRY, 658 Ko, est un niveau spécial ; SUNKEN/KILENTRY ont deux champs mal lus par le script sur les
palettes, sans effet sur les totaux.)

**E1M1 converti** (`build/doom2ps/TOMB_e1m1.LEV`, E4.1c, 141 tuiles distinctes) : **1,20 Mo** —
géométrie 228 Ko, sons 43 Ko, palettes 12 Ko, tuiles 776 Ko, séquences 8 Ko ⇒ **résident ~1,0 Mo,
sous la médiane retail**. Avec les sprites des monstres Doom (8 rotations, 200-225 Ko `[mesuré v1]`) :
~1,25 Mo, sous SHRINE (1,32 Mo). En E4.1b (−290 Ko de tuiles) : ~0,95 Mo. Le disque `TOMB_e1m1`
**boote et se joue** : la preuve est faite par construction.

### 4.2 Pourquoi la v1 débordait, et ce que D retire

| poste v1 (architecture C) | Ko | en D |
|---|---|---|
| code Doom non-renderer (text + rodata + data + bss) | +336 (−184 de PowerSlave) | **tables + verbes ≈ 80-100** `[est]` ; le code PowerSlave reste (GAME=) |
| zone Doom réservée avant `mem_lock()` | 256 | **0** (pas de `Z_Malloc`, les objets vivent dans les pools du moteur) |
| structures de carte Doom (`PU_LEVEL`, blockmap, nodes, reject) | ~81-250 | **0** (le `.LEV` est la carte) |
| `RES.PSW` + `.PSW` + `.LNK` | 137 | **0** (assets convertis dans STATIC et le `.LEV`) |
| framebuffer `I_VideoBuffer` | 72 | **0 en jeu** ; 72 si l'option NBG1 hors jeu est retenue (récupérable : alloué hors niveau) |
| psprites (95 chunks) | 130 | inchangé — PowerSlave a aussi ses armes en STATIC |
| sprites de monstres | 200-225 | inchangé ; 4 rotations (÷1,6) reste un levier |

Le débordement de −75..−400 Ko de la v1 était **entièrement** fait des trois premières lignes.

### 4.3 Leviers du convertisseur (l'auteur de niveau, c'est lui)

Le moteur charge tout ; les niveaux retail sont *écrits* pour son enveloppe. Le convertisseur fait le
même métier, et il a déjà les outils : échelle E4.1b/E4.1c (une tuile par texture à demi-résolution vs
141 tuiles pleine échelle : 290 Ko d'écart sur E1M1), cache VDP1 de 28 tuiles comme **ressource rare**
(mémoire `duke-pc-converter`), `merge_leaves` pour les cartes > 600 feuilles. À ajouter quand une carte
déborde : tuiles et sprites **4 bpp CLUT** comme Lobotomy gen 2 (Duke/Quake Saturn : ×2 sur les tuiles,
flats Doom mesurés quasi sans perte, `lobotomy-gen2-findings`) ; sous-ensemble de sprites par skill ;
4 rotations. **La cartouche 4 Mo** (`validPtr` +1 ligne, `UTIL.H:83`) reste l'issue de secours pour
Doom II MAP15/MAP29 et l'Ultimate E4M9 (956 feuilles), pas une condition du shareware.

---

## 5. Duke — pourquoi c'est cher, et le chemin Lobotomy

### 5.1 Ce que Duke fait que ni Doom ni PowerSlave ne font `[mesuré v1, R4]`

| | Doom | PowerSlave (le moteur) | Duke PC |
|---|---|---|---|
| séparation jeu/moteur | nette : 20 fonctions `R_*` sur 75 sites | nette : services + `xxx_func` | **aucune** : 1 302 appels + ≈ 3 950 accès directs à `sector[]/wall[]/sprite[]` |
| logique des acteurs | tables + `A_*` en C | C par monstre | **CON** interprété par acteur et par tic (16 888 tokens, 119 acteurs, 136 états, 189 actions, 76 moves, 83 ai) |
| géométrie mobile | verticale seulement | push blocks **dy seulement** (`SPRITE.C:852-890`) | verticale **+ translation** (métro SE6/14/30) **+ rotation** (SE0/1/11), portes coulissantes/pivotantes, séismes, grues |
| vues | pas de miroir ni de caméra | idem | **miroirs, caméras de sécurité**, `rotscrnang` |
| sprites | billboards | billboards | billboards **+ muraux + au sol** (138 muraux sur E1L1), 5 vues + miroir |
| 2D | 1 framebuffer | VDP1 | **`rotatesprite` sur ~700 sites** (HUD, inventaire, menus, cinématiques ANM) |
| physique | `p_map` (GPL) | `moveSprite` | `clipmove/getzrange/hitscan` **BUILDLIC** (1 872 l.) |
| audio | 55 DS*, 535 Ko | 42-196 Ko/niveau | 181 VOC, 2,9 Mo, voix > 64 Ko |

Le coût de la v1 (90-120 j) venait de vouloir **le jeu PC** : physique clean-room + harnais différentiel
(10-15 j et une question juridique sur les structs), interprète CON sur le SH-2 (5-25 ms/tic estimés,
risque n° 1), `tile2d` pour 700 sites.

### 5.2 Le chemin moins cher — Duke comme jeu du moteur, comme Lobotomy l'a fait

Le Duke Saturn tournait sur ce moteur : Lobotomy a réécrit les acteurs en C sur ses services et coupé
ce que le moteur ne sait pas faire. En D, le même chemin devient systématique :

| brique | v1 | v2 | j `[est]` |
|---|---|---|---|
| physique | clean-room BUILDLIC + harnais | **`moveSprite`, `hitScan`, `canSee`, `radialDamage` du moteur** ; constantes Build en `gameparams duke` (8 unités Build = 1 u déjà dans le convertisseur) | 0 (+ réglage) |
| CON | compilé hors ligne en bytecode, interprété sur SH-2 | **transpilé hors ligne en C** : `action/move/ai` → table d'états du runtime §2 ; corps d'`actor` → verbe ; 113 mots-clés → bibliothèque de ~1 000 l. ; outil GPL-3 à nous, CON de 3D Realms jamais au dépôt, sortie générée chez le testeur | 8-10 (transpileur, PC) + 6-8 (bibliothèque) |
| effets de secteur | translation parquée, rotation ~50 l. | **sous-ensemble vertical** : portes ST, ascenseurs SE, séisme (`earthQuake` existe) ; métro/rotation/miroirs/caméras **hors plan** (comme le disque Saturn, à vérifier carte par carte sur `RETAIL_DISCS.md`) | 6-8 |
| sprites muraux/au sol, 11 armes, inventaire | D5-D8 | services + tables ; `displayweapon` = séquences d'arme du moteur | 12-15 |
| 2D | `tile2d` 300-500 l. | HUD/menus du moteur habillés (comme Doom §2.5) ; `tile2d` seulement pour les écrans spéciaux | 6-8 |
| son, disque | D9-D12 | liste blanche par niveau (≤ 512 Ko, voix > 64 Ko découpées), CDDA | 5 |
| convertisseur | `convex.py` 33/194 cartes | **reste le goulot** ; robustesse | 6+ |
| **total** | 90-120 j | **45-60 j** au-dessus de Doom-en-D | |

Ce qu'on perd par rapport à la v1 : la fidélité « Duke PC » (la sensation Build, le métro, les miroirs).
Ce qu'on gagne : plus d'interprète par tic (les verbes sont compilés : 1-3 ms/tic au lieu de 5-25
`[est]`), plus de clean-room, plus de question juridique, et **le même runtime que Doom** — chaque
service optimisé sert les trois jeux.

---

## 6. Jalons v2

Règles inchangées : chaque jalon livre **un disque** et une table « vu → sens » (le propriétaire teste
sur console ; l'assistant ne lance jamais l'émulateur `[mem]`) ; PowerSlave reste jouable à chaque
jalon ; toute mesure de temps se fait sur console.

| J | livrable | j | dépend | kill / décision |
|---|---|---|---|---|
| **J0 — décomposer** | (1) `build.ps1 -NDebug -StatusText`, les 5 mêmes captures → nouvelle loi ; (2) les 10 timers d'étage (§3.3) + sonde de jointure (`i`, `slaveSize`, `updateListSize`) sur le disque `TOMB_e1m1` existant, 3 scènes ; (3) commit propre de l'arbre de travail (profileur à l'écran, `PLRCYL`, `gameparams`) | 1-2 | — | c'est une mesure : pas de kill ; **sortie = table ms par étage** et la capture 5 expliquée ou non |
| **J1 — piste moteur** | les 3 étages les plus lourds de J0 (jointure/`slaveSize`, doorways, overlay) ; objectif c ≤ 8 ms, s ≤ 30 µs ; sol dominant si les sols ressortent | 5-10 | J0 | 30 fps tenu en couloir et salle d'E1M1 sur le disque existant ; sinon la piste continue en parallèle des jalons suivants |
| **J2 — runtime + monstres** | `game_actor_func` (états à durée, verbes, HURT/VIEW/DESTROYED), `info2tables.py`, ~50 `A_*`, alerte bruit, cadavres, portes, hitscan monstre, painchance ; POSS/SPOS/TROO sur `TOMB_e1m1` avec sprites Doom (8 rotations, `wad2sprites.py`) ; cadence 35 Hz tranchée | 20-25 | J0 | `Run Objects` > 8 ms/tic avec 30 monstres actifs ⇒ throttle AISLOT par distance (le moteur l'a) ; `newSprite` NULL ⇒ pools relevés |
| **J3 — joueur, armes, règles** | `PLRCYL` avec les clés Doom, intégrateur 35 Hz / ×0,90625, sans saut ni tangage ; 7 armes + poing/tronçonneuse (`queueWeaponSequence`), autoaim ; table des ramassages, armure, clés → `keyMask` ; convertisseur : **un push block par secteur tagué**, `door/elevator` paramétrés, interrupteurs (2 tuiles), G1 par `SIGNAL_HURT`, automate lumière, ciel SKY1, midtex ; E1M1 **finissable** | 12-15 | J2 | proxies §2.4 à ±10 % ; E1M1 non finissable = bloquant |
| **J4 — ça ressemble et ça sonne Doom** | `SCL_224LINE` ; barre STBAR en char + chiffres + clés + visage (1 slot libéré), STCFN en police 4 bpp, menus `dlgItem` Doom, intermission stats, automap couleurs Doom ; `SOUND.C` + liste blanche + CDDA ; 14 teintes ; option NBG1 hors jeu | 6-8 | J3 | visage évincé du cache avec 6+ monstres ⇒ visage en char 16 bpp hors cache ; `assert soundTop` sur E1M8 |
| **J5 — le shareware entier** | 9 `.LEV`, STATIC Doom, progression (`levelGraph` → liste + sortie secrète), `SaveRec` +8 o, `mkdisc.py --game doom`, `/ship` (convertisseur chez le testeur) | 6-8 | J4 | E1M6 (606 feuilles) : `merge_leaves` ; sinon « E1M6 cart only » dit tel quel |
| **J6 — autres WADs** | Doom II / Ultimate / PWAD (`merge_wad` corrigé) / DEH (patch des tables générées) ; tuiles 4 bpp CLUT, sprites par skill, 4 rotations ; cart en secours | 8-10 | J5 | `size ≥ 900 000` ⇒ carte déclarée non portable |
| **Duke D0** (PC, parallèle) | transpileur CON → C + tables ; E1L1 acteurs tournent headless sur une build PC des services si séparables, sinon sur console à D1 | 8-10 | J2 (runtime) | < 80 % des acteurs d'E1L1 transpilés sans cas spécial ⇒ interprète minimal pour le reste |
| **Duke D1-D4** | bibliothèque de verbes, SE/ST vertical, sprites muraux, armes, 2D habillée, son, disque, robustesse convertisseur | 35-50 | D0, J3 | E1L1 injouable (SE parqués) ⇒ carte non portable, pas le jeu |

**Total Doom J0-J6 ≈ 60-70 j d'écriture, 70-85 j avec les allers-retours console. Duke D0-D4 ≈ 45-60 j.**

### Vu → sens (extraits)

| jalon | vu sur console | sens |
|---|---|---|
| J0 | la ligne `jn` (jointure) > 5 ms sur une scène | le maître attend l'esclave : la politique `slaveSize` est le premier levier |
| J0 | `dw` (doorways) ≈ 3-4 ms partout | la constante est là pour un quart : cache de portails |
| J0 | somme des 10 timers ≠ `calc` de plus de 2 ms | un étage n'est pas couvert (sprites hors sous-listes ? DMA ?) |
| J1 | `fps:30 30` stable en salle, `20` seulement en arène | objectif §3.5 atteint hors arène ; l'arène est un problème de cellules (convertisseur) |
| J2 | un zombie marche vers le joueur, tire, meurt, **reste au sol** | runtime + cadavres OK ; s'il disparaît, `A_Fall` n'a pas retenu l'objet |
| J2 | les imps tirent des boules qui traversent les murs | `constructGenproj` sans `COLLIDE_WALL` : flags de blocage du projectile |
| J3 | traversée E1M1 départ → porte de sortie : temps à ±10 % du PC | sensation dans la tolérance ; sinon `gameparams` (thrust/friction) |
| J3 | porte qui « claque » ou plafond qui traverse le sol | course du push block mal bornée par le convertisseur |
| J4 | visage qui clignote ou se change en texture de mur | éviction du slot : visage hors cache |

---

## 7. Décisions owner v2

| décision | recommandé | pourquoi |
|---|---|---|
| Architecture : C (playsim intact) vs **D (services + runtime + données)** | **D** | performance, RAM, un moteur, Duke à moitié prix ; réserve de fidélité §2.4 bornée par des proxies |
| Cadence des objets Doom : pas 35 Hz dans `runLevel` vs `tics` ×6/7 | **35 Hz** (accumulateur) | exact ; le 30 Hz reste pour PowerSlave |
| 2D en jeu : moteur habillé vs feuille NBG1 permanente | **moteur habillé** ; NBG1 hors jeu en option | 0 ms, 0 Ko ; les écrans hors jeu peuvent payer 6-12 ms |
| Son | **`SOUND.C` + `playCDTrack`** tels quels | shippé |
| Sauvegarde | **`SaveRec` +8 o**, checkpoint sortie de niveau | modèle du moteur = Doom entre niveaux |
| Cadre | **320×224**, 3D 192 lignes | −4 % de fill ; barre Doom |
| Aspect ×1,12 | après J4, côte à côte | décision visuelle |
| Tuiles | **E4.1b** par défaut ; E4.1c quand la carte le permet | enveloppe §4 |
| Rotations | 8 ; 4 si les swaps le demandent | idem v1 |
| Gouverneurs | **aucun** | le pacing du moteur suffit ; on mesure d'abord |
| Sol dominant VDP2 | après J1, si les sols ressortent | 2-2,7 ms, meilleur avec Doom ; exclusif avec le ciel HW du split |
| Duke | **chemin Lobotomy** (D0 après J2) | 45-60 j ; plus de clean-room |
| Dépôt | renommer maintenant, détacher à J2 | §8 |
| Distribution | convertisseur chez le testeur | inchangé (IWAD dérivé) |
| Split-screen | hors plan | inchangé |

---

## 8. Le dépôt — renommer, détacher

État `[mesuré]` (`gh repo view N0rt0N85/SlaveDriver-Engine`) : **fork** public de
`Lobotomy-Software/SlaveDriver-Engine`, 0 fork enfant, ~1,4 Mo, remotes `origin` (fork) et `upstream`
(Lobotomy). Licence : GPL-3, `LICENSE.txt`, copyright « 1996, 2006, 2025 Ezra Dreisbach » — à
conserver tels quels, avec une ligne « basé sur SlaveDriver de Lobotomy Software » dans le README.

**Renommer** (Settings → General → Repository name) : GitHub redirige l'ancienne URL web et les remotes
git ; à faire d'abord, sans risque. **Détacher** (« Leave fork network », Settings → Danger Zone
`[web]`) : conditions remplies (public, < 1 Go, sans fork enfant) ; effets : perte des issues, PR,
étoiles, watchers ; commits conservés ; **irréversible** ; plus de synchronisation automatique avec
l'amont — on garde le remote `upstream` pour cueillir à la main. Un fork ne peut pas devenir privé ; un
dépôt détaché, si. Recommandation : détacher au premier jalon de §2 (J2), quand le dépôt cesse d'être
« la source PowerSlave + des patchs ». Rester en fork n'apporte rien d'autre que le bandeau.

**Le nom** : `aguzzino` (le garde-chiourme, le meneur de galériens — les deux SH-2 apprécieront) est
libre sur GitHub : 0 dépôt, 0 utilisateur `[mesuré 14-09]`. Le moteur contient déjà `ramsesLid_func`
(`AI2.C:34`) et `GAMEFLAG_TALKEDTORAMSES`. Convention GitHub : minuscules pour le dépôt (`aguzzino`),
le nom propre dans le README.

**Impact d'un renommage du dossier local** (`C:\Users\pcico\Projects\SlaveDriver-Engine` → `…\Aguzzino`)
`[mesuré]` : 6 scripts à chemin codé en dur (`tools/duke2ps/budget_correct.py`,
`tools/study/fullconv/{j3_check,r3_duke_measure,r3_static_measure,r4_count}.py`, `build/measure_lev.py`) ;
mentions documentaires sans effet (4 docs du fork, 10 docs Mimas, `saturn-refs/CATALOG.md` + 2 fiches
knowledge) ; 11 fichiers de mémoire de l'assistant ; un clone `saturn-refs/SlaveDriver-Engine`. La
convention « diff minimal, commits N0rt0N85 sans mention d'IA » reste ; elle se précise : **nouveaux
sous-systèmes dans de nouveaux fichiers** (`runtime/`, `game/doom/`, `game/duke/`), fichiers du moteur
touchés au minimum, `GAME=powerslave` toujours vert.

---

## 9. Ce que la v2 sacrifie

- **Exactitude Doom** : pas de relecture des démos ; collisions sphériques pour les monstres ;
  glissement et coins du moteur ; alerte par le bruit et infighting réécrits (règles, pas code) ;
  sensation bornée par des proxies (§2.4), jugée en côte à côte.
- **Menus par-dessus la vue 3D**, melt, sauvegarde en cours de niveau, cheats au clavier (page de menu).
- **Éclairage** : 6 bancs CRAM pour les sprites, gouraud par sommet sur les murs ; flashs en teintes
  uniformes ; invulnérabilité approchée.
- **Cadence** : 30 fps plafond structurel ; 20 en arène tant que la piste moteur n'a pas livré.
- **Duke** : métro, rotation de secteurs, miroirs, caméras, sensation Build ; 181 VOC réduits par
  niveau.
- **Disque** : le WAD n'est plus lu à l'exécution ; PWAD et CON passent par le convertisseur, chez
  l'utilisateur.

---

## 10. Repris de la v1 sans changement

Dans `docs/study/full-conversion-2026-09-13/FULL_CONVERSION_PLAN_v1.md` : §1.2 (faits du moteur
vérifiés), §1.3 (réfutations, à ne plus recopier), §3.2-3.3 items 15-27 et 36-42 (convertisseur :
portes fermées + course, interrupteurs, animations, lumière par secteur, midtex, ciel, feuilles > 600,
sprites, sons, STATIC, disque, validateurs, PWAD, DEH, distribution), §5.3 (VDP1 et caches), §10
(corrections à reporter dans `DOOM_ON_SLAVEDRIVER.md` et `HW_USAGE_VS_MIMAS.md`). À y ajouter :
`DOOM_ON_SLAVEDRIVER.md` §3-A n'est plus « rejeté » mais « retenu sous réserve §2.4 » ; §6 (RAM) est
remplacé par §4 ci-dessus.

Sources en ligne citées : [GitHub — Detaching a fork](https://docs.github.com/en/pull-requests/how-tos/work-with-forks/detaching-a-fork) ;
[Time Extension — SlaveDriver source release](https://www.timeextension.com/news/2025/08/the-source-code-for-the-engine-that-powered-the-sega-saturn-fps-powerslave-has-been-released) ;
[PowerSlave — Wikipedia](https://en.wikipedia.org/wiki/PowerSlave) (30 fps cité, non mesuré par nous).
