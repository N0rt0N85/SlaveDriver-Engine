/* MPSKY.C -- the split-screen sky, an engine service (GCC14).

   Solo draws its sky on RBG0: one panorama turned by one yaw.  Split screen has two to four
   yaws and RBG0 one parameter set, so this sky belongs to no view.  Its layers drift on their
   own wind, whatever the players do, and every view sees them through its sky openings --
   wherever the VDP1 painted nothing above the view's horizon.  Two players share one band
   (lines 0..111, one horizon); three and four have two bands, one per row of quadrants, cut by
   the two windows (the 3p empty quadrant gets none).

   The layers, back to front, all under the walls (sprite priority 4):
     RBG0, 1  the vault: an opaque 512x256 RGB bitmap over A0+A1, shown at 2x -- the sky's
              gradient and its high clouds.  No coefficient table: A0 holds bitmap too.
     NBG1, 2  the clouds: 32768-colour cells in B1, with gaps; a lit copy of every cell and four
              baked bolts, swapped in through the map when lightning strikes.
     NBG0, 3  the haze: 32768-colour cells in B0 -- the panorama's own ridges, fogged, over a
              veil that goes to the fog's black at the horizon; semi-transparent (colour
              calculation), raised and thickened as the fog closes in.
   The CRAM is full in split screen (the players' colours took the sky's bank, MPLAYER.C), so
   every layer is RGB, and NBG0/NBG1 at 32768 colours evict NBG2/NBG3 (VDP2 p.61): three
   textured layers is the ceiling.  Plus, per view, a sun or a moon on the VDP1 (mpSkySun).

   Everything comes from the level's own sky (PLAX.C): its palette, and its panorama read back
   from A1 before the vault overwrites it -- colours by luminance percentile, the ridge line of
   each column, the brightest azimuth for the sun.  A new seed at every build: the same level
   never gets the same clouds twice.  Built when the level starts in split screen or when a
   second player joins (~0.3 s of CPU); a third or fourth player only re-lays the maps.
   Only when the level loaded no VDP2 picture: B0-B1 then hold nothing (Doom's STATIC.DAT
   sheet is zeros); a solo level load takes everything back (mpSkyOff, then setVDP2/initPlax). */
#include <stdlib.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "walls.h"
#include "plax.h"
#include "pic.h"
#include "mplayer.h"
#include "v_blank.h"
#include "mpsky.h"

extern Sprite *camera;
extern SclRotreg *SclRotregBuff;
void project_point(MthXyz *v,XyInt *p);        /* wallasm_gnu.s */

int mpSkyOn;

/* VDP2 VRAM (offsets from SCL_VDP2_VRAM).  A0+A1: the vault, 512x256 x 2 bytes, all of it. */
#define HAZE_CHARS   0x40000    /* B0: blank + 7x64 cells of 128 bytes */
#define CLOUD_MAP    0x50000    /* B0: NBG1's plane, 64x64 two-word names */
#define RPT_ADDR     0x5F000    /* B0: RBG0's parameter table, A at +0, B at +0x80 */
#define CLOUD_CHARS  0x60000    /* B1: blank + 5x64 cells, 5x64 lit, 4 bolts of 5x3 */
#define HAZE_MAP     0x7C000    /* B1: NBG0's plane */
#define CLOUD_LIT    (1+320)
#define CLOUD_BOLT   (1+640)
#define VW(o)        ((volatile Uint16 *)(SCL_VDP2_VRAM+(o)))
#define VL(o)        ((volatile Uint32 *)(SCL_VDP2_VRAM+(o)))
#define CHARNM(base,k) (((base)>>5)+((k)<<2))  /* a 32768-colour cell is 4 units of 32 bytes */

/* Each layer reads its cells from one bank and its names from the other: B0 = NBG0's four
   character reads + NBG1's name read, B1 the reverse.  Names at T0, so the character reads may
   sit anywhere in T0-T2/T4-T7 (VDP2 p.34, Table 3.4).  CPU at T7 in both banks after a no-access
   T6 (the partitioned-VRAM rule, p.36).  A0/A1 belong to the rotation (RAMCTL): their patterns
   are don't-care, kept as solo has them. */
static const Uint16 skyCycle[8]={0xeeee,0xeeee,0xeeee,0xeeee,
				 0x144f,0x44fe,         /* B0: N1 name, N0 x4, CPU */
				 0x055f,0x55fe};        /* B1: N0 name, N1 x4, CPU */

/* per layout (players 2, 3, 4): the first map row of each band's clouds and haze, the bands */
static const signed char cloudRow[3][2]={{6,-1},{0,14},{0,14}};
static const signed char hazeRow[3][2]={{10,-1},{3,17},{3,17}};
static const short bandRect[3][2][4]={{{0,0,319,111},{1,1,0,0}},
				      {{0,0,319,55},{0,112,159,167}},
				      {{0,0,319,55},{0,112,319,167}}};

