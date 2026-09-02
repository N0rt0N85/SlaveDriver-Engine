# Makefile — SlaveDriver (PowerSlave Saturn) with the sh2eb-elf GCC 14.2 toolchain
# GCC14: replaces the SN Systems Psy-Q build (asmsh/ccsh/psylink -> .CPE) that produced
# INIT.CPE / SRUINS.CPE / KEY.CPE.  See docs/PORTING_NOTES.md for the whole story.
#
#   make            -> build/INIT.BIN build/MAIN.BIN build/KEYGEN.BIN (+ .elf/.map)   [debug, like the CPEs]
#   make NDEBUG=1   -> the same under build/ndebug/ (asserts off); the two trees never mix
#   make BOOTPROBE=1 iso iso-ipjump -> build/probe/: INIT paints a colour per boot step (INITMAIN.C
#                      BOOT_PROBE) + two discs: SRL IP (SGL sysinit first) / jump-only IP (retail style)
#   make BOOTPROBE=1 iso-retailbins -> the RETAIL 0 + MAIN.BIN (refs/extract/PS) on our disc recipe, both IPs
#   make size       -> text/data/bss per program vs the original CPE spans
#   make iso        -> build/slavedriver.iso: bootable test disc (SRL generic IP.BIN + cd/ data)
#   make clean      -> remove build/
#   make -k         -> keep going: compile everything that compiles today, report the rest
#
# Requires GNU make (C:\msys64\usr\bin\make) and the toolchain on PATH — or leave TOOLCHAIN_BIN
# pointing at it (default = the copy SaturnRingLib unpacks); build.ps1 does both.

# ---------------------------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------------------------
# SaturnRingLib (https://github.com/ReyeMe/SaturnRingLib) provides both the sh2eb-elf toolchain
# (setup_compiler.bat) and the generic IP.BIN used by `make iso`: point SRL_DIR at your checkout,
# or put sh2eb-elf-gcc on PATH and set IPFILE yourself.  build.ps1 locates both on Windows.
SRL_DIR       ?= ../SaturnRingLib
TOOLCHAIN_BIN ?= $(SRL_DIR)/Compiler/sh2eb-elf/bin
ifneq ($(wildcard $(TOOLCHAIN_BIN)/sh2eb-elf-gcc*),)
  export PATH := $(TOOLCHAIN_BIN):$(PATH)
endif
# MSYS2 make started from cmd/PowerShell without C:\msys64\usr\bin on PATH: append /usr/bin
# (= C:\msys64\usr\bin under MSYS, a no-op elsewhere) so cp/mkdir/xorrisofs are always found.
export PATH := $(PATH):/usr/bin
CROSS   ?= sh2eb-elf-
CC       = $(CROSS)gcc
AS       = $(CROSS)gcc
LD       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
NM       = $(CROSS)nm
SIZE     = $(CROSS)size
AR       = $(CROSS)ar

# ---------------------------------------------------------------------------------------------
# Build tree: debug (default, matches the shipped CPEs: asserts + file-name strings present)
# or NDEBUG=1 — separate directories so debug and NDEBUG objects can never be mixed.
# ---------------------------------------------------------------------------------------------
ifeq ($(NDEBUG),1)
  BUILD   := build/ndebug
  DEFINES := -DNDEBUG
else
  BUILD   := build
  DEFINES :=
endif
# BOOTPROBE=1: INITMAIN.C paints a colour after each step of main() (see BOOT_PROBE there);
# its own tree so a probe INIT can never be mistaken for the real one.  Combines with NDEBUG=1.
ifeq ($(BOOTPROBE),1)
  BUILD   := $(BUILD)/probe
  DEFINES += -DBOOTPROBE
endif
# STATUSTEXT=1: the game's own in-game metrics overlay (SRUINS.C: fps, polys, calc/draw
# times from the hblank line counter, free memory, texture-slot swaps).  Own tree, like
# BOOTPROBE, so the flagged objects never mix with the normal ones.
ifeq ($(STATUSTEXT),1)
  BUILD   := $(BUILD)/stext
  DEFINES += -DSTATUSTEXT
