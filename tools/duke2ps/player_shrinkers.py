"""player_shrinkers.py -- sources de retrecissement du joueur dans les cartes : ennemis NEWBEAST (seul acteur
qui `shoot SHRINKER`, maps/atomic/GAME.CON:8485-8494, jamais si le joueur est deja retreci : 8487), armes
retrecisseur au sol (SHRINKERSPRITE 25, recuperable par le joueur ; son tir renvoye par un miroir peut le
toucher : jfduke3d actors.c:2509-2515 fait rebondir tout projectile sauf RPG/FREEZEBLAST/SPIT sur un mur MIRROR),
et murs miroir (picnum/overpicnum 560, names.h:162).

Usage : python tools/duke2ps/player_shrinkers.py   -> build/duke2ps/player/shrinkers.json + tableau
"""
from __future__ import annotations
import glob
import json
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import buildmap  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MAPS = os.path.join(ROOT, "refs", "extract", "KILLATON", "maps")
OUT = os.path.join(ROOT, "build", "duke2ps", "player")
CORPORA = ["d13", "atomic", "dukedc", "dz2", "xtreme_sp"]
NEWBEAST = {4610, 4611, 4690, 4670}      # names.h:708-711
SHRINKERSPRITE = 25                       # names.h:17
MIRROR = 560                              # names.h:162


def main():
    res = {}
    print("| corpus | cartes | cartes avec NEWBEAST | NEWBEAST | cartes avec retrecisseur au sol | murs miroir |")
    print("|---|---|---|---|---|---|")
    for c in CORPORA:
        maps = sorted(set(glob.glob(os.path.join(MAPS, c, "*.MAP")) + glob.glob(os.path.join(MAPS, c, "*.map"))))
        nb = Counter()
        sh = Counter()
        mir = 0
        for p in maps:
            m = buildmap.load(p)
            name = os.path.splitext(os.path.basename(p))[0].upper()
            for s in m.sprites:
                if s.picnum in NEWBEAST:
                    nb[name] += 1
                if s.picnum == SHRINKERSPRITE:
                    sh[name] += 1
            mir += sum(1 for w in m.walls if w.picnum == MIRROR or w.overpicnum == MIRROR)
        res[c] = {"cartes": len(maps), "newbeast": dict(nb), "shrinker": dict(sh), "murs_miroir": mir}
        print(f"| {c} | {len(maps)} | {len(nb)} | {sum(nb.values())} | {len(sh)} | {mir} |")
    path = os.path.join(OUT, "shrinkers.json")
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    os.replace(path + ".tmp", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