typedef struct {short r,g,b;} C3;               /* 0..255 */

static unsigned int seed;
static unsigned char perm[256];
static C3 cZen,cHor,cDark,cMid,cLight,cTop,cTint,cVeil;
static int stormy,night,sunAz,layout;
static Fixed32 windDeck,windCloud,windHaze;
static int windDir,hazeY,ccRate,lastB[3];
static unsigned int now,evNext,gustStart,gustEnd;
static int evType,evT,evBand,evCol,evBoltNm,flash;
static short savedPri[2];

static const unsigned char bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};

static int rnd(void)
{seed=seed*1103515245+12345;
 return (seed>>16)&0x7fff;
}

static void mix(C3 *d,const C3 *a,const C3 *b,int t)      /* t 0..256 */
{d->r=a->r+(((b->r-a->r)*t)>>8);
 d->g=a->g+(((b->g-a->g)*t)>>8);
 d->b=a->b+(((b->b-a->b)*t)>>8);
}

static int lum(const C3 *c)
{return (c->r*77+c->g*150+c->b*29)>>8;
}

static int clamp8(int v)
{return v<0? 0: v>255? 255: v;
}

/* 8-bit colour to VDP2 RGB, opaque, with a 4x4 ordered dither of one 5-bit step */
static Uint16 pack(const C3 *c,int x,int y)
{int d=bayer[((y&3)<<2)|(x&3)]-8,r,g,b;
 r=clamp8(c->r+d)>>3; g=clamp8(c->g+d)>>3; b=clamp8(c->b+d)>>3;
 return 0x8000|(b<<10)|(g<<5)|r;
}

/* ---- the panorama (PLAX.C: row yb = azimuth, 256 per 90 degrees; column xb = height,
   up as xb grows, the horizon near 256) ---- */
#define H0 256
static const unsigned short *pal;
#define PANO(yb,xb) (*(volatile unsigned char *)(SCL_VDP2_VRAM_A1+((yb)<<9)+(xb)))

static void panoPixel(int yb,int xb,C3 *c)
{unsigned short v=pal[PANO(yb&255,xb)];
 c->r=(v&31)<<3; c->g=((v>>5)&31)<<3; c->b=((v>>10)&31)<<3;
}

static int diff(const C3 *a,const C3 *b)
{return abs(a->r-b->r)+abs(a->g-b->g)+abs(a->b-b->b);
}

/* The ridge of every azimuth (where the colour leaves the sky's, walking down), and the sky's
   colours: zenith, horizon, and four luminance percentiles of what lies above the ridges. */