endif
OBJDIR   := $(BUILD)/obj

# ---------------------------------------------------------------------------------------------
# Flags (baseline from docs/PORTING_NOTES.md)
#   -x c            the sources are *.C (upper case) -> GCC would compile them as C++
#   -std=gnu89 -fgnu89-inline   `extern inline` (UTIL.H fixMul) with 1996 semantics
#   -fcommon        tentative definitions in headers (stat_*, camera, clr256, meter_* ...)
#   -fno-builtin    the game ships its own memcpy/qmemcpy and friends
#   -g              symbols in the .map / .elf (objcopy -O binary drops them from the .BIN)
#   GCC14: GCC 2.7 semantics the code base relies on (docs/PORTING_NOTES.md):
#   -fno-delete-null-pointer-checks   address 0 is a valid Saturn address (BIOS ROM); also turns off
#                   -fisolate-erroneous-paths-dereference, which rewrote NULL-store paths (AI2.C:635,
#                   OBJECT.C:419, WALLS.C:2211, AI.C:1439) into newlib abort() -> silent hang
#   -fno-toplevel-reorder   statics in source order (GCC 14 emits them reversed: UTIL.C mem_init's
#                   out-of-bounds stackPos[2] moved from areaEnd[0] onto memStack[0][0])
#   -fno-strict-aliasing    type punning through char buffers (MENU.C/PICSET.C/PRINT.C/SRUINS.C)
#   -fwrapv                 wrapping signed arithmetic
#   -fno-aggressive-loop-optimizations   no UB-derived trip counts on the 1-element COMMON arrays
# ---------------------------------------------------------------------------------------------
INCLUDES := -Isdk/sbl6/include -Ishim -I.
CFLAGS   := -x c -m2 -O2 -std=gnu89 -fgnu89-inline -fcommon -fno-builtin -Wall -g $(DEFINES) $(INCLUDES) -MMD -MP \
            -fno-delete-null-pointer-checks -fno-toplevel-reorder -fno-strict-aliasing -fwrapv -fno-aggressive-loop-optimizations
ASFLAGS  := -m2 -g $(DEFINES) $(INCLUDES)
# Link: no crt0/crti from the toolchain (crt0.s is ours), our layout, map file, newlib syscall
# stubs.  -specs=nosys.specs appends `--start-group -lgcc -lc -lnosys --end-group` after our
# objects and libs: libgcc for the GCC-2.7 helper names the SBL objects use (__ashiftrt_r4_N,
# __sdivsi3, __muldf3…), newlib for sprintf/mem*/str*/abs/abort, libnosys for the syscall stubs
# newlib's stdio references.
# GCC14: shim/syscalls.c (linked before the group) overrides libnosys _sbrk with a failing one.
LDFLAGS  := -m2 -nostartfiles -T saturn.ld -specs=nosys.specs -Wl,--no-warn-rwx-segments
# GCC14: the SBL archives are linked AS COFF (`-b coff-sh` ... `-b elf32-sh`), never converted:
# `objcopy -I coff-sh -O elf32-sh` keeps the COFF section VMAs and the ELF link then resolves
# every section-relative reference (statics, tables, literals) shifted by that VMA -- 66 of the
# 342 SBL objects are affected (see sdk/sbl6/README.md).  GNU ld reads coff-sh in a final link.
# SEGA_SAT.A = SEGA's all-in-one archive (the separate .A files add nothing we use).
LIBDIR   := sdk/sbl6/lib/coff
LIBS     := -Wl,-b,coff-sh $(LIBDIR)/SEGA_SAT.A -Wl,-b,elf32-sh

