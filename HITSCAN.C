#include <machine.h>

#include <libsn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sega_int.h"
#include "sega_mth.h"
#include "sega_sys.h"

#include "level.h"
#include "util.h"
#include "sprite.h"
#include "hitscan.h"

#ifndef NDEBUG
int lastHitWall;
#endif

/* ray must be normalized */
int hitSpriteP(Fixed32 ray[3],Fixed32 pos[3],Sprite *sprite,
	       Fixed32 *out_pos,
	       Fixed32 *out_distance)
{Fixed32 v;
 Fixed32 EO[3];
 Fixed32 disc,d;

 assert(ray[0]>=-F(1) && ray[0]<=F(1));
 assert(ray[1]>=-F(1) && ray[1]<=F(1));
 assert(ray[2]>=-F(1) && ray[2]<=F(1));

 if (sprite->flags & SPRITEFLAG_NOHITSCAN)
    return 0;
 EO[0]=sprite->pos.x-pos[0];
 EO[1]=sprite->pos.y-pos[1];
 EO[2]=sprite->pos.z-pos[2];

 v=MTH_Product(ray,EO);

 if (v<0)
    return 0;
 if (f(EO[0])*f(EO[0])+f(EO[1])*f(EO[1])+f(EO[2])*f(EO[2])-
     f(v)*f(v)>4*f(sprite->radius2))
    return 0;
 disc=sprite->radius2-(MTH_Product(EO,EO)-MTH_Mul(v,v));

 if (disc<0)
    return 0;
 d=fixSqrt(disc,16);
 out_pos[0]=pos[0]+MTH_Mul(v-d,ray[0]);
 out_pos[1]=pos[1]+MTH_Mul(v-d,ray[1]);
 out_pos[2]=pos[2]+MTH_Mul(v-d,ray[2]);
 *out_distance=v-d;
#if 0
#ifndef NDEBUG
 if (*out_distance<0)
    {char buff[160];
     sprintf(buff,"(%d,%d,%d)",ray[0],ray[1],ray[2]);
     message(buff); message(buff); message(buff);
     sprintf(buff,"(%d,%d,%d)",EO[0],EO[1],EO[2]);
     message(buff); message(buff); message(buff);
     sprintf(buff," %d %d %d ",disc,d,v);
     message(buff); message(buff); message(buff);
    }
#endif
#endif
 assert(*out_distance>=0);
 if (*out_distance<0)
    return 0;
 return 1;
}