static void analyse(unsigned char *ridge)
{int hist[32][4],yb,xb,i,n,run,total,best=-1,maxR=0;
 int zs[4],hs[4];
 C3 c,ref;
 for (i=0;i<32;i++)
    hist[i][0]=hist[i][1]=hist[i][2]=hist[i][3]=0;
 for (i=0;i<4;i++)
    zs[i]=hs[i]=0;
 for (yb=0;yb<256;yb++)
    {int sum=0,cnt=0,r=0;
     ref.r=ref.g=ref.b=0;
     for (xb=H0+104;xb<H0+124;xb+=4)
	{panoPixel(yb,xb,&c);
	 ref.r+=c.r/5; ref.g+=c.g/5; ref.b+=c.b/5;
	}
     run=0;
     for (xb=H0+100;xb>H0-8;xb--)
	{panoPixel(yb,xb,&c);
	 if (diff(&c,&ref)>60)
	    {if (++run==2)
		{r=xb+2-H0;
		 break;
		}
	    }
	 else
	    {run=0;
	     ref.r=(ref.r*3+c.r)>>2; ref.g=(ref.g*3+c.g)>>2; ref.b=(ref.b*3+c.b)>>2;
	    }
	}
     if (r<0) r=0;
     ridge[yb]=r;
     if (r>maxR) maxR=r;
     if (yb&3)
	continue;
     /* the sky above this ridge */
     for (xb=H0+r+4;xb<H0+124;xb+=3)
	{panoPixel(yb,xb,&c);
	 i=lum(&c)>>3;
	 hist[i][0]+=c.r; hist[i][1]+=c.g; hist[i][2]+=c.b; hist[i][3]++;
	 sum+=lum(&c); cnt++;
	 if (xb>=H0+100)
	    {zs[0]+=c.r; zs[1]+=c.g; zs[2]+=c.b; zs[3]++;}
	 else if (xb<H0+r+16)
	    {hs[0]+=c.r; hs[1]+=c.g; hs[2]+=c.b; hs[3]++;}
	}
     if (cnt && sum/cnt>best)
	{best=sum/cnt;
	 sunAz=yb;
	}
    }
 /* the ridges, scaled to the haze's 30 lines */
 if (maxR>30)
    for (yb=0;yb<256;yb++)
       ridge[yb]=(ridge[yb]*30)/maxR;
 ridge[256]=maxR>30? maxR: 30;  /* the scale back to the panorama, read by makeHaze */
 if (zs[3]) {cZen.r=zs[0]/zs[3]; cZen.g=zs[1]/zs[3]; cZen.b=zs[2]/zs[3];}
 if (hs[3]) {cHor.r=hs[0]/hs[3]; cHor.g=hs[1]/hs[3]; cHor.b=hs[2]/hs[3];}
 else cHor=cZen;
 for (total=0,i=0;i<32;i++)
    total+=hist[i][3];
 {static const unsigned char pct[4]={15,50,85,98};
  C3 *out[4]={&cDark,&cMid,&cLight,&cTop};
  int k;
  for (k=0;k<4;k++)
     {int want=(total*pct[k])/100;
      for (run=0,i=0;i<31;i++)
	 {run+=hist[i][3];
	  if (run>=want && hist[i][3])
	     break;
	 }
      n=hist[i][3]? hist[i][3]: 1;
      out[k]->r=hist[i][0]/n; out[k]->g=hist[i][1]/n; out[k]->b=hist[i][2]/n;
     }
 }
 /* a grey, dull sky is a stormy one; a dark zenith is night */
 {int mx=cMid.r,mn=cMid.r,s;
  if (cMid.g>mx) mx=cMid.g;
  if (cMid.b>mx) mx=cMid.b;
  if (cMid.g<mn) mn=cMid.g;
  if (cMid.b<mn) mn=cMid.b;
  s=255-(mx-mn)*5;
  if (lum(&cMid)<90) s+=60;
  stormy=clamp8(s);
  night=lum(&cZen)<48;
 }
 /* lightning's tint: the brightest sky colour pushed to full, halfway to white */
 {int mx=cTop.r;
  if (cTop.g>mx) mx=cTop.g;
  if (cTop.b>mx) mx=cTop.b;
  if (mx<1) mx=1;
  cTint.r=(cTop.r*255/mx+255)>>1; cTint.g=(cTop.g*255/mx+255)>>1; cTint.b=(cTop.b*255/mx+255)>>1;
 }
 /* the veil: the horizon's colour, most of the way to the fog's black */
 cVeil.r=cHor.r>>2; cVeil.g=cHor.g>>2; cVeil.b=cHor.b>>2;
}

/* ---- value noise, wrapping at 512 pixels horizontally ---- */
static int lat(int x,int y)
{return perm[(perm[x&255]+y)&255];
}

static int vnoise(int x,int y,int sh,int o)
{int n=(512>>sh)-1,xi=x>>sh,yi=y>>sh,fx=(x<<(8-sh))&255,fy=(y<<(8-sh))&255,a,b,c,d;
 int x1=(xi+1)&n;
 xi&=n;
 a=lat(xi+o,yi); b=lat(x1+o,yi); c=lat(xi+o,yi+1); d=lat(x1+o,yi+1);
 fx=(fx*fx*(768-2*fx))>>16;
 fy=(fy*fy*(768-2*fy))>>16;
 a+=((b-a)*fx)>>8;
 c+=((d-c)*fx)>>8;
 return a+(((c-a)*fy)>>8);
}

static int fbm(int x,int y,int o)
{return (vnoise(x,y,7,o)*8+vnoise(x,y,6,o+61)*4+vnoise(x,y,5,o+122)*2+vnoise(x,y,4,o+183))/15;
}

/* ---- the vault: rows 0..55 of the A0+A1 bitmap, copied to 112..167 for the second band ---- */
static void makeVault(void)
{volatile Uint16 *v=VW(0);
 int x,y,n,a,t,thr=150-stormy/6;
 C3 base,cc;
 for (y=0;y<56;y++)
    {mix(&base,&cZen,&cHor,(y*y*256)/(55*55));
     for (x=0;x<512;x++)
	{n=fbm(x,y*3,0);
	 a=clamp8((n-thr)*4);
	 t=clamp8((n-thr)*3+128-y*2);
	 mix(&cc,&cMid,&cLight,t);
	 mix(&cc,&base,&cc,a*3>>2);
	 v[(y<<9)+x]=pack(&cc,x,y);
	}
    }
 for (x=0;x<56*512;x++)
    v[112*512+x]=v[x];
}

