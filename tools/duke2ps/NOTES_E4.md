# E4 — les vraies textures de Duke (2026-09-12)

`tools\duke2ps\duketiles.py` → `build\duke2ps\e1l1_tiles.json` (+ planche `e1l1_tiles.png`).
Non suivi, jamais commité. Les textures restent de la recherche, jamais diffusées (LICENSE.TXT [4][A]).

## Ce qu'il a fallu savoir

**Format ART** (Ken Silverman) : `int32 artversion, numtiles, localtilestart, localtileend`, puis
`int16 tilesizx[n]`, `int16 tilesizy[n]`, `int32 picanm[n]`, puis les pixels bruts tuile par tuile
**en colonnes** — `pixel(x, y) = data[x*sizy + y]`. `PALETTE.DAT` : 768 octets R,G,B en 6 bits.

**Cible**, mesurée sur les 24 `.LEV` retail :

- une tuile de géométrie a les flags **0x32** (64×64 | 16BPP | PALLETE) et contient **4 096 octets
  d'indices 8 bits** — le « 16 bpp » qualifie la *palette*, pas les pixels ;
- une palette fait 256 entrées 16 bits **BGR555 avec le bit 15 posé**, sauf l'entrée **0** qui vaut
  `0x0000` : l'index 0 est la transparence (vérifié sur les 23 palettes de KILENTRY, 255/256 entrées
  avec le bit 15 dans chacune) ;
- la transparence de Duke est l'index **255** (`CONVERT.C:837-840, 1271-1277`), donc on **échange 0 et
  255**, dans les pixels *et* dans la palette.

`PIC.C:517` fait `pal = palletes + 256*palNm + 1` sans borne : le `+1` saute le `objectPalette` de
tête, et notre palette ajoutée en position 23 tombe exactement à la fin du bloc de 24 × 512 + 2 octets.

## Insertion dans le niveau

Nos tuiles passent **en tête** (indices 0..N−1), comme la géométrie retail qui n'utilise qu'un préfixe
à partir de 0, et les tuiles du donneur sont décalées de +N. Les **séquences** du donneur (arme, HUD,
effets) sont le seul autre consommateur du tableau de tuiles : leurs 655 morceaux ont donc leur champ
`tile` décalé de +N. Vérifié après écriture : géométrie 0..63 (tous flags 0x32, palNm 23), séquences
70..289, 0 indice hors bornes.

## Résultat (MESURE)

64 picnums demandés par la géométrie, **64 trouvés** dans les 1 605 tuiles du GRP, 0 manquant,
0 tuile entièrement transparente, 99,9 % de pixels opaques. La planche PNG montre bien les textures
de Hollywood Holocaust : la marquise du cinéma, l'affiche *Duke Nukem*, le flipper *Balls of Steel*,
l'étoile du Walk of Fame, le trottoir, la ville de nuit.

Coût mémoire : le bloc tuiles passe de 306 251 à **568 651** o, la **demande** de 915 746 à
**1 178 658** o — sous la cible de 1 250 000, avec 71 kio de marge.

```
fichier      1 355 183 o
bloc niveau    589 641 o
demande      1 178 658 o
sha1         836fb09aae7292b8792d634f5934ddf67c2e56b5
```

## Reproduire

```
python tools\duke2ps\duketiles.py          # -> build\duke2ps\e1l1_tiles.json
python tools\duke2ps\assemble.py           # les prend automatiquement si le JSON existe
python tools\duke2ps\assemble.py --tuiles ""   # revient a la boite grise du donneur
```

## Reste HYPOTHÈSE

- **Rééchantillonnage au plus proche voisin** vers 64×64. Les textures de Duke en 128×128 perdent la
  moitié de leur définition, et les non carrées sont déformées. Lobotomy, lui, découpait en
  sous-tuiles (la variante « 131 tuiles » du plan) — non fait.
- **Pas de répétition ni de panoramique.** `xrepeat`/`yrepeat`/`xpanning`/`ypanning` de Build sont
  ignorés : une texture couvre exactement une cellule de 64 u. L'échelle sera donc fausse là où Duke
  étirait ou répétait.
- **Aucun retournement de coin** (le motif vaut 0 partout), donc pas de miroir horizontal/vertical.
- La palette est celle de Duke sans les tables de lumière (`palookup`) : l'ombrage vient uniquement
  du gouraud par sommet, calculé depuis `shade` par une conversion linéaire non validée.
- Les 74 tuiles annoncées par le plan sont en réalité **64** pour E1L1 après la découpe convexe.
