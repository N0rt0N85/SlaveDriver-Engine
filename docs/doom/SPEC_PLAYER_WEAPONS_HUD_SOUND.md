# SPEC — joueur, armes, HUD, son Doom côté moteur (2026-09-14)

Spécification d'implémentation sous `#ifdef GP_GAME_DOOM` (`params/doom.cfg` + `GAME = doom`,
DOOM_ABI §7), conforme à `docs/doom/DOOM_ABI.md` (« le contrat »). `[src]` = fichier:ligne du moteur,
`[doom]` = `Mimas/core`, `[wad]` = mesuré sur `DOOM1.WAD`. Tout ce qui est écrit ici vit dans
`game/doom/` (contrat §8) : `DOOM_PLAYER.C`, `DOOM_WEAPON.C`, `DOOM_HUD.C`, `DOOM_SOUND.C`, `DOOM.H`,
plus les crochets listés en §5. Échelle : **1 u Doom = 1 u Saturn** (`doom3d.py:11-12`, contrat en-tête).

**Écarts au contrat, explicités** : (a) §5 du contrat dit « une séquence d'une frame par état,
re-queuée à chaque changement » — la file de 8 (`SEQUENCE.C:155-157`, `assert(qHead!=qTail)`) déborde
avec les états à 1 tic (`A_Lower`/`A_Raise`, 35 Hz > 30 Hz de consommation `:216-219`) : on **épingle**
l'entrée courante au lieu de la queuer (§2.3, nouvel accesseur). (b) Le contrat §7 garde
`PLAYER_MODEL = ball` ; la spec accepte les deux modèles (§1.1), l'intégrateur n'en dépend pas.
(c) Le visage a besoin d'un char 24×32, pas 32×29 (§3.5, mesuré).

## 1. Joueur

### 1.1 Clés `doom.cfg`

| clé | ball (contrat §7, actuel `[src] params/doom.cfg:26-37`) | cylinder (si le propriétaire bascule) | Doom `[doom]` |
|---|---|---|---|
| `PLAYER_RADIUS` | 16 | 16 | `MT_PLAYER radius 16` info.c |
| œil | `PLAYER_EYE_HOVER 25` → 16+25 = 41 | `PLAYER_EYE_STAND 41` (`PLRCYL.C:20`) | `VIEWHEIGHT 41` |
| tête | = rayon (16) → corps 57 | `PLAYER_HEAD 15` → corps 56 (`PLRCYL.H:4`) | height 56 |
| marche | `PLAYER_STEP 24` | `PLAYER_STEP_STAND 24`, `_CROUCH 24` | `MAXSTEPMOVE 24` |
| postures | — | `EYE_CROUCH = EYE_SHRUNK = 41`, `CROUCH_BUTTON none` (`PLRCYL.C:82-85` inerte) | pas d'accroupi |
| `NEAR_CLIP` | 10 | 10 | — |

Le cylindre apporte une seule chose à Doom : la collision verticale exacte sous les plafonds bas
(`SPR_YLO/YHI`, `PLRCYL.H:16-17`). Décision propriétaire ; la valeur par défaut reste `ball`.

### 1.2 Intégrateur Doom dans `movePlayer` (`SRUINS.C:857-1048`)

`movePlayer` consomme **une entrée `inputQ` par trame 60 Hz** (`:865-867`) et, pour chacune, appelle
`controlInput` (`:960`, corps `:432-670`) puis `CFG_FRAME(); moveCamera();` (`:1039`, `SPRITE.C:902-906`
= `doFriction` ×0,90 par trame `SPRITE.C:480-487`, `friction` posée à `AI.C:46`). Doom intègre à 35 Hz
(`P_MovePlayer` p_user.c:141-160, `P_XYMovement` p_mobj.c:109-220). Principe : **l'état Doom (`momx`,
`momz`, angle) avance à 35 Hz sur l'horloge unique de SPEC_RUNTIME §1 ; la position avance à 60 Hz avec
`vel = mom × 35/60`** — exact pour le déplacement par tic, et la collision du moteur (`collideSprite`)
reste seule maîtresse.

```c
/* DOOM.H — UN SEUL typedef DoomPlayer (propriété de DOOM_PLAYER.C ; SPEC_RUNTIME §6 le lit/écrit tel quel) */
typedef struct {
  Fixed32 momx,momz;            /* u/tic, 16.16 (Doom momx/momy ; z moteur = y Doom) */
  int turnHeld;                 /* tics de rotation continue (g_game.c:362 SLOWTURNTICS 6) */
  int health,armorPoints,armorType;
  int ammo[4],maxAmmo[4];       /* am_clip, am_shell, am_cell, am_misl */
  unsigned char weaponOwned;    /* bit wp_* ; = WEAPONINV(currentState.inventory) */
  unsigned char keys;           /* bits it_bluecard.. (p_inter.c:421-467) ; miroir keyMask SRUINS.C:105 si portes à clé */
  signed char readyWeapon,pendingWeapon;   /* pendingWeapon = -1 : wp_nochange */
  int backpack;                 /* maxAmmo ×2 une fois (p_inter.c:589-600) */
  int refire,attackDown,damageCount,bonusCount;
  short pspState[2],pspTics[2]; /* ps_weapon, ps_flash */
  Fixed32 bob,pspSx,pspSy;
  unsigned short input,pushed;  /* dernière entrée 60 Hz, lue par doom_playerTic */
} DoomPlayer;
extern DoomPlayer doomPlayer;
void doom_playerInit(void);                                  /* à SRUINS.C:2010, après initWeapon() */
void doom_playerTic(void);                                   /* 35 Hz — appelé par la boucle de tics SRUINS.C:2142 (CFG_PLAYER_TIC, SPEC_RUNTIME §9), AVANT runObjects */
void doom_playerFrame(unsigned short input,unsigned short pushed); /* 60 Hz, avant moveCamera() */
```

Site exact : dans la branche vivante de `movePlayer`, **remplacer** l'appel `controlInput(input,changeInput)`
(`:960`) par :

```c
#ifdef GP_GAME_DOOM
 doom_playerFrame(input,pushed);     /* mémorise l'entrée ; camera->vel = mom*35/60, yavel/xavel = 0 */
#else
 controlInput(input,changeInput);
#endif
```

et **court-circuiter** : le bloc tir `:977-980` (§2.2), `weaponPlayerMove(yavel)` `:1043` (force de
recul au virage), la mise à jour d'angle `:1026-1037` (yaw écrit par le tic, `pitch = roll = 0`), le vol
cheat `:1022-1024` (garder, il est derrière `cheatsEnabled`).

