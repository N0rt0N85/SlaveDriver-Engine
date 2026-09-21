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

The checkpoints continue all the way to the title screen, because everything from the Playmates
logo to the title runs with the display disabled: a hang anywhere in there is an ordinary black
screen and says nothing about where it stopped.

Two things are worth knowing before reading the table. **There are two different `playIntro()`
functions**: the one in INITMAIN.C (INIT.BIN) plays the logos and `OPEN.MOV`, the one in INTRO.C
(MAIN.BIN) is the title screen. They are separate functions in separate programs, and INIT loads
MAIN off the CD between them (`link("+MAIN.BIN")`). And **a probe cannot just poke the registers
once**: `displayEnable(0)` writes SBL's own copy of the VDP2 registers, `SCL_ScrollShow()` restores
that copy at every vblank-in, so a colour painted from the main path is erased within a frame.
`bootProbePaint` (V_BLANK.C) therefore repaints from vblank-out, the last VDP2 write of the field,
which also means the colour survives after the main path has stopped.

The colour **throbs** between full and half brightness while the vblank interrupt still runs.
A **slow** throb (about one second) is INIT.BIN, a **fast** one (about a quarter second) is
MAIN.BIN, which is how the two programs share one set of colours. A colour that sits perfectly
steady means the interrupt stopped too — the CPU itself is down, not just the main path.

| Colour | INIT.BIN — slow throb | MAIN.BIN — fast throb |
|--------|------------------------|------------------------|
| magenta | `main` entered | MAIN.BIN reached (its first instruction ran) |
| red | `fadeSegaLogo` returned | backup RAM read, about to enter the title screen |
| green | `megaInit` returned | title screen entered |
| blue | `fs_init` returned | sound, VDP2, sprites, fonts and the pic system up |
| yellow | `mem_init` returned | `stopCD()` returned |
| white | `PER_GET_SYS` answered | the 512 KB VDP2 VRAM clear finished |
| cyan | VDP2, sprites, fonts and the vblank interrupt up | `INTRO.PCS` loaded (pic set, pics, two sounds) |
| orange | about to load MAIN.BIN off the CD | the title picture is in VRAM (`loadVDPPic`) |
| grey | the logos and `OPEN.MOV` returned | `getDateTime()` returned (SMPC clock) |
| purple | — | CD music started; the title screen is one call away |

The probe switches itself off (`BOOT_PROBE2(0)`) once the title screen has the display back, so on
a `BOOTPROBE=1` disc reaching the title screen looks exactly like a normal boot.

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

## Near plane — the wall grid clamped z instead of clipping against it

The two vertex transforms in `wallasm_gnu.s` did not near-clip: they projected every wall grid
point against `max(z, F(33))`, a hardcoded constant, leaving x and y untouched. That is a clamp,
not a clip — the vertex is not walked along its edge to the plane, it is divided by 33 instead of
by its own z, so the projected point contracts radially toward the vanishing point by z/33. x and
y shrink together, the corner slides along the ray from the screen centre and the edge pivots.

