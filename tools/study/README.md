# tools/study — scripts de mesure

Lancer depuis la racine du fork. Chemins d'entrée en dur dans chaque script.

## Gisements de la conversion (2026-09-18)

Mesurés sur **nos** `.LEV` (`cd_doom/E1M1.LEV`, `cd_duke/TOMB.LEV`), pas sur le retail — la
différence est le résultat : E1M1 converti est fait de sols (murs 22,2 % / sols 46,6 % /
plafonds 31,2 % des cellules), l'inverse du retail (66,8 / 19,6 / 13,5).

| script | ce qu'il mesure | résultat |
|---|---|---|
| `balayage_ordre.py` | TOURS, PAS, budget et plafond de `tools/ordre.py` | les trois réglages sont au plafond ; toutes les paires = 86 %, pire que rien |
| `debord.py` | redondance du débord des sols et plafonds | ×1,179 (pas ×1,57) ; 321 faces sur 2 985 couvertes par les autres, union inchangée après retrait ; TOMB 0 |
| `debord_sur.py` | **et ces faces-là, peut-on vraiment les retirer ?** | **NON : 311 sur 319 ouvrent un trou**, 1 073 positions sur 1 089 en verraient un, pire face 4 096 u². Le gisement du débord est CLOS par cette voie |
| `bandes_plafond.py` | jusqu'où la soudure de faces peut aller | 54,4 % aujourd'hui, **81,1 %** par réordonnancement seul, 95,7 % avec le virage ; tessellation = 51 jointures |
| `soldom_vue.py` | le plan dominant, par position debout et non par saut de portail | 140 cellules médianes retirées (19,6 % de l'image), p90 324 |
| `soldom_secteur.py` | le même, élu par SECTEUR, avec tolérance de lumière et hystérésis | E1 : médiane 22,5 % de l'image (E1M8 70 %) ; aucun secteur ne porte deux plans ; marge x1,6 = 0,5-1,3 % du dépôt |

## Études 2026-09-12

| script | ce qu'il mesure | doc |
|---|---|---|
| `floorshare.py` | part des cellules sol/plafond dans les 24 `.LEV` retail | VDP2_DOMINANT_FLOOR §1 |
| `domfloor2.py` | part du couple (hauteur, tuile) dominant, sols et plafonds | VDP2_DOMINANT_FLOOR §1 |
| `unif.py` | uniformité de texture des murs-sol PARALLELOGRAM | VDP2_DOMINANT_FLOOR §1 |
| `neigh.py` | couverture du plan dominant dans un voisinage de 1/2/3 portails | VDP2_DOMINANT_FLOOR §1 |
| `ktab.py` | identifie la table K du ciel (atan par COLONNE, 320 entrées) | VDP2_DOMINANT_FLOOR §2 |
| `wadstat.py` | sous-secteurs / segs / secteurs des cartes Doom vs limites moteur | DOOM_ON_SLAVEDRIVER §3 |
| `wadtex.py` | textures+flats distincts par carte Doom | DOOM_ON_SLAVEDRIVER §4 |
| `wadcache.py` | textures distinctes dans un voisinage de 0..3 secteurs vs cache 28 | DOOM_ON_SLAVEDRIVER §4 |

`domfloor.py` est une **version FAUSSE** conservée pour mémoire : elle ne comptait les tuiles
que des murs PARALLELOGRAM (≈ 25 % des sols) et sortait 3,1 % au lieu de 13,8 %.

## `fullconv/` — scripts de l'étude « conversion totale » (2026-09-13)

Mesures des rapports `docs/study/full-conversion-2026-09-13/` (synthèse `docs/FULL_CONVERSION_PLAN.md`) :
`r1_wad_dir.py` (lumps 2D/sons/sprites de DOOM1.WAD), `r2_wadstat.py`, `r3_doom_measure.py` /
`r3_duke_measure.py` / `r3_static_measure.py` (sprites→chunks, sons, STATIC.DAT, ART), `r4_count.py`
(appels jfduke3d → moteur Build), `parsemap.py` / `wadmem.py` / `wadassets.py` / `fields.sh` (mémoire :
.map, structures de carte, champs lus par le playsim), `d2_playsim_wad.py` (.PSW par carte),
`j1_wadcheck.py` / `j2_wadcheck.py` / `j3_check.py` (vérifications des juges). Chemins d'entrée en dur
dans chaque script (DOOM1.WAD de Mimas, DUKE3D.GRP de `refs/build/duke13/`).
