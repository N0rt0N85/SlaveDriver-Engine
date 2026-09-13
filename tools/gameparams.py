#!/usr/bin/env python3
"""gameparams.py -- turn gameparams.cfg into the C header the build includes.

The Makefile always reads `gameparams.cfg`; params/*.cfg are presets that are never read on
their own (copy one over, or pass PARAMS=params/<name>.cfg for a single build).

    KEY = 47          ->  #define GP_KEY 47
    KEY = cylinder    ->  #define GP_KEY cylinder   and   #define GP_KEY_CYLINDER 1

The second form is what lets C select behaviour with #ifdef GP_KEY_WORD.

Usage:  gameparams.py <cfg> <out.h>          write the header
        gameparams.py --get KEY <cfg>        print one value (for the Makefile)
"""
import os
import sys


def read(path):
    """[(key, value)] in file order, last assignment of a key winning."""
    out = {}
    order = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if "=" not in line:
                continue
            k, v = line.split("=", 1)
            k, v = k.strip(), v.strip()
            if not k or not v or not k.replace("_", "").isalnum():
                continue
            if k not in out:
                order.append(k)
            out[k] = v
    return [(k, out[k]) for k in order]


def is_word(v):
    return v.replace("_", "a").isalnum() and not v[0].isdigit()


def main(argv):
    if len(argv) == 4 and argv[1] == "--get":
        for k, v in read(argv[3]):
            if k == argv[2]:
                print(v)
                return 0
        return 0                                    # absent: print nothing, Makefile sees ""
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    cfg, out = argv[1], argv[2]
    lines = ["/* generated from %s by the Makefile -- do not edit */"
             % os.path.basename(cfg)]
    for k, v in read(cfg):
        lines.append("#define GP_%s %s" % (k, v))
        if is_word(v):
            lines.append("#define GP_%s_%s 1" % (k, v.upper()))
    text = "\n".join(lines) + "\n"
    if os.path.exists(out) and open(out, encoding="utf-8").read() == text:
        return 0                                    # unchanged: keep the mtime, no rebuild
    tmp = out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
