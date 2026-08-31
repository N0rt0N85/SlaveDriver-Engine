! link_gnu.s -- GNU as (sh-elf, big-endian) translation of LINK.S (SNASM syntax).
! GCC14: named link_gnu.s (not link.s) because the Windows checkout is case-insensitive: link.s IS LINK.S.
! GCC14: FILE.C link() memcpy()s the first 256 bytes of executeLink into a heap buffer and
! GCC14: calls the COPY, so everything from _executeLink to the end of the literal pool must be
! GCC14: position independent: only PC-relative literal loads and PC-relative branches, and the
! GCC14: pool sits right after the code exactly like the original 'littab'.
! GCC14: Size of the copied region is checked in build/tmp-asm-translate (must be < 256 bytes).
!
!	xdef executeLink
	.global _executeLink

	.text

	.align 2

! void executeLink(void *data,int sizeInDWords)
_executeLink:
	mov.l .Lel_dest,r1	! GCC14: was  mov =$06004000,r1  (literal-pool load)

	! copy data to proper position
.Lel_1:	mov.l @r4+,r0
	mov.l r0,@r1
	add #4,r1
	dt r5
	bf .Lel_1

	! setup stack register
	mov.l .Lel_stack,r15	! GCC14: was  mov.l =$06001000,r15
	! jump to start
	mov.l .Lel_dest,r1	! GCC14: was  mov.l =$06004000,r1  (same literal as above; SNASM shares it)
	jmp @r1
	nop

	.align 2
!	littab
.Lel_dest:	.long 0x06004000	! GCC14: literal pool of executeLink
.Lel_stack:	.long 0x06001000
.Lel_end:
