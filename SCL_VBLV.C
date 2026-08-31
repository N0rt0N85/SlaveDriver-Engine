/* GCC14: SCL_VBLV.C -- replacement for SBL 6.01's scl_vblv.o with the SBL 2.10 semantics
   the game was written against.  In 2.10, SCL_VblankStart (VBLANK-IN) ARMS the frame
   change -- for the game's SCL_SetFrameInterval(0xfffe) mode it raises TVMR.VBE +
   FBCR.FCM|FCT, so the VDP1 erases and swaps at the END of that vblank -- and
   SCL_VblankEnd (VBLANK-OUT) consumes the state and restores TVMR.  SBL 6.01 runs both
   halves back to back inside SCL_VblankEnd: VBE is high ~1 us mid-field, never at an
   evaluation point, and the VDP1 never erases its framebuffers (title text trails, the
   loading screen stuck in place of the sky, wall trails).  The five functions below were
   disassembled from SRUINS.CPE (0x603bab4..0x603bd8e); defining every public symbol of
   scl_vblv.o keeps the archive member out of the link (same override pattern as
   SCL_FUNC.C vs scl_func.o).
   SCL_SetFrameInterval modes: 0xffff = manual change gated on VDP1 draw-end (CEF);
   0xfffe = VBE erase & change (the game's mode); <=1 = one-cycle auto; >=2 = change every
   n fields with an erase pulse one field before; bit 15 clears SpFrameEraseMode. */

#include <sega_xpt.h>
#include <sega_spr.h>	/* volatile decls of SpFrameChgMode/VBInterval/ReqDisplayFlag... */
#include <sega_scl.h>	/* SCL_SetFrameInterval/SCL_VblankStart/SCL_VblankEnd prototypes */

#define VBLV_TVMR   (*(volatile Uint16 *)0x25d00000)
#define VBLV_FBCR   (*(volatile Uint16 *)0x25d00002)
#define VBLV_EDSR   (*(volatile Uint16 *)0x25d00010)	/* bit1 = CEF (draw end)         */
#define VBLV_TVSTAT (*(volatile Uint16 *)0x25f80004)	/* bit1 = ODD field              */

/* scl_vblv.o public state (spr_1c.o's SPR_GetStatus reads VBInterval) */
volatile Uint16	SpFrameChgMode    = 0;
volatile Sint32	SpFrameEraseMode  = 1;
volatile Sint32	VBInterval        = 1;
volatile Sint32	VBIntervalCounter = 0;
volatile Sint32	ReqDisplayFlag    = 0;

/* scl_vblv.o private state */
static volatile Sint32	ReqDisplayFlag2 = 0;	/* DisplayFrame's second wait (CEF gate) */
static volatile Sint32	frameChgFlag    = 0;	/* posted at vblank-in, eaten at vblank-out */

extern Sint32	SpInitialFlag;		/* spr_1c.o: SPR_Initial() has run             */
extern void	SCL_ScrollShow(void);	/* SCL_FUNC.C: applies the buffered VDP2 registers
					   if SclProcess was posted, then SCL_PriIntProc();
					   the CPE's vblank jsr target -- NOT bare
					   SCL_CopyReg, which applies uninitialized
					   K-tables and freezes */

void SCL_SetFrameInterval(Uint16 interval)
{
 if (interval == 0xffff)
    {SpFrameChgMode   = 2;
     SpFrameEraseMode = 0;
     VBLV_FBCR  = SpFbcrMode | 3;
     VBInterval = interval;
     return;
    }
 if (interval == 0xfffe)
    {SpFrameChgMode   = 3;
     SpFrameEraseMode = 0;
     VBLV_FBCR  = SpFbcrMode | 3;
     VBInterval = interval;
     return;
    }
 SpFrameEraseMode = (interval & 0x8000)? 0: 1;
 interval &= 0x7fff;
 SpFrameChgMode = (interval <= 1)? 0: 1;
 VBInterval = interval;
 if (SpFrameChgMode == 0)
    VBLV_FBCR = SpFbcrMode;		/* one-cycle mode: hardware erases+swaps alone */
 else
    VBLV_FBCR = SpFbcrMode | 3;
}

void SCL_DisplayFrame(void)
{
 if (VBInterval == 0)
    return;
 ReqDisplayFlag = 1;
 while (ReqDisplayFlag) ;
 while (ReqDisplayFlag2) ;
}

void SCL_VblInit(void)
{
 SpFrameChgMode    = 0;
 SpFrameEraseMode  = 1;
 VBInterval        = 0;
 VBIntervalCounter = 0;
 ReqDisplayFlag    = 0;
 ReqDisplayFlag2   = 0;
 frameChgFlag      = 0;
}

/* VBLANK-IN (UsrVblankStart): arm the frame change requested by SCL_DisplayFrame; the
   hardware performs the VBE erase & change at the END of this same vblank. */
void SCL_VblankStart(void)
{
 if (SpFrameChgMode == 2)
    {if (!ReqDisplayFlag)
	return;
     SCL_ScrollShow();
     frameChgFlag = 2;
     return;
    }
 if (SpFrameChgMode == 3)
    {if (!ReqDisplayFlag)
	return;
     VBLV_TVMR = SpTvMode | 8;		/* VBE: erase during THIS vblank        */
     VBLV_FBCR = SpFbcrMode | 3;	/* FCM|FCT: change at end of THIS vblank */
     SCL_ScrollShow();
     frameChgFlag = 3;
     return;
    }
 if (SpFrameChgMode == 0)
    {SCL_ScrollShow();
     frameChgFlag = 4;
     return;
    }
 /* mode 1: change every VBInterval fields */
 VBIntervalCounter++;
 if (SpFrameEraseMode == 1 && VBIntervalCounter >= VBInterval - 1)
    VBLV_FBCR = SpFbcrMode | 2;		/* erase pulse one field before the change */
 if (VBIntervalCounter >= VBInterval)
    {if (ReqDisplayFlag)
	{SCL_ScrollShow();
	 frameChgFlag = 1;
	}
     VBIntervalCounter = 0;
    }
}

/* VBLANK-OUT (UsrVblankEnd): the erase+change armed at vblank-in has been performed by the
   hardware during the vblank that just ended -- consume the state, restore TVMR, release
   SCL_DisplayFrame. */
void SCL_VblankEnd(void)
{
 Sint32 state;

 if (SpDie)
    {VBLV_FBCR = SpFbcrMode | ((VBLV_TVSTAT & 2)? 12: 8);	/* double interlace */
     frameChgFlag   = 0;
     ReqDisplayFlag = 0;
     return;
    }
 state = frameChgFlag;
 if (state == 0)
    return;
 if (state == 2 && SpInitialFlag && !(VBLV_EDSR & 2))
    {/* mode 2: plot still running -- keep the state armed, park DisplayFrame on flag2 */
     ReqDisplayFlag2 = 1;
     ReqDisplayFlag  = 0;
     return;
    }
 if (state == 3)
    VBLV_TVMR = SpTvMode;		/* VBE erase+change done during the vblank that just ended */
 else if (state == 1 || state == 2)
    VBLV_FBCR = SpFbcrMode | 3;
 frameChgFlag    = 0;
 ReqDisplayFlag2 = 0;
 ReqDisplayFlag  = 0;
}