static void cloudPixel(int k,int x,int y,Uint16 p)
{VW(CLOUD_CHARS)[(k<<6)+(((y&7)<<3)|(x&7))]=p;
}

/* the clouds, 512x40: puffs under an envelope, lit from above (bright where the puff three
   lines up is thin); the lit copy is the same mask pushed toward lightning's tint */
static void makeClouds(void)
{unsigned char above[4][512];
 int x,y,n,a,t,k,env,thr=140-(stormy>>3);
 C3 c,lit;
 for (x=0;x<64;x++)
    {VW(CLOUD_CHARS)[x]=0;      /* the blank cell */
    }
 for (y=0;y<40;y++)
    {env=256-(((y-20)*(y-20))<<8)/400;
     for (x=0;x<512;x++)
	{k=((y>>3)<<6)+(x>>3);
	 n=fbm(x,y*2,97);
	 a=((n*env)>>8)-thr;
	 above[y&3][x]=clamp8(a*2);
	 if (a<=0 || (a<24 && (a*16)/24<=bayer[((y&3)<<2)|(x&3)]))
	    {cloudPixel(1+k,x,y,0);
	     cloudPixel(CLOUD_LIT+k,x,y,0);
	     continue;
	    }
	 t=clamp8(220-(y>=3? above[(y-3)&3][x]: 0)+((n-128)>>1));
	 if (t<128)
	    mix(&c,&cDark,&cMid,t<<1);
	 else
	    mix(&c,&cMid,&cTop,(t-128)<<1);
	 mix(&lit,&c,&cTint,160);
	 cloudPixel(1+k,x,y,pack(&c,x,y));
	 cloudPixel(CLOUD_LIT+k,x,y,pack(&lit,x,y));
	}
    }
}

/* four bolts at map columns 6, 22, 38, 54, each over 3x5 cells: the lit cells, and a jagged
   core with a glow on each side, forked once */
static void boltPixel(int b,int x,int y,const C3 *c)
{int c0=6+(b<<4),col=(x>>3)-c0;
 if (col<0 || col>2 || y<0 || y>=40)
    return;
 cloudPixel(CLOUD_BOLT+b*15+(y>>3)*3+col,x,y,pack(c,x,y));
}

static void makeBolts(void)
{int b,i,x,y,fx,fy,fl,fd,c0,r;
 C3 core,glow;
 mix(&core,&cTint,&(C3){255,255,255},192);
 glow=cTint;
 for (b=0;b<4;b++)
    {c0=6+(b<<4);
     for (i=0;i<15;i++)
	{volatile Uint16 *s=VW(CLOUD_CHARS)+((CLOUD_LIT+(i/3)*64+c0+i%3)<<6),
	    *d=VW(CLOUD_CHARS)+((CLOUD_BOLT+b*15+i)<<6);
	 for (x=0;x<64;x++)
	    d[x]=s[x];
	}
     x=(c0<<3)+12;
     fy=8+rnd()%10; fl=10+rnd()%8; fd=(rnd()&1)? 1: -1; fx=0;
     for (y=0;y<40;y++)
	{r=rnd();
	 x+=(r%3)-1;
	 if (!(r&0x70))
	    x+=(r&0x80)? 2: -2;
	 if (x<(c0<<3)+2) x=(c0<<3)+2;
	 if (x>(c0<<3)+21) x=(c0<<3)+21;
	 boltPixel(b,x-1,y,&glow);
	 boltPixel(b,x+1,y,&glow);
	 boltPixel(b,x,y,&core);
	 if (y==fy)
	    fx=x;
	 if (y>fy && y<fy+fl)
	    {fx+=fd*((r>>8)&1)+((r&0x300)? 0: fd);
	     boltPixel(b,fx,y,&glow);
	    }
	}
    }
}

/* the haze, 512x56: 32 lines of the panorama's own ridges, fogged the nearer they come to the
   horizon, over a dithered veil that thickens down to it; 24 lines of black below, which the
   fog raises into the band as it closes in */
static void makeHaze(const unsigned char *ridge)
{int x,y,h,k,d,xb;
 C3 c;
 for (x=0;x<64;x++)
    VW(HAZE_CHARS)[x]=0;
 for (y=0;y<56;y++)
    for (x=0;x<512;x++)
       {Uint16 p;
	k=1+((y>>3)<<6)+(x>>3);
	h=32-y;
	if (h<=0)
	   p=0x8000;
	else if (h<=ridge[x&255])
	   {xb=H0+(h*ridge[256])/30;
	    panoPixel(x,xb,&c);
	    mix(&c,&c,&cVeil,96+((32-h)<<2));
	    p=pack(&c,x,y);
	   }
	else
	   {d=((32-h)*(32-h)*200)>>10;
	    p=(d>(bayer[((y&3)<<2)|(x&3)]<<4)+8)? pack(&cVeil,x,y): 0;
	   }
	VW(HAZE_CHARS)[(k<<6)+(((y&7)<<3)|(x&7))]=p;
       }
}