# ---------------------------------------------------------------------------------------------
# Object closures (docs/PORTING_NOTES.md "Program structure").
#
# NOT linked standalone:
#   AIRBUB.C   #included by SRUINS.C
#   STATBAR.C  #included by ART.C (the stat_* / clr256 pixel tables)
#   DIAL.C     only `static` tables (airMeterDial ...), nothing #includes it and no object
#              references any symbol of it (nm survey) -> dead file, not built
#   ART.C      IS linked, into MAIN only: BIGMAP/INTRO/PIC/SRUINS declare `unsigned char stat_bar[];`
#              (art.h) which -fcommon turns into 1-byte COMMON placeholders; the real 13 KB
#              tables live in ART.o (via statbar.c).  Without ART.o the link still SUCCEEDS
#              (COMMON silently wins) and the game reads garbage -> keep it in MAIN_OBJS.
#              INIT and KEYGEN reference none of those symbols (nm survey).
#   The original LINK.S / START.S / WALLASM.S are SNASM syntax and are NOT assembled: their GNU
#   translations are link_gnu.s / crt0.s / wallasm_gnu.s (the `_gnu` suffix because on the
#   case-insensitive Windows checkout `link.s` IS `LINK.S`).  MEMCPY.S is already GNU syntax.
# ---------------------------------------------------------------------------------------------
# Closures derived from (a) the assert() file-name strings inside the original
# CPEs (INIT.CPE: dma file initmain level local mov picset print sound spr util; KEY.CPE: the
# same with keygen instead of initmain; megainit/scl_func/v_blank have no assert so they are
# invisible there) and (b) an nm-based transitive closure over the compiled objects:
#   INIT   needs MEGAINIT (_megaInit), SCL_FUNC (SCL_Open/Close/MoveTo/SetColRamMode…),
#          V_BLANK (SetVblank, PadWorkArea, inputAccum…) — and NOT WALLS: WALLS.o references
#          mapPic/getPicClass (PIC), pushProfile (PROFILE), level_frame/level_sequence (SEQUENCE),
#          currentState (SRUINS), signalObject (OBJECT), sectorSpriteList (SPRITE) and would drag
#          the whole game into the 147 KB INIT.  The slave-CPU start (startSlave) is MAIN-only.
#   KEYGEN needs FILE/LOCAL/MOV/SOUND/PICSET/LEVEL (per KEY.CPE) + SCL_FUNC (SclProcess,
#          Scl_s_reg) + V_BLANK (vtimer, lastInputSample) + link.o (FILE.C link() -> executeLink).
#   MAIN   = everything else + the four shared files + WALLS.
# GCC14: SCL_VBLV overrides the library's scl_vblv.o (SBL 2.10 frame-change semantics --
# 6.01 raises and restores TVMR.VBE inside one vblank-out interrupt, so the VDP1 never
# erases; see the header of SCL_VBLV.C).  Same override pattern as SCL_FUNC vs scl_func.o.
COMMON4      := LEVEL MEGAINIT SCL_FUNC SCL_VBLV V_BLANK
INIT_C       := DMA FILE INITMAIN LOCAL MOV PICSET PRINT SOUND SPR UTIL $(COMMON4)
MAIN_C       := AI AI2 AICOMMON ART BIGMAP BUP DMA FILE HITSCAN INTRO LOCAL MAP MENU OBJECT PIC \
                PICSET PLAX PRINT PROFILE ROUTE SEQUENCE SOUND SPR SPRITE SRUINS UTIL WEAPON $(COMMON4) WALLS
KEYGEN_C     := DMA FILE KEYGEN LOCAL MOV PICSET PRINT SOUND SPR UTIL LEVEL SCL_FUNC SCL_VBLV V_BLANK