**Pas de second accumulateur ici** : `doom_playerTic` est tiré par la même boucle que les acteurs (`mmc += vtimer·7`,
`for (;mmc>=12;mmc-=12)`, SRUINS.C:2139-2142/2302 sous les macros de SPEC_RUNTIME §9), joueur puis thinkers comme
`P_Ticker` — deux accumulateurs de même moyenne dérivent d'un tic et consomment `P_Random` dans un ordre non-Doom.
Formules de `doom_playerTic` (`[doom]`
g_game.c:158-160, 362-391 ; p_user.c:52-60, 141-160 ; p_mobj.c:106-107, 132-140, 216-219) :

| grandeur | formule | valeur |
|---|---|---|
| vitesse avant | `move = forwardmove[run]*2048` | 0x19 → 51 200 (0,78125 u/tic) ; 0x32 → 102 400 (1,5625) |
| vitesse latérale | `smove = sidemove[run]*2048` | 0x18 → 49 152 ; 0x28 → 81 920 |
| poussée avant (repère moteur) | `momx += MTH_Mul(move,-MTH_Sin(yaw)); momz += MTH_Mul(move, MTH_Cos(yaw))` | même vecteur que `SRUINS.C:509-518` (`ang_doom = yaw+90°`, AI.C:51) |
| poussée droite | `momx += MTH_Mul(smove, MTH_Cos(yaw)); momz += MTH_Mul(smove, MTH_Sin(yaw))` | = `:544-556` |
| plafond | `|mom| ≤ 30 u/tic` | `MAXMOVE` p_mobj.c:132-140 |
| friction (au sol) | `mom = MTH_Mul(mom, 0xE800)` | 0,90625 (p_mobj.c:107) ; arrêt sous `0x1000` sans entrée (`:216-219`) |
| rotation | `yaw += turning * F(a)`, `a = 640·360/65536 = 3,515625°` (marche), 7,03125° (course), 1,7578° les 6 premiers tics (`angleturn[]`, `turnHeld`) | 230 400 / 460 800 / 115 200 (16.16), **sans inertie** : `yavel` n'est plus lu |
| `run` | `!(input & IMASK(ACTION_RUN))` | Doom `speed` = bouton run (g_game.c:373-383) |
| par trame | `camera->vel.x = MTH_Mul(momx,38229); camera->vel.z = MTH_Mul(momz,38229)` (35/60 = 0,58333) ; `camera->friction = F(1)` (⇒ `doFriction` inerte, `SPRITE.C:481`) ; `camera->gravity = 22301` (1 u/tic² × (35/60)²) | après `moveCamera()` : **relire** `mom = MTH_Div(camera->vel, 38229)` pour que le mur qui annule une composante se voie côté Doom (équivalent `P_SlideMove`) |

Supprimé pour Doom : saut (`:609-628`, bouton `ACTION_JUMP` libre → **arme précédente**), tangage
(`:378-401`, `ACTION_FREELOC` libre), roulis (`:669`), balancement de marche `:657-668` (remplacé par le
bob Doom §2.4), dégâts de chute (`playerDYChange` `:236-245` → `return` sous `GP_GAME_DOOM`), bulle
sous l'eau (aucun `SECFLAG_WATER` émis par doom2ps). Vue verrouillée : `playerAngle.pitch = 0` à chaque
trame — Doom n'a pas de vue verticale, l'autoaim §2.5 fait le reste.

Boutons (`UTIL.H:245-248`, `IMASK` actif bas) : `ACTION_FIRE` tir, `ACTION_PUSH` = *use* (`push()` `:992`,
hitscan < 120 u contrat §6), `ACTION_WEPUP`/`ACTION_WEPDN` armes, `ACTION_STRAFE`/`ACTION_RUN` = pas
latéraux **et** `run` (comme `:544-556` : les deux boutons font strafe ; run = `ACTION_RUN` seul), L+R+X
debug inchangé.

### 1.3 Santé, armure, dégâts, mort

`currentState.health` reste **la** santé (lue par `drawStatBar`, `playerGetObject`, `:962`) ; l'armure vit
dans `doomPlayer` (SaveState +8 o : plan §2.7). `constructPlayer` pose `health = 700` sur l'objet
(`AI.C:54`) — sans effet, seuls `currentState.health` et `SIGNAL_HURT` comptent. `player_func`
(`AI.C:31-36`) route `SIGNAL_HURT(damage, hurter)` vers `playerHurt(param1)` (`SRUINS.C:159-178`) :

```c
void doom_playerDamage(int damage,Object *source);   /* remplace playerHurt sous GP_GAME_DOOM ; SEUL calcul d'armure */
```

**Appelants** : `player_func` (`AI.C:33-34`, hitscan moteur WEAPON.C:582 le temps de J2) et la branche
joueur de `doom_damage` (SPEC_RUNTIME §4) — qui passe `source` (le tireur d'un projectile, p_inter.c:781) et
**ne calcule pas l'armure** ; `playerHurt` n'est plus compilé (plus de `CFG_OUCH`). `[doom]` p_inter.c:781-900 :
`saved = damage/3` (type 1) ou `/2` (type 2) `:860-866`, borné par
`armorPoints` (`:867-871`, type remis à 0) ; `health -= damage` `:876-878` ; `damageCount += damage`
(plafond 100, `:881-884`) → conserver le flash rouge `colorOffset = {63,-63,-63}` `SRUINS.C:169-171`
mais **proportionnel** à `damageCount` (décrément 1/tic, p_user.c:209-217) ; `painchance 255` ⇒
`sfx_plpain` à chaque coup (`ouchTime` `:173-176` supprimé : Doom ne dédoublonne pas) ; `health ≤ 0` ⇒
`sfx_pldeth` (`A_PlayerScream`, health > −50 ; `sfx_slop` n'existe pas pour le joueur en shareware)
et la branche mort existante `:962-975` : `playerIsDead`, `switchPlayerMotion(0)`, chute de la caméra
(`stepPlayerHeight` `:214-234` ≈ `P_DeathThink` viewheight → 6 u p_user.c:182-186), rotation vers le
tueur (p_user.c:196-212) via `source`, `ACTION_PUSH` ⇒ `runLevel` retourne « recommencer » (retour 1 quand le
fondu atteint −255, `:2321-2322` ; `currentState = levStart` `:2531-2533`). **Piège de cette branche** : elle
s'exécute **à chaque trame** tant que `health ≤ 0` et joue `playStaticSound(ST_JOHN,1)` (`:967`) — sous la
liste statique du contrat §3 l'index 1 est **SHOTGN** : coup de fusil en boucle à la mort (le dédoublonnage
SOUND.C:326-328 ne filtre qu'au sein d'une même trame). Retouche : `1` → macro `CFG_DEATH_SFX` (**11** =
PLDETH) sur `:967`, et tout le bloc `:963-974` gardé par `if (!playerIsDead)` (le son, la poussée de caméra
`:970-972` et `deathTimer=0` `:973` ne doivent courir qu'à l'entrée en mort). `doomPlayer` n'est pas dans
`levStart` : c'est `doom_playerInit` au rechargement qui la remet (`G_PlayerReborn`, comme Doom en solo). Secteurs spéciaux 7 (nukage) : **SPEC_RUNTIME §6** (`OT_DOOM_DAMAGE`, appel de
`doom_playerDamage(5, NULL)` toutes les 32 tics depuis `doom_playerTic`) ; 9 (secret) : hors J3.