/* ---- the maps ---- */
static void mapRows(int players)
{int li=players-2,r,c,b,row;
 volatile Uint32 *cm=VL(CLOUD_MAP),*hm=VL(HAZE_MAP);
 Uint32 cb=CHARNM(CLOUD_CHARS,0),hb=CHARNM(HAZE_CHARS,0);
 for (r=0;r<24;r++)
    for (c=0;c<64;c++)
       cm[(r<<6)+c]=hm[(r<<6)+c]=0;
 for (b=0;b<2;b++)
    {if ((row=cloudRow[li][b])>=0)
	for (r=0;r<5;r++)
	   for (c=0;c<64;c++)
	      cm[((row+r)<<6)+c]=cb+((1+(r<<6)+c)<<2);
     if ((row=hazeRow[li][b])>=0)
	for (r=0;r<7;r++)
	   for (c=0;c<64;c++)
	      hm[((row+r)<<6)+c]=hb+((1+(r<<6)+c)<<2);
    }
}

/* lightning's cells: lit around column col (+-4, a dithered ring at 5-6), the bolt's 3x5
   cells over bolt b's columns; on=0 puts the plain cells back */
static void mapLight(int band,int col,int bolt,int on)
{int row=cloudRow[layout-2][band],r,dc,cc,k;
 volatile Uint32 *cm=VL(CLOUD_MAP);
 Uint32 cb=CHARNM(CLOUD_CHARS,0);
 if (row<0)
    return;
 for (r=0;r<5;r++)
    for (dc=-6;dc<=6;dc++)
       {cc=(col+dc)&63;
	k=1+(r<<6)+cc;
	if (on && (abs(dc)<=4 || !((r+cc)&1)))
	   k+=CLOUD_LIT-1;
	if (on && bolt>=0 && cc>=6+(bolt<<4) && cc<=8+(bolt<<4))
	   k=CLOUD_BOLT+bolt*15+r*3+cc-6-(bolt<<4);
	cm[((row+r)<<6)+cc]=cb+(k<<2);
       }
}

/* ---- the sun (VDP1, per view): a 32x32 4-bit cell and its lookup table, from the sky ---- */
#define SUN_CHAR 511            /* the last of SPR.C's MAXNMCHARS: never a pic slot */
#define SUN_CLUT 0              /* Doom's fonts take tables 1-3 (PRINT.C initFonts) */
static int sunOk;
static Fixed32 sunDir[3];
static short sunBB[MPMAX][4];
static char sunBBok[MPMAX];

static void makeSun(void)
{static struct sprLookupTbl tbl;   /* dmaMemCpy reads it after we return */
 C3 core,rim,c;
 int i,x,y,r2,idx;
 volatile unsigned char *p;
 static const Fixed32 sin16[16]={0,25080,46341,60547,65536,60547,46341,25080,
				 0,-25080,-46341,-60547,-65536,-60547,-46341,-25080};
 sunOk=0;
 if (EZ_charRoom()<512+64)
    return;
 EZ_setChar(SUN_CHAR,COLOR_1,32,32,NULL);
 if (night)
    {mix(&core,&cTop,&(C3){215,222,240},150);
     mix(&rim,&cZen,&core,90);
    }
 else
    {mix(&core,&cTop,&(C3){255,244,214},170);
     mix(&rim,&cHor,&cTint,128);
    }
 tbl.entry[0]=0;
 for (i=1;i<16;i++)
    {mix(&c,&rim,&core,(i*256)/15);
     tbl.entry[i]=pack(&c,0,0);
    }
 EZ_setLookupTbl(SUN_CLUT,&tbl);
 p=(volatile unsigned char *)(0x25c00000+(EZ_charNoToVram(SUN_CHAR)<<3));
 for (y=0;y<32;y++)
    for (x=0;x<32;x+=2)
       {int v[2],j;
	for (j=0;j<2;j++)
	   {int dx=(x+j)*2-31,dy=y*2-31;
	    r2=(dx*dx+dy*dy)>>2;          /* squared radius in pixels */
	    if (night)
	       idx=r2<36? 15-((lat(x+j,y)>>6)&1)*3: r2<81? (r2<56? 5: 3): 0;
	    else
	       idx=r2<16? 15: r2<36? 13: r2<64? 10: r2<225? 1+((225-r2)*7)/161: 0;
	    if (idx && idx<8 && (idx<<1)<=bayer[((y&3)<<2)|((x+j)&3)])
	       idx=0;                     /* the glow thins out, dithered */
	    v[j]=idx;
	   }
	p[(y<<4)+(x>>1)]=(v[0]<<4)|v[1];
       }
 /* where: the panorama's brightest azimuth, one of its four turns, 14 degrees up */
 i=((sunAz>>6)+((rnd()&3)<<2))&15;
 sunDir[0]=MTH_Mul(sin16[i],63587);
 sunDir[1]=15854;
 sunDir[2]=MTH_Mul(sin16[(i+4)&15],63587);
 sunOk=1;
}