# crt0 first (its .text.crt0 section is placed first by saturn.ld anyway, but keep the order
# explicit: the .BIN must start with the entry — executeLink jumps to MAIN's first byte).
CRT0_OBJ     := $(OBJDIR)/crt0.o
MEMCPY_OBJ   := $(OBJDIR)/MEMCPY.o
LINK_OBJ     := $(OBJDIR)/link_gnu.o
WALLASM_OBJ  := $(OBJDIR)/wallasm_gnu.o
SNSTUBS_OBJ  := $(OBJDIR)/sn_stubs.o
SYSCALLS_OBJ := $(OBJDIR)/syscalls.o   # GCC14: _sbrk trap so newlib can never allocate over the game's mem_malloc(1) area (starts at `end`)

INIT_OBJS    := $(CRT0_OBJ) $(addprefix $(OBJDIR)/,$(addsuffix .o,$(INIT_C)))   $(MEMCPY_OBJ) $(LINK_OBJ) $(SNSTUBS_OBJ) $(SYSCALLS_OBJ)
MAIN_OBJS    := $(CRT0_OBJ) $(addprefix $(OBJDIR)/,$(addsuffix .o,$(MAIN_C)))   $(MEMCPY_OBJ) $(WALLASM_OBJ) $(LINK_OBJ) $(SNSTUBS_OBJ) $(SYSCALLS_OBJ)
KEYGEN_OBJS  := $(CRT0_OBJ) $(addprefix $(OBJDIR)/,$(addsuffix .o,$(KEYGEN_C))) $(MEMCPY_OBJ) $(LINK_OBJ) $(SNSTUBS_OBJ) $(SYSCALLS_OBJ)

ALL_OBJS     := $(sort $(INIT_OBJS) $(MAIN_OBJS) $(KEYGEN_OBJS))
PROGRAMS     := INIT MAIN KEYGEN
BINS         := $(addprefix $(BUILD)/,$(addsuffix .BIN,$(PROGRAMS)))
ELFS         := $(addprefix $(BUILD)/,$(addsuffix .elf,$(PROGRAMS)))

# Original SN builds (debug), byte spans 0x06004000..end from the .CPE headers — for `make size`.
ORIG_SPAN_INIT   := 146660
ORIG_SPAN_MAIN   := 326976
ORIG_SPAN_KEYGEN := 136980

# ---------------------------------------------------------------------------------------------
.SUFFIXES:
MAKEFLAGS += --no-builtin-rules
.PHONY: all bins elfs objs size iso iso-ipjump iso-retailbins clean help
.SECONDARY:
.DELETE_ON_ERROR:

