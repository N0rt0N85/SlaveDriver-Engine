"""player_report.py -- statistiques et rapport Markdown du releve des ouvertures (player_openings.py).

Usage :
  python tools/duke2ps/player_dims.py
  python tools/duke2ps/player_openings.py
  python tools/duke2ps/player_report.py        -> build/duke2ps/player/openings_report.md (+ coverage.json)

Boule PowerSlave de rayon R (SPRITE.C) : centre = oeil (camera = sprite), flotte a R+8 au-dessus du sol
(SPRITE.C:642-644, +F(8) pour la camera), plafond a pos.y > plafond-R (SPRITE.C:703-708) -> il faut
h >= 2R+8 ("strict").  Sans le flottement (boule comprimee contre le sol) : h >= 2R ("optimiste").
Largeur : murs a planeDist < R (SPRITE.C:141) et aretes a distance < R (SPRITE.C:224) -> w_eff >= 2R.
"""
from __future__ import annotations
import csv
import json
import math
import os
import sys
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "duke2ps", "player")
CORPORA = ["d13", "atomic", "dukedc", "dz2", "xtreme_sp"]
POST = ["debout", "accroupi", "retreci"]
CLASSES = POST + ["infr_h", "infr_l", "bloque", "porte_mobile"]
RADII = [8, 10, 12, 14, 16, 18, 20, 21, 24, 28, 30, 31, 32, 36, 40, 44, 47]
DUKE_W = 41.0
HOVER = 8.0


def fl(s):
    return math.inf if s == "inf" else (math.nan if s == "nan" else float(s))