/* ---- registers ---- */
static void setWindows(int players)
{const short (*b)[4]=bandRect[players-2];
 int two=players>2,bits=two? 0x8f: 0x03;      /* cut outside W0 (AND outside W1) */
 Scl_w_reg.win0_start[0]=b[0][0]<<1; Scl_w_reg.win0_start[1]=b[0][1];
 Scl_w_reg.win0_end[0]=b[0][2]<<1;   Scl_w_reg.win0_end[1]=b[0][3];
 Scl_w_reg.win1_start[0]=b[1][0]<<1; Scl_w_reg.win1_start[1]=b[1][1];
 Scl_w_reg.win1_end[0]=b[1][2]<<1;   Scl_w_reg.win1_end[1]=b[1][3];
 Scl_w_reg.wincontrl[0]=(bits<<8)|bits;
 Scl_w_reg.wincontrl[2]=(Scl_w_reg.wincontrl[2]&0xff00)|bits;
}

static void setVault(int players)
{SclRotreg *r=SclRotregBuff;
 r->screenst.x=r->screenst.y=r->screenst.z=0;
 r->screendlt.x=0;       r->screendlt.y=F(1);
 r->delta.x=F(1);        r->delta.y=0;
 r->matrix_a=F(1); r->matrix_b=0; r->matrix_c=0;
 r->matrix_d=0;    r->matrix_e=F(1); r->matrix_f=0;
 r->viewp.x=r->viewp.y=r->viewp.z=0;
 r->rotatecenter.x=r->rotatecenter.y=r->rotatecenter.z=0;
 r->move.x=windDeck; r->move.y=0;
 r->zoom.x=F(1)>>1;                   /* two pixels a texel */
 r->zoom.y=players>2? F(1): F(1)>>1;  /* a band's 56 texel rows: 112 lines, or 56 */
 r->k_tab=0; r->k_delta.x=r->k_delta.y=0;
}

static void registers(void)
{int i;
 SCL_InitRotateTable(SCL_VDP2_VRAM+RPT_ADDR,1,SCL_RBG0,SCL_NON);
 Scl_r_reg.paramode=0;
 Scl_r_reg.paramcontrl=0;
 Scl_r_reg.k_contrl=0;          /* no coefficient table: A0 is bitmap */
 Scl_r_reg.k_offset=0;
 Scl_s_reg.ramcontrl=(Scl_s_reg.ramcontrl&0xff00)|0x0f;   /* A0, A1: RBG0 bitmap */
 for (i=0;i<8;i++)
    Scl_s_reg.vramcyc[i]=skyCycle[i];
 Scl_s_reg.dispenbl&=~0x0300;   /* NBG0, NBG1: RGB transparency on */
 Scl_s_reg.dispenbl|=0x1000;    /* the vault is opaque */
 Scl_d_reg.charcontrl0=0x3030;  /* NBG0, NBG1: cells 1x1, 32768 colours */
 Scl_d_reg.charcontrl1=(Scl_d_reg.charcontrl1&0x00ff)|0x3200;  /* RBG0: 512x256 bitmap, 32768 */
 Scl_d_reg.bmpalnum1=0;
 Scl_d_reg.patnamecontrl[0]=0;  /* two-word names */
 Scl_d_reg.patnamecontrl[1]=0;
 Scl_d_reg.platesize&=~0x0c0f;  /* NBG0, NBG1 planes 1x1; RBG0 screen-over: repeat */
 Scl_d_reg.mapoffset0&=~0x0077;
 Scl_d_reg.mapoffset1&=~0x0007; /* the vault at 0 */
 Scl_d_reg.normap[0]=Scl_d_reg.normap[1]=(HAZE_MAP/0x4000)*0x101;
 Scl_d_reg.normap[2]=Scl_d_reg.normap[3]=(CLOUD_MAP/0x4000)*0x101;
 Scl_n_reg.n0_delta_x=Scl_n_reg.n0_delta_y=F(1);
 Scl_n_reg.n1_delta_x=Scl_n_reg.n1_delta_y=F(1);
 Scl_n_reg.zoomenbl&=~0x0303;
 Scl_n_reg.linecontrl&=~0x3f3f;
 savedPri[0]=SCL_GetPriority(SCL_NBG1);
 savedPri[1]=SCL_GetPriority(SCL_RBG0);
 SCL_SetPriority(SCL_NBG0,3);
 SCL_SetPriority(SCL_NBG1,2);
 SCL_SetPriority(SCL_RBG0,1);
 SCL_SET_N0CCEN(1);
 SCL_SET_N1CCEN(0);
 SCL_SET_R0CCEN(0);
 ccRate=-1;
 lastB[0]=lastB[1]=lastB[2]=1000;
}