all: $(BINS)
bins: $(BINS)
elfs: $(ELFS)
objs: $(ALL_OBJS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# --- C: every root *.C, compiled as C ---------------------------------------------------------
$(OBJDIR)/%.o: %.C | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- shim (lower-case .c, keeps the same flags) -----------------------------------------------
$(SNSTUBS_OBJ): shim/sn_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(SYSCALLS_OBJ): shim/syscalls.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- assembly: explicit rules on purpose.  A `%.o: %.s` pattern would, on a case-insensitive
#     file system, resolve `link.s` to the SNASM original LINK.S; hence the *_gnu.s names.
$(CRT0_OBJ): crt0.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -c $< -o $@
$(LINK_OBJ): link_gnu.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -c $< -o $@
$(WALLASM_OBJ): wallasm_gnu.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -c $< -o $@
$(MEMCPY_OBJ): MEMCPY.S | $(OBJDIR)
	$(AS) $(ASFLAGS) -c $< -o $@

# --- link: objects BEFORE the archive so the game's SCL_FUNC.o (same symbol set as the
#     library's scl_func.o, verified with nm) satisfies every SCL_* reference first and the
#     archive member is never pulled.  If a future SBL member drags scl_func.o in anyway
#     ("multiple definition of _SCL_..."), the fallback is a copy of the archive without it:
#         cp sdk/sbl6/lib/coff/SEGA_SAT.A build/SEGA_SAT_noscl.A
#         $(AR) --target=coff-sh d build/SEGA_SAT_noscl.A scl_func.o   and point LIBS at it
$(BUILD)/INIT.elf:   $(INIT_OBJS)   saturn.ld
$(BUILD)/MAIN.elf:   $(MAIN_OBJS)   saturn.ld
$(BUILD)/KEYGEN.elf: $(KEYGEN_OBJS) saturn.ld
$(ELFS):
	$(LD) $(LDFLAGS) -Wl,-Map,$(@:.elf=.map) $(filter %.o,$^) $(LIBS) -o $@
	@# GCC14: regression check for the COFF section-relative relocation bug (see LIBS above):
	@# INT_SetScuFunc must read its trampoline table exactly at int.o's .data.
	@t=$$($(OBJDUMP) -d $@ | awk '/<_INT_SetScuFunc>:/,/rts/' | grep -o '! 60[0-9a-f]*' | sed -n 2p | cut -c3-); \
	 d=$$(grep -A0 '^ \.data .*SEGA_SAT.A(int\.o)' $(@:.elf=.map) | awk '{print $$2}' | sed 's/^0x0*//'); \
	 if [ -n "$$t" ] && [ -n "$$d" ] && [ "$$t" != "$$d" ]; then echo "*** $@: INT_SetScuFunc reads table at 0x$$t but int.o .data is at 0x$$d (COFF reloc bug back?)"; exit 1; fi

$(BUILD)/%.BIN: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@
	@printf '%s: %s bytes (loads at 0x06004000, entry = first byte)\n' $@ $$(stat -c %s $@)

# --- sizes vs the original CPEs --------------------------------------------------------------
size: $(ELFS)
	@echo "program   text     data     bss      text+data  orig-span(CPE)  delta"
	@for p in $(PROGRAMS); do \
	  set -- $$($(SIZE) -B $(BUILD)/$$p.elf | tail -1); t=$$1; d=$$2; b=$$3; \
	  case $$p in INIT) o=$(ORIG_SPAN_INIT);; MAIN) o=$(ORIG_SPAN_MAIN);; KEYGEN) o=$(ORIG_SPAN_KEYGEN);; esac; \
	  printf '%-8s %8d %8d %8d %10d %15d %+7d\n' $$p $$t $$d $$b $$((t+d)) $$o $$((t+d-o)); \
	done
	@echo "(orig-span = 0x06004000..last CPE byte = text+data only, the original .bss started exactly at its end; compare text+data)"

