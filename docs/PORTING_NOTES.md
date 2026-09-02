# Porting SlaveDriver to a modern GCC toolchain — working notes

Goal: make the 1996 SlaveDriver (PowerSlave Saturn) sources build with the
`sh2eb-elf` GCC 14.2 toolchain shipped by SaturnRingLib, producing the same two flat
binaries the original disc used (`INIT` = first program, `MAIN.BIN` = game).

## Original toolchain (what the sources were written for)
* Compiler: SN Systems Psy-Q for Saturn (GCC 2.7-era `ccsh`), assembler `asmsh`/SNASM syntax
  (`xdef`, `xref`, `section`, `littab`, `=$hex` literal-pool operands, `@1` local labels,
  `mov.l #imm`, `add.l`, `cnop`). Outputs `.CPE` (SN executable, loaded by the debugger).
* Libraries: **SEGA Basic Library (SBL)** — `sega_scl/spr/per/int/sys/mth/cdc/gfs/dma/bup.h` —
  NOT SGL. Plus `libsn.h` (SN PC file server, only under `#ifdef PSYQ`) and `machine.h`
  (`set_imask`/`get_imask`).
* The three `.CPE` in the tree are SN-debugger builds. INIT.CPE and KEY.CPE carry the `__FILE__`
  strings of `assert()` (`INITMAIN.c`, `FILE.c`, …) → asserts ON; SRUINS.CPE has none but carries the
  `dPrint` texts (`Acquiring system info…`, only compiled under `#ifdef PSYQ`) → a PSYQ + NDEBUG build.
  The retail disc's `MAIN.BIN`/`0` have neither (release). CPE SETREG 0x58 = entry **PC** of SN's crt0
  (0x0602f840 for SRUINS), which loads r15 from `_stackinit` = `&mystack[STICKSIZE]` (UTIL.C).
  Image spans: INIT.CPE 0x06004000..0x06027ce4 (147 KB), SRUINS.CPE ..0x06053d40 (327 KB), KEY.CPE ..0x06025714.
* **INIT** (`INITMAIN.C` main): Sega logo/intro, then `link("+MAIN.BIN")` (FILE.C) which loads
  the file into heap and calls `executeLink` (LINK.S) — copied to a 256-byte heap buffer and
  executed there (so it must be position-independent within 256 bytes): copies MAIN to
  0x06004000, sets r15=0x06001000, jumps to 0x06004000.
* **MAIN** (`SRUINS.C` main; SRUINS.C `#include "airbub.c"`, ART.C `#include "statbar.c"`).
* **KEYGEN** (`KEYGEN.C` main): separate small program (KEY.CPE).
* Object closures (computed from undefined symbols, -DNDEBUG survey, objects that failed to
  compile excluded — LEVEL/MEGAINIT/SCL_FUNC/V_BLANK/WALLS belong to both INIT and MAIN):
  - INIT: DMA FILE INITMAIN LOCAL MOV PICSET PRINT SOUND SPR UTIL (+ the 5 above, + MEMCPY.S, LINK.S, START.S)
  - MAIN: AI AI2 AICOMMON BIGMAP BUP DMA FILE HITSCAN INTRO LOCAL MAP MENU OBJECT PIC PICSET PLAX
    PRINT PROFILE ROUTE SEQUENCE SOUND SPR SPRITE SRUINS UTIL WEAPON (+ the 5, + MEMCPY.S, WALLASM.S, LINK.S, START.S)
  - KEYGEN: DMA KEYGEN PRINT SPR UTIL (+ ...)
  - NOT compiled separately: AIRBUB.C, STATBAR.C (#included by SRUINS.C / ART.C); DIAL.C is dead (two static tables, no references); ART.C IS linked into MAIN (statbar tables)
* Slave SH-2: `startSlave()` (WALLS.C) uses SMPC SSHOFF/SSHON + `SYS_SETSINT(0x94, slaveMain)`;
  `wallRenderSlaveMain` runs on the slave. No SCU-DSP program (DSP/ is a SEGA sample; MEGAINIT only clears the DSP).

## Toolchain facts (verified)
* SaturnRingLib's `Compiler/sh2eb-elf/bin` must be on PATH (gcc silently fails with rc=1
  otherwise — it cannot find cc1/DLLs).