/* ---- entry points ---- */
static int skyAllowed(void)
{return vdp2PicCount()==0;
}

void mpSkyPlayers(int players)
{unsigned char ridge[257];
 int i;
 if (players<2 || players>4 || !skyAllowed())
    return;
 if (!mpSkyOn)
    {seed=seed*69069+vtimer+(players<<8)+1;
     for (i=0;i<256;i++)
	perm[i]=i;
     for (i=255;i>0;i--)
	{int j=rnd()%(i+1),t=perm[i];
	 perm[i]=perm[j]; perm[j]=t;
	}
     pal=plaxPalette();
     Scl_s_reg.dispenbl&=~0x0013;
     /* now, not at the next image: solo's sky is on screen and its K table is about to go */
     *(volatile Uint16 *)0x25f80020=Scl_s_reg.dispenbl;
     analyse(ridge);
     makeHaze(ridge);           /* reads the panorama: before the vault */
     makeClouds();
     makeBolts();
     for (i=0;i<64*64;i++)
	VL(CLOUD_MAP)[i]=VL(HAZE_MAP)[i]=0;
     makeVault();
     makeSun();
     windDir=(rnd()&1)? 1: -1;
     windDeck=windCloud=windHaze=0;
     hazeY=0;
     now=0;
     evType=0; flash=0;
     evNext=240+rnd()%240;
     gustEnd=0;
     for (i=0;i<MPMAX;i++)
	sunBBok[i]=0;
     registers();
     mpSkyOn=1;
    }
 layout=players;
 mapRows(players);
 setWindows(players);
 setVault(players);
 if (SclProcess==0)
    SclProcess=1;
}

void mpSkyOff(void)
{if (!mpSkyOn)
    return;
 mpSkyOn=0;
 sunOk=0;
 Scl_s_reg.dispenbl&=~0x0013;
 Scl_w_reg.wincontrl[0]=0;
 Scl_w_reg.wincontrl[2]&=0xff00;
 SCL_SetPriority(SCL_NBG1,savedPri[0]);
 SCL_SetPriority(SCL_RBG0,savedPri[1]);
 SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0|SCL_NBG1|SCL_RBG0,0,0,0);
 if (SclProcess==0)
    SclProcess=1;
}

void mpSkyShow(int on)
{if (!mpSkyOn)
    return;
 if (on)
    Scl_s_reg.dispenbl|=0x0013;
 else
    Scl_s_reg.dispenbl&=~0x0013;
}

/* the events: a bolt, a sheet of lightning, a gust.  Stormy skies strike more often. */
static int flashCurve(int t,int bolt)
{if (bolt)
    return t<3? 256: t<6? 60: t<9? 220: t<24? (220*(24-t))/15: 0;
 return t<2? 150: t<5? 30: t<8? 130: t<20? (130*(20-t))/12: 0;
}

static void events(int ticks)
{if (!evType && now>=evNext)
    {int r=rnd()%100;
     evType=r<50? 1: r<85? 2: 3;
     evT=0;
     evBand=layout>2? rnd()&1: 0;
     if (evType==1)
	{evBoltNm=rnd()&3;
	 evCol=7+(evBoltNm<<4);
	 mapLight(evBand,evCol,evBoltNm,1);
	}
     else if (evType==2)
	{evCol=rnd()&63;
	 mapLight(evBand,evCol,-1,1);
	}
     else
	{gustStart=now;
	 gustEnd=now+240+rnd()%240;
	}
    }
 if (!evType)
    return;
 evT+=ticks;
 if (evType==3)
    {if (now>=gustEnd)
	evType=0;
    }
 else
    {flash=flashCurve(evT,evType==1);
     if (evType==1 && evT>=9 && evT-ticks<9)
	mapLight(evBand,evCol,-1,1);      /* the bolt is gone, the clouds still glow */
     if (evT>=16 && evT-ticks<16)
	mapLight(evBand,evCol,-1,0);
     if (evT>=24)
	{evType=0;
	 flash=0;
	}
    }
 if (!evType)
    {int base=1200-stormy*4;           /* 20 s calm .. 3 s at the stormiest */
     evNext=now+base+rnd()%base;
    }
}

