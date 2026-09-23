#ifndef __INCLUDEDhitscanh
#define __INCLUDEDhitscanh

int hitScan(Sprite *dontHit,MthXyz *ray,MthXyz *pos,int sector,
	    MthXyz *outPos,int *outSector);
int canSee(Sprite *s1,Sprite *s2);
/* GCC14: the same line of sight from two POINTS and their leaves -- reads no sprite, so a job
   running while sprites are spawned and freed (the slave's sight batch) can call it */
int canSeePos(const MthXyz *p1,int s1,const MthXyz *p2,int s2);
int singleSectorWallHitScan(MthXyz *ray,MthXyz *pos,int sector,
			    MthXyz *outPos,int *outSector);
#endif