int hitWallP(Fixed32 ray[3],Fixed32 pos[3],sWallType *wall,
	     Fixed32 *out_pos)
{Fixed32 t;
 Fixed32 f;
 Fixed32 P[3]; /* intersection point */
 int p[3];
 int inside;
 int axis[3],i,biggest,p1,p2,x;
 Fixed32 V[4][2];
 f=MTH_Product((Fixed32 *)wall->normal,ray);
 if (f>-100)
    /* reject walls which are back facing w/respect to the ray.
       we reject slightly more to avoid infinitly repeating between
       two sectors */
    return 0;
 t=-MTH_Div(wall->d+MTH_Product((Fixed32 *)wall->normal,pos) ,f);
 if (t<=F(-3))
    /* reject walls where the collision point is behind the origin of the
       ray.  We can be loose here because we have convex sectors
       if everything was perfect, t would always be positive.  Loosening this
       allows us to recover from being slightly outside the sector we think
       we're in.  There is still a case where we are slightly outside the
       sector, but the angle of the ray is such that the sector boundry is
       rejected by the previous test */
    return 0;

 /* idea: since sectors are convex, we only need to test the bounds of the
    sector boundry walls.  If we don't hit a sector boundry, then the
    closest wall plane hit is the intersection point.  This would avoid
    problems where we mistakenly hit no walls.
    what about multiple walls on same plane? */

 /* Ray hits plane of wall & t is ray parameter for point of
    intersection */

 for (i=0;i<3;i++)
    P[i]=MTH_Mul(ray[i],t)+pos[i];


 /* find largest component of normal (this could be precomputed) */
 i=abs(wall->normal[0]);
 biggest=0;
 if (abs(wall->normal[1])>i)
    {i=abs(wall->normal[1]);
     biggest=1;
    }
 if (abs(wall->normal[2])>i)
    {i=abs(wall->normal[2]);
     biggest=2;
    }

 axis[0]=biggest; /* largest */

 /* get endpoint and plane intersection into V[] and p[] */

 switch (biggest)
    {case 0:
	for (i=0;i<4;i++)
	   {V[i][0]=F(level_vertex[wall->v[i]].z);
	    V[i][1]=F(level_vertex[wall->v[i]].y);
	   }
	p[0]=P[2]; p[1]=P[1];
	break;
     case 1:
	for (i=0;i<4;i++)
	   {V[i][0]=F(level_vertex[wall->v[i]].x);
	    V[i][1]=F(level_vertex[wall->v[i]].z);
	   }
	p[0]=P[0]; p[1]=P[2];
	break;
     case 2:
	for (i=0;i<4;i++)
	   {V[i][0]=F(level_vertex[wall->v[i]].x);
	    V[i][1]=F(level_vertex[wall->v[i]].y);
	   }
	p[0]=P[0]; p[1]=P[1];
	break;
       }

 inside=0;
 for (p1=0;p1<4;p1++)
    {p2=p1+1;
     if (p2==4)
	p2=0;
     if ((p[1]<=V[p1][1] && p[1]<=V[p2][1]) ||
	 (p[1]>V[p1][1] && p[1]>V[p2][1]))
	/* point is outside y extent of line */
	continue;
     if (V[p2][1]==V[p1][1])
	{/* horizontal edges */
	 if ((p[0]>V[p2][0] && p[0]<=V[p1][0]) ||
	     (p[0]>V[p1][0] && p[0]<=V[p2][0]))
	    inside=!inside;
	 continue;
	}

     if (p[0]<=V[p1][0] && p[0]<=V[p2][0])
	{inside=!inside;
	 continue;
	}
     if (p[0]>V[p1][0] && p[0]>V[p2][0])
	continue;

     x=V[p1][0]+
	MTH_Mul((V[p2][0]-V[p1][0]),
		MTH_Div(p[1]-V[p1][1],
			V[p2][1]-V[p1][1]));
     if (p[0]<=x)
	inside=!inside;
    }

 if (inside)
    {for (i=0;i<3;i++)
	out_pos[i]=P[i];
     return 1;
    }
 return 0;
}


int hitScan(Sprite *dontHit,MthXyz *ray,MthXyz *pos,int sector,
	    MthXyz *outPos,int *outSector)
{Sprite *collideSprite,*spr;
 int w;
 Fixed32 tempDist,collideSpriteDist;
 MthXyz tempPos;
 sSectorType *s;
#ifndef NDEBUG
 int count=0;
#endif
 while (1)
    {assert(count++<50);
     /* collide with sprites in this sector */
     collideSprite=NULL;
     collideSpriteDist=F(30000);
     for (spr=sectorSpriteList[sector];spr;spr=spr->next)
	{if (spr!=dontHit &&
	     hitSpriteP((Fixed32 *)ray,(Fixed32 *)pos,spr,
			(Fixed32 *)&tempPos,&tempDist))
	    {assert(tempDist>=0);
	     if (tempDist>=collideSpriteDist)
		continue;
	     collideSpriteDist=tempDist;
	     *outPos=tempPos;
	     collideSprite=spr;
	    }
	}
     if (collideSprite)
	{*outSector=sector;
	 return COLLIDE_SPRITE|(collideSprite-sprites);
	}
     /* collide with walls in this sector */
     s=level_sector+sector;
     for (w=s->firstWall;w<=s->lastWall;w++)
	{if (level_wall[w].normal[1]!=0)
	    continue;
	 if (hitWallP((Fixed32 *)ray,(Fixed32 *)pos,level_wall+w,
		      (Fixed32 *)&tempPos))
	    if (level_wall[w].nextSector!=-1 &&
		!(level_wall[w].flags & WALLFLAG_BLOCKED))
	       {/* we've gone into a new sector */
		sector=level_wall[w].nextSector;
		goto Break;
	       }
	    else
	       {/* we've smacked into a wall */
		if (level_wall[w].flags & WALLFLAG_PARALLAX)
		   return 0;
		*outPos=tempPos;
		*outSector=sector;
		return COLLIDE_WALL|(w);
	       }
	}
     /* collide with floors in this sector */
     for (w=s->firstWall;w<=s->lastWall;w++)
	{if (level_wall[w].normal[1]==0)
	    continue;
	 if (hitWallP((Fixed32 *)ray,(Fixed32 *)pos,level_wall+w,
		      (Fixed32 *)&tempPos))
	    if (level_wall[w].nextSector!=-1 &&
		!(level_wall[w].flags & WALLFLAG_BLOCKED))
	       {/* we've gone into a new sector */
		sector=level_wall[w].nextSector;
		goto Break;
	       }
	    else
	       {/* we've smacked into a wall */
		if (level_wall[w].flags & WALLFLAG_PARALLAX)
		   return 0;
		*outPos=tempPos;
		*outSector=sector;
		return COLLIDE_WALL|(w);
	       }
	}
     return 0;
  Break:
    }
}

