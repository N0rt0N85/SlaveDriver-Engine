# tools/study — scripts de mesure des études 2026-09-12

Hors commits (comme `tools/duke2ps/`). Lancer depuis la racine du fork.

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
