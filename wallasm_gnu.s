! wallasm_gnu.s -- GNU as (sh-elf, big-endian) translation of WALLASM.S (SNASM syntax).
! GCC14: named wallasm_gnu.s (not wallasm.s) because the Windows checkout is case-insensitive: wallasm.s IS WALLASM.S.
! GCC14: mechanical translation of the original WALLASM.S; machine code is intended to be
! GCC14: byte-identical to the shipped build (verified against SRUINS.CPE, see
! GCC14: build/tmp-asm-translate/cpe_match.py).  Mapping applied:
! GCC14:   xdef X            -> .global _X            (C symbols carry a leading underscore)
! GCC14:   mov.l =lit,rN     -> mov.l <pool label>,rN  (PC-relative literal pool, same place as 'littab')
! GCC14:   mov.l #imm / mov.l rM,rN / add.l / sts.l mach,rN -> mov #imm / mov / add / sts
! GCC14:   @(rN,d)           -> @(d,rN)   ;  @(gbr,d) -> @(d,gbr)
! GCC14:   sub #imm,rN       -> add #-imm,rN  (SH-2 has no immediate subtract; SN assembled it that way)
! GCC14:   .align 4 (bytes)  -> .align 2 (power of two)
! GCC14:   @label (function-scoped in SNASM) -> .L<function>_<label> (unique, local)
!
!	xdef rectTransform
!	xdef normTransform
!	xdef project_point
	.global _rectTransform
	.global _normTransform
	.global _project_point

	.text

	.align 2
!	littab           (empty)

! void project_point(MthXyz *v,XyInt *p);

	.align 2
_project_point:
	mov.l .Lpp_divu,r0	! GCC14: was  mov.l =$0ffffff00,r0
	ldc r0,gbr		! point gbr to hardware divider
	mov.l @(8,r4),r0	! GCC14: was  mov.l @(r4,8),r0
	mov #1,r1		! GCC14: was  mov.l #1,r1
	shll16 r1
	cmp/gt r1,r0
	bt .Lpp_1
	mov r1,r0		! GCC14: was  mov.l r1,r0
.Lpp_1:	! ... r0 now holds the z coord or F(1), whichever is greater
	! ... start the hardware divide
	mov.l r0,@(0,gbr)	! GCC14: was  mov.l r0,@(gbr,0)
	mov #80,r0		! GCC14: was  mov.l #80,r0
	shll r0
	mov.l r0,@(16,gbr)	! GCC14: was  mov.l r0,@(gbr,16)
	mov #0,r0		! GCC14: was  mov.l #0,r0
	mov.l r0,@(20,gbr)	! GCC14: was  mov.l r0,@(gbr,20)

	! cannot do much during the divide wait, too bad.
	mov.l @(4,r4),r2	! r2=v.y   GCC14: was @(r4,4)
	mov.l @(0,r4),r1	! r1=v.x   GCC14: was @(r4,0)
	!neg r2,r2 ! neg here gives rounding differences

	! get results of hardware divide
	mov.l @(0x1c,gbr),r0	! GCC14: was  mov.l @(gbr,$1c),r0

	dmuls.l r1,r0
	sts mach,r3
	dmuls.l r2,r0
	mov.w r3,@r5
	sts mach,r0
	neg r0,r0
	rts
	mov.w r0,@(2,r5)	! GCC14: was  mov.w r0,@(r5,2)

	.align 2,0		! GCC14: fill 0 (SNASM padded with 0x0000, GNU as pads code with nop 0x0009)
!	littab
.Lpp_divu:	.long 0xffffff00	! GCC14: literal pool of project_point (=$0ffffff00 -> 32-bit 0xffffff00)

