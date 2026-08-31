SlaveDriver Engine for Sega Saturn GPL Source Code
==================================================

Copyright (c) 1996, 2006, 2025 Ezra Dreisbach

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

SPDX-License-Identifier: GPL-3.0-or-later

Notes
-----

This source release does not contain any game data. Associated game
data remains subject to applicable law.

Thanks to Ezra Dreisbach for preserving this data and approving its
publication.

Packaged by Lobotomy Software Open Source Group.

Building with GCC
-----------------

This fork builds the three flat Saturn programs with the free `sh2eb-elf` GCC 14.2
toolchain shipped by [SaturnRingLib](https://github.com/ReyeMe/SaturnRingLib)
(`SaturnRingLib/setup_compiler.bat`) and the SEGA Basic Library 6.01 import in `sdk/sbl6/`
(see `sdk/sbl6/README.md`; the original build used SN Systems Psy-Q and produced the `.CPE`
files kept in the tree).  The port notes live in `docs/PORTING_NOTES.md`.

Requirements: the toolchain above, GNU make and `xorrisofs` from MSYS2 (`C:\msys64\usr\bin`),
and a one-time SBL import -- the SEGA library binaries are not part of this repository:
`tools/import_sbl6.sh SBL601.zip` regenerates `sdk/sbl6/` (see `sdk/sbl6/README.md`).

    powershell -ExecutionPolicy Bypass -File build.ps1            # build/INIT.BIN MAIN.BIN KEYGEN.BIN
    powershell -ExecutionPolicy Bypass -File build.ps1 -NDebug    # asserts off, under build/ndebug/
    powershell -ExecutionPolicy Bypass -File build.ps1 size       # text/data/bss vs the original CPEs
    powershell -ExecutionPolicy Bypass -File build.ps1 iso        # bootable test image (needs cd/ data)
    powershell -ExecutionPolicy Bypass -File build.ps1 -Clean

`build.ps1` only puts the toolchain and `C:\msys64\usr\bin` on `PATH` and runs `make` with its
arguments (`make`, `make NDEBUG=1`, `make size`, `make iso`, `make clean`, `make -k`).  In an
MSYS2 shell call `make` directly (a case-insensitive filesystem is expected: the sources include
`"util.h"` while the files are `UTIL.H`).  Do not start `C:\msys64\usr\bin\make.exe` from Git
Bash: it needs a native parent (PowerShell/cmd), otherwise it stops with "Cannot create
temporary file in C:\WINDOWS".

What gets built (`Makefile`, `saturn.ld`):

* `build/INIT.BIN`   — `INITMAIN.C` main: the first program on the disc (loaded at 0x06004000).
* `build/MAIN.BIN`   — `SRUINS.C` main: the game, loaded by INIT through `executeLink`.
* `build/KEYGEN.BIN` — `KEYGEN.C` main (the original `KEY.CPE`).
* `build/<prog>.elf` / `.map` next to them (debug symbols, `-g`), objects under `build/obj/`.
* Default = debug (asserts on), like the shipped CPEs.  `NDEBUG=1` builds under `build/ndebug/`
  so the two never mix.
* `make iso` writes `build/slavedriver.iso` with SaturnRingLib's generic `IP.BIN`
  (`modules/sgl/IP.BIN`), `INIT.BIN` as `0.BIN` (first file, what the IP loads) and
  `MAIN.BIN`, plus everything you put in `cd/` — the game data is not part of this repository.

The SNASM sources (`START.S`, `LINK.S`, `WALLASM.S`, `BOOT/*.S`) are kept untouched; their GNU
`as` translations are `crt0.s`, `link_gnu.s`, `wallasm_gnu.s` (`_gnu` because `link.s` is `LINK.S` on a
case-insensitive checkout).  `MEMCPY.S` was already GNU syntax but its `.align 4` became `.balign 4`
(GNU `as` on sh-elf reads `.align` as a power of two).

Nine original files carry minimal edits, each tagged `/* GCC14: ... */` (`git diff` shows them
all): `LEVEL.C` (cast used as lvalue), `V_BLANK.C` (`volatile` to match the header, inline-asm
mnemonics, user-break frame offset), `WALLS.C` (`const` on a variable modified under `#if MIPMAP`),
`UTIL.C` (an out-of-bounds `stackPos[2]=0` that GCC 14's static layout turned into a real bug,
inline-asm mnemonic, and `waitSystemData()`: the `PER_KD_SYS` wait paced by vblank with an INTBACK
re-arm, as the SBL manual asks), `UTIL.H` (`noreturn` on `assertFail`, `mov #0` mnemonic, that
prototype), `INITMAIN.C` / `SRUINS.C` (call it instead of the bare `PER_GET_SYS()` poll; INITMAIN
also carries the `BOOTPROBE` colour probe, see the Makefile), `ART.H` (`extern` on declarations),
`MEMCPY.S` (`.balign`), and the trailing Ctrl-Z byte removed from `MEGAINIT.C` and `SCL_FUNC.C`.
SN-runtime hooks are stubbed in `shim/` (`pollhost`, `_sbrk`).

The SBL archives are linked **as COFF** (`-Wl,-b,coff-sh sdk/sbl6/lib/coff/SEGA_SAT.A
-Wl,-b,elf32-sh`).  Do not convert them with `objcopy`: the COFF section VMAs survive and every
section-relative reference of the library ends up shifted — that is what kept the first build of
this port from booting (`docs/PORTING_NOTES.md`, "Boot").

Only the US/PAL configuration links; `-DJAPAN` needs `PIC.C` in the INIT/KEYGEN closures, and the
`FLASH/` and `OLDJAP/` directories are diverged snapshots outside this build.
