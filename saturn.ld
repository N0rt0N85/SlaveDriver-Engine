/* saturn.ld — linker script for the SlaveDriver flat Saturn binaries (INIT / MAIN / KEYGEN).
 *
 * GCC14: replaces the SN Systems (Psy-Q) link/CPE layout.  Every program is a raw image
 * loaded at 0x06004000 in high work RAM (INIT by the IP, MAIN by executeLink in link.s)
 * and executed from its first byte, so:
 *   - the crt0 entry (section .text.crt0) is placed FIRST, before any other .text input;
 *   - .text/.rodata/.data are contiguous (objcopy -O binary emits exactly that span);
 *   - .bss (+ COMMON: the game relies on -fcommon tentative definitions) follows and is
 *     bracketed by __bss_start/__bss_end so crt0 can clear it (it is NOT in the .bin);
 *   - `_end` follows .bss: UTIL.C does `extern int end; mem2Start=(int)&end;` and this GCC
 *     prefixes C symbols with '_', so the C `end` IS the linker's `_end` (asm-level `end`
 *     is PROVIDEd too, for completeness);
 *   - no stack section: r15 is loaded from `_stackinit` (UTIL.C, top of mystack[]) by crt0,
 *     exactly what the SN crt0 did (CPE SETREG 0x58 is the entry PC of that crt0, not r15).
 * The address ranges come from the game's validPtr(): high work RAM 0x06004000-0x060FFFFF.
 * Section names follow `sh2eb-elf-ld --verbose` (GCC 14 emits .text.startup/.text.unlikely,
 * .rodata.str1.*, .rodata.cst*, .eh_frame, .init/.fini …); unneeded ones are discarded.
 */
OUTPUT_FORMAT("elf32-sh", "elf32-sh", "elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
  hwram (rwx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000   /* 0x06004000 .. 0x060FFFFF */
}

SECTIONS
{
  .text 0x06004000 :
  {
    __text_start = .;
    KEEP(*(.text.crt0))                       /* crt0 FIRST: the .bin starts with the entry */
    *(.text.startup .text.startup.*)          /* main() in GCC 14 -O2 */
    *(.text .stub .text.* .gnu.linkonce.t.*)  /* includes .text.unlikely/.hot */
    *(.gnu.warning)
    . = ALIGN(4);
    __text_end = .;
  } > hwram

  .rodata :
  {
    __rodata_start = .;
    *(.rodata .rodata.* .gnu.linkonce.r.*)    /* .rodata.str1.4, .rodata.cst4 … */
    *(.rodata1)
    . = ALIGN(4);
    __rodata_end = .;
  } > hwram

  PROVIDE (__etext = .);
  PROVIDE (_etext = .);
  PROVIDE (etext = .);

  .data :
  {
    __data_start = .;
    *(.data .data.* .gnu.linkonce.d.*)
    *(.data1)
    *(.sdata .sdata.* .gnu.linkonce.s.*)
    . = ALIGN(4);
    __data_end = .;
  } > hwram

  _edata = .;
  PROVIDE (edata = .);

  /* end of the flat binary image (objcopy -O binary stops here) */
  __image_end = .;

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.dynbss)
    *(.sbss .sbss.* .gnu.linkonce.sb.*)
    *(.scommon)
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)                                 /* -fcommon tentative definitions (stat_*, camera …) */
    . = ALIGN(4);
    __bss_end = .;
  } > hwram

  . = ALIGN(4);
  _end = .;                                   /* C `end` (UTIL.C: mem2Start=(int)&end) */
  PROVIDE (end = .);
  __heap_start = .;                           /* mem_malloc(1,…) area: _end .. 0x06100000 */

  /* Not needed in a flat, no-libc-startup, no-C++ image. */
  /DISCARD/ :
  {
    *(.init) *(.fini)
    *(.ctors) *(.dtors) *(.jcr)
    *(.init_array*) *(.fini_array*) *(.preinit_array*)
    *(.eh_frame) *(.eh_frame_hdr) *(.eh_frame_entry*) *(.sframe*) *(.gcc_except_table*)
    *(.note.GNU-stack) *(.note.gnu.build-id) *(.gnu_debuglink) *(.gnu.lto_*)
    *(.stack)
  }

  /* Stabs / DWARF debugging sections (from `sh2eb-elf-ld --verbose`), non-allocated. */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .comment       0 (INFO) : { *(.comment) }
  .gnu.build.attributes : { *(.gnu.build.attributes .gnu.build.attributes.*) }
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  .debug_ranges   0 : { *(.debug_ranges) }
  .debug_addr     0 : { *(.debug_addr) }
  .debug_line_str 0 : { *(.debug_line_str) }
  .debug_loclists 0 : { *(.debug_loclists) }
  .debug_macro    0 : { *(.debug_macro) }
  .debug_names    0 : { *(.debug_names) }
  .debug_rnglists 0 : { *(.debug_rnglists) }
  .debug_str_offsets 0 : { *(.debug_str_offsets) }
  .debug_sup      0 : { *(.debug_sup) }
  .gnu.attributes 0 : { KEEP (*(.gnu.attributes)) }
}