!rectTransform(Fixed32 wx,Fixed32 wy,Fixed32 wz,
! 	       int light,int h,int w,
!	       Fixed32 px,Fixed32 py,Fixed32 pz,
!	       Fixed32 hx,Fixed32 hy,Fixed32 hz,
!	       vCalc *output,
!	       unsigned short (*lightFunc)(int vLightIndex,
!				                  MthXyz *pos);

!	xref level_vertexLight   -> _level_vertexLight
!	xref greyTable           -> _greyTable
	.align 2
_rectTransform:
	mov.l r14,@-r15
	mov r15,r14		! r14 is gonna be the stack pointer   GCC14: was mov.l
	mov.l r8,@-r15
	add #4,r14		! r14 now points to "h" on the stack  GCC14: was add.l
	mov.l r9,@-r15
	mov.l r10,@-r15
	mov.l r11,@-r15
	mov.l r12,@-r15
	mov.l r13,@-r15
	sts.l pr,@-r15

	mov.l .Lrt_vlight,r0	! GCC14: was  mov.l =level_vertexLight,r0
	mov.l @r0,r0
	add r0,r7		! GCC14: was add.l
	! r4,r5,r6 = width vector
	! r7 = address of light array

	mov.l @(8,r14),r8	! GCC14: @(r14,8)
	mov.l @(12,r14),r9	! GCC14: @(r14,12)
	mov.l @(16,r14),r10	! GCC14: @(r14,16)
	! r8,r9,r10 = position
	mov.l @(32,r14),r12	! GCC14: @(r14,32)
	! r12=vcalc output pointer

	mov.l .Lrt_divu,r0	! GCC14: was  mov.l =$0ffffff00,r0
	ldc r0,gbr		! point gbr to hardware divider
	mov.l @(36,r14),r13	! GCC14: @(r14,36)
	! r13=light calc function or NULL if none needed
.Lrt_heightLoop:
	mov.l @(4,r14),r11	! GCC14: @(r14,4)
	! r11=width loop counter
.Lrt_widthLoop:
	! project point
	! ... compare z coordinate against F(33)
	mov #33,r1		! GCC14: was mov.l #33,r1
	shll16 r1
	cmp/gt r1,r10
	movt r2
	bt/s .Lrt_2
	mov r10,r0		! GCC14: was mov.l r10,r0
	mov r1,r0		! GCC14: was mov.l r1,r0
.Lrt_2:	! ... r0 now holds the z coord or F(33), whichever is greater
	! ... start the hardware divide
	mov.l r0,@(0,gbr)	! GCC14: @(gbr,0)
	mov r0,r3		! save r0 for depth cue calculation below   GCC14: was mov.l
	mov #80,r0		! GCC14: was mov.l #80,r0
	shll r0
	mov.l r0,@(16,gbr)	! GCC14: @(gbr,16)
	mov #0,r0		! GCC14: was mov.l #0,r0
	mov.l r0,@(20,gbr)	! GCC14: @(gbr,20)

	! compute light value with depth cueing
	mov.b @r7+,r1		! get light value
	mov.l @(40,r14),r0	! GCC14: extra per-vertex light step (0; 1 when the caller
	add r0,r7		!        walks a full-grid light list on a halved mip grid)
	! ... do depth cueing calculations to modify r1
	! ... r3 = clamped z coord from above
	shlr16 r3
	shlr8 r3
	! stuff to support wavywalls
	!mov.l r1,r0
	!and #31,r0
	! end
	cmp/hi r1,r3
	bf/s .Lrt_9
	sub r3,r1
	mov #0,r1		! GCC14: was mov.l #0,r1
.Lrt_9:
	! do light source computations if necessary
	tst r13,r13
	bf .Lrt_callLit

	! otherwise do inline light
	mov.l .Lrt_grey,r0	! GCC14: was  mov.l =greyTable,r0
	shll r1
	mov.w @(r0,r1),r0	! lookup in grey table
.Lrt_retFromLit:
	shll16 r2		! test bit from way above
	shlr r2
	xor r2,r0		! flip clip bit
	mov.w r0,@(4,r12)	! store vCalc.light   GCC14: @(r12,4)

	add r6,r10		! p->z+=width->z

	! get results of hardware divide
	mov.l @(0x1c,gbr),r0	! GCC14: @(gbr,$1c)

	dmuls.l r8,r0
	add r4,r8		! p.x+=width.x
	sts mach,r3
	dmuls.l r9,r0
	mov.w r3,@r12		! store vCalc.x
	add r5,r9		! p.y+=width.y
	sts mach,r0
	neg r0,r0
	mov.w r0,@(2,r12)	! store vCalc.y   GCC14: @(r12,2)

	dt r11
	bf/s .Lrt_widthLoop
	add #6,r12

	mov.l @(44,r14),r0	! GCC14: extra per-row light skip (0; 2*(fullWidth-width) on
	add r0,r7		!        a halved mip grid, to land on the next even row)

	! re-load position from memory
	mov.l @(8,r14),r8	! GCC14: @(r14,8)
	mov.l @(12,r14),r9	! GCC14: @(r14,12)
	mov.l @(16,r14),r10	! GCC14: @(r14,16)
	! add height vector
	mov.l @(20,r14),r0	! GCC14: @(r14,20)
	mov.l @(24,r14),r1	! GCC14: @(r14,24)
	add r0,r8
	mov.l @(28,r14),r2	! GCC14: @(r14,28)
	add r1,r9
	add r2,r10
	! and save it out
	mov.l r8,@(8,r14)	! GCC14: @(r14,8)
	mov.l r9,@(12,r14)	! GCC14: @(r14,12)
	mov.l r10,@(16,r14)	! GCC14: @(r14,16)

	mov.l @(0,r14),r0	! h -> r0   GCC14: @(r14,0)
	dt r0
	bf/s .Lrt_heightLoop
	mov.l r0,@(0,r14)	! GCC14: was  mov r0,@(r14,0)

	! donesville, pop the stack
	lds.l @r15+,pr
	mov.l @r15+,r13
	mov.l @r15+,r12
	mov.l @r15+,r11
	mov.l @r15+,r10
	mov.l @r15+,r9
	mov.l @r15+,r8
	rts
	mov.l @r15+,r14

	.align 2
!	littab
.Lrt_vlight:	.long _level_vertexLight	! GCC14: literal pool of rectTransform
.Lrt_divu:	.long 0xffffff00
.Lrt_grey:	.long _greyTable

	.align 2
.Lrt_callLit:	! perform function call to @r13
	!
	! ... save some regs
	mov.l r4,@-r15
	mov.l r5,@-r15
	mov r1,r4		! r4=vlight value   GCC14: was mov.l
	mov.l r6,@-r15
	mov.l r7,@-r15
	mov.l r2,@-r15
	! ... put pos on stack
	mov.l r10,@-r15
	mov.l r9,@-r15
	mov.l r8,@-r15
	jsr @r13		! do call
	mov r15,r5		! load pos address   GCC14: was mov.l

	add #12,r15		! pop pos off stack
	mov.l @r15+,r2
	mov.l @r15+,r7
	mov.l @r15+,r6
	mov.l @r15+,r5
	bra .Lrt_retFromLit
	mov.l @r15+,r4



!normTransform(sVertexType *firstVertex,MthMatrix *viewMatrix,
!	       int nmVert,vCalc *output,
!	       unsigned short (*lightFunc)(int vLightIndex,
!				           MthXyz *pos);

!	xref level_vertex        (unused)
!	xref greyTable           -> _greyTable
	.align 2
_normTransform:
	! r4=sVertexType *vertex
	! r5=MthMatrix *view
	! r6=nmVert left
	! r7=vCalc *output

	mov.l r10,@-r15
	mov.l r11,@-r15
	mov.l r12,@-r15
	mov.l r13,@-r15
	mov.l r14,@-r15
	mov.l @(20,r15),r13	! GCC14: @(r15,20)
	! r13=lightFunc
	sts.l pr,@-r15
	! reserve space for point
	mov #0,r0
	mov.l r0,@-r15
	mov.l r0,@-r15
	mov.l r0,@-r15
	mov r15,r14
	! r14 -> point on stack
	add #(8*4),r5		! point r5 @ z coordinate
	mov.l .Lnt_divu,r0	! GCC14: was  mov.l =$0ffffff00,r0
	ldc r0,gbr		! point gbr to hardware divider
.Lnt_vloop:
	! copy vertex point from vertex array to point
	mov.l @r4+,r0
	mov.w r0,@(4,r14)	! GCC14: @(r14,4)
	swap.w r0,r0
	mov.w r0,@(0,r14)	! GCC14: @(r14,0)
	mov.w @r4+,r0
	clrmac
	mov.w r0,@(8,r14)	! GCC14: @(r14,8)
	! find the transformed z coordinate
	mac.l @r5+,@r14+
	mac.l @r5+,@r14+
	mac.l @r5+,@r14+
	sts mach,r1		! GCC14: was sts.l mach,r1
	sts macl,r0		! GCC14: was sts.l macl,r0
	xtrct r1,r0
	mov.l @r5+,r1
	add r1,r0
	! z coordinate is now in r0
	mov #33,r1		! GCC14: was mov.l #33,r1
	shll16 r1
	cmp/gt r1,r0
	movt r2
	bt/s .Lnt_2
	mov r0,r10		! copy of z coordinate in r10
	mov r1,r0		! GCC14: was mov.l r1,r0
.Lnt_2:	! ... r0 now holds the z coord or F(33), whichever is greater
	! ... start the hardware divide
	mov.l r0,@(0,gbr)	! GCC14: @(gbr,0)
	mov #80,r0		! GCC14: was mov.l #80,r0
	shll r0
	mov.l r0,@(16,gbr)	! GCC14: @(gbr,16)
	mov #0,r0		! GCC14: was mov.l #0,r0
	mov.l r0,@(20,gbr)	! GCC14: @(gbr,20)

	! transform other coordinates
	clrmac
	add #-(12*4),r5		! put r5 back to front of matrix   GCC14: was sub #(12*4),r5
	add #-(3*4),r14		! put r14 back to front of points  GCC14: was sub #(3*4),r14
	mac.l @r5+,@r14+
	mac.l @r5+,@r14+
	mac.l @r5+,@r14+
	sts mach,r1		! GCC14: was sts.l
	sts macl,r11		! GCC14: was sts.l
	xtrct r1,r11
	mov.l @r5+,r1
	add r1,r11
	! r11 now holds transformed x coordinate
	clrmac
	add #-(3*4),r14		! put r14 back to front of points  GCC14: was sub #(3*4),r14
	mac.l @r5+,@r14+
	mac.l @r5+,@r14+
	mac.l @r5+,@r14+
	add #-(3*4),r14		! put r14 back to front of points  GCC14: was sub #(3*4),r14
	sts mach,r1		! GCC14: was sts.l
	sts macl,r12		! GCC14: was sts.l
	xtrct r1,r12
	mov.l @r5+,r1
	add r1,r12
	! r12 now holds transformed y coordinate


	! compute light value with depth cueing
	mov.b @r4+,r1		! get light value
	! ... do depth cueing calculations to modify r1
	! ... r10 = clamped z coord from above
	mov r10,r3		! GCC14: was mov.l r10,r3
	shlr16 r3
	shlr8 r3
!	shlr2 r3
!	shlr2 r3
!	shlr r3
	cmp/hi r1,r3
	bf/s .Lnt_9
	sub r3,r1
	mov #0,r1		! GCC14: was mov.l #0,r1
.Lnt_9:
	! do light source computations if necessary
	tst r13,r13
	bf .Lnt_callLit

	! otherwise do inline light
	mov.l .Lnt_grey,r0	! GCC14: was  mov.l =greyTable,r0
	shll r1
	mov.w @(r0,r1),r0	! lookup in grey table
.Lnt_retFromLit:
	shll16 r2		! test bit from way above
	shlr r2
	xor r2,r0		! flip clip bit
!	mov.l =$7fff,r0 ! temp
	mov.w r0,@(4,r7)	! store vCalc.light   GCC14: @(r7,4)
	add #1,r4		! skip r4 past padding

	! time to fetch the divide result
	mov.l @(0x1c,gbr),r0	! GCC14: @(gbr,$1c)

	dmuls.l r11,r0
	sts mach,r1
	dmuls.l r12,r0
	mov.w r1,@r7		! store vCalc.x
	sts mach,r0
	neg r0,r0
	mov.w r0,@(2,r7)	! store vCalc.y   GCC14: @(r7,2)

	dt r6
	bf/s .Lnt_vloop
	add #6,r7		! point r7 to next vCalc entry

	add #(3*4),r15		! pop point space off stack
	lds.l @r15+,pr
	mov.l @r15+,r14
	mov.l @r15+,r13
	mov.l @r15+,r12
	mov.l @r15+,r11
	rts
	mov.l @r15+,r10



.Lnt_callLit:	! perform function call to @r13
	!
	! ... save some regs
	mov.l r4,@-r15
	mov r1,r4		! vlight value   GCC14: was mov.l
	mov.l r5,@-r15
	mov.l r6,@-r15
	mov.l r7,@-r15
	mov.l r2,@-r15
	! ... put pos on stack
	mov.l r10,@-r15
	mov.l r12,@-r15
	mov.l r11,@-r15
	jsr @r13		! do call
	mov r15,r5		! load pos address   GCC14: was mov.l

	add #12,r15		! pop pos off stack
	mov.l @r15+,r2
	mov.l @r15+,r7
	mov.l @r15+,r6
	mov.l @r15+,r5
	bra .Lnt_retFromLit
	mov.l @r15+,r4

	! GCC14: the original has no 'littab' after normTransform; SNASM flushed the pending
	! GCC14: literals at the end of the section, i.e. right here.
	.align 2
.Lnt_divu:	.long 0xffffff00	! GCC14: literal pool of normTransform
.Lnt_grey:	.long _greyTable
