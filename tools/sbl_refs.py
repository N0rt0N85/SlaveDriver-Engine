#!/usr/bin/env python3
"""sbl_refs.py -- what SEGA's archive expects the rest of the link to define, for --gc-sections.

The SBL archive (sdk/sbl6/lib/coff/SEGA_SAT.A) is linked as COFF.  ld never sweeps its sections,
but it does not follow their relocations either when it marks what to keep: a symbol that only an
SBL member uses looks dead, its section goes, and the member's reference silently resolves to
nothing.  Measured on the first --gc-sections link of the Doom MAIN (2026-09-18): ten of them --
memcmp/strncmp/strncpy for GFS (the CD file system), SclDisplayX/Y, SclRotateTableMode,
SCL_Memcpyw and three double-float helpers for the rotation scroll -- and the link succeeded.

    sbl_refs.py --roots NM ARCHIVE [OBJ...]
        the symbols some member of ARCHIVE leaves undefined and that either no member defines
        (libc, libgcc) or one of OBJ defines (our overrides of SBL members: SCL_FUNC, SCL_VBLV),
        as `-Wl,-u,SYM` flags: given to ld, each is a root of the garbage collection.  A symbol
        only other members define is left out -- `-u` on it would pull that member in.
    sbl_refs.py --check NM ARCHIVE MAP ELF
        after the link: every symbol a member listed in MAP (a linked member) leaves undefined
        must be defined in ELF.  Exit 1 with the list otherwise.
"""
import re
import subprocess
import sys


def archive_symbols(nm, archive):
    """({member: {undefined symbols}}, {defined symbols}) of a COFF archive."""
    out = subprocess.run([nm, "--target=coff-sh", "-A", archive], capture_output=True,
                         text=True, check=True).stdout
    undefined, defined = {}, set()
    for line in out.splitlines():
        m = re.match(r".*:([A-Za-z0-9_]+\.o):\s*([0-9a-fA-F]*)\s+(\w)\s+(\S+)$", line.strip())
        if not m:
            continue
        member, _addr, kind, sym = m.groups()
        if kind == "U":
            undefined.setdefault(member, set()).add(sym)
        else:
            defined.add(sym)
    return undefined, defined


def main(argv):
    if len(argv) >= 4 and argv[1] == "--roots":
        nm, archive, objs = argv[2], argv[3], argv[4:]
        undefined, defined = archive_symbols(nm, archive)
        ours = set()
        for obj in objs:
            out = subprocess.run([nm, "--defined-only", obj], capture_output=True, text=True,
                                 check=True).stdout
            ours |= {line.split()[-1] for line in out.splitlines() if line.strip()}
        used = set().union(*undefined.values())
        roots = sorted(s for s in used if s not in defined or s in ours)
        print(" ".join("-Wl,-u," + s for s in roots))
        return 0
    if len(argv) == 6 and argv[1] == "--check":
        nm, archive, mapfile, elf = argv[2:]
        undefined, _defined = archive_symbols(nm, archive)
        text = open(mapfile, encoding="utf-8", errors="replace").read()
        linked = set(re.findall(r"SEGA_SAT\.A\(([A-Za-z0-9_]+\.o)\)", text))
        out = subprocess.run([nm, "--defined-only", elf], capture_output=True, text=True,
                             check=True).stdout
        present = {line.split()[-1] for line in out.splitlines() if line.strip()}
        missing = sorted({(s, m) for m in linked for s in undefined.get(m, ()) if s not in present})
        if missing:
            print("*** %s: %d symbol(s) an SBL member uses are gone from the link: %s"
                  % (elf, len(missing), ", ".join("%s (%s)" % sm for sm in missing[:20])))
            return 1
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
