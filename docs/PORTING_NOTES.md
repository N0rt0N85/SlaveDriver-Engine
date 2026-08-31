# Porting SlaveDriver to a modern GCC toolchain — working notes (2026-08-30)

Goal of this fork: make the 1996 SlaveDriver (PowerSlave Saturn) sources build with the
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
* `C:\Users\pcico\Projects\Mimas\SaturnRingLib\Compiler\sh2eb-elf\bin` must be on PATH
  (gcc silently fails with rc=1 otherwise — it cannot find cc1/DLLs).
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

## Result (2026-08-30)

* `make` (via `build.ps1`) and `make NDEBUG=1` build **INIT.BIN / MAIN.BIN / KEYGEN.BIN** from a clean
  tree, 0 errors, 0 link warnings. Sizes (text+data): INIT 116 980 (orig 146 660), MAIN 288 048
  (orig 326 976), KEYGEN 106 724 (orig 136 980) — GCC 14 -O2 is denser than SN's GCC 2.7; .bss of MAIN
  305 408 vs 302 488 original (same data layout).
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
  behaviour between compilers; the SBL binaries in `sdk/sbl6/lib` are SEGA's (licence decision before
  publishing the fork).

## Boot (2026-08-30) — `build/slavedriver.iso` = écran noir sur Ymir

Constats (statiques, avant tout test) :

* `assertFail` (UTIL.C:181) affiche « Write This Down » à l'écran et un échec de `GFS_Init` finit
  en `SYS_EXECDMP` (écran CD du BIOS) : un écran **noir** est donc un hang silencieux, pas un assert.
* `fadeSegaLogo()` (INITMAIN.C) fond le logo au noir (offset couleur −255) puis `megaInit()` attend
  l'interruption vblank (`syncVbI`, MEGAINIT.C:171) ; `PER_GET_SYS()` attend le SMPC. Les deux sont
  des attentes infinies possibles ; `vbIcnt` et `PEEK_W` sont bien `volatile` (codegen vérifié).
* **IP retail vs IP SRL** : les 0x100-0xDFF (code de sécurité SEGA) sont identiques ; le retail n'a
  qu'un code région « U » puis `mov.l @(1,pc),r0 ; jmp @r0 ; .long 0x06004000` (offset 0xE20, IP size
  0xE2C) — **aucun programme de boot** : `megaInit` du jeu est le premier sysinit. L'IP de SRL
  (`modules/sgl/IP.BIN`, 4 régions JTUE) enchaîne à 0xE80 le sysinit SGL (vecteurs vblank via
  0x06000300, purge cache, effacement VDP/SCU/son — le même travail que MEGAINIT.C) avant de sauter.
* La disposition de l'ISO est saine : `0.BIN` (INIT, 116 980 o) est la 1re entrée du répertoire
  racine, 98 fichiers comme le retail ; `_start` est à 0x06004000, `.data` contiguë à `.text`.

Outils livrés :

* `make BOOTPROBE=1` (`build.ps1 -Probe`) → `build/probe/` : `INITMAIN.C` peint tout l'écran d'une
  couleur après chaque étape de `main()` (`BOOT_PROBE`, écran de fond VDP2 : TVMD on, BGON 0,
  CLOFEN 0, BKTAU/BKTAL 0, couleur à VRAM 0) : magenta = entrée de `main`, rouge = après `fadeSegaLogo`, vert = après
  `megaInit`, bleu = après `fs_init`, jaune = après `mem_init`, blanc = après `PER_GET_SYS`.
* `make iso-ipjump` → `slavedriver-ipjump.iso` : IP SRL dont le programme de boot est remplacé par
  le stub retail (`tools/mk_ip_jump.py`, header 0xE0 = 0xE8C).

Résultats (propriétaire, Ymir) : disques `retailbins` (0/MAIN.BIN retail sur notre recette) **bootent**
⇒ recette/IP/émulateur hors de cause ; nos INIT (IP SRL et IP saut) : magenta→rouge→vert→bleu→**jaune**
puis gel, PC 0x060040DC-E2 = la boucle `while (!(sys_data=PER_GET_SYS()))`. Avec le ré-armement
INTBACK de `waitSystemData` : **orange** = INTBACK émis, jamais d'interruption SMPC.

**Cause trouvée** : `INT_SetScuFunc` (lib INT 6.01) lisait sa table `___interrupt_handler[]` à
`.data + 0x40` (0x060204F0 au lieu de 0x060204B0) ⇒ `SYS_SETUINT(0x47, handler87)` au lieu de
`handler71` ⇒ le trampoline 87 lit `___interrupt_vector[87]` = NULL ⇒ rien. 0x40 = la VMA COFF de
`.data` dans `int.o` : `objcopy -I coff-sh -O elf32-sh` conserve les VMA de section et l'éditeur ELF
résout `.data - 0x40` (reloc COFF, valeur en place 0x40) en `.data + 0x40`. Toute référence
section-relative des 66/342 objets SBL concernés était décalée. Fix : lier les `.A` **en COFF**
(`-Wl,-b,coff-sh sdk/sbl6/lib/coff/SEGA_SAT.A -Wl,-b,elf32-sh`), vérifié : même 39 membres tirés,
table lue à l'adresse de `.data`(int.o) du `.map` ; garde-fou dans la règle de lien du Makefile.