int singleSectorWallHitScan(MthXyz *ray,MthXyz *pos,int sector,
			    MthXyz *outPos,int *outSector)
{int w,loop;
 sSectorType *s;
 MthXyz tempPos;

 s=level_sector+sector;
 for (loop=0;loop<2;loop++)
    for (w=s->firstWall;w<=s->lastWall;w++)
       {if (loop==0 && level_wall[w].normal[1]!=0)
	   continue;
	if (loop==1 && level_wall[w].normal[1]==0)
	   continue;
	if (hitWallP((Fixed32 *)ray,(Fixed32 *)pos,level_wall+w,
		     (Fixed32 *)&tempPos))
	   if (level_wall[w].nextSector!=-1 &&
	       !(level_wall[w].flags & WALLFLAG_BLOCKSSIGHT))
	      {/* we've gone into a new sector */
	       *outSector=level_wall[w].nextSector;
	       *outPos=tempPos;
	       return 2;
	      }
	   else
	      {/* we've smacked into a wall */
	       *outPos=tempPos;
	       *outSector=sector;
	       return 1;
	      }
       }
 return 0;
}


static int wallHitScan(MthXyz *ray,MthXyz *pos,int sector,
		       MthXyz *outPos,int *outSector)
{int w,count,loop;
 sSectorType *s;
 MthXyz tempPos;

 count=0;
 while (1)
    {if (count++>50)
	return 0;
     s=level_sector+sector;
     for (loop=0;loop<2;loop++)
	for (w=s->firstWall;w<=s->lastWall;w++)
	   {if (loop==0 && level_wall[w].normal[1]!=0)
	       continue;
	    if (loop==1 && level_wall[w].normal[1]==0)
	       continue;
	    if (hitWallP((Fixed32 *)ray,(Fixed32 *)pos,level_wall+w,
			 (Fixed32 *)&tempPos))
	       if (level_wall[w].nextSector!=-1 &&
		   !(level_wall[w].flags & WALLFLAG_BLOCKSSIGHT))
		  {/* we've gone into a new sector */
		   sector=level_wall[w].nextSector;
		   goto Break;
		  }
	       else
		  {/* we've smacked into a wall */
		   *outPos=tempPos;
		   *outSector=sector;
		   return 1;
	       }
	   }
     return 0;
     Break:
    }
}


int canSee(Sprite *s1,Sprite *s2)
{MthXyz v,pos,outPos;
 int sector,outSector;
 int d2s,d2w;
 /* GCC14: the level's reject table, when it has one (SLEVEL.H): two sectors it says never see
    each other are not traced -- Doom's P_CheckSight reads its REJECT first */
 if (!level_maySee(s1->s,s2->s))
    return 0;
 v.x=s2->pos.x-s1->pos.x;
 v.y=s2->pos.y-s1->pos.y;
 v.z=s2->pos.z-s1->pos.z;
 while (abs(v.x)>F(8) || abs(v.y)>F(8) || abs(v.z)>F(8))
    {v.x>>=1;
     v.y>>=1;
     v.z>>=1;
    }
 pos=s1->pos;
 sector=s1->s;
 if (!wallHitScan(&v,&pos,sector,&outPos,&outSector))
    return 1;
 /* see if collision point is closer than object */
 d2s=f(s1->pos.x-s2->pos.x)*f(s1->pos.x-s2->pos.x)+
     f(s1->pos.y-s2->pos.y)*f(s1->pos.y-s2->pos.y)+
     f(s1->pos.z-s2->pos.z)*f(s1->pos.z-s2->pos.z);
 assert(d2s>=0);
 d2w=f(s1->pos.x-outPos.x)*f(s1->pos.x-outPos.x)+
     f(s1->pos.y-outPos.y)*f(s1->pos.y-outPos.y)+
     f(s1->pos.z-outPos.z)*f(s1->pos.z-outPos.z);
 assert(d2w>=0);
 if (d2s>d2w)
    return 0;
 else
    return 1;
}
