/* saturn_ovl.ld -- linker script for an overlay (GCC14; OVL.C, tools/ovlpack.py).
 *
 * An overlay is linked TWICE, at OVL_BASE = 0x0A000000 and 0x0B000000 (--defsym), against MAIN's
 * symbols (--just-symbols=MAIN.elf: absolute addresses), and ovlpack.py diffs the two images to
 * find its relocations.  Layout: one output section, the image, whose first bytes are the entry
 * table `ovlEntries` (KEEP: the loader calls entry k through word k), then text, rodata and data;
 * the BSS follows it directly (ovl_run clears it over the spent relocation table).  No library is
 * linked: everything the overlay uses is MAIN's (the Makefile roots it in MAIN's link).
 */
OUTPUT_FORMAT("elf32-sh", "elf32-sh", "elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_ovlEntries)

SECTIONS
{
  .image OVL_BASE :
  {
    KEEP(*(.rodata.ovlEntries .data.rel.ro.ovlEntries))
    *(.text .text.*)
    *(.rodata .rodata.* .data.rel.ro .data.rel.ro.*)
    *(.data .data.*)
    . = ALIGN(4);
    __ovl_image_end = .;
  }

  .bss (NOLOAD) :
  {
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __ovl_bss_end = .;
  }

  /* the ELF is an intermediate (the disc gets .image): no debug info; any other section is an
     error (--orphan-handling=error, Makefile), not a silent loss outside the image */
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) *(.debug .debug.* .debug_*) }
}
