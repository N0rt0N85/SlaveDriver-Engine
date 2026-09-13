# E0 — écrivain `.LEV` « identité » (2026-09-10)

Code : `tools\duke2ps\lev_io.py` (modèle de contenu, construit sur `tools\lev.py` sans le modifier) et
`tools\duke2ps\lev_write.py` (sérialiseur + suite d'acceptation). Sorties : `build\duke2ps\e0\`.
Rien de suivi n'a été modifié, rien n'a été commité. ⚠ `tools\duke2ps\` n'est **pas** ignoré par git
(`build/` l'est) : il apparaît comme non suivi.

## Reproduire

```powershell
python tools\duke2ps\lev_write.py                        # suite complète, 24 fichiers, ~13 s, code retour 0 si tout passe
python tools\duke2ps\lev_write.py --emit build\duke2ps\e0\identity   # idem + écrit les 24 fichiers ré-émis
python tools\duke2ps\lev_write.py --copy IN.LEV OUT.LEV  # parse puis ré-sérialise un fichier (écriture atomique)
```

Rapport machine : `build\duke2ps\e0\e0_report.json`. Fichier muté sur disque : `build\duke2ps\e0\TOMB_M1.LEV`.

## Principe (ce qui garantit qu'on ne recopie pas le fichier)

- `lev_io.model_from_bytes` appelle les `parse_*` de `lev.py` sur un tampon mémoire (`MemReader`, car
  `lev.Reader` exige un chemin), puis décode la queue (ciel, sons, palettes, tuiles, séquences) avec **son
  propre parcours**. Chaque offset de ce parcours est comparé à celui de `lev.py` (assert) : deux lectures
  indépendantes, et elles concordent sur les 24 fichiers.
- Le **modèle** ne contient que du contenu : aucun offset, aucun compteur, aucune taille, aucune copie du
  fichier. L'écrivain **recalcule** tout ce qui se déduit du contenu : `size` = 56 + Σ LOADPART (LEVEL.C:37-68),
  les 14 compteurs du `sLevelHeader` (SLEVEL.H:5-20), le nombre de sons et la taille de chaque PCM
  (SOUND.C:203-236), la taille du bloc palettes (PIC.C:614), le nombre de tuiles (PIC.C:675), la taille des
  RLE (PIC.C:572/586/601), l'en-tête et la taille du tampon séquences (SEQUENCE.C:29-48). L'identité sha1
  valide donc à la fois l'ordre des champs **et** ces dérivations.
- **Gardé en brut** (`bytes`), parce que le chargeur le copie sans le décoder : bitmap du ciel (→ VRAM A1,
  PLAX.C:94), PCM (→ SCSP, SOUND.C:220-223), pixels 8 bpp des tuiles 16 bpp (index de palette,
  PIC.C:518/552), flux RLE (passés tels quels à `addPic`, PIC.C:576/590/604). **Mesure** : 21 898 813 octets
  sur 34 426 267 (63,6 %) des 24 fichiers. Ce n'est pas de la structure : ce sont des images et du son.
- **Tableaux d'octets** déclarés `unsigned char` par le moteur (`texture`, `vertexLight`, `objectParams`,
  `cutPlane`, lignes de 128) : stockés en listes d'entiers, re-sérialisés élément par élément (356 040 o,
  1,03 %). La grammaire de `objectParams` (propre à chaque type d'objet, OBJECT.C) **n'est pas décodée**.
- Tout le reste = champs de structure (secteur 24 o, mur 48, sommet 8, face 10, objet 4, PB 18, PBVert 4,
  WaveVert 16, WaveFace 8, PBWall 2, frame 8, chunk 8, cartes de 227 shorts, palettes 256 × u16, table ciel
  320 × int, en-têtes de sons et de tuiles).

## Résultats — MESURE (suite du 2026-09-10, 12,4 s)

| critère | résultat |
|---|---|
| C1 identité octet pour octet (sha1 sortie = sha1 entrée) | **24/24** |
| C2 aller-retour modèle `parse(write(parse(x))) == parse(x)` | **24/24** |
| C3 aller-retour structure `lev.py` (offsets, en-tête, size, tuiles, sons) | **24/24** |
| C4 `lev.validate() == []` sur les fichiers ré-émis | **24/24** |
| C5 `size == 56 + Σ parts` (LEVEL.C) | **24/24** |
| C6 aucun assert chargeur violé (`engine_problems`) | **24/24** |
| C7 mutations : seul le champ visé change, fichier valide | **21/21** |
| C8 témoins négatifs : modèle invalide refusé, puis identité retrouvée | **5/5** |

Les 24 fichiers de `refs\extract\PS\` (tailles et sha1 dans la sortie console et le JSON) : **24**, pas
23 : `TEST.LEV` et `SANCTUAR.LEV` sont identiques (sha1 `6ef35941…`). Le « 23/23 » du tableau E0 du plan
compte donc les niveaux distincts.

Mutations (fichiers KILENTRY = donneur, TOMB = emplacement, SUNKEN = le plus gros, THOTH = 82 cut-sectors) :
- M1 `sectors[n/2].floorLevel += 16` → diff du modèle = ce seul champ ; octets modifiés tous dans
  `off_secteurs + 24k + 10..11` (ex. TOMB secteur 98, 592 → 608, 1 octet changé à 135 295).
- M2 `vertices[n/3].x += 1` → seul ce champ ; octets dans `off_sommets + 8j + 0..1`.
- M3 `sequences.chunks[0].tile += 1` (le champ qu'E5 décalera de +N) → seul ce champ.
- M4 ajout d'un sommet → `nmVerticies` +1 (seul compteur changé), `size` +8 (TOMB 295 907 → 295 915),
  toutes les parts suivantes, sons et séquences décalés de +8, `validate` OK.
- M5 ajout d'une palette → bloc palettes = 2 + 512 × (N+1) (TOMB 9 730 → 10 242 = 2 + 512 × 20) ;
  c'est la formule du critère E4, vérifiée ici.
- M1 sur disque : `write_file` → `build\duke2ps\e0\TOMB_M1.LEV` → relu par `lev.parse_lev(path)` (le
  point d'entrée fichier de `lev.py`) : `validate` OK, seul le champ visé diffère.

Témoins négatifs (sur KILENTRY) : `floorLevel = 40000` → ValueError (dépassement de `short`, pas de
troncature silencieuse) ; `frame.pad = [1,0]` → refus (SEQUENCE.C:54-55) ; drapeaux de tuile 0x99 → refus
(PIC.C:700) ; 601 secteurs → refus (UTIL.H:21). Modèle restauré ensuite : sha1 identique.

`lev_write.py --copy` testé sur KILENTRY : 673 815 o, sha1 `8d9d9a5f…` = entrée.

## SOURCE (ordre et contrôles implémentés)

Ordre du fichier : `initPlax` PLAX.C:84-115 → `loadLevel` LEVEL.C:37-67 → `loadDynamicSounds`
SOUND.C:243-248 (`loadSoundSet` :230-241, `loadSound` :199-209) → `loadTiles` PIC.C:713-716
(`loadPalletes` :611-625, `loadTileSet` :671-707) → `loadSequences` SEQUENCE.C:24-85. Dans le tampon
séquences, l'ordre réel est **en-tête, frames, chunks, liste des séquences, carte OT_NMTYPES**
(SEQUENCE.C:50-72), et non l'ordre des termes de l'assert :44-48. Bloc palettes = 1 short (numéro de la
palette objet, PIC.C:630) + N × 256 u16 ; mesuré « 2 + 512·N » sur les 24 fichiers (sinon `lev_io` lève
une erreur).

`engine_problems` (refus en mode strict, par défaut) : ciel 512×256, palette 256, table 320
(PLAX.C:84-115) ; 0 < size < 900 000 (LEVEL.C:40-41) ; ≤ 600 secteurs, ≤ 5 500 murs (UTIL.H:21-22) ;
carte des sons = 227 (SOUND.C:233), 0 ≤ sons < 80 (SOUND.C:237) ; 0 < bloc palettes < 1 Mo (PIC.C:615) ;
palette objet et `palNm` dans les bornes (PIC.C:517/551/605/630, lecture hors tableau sinon, pas un
assert) ; drapeaux de tuile connus (PIC.C:700) ; RLE 1..32 767 (short + assert PIC.C:573/587/602) ;
0 < séquences < 1 Mo (SEQUENCE.C:30), pads nuls (:54-55, :65), `sequence[0] == 0` (:80).

## HYPOTHÈSES et limites connues

- Signes : repris de `lev.py` (ex. `wall.flags` en `short` signé). Un mur avec EXPLODABLE (0x8000) doit
  être donné en négatif (−32 768 + …) : l'écrivain **refuse** 0x8000 au lieu de le replier. E3 doit
  construire ses drapeaux en conséquence (ou masquer puis signer).
- Non vérifiable sans `STATIC.DAT` / la mémoire : total sons statiques + dynamiques < 80 (43 statiques
  selon RETAIL_DISCS §4, donc ≤ 36 dynamiques), SCSP < 512 Ko (SOUND.C:218), plafond de 50 images VDP2
  (PIC.C:683, 18 dans STATIC.DAT), demande mémoire (critère E5). Ces contrôles relèvent d'E5.
- La grammaire de `objectParams` et le sens des entrées paires de `texture` ne sont pas décodés (seules
  les entrées impaires sont connues : index de tuile, LEVEL.C:69-70). Ça suffit pour E0 (octet pour
  octet) ; E5 devra écrire le bloc de l'objet joueur (5 shorts, OBJECT.C:191-234) à la main.
- Le modèle n'a pas de constructeur « à partir de zéro » : E3/E5 partiront d'un modèle donneur
  (`lev_io.read_model`) dont ils remplaceront `level`, et ajouteront tuiles et palettes.
- Aucun test console : l'identité octet pour octet rend la chose inutile pour les fichiers retail, mais
  les fichiers générés en E5 devront passer la table §4 du plan.