**`doom_playerInit` s'exécute à chaque niveau** (SRUINS.C:2009-2012 est le corps de `runLevel`). Jusqu'au
18-09 il faisait `G_PlayerReborn` à chaque fois : pistolet et 50 balles en passant E1M1 → E1M2 (vu sur console).
Depuis, une sortie appelle `doom_playerFinishLevel` (`G_PlayerFinishLevel` : clés et pouvoirs perdus) et le
niveau suivant garde santé, armure, armes, munitions et sac ; seules une nouvelle partie et la reprise après la
mort font `G_PlayerReborn` (100 de santé, écrasant la santé que `levStart` remet). Pas de `SaveState` : le
transport vit dans `doomPlayer`, en RAM, d'un `runLevel` au suivant (SPEC_RUNTIME §6).

## 2. Armes

### 2.1 Table (E1M1 ; plasma/BFG exclus, chainsaw absent du WAD)

| wp | arme | munition (`ammo[]`) | cadence | dégâts / tir | dispersion | autoaim | états `[doom]` d_items.c:37-82 | séquences STATIC `wseq = state−1` (contrat §5) |
|---|---|---|---|---|---|---|---|---|
| 0 | poing | — | `S_PUNCH1..5` = 4+4+5+4+5 tics | `(P_Random%10+1)*2` (×10 berserk), portée `MELEERANGE 64` (p_pspr.c:459-476) | ±5,6° (`<<18`) | oui, vertical | up/down/ready/atk `S_PUNCHUP/DOWN/PUNCH/PUNCH1`, flash `S_NULL` | PUNG A-D |
| 1 | pistolet | clip (0) | `S_PISTOL1..4` = 4+6+4+5 = 14 tics (refire boucle à `S_PISTOL2` via `A_ReFire`) | `5*(P_Random%3+1)` ×1 (`P_GunShot` :635-650) | 0 au 1er tir, ±5,6° en refire (`:646`) | `P_BulletSlope` :610-632 | `S_PISTOL…FLASH` | PISG A-E + PISF A |
| 2 | fusil | shell (1) | `S_SGUN1..9` = 3+7+5+5+4+5+5+3+7 = 44 tics | 7 balles × `5*(P_Random%3+1)` (`:678-698`) | ±5,6° toutes | idem | `S_SGUN…SGUNFLASH1/2` | SHTG A-D + SHTF A-B |
| 3 | chaingun | clip (0) | `S_CHAIN1/2` = 4+4 tics, 1 balle chacun | idem pistolet | 0 puis ±5,6° | idem | `S_CHAIN…CHAINFLASH1/2` | CHGG A-B + CHGF A-B (**12 tuiles dans STATIC dès J3** — 10 familles par défaut, contrat §5 ; l'arme n'est ramassable qu'à E1M2) |
| 4 | roquettes | misl (3) | `S_MISSILE1..3` = 8+12+0 tics | projectile `MT_ROCKET` : 20 u/tic, `(P_Random%8+1)*20` au contact, `A_Explode` 128 u/128 hp (info.c:1958-1980) | 0 | idem (pente) | `S_MISSILE…MISSILEFLASH1-4` | MISG A-B + MISF A-D (idem, E1M3) |

Munitions `[doom]` p_inter.c:52-53 : `maxammo {200, 50, 300, 50}`, ×2 sac à dos (absent d'E1M1) ;
`clipammo {10, 4, 20, 1}`. Départ : pistolet, 50 balles, poing + pistolet possédés
(`G_PlayerReborn`). **`weaponAmmo[]`/`weaponMaxAmmo[]` du moteur ne servent plus** : `drawStatBar`
`:1230-1262`, `fireWeapon` `:144-155`, `AMMOBALL` `:1760-1785` sont tous remplacés (§3, §2.2, §1.3).

### 2.2 Machine d'états des psprites (remplace `runWeapon`/`fireWeapon`/`weaponFire`)

```c
void doom_psprTic(void);          /* = P_MovePsprites p_pspr.c:860-880, appelé par doom_playerTic */
void doom_setPsprite(int ps,int state);   /* = P_SetPsprite :50-115 : boucle tant que tics==0 */
void doom_fireWeapon(void);       /* = P_FireWeapon :236-247 : P_CheckAmmo, S_PLAY_ATK1, atkstate, P_NoiseAlert */
int  doom_checkAmmo(void);        /* = P_CheckAmmo :152-230 : ordre chaingun>shotgun>pistol>chainsaw>missile */
void doom_bringUpWeapon(void);    /* = P_BringUpWeapon :129-145 : sy = 128, upstate */
```

Le moteur ne tire plus par `weaponFire()` sur `FRAMEFLAG_FIRE` (`WEAPON.C:920-921`, contrat §5) : les
verbes viennent de `doomStates[].action` (`DOOM_VERBS.C`, **même table `doomActions[]` que les acteurs** :
`DoomAction` est une union `{mobj, psp}`, contrat §4, et `doom_setPsprite` appelle le membre `.psp` — les états
1..89 portent `flags` bit 1), signatures `void A_x(DoomPlayer *p, int ps)`
: `A_WeaponReady` (:273-326 : `pendingWeapon` ⇒ downstate ; `attackDown` ⇒ `doom_fireWeapon` ;
bob §2.4), `A_ReFire` (:334-352), `A_Lower`/`A_Raise` (:376-432, `±6 u/tic`, bornes 32/128),
`A_GunFlash` (:440-446), `A_Light0/1/2` (`extralight` → `SCL_SetColOffset` uniforme, plan §2.5),
`A_Punch`, `A_FirePistol`, `A_FireShotgun`, `A_FireCGun`, `A_FireMissile`. Le bouton tir est lu **par
tic** : `doomPlayer.attackDown = !(input & IMASK(ACTION_FIRE))` dans `doom_playerTic`, jamais depuis
`movePlayer:977-980` (bloc court-circuité). `runWeapon(nmFrames, …)` `WEAPON.C:785-921` devient, sous
`GP_GAME_DOOM`, le seul appel `advanceWeaponSequence(cx, cy, 0)` (le dessin) — plus de `moveWeapon`,
`weaponOK`, `weaponSwitchTimer`, `weaponIn/Out`.

### 2.3 Affichage : épingler, pas queuer (écart a)

```c
/* SEQUENCE.C, nouveau : écrase l'entrée courante (qTail) et remet frame=clock=0 ; sequenceOver=0 */
void setWeaponSequence(int seqNm,int cx,int cy);
```

À chaque `doom_setPsprite(ps_weapon, s)` : `setWeaponSequence(wseq(s), 1 + f(pspSx), 32 + f(pspSy - 32*F(1)))` ;
flash : `addWeaponSequence(wseq(flash))` (`:165-167`, overlay `:308-311`) ou `-1` pour l'éteindre.
Coordonnées contrat §5 : `pos = (xo − 160 + chunkx, yo − 112 + chunky)` en 224 lignes (§3.1 ; **`:297`
passe de `240/2` à `224/2`**) ; la ligne `:252-253` (`overlay = 0x4000` dès la séquence 50) est retirée
(contrat §5). `A_Lower/A_Raise` réépinglent avec `cy` qui court de 32 à 128 : c'est **exactement** la
latence Doom, 16 tics + 16 tics ≈ 0,91 s, à la place des 15 trames de `weaponSwitchTimer` (`:824-829`).

### 2.4 Bob

`A_WeaponReady` p_pspr.c:320-324 : `bob = min((momx²+momz²)>>2, 16 u)` (`P_CalcHeight` p_user.c:81-88,
`MAXBOB 0x100000`), `angle = (128·leveltime) & FINEMASK`, `pspSx = F(1) + bob·cos`, `pspSy = 32 +
bob·sin(angle & (FINEANGLES/2−1))`. `FINEANGLES = 8192` (tables.h:41) : 128/8192 = **1/64 de tour par tic**,
période 64 tics. En unités moteur : `MTH_Cos(F((leveltime·360/64) % 360))` = `leveltime · 5,625°`
(avec `/20` le balancement serait 3,2× trop rapide). Le
`moveWeapon` ressort (`WEAPON.C:118-123`) et `weaponForce` de `:660` ne sont plus appelés.

### 2.5 Hitscan et autoaim

```c
int  doom_aimSlope(Fixed32 *outPitch);    /* P_BulletSlope : autoTarget → pente ; 0 sinon */
void doom_gunShot(int accurate,int damage,int range);   /* P_GunShot + P_LineAttack */
```

Le renderer élit déjà `autoTarget` : sprite `CLASS_MONSTER` dont les pieds tombent à **±20 px** du centre
(`WALLS.C:2724-2733`, remis à `NULL` `:2264`) — `atan(20/160)` = **7,1°**, contre 5,625° (`1<<26` sur 2^32)
pour les trois `P_AimLineAttack` (p_pspr.c:618-626) : fenêtre moteur ≈ 27 % plus large, **écart accepté**
(si trop généreux sur console : filtrer sur `|feetScreenPos.x| < 16`). Les cadavres ne sont **pas** candidats :
`A_Fall` les passe en `CLASS_SPRITE` (SPEC_RUNTIME §3), aucun filtre `DF_CORPSE` ici. Doom ne dévie la balle
qu'en **pente** : on prend `pitch = getAngle(dist_xz, autoTarget->pos.y − camera->pos.y)` (le `#if 0` de
`WEAPON.C:283-292` est exactement ce calcul) et l'azimut reste `yaw + spread`, `spread =
(P_Random()−P_Random())<<18` converti en `F(deg)` (±5,6° max). Portée `MISSILERANGE 2048` (poing 64) :
`hitScan(camera,&ray,&camera->pos,camera->s,&hit,&sec)` (`HITSCAN.C:194`) puis rejet si `approxDist > range`.
Résultat `COLLIDE_SPRITE|i` ⇒ `doom_damage(sprites+i, (Object *)player, (Object *)player, damage)` (SPEC_RUNTIME
§4 → `SIGNAL_HURT` → `game_actor_func`) puis **`doom_spawnBlood(&hit, sec, damage)`** si la cible saigne (pas
`DF_NOBLOOD`), sinon `doom_spawnPuff` ; `COLLIDE_WALL|w` ⇒ **`doom_spawnPuff(&p, sec, melee)`** avec `p = hit − 4·ray`
(`PTR_ShootTraverse` p_map.c:1029-1040) — **une seule implémentation** de `P_SpawnPuff`/`P_SpawnBlood`, celle
de SPEC_RUNTIME §5 (`vel.y`, `pos.y` aléatoire, `tics −= P_Random()&3`, `S_PUFF3`, `S_BLOOD2/3`), à la place de
`constructKapow` (`AI.C:3688`). Roquette : `doom_spawn(MT_ROCKET)` à
`camera->pos + (0, 32−41, 0)` (`P_SpawnPlayerMissile` p_mobj.c:1167-1200, z + 32 u au-dessus des pieds),
`vel = 20 u/tic × ray`, `seesound sfx_rlaunc` joué par le runtime des acteurs (contrat §4) — **RLAUNC est
statique** (index 19, contrat §3) : `MT_ROCKET` n'est jamais « présent » dans un WAD, la liste blanche dynamique
ne le porterait pas. `P_Random` = `rndtable[256]` (m_random.c:24-55) recopié dans `DOOM_TABLES.C`.

### 2.6 Sélection

`weaponUp/weaponDown` (`WEAPON.C:973-993`) scannent `WEAPONINV(inventory)` = bits 8-15
(`GAMESTAT.H:23`) : **on garde l'encodage** (`wp_fist..wp_chainsaw` = 8 bits) et on remplace le corps :
`pendingWeapon = suivant possédé (et `doom_checkAmmo`-compatible)`, `A_WeaponReady` fait descendre
l'arme (p_user.c:276-306 pour la règle poing/tronçonneuse). Plus de `redrawBowlDots()` (`:984, :989`).

## 3. HUD

### 3.1 Cadre 224 lignes

| site | 240 lignes aujourd'hui | Doom 224 |
|---|---|---|
| `SPR_SetTvMode` `SRUINS.C:1878, :2433` | `SPR_TV_320X240` | `SPR_TV_320X224` (`sega_spr.h:117`), `SCL_224LINE` (`sega_scl.h:350`) |
| fenêtre 3D `WALLS.C:128-131` | `XMIN −160 YMIN −110 XMAX 160 YMAX 90` (200 l.) | `−160 / −112 / 160 / 80` : **192 lignes** (0-191), plan §2.8 |
| clip arme `SEQUENCE.C:256-262` | 320×210 | 320×192 |
| psprite `SEQUENCE.C:296-297` | `yo − 240/2` | `yo − 224/2`, `Y0 = 0` (contrat §5) |
| automap `MAP.C:127-129` | `MINY+120` | `+112` |
| barre `drawStatBar` `:1208` | `{−160, 72}` 320×42 | `{−160, 80}` 320×32 → lignes 192-223 |

Repère écran : `x_e = x_doom − 160`, `y_e = y_doom − 88` (barre Doom à y = 168, `ST_Y` st_stuff.c:88-96).

### 3.2 Fond STBAR (remplace `stat_bar`, `SRUINS.C:1888-1889`, `redrawStatBar` `:1198-1202`)

`STBAR` 320×32 `[wad]` + `STARMS` 40×32 collé à x = 104 (`ST_ARMSBGX`) → un char 8 bpp **10 240 o**
(contre 13 440 pour `stat_bar` + 3 × compas `:1890-1897` supprimés), `EZ_setChar(0, COLOR_4, 320, 32,
data)` ; banc CRAM 0 = PLAYPAL (contrat §2). **Source des pixels** : `build/doom/doom_art.h`, tableau C
généré par `tools/doom2ps/wad2hud.py` (SPEC_CONVERTER §3bis ; règle Makefile contrat §8, `-I$(BUILD)`
Makefile:110) — même forme que `stat_bar` (STATBAR.C:74 : en-tête `{w, h}` + index 8 bpp) : `doom_stbar[8+10240]`,
`doom_faces[26][768]`, `doom_keys[3][64]`. La feuille VDP2 (STATIC.DAT bloc 2) n'est utilisée par **aucun**
élément du HUD Doom et reste à zéro. `EZ_normSpr(DIR_NOREV, COLOR_4, 0x4000, 0, &statusbar, NULL)`
inchangé `:1322`. Les gauges `:1225-1313` (erase bar, ammo bar, health rect, sparkle) et le compas
`:1324-1337` disparaissent ; `nmFullBowls`/`redrawBowlDots` `:1168-1196` ne sont plus appelés.

### 3.3 Polices 4 bpp (conversion `[src] PRINT.C:51-80`)

Format lu par `initFonts` : `short hauteur` (gros-boutiste), **32 o = CLUT 16 couleurs** BGR555
(`EZ_setLookupTbl`, `:63`), **256 largeurs** (`:64-65`), puis par caractère de largeur ≠ 0 :
`hauteur × ceil(largeur/2)` octets 4 bpp (`:73-75`, complété à un multiple de 8 px `:76-77`) ; index 0
transparent ; `drawString` avance de `largeur+1` (`PRINT.C:145`). Trois polices, `fontList[]`
(`PRINT.C:17`) redéfinie sous `GP_GAME_DOOM`, `MAXNMFONTS 4` :

| font | lumps `[wad]` | palette | usage |
|---|---|---|---|
| 1 (`MESSAGEFONT` `:1356`) | `STCFN033..095` 64 glyphes, h ≤ 8, l 4-9 | 15 rouges quantifiés (`tools/duke2ps/quantize.py`) | messages HU (§3.6), chiffres rouges Mimas-style si voulu |
| 2 | `STTNUM0-9` 14×16 (`1` = 11), `STTPRCNT` 14×16 → '%', `STTMINUS` → '-' | rouge | santé, armure, munitions courantes |
| 3 | `STYSNUM0-9` 4×6, `STGNUM2-7` 4×6 (gris, seulement pour le fond) | jaune | munitions ×4, arms |

`initFonts(4, 0xE)` à `:1902` (masque = bits 1-3 ; `fontMask &= ~1` `:55`). Générateur :
`tools/doom2ps/wad2font.py` (appelé par `wad2hud.py`, SPEC_CONVERTER §3bis) → les trois polices au format
PRINT.C:51-80 dans **`build/doom/doom_art.h`** (`doom_font_stcfn[]`, `doom_font_sttnum[]`, `doom_font_stysnum[]`),
inclus par `PRINT.C` comme `FONT1.H` — `build/doom2ps/` n'est **pas** sur le chemin d'inclusion, `$(BUILD)` l'est
(Makefile:110). Chiffres **cadrés à droite, pas fixe 14** (`STlib_drawNum` utilise la largeur
du `0`) : `drawStringFixedPitch(x − 14·n, y, 2, s, 13)` (`PRINT.C:164-178`).

### 3.4 Champs (`[doom]` st_stuff.c:145-202, `y_e = y_doom − 88`)

| champ | Doom (x, y) | moteur (x_e, y_e) | source | police |
|---|---|---|---|---|
| munitions courantes | 44, 171 (bord droit) | −116, 83 | `ammo[weaponinfo[ready].ammo]` ; rien pour le poing | 2 |
| santé % | 90, 171 | −70, 83 + '%' | `currentState.health` | 2 |
| arms 2-7 | 111+12·(i%3), 172+10·(i/3) | −49…, 84… | `weaponOwned` bit i ⇒ jaune, sinon gris du fond | 3 |
| armure % | 221, 171 | 61, 83 | `armorPoints` | 2 |
| clés | 239, 171/181/191 | 79, 83/93/103 | `keyMask` (contrat §6) — 3 chars 8×8 COLOR_4 (`STKEYS0-2` 7×5), slots 1-3 (ex-compas) | — |
| munitions ×4 / max | 288 et 314, 173/179/191/185 | 128 / 154, 85/91/103/97 | `ammo[]`, `maxAmmo[]` ordre clip/shell/misl/cell | 3 |

### 3.5 Visage (écart c)

Jeu minimal : `STFST{0-4}{0-2}` (droit ×3 par palier), `STFKILL0-4` (touché), `STFEVL0-4` (sourire),
`STFDEAD0` = 26 patches, **tous 24 px de large, h 29-31 `[wad]`** → un char 8 bpp **24×32 = 768 o**
re-uploadé par `EZ_setChar(4, COLOR_4, 24, 32, face)` au changement d'index (pixels convertis une fois
au chargement, 26 × 768 = 20 Ko en work RAM), dessiné à `(−17, 80)` (`ST_FACESX 143`, `y 168`).
Logique `ST_updateFaceWidget` réduite (st_stuff.c:684-700, 707-875) : palier `((100−h)·5)/101`
(`:694`) ; touché ⇒ `STFKILL` 35 tics (`ST_TURNCOUNT`) ; nouvelle arme ⇒ `STFEVL` 70 tics
(`ST_EVILGRINCOUNT`) ; sinon droit, index `P_Random % 3` toutes les 17 tics ; mort ⇒ `STFDEAD0`.
Ledger VDP1 : −13 440 (`stat_bar`) −1 920 (compas) + 10 240 + 768 + 192 + ~3 900 (3 polices) ≈ **−260 o**
: le slot 8 bpp libéré (`initPicSystem(i, {28, 31, …})` `:1903` → `{28, 30, …}`, classe `TILE8BPP`
`PIC.H:6`) est la **marge** à prendre seulement si `EZ_setChar` échoue (`assert(EZ_charNoToVram(_picNmBase-1))`
dans `initPicSystem`, `PIC.C:221` ; `:233-237` est `resetPics`).

### 3.6 Message HU

`changeMessage(char *)` `:1359-1363` + `drawMessage` `:1365-1403` : sous `GP_GAME_DOOM`, `drawString(−160,
−112, 1, msg)` (`HU_MSGX 0, HU_MSGY 0` hu_stuff.h:35-36) sans Gouraud, durée `4·35` tics
(`HU_MSGTIMEOUT` `:40`, contre `60·5` trames `:1401`). Textes = `GOT*` de `d_englsh.h` recopiés dans
`DOOM_GAME.C` (`getText(LB_ITEMMESSAGE, n)` `:1593` non utilisé).

### 3.7 Automap (`MAP.C:116-250`)

Couleurs par Δ hauteur (`:157-176`) → table Doom (am_map.c:50-83, PLAYPAL → BGR555) : mur
plein `REDS 176`, portail à Δsol `BROWNS 64`, à Δplafond `YELLOWS 231`, invisible/1s `GRAYS 96+3`,
joueur `WHITE 209` (`:245-247`), non vu = non tracé (`SECFLAG_SEEN` `:160-163` = `ML_MAPPED`) ; fond
noir. Type de mur = `level_wall[w].nextSector` (−1 = plein) + comparaison des `floorLevel`/`ceilLevel`
des deux secteurs. Pas de grille ni de marques (plan §2.5).

## 4. Son

### 4.1 Résolution `sfx → index` (contrat §3)

```c
/* DOOM_SOUND.C — SEULE définition (SPEC_RUNTIME §8 ne fait qu'appeler) ; déclarées dans DOOM.H */
int  doom_sfxIndex(int sfx);      /* statique : level_staticSoundMap[ST_JOHN]+n ; sinon level_objectSoundMap[sfx] ; <0 = absent */
void doom_sound(Sprite *s,int sfx);   /* spriteMakeSound(s, idx) ; s == NULL ⇒ playSound(0, idx) (SPEC_RUNTIME §8) */
void doom_playerSound(int sfx);   /* playSound((int)player, idx)  -- source = l'objet joueur */
void doom_posSound(Object *src,MthXyz *pos,int sfx);   /* posMakeSound(src, pos, idx) SOUND.C:415-421 */
```

`doomStaticSfx[20]` (contrat §4) ordonne `PISTOL SHOTGN PUNCH SWTCHN SWTCHX NOWAY DOROPN DORCLS PSTART
PSTOP PLPAIN PLDETH OOF ITEMUP WPNUP SLOP BAREXP TELEPT STNMOV RLAUNC` ; `playStaticSound(ST_JOHN, n)`
(`SOUND.H:39`) reste utilisable. Appels de ce document :

| événement | sfx | site moteur | Doom |
|---|---|---|---|
| tir pistolet/chaingun, fusil, poing | `sfx_pistol`, `sfx_shotgn`, `sfx_punch` (poing : seulement si touche) | verbes §2.2 | p_pspr.c:661, 684, 480 |
| *use* refusé / interrupteur | `sfx_noway`, `sfx_swtchn`/`swtchx` | `push()` `:992` ; `OT_SW1` AI2.C:582-596 | p_map.c:1184 ; p_switch.c:212-216 |
| portes / ascenseurs | index 6-9 (`ST_PUSHBLOCK`) | AI.C:4336-4673 tels quels | contrat §3 (écart DORCLS accepté) |
| joueur touché / mort / chute | `sfx_plpain`, `sfx_pldeth`, `sfx_oof` (atterrissage `momz < −8` p_mobj.c:324) | `doom_playerDamage`, `doom_playerFrame` (via `camera->floorSector` −1 → ≥ 0) | p_inter.c ; p_mobj.c |
| ramassage | `sfx_itemup` (défaut :354), `sfx_wpnup` (armes) | `doom_playerGetObject` §4.3 | p_inter.c:335-660 |
| monstres, projectiles | dynamiques | runtime acteurs (J2) | — |

### 4.2 Les deux écarts (`[src]`)

| écart | moteur | Doom | retouche (si audible) |
|---|---|---|---|
| dédoublonnage | un one-shot **par son et par trame**, toutes sources (`SOUND.C:320-329`, `lastFrameUsed`) | 8 canaux, doublons libres | clé `(source, sNm)` : comparer aussi `slotOwner` — 3 lignes dans `playSoundE` |
| priorité | `silenceVoice` `:58-76` prend le slot suivant en round-robin (saute 16/17 et les boucles) : un son en cours peut être coupé | éviction du **moins prioritaire** (`S_getChannel`, `sfxinfo.priority` sounds.c:120-212) | garder ; à la rigueur réserver 2 slots aux sons joueur (`slot == 18,19` sautés + `playSoundE` variante) — 20 lignes |

Volume/pan de position : `posGetSoundParams` `:392-413` (`vol = dist/32 − 15`, coupure 255 ⇒ ~8 640 u)
contre `S_CLIPPING_DIST 1200` (s_sound.c:57) : accepter (le moteur porte plus loin, monotone).

### 4.3 Ramassages (`doom_playerGetObject`, remplace `playerGetObject` `:1583-1799`)

```c
int doom_playerGetObject(int mt,int dropped);   /* 1 = consommé (greenFlash :1796 → bonusCount += 6) */
```

**Appelant unique : `doom_item_func` (SPEC_RUNTIME §6)**, qui détecte la collision joueur/objet par
`moveSprite` (IMMOBILE, AI.C:3819-3820) et appelle `doom_playerGetObject(item->mt, item->mflags & DF_DROPPED)` ;
retour 1 ⇒ `delayKill` côté runtime. Cette fonction ne fait que les **effets** : inventaire (`doomPlayer`),
son `sfx_itemup`/`sfx_wpnup`, message `GOT*`, visage, `bonusCount`. (`game_placeObject` est le placement,
pas le ramassage.)

`[doom]` p_inter.c : `BON1` +1 ≤ 200 `:379-384` ; `BON2` armure +1 ≤ 200, type 1 si 0 `:387-395` ;
`ARM1`/`ARM2` `P_GiveArmor(1|2)` `:248-260, 366-375` ; `STIM` +10, `MEDI` +25 ≤ 100 (`P_GiveBody`
`:225-241`, message `GOTMEDINEED` si < 25) ; `CLIP` 10 (5 si `dropped`) `:533-544` ; `AMMO` 50, `SHEL` 4,
`SBOX` 20 (`P_GiveAmmo` `:68-103`, refus au max) ; `SHOT` : `P_GiveWeapon` `:162-215` = arme + 2 clips
(8 cartouches, 1 clip si `dropped`), `pendingWeapon = wp_shotgun`, `sfx_wpnup`, visage sourire.
Le `MF_DROPPED` vient du runtime acteurs (`A_Fall`/`P_KillMobj` p_inter.c:745-763 : POSS lâche CLIP,
SPOS lâche SHOT).

### 4.4 Musique CDDA

`playCDTrackForLevel(lev)` `SOUND.C:387-390` → `trackMap[]` `:374-380` ; sous `GP_GAME_DOOM` :
`doomTrackMap[] = {2..10}` pour E1M1-E1M9, 11 `D_INTER`, 12 `D_INTRO`, 13 `D_VICTOR` (13 MUS `[wad]`),
`playCDTrack(track, 1)` (`FILE.C:275-289`, `PMODE 0x0f` = boucle). Chaîne PC (toolchain à installer,
sortie dans `build/doom/cdda/`, jamais commitée) :

```
python tools/doom2ps/wad_extract.py DOOM1.WAD D_E1M1 build/doom/cdda/D_E1M1.mus   # lump brut (17 283 o)
mus2mid build/doom/cdda/D_E1M1.mus build/doom/cdda/D_E1M1.mid                    # Chocolate Doom tools
fluidsynth -ni -g 0.7 -r 44100 -F build/doom/cdda/D_E1M1.wav <soundfont.sf2> build/doom/cdda/D_E1M1.mid
sox build/doom/cdda/D_E1M1.wav --norm=-1 -t raw -r 44100 -e signed-integer -b 16 -c 2 -L build/doom/cdda/02.raw pad 0 2
python tools/mkcue.py build/doom/slavedriver.iso build/doom/cdda/*.raw > build/doom/slavedriver.cue   # pad 2352, TRACK nn AUDIO, PREGAP 00:02:00
```

`mkiso` (`Makefile:280-300`) ne produit que la piste 1 : `tools/mkcue.py` (nouveau, ~40 lignes) écrit
le `.cue` multi-pistes ; sans pistes audio `playCDTrack` est inoffensif (cas `TOMB_e1m1`, contrat §9).

## 5. Touch points moteur (tous sous `#ifdef GP_GAME_DOOM`)

| fichier:ligne | aujourd'hui | Doom |
|---|---|---|
| `SRUINS.C:960` | `controlInput(input,changeInput)` | `doom_playerFrame(input,pushed)` (§1.2) ; `doom_playerTic` est appelé par la boucle de tics SRUINS.C:2142 (`CFG_PLAYER_TIC`, SPEC_RUNTIME §9) |
| `SRUINS.C:977-990` | tir sur file vide, `weaponUp/Down` + `redrawBowlDots` | `#else` ; sélection dans `doom_playerTic` |
| `SRUINS.C:1026-1037, 1043` | `yaw += yavel`, pitch, `weaponPlayerMove` | `yaw` posé par le tic ; `pitch = roll = 0` |
| `SRUINS.C:1039` | `moveCamera()` | + relecture `mom = vel × 60/35` juste après |
| `SRUINS.C:159-178` (`playerHurt`) ; `AI.C:33-34` | `health −= hp`, ouch (`3+i` :174) | `doom_playerDamage` (§1.3) — `playerHurt` non compilé, pas de `CFG_OUCH` ; la branche joueur de `doom_damage` (SPEC_RUNTIME §4) appelle la même fonction sans armure |
| `SRUINS.C:236-245` | dégâts de chute | `return` |
| `SRUINS.C:962-975` | mort | garder, **gardé par `if (!playerIsDead)`** ; `:967` `playStaticSound(ST_JOHN,1)` → `CFG_DEATH_SFX` (11 = PLDETH, pas SHOTGN) ; tueur = `source` (§1.3) |
| `SRUINS.C:1206-1338` | `drawStatBar` | `doom_drawStatBar()` (§3.2-3.5) |
| `SRUINS.C:1359-1403` | `changeMessage`/`drawMessage` | `doom_setMessage`/`doom_drawMessage` (§3.6) |
| `SRUINS.C:1583-1799` | `playerGetObject` | `doom_playerGetObject` (§4.3) ; **appelant = `doom_item_func`** (SPEC_RUNTIME §6, collision) |
| `SRUINS.C:1878, 2433` ; `WALLS.C:128-131` ; `MAP.C:127-129` | 240 lignes | 224 (§3.1) |
| `SRUINS.C:1888-1897` | `stat_bar` + 3 compas | STBAR char + 3 clés + visage (slots 0, 1-3, 4) |
| `SRUINS.C:1902-1903` | `initFonts(4,3)`, `{28,31,1,10,12}` | `initFonts(4,0xE)` ; `{28,30,…}` seulement si besoin |
| `SRUINS.C:2010` | `initWeapon()` | + `doom_playerInit()` |
| `SRUINS.C:2016` ; `SOUND.C:387-390` | `trackMap[lev]` | `doomTrackMap[]` |
| `SRUINS.C:2207` | `runWeapon(...)` | `advanceWeaponSequence(cx,cy,0)` seul (§2.2) |
| `SRUINS.C:2211-2214` | `drawMessage`, `drawStatBar`, `drawAirMeter` | Doom ×2, air supprimé |
| `WEAPON.C:141-260, 574-626, 785-921` | `fireWeapon`, `weaponFire`, `runWeapon` | non compilés (`#ifndef`) ; `weaponUp/Down :973-993` corps Doom |
| `SEQUENCE.C:155-163` (+nouveau) ; `:252-253` ; `:256-262` ; `:297` | queue ; overlay ≥ 50 ; clip 210 ; `240/2` | `setWeaponSequence` ; retiré ; 192 ; `224/2` |
| `PRINT.C:11-17` | `MAXNMFONTS 3`, `fontList` | 4, polices Doom |
| `SOUND.C:320-329`, `:58-76` | dédup, round-robin | retouches §4.2, **seulement si audible** |
| `AI.C:4673` | `+1` à l'arrêt d'ascenseur | `+3` (PSTOP, contrat §3) |

Rien d'autre du moteur n'est touché par **ce** document ; `PLRCYL.C`, `OBJECT.C`, `HITSCAN.C`, `PIC.C` inchangés.
Les touch points partagés avec le joueur mais **possédés par SPEC_RUNTIME §9** : `AI.C:4304` (seuil SHORTOPENING
80 → `GP_PLAYER_FIT_HEIGHT` 56 — sans lui le joueur `SPRITEFLAG_BSHORT` AI.C:47 ne franchit **aucune** porte
d'E1M1), `AI.C:4660-4663` (réarmement de l'ascenseur WR), `WALLS.C:2701` (`F(32)` → `F(10)`), `SRUINS.C:2142`
(`CFG_PLAYER_TIC`), BIGMAP.C:84 / SRUINS.C:2491-2500 / INITMAIN.C:185-246 (noms, warp, logos).

## 6. Ordre d'implémentation (tâches ≤ 1 j)

| # | tâche | critère PC | vu sur console → sens |
|---|---|---|---|
| 1 | `DOOM.H` (typedef `DoomPlayer` unique, `DoomAction` union) + `doom_playerTic/Frame`, crochet `SRUINS.C:960` + `CFG_PLAYER_TIC` (SPEC_RUNTIME §9), constantes §1.2, saut/pitch/chute coupés | compile ; test unitaire PC de l'intégrateur (`tools/doom2ps/sim_player.py` : 35 tics de course = 16,67 u/tic asymptote, Doom identique) | traversée départ → porte de sortie à ±10 % du PC (plan §2.4) ; **glisse** trop longue ⇒ relecture `mom` absente ; virage « mou » ⇒ `yavel` encore lu |
| 2 | santé/armure/dégâts/mort, flash rouge proportionnel, sons joueur (statiques) | `assert` de bornes ; grep des 19 index statiques | barre rouge à chaque coup, `plpain` à chaque coup, mort = caméra tombe et `pldeth` ; pas de son ⇒ `doom_sfxIndex < 0` (liste STATIC) |
| 3 | machine psprite + `setWeaponSequence` + pistolet complet (tir, flash, refire, bob) | `verif_doom.py` : `wseq` de chaque état atteignable < `nmSequences` ; simulateur d'états PC (tics cumulés = 14) | pistolet monte en ~0,5 s, tire à 2,5 coups/s en maintenant, flash 1 tic ; arme figée ⇒ `sequenceOver` mal remis ; arme qui « saute » ⇒ `chunky` (contrat §5) |
| 4 | hitscan/autoaim/puff/blood + poing + fusil | PC : histogramme des dégâts `P_GunShot` sur `rndtable` = Doom | zombie tué en 1-3 balles (20 hp), imp 3-5 (60 hp ; par table) ; balle qui passe **sous** un monstre en contrebas ⇒ pente non prise (`autoTarget`) |
| 5 | sélection, munitions, ramassages, `weaponUp/Down`, sac/refus au max | PC : table §4.3 rejouée sur les 11 types d'E1M1 | fusil ramassé ⇒ 8 cartouches, `wpnup`, sourire ; clip refusé à 200 ; « picked up » par `drawString` |
| 6 | cadre 224, STBAR char, polices 3 (`wad2font.py`), chiffres, arms, clés | image de la barre rendue en PC (`tools/doom2ps/render_hud.py`) = `STBAR` + chiffres au pixel | barre collée au bas, vue 192 l., pas de bande ; chiffres décalés ⇒ `y_e = y_doom − 88` ; arme coupée ⇒ clip 192 |
| 7 | visage 24×32, logique réduite, ledger VDP1 | `assert(EZ_charNoToVram)` passe avec `{28,31,…}` | visage change par palier de 20 hp ; visage = texture de mur ⇒ éviction : passer `{28,30,…}` |
| 8 | automap couleurs Doom, message HU, 224 dans `MAP.C` | **image PC** `tools/doom2ps/render_map.py` : murs d'E1M1 colorés par `nextSector`/Δ`floorLevel`/Δ`ceilLevel` depuis le `.LEV` relu = la carte Doom de Mimas au pixel près (mêmes 4 couleurs) | murs rouges, sols bruns, plafonds jaunes ; blanc partout ⇒ `nextSector` non lu |
| 9 | roquettes (`doom_spawn(MT_ROCKET)`) + `A_Explode` côté runtime, chaingun (E1M2, tuiles STATIC) | `verif_doom.py` : tuiles CHGG/MISG présentes dans STATIC | — (hors E1M1 ; se teste sur E1M2/E1M3 à J5) |
| 10 | CDDA : `mkcue.py`, chaîne MUS→raw, `doomTrackMap` | `.cue` valide (`bchunk`/`cdrdao --check`), durée D_E1M1 ≈ 1 min 40 | musique boucle ; silence ⇒ pas de piste 2 (`.cue`) ; grésillement ⇒ endianness `-L` |
| 11 | écarts son §4.2 **si** audibles après 1-10 | **simulateur PC** du dédoublonnage : rejouer `playSoundE` (SOUND.C:320-329, clé `(source, sNm)`) sur une trace de 2 POSS tirant au même tic ⇒ 2 sons ; priorité : trace `silenceVoice` round-robin sur 32 slots ⇒ aucun son joueur coupé si 2 slots réservés | deux zombies tirant ensemble n'émettent qu'un coup ⇒ retouche dédup ; cri de mort coupé ⇒ retouche priorité |

Total ≈ 9-11 j, cohérent avec J3 (12-15 j, dont la part convertisseur) + J4 (6-8 j) du plan §6.
