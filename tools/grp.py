"""grp.py -- lecteur d'archives GRP de Ken Silverman (STUFF.DAT, DUKE3D.GRP).

Format (UTIL/RIPKEN.C:19-24 et :34-42 de ce depot ; refs/build/jfbuild/src/cache1d.c) :
    char  magic[12]  = "KenSilverman"          (RIPKEN.C:19 fread 12)
    int32 count       (little-endian)          (RIPKEN.C:20)
    count x { char name[12] ; int32 size }     (RIPKEN.C:24-27 : name 12 o, size 4 o)
    puis les donnees, dans l'ordre des entrees, contigues :
        filePos = 12 + 4 + 16*count ; filePos += size apres chaque fichier (RIPKEN.C:22, :41)
Les noms ne sont pas forcement termines par NUL (RIPKEN.C:25 force name[12]=0).

Usage : python tools\\grp.py <archive.grp> [list | extract <dir> [pattern]]
"""
from __future__ import annotations
import fnmatch
import os
import struct
import sys
from dataclasses import dataclass

MAGIC = b"KenSilverman"
HEADER_SIZE = 16          # 12 + 4          (RIPKEN.C:22)
ENTRY_SIZE = 16           # name[12] + int32 (RIPKEN.C:24-27)


@dataclass(frozen=True)
class GrpEntry:
    name: str
    size: int
    offset: int


class GrpFile:
    def __init__(self, path: str):
        self.path = path
        self.entries: list[GrpEntry] = []
        self._index: dict[str, GrpEntry] = {}
        with open(path, "rb") as f:
            head = f.read(HEADER_SIZE)
            if len(head) != HEADER_SIZE or head[:12] != MAGIC:
                raise ValueError(f"{path}: not a KenSilverman GRP (magic={head[:12]!r})")
            (count,) = struct.unpack("<I", head[12:16])
            table = f.read(ENTRY_SIZE * count)
            if len(table) != ENTRY_SIZE * count:
                raise ValueError(f"{path}: truncated directory")
        pos = HEADER_SIZE + ENTRY_SIZE * count
        for i in range(count):
            raw = table[i * ENTRY_SIZE:(i + 1) * ENTRY_SIZE]
            name = raw[:12].split(b"\0", 1)[0].decode("latin-1")
            (size,) = struct.unpack("<I", raw[12:16])
            e = GrpEntry(name, size, pos)
            self.entries.append(e)
            self._index.setdefault(name.upper(), e)   # first wins, like a linear search
            pos += size
        self.data_end = pos

    def names(self) -> list[str]:
        return [e.name for e in self.entries]

    def find(self, name: str) -> GrpEntry | None:
        return self._index.get(name.upper())

    def read(self, name_or_entry) -> bytes:
        e = name_or_entry if isinstance(name_or_entry, GrpEntry) else self.find(name_or_entry)
        if e is None:
            raise KeyError(name_or_entry)
        with open(self.path, "rb") as f:
            f.seek(e.offset)
            data = f.read(e.size)
        if len(data) != e.size:
            raise ValueError(f"{self.path}: truncated data for {e.name}")
        return data

    def extract(self, out_dir: str, pattern: str = "*") -> list[str]:
        os.makedirs(out_dir, exist_ok=True)
        written = []
        for e in self.entries:
            if not fnmatch.fnmatch(e.name.upper(), pattern.upper()):
                continue
            dst = os.path.join(out_dir, e.name)
            with open(dst, "wb") as g:
                g.write(self.read(e))
            written.append(dst)
        return written


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    grp = GrpFile(argv[1])
    cmd = argv[2] if len(argv) > 2 else "list"
    if cmd == "list":
        print(f"{len(grp.entries)} entries, data ends at {grp.data_end} (file size {os.path.getsize(argv[1])})")
        for e in grp.entries:
            print(f"{e.name:12s} {e.size:10d} @{e.offset}")
    elif cmd == "extract":
        out = argv[3] if len(argv) > 3 else "."
        pat = argv[4] if len(argv) > 4 else "*"
        for p in grp.extract(out, pat):
            print(p)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
