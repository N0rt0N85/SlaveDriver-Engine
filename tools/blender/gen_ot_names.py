#!/usr/bin/env python3
"""gen_ot_names.py -- fabrique io_lev/ot_names.py depuis l'enum ObjectType de SLEVEL.H.

L'extension Blender doit nommer les objets qu'elle pose dans la scene, et elle n'a pas le depot
sous la main : la table est donc EMBARQUEE, mais engendree, jamais recopiee a la main. A relancer
si SLEVEL.H bouge (l'enum est a SLEVEL.H:41-100, 227 valeurs vivantes + la sentinelle OT_DEAD).

Usage : python tools\\blender\\gen_ot_names.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "SLEVEL.H")
OUT = os.path.join(HERE, "io_lev", "ot_names.py")


def extraire(texte):
    m = re.search(r"enum\s+ObjectType\s*\{(.*?)\}\s*;", texte, re.S)
    if not m:
        raise SystemExit("enum ObjectType introuvable dans SLEVEL.H")
    corps = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    noms, v = {}, 0
    for part in corps.split(","):
        p = part.strip()
        if not p:
            continue
        mm = re.match(r"^(OT_[A-Z0-9_]+)\s*(?:=\s*(-?\d+))?$", p)
        if not mm:
            raise SystemExit("entree d'enum non reconnue : %r" % p)
        if mm.group(2) is not None:
            v = int(mm.group(2))
        noms.setdefault(v, mm.group(1))     # le premier nom gagne sur les alias (MAGMANTIS...)
        v += 1
    return noms


def main():
    with open(SRC, encoding="utf-8", errors="replace") as f:
        noms = extraire(f.read())
    lignes = ['"""ot_names.py -- ENGENDRE par tools/blender/gen_ot_names.py depuis SLEVEL.H.',
              "",
              "Ne pas editer a la main : relancer le generateur. %d types." % len(noms),
              '"""',
              "OT = {"]
    for v in sorted(noms):
        lignes.append("    %d: %r," % (v, noms[v]))
    lignes += ["}", "",
               "",
               "def nom(t):",
               '    """Le nom du type, ou OT_<n> pour une valeur que SLEVEL.H ne connait pas."""',
               "    return OT.get(t, 'OT_%d' % t)", ""]
    tmp = OUT + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lignes))
    os.replace(tmp, OUT)
    print("ecrit %s (%d types, OT_PLAYER=%s)"
          % (OUT, len(noms), [k for k, n in noms.items() if n == "OT_PLAYER"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