* GNU make 4.4.1: `C:\msys64\usr\bin\make.exe`; `xorrisofs` in `C:\msys64\usr\bin`.
* Files are `*.C` (uppercase) → GCC treats them as **C++** unless `-x c` is passed. Always `-x c`.
* SBL 6.01 archives are `coff-sh`; `ld` needs `-b coff-sh` to read them (bare, it says "file format
  is ambiguous"). They are linked AS COFF from `sdk/sbl6/lib/coff/` (Makefile `LIBS`). ⚠ The first
  version of this port converted every member with `objcopy -I coff-sh -O elf32-sh`: that links but
  is WRONG — COFF section VMAs survive and every section-relative reference is shifted (see "Boot").
  (script `tools/import_sbl6.sh`). Link **through the gcc driver with `-lgcc`** (objects need the
  GCC 2.7 libgcc helpers `__ashiftrt_r4_N`, `__floatsidf`, `__muldf3`, `__fixdfsi`, `__sdivsi3`…).
  Verified: tiny program linking SCL/SPR/MTH/PER/DMA/INT/SYS resolves and disassembles correctly.
* SBL headers: raw files have a trailing Ctrl-Z and Shift-JIS text (also outside comments in an
  `#if 0` block of sega_per.h) → `sdk/sbl6/include/` = Ctrl-Z stripped + CP932→UTF-8 + lower-case.
  Do NOT use `-finput-charset=CP932` on the game sources (breaks many files).
* `SEGA_SAT.A` = all sub-libraries in one archive (134 members); the per-lib archives add only SEGADGFS + SGL glue.
  Function coverage: every SBL function the game calls is provided (SCL/SPR/PER/DMA/INT/SYS via
  SEGA_SAT.A: CDC, GFS, MTH, PER, INT, SCL, SPR, SYS, DMA…). BUP_* / SYS_* / INT_SetMsk etc. are header macros (BIOS vectors).
* The game ships its own **SCL_FUNC.C** (modified SBL `scl_func.c`) defining SCL_Vdp2Init,
  SCL_Open/Close, SCL_Move/MoveTo/Scale, SCL_SetColRamMode, SCL_SglOn/Off, SCL_Vdp2_SGLInit,
  SCL_CopyReg, SCL_Memcpyw, SCL_ParametersInit, SCL_PriIntProc, SCL_ScrollShow + Scl* globals.
  The library's `scl_func.o` defines the same set → expect "multiple definition" if it gets pulled;
  the game's object must win (link objects before libs; if still pulled, build a lib copy without scl_func.o).
* Newlib (`-lc`) needs `_read/_write/_close/_lseek/_sbrk` → either `-specs=nosys.specs`
  (SRL does this for GCC 14) or a syscalls stub. The game uses `sprintf`, `memcpy`, `memset`,
  `strcpy`, `strcat`, `strlen`, `abs`, `abort`.

## Compile survey — HISTORICAL (state before the port; see 'Result' below) (all 38 root .C, `-x c -m2 -O2 -std=gnu89 -fgnu89-inline -fno-builtin -DNDEBUG`)
Only 5 files fail, 10 errors total:
* LEVEL.C:29 (macro LOADPART at :64) — `((char *)level_cutPlane) = ...` cast-as-lvalue (GCC 2.x extension).
* MEGAINIT.C:273, SCL_FUNC.C:817 — stray Ctrl-Z (0x1A) at end of file.
* V_BLANK.C:13-15,26 — `inputQ`, `lastInputSample`, `inputAccum`, `abcResetEnable` defined without
  `volatile` but declared `volatile` in v_blank.h (GCC 2.7 tolerated it). Add `volatile` to the definitions.
* WALLS.C:1287-1288 — `const int width/height` (line 1268-1269) modified under `#if MIPMAP` (MIPMAP is 1). Drop `const`.
Also needed:
* `-fcommon`: `stat_*`, `camera`, `clr256`, `meter_*`, `saveGames`, `bupWork`… are tentative
  definitions in headers, defined in several objects (pre-GCC-10 COMMON semantics).
* `extern inline` in UTIL.H (fixMul) → `-fgnu89-inline` (or -std=gnu89).
* `#pragma interrupt` (V_BLANK.C:147, `userBreakBlam`, debug only) → `__attribute__((interrupt_handler))`.
* `pollhost()` (SRUINS.C:2307, UTIL.C:227/252) is the SN debugger poll → stub.
* `mem2Start=(int)&end` (UTIL.C:345) → linker must define `end`/`_end` after .bss.
* `_stackinit` (UTIL.C:30) → the entry code must load r15 from it (that is what SN's crt0 did).
* MEMCPY.S is GNU-as syntax (Morita memcpy, `_qmemcpy`) but its `.align 4` meant 4 bytes to the SN
  assembler and 2^4 = 16 bytes to GNU as -> `.balign 4` (verified byte-identical to the CPEs, 154 B). WALLASM.S, START.S, LINK.S,
  BOOT/*.S are SNASM syntax. BOOT/*.S are the IP.BIN sources (use SRL's `modules/sgl/IP.BIN` instead for a test ISO).
* Build defines: none for the US release (PAL / JAPAN / FLASH / PSYQ / STATUSTEXT are variants).
  Keep asserts (no NDEBUG) to match the original CPEs; NDEBUG=1 as an option.

## Memory map (from sources)
* Code+data loaded at 0x06004000 (both programs). validPtr() accepts 0x00200000-0x002fffff (low
  work RAM, `mem_malloc(0,…)` area, mem1Start=0x200000, end 0x300000) and 0x06004000-0x060fffff
  (high work RAM, `mem_malloc(1,…)` from `&end` to 0x6100000).
* Stack: `mystack[5096]` ints inside .bss (`_stackinit` = top). `_checkStack` asserts r15 > mystack+0x100.

## Result

* `make` (via `build.ps1`) and `make NDEBUG=1` build **INIT.BIN / MAIN.BIN / KEYGEN.BIN** from a clean
  tree, 0 errors, 0 link warnings. Sizes (text+data): INIT 117 036 (orig 146 660), MAIN 287 452
  (orig 326 976), KEYGEN 106 812 (orig 136 980) — GCC 14 -O2 is denser than SN's GCC 2.7; .bss of MAIN
  is within 1% of the original (same data layout).
* Hand-translated asm is **byte-identical** to the shipped CPEs (verified by rebuilding the flat
  images from the CPE chunks and searching): `_qmemcpy` 154 B (after `.balign`), `_executeLink` 28 B,
  `_project_point` 56 B, `_rectTransform` 228 B, `_normTransform` 248 B (modulo the two R_SH_DIR32
  pool slots). `crt0.s` reproduces the SN crt0 found in the CPEs (r15 from `_stackinit` if non-NULL,
  .bss clear, `jsr _main`) — CPE SETREG 0x58 is the entry **PC**, not r15.
* The game's `SCL_FUNC.o` wins over `SEGA_SAT.A(scl_func.o)` (never pulled). `_sbrk` is trapped by
  `shim/syscalls.c` so newlib can never carve the game's `mem_malloc(1)` heap (both start at `end`).
* Tagged edits: LEVEL.C, V_BLANK.C, WALLS.C (`#define MIPMAP 0`), UTIL.C, UTIL.H,
  ART.H, MEMCPY.S (.balign ×7), MEGAINIT.C/SCL_FUNC.C (Ctrl-Z byte, untaggable). **Real pre-existing
  bug**: UTIL.C:350 `stackPos[2]=0` with `int stackPos[2]` — under GCC 2.7's static layout it
  clobbered `areaEnd[0]` (rewritten right after, harmless); GCC 14 lays statics in reverse order so it
  clobbered `memStack[0][0]` and every `mem_malloc(0,…)` returned 0 → INIT could never launch MAIN.
* CFLAGS made conservative to match GCC 2.7 semantics: `-fno-strict-aliasing -fwrapv
  -fno-delete-null-pointer-checks -fno-toplevel-reorder -fno-aggressive-loop-optimizations`.
* **Runtime**: with the retail PowerSlave data under `cd/`, `make iso` produces a disc that boots
  and plays (Ymir).  Two library-era fixes were needed on top of the compile port: `SCL_VBLV.C`
  (SBL 2.10 frame-change semantics -- under SBL 6.01's scl_vblv.o, TVMR.VBE was never latched and
  the VDP1 framebuffers were never erased) and `#define MIPMAP 0` in WALLS.C/PIC.C (the mip path
  is unfinished and wrong three ways; retail shipped without it).
* Remaining gaps: JAPAN variant does not link (PIC.o must join INIT/KEYGEN); FLASH/ and OLDJAP/ are
  diverged trees outside the build; MOV.C:212 `-Wsequence-point` is the one warning that may change
  behaviour between compilers; the SBL headers and binaries under `sdk/sbl6/` are SEGA's and are not
  committed (`tools/import_sbl6.sh` regenerates them from the SBL 6.01 release).

## Boot — `build/slavedriver.iso` was a black screen

Static findings, before any test:

* `assertFail` (UTIL.C:181) prints "Write This Down" on screen and a failed `GFS_Init` ends in
  `SYS_EXECDMP` (the BIOS CD screen): a **black** screen is therefore a silent hang, not an assert.
* `fadeSegaLogo()` (INITMAIN.C) fades the logo to black (colour offset −255), then `megaInit()`
  waits for the vblank interrupt (`syncVbI`, MEGAINIT.C:171) and `PER_GET_SYS()` waits for the
  SMPC. Both are possible infinite waits; `vbIcnt` and `PEEK_W` are properly `volatile` (codegen
  checked).
* **Retail IP vs SaturnRingLib IP**: bytes 0x100-0xDFF (the SEGA security code) are identical; the
  retail one carries a single "U" region code then `mov.l @(1,pc),r0 ; jmp @r0 ; .long 0x06004000`
  (offset 0xE20, IP size 0xE2C) — **no boot program at all**: the game's `megaInit` is the first
  sysinit. SaturnRingLib's IP (`modules/sgl/IP.BIN`, four JTUE regions) chains the SGL sysinit at
  0xE80 (vblank vectors through 0x06000300, cache purge, VDP/SCU/sound clear — the same work
  MEGAINIT.C does) before jumping.
* The ISO layout is sound: `0.BIN` (INIT) is the first entry of the root directory, 98 files like
  the retail disc; `_start` is at 0x06004000 and `.data` is contiguous with `.text`.

Tools this port adds for that hunt:

* `make BOOTPROBE=1` (`build.ps1 -Probe`) → `build/probe/`: `INITMAIN.C` paints the whole screen
  one colour after each step of `main()` (`BOOT_PROBE`, VDP2 back screen: TVMD on, BGON 0, CLOFEN 0,
  BKTAU/BKTAL 0, colour word at VRAM 0): magenta = `main` entered, red = after `fadeSegaLogo`,
  green = after `megaInit`, blue = after `fs_init`, yellow = after `mem_init`, white = after
  `PER_GET_SYS`.
* `make iso-ipjump` → `slavedriver-ipjump.iso`: the SaturnRingLib IP with its boot program replaced
  by the retail-style stub (`tools/mk_ip_jump.py`, header 0xE0 = 0xE8C).

Results (emulator): discs built from the **retail** `0`/`MAIN.BIN` on our own ISO recipe boot, which
clears the recipe, the IP and the emulator. Our INIT (with either IP) went
magenta→red→green→blue→**yellow** then froze, PC 0x060040DC-E2 = the
`while (!(sys_data=PER_GET_SYS()))` loop. With the INTBACK re-arm of `waitSystemData`: **orange** =
INTBACK issued, no SMPC interrupt ever.

**Root cause**: `INT_SetScuFunc` (SBL 6.01 INT library) read its `___interrupt_handler[]` table at
`.data + 0x40` (0x060204F0 instead of 0x060204B0), so it registered `handler87` for SCU vector 0x47
instead of `handler71`; trampoline 87 reads `___interrupt_vector[87]` = NULL and does nothing. 0x40
is the COFF VMA of `.data` in `int.o`: `objcopy -I coff-sh -O elf32-sh` keeps the section VMAs, and
the ELF link then resolves `.data - 0x40` (the COFF reloc, in-place value 0x40) to `.data + 0x40`.
Every section-relative reference of the 66 affected objects (of 342) was shifted the same way. Fix:
link the `.A` archives **as COFF** (`-Wl,-b,coff-sh sdk/sbl6/lib/coff/SEGA_SAT.A -Wl,-b,elf32-sh`);
verified to pull the same 39 members and to read the table at the `.data`(int.o) address from the
`.map`. The Makefile's link rule keeps a guard on that address.
