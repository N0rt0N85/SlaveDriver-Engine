#!/bin/sh
# Rebuild sdk/sbl6/ from the SEGA Basic Library 6.01 archive.
#   usage: tools/import_sbl6.sh SBL601.zip   (https://antime.kapsi.fi/sega/files/SBL601.zip)
# - headers: Ctrl-Z stripped, Shift-JIS -> UTF-8, lower-cased names
# - libs:    the original coff-sh archives copied VERBATIM to sdk/sbl6/lib/coff/ -- they are linked
#            as COFF (Makefile LIBS: -Wl,-b,coff-sh SEGA_SAT.A -Wl,-b,elf32-sh).  Do NOT convert
#            them with objcopy: it keeps the COFF section VMAs and every section-relative reference
#            (statics, tables) ends up shifted by that VMA in the ELF link (66/342 objects are
#            affected; see sdk/sbl6/README.md).
# Needs unzip, iconv.
set -e
ZIP="$1"; [ -f "$ZIP" ] || { echo "usage: $0 SBL601.zip"; exit 1; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
unzip -q "$ZIP" -d "$TMP"
LIB="$TMP/SBL6/SEGALIB"
mkdir -p "$HERE/sdk/sbl6/include" "$HERE/sdk/sbl6/lib/coff"
for f in "$LIB"/INCLUDE/*.H; do
  n=$(basename "$f" | tr 'A-Z' 'a-z')
  tr -d '\032' < "$f" | iconv -f CP932 -t UTF-8 -c > "$HERE/sdk/sbl6/include/$n"
done
rm -f "$HERE"/sdk/sbl6/lib/lib*.a   # old objcopy'd archives, wrong (see above)
cp "$LIB"/LIB/*.A "$HERE/sdk/sbl6/lib/coff/"
echo "done: $(ls "$HERE/sdk/sbl6/lib/coff" | wc -l) COFF archives, $(ls "$HERE/sdk/sbl6/include" | wc -l) headers"
