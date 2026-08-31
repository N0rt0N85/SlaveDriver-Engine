# SEGA Basic Library 6.01 (SBL) — GCC/ELF import

SlaveDriver was written against SEGA's **SBL** (`sega_scl.h`, `sega_spr.h`, `sega_per.h`,
`sega_mth.h`, `sega_cdc.h`, `sega_gfs.h`, …), not SGL. This directory is a mechanical import
of the SBL 6.01 developer release (`SBL601.zip`, mirrored at
<https://antime.kapsi.fi/sega/files/SBL601.zip>) made usable by a modern `sh2eb-elf` GCC:

* `include/` — the original `SEGALIB/INCLUDE/*.H`, lower-cased, trailing Ctrl-Z removed,
  Shift-JIS comments converted to UTF-8 (GCC 14 rejects the raw files).
* `lib/coff/` — the original `SEGALIB/LIB/*.A` (Cygnus GCC 2.7 `coff-sh` archives), **verbatim**.
  They are linked as COFF: `-Wl,-b,coff-sh SEGA_SAT.A -Wl,-b,elf32-sh` (GNU ld reads `coff-sh`
  in a final link). `SEGA_SAT.A` is SEGA's all-in-one archive (every sub-library in one file).
  The objects still reference the GCC 2.7 libgcc helper names (`__ashiftrt_r4_N`, `__muldf3`, …)
  which modern libgcc still provides — always link with `-lgcc`.
  **Never convert them with `objcopy -I coff-sh -O elf32-sh`** (the first version of this import
  did): the COFF section VMAs survive the conversion and every section-relative reference
  (static variables, jump/handler tables, string literals) is resolved `vma` bytes too far by the
  ELF link — `INT_SetScuFunc` registered the wrong trampoline and no SMPC interrupt ever reached
  the PER library (boot stuck in `PER_GET_SYS`). 66 of the 342 objects are affected.

The headers and library binaries are SEGA's proprietary SDK and are **not committed**
(`include/` and `lib/` are gitignored): regenerate this directory with
`tools/import_sbl6.sh SBL601.zip`.
