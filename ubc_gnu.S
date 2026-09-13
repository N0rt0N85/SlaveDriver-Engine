! UBC trap entry -- linked into INIT, MAIN and KEYGEN, all three of which compile V_BLANK.C.
!
! V_BLANK.C arms the SH-2 User Break Controller as a hardware watchpoint over the low 4 KB
! (BAR=0, BAMR=0xfff, BBR=0x06c): any access through a null-ish pointer traps here.  The C
! handler used to GUESS where the hardware-pushed PC sat on the stack (dummy[8]), which depends
! on GCC's prologue and printed nonsense (1f04 is not a code address).  This stub runs BEFORE
! any prologue, so @r15 is exactly the pushed PC.
!
! Uppercase .S so the C preprocessor runs: userBreakBlam only exists in ASSERT builds.

#ifndef NDEBUG
	.text
	.align 2
	.global _ubcTrap
_ubcTrap:
	mov.l .Lubc_pc,r0
	mov.l @r15,r1		! the exception PC, pushed by the hardware on entry
	mov.l r1,@r0
	mov.l .Lubc_c,r0
	jmp @r0			! chain to the C handler, which logs it and returns with RTE
	nop
	.align 2,0
.Lubc_pc:	.long _ubcFaultPC
.Lubc_c:	.long _userBreakBlam

	.comm _ubcFaultPC,4,4
#endif
