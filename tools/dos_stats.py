"""dos_stats.py -- extrait LEV*.MAP de STUFF.DAT (GOG PowerSlave DOS) et ecrit dos_stats.json.

Usage : python tools\\dos_stats.py [STUFF.DAT] [out_dir]
"""
from __future__ import annotations
import json
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import grp        # noqa: E402
import buildmap   # noqa: E402

DEFAULT_GRP = r"C:\Program Files\GOG Galaxy\Games\Powerslave\STUFF.DAT"
DEFAULT_OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "tmp-b2d")


def hist(values) -> dict[str, int]:
    c = Counter(values)
    return {str(k): v for k, v in sorted(c.items())}


def stats_for(m: buildmap.BuildMap) -> dict:
    xs = [w.x for w in m.walls]
    ys = [w.y for w in m.walls]
    fz = [s.floorz for s in m.sectors]
    cz = [s.ceilingz for s in m.sectors]
    return {
        "version": m.version,
        "start": {"x": m.posx, "y": m.posy, "z": m.posz, "ang": m.ang, "sect": m.cursectnum},
        "counts": {"sectors": len(m.sectors), "walls": len(m.walls), "sprites": len(m.sprites)},
        "trailing_bytes": m.trailing,
        "bbox": {"xmin": min(xs), "xmax": max(xs), "ymin": min(ys), "ymax": max(ys)} if xs else None,
        "floorz": {"min": min(fz), "max": max(fz)} if fz else None,
        "ceilingz": {"min": min(cz), "max": max(cz)} if cz else None,
        "sloped_sectors": {
            "floor": sum(1 for s in m.sectors if s.floorstat & 2),
            "ceiling": sum(1 for s in m.sectors if s.ceilingstat & 2),
            "any": sum(1 for s in m.sectors if (s.floorstat | s.ceilingstat) & 2),
        },
        "parallax_ceilings": sum(1 for s in m.sectors if s.ceilingstat & 1),
        "parallax_floors": sum(1 for s in m.sectors if s.floorstat & 1),
        "two_sided_walls": sum(1 for w in m.walls if w.nextsector >= 0),
        "masked_walls": sum(1 for w in m.walls if w.nextsector >= 0 and (w.cstat & 16)),
        "sprites_by_statnum": hist(s.statnum for s in m.sprites),
        "sprites_by_cstat_type": hist((s.cstat & 48) for s in m.sprites),   # 0 face, 16 wall, 32 floor
        "picnums": {
            "walls": sorted({w.picnum for w in m.walls}),
            "overpicnums": sorted({w.overpicnum for w in m.walls if w.overpicnum > 0}),
            "floors": sorted({s.floorpicnum for s in m.sectors}),
            "ceilings": sorted({s.ceilingpicnum for s in m.sectors}),
            "sprites": sorted({s.picnum for s in m.sprites}),
        },
        "lotag": {"sectors": hist(s.lotag for s in m.sectors),
                  "walls": hist(w.lotag for w in m.walls),
                  "sprites": hist(s.lotag for s in m.sprites)},
        "hitag": {"sectors": hist(s.hitag for s in m.sectors),
                  "walls": hist(w.hitag for w in m.walls),
                  "sprites": hist(s.hitag for s in m.sprites)},
    }


def main(argv):
    src = argv[1] if len(argv) > 1 else DEFAULT_GRP
    out = argv[2] if len(argv) > 2 else DEFAULT_OUT
    dos_dir = os.path.join(out, "dos")
    g = grp.GrpFile(src)
    paths = g.extract(dos_dir, "LEV*.MAP")
    result = {}
    for p in sorted(paths, key=lambda s: int(re.search(r"LEV(\d+)", s, re.I).group(1))):
        m = buildmap.load(p)
        result[os.path.basename(p)] = stats_for(m)
    with open(os.path.join(out, "dos_stats.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    for k, v in result.items():
        c = v["counts"]
        print(f"{k:10s} v{v['version']} S{c['sectors']:4d} W{c['walls']:5d} P{c['sprites']:4d} "
              f"slope{v['sloped_sectors']['any']:3d} plax{v['parallax_ceilings']:3d} "
              f"2s{v['two_sided_walls']:5d} trailing{v['trailing_bytes']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