# --- bootable test ISO -----------------------------------------------------------------------
#  The BIOS loads the FIRST file of the root directory at 0x06004000 (header 0xF0) and then runs
#  the IP's boot program: INIT.BIN goes on the disc as 0.BIN so it sorts first; MAIN.BIN keeps
#  its name (FILE.C link("+MAIN.BIN")).  Everything under cd/ (the game data, not distributed)
#  is added as is.
#  Two IPs (docs/PORTING_NOTES.md "Boot"):
#    iso        SRL's generic IP.BIN (SGL): after the area codes it runs SGL's system init
#               (VDP/SCU/sound-RAM clear, vblank vectors -- the same job as MEGAINIT.C) and jumps.
#    iso-ipjump the same header/security code/area codes, boot program replaced by what the retail
#               PowerSlave disc has: `mov.l @(1,pc),r0; jmp @r0` -> 0x06004000, nothing else.
IPFILE   ?= $(SRL_DIR)/modules/sgl/IP.BIN
IPJUMP   := $(BUILD)/ip_jump.bin
CDDIR    ?= cd
ISO      := $(BUILD)/slavedriver.iso
ISOJ     := $(BUILD)/slavedriver-ipjump.iso
ISOSTAGE := $(BUILD)/iso
XORRISO  ?= xorrisofs
PYTHON   ?= python
CD_DATA  := $(filter-out $(CDDIR)/README% $(CDDIR)/readme% $(CDDIR)/.gitkeep,$(wildcard $(CDDIR)/*))

# $(call mkiso,<output.iso>,<ip file>,<INIT file -> 0.BIN>,<MAIN file -> MAIN.BIN>)
define mkiso
	@test -f "$(2)" || { echo "ERROR: IP file not found at $(2) (set IPFILE=...)"; exit 1; }
	@if [ -z "$(CD_DATA)" ]; then \
	  echo "*** WARNING: $(CDDIR)/ holds no game data: the image will only contain 0.BIN (INIT) and MAIN.BIN."; \
	  echo "***          Copy the PowerSlave/Exhumed Saturn data files into $(CDDIR)/ to get a playable disc."; \
	fi
	rm -rf $(ISOSTAGE); mkdir -p $(ISOSTAGE)
	@if [ -n "$(CD_DATA)" ]; then cp -r $(CD_DATA) $(ISOSTAGE)/; fi
	cp "$(3)" $(ISOSTAGE)/0.BIN
	cp "$(4)" $(ISOSTAGE)/MAIN.BIN
	echo "NOT Abstracted by SEGA"      > $(BUILD)/ABS.TXT
	echo "NOT Bibliographiced by SEGA" > $(BUILD)/BIB.TXT
	: > $(BUILD)/CPY.TXT
	$(XORRISO) --norock -quiet -sysid "SEGA SATURN" -volid "SLAVEDRIVER" -volset "SLAVEDRIVER" \
	  -publisher "SEGA ENTERPRISES, LTD." -preparer "SEGA ENTERPRISES, LTD." -appid "SLAVEDRIVER" \
	  -abstract $(BUILD)/ABS.TXT -copyright $(BUILD)/CPY.TXT -biblio $(BUILD)/BIB.TXT \
	  -generic-boot "$(2)" -full-iso9660-filenames -o $(1) $(ISOSTAGE)
	@echo "ISO: $(1) ($$(stat -c %s $(1)) bytes): IP = $(2); 0.BIN = $(3); MAIN.BIN = $(4)"
endef

iso: $(ISO)
iso-ipjump: $(ISOJ)
$(ISO): $(BUILD)/INIT.BIN $(BUILD)/MAIN.BIN $(CD_DATA) $(IPFILE)
	$(call mkiso,$@,$(IPFILE),$(BUILD)/INIT.BIN,$(BUILD)/MAIN.BIN)
$(ISOJ): $(BUILD)/INIT.BIN $(BUILD)/MAIN.BIN $(CD_DATA) $(IPJUMP)
	$(call mkiso,$@,$(IPJUMP),$(BUILD)/INIT.BIN,$(BUILD)/MAIN.BIN)
# --- bisection discs: the RETAIL binaries (`0` = INIT, `MAIN.BIN`, extracted from the PowerSlave
#     disc into $(RETAIL_DIR), not distributed) on our ISO recipe.  Boots -> our recipe/IP/emulator
#     are fine and the bug is in our build; stays black -> the disc side is at fault.
RETAIL_DIR ?= refs/extract/PS
ISOR  := $(BUILD)/slavedriver-retailbins.iso
ISORJ := $(BUILD)/slavedriver-retailbins-ipjump.iso
iso-retailbins: $(ISOR) $(ISORJ)
$(ISOR): $(RETAIL_DIR)/0 $(RETAIL_DIR)/MAIN.BIN $(CD_DATA) $(IPFILE)
	$(call mkiso,$@,$(IPFILE),$(RETAIL_DIR)/0,$(RETAIL_DIR)/MAIN.BIN)
$(ISORJ): $(RETAIL_DIR)/0 $(RETAIL_DIR)/MAIN.BIN $(CD_DATA) $(IPJUMP)
	$(call mkiso,$@,$(IPJUMP),$(RETAIL_DIR)/0,$(RETAIL_DIR)/MAIN.BIN)
$(IPJUMP): tools/mk_ip_jump.py $(IPFILE)
	$(PYTHON) tools/mk_ip_jump.py "$(IPFILE)" $@

clean:
	rm -rf build

help:
	@sed -n '2,13p' Makefile

# auto dependencies (-MMD)
-include $(ALL_OBJS:.o=.d)