def load(name):
    with open(os.path.join(OUT, name), newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def pct(vals, p):
    v = sorted(x for x in vals if not math.isnan(x))
    if not v:
        return math.nan
    k = max(0, min(len(v) - 1, int(math.ceil(p / 100.0 * len(v))) - 1))
    return v[k]


def modes(vals, n=5, q=1.0):
    c = Counter(round(x / q) * q for x in vals if math.isfinite(x))
    return ", ".join(f"{k:g} ({v})" for k, v in c.most_common(n)) if c else "-"


def f1(x):
    if isinstance(x, float) and math.isinf(x):
        return "inf"
    if isinstance(x, float) and math.isnan(x):
        return "-"
    return f"{x:.1f}" if isinstance(x, float) else str(x)


def pc(a, b):
    return "-" if b == 0 else f"{100.0 * a / b:.1f} %"


def dist_row(label, vals):
    vals = [x for x in vals if not math.isnan(x)]
    fin = [x for x in vals if math.isfinite(x)]
    return (f"| {label} | {len(vals)} | {f1(pct(vals, 5))} | {f1(pct(vals, 10))} | {f1(pct(vals, 50))} | "
            f"{f1(pct(vals, 90))} | {modes(fin)} |")


def main():
    P = load("passages.csv")
    S = load("sectors.csv")
    dims = json.load(open(os.path.join(OUT, "duke_dims.json"), encoding="utf-8"))
    meta = json.load(open(os.path.join(OUT, "survey_meta.json"), encoding="utf-8"))
    for r in P:
        for k in ("len_u", "axis_f", "h_u", "step_u", "ceil_diff_u", "w_eff_u", "duke_w_req_u"):
            r[k] = fl(r[k])
        for k in ("partition", "blocked", "door_adj", "skyA", "skyB"):
            r[k] = int(r[k])
    for s in S:
        for k in ("hmin_u", "hmax_u", "area_u2", "w_sec_u"):
            s[k] = fl(s[k])
        s["n_pass"] = int(s["n_pass"])
    L = []
    w = L.append
    nmaps = Counter(k.split("/")[0] for k, v in meta["maps"].items() if "erreur" not in v)
    errs = [k for k, v in meta["maps"].items() if "erreur" in v]

    w("# Joueur Duke dans SlaveDriver : ouvertures des cartes Duke PC et rayons de la boule")
    w("")
    w("Généré par `python tools\\duke2ps\\player_report.py` (après `player_dims.py` et `player_openings.py`).")
    w("Toutes les longueurs sont en u : xy/8, z/128. Chaque chiffre de ce fichier est une **MESURE** faite par ces")
    w("commandes ; les règles sont citées **SOURCE** dans l'en-tête des scripts.")
    w("")
    w("```powershell")
    w("python tools\\duke2ps\\player_dims.py       # dimensions de Duke (simulation des boucles de player.c)")
    w("python tools\\duke2ps\\player_openings.py   # relevé : passages.csv, sectors.csv, survey_meta.json")
    w("python tools\\duke2ps\\player_report.py     # ce rapport + coverage.json")
    w("```")
    w("")

    # ---------------------------------------------------------------- 1. dimensions
    d = dims
    w("## 1. Dimensions de Duke (MESURE par simulation, SOURCE jfduke3d)")
    w("")
    w("| grandeur | debout | accroupi | rétréci | règle (SOURCE) |")
    w("|---|---|---|---|---|")
    w(f"| œil au-dessus du sol, physique | {f1(d['debout']['oeil_clip_u'])} | {f1(d['accroupi']['oeil_clip_u'])} | "
      f"{f1(d['retreci']['oeil_clip_u'])} | player.c:2785 (40<<8), 3048-3052 (+2816/tic), 3322 (+32<<8) |")
    w(f"| œil rendu | {f1(d['debout']['oeil_rendu_u'])} | {f1(d['accroupi']['oeil_rendu_u'])} | "
      f"{f1(d['retreci']['oeil_rendu_u'])} | game.c:3065-3073 (borné à sol−4<<8) |")
    w(f"| hauteur libre pour passer | {f1(d['debout']['hauteur_passage_u'])} | {f1(d['accroupi']['hauteur_passage_u'])} | "
      f"{f1(d['retreci']['hauteur_passage_u'])} | œil + ceildist 4<<8 (player.c:3320, engine.c:9210-9212) |")
    w(f"| rayon (clipdist) | {f1(d['walldist_u'])} | {f1(d['walldist_u'])} | {f1(d['walldist_u'])} | "
      f"clipmove 164L constant (player.c:3320), pushmove 164L (3368) |")
    w(f"| largeur mini entre jambages | {f1(d['largeur_min_axe_u'])} (axe) / {f1(d['largeur_min_45deg_u'])} (45°) | idem | idem | "
      f"carrés axés de demi-côté 164 (engine.c:9219-9228) |")
    w(f"| marche franchissable | ≤ {f1(d['debout']['marche_max_u'])} | < 2 | < 2 | flordist 20<<8 depuis l'œil "
      f"(player.c:3304, engine.c:9203-9205 ; écart < 1<<8 ignoré) |")
    w(f"| saut : montée de l'œil / rebord max | {f1(d['saut']['montee_oeil_u'])} / {f1(d['saut']['rebord_max_u'])} | "
      f"non | non | player.c:3057-3095, gravité 176+80 (2970) ; exige h > {f1(d['saut']['hauteur_libre_requise_u'])} |")
    r = d["retrecissement"]
    w("")
    w(f"Rétréci : sprite {r['taille_sprite']} ; état « rétréci » (yrepeat < 32) du tic {r['tic_premier_retreci']} au "
      f"tic {r['tic_dernier_retreci']} après l'impact, soit **{r['duree_retreci_s']:.1f} s** à 30 tics/s ; regrandit à "
      f"{r['debut_regrandir_s']:.1f} s, fin à {r['fin_s']:.2f} s ; **écrasé** (strength 0) si la hauteur libre "
      f"(getzrange) est < {f1(r['ecrase_si_hauteur_libre_lt_u'])} u pendant qu'il regrandit (GAME.CON d13:1648-1670, "
      "USER.CON d13:79-80, gamedef.c:2973-2975). Accroupi + rétréci : œil physique "
      f"{f1(d['retreci_accroupi']['oeil_clip_u'])} u (sous le sol, dégénéré).")
    w("")
    p = d["powerslave"]
    w(f"PowerSlave aujourd'hui : boule R = {p['rayon_u']} u, œil à **{p['oeil_au_dessus_sol_u']} u** au-dessus du sol "
      f"(R + 8, SPRITE.C:643-644), hauteur libre requise {p['hauteur_passage_u']} u, STEPHEIGHT {p['stepheight_u']} u, "
      f"saut bouton tenu ≈ {f1(p['saut_montee_u_tenu'])} u (sandales {f1(p['saut_sandales_montee_u'])} u, trames à 60 Hz : HYPOTHÈSE).")
    w("")

    # ---------------------------------------------------------------- 2. corpus
    w("## 2. Relevé des passages")
    w("")
    w("Un passage = une paire de murs rouges. Portes lotag 20/21/22 fermées → ouvertes (sector.c:786-868). Vantaux "
      "et panneaux 9/23/25/26 : considérés ouverts (HYPOTHÈSE). Sprites bloquants ignorés (HYPOTHÈSE).")
    w("")
    w("| corpus | cartes | passages | cloisons | hors cloisons | " + " | ".join(CLASSES) + " |")
    w("|---|---|---|---|---|" + "---|" * len(CLASSES))
    for c in CORPORA + ["tous"]:
        R = [x for x in P if c == "tous" or x["corpus"] == c]
        cc = Counter(x["cls"] for x in R)
        w(f"| {c} | {sum(nmaps.values()) if c == 'tous' else nmaps.get(c, 0)} | {len(R)} | "
          f"{sum(x['partition'] for x in R)} | {sum(1 - x['partition'] for x in R)} | "
          + " | ".join(str(cc.get(k, 0)) for k in CLASSES) + " |")
    if errs:
        w(f"\nCartes illisibles : {', '.join(errs)}")
    w("")
    w("Classes : `debout` h ≥ 88, `accroupi` 44 ≤ h < 88, `retreci` 24 ≤ h < 44, `infr_h` h < 24, `infr_l` largeur "
      "utile < 41 u / max(|ux|,|uy|), `bloque` cstat&1, `porte_mobile` vantail fermé (h ≤ 0).")
    w("")
    w("Répartition **hors cloisons** (ouvertures proprement dites) :")
    w("")
    w("| corpus | " + " | ".join(CLASSES) + " |")
    w("|---|" + "---|" * len(CLASSES))
    for c in CORPORA + ["tous"]:
        R = [x for x in P if (c == "tous" or x["corpus"] == c) and not x["partition"]]
        cc = Counter(x["cls"] for x in R)
        w(f"| {c} | " + " | ".join(f"{cc.get(k, 0)} ({pc(cc.get(k, 0), len(R))})" for k in CLASSES) + " |")
    w("")

    # ---------------------------------------------------------------- 3. distributions
    def is_open(x):
        return x["cls"] in POST

    def lintel(x):
        return is_open(x) and not x["partition"] and x["ceil_diff_u"] > 0.5 and math.isfinite(x["h_u"])

    w("## 3. Distributions (passages franchissables par Duke)")
    w("")
    w("`w_eff` = diamètre du plus grand disque centré sur le passage sans toucher un mur bloquant (plafonné à 256). "
      "`longueur` = longueur du mur / 8.")
    w("")
    for title, sel in (("Ouvertures hors cloisons", lambda x: is_open(x) and not x["partition"]),
                       ("Linteaux (plafond différent des deux côtés = embrasures, portes, fenêtres)", lintel),
                       ("Passages touchant une porte (lotag 9/20-26)", lambda x: is_open(x) and x["door_adj"])):
        w(f"### {title}")
        w("")
        w("| corpus / grandeur | n | p5 | p10 | médiane | p90 | modes (effectif) |")
        w("|---|---|---|---|---|---|---|")
        for c in CORPORA + ["tous"]:
            R = [x for x in P if (c == "tous" or x["corpus"] == c) and sel(x)]
            w(dist_row(f"{c} h", [x["h_u"] for x in R]))
            w(dist_row(f"{c} w_eff", [x["w_eff_u"] for x in R]))
            w(dist_row(f"{c} longueur", [x["len_u"] for x in R]))
        w("")
    # joint mode (h, w_eff) des linteaux
    LN = [x for x in P if lintel(x)]
    jm = Counter((round(x["h_u"]), round(x["w_eff_u"])) for x in LN)
    w("### Ouverture standard")
    w("")
    w("Couples (h, w_eff) les plus fréquents parmi les linteaux franchissables, tous corpus :")
    w("")
    w("| h (u) | w_eff (u) | effectif |")
    w("|---|---|---|")
    for (h, ww), n in jm.most_common(12):
        w(f"| {h} | {ww} | {n} |")
    w("")
    hs = [x["h_u"] for x in LN]
    ws = [x["w_eff_u"] for x in LN]
    std = {"h_mode": Counter(round(v) for v in hs).most_common(1)[0][0] if hs else None,
           "w_mode": Counter(round(v) for v in ws).most_common(1)[0][0] if ws else None,
           "h_p10": pct(hs, 10), "w_p10": pct(ws, 10), "h_p5": pct(hs, 5), "w_p5": pct(ws, 5)}
    w(f"Hauteur modale {std['h_mode']} u, largeur utile modale {std['w_mode']} u ; p10 = {f1(std['h_p10'])} u × "
      f"{f1(std['w_p10'])} u ; p5 = {f1(std['h_p5'])} u × {f1(std['w_p5'])} u.")
    w("")

    # ---------------------------------------------------------------- 4. secteurs
    w("## 4. Largeur praticable des secteurs")
    w("")
    w("`w_sec` = diamètre du plus grand cercle libre centré dans le secteur (murs bloquants de la posture du "
      "secteur). Retenus : secteurs où Duke tient (w_sec ≥ 41 u), avec au moins 2 passages franchissables "
      "(secteurs de passage).")
    w("")
    w("| corpus | classe | n | p5 | p10 | médiane | < 48 | < 64 | < 80 | < 94 | aire < 80 |")
    w("|---|---|---|---|---|---|---|---|---|---|---|")
    for c in CORPORA + ["tous"]:
        for cl in POST:
            R = [s for s in S if (c == "tous" or s["corpus"] == c) and s["cls"] == cl and s["n_pass"] >= 2
                 and s["w_sec_u"] >= DUKE_W]
            if not R:
                continue
            v = [s["w_sec_u"] for s in R]
            A = sum(s["area_u2"] for s in R)
            A80 = sum(s["area_u2"] for s in R if s["w_sec_u"] < 80)
            w(f"| {c} | {cl} | {len(R)} | {f1(pct(v, 5))} | {f1(pct(v, 10))} | {f1(pct(v, 50))} | "
              + " | ".join(pc(sum(1 for x in v if x < t), len(v)) for t in (48, 64, 80, 94)) + f" | {pc(A80, A)} |")
    w("")

    # ---------------------------------------------------------------- 5. couverture
    cov = {}
    w("## 5. Couverture d'une boule de rayon R")
    w("")
    w("Pour chaque posture, on prend les passages que Duke franchit **dans cette posture** (classe = posture). "
      "Hauteur : strict h ≥ 2R+8 (flottement caméra) / optimiste h ≥ 2R ; largeur w_eff ≥ 2R ; les deux = strict "
      "ET largeur. « parasites » = passages que Duke ne franchit pas faute de largeur (`infr_l`) mais que la boule "
      "franchit (h ≥ 2R+8 et w_eff ≥ 2R) : raccourcis ou sorties de carte possibles.")
    w("")
    for scope, selp in (("tous les passages", lambda x: True), ("hors cloisons", lambda x: not x["partition"]),
                        ("linteaux seulement", lambda x: x["ceil_diff_u"] > 0.5 and not x["partition"])):
        w(f"### {scope} (tous corpus)")
        w("")
        w("| R | œil | " + " | ".join(f"{p} h strict | {p} h optim. | {p} largeur | {p} les deux" for p in POST)
          + " | parasites | secteurs debout w_sec ≥ 2R |")
        w("|---|---|" + "---|" * (4 * len(POST)) + "---|---|")
        for R in RADII:
            row = [str(R), f1(R + HOVER)]
            for p_ in POST:
                X = [x for x in P if x["cls"] == p_ and selp(x)]
                n = len(X)
                a = sum(1 for x in X if x["h_u"] >= 2 * R + HOVER)
                b = sum(1 for x in X if x["h_u"] >= 2 * R)
                c_ = sum(1 for x in X if x["w_eff_u"] >= 2 * R)
                e = sum(1 for x in X if x["h_u"] >= 2 * R + HOVER and x["w_eff_u"] >= 2 * R)
                row += [pc(a, n), pc(b, n), pc(c_, n), pc(e, n)]
                cov.setdefault(scope, {}).setdefault(p_, {})[R] = {"n": n, "h": a, "h_opt": b, "w": c_, "both": e}
            par = sum(1 for x in P if x["cls"] == "infr_l" and selp(x) and x["h_u"] >= 2 * R + HOVER
                      and x["w_eff_u"] >= 2 * R)
            SS = [s for s in S if s["cls"] == "debout" and s["n_pass"] >= 2 and s["w_sec_u"] >= DUKE_W]
            row += [str(par), pc(sum(1 for s in SS if s["w_sec_u"] >= 2 * R), len(SS))]
            w("| " + " | ".join(row) + " |")
        w("")
    # par corpus, rayons clefs
    w("### Par corpus, rayons clefs (tous les passages, « les deux » = h ≥ 2R+8 et w_eff ≥ 2R)")
    w("")
    KEY = [(p_, R) for p_, Rs in (("debout", [24, 28, 32, 36, 40, 47]), ("accroupi", [12, 16, 18]),
                                  ("retreci", [6, 8])) for R in Rs]
    w("| corpus | " + " | ".join(f"{p_} R={R}" for p_, R in KEY) + " |")
    w("|---|" + "---|" * len(KEY))
    for c in CORPORA + ["tous"]:
        row = [c]
        for p_, R in KEY:
            X = [x for x in P if x["cls"] == p_ and (c == "tous" or x["corpus"] == c)]
            e = sum(1 for x in X if x["h_u"] >= 2 * R + HOVER and x["w_eff_u"] >= 2 * R)
            row.append(f"{pc(e, len(X))} ({len(X) - e})")
        w("| " + " | ".join(row) + " |")
    w("")
    w("(entre parenthèses : nombre de passages que Duke franchit dans cette posture et que la boule ne franchit pas)")
    w("")

    # rayon mini pour la largeur
    w("### Rayon maximal compatible, par posture (tous corpus)")
    w("")
    w("| posture | contrainte hauteur (100 % strict) | R max largeur 100 % | R max largeur 99 % | R max largeur 95 % |")
    w("|---|---|---|---|---|")
    for p_, H in (("debout", 88.0), ("accroupi", 44.0), ("retreci", 24.0)):
        X = [x for x in P if x["cls"] == p_]
        wv = sorted(x["w_eff_u"] for x in X)

        def rmax(q):
            if not wv:
                return math.nan
            k = int(math.floor((1 - q) * len(wv)))
            return wv[min(k, len(wv) - 1)] / 2

        w(f"| {p_} | R ≤ {f1((H - HOVER) / 2)} (h ≥ {H:g}) | {f1(rmax(1.0))} | {f1(rmax(0.99))} | {f1(rmax(0.95))} |")
    w("")

    # cylindre vertical (alternative)
    w("### Alternative : cylindre vertical de rayon 20,5 u (hauteurs de Duke par posture)")
    w("")
    w("Par construction il franchit 100 % des passages de sa posture (mêmes seuils que Duke, largeur euclidienne "
      "≥ 41 u ≤ exigence de Chebyshev de Duke). Parasites = passages `infr_l` (trop étroits pour les carrés "
      "axés de Build, donc diagonaux) que le cercle franchit :")
    w("")
    w("| posture | hauteur requise | parasites (w_eff ≥ 41, h ≥ seuil) | pour comparaison : boule de même hauteur |")
    w("|---|---|---|---|")
    for p_, H, Rb in (("debout", 88.0, 40), ("accroupi", 44.0, 18), ("retreci", 24.0, 8)):
        par = sum(1 for x in P if x["cls"] == "infr_l" and x["w_eff_u"] >= DUKE_W and x["h_u"] >= H)
        parb = sum(1 for x in P if x["cls"] == "infr_l" and x["w_eff_u"] >= 2 * Rb and x["h_u"] >= 2 * Rb + HOVER)
        w(f"| {p_} | {H:g} | {par} | R = {Rb} : {parb} |")
    w(f"\n(total `infr_l` : {sum(1 for x in P if x['cls'] == 'infr_l')})")
    w("")

    # ---------------------------------------------------------------- 6. marches
    w("## 6. Marches")
    w("")
    w("| corpus | passages debout avec marche > 32 u et ≤ 40 u (Duke monte, STEPHEIGHT PS non) | marche > 40 u "
      "(saut Duke ou sens unique) | accroupi/rétréci avec marche ≥ 2 u (Duke ne monte pas) |")
    w("|---|---|---|---|")
    for c in CORPORA + ["tous"]:
        X = [x for x in P if (c == "tous" or x["corpus"] == c)]
        a = sum(1 for x in X if x["cls"] == "debout" and 32 < x["step_u"] <= 40)
        b = sum(1 for x in X if x["cls"] == "debout" and x["step_u"] > 40)
        e = sum(1 for x in X if x["cls"] in ("accroupi", "retreci") and x["step_u"] >= 2)
        w(f"| {c} | {a} | {b} | {e} |")
    w("")

    # ---------------------------------------------------------------- 6b. sources de retrecissement
    shp = os.path.join(OUT, "shrinkers.json")
    if os.path.exists(shp):
        sh = json.load(open(shp, encoding="utf-8"))
        w("## 6b. Sources de rétrécissement dans les cartes (`player_shrinkers.py`)")
        w("")
        w("NEWBEAST (4610/4611/4670/4690) est le seul acteur qui `shoot SHRINKER` (maps/atomic/GAME.CON:8485-8494, "
          "pas si le joueur est déjà rétréci : 8487) ; aucun acteur de 1.3D ne le fait (grep : 0). Un tir de "
          "rétrécisseur renvoyé par un mur MIRROR change de propriétaire et peut toucher le tireur "
          "(jfduke3d actors.c:2509-2515).")
        w("")
        w("| corpus | cartes | cartes avec NEWBEAST | NEWBEAST | cartes avec rétrécisseur au sol | murs miroir |")
        w("|---|---|---|---|---|---|")
        for c in CORPORA:
            v = sh.get(c)
            if v:
                w(f"| {c} | {v['cartes']} | {len(v['newbeast'])} | {sum(v['newbeast'].values())} | "
                  f"{len(v['shrinker'])} | {v['murs_miroir']} |")
        w("")

    # ---------------------------------------------------------------- 7. E1L1
    E = [x for x in P if x["corpus"] == "d13" and x["map"] == "E1L1"]
    if E:
        w("## 7. Contrôle E1L1 (carte brute d13 contre l'import E1)")
        w("")
        w(f"Carte brute : {len(E)} passages, h < 90 : {sum(1 for x in E if x['h_u'] < 90)}, h < 47 : "
          f"{sum(1 for x in E if x['h_u'] < 47)}, h ≤ 0 : {sum(1 for x in E if x['h_u'] <= 0)} ; portes 101 et 286 : "
          + ", ".join(sorted({f"{x['secA']}↔{x['secB']} h {x['h_u']:.0f}" for x in E
                              if x["secA"] in ("101", "286")})) + ".")
        w("L'import E1 (NOTES_E1.md §5) comptait 518 portails, 46 < 90 u, 7 < 47 u : l'écart vient des règles "
          "de l'import (secteurs supprimés, vantaux retirés, composantes retirées).")
        w("")
    path = os.path.join(OUT, "openings_report.md")
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    os.replace(path + ".tmp", path)
    cp = os.path.join(OUT, "coverage.json")
    with open(cp + ".tmp", "w", encoding="utf-8") as f:
        json.dump({"coverage": cov, "standard": std}, f, indent=1)
    os.replace(cp + ".tmp", cp)
    print(f"-> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
