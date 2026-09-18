! CRASH.C's vector entries -- the freeze and crash report (the model is at the top of CRASH.C).
! MAIN only: crashInstall points the vectors here and crashUninstall gives them back before
! another program replaces this one (FILE.C link).

	.text
	.align	2

! VBlank-OUT, in front of the BIOS handler that calls UsrVblankEnd.  Records F, where the
! hardware pushed PC (@F) and SR (@(4,F)), then hands over with every register and the stack
! as the interrupt found them: a second frame is built just below F -- PC = the BIOS handler,
! SR = this one's -- and RTE takes it, leaving r15 = F.  Uses 16 bytes below F, which the
! handler would have used anyway.
	.global	_crashVbOut
_crashVbOut:
	add	#-8,r15			! room for the hand-over frame
	mov.l	r0,@-r15		! F-12
	mov.l	r1,@-r15		! F-16
	mov.l	.Lframe,r0
	mov	r15,r1
	add	#16,r1
	mov.l	r1,@r0			! crashFrame = F
	stc	sr,r1
	mov.l	r1,@(12,r15)		! F-4: SR of the hand-over
	mov.l	.Lchain,r0
	mov.l	@r0,r0
	mov.l	r0,@(8,r15)		! F-8: PC of the hand-over
	mov.l	@r15+,r1
	mov.l	@r15+,r0
	rte				! PC = @(F-8), SR = @(F-4), r15 = F
	nop

! The exceptions never go back.  Record the vector, PC, SR, PR and the faulting code's SP,
! then return into crashEntry with the interrupts open.
	.global	_crashExc4
_crashExc4:
	bra	.Lexc
	mov	#4,r0
	.global	_crashExc6
_crashExc6:
	bra	.Lexc
	mov	#6,r0
	.global	_crashExc9
_crashExc9:
	bra	.Lexc
	mov	#9,r0
	.global	_crashExc10
_crashExc10:
	bra	.Lexc
	mov	#10,r0
.Lexc:
	mov.l	.Lcause,r1
	mov.l	r0,@r1			! crashCause
	mov.l	.Lpr,r1
	sts	pr,r0
	mov.l	r0,@r1			! crashPR
	mov.l	.Lpc,r1
	mov.l	@r15,r0
	mov.l	r0,@r1			! crashPC
	mov.l	.Lsr,r1
	mov.l	@(4,r15),r0
	mov.l	r0,@r1			! crashSR
	mov.l	.Lsp,r1
	mov	r15,r0
	add	#8,r0
	mov.l	r0,@r1			! crashSP: before the exception
	mov.l	.Lentry,r0
	mov.l	r0,@r15			! return PC: crashEntry
	mov	#-16,r0
	shll2	r0
	shll2	r0
	or	#0x0f,r0		! 0xffffff0f
	mov.l	@(4,r15),r1
	and	r0,r1
	mov.l	r1,@(4,r15)		! return SR: I3-I0 cleared
	rte
	nop

! Lands here from both.  PR is still the stuck code's (a hang: the BIOS handler restored it
! before its RTE).  The stack restarts from the top of mystack: nothing on it is kept.
	.global	_crashEntry
_crashEntry:
	mov.l	.Lepr,r1
	sts	pr,r0
	mov.l	r0,@r1			! crashEntryPR
	mov.l	.Lstk,r0
	mov.l	@r0,r15			! r15 = _stackinit
	mov.l	.Lscreen,r0
	jsr	@r0
	nop
1:	bra	1b
	nop

	.align	2
.Lframe:	.long	_crashFrame
.Lchain:	.long	_crashVbOutChain
.Lcause:	.long	_crashCause
.Lpr:		.long	_crashPR
.Lpc:		.long	_crashPC
.Lsr:		.long	_crashSR
.Lsp:		.long	_crashSP
.Lentry:	.long	_crashEntry
.Lepr:		.long	_crashEntryPR
.Lstk:		.long	__stackinit
.Lscreen:	.long	_crashScreen
