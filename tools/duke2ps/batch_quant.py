#!/usr/bin/env python3
"""batch_quant.py -- valide la regle E2.5b (snap preservant les features) sur un corpus de cartes.

Pour chaque .MAP : E1 (build_import) -> E2.5/E2.5b (quantize --no-convex) -> E2 (convex).
On ne cherche PAS a produire un niveau jouable ici : on cherche a savoir si la REGLE de snap tient
ailleurs que sur E1L1 (derive bornee, 0 degenerescence, decoupe convexe qui passe).

Usage : python tools\duke2ps\batch_quant.py --maps <glob...> [--jobs N] [--out J]
"""
from __future__ import annotations
import argparse, concurrent.futures as cf, glob, json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WORK = os.path.join(ROOT, "build", "duke2ps", "batch")
PY = sys.executable


def run(cmd, timeout):
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        return p.returncode, p.stdout + p.stderr, time.time() - t0
    except subprocess.TimeoutExpired:
        return -9, "TIMEOUT", time.time() - t0


def one(mp, timeout):
    name = os.path.splitext(os.path.basename(mp))[0]
    corpus = os.path.basename(os.path.dirname(mp))
    tag = f"{corpus}/{name}"
    d = os.path.join(WORK, corpus)
    os.makedirs(d, exist_ok=True)
    imp = os.path.join(d, name + "_i.json")
    qnt = os.path.join(d, name + "_q.json")
    cvx = os.path.join(d, name + "_c.json")
    r = {"carte": tag, "etape": "E1"}

    rc, out, dt = run([PY, os.path.join(HERE, "build_import.py"), "--map", mp, "--out", imp, "--no-png"], timeout)
    r["t_e1"] = round(dt, 1)
    if not os.path.exists(imp):
        r["echec"] = out.strip().splitlines()[-1][:200] if out.strip() else f"rc={rc}"
        return r
    r["e1_rc"] = rc                      # criteres E1 specifiques a E1L1 : informatif, non bloquant
    for line in out.splitlines():
        if "ECHEC" in line:
            r["e1_criteres_ko"] = line.split("ECHEC :")[-1].split(";")[0].strip()[:160]

    r["etape"] = "E2.5"
    rc, out, dt = run([PY, os.path.join(HERE, "quantize.py"), "--in", imp, "--out", qnt, "--no-convex"], timeout)
    r["t_q"] = round(dt, 1)
    if not os.path.exists(qnt):
        r["echec"] = out.strip().splitlines()[-1][:200] if out.strip() else f"rc={rc}"
        return r
    r["quant_rc"] = rc
    try:
        Q = json.load(open(qnt))
        qz = Q.get("quantization", {})
        xz = qz.get("rapport", {}).get("xz", {})
        r["secteurs"] = len(Q["sectors"]); r["murs"] = len(Q["walls"])
        r["derive_u"] = xz.get("snap_derive_max_u")
        r["iterations"] = xz.get("snap_iterations")
        r["paires"] = xz.get("features_preservees")
        r["ecartees"] = xz.get("valeurs_ecartees")
        r["soudures"] = xz.get("sommets_soudes")
        r["crit_ko"] = sorted(k for k, v in (qz.get("criteres") or {}).items() if not v.get("ok"))
        df = qz.get("a_deferer") or {}
        r["boucles_degen"] = (df.get("boucles_degenerees") or {}).get("n_secteurs")
        r["murs_nuls"] = (df.get("murs_longueur_nulle") or {}).get("n")
        r["ecarts_pente"] = (df.get("jonctions_pente") or df.get("ecarts_pente") or {}).get("n")
    except Exception as e:
        r["echec_lecture"] = str(e)[:120]

    cvx0 = os.path.join(d, name + "_c0.json")
    rc0, out0, dt0 = run([PY, os.path.join(HERE, "convex.py"), "--in", imp, "--out", cvx0,
                          "--no-png", "--no-holywood"], timeout)
    r["t_c0"] = round(dt0, 1); r["convex_brut_rc"] = rc0
    if os.path.exists(cvx0):
        try:
            C0 = json.load(open(cvx0)); r["morceaux_brut"] = len(C0.get("pieces", []))
            r["convex_brut_ko"] = sorted(k for k, v in (C0.get("criteres") or {}).items()
                                         if not (v.get("ok") if isinstance(v, dict) else v))
        except Exception: pass
    else:
        r["convex_brut_ko"] = ["<plantage>"]

    r["etape"] = "E2"
    rc, out, dt = run([PY, os.path.join(HERE, "convex.py"), "--in", qnt, "--out", cvx,
                       "--no-png", "--no-holywood"], timeout)
    r["t_c"] = round(dt, 1)
    r["convex_rc"] = rc
    if not os.path.exists(cvx):
        r["echec"] = "convex: " + (out.strip().splitlines()[-1][:200] if out.strip() else f"rc={rc}")
        return r
    try:
        C = json.load(open(cvx))
        r["morceaux"] = len(C.get("pieces", []))
        r["convex_crit_ko"] = sorted(k for k, v in (C.get("criteres") or {}).items() if not (v.get("ok") if isinstance(v, dict) else v))
    except Exception:
        pass
    for line in out.splitlines():
        if "CRITERE" in line.upper() and ("KO" in line or "ECHEC" in line):
            r.setdefault("convex_ko", []).append(line.strip()[:160])
    r["etape"] = "OK" if rc == 0 else "E2-KO"
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", nargs="+", required=True)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--out", default=os.path.join(WORK, "rapport.json"))
    a = ap.parse_args()
    maps = []
    for g in a.maps:
        maps.extend(sorted(glob.glob(g)))
    os.makedirs(WORK, exist_ok=True)
    print(f"{len(maps)} cartes, {a.jobs} en parallele", flush=True)
    res = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for r in ex.map(lambda m: one(m, a.timeout), maps):
            res.append(r)
            flag = "ok " if r.get("etape") == "OK" else "KO "
            print(f"{flag}{r['carte']:<28} sect={r.get('secteurs','?'):>5} mur={r.get('murs','?'):>5} "
                  f"morc={r.get('morceaux_brut','?')}->{r.get('morceaux','?')} "
                  f"derive={r.get('derive_u','?')} it={r.get('iterations','?')} "
                  f"KOb={len(r.get('convex_brut_ko') or [])} KOq={len(r.get('convex_crit_ko') or [])} "
                  f"{r.get('echec','')}", flush=True)
    json.dump(res, open(a.out, "w"), indent=1)
    ok = sum(1 for r in res if r.get("etape") == "OK")
    print(f"\n{ok}/{len(res)} chaine complete OK -> {a.out}")


if __name__ == "__main__":
    main()