For a wall the player is touching, every grid point shares the same perpendicular distance `d`, so
all the points nearer than 33 land on one column: the near part of the wall collapses onto a
vertical seam at `FOCALDIST*d/33` px from the screen centre, and the band between that seam and the
screen edge is never painted. Retail never showed it because PowerSlave's radius is 47: the seam
sits at 228 px, off a 160 px half-screen. It becomes visible as soon as the radius drops below 33 —
Duke's cylinder at 20 puts it at 97 px, the Doom player at 16 puts it at 78 px. `#define
TILENEARCLIP F(33)` (WALLS.C) was the C-side record of the constant and `clipZTile` its mirror;
both were already unreferenced.

`NEAR_CLIP` was made a per-game parameter (32 / 18 / 10) but the 33 in the assembly was not, so the
face and the grid disagreed: `clipZ` (WALLS.C) cuts the face correctly at `NEARCLIP`, while the
grid inside that face was projected against 33. The floor is now the last argument of
`rectTransform` (`@(48,r14)`) and of `normTransform` (`r8`, which joins the saved set), and all
four call sites pass `NEARCLIP`. The inner loops lose an instruction each. The default build's
floor moves 33 -> 32, its `NEAR_CLIP`.

Consequence: projected coordinates near the plane grow by the ratio of the two floors (3.3x in the
Doom build) and can leave the VDP1 range of -1024..1023. The master path already handles that
(`EZ_distSprVClip`, WALLASM.H: exact V windowing of the pattern, `VDP1DIAG` paints what it cannot
window); the slave queue went straight to `EZ_specialDistSpr` and now goes through the same call.
The U axis still has no windowing — a horizontal overflow shows up as a red cell on a `VDP1DIAG`
disc.

Lowering the floor left two artefacts, both from the same place: the floor decides *which* z is
used, it does not make the clamp a clip. A grid point **behind** the eye still got a projection
invented for it, and since the clamp keeps x, its screen x is `FOCALDIST*x/T` whatever z is — so
as a wall's near end swings behind the player its view x goes to zero and the corner drifts to the
middle of the screen. And the tile that straddles the plane has its near corner at
`FOCALDIST*d/T`, with the whole pattern spread affinely from there: at T = 10 and d = 16 the
border shows texel 41% where perspective wants 12%, i.e. the tile reads ~1.5x too zoomed. Moving
the corner further out makes that worse, moving it inside the screen edge leaves a hole, so the
screen edge itself is the optimum — and a tile's pattern cannot be windowed in U (WALLASM.H), so
the corner is the only handle there is.

`repairNearRow` (WALLS.C) therefore moves every flagged point of a grid row to where that row
crosses the cut, the further along U of the near plane `z = NEARCLIP` and the lateral frustum
plane `|x| = z`. The first fixes the drift — the flagged points land on a fixed world position
instead of a projection of a point that has none. The second puts the straddling tile's corner
exactly on the screen edge, which leaves no hole and is the least-stretched position available;
the residual, ~14% squashed in the same case, is inherent to one sprite per cell. It costs two
divides per row and runs only on a wall with a corner nearer than the plane (`wallCrossesNear`).

## Cells that leave the view — the affine stretch (supersedes `repairNearRow`)

"The screen edge itself is the optimum" above is wrong. The VDP1 lays a tile over a cell's four
corners affinely, so with two corners off the view the part left on screen is zoomed `z_e/z_n` —
`z_e` the depth where the cell's edge leaves the view, `z_n` the off-view corner's. Walking along
the surface, that zoom grows `1 + z_f/z_n` times faster than it should (`z_f` the corner in view;
`z_e` stays put): never less than twice, the "texture grows twice too fast" of the console report.
The edge squeezes the whole tile into the part in view instead: a wall 64 away, a 64 cell,
jumped from x4.7 to x0.16 as its corner crossed the plane. Floors and ceilings had no repair.

The corner that makes the affine map exact both at the corner in view (A) and where the edge
leaves the view (e) is `s* = s_A + (s_n - s_A) z_n/z_e`, past `e` on the edge's own line, so the
part of the edge in view does not move — the projection of `Q = N + A (z_e - z_n)/z_A` at depth
`z_e`, defined behind the eye too. `fitRow` (WALLS.C) fits both ends of every grid row that
leaves the view's width — the window's planes, so split screen too (`|x| = z` was solo only) — or
passes the near plane inside it (a lintel walked under), and every point further out on it.

`s*` is capped. For a corner behind the eye it lands `1 + k` times further out than `e`,
`k = (z_e - z_n)/z_A` (over 26 with a 256 cell): past a short, and — first — so tall that
`vdp1Fit`'s V window, which keeps the INTERSECTION of both V edges' intervals, cuts the edge in
view as well and opens a hole. The corner goes to `s_e + lam (s* - s_e)`, `s_e` the projection of
`e`, `lam = (z_e - rho z_A)/(rho z_A k)` clamped to [0,1], `rho = max(-ymin, ymax)/VDP1LIM`:
the cut then stays past the window's top and bottom. No height in it, so a wall's V edges stay
vertical; continuous across the near plane; `lam < 1` only on cells long against the distance,
which show squeezed there as before rather than cut.

`fitFace` does it on mesh faces in screen space, `z_n/z_e = r + sg(1 - r)`, for a corner with one
neighbour in view and the other off it. A fit that switches on or off between two images moves the
texture of the whole face, the part in view too — the floor jumps — so it eases to nothing over
16 px (8 units at the near plane) wherever its conditions change, and each moved edge keeps
off the window by an exact test, the largest share of the fit that does being kept. Left alone:
a corner further than its anchor (squeezed, not stretched), and a mesh corner behind the plane —
the face where it has two neighbours in view cannot follow, and the two would part. No command
added; a corner in front of the plane only comes in, so the VDP1 walks less off the screen there.
