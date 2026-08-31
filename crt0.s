! crt0.s -- program entry for the GCC 14 build of SlaveDriver (INIT / MAIN / KEYGEN).
!
! GCC14: replaces START.S (__MYENTRYPOINT: 'mov.l =__SN_ENTRY_POINT,r0; jmp @r0') plus the
! GCC14: SN Systems hidden crt0 (__SN_ENTRY_POINT) that the original link pulled in.
!
! SECTION: .text.crt0  -- the linker script MUST place this section FIRST, at 0x06004000:
!   the IP.BIN of INIT and executeLink() (link.s) both jump to 0x06004000 blindly.
! Symbols required from the linker script / C code:
!   __bss_start, _end : start / end of .bss (both 4-byte aligned, ALIGN(4) in the script;
!                       _end is also 'end' in C, UTIL.C uses &end as the heap base)
!   __stackinit       : C 'void *_stackinit=&mystack[STICKSIZE]' (UTIL.C) -> initial r15
!   _main             : C main()
!
! What it does, in order (same effects as the SN crt0 disassembled from SRUINS.CPE @0602f840:
! byte-clear .bss, r15=_stackinit if non-NULL, run C++ ctors [none here], jsr main,
! trapa #34 + loop):
!   1. r15 = _stackinit if non-NULL (the loader value / executeLink's 0x06001000 is only a
!                                    bootstrap stack; _stackinit lives in .data, not .bss)
!   2. zero .bss  [__bss_start, _end)  as longwords
!   3. call _main
!   4. never return: loop forever (no SN debugger, so no trapa #34)

	.section .text.crt0,"ax"
	.align 2
	.global _start
	.global __MYENTRYPOINT
_start:
__MYENTRYPOINT:
	! 1. stack pointer from the C global _stackinit (the SN crt0 in SRUINS.CPE @0602f852
	!    does exactly this, and keeps the loader's r15 when _stackinit is NULL)
	mov.l .Lc0_stackinit,r0
	mov.l @r0,r0
	tst r0,r0
	bt .Lc0_sp_done
	mov r0,r15
.Lc0_sp_done:

	! 2. clear .bss (longword stores, __bss_start/_end 4-byte aligned)
	mov.l .Lc0_bss_start,r0
	mov.l .Lc0_bss_end,r1
	mov #0,r2
	cmp/hs r1,r0		! r0 >= r1 -> empty .bss
	bt .Lc0_bss_done
.Lc0_bss_loop:
	mov.l r2,@r0
	add #4,r0
	cmp/hs r1,r0
	bf .Lc0_bss_loop
.Lc0_bss_done:

	! 3. call main()
	mov.l .Lc0_main,r0
	jsr @r0
	nop

	! 4. main() must never return; if it does, spin
.Lc0_hang:
	bra .Lc0_hang
	nop

	.align 2
.Lc0_stackinit:	.long __stackinit	! C: _stackinit
.Lc0_bss_start:	.long __bss_start
.Lc0_bss_end:	.long _end
.Lc0_main:	.long _main