void mpSkyFrame(int fog,const int *offA,int ticks)
{int t,g=256,dy,rate,i,b[3];
 if (!mpSkyOn)
    return;
 if (ticks<1) ticks=1;
 if (ticks>8) ticks=8;
 now+=ticks;
 events(ticks);
 /* a gust: the winds swell to three times and back */
 if (now<gustEnd)
    {int ramp=gustEnd-now;
     if (now-gustStart<ramp) ramp=now-gustStart;
     if (ramp>120) ramp=120;
     g=256+ramp*4;
    }
 windDeck+=windDir*((0x1000*g)>>8)*ticks;     /* 1/16 texel a tick: 7.5 pixels a second */
 windCloud+=windDir*((0x2800*g)>>8)*ticks;    /* the nearer clouds, 9.4 */
 windHaze+=windDir*((0x0600*g)>>8)*ticks;     /* the haze, 1.4 */
 SclRotregBuff->move.x=windDeck&(F(512)-1);
 Scl_n_reg.n1_move_x=windCloud&(F(512)-1);
 Scl_n_reg.n1_move_y=0;
 Scl_n_reg.n0_move_x=windHaze&(F(512)-1);
 /* the fog: 4096 = none, 512 = the thickest.  The haze climbs up to 20 lines and loses its
    transparency */
 t=fog>=4096? 0: fog<=512? 256: ((4096-fog)<<8)/3584;
 dy=t*20;                       /* 8.8 lines */
 hazeY+=(dy-hazeY)>>3;
 Scl_n_reg.n0_move_y=hazeY<<8;
 rate=12-((t*9)>>8);
 if (rate!=ccRate)
    {SCL_SetColMixRate(SCL_NBG0,rate);
     ccRate=rate;
    }
 /* offset B: A (the damage flash, the fades), plus the lightning in its tint */
 b[0]=offA[0]+((cTint.r*flash)>>9);
 b[1]=offA[1]+((cTint.g*flash)>>9);
 b[2]=offA[2]+((cTint.b*flash)>>9);
 for (i=0;i<3;i++)
    if (b[i]>255) b[i]=255;
 if (b[0]!=lastB[0] || b[1]!=lastB[1] || b[2]!=lastB[2])
    {SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0|SCL_NBG1|SCL_RBG0,b[0],b[1],b[2]);
     lastB[0]=b[0]; lastB[1]=b[1]; lastB[2]=b[2];
    }
 if (SclProcess==0)
    SclProcess=1;
}

/* the view's sky box (WALLS.C plaxBB, local), for its next sun */
void mpSkyViewDone(int view)
{if (!mpSkyOn)
    return;
 sunBBok[view]=plaxBBxmin<plaxBBxmax && plaxBBymin<plaxBBymax;
 sunBB[view][0]=plaxBBxmin; sunBB[view][1]=plaxBBymin;
 sunBB[view][2]=plaxBBxmax; sunBB[view][3]=plaxBBymax;
}

/* Emitted first in the view's list, so every wall and thing covers it; clipped to where the
   view saw sky in its last image, so the fog's cut-off holes never show it. */
void mpSkySun(int view,MthMatrix *m)
{MthXyz w,t;
 XyInt p,r[2];
 if (!mpSkyOn || !sunOk || !sunBBok[view])
    return;
 w.x=camera->pos.x+(sunDir[0]<<11);
 w.y=camera->pos.y+(sunDir[1]<<11);
 w.z=camera->pos.z+(sunDir[2]<<11);
 MTH_CoordTrans(m,&w,&t);
 if (t.z<F(64))
    return;
 project_point(&t,&p);
 if (p.x+16<sunBB[view][0] || p.x-16>sunBB[view][2] ||
     p.y+16<sunBB[view][1] || p.y-16>sunBB[view][3])
    return;
 r[0].x=viewCx+sunBB[view][0]; r[0].y=viewCy+sunBB[view][1];
 r[1].x=viewCx+sunBB[view][2]; r[1].y=viewCy+sunBB[view][3];
 if (r[1].x>viewCx+viewXmax-1) r[1].x=viewCx+viewXmax-1;
 if (r[1].y>viewCy+viewYmax-1) r[1].y=viewCy+viewYmax-1;
 EZ_userClip(r);
 p.x-=16; p.y-=16;
 EZ_normSpr(DIR_NOREV,UCLPIN_ENABLE|ECD_DISABLE|COLOR_1,SUN_CLUT,SUN_CHAR,&p,NULL);
 r[0].x=viewCx+viewXmin;   r[0].y=viewCy+viewYmin;
 r[1].x=viewCx+viewXmax-1; r[1].y=viewCy+viewYmax-1;
 EZ_userClip(r);
}
