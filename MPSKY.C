/* MPSKY.C -- the split-screen sky, an engine service (GCC14).

   Solo draws its sky on RBG0: one panorama turned by one yaw.  Split screen has two to four
   yaws and RBG0 one parameter set, so this sky belongs to no view.  Its layers drift on their
   own wind, whatever the players do, and every view sees them through its sky openings --
   wherever the VDP1 painted nothing above the view's horizon.  Two players share one band
   (lines 0..111, one horizon); three and four have two bands, one per row of quadrants, cut by
   the two windows (the 3p empty quadrant gets none).

   A band runs from the top of its views down to THEIR BOTTOM, not to the horizon: the vault's
   96 rows are 56 down to the horizon (line 112 of a half screen, 56 of a quadrant) and 40 under
   it, where the gradient goes to the haze's colour -- so a player standing high sees sky, not
   black, below the horizon.  The fog never moves a layer: it thickens the haze and takes all
   three towards black through colour offset B, which is the fog passing in front of the sky.

   The layers, back to front, all under the walls (sprite priority 4):
     RBG0, 1  the vault: an opaque 512x256 RGB bitmap over A0+A1, one texel a pixel -- the
              sky's gradient, its high haze and its fogged floor.  No coefficient table: A0
              holds bitmap too.
     NBG1, 2  the clouds: 32768-colour cells in B1, with gaps; a lit copy of every cell and four
              baked bolts, swapped in through the map when lightning strikes.  Mixed with the
              vault, and kept near its tone.
     NBG0, 3  the mist: 32768-colour cells in B0 -- the fog of the horizon, thickening down to
              it and dissolving upward into the sky's colour; semi-transparent (colour
              calculation), thickened as the fog closes in.
   The three are meant to be hard to tell apart: one slow warped field feeds all of them (soft,
   density), so what moves is thickness, not a pattern.
   The CRAM is full in split screen (the players' colours took the sky's bank, MPLAYER.C), so
   every layer is RGB, and NBG0/NBG1 at 32768 colours evict NBG2/NBG3 (VDP2 p.61): three
   textured layers is the ceiling.  Plus, per view, a moon on the VDP1 (mpSkyMoon).

   Everything comes from the level's own sky (PLAX.C): its palette, and its panorama read back
   from A1 before the vault overwrites it -- colours by luminance percentile, and the brightest
   azimuth, where the moon goes.  A new seed at every build: the same level
   never gets the same clouds twice.  Built when the level starts in split screen or when a
   second player joins (~0.3 s of CPU); a third or fourth player only re-lays the maps.
   The moon (mpSkyMoon) is two fans of VDP1 triangles per view, not a character: the level's
   tiles leave no room in the VDP1's character VRAM (PIC.C initPicSystem takes what is left).

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

/* per layout (players 2, 3, 4): the first map row of each band's clouds and haze, the bands --
   each the whole height of its views (2 players: lines 0..191; 3-4: a row of quadrants) */
static const signed char cloudRow[3][2]={{6,-1},{0,14},{0,14}};
static const signed char hazeRow[3][2]={{10,-1},{3,17},{3,17}};
static const short bandRect[3][2][4]={{{0,0,319,191},{1,1,0,0}},
				      {{0,0,319,95},{0,112,159,207}},
				      {{0,0,319,95},{0,112,319,207}}};
#define VAULT_ROWS   96         /* a band's rows: 56 down to the horizon, 40 under it */
#define VAULT_HOR    56
#define VAULT_BAND2  112        /* the second band's first row */

typedef struct {short r,g,b;} C3;               /* 0..255 */

static unsigned int seed;
static unsigned char perm[256];
static C3 cZen,cHor,cDark,cMid,cLight,cTop,cTint,cVeil;
static int stormy,brightAz,layout;
static Fixed32 windDeck,windCloud,windHaze;
static int windDir,fogDim,ccRate,lastB[3];
static unsigned int now,evNext,gustStart,gustEnd;
static int evType,evT,evBand,evBoltNm,flash;
static short savedPri[2];

/* 0..15 from the pixel.  The ordered 4x4 matrix repeats its dots over a slow gradient and the
   eye finds the grid, so the thresholds here are hashed instead. */
static int hash2(int x,int y)
{unsigned int h=(unsigned int)x*374761393u+(unsigned int)y*668265263u;
 h=(h^(h>>13))*1274126177u;
 return (int)((h>>16)&15);
}

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
{int d=hash2(x,y)-8,r,g,b;
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

/* The sky's colours, read from the level's own panorama: zenith, horizon, and four luminance
   percentiles of everything above the horizon.  Its ridges are NOT taken: a mountain line that
   drifts with the wind reads as the mountains moving, and it cut the haze off from the rest. */
static void analyse(void)
{int hist[32][4],yb,xb,i,n,run,total,best=-1;
 int zs[4],hs[4];
 C3 c;
 for (i=0;i<32;i++)
    hist[i][0]=hist[i][1]=hist[i][2]=hist[i][3]=0;
 for (i=0;i<4;i++)
    zs[i]=hs[i]=0;
 for (yb=0;yb<256;yb++)
    {int sum=0,cnt=0;
     if (yb&3)
	continue;
     for (xb=H0+8;xb<H0+124;xb+=3)
	{panoPixel(yb,xb,&c);
	 i=lum(&c)>>3;
	 hist[i][0]+=c.r; hist[i][1]+=c.g; hist[i][2]+=c.b; hist[i][3]++;
	 sum+=lum(&c); cnt++;
	 if (xb>=H0+100)
	    {zs[0]+=c.r; zs[1]+=c.g; zs[2]+=c.b; zs[3]++;}
	 else if (xb<H0+24)
	    {hs[0]+=c.r; hs[1]+=c.g; hs[2]+=c.b; hs[3]++;}
	}
     if (cnt && sum/cnt>best)
	{best=sum/cnt;
	 brightAz=yb;
	}
    }
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
 /* a grey, dull sky is a stormy one */
 {int mx=cMid.r,mn=cMid.r,s;
  if (cMid.g>mx) mx=cMid.g;
  if (cMid.b>mx) mx=cMid.b;
  if (cMid.g<mn) mn=cMid.g;
  if (cMid.b<mn) mn=cMid.b;
  s=255-(mx-mn)*5;
  if (lum(&cMid)<90) s+=60;
  stormy=clamp8(s);
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

/* Every layer's shape comes from here.  A plain fbm's fine octaves read as grain -- what is
   wanted is fog: slow fields, warped by another slow one so the lattice never shows, with their
   own slow density on top, so some stretches are thick and others clear. */
static int soft(int x,int y,int o)
{int wx=x+((vnoise(x,y,6,o+29)-128)>>1),
     wy=y+((vnoise(x+211,y+37,6,o+53)-128)>>1);
 return (vnoise(wx,wy,7,o)*8+vnoise(wx,wy,6,o+61)*5+vnoise(wx,wy,5,o+122)*3)/16;
}

static int density(int x,int y,int o)
{return 64+((vnoise(x,y,7,o)*3)>>2);           /* 64..255: thick stretches and thin ones */
}

/* These fields are slow by construction, so they are sampled on a grid of one point every four
   pixels and read back between the points: the same picture for a sixteenth of the work.  It
   matters -- a band is 192 rows of 512 now, and the vault is built again when a player joins,
   with the game running. */
#define GSH 2
#define GW  ((512>>GSH)+1)
typedef struct {unsigned char a[GW],b[GW]; short yc,o,ys,dens;} Field;
static Field fShape,fDens;

static void fieldFill(Field *f,unsigned char *r,int yc)
{int i,y=(yc<<GSH)*f->ys;
 for (i=0;i<GW;i++)
    r[i]=(unsigned char)(f->dens? density((i<<GSH)&511,y,f->o): soft((i<<GSH)&511,y,f->o));
}

static void fieldStart(Field *f,int o,int ys,int dens)
{f->o=o; f->ys=ys; f->dens=dens; f->yc=0;
 fieldFill(f,f->a,0);
 fieldFill(f,f->b,1);
}

/* once per line, y growing */
static void fieldRow(Field *f,int y)
{int yc=y>>GSH;
 if (yc==f->yc)
    return;
 if (yc==f->yc+1)
    {int i;
     for (i=0;i<GW;i++)
	f->a[i]=f->b[i];
    }
 else
    fieldFill(f,f->a,yc);
 f->yc=yc;
 fieldFill(f,f->b,yc+1);
}

static int fieldAt(Field *f,int x,int y)
{int xi=x>>GSH,fx=x&((1<<GSH)-1),fy=y&((1<<GSH)-1),v0,v1;
 v0=f->a[xi]+(((f->a[xi+1]-f->a[xi])*fx)>>GSH);
 v1=f->b[xi]+(((f->b[xi+1]-f->b[xi])*fx)>>GSH);
 return v0+(((v1-v0)*fy)>>GSH);
}

/* ---- the vault: one texel per line and per pixel (a texel over two read as a coarse sky), a
   band as tall as its views.  Above the horizon: the gradient, with a slow brightening where the
   high haze thickens -- no cut-out cloud, that is NBG1's work.  Below: the gradient walked down
   to the fog's colour, so sky and fogged ground meet in the same tone.
   2 players: 192 rows, horizon 112.  3-4: 96 rows, horizon 56, copied to row 112. ---- */
static int vaultRows=96,vaultHor=56;

static void vaultBase(int y,C3 *c)
{static const C3 black={0,0,0};
 C3 deep;
 int t;
 if (y<vaultHor)
    mix(c,&cZen,&cHor,(y*y*256)/(vaultHor*vaultHor));
 else
    {mix(&deep,&cVeil,&black,128);
     t=((y-vaultHor)*256)/(vaultRows-vaultHor);
     mix(c,&cHor,&deep,(t*t)>>8);
    }
}

static void makeVault(int players)
{volatile Uint16 *v=VW(0);
 int x,y,n,a;
 C3 base,cc,tone;
 vaultRows=(players==2)? 192: 96;
 vaultHor=(players==2)? 112: 56;
 fieldStart(&fShape,0,1,0);
 fieldStart(&fDens,171,1,1);
 for (y=0;y<vaultRows;y++)
    {vaultBase(y,&base);
     mix(&tone,&base,&cLight,110);      /* what a thick stretch of high haze looks like */
     fieldRow(&fShape,y);
     fieldRow(&fDens,y);
     for (x=0;x<512;x++)
	{if (y<vaultHor)
	    {n=fieldAt(&fShape,x,y);    /* a whisper of high haze, not a cloud */
	     a=((n-112)*2*fieldAt(&fDens,x,y))>>8;
	     a=(a<0)? 0: (a>255)? 255: a;
	     mix(&cc,&base,&tone,a);
	    }
	 else
	    cc=base;
	 v[(y<<9)+x]=pack(&cc,x,y);
	}
    }
 if (players>2)
    for (x=0;x<VAULT_ROWS*512;x++)
       v[VAULT_BAND2*512+x]=v[x];
}

static void cloudPixel(int k,int x,int y,Uint16 p)
{VW(CLOUD_CHARS)[(k<<6)+(((y&7)<<3)|(x&7))]=p;
}

/* the clouds, 512x40 on NBG1: sheets rather than puffs -- a slow field under an envelope, with
   its own density, lit from above (bright where the sheet three lines up is thin), and kept
   near the sky's own tone so the layer does not stand out.  Over its last CLOUD_EDGE levels it
   takes the colour of the vault behind it AND thins out in a dithered pattern, so its edge is
   the layer behind showing through and not an outline; the layer itself is mixed with what is
   behind it as well (registers: N1CCEN).  The lit copy is the same mask pushed toward
   lightning's tint; only the bolts use it now. */
#define CLOUD_EDGE 96
static void makeClouds(void)
{unsigned char above[4][512];
 int x,y,n,a,t,k,env,thr=120-(stormy>>3);   /* soft() sits around 130: the cover of a sheet */
 C3 c,lit,bg;
 for (x=0;x<64;x++)
    {VW(CLOUD_CHARS)[x]=0;      /* the blank cell */
    }
 fieldStart(&fShape,97,2,0);
 fieldStart(&fDens,213,2,1);
 for (y=0;y<40;y++)
    {env=256-(((y-20)*(y-20))<<8)/440;
     /* the vault behind these lines.  One sheet serves both layouts, whose cloud bands do not
	sit at the same height, so this takes the middle of the two. */
     t=24+((y*3)>>2);
     mix(&bg,&cZen,&cHor,(t*t*256)/(56*56));
     fieldRow(&fShape,y);
     fieldRow(&fDens,y);
     for (x=0;x<512;x++)
	{k=((y>>3)<<6)+(x>>3);
	 n=fieldAt(&fShape,x,y);
	 a=(((n*env)>>8)-thr)*3;
	 a=(a*fieldAt(&fDens,x,y))>>8;
	 above[y&3][x]=clamp8(a);
	 if (a<CLOUD_EDGE && (a<<4)/CLOUD_EDGE<=hash2(x,y))
	    {cloudPixel(1+k,x,y,0);      /* the edge dissolves into what is behind it */
	     cloudPixel(CLOUD_LIT+k,x,y,0);
	     continue;
	    }
	 t=clamp8(200-(y>=3? above[(y-3)&3][x]: 0)+((n-128)>>1));
	 if (t<128)
	    mix(&c,&cMid,&cLight,t<<1);
	 else
	    mix(&c,&cLight,&cTop,(t-128)<<1);
	 mix(&c,&bg,&c,150);            /* toward the sky: little contrast with the vault */
	 if (a<CLOUD_EDGE)              /* and its edge is the vault's own colour */
	    mix(&c,&bg,&c,(a*256)/CLOUD_EDGE);
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

/* The mist, 512x56 on NBG0: the fog of the horizon, seen from inside it.  It thickens downward
   -- clear at the top of its band, full at the horizon line (y = 32) -- and nothing below, where
   the vault goes on fading by itself.  Where it is thin it paints the sky's own colour, so it
   dissolves into the vault instead of ending on a line; only the very top edge is dithered, and
   the layer is mixed with what is behind it (registers: N0CCEN, the rate the fog moves).  No
   ridge from the panorama any more: a mountain that drifts with the wind is not a mountain. */
static void makeMist(void)
{int x,y,k,d,a;
 C3 c,bg;
 for (x=0;x<64;x++)
    VW(HAZE_CHARS)[x]=0;
 fieldStart(&fShape,131,3,0);
 fieldStart(&fDens,19,1,1);
 for (y=0;y<56;y++)
    {mix(&bg,&cZen,&cHor,128+(y<<2));   /* the sky around the horizon: what `thin` means here */
     fieldRow(&fShape,y);
     fieldRow(&fDens,y);
     for (x=0;x<512;x++)
	{Uint16 p;
	 k=1+((y>>3)<<6)+(x>>3);
	 if (y>=32)
	    p=0;                        /* under the horizon: the vault, which fades on its own */
	 else
	    {d=(y*y*272)>>10;           /* 0 at the top of the band, 256 at the horizon */
	     a=(d*fieldAt(&fDens,x,y))>>8;
	     a+=((fieldAt(&fShape,x,y)-128)*3)>>3;
	     a=(a<0)? 0: (a>256)? 256: a;
	     if (a<24 && (a<<3)<=(hash2(x+7,y+3)<<4)+8)
		p=0;                    /* the last of it, dithered rather than cut */
	     else
		{mix(&c,&bg,&cVeil,a);
		 p=pack(&c,x,y);
		}
	    }
	 VW(HAZE_CHARS)[(k<<6)+(((y&7)<<3)|(x&7))]=p;
	}
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

/* The bolt's own 3x5 cells, swapped in over its columns; on=0 puts the plain ones back.  The
   ring of lit cells around it is gone: a cell is 8 pixels wide, so it lit square patches of
   white over layers that were not moving.  The sheet of light is the colour offset's work
   (mpSkyFrame): it brightens all three layers together, and fades. */
static void mapBolt(int band,int bolt,int on)
{int row=cloudRow[layout-2][band],r,c,cc,k;
 volatile Uint32 *cm=VL(CLOUD_MAP);
 Uint32 cb=CHARNM(CLOUD_CHARS,0);
 if (row<0 || bolt<0)
    return;
 for (r=0;r<5;r++)
    for (c=0;c<3;c++)
       {cc=(6+(bolt<<4)+c)&63;
	k=on? CLOUD_BOLT+bolt*15+r*3+c: 1+(r<<6)+cc;
	cm[((row+r)<<6)+cc]=cb+(k<<2);
       }
}

/* ---- the moon (VDP1, per view) ----
   A dark disc, high in the sky.  Not a character: a level's tiles take what the VDP1's
   character VRAM has (PIC.C initPicSystem), leaving no room to allocate one.  Two fans of
   triangles, round enough at this size, both drawn IN THE SKY'S OWN COLOUR at the line where
   the moon lands and darkened by gouraud -- the outer fan walks that darkening back to 16 (the
   sky untouched) at its rim, which is the few pixels of edge that fade into the sky.  The disc
   is not flat: each rim vertex carries its own level (moonRelief), and two quads are its seas,
   shaded corner to corner so no edge of them is ever drawn.  No mesh: a halftone read as dirt.
   Its colour is the sky's OWN, offset B included (mpSkyFrame lastB), so the fog that dims the
   three layers dims the moon with them instead of leaving it hanging in front. ---- */
#define MOON_SEGS 12
#define MOON_R    6                     /* the disc; its edge is MOON_EDGE pixels around it */
#define MOON_EDGE 3
#define MOON_DARK 12                    /* the disc's gouraud level; 16 is the sky untouched */
#define MOON_SEA  4                     /* a sea, that much darker again */
/* GCC14: the same disc, lit the other way (SPRITE.H CFG_SKY_SUN).  A moon is DARKER than the
   sky it sits in and carries its seas and an uneven ground; a sun is BRIGHTER and perfectly
   smooth -- above 16 the gouraud brightens instead of darkening, so one constant and two
   omissions turn the one into the other, and the fog still takes it down with the sky. */
#if CFG_SKY_SUN
#define DISC_LEVEL  26                  /* brighter than the sky: a sun */
#define DISC_RELIEF NULL                /* ... with no ground to speak of */
#else
#define DISC_LEVEL  MOON_DARK
#define DISC_RELIEF moonRelief
#endif
static int moonOk;
static Fixed32 moonDir[3];
static short skyBB[MPMAX][4];
static char skyBBok[MPMAX];
/* 12 unit vectors, 256 = 1 */
static const short moonUnit[MOON_SEGS+1][2]=
   {{256,0},{221,128},{128,221},{0,256},{-128,221},{-221,128},{-256,0},
    {-221,-128},{-128,-221},{0,-256},{128,-221},{221,-128},{256,0}};
/* the disc's ground, one level per rim vertex (the ends are the same vertex) */
static const signed char moonRelief[MOON_SEGS+1]=
   {0,-2,-1,1,2,1,-1,-2,-1,1,2,1,0};
/* two seas, in sixteenths of the radius: centre x,y then half width, half height */
static const signed char moonSeaBox[2][4]={{-5,-5,7,5},{5,4,5,4}};

static void makeMoon(void)
{int i;
 static const Fixed32 sin16[16]={0,25080,46341,60547,65536,60547,46341,25080,
				 0,-25080,-46341,-60547,-65536,-60547,-46341,-25080};
 /* where: the panorama's brightest azimuth, one of its four turns, 32 degrees up */
 i=((brightAz>>6)+((rnd()&3)<<2))&15;
 moonDir[0]=MTH_Mul(sin16[i],55706);
 moonDir[1]=34734;
 moonDir[2]=MTH_Mul(sin16[(i+4)&15],55706);
 moonOk=1;
}

#define MOONMODE (UCLPIN_ENABLE|ECDSPD_DISABLE|DRAW_GOURAU|COLOR_5)

static int moonLevel(int v)
{return (v<0)? 0: (v>31)? 31: v;
}

/* a fan of triangles around (cx,cy): gouraud `mid` at the centre, `edge` at the rim (16 = the
   colour as given, less is darker).  `relief` adds a level per rim vertex, so two neighbouring
   wedges share theirs and the ground is uneven without a facet anywhere; NULL = a plain rim. */
static void moonFan(int cx,int cy,int r,Uint16 color,int mid,int edge,
		    const signed char *relief)
{struct gourTable g;
 XyInt q[4];
 int i;
 g.entry[0]=greyTable[mid];
 g.entry[3]=greyTable[mid];
 q[0].x=cx; q[0].y=cy;
 q[3].x=cx; q[3].y=cy;
 for (i=0;i<MOON_SEGS;i++)
    {g.entry[1]=greyTable[moonLevel(edge+(relief? relief[i]: 0))];
     g.entry[2]=greyTable[moonLevel(edge+(relief? relief[i+1]: 0))];
     q[1].x=cx+((moonUnit[i][0]*r)>>8);   q[1].y=cy+((moonUnit[i][1]*r)>>8);
     q[2].x=cx+((moonUnit[i+1][0]*r)>>8); q[2].y=cy+((moonUnit[i+1][1]*r)>>8);
     EZ_polygon(MOONMODE,color,q,&g);
    }
}

/* the seas: a quad each, darkest at one corner and back to the disc's own level at the opposite
   one, so what is drawn is a gradient inside the disc and never an outline */
static void moonSeas(int cx,int cy,int r,Uint16 color,int disc)
{struct gourTable g;
 XyInt q[4];
 int i,x,y,w,h,d,m;
 d=moonLevel(disc-MOON_SEA);
 m=(d+disc)>>1;
 g.entry[0]=greyTable[d];
 g.entry[1]=greyTable[m];
 g.entry[2]=greyTable[disc];
 g.entry[3]=greyTable[m];
 for (i=0;i<2;i++)
    {x=cx+((moonSeaBox[i][0]*r)>>4); y=cy+((moonSeaBox[i][1]*r)>>4);
     w=(moonSeaBox[i][2]*r)>>4;      h=(moonSeaBox[i][3]*r)>>4;
     if (w<1) w=1;
     if (h<1) h=1;
     q[0].x=x-w; q[0].y=y-h;
     q[1].x=x+w; q[1].y=y-h;
     q[2].x=x+w; q[2].y=y+h;
     q[3].x=x-w; q[3].y=y+h;
     EZ_polygon(MOONMODE,color,q,&g);
    }
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
 r->zoom.x=F(1);                      /* one texel a pixel: at two, the sky read as coarse */
 r->zoom.y=F(1);                      /* ... and a band holds one row per line (makeVault) */
 (void)players;
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
 SCL_SET_N1CCEN(1);             /* the clouds are mixed in too: one layer less to pick out */
 SCL_SET_R0CCEN(0);
 SCL_SetColMixRate(SCL_NBG1,16);   /* half of it: the clouds are a veil, not a picture */
 ccRate=-1;
 lastB[0]=lastB[1]=lastB[2]=1000;
}

/* ---- entry points ---- */
/* GCC14: the sky owns VDP2 VRAM B0-B1.  Doom leaves them empty; PowerSlave puts its weapon
   sheet there, and the sky may only have them once nothing reads the sheet any more -- that is,
   once PIC.C has cut the weapon into VDP1 tiles (picWeaponSprites). */
static int skyAllowed(void)
{return vdp2PicCount()==0 || weaponSpritesOn;
}

void mpSkyPlayers(int players)
{int i;
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
     layout=0;                  /* a new level's sky: the vault is built again below */
     Scl_s_reg.dispenbl&=~0x0013;
     /* now, not at the next image: solo's sky is on screen and its K table is about to go */
     *(volatile Uint16 *)0x25f80020=Scl_s_reg.dispenbl;
     analyse();                 /* reads the panorama: before the vault overwrites it */
     makeMist();
     makeClouds();
     makeBolts();
     for (i=0;i<64*64;i++)
	VL(CLOUD_MAP)[i]=VL(HAZE_MAP)[i]=0;
     makeMoon();
     windDir=(rnd()&1)? 1: -1;
     windDeck=windCloud=windHaze=0;
     fogDim=0;
     now=0;
     evType=0; flash=0;
     evNext=240+rnd()%240;
     gustEnd=0;
     for (i=0;i<MPMAX;i++)
	skyBBok[i]=0;
     registers();
     mpSkyOn=1;
    }
 if (layout!=players)
    {layout=players;
     makeVault(players);        /* the band is as tall as the views of this layout */
    }
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
 moonOk=0;
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
/* One ramp up, one long fall.  It used to flicker in three steps, which with the lit cells read
   as squares blinking; a bolt is brighter and lasts a little longer than a sheet. */
static int flashCurve(int t,int bolt)
{int peak=bolt? 230: 140,n=bolt? 26: 20;
 if (t>=n)
    return 0;
 if (t<2)
    return (peak*(t+1))>>1;
 return (peak*(n-t))/(n-2);
}

static void events(int ticks)
{if (!evType && now>=evNext)
    {int r=rnd()%100;
     /* GCC14: a game without storms keeps only the gust -- no bolt, no sheet of lightning
        (SPRITE.H CFG_SKY_STORM).  Egypt at noon does not flicker. */
     evType=CFG_SKY_STORM? (r<50? 1: r<85? 2: 3): 3;
     evT=0;
     evBand=layout>2? rnd()&1: 0;
     if (evType==1)
	{evBoltNm=rnd()&3;
	 mapBolt(evBand,evBoltNm,1);
	}
     else if (evType==2)
	evBoltNm=-1;                      /* a sheet: light alone, nothing swapped */
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
	mapBolt(evBand,evBoltNm,0);       /* the bolt is gone; its light goes on fading */
     if (evT>=26)
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
{int t,g=256,rate,i,b[3];
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
 /* The fog: 4096 = none, 512 = the thickest.  Nothing MOVES with it -- a sky sliding up and
    down as the budget breathed read as the sky itself moving.  It thickens the haze and pulls
    the three layers towards black, which is the fog passing in front of the sky. */
 t=fog>=4096? 0: fog<=512? 256: ((4096-fog)<<8)/3584;
 fogDim+=(t-fogDim)>>3;         /* and it closes in as smoothly as the budget's own fog */
 Scl_n_reg.n0_move_y=0;
 rate=20-((fogDim*17)>>8);      /* a breath of mist with no fog, nearly solid at its worst */
 if (rate!=ccRate)
    {SCL_SetColMixRate(SCL_NBG0,rate);
     ccRate=rate;
    }
 /* offset B: A (the damage flash, the fades), less the fog, plus the lightning in its tint */
 b[0]=offA[0]+((cTint.r*flash)>>9);
 b[1]=offA[1]+((cTint.g*flash)>>9);
 b[2]=offA[2]+((cTint.b*flash)>>9);
 for (i=0;i<3;i++)
    {b[i]-=(fogDim*150)>>8;
     if (b[i]>255) b[i]=255;
     if (b[i]<-255) b[i]=-255;
    }
 if (b[0]!=lastB[0] || b[1]!=lastB[1] || b[2]!=lastB[2])
    {SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0|SCL_NBG1|SCL_RBG0,b[0],b[1],b[2]);
     lastB[0]=b[0]; lastB[1]=b[1]; lastB[2]=b[2];
    }
 if (SclProcess==0)
    SclProcess=1;
}

/* the view's sky box (WALLS.C plaxBB, local), for its next moon */
void mpSkyViewDone(int view)
{if (!mpSkyOn)
    return;
 skyBBok[view]=plaxBBxmin<plaxBBxmax && plaxBBymin<plaxBBymax;
 skyBB[view][0]=plaxBBxmin; skyBB[view][1]=plaxBBymin;
 skyBB[view][2]=plaxBBxmax; skyBB[view][3]=plaxBBymax;
}

/* Emitted first in the view's list, so every wall and thing covers it; clipped to where the
   view saw sky in its last image, so the fog's cut-off holes never show it.  p is already in
   the view's local coordinates (mpSetViewport), as the fans are. */
void mpSkyMoon(int view,MthMatrix *m)
{MthXyz w,t;
 XyInt p,r[2];
 if (!mpSkyOn || !moonOk || !skyBBok[view])
    return;
 w.x=camera->pos.x+(moonDir[0]<<11);
 w.y=camera->pos.y+(moonDir[1]<<11);
 w.z=camera->pos.z+(moonDir[2]<<11);
 MTH_CoordTrans(m,&w,&t);
 if (t.z<F(64))
    return;
 project_point(&t,&p);
 if (p.x+16<skyBB[view][0] || p.x-16>skyBB[view][2] ||
     p.y+16<skyBB[view][1] || p.y-16>skyBB[view][3])
    return;
 r[0].x=viewCx+skyBB[view][0]; r[0].y=viewCy+skyBB[view][1];
 r[1].x=viewCx+skyBB[view][2]; r[1].y=viewCy+skyBB[view][3];
 if (r[1].x>viewCx+viewXmax-1) r[1].x=viewCx+viewXmax-1;
 if (r[1].y>viewCy+viewYmax-1) r[1].y=viewCy+viewYmax-1;
 EZ_userClip(r);
 /* the sky's own colour at the line it lands on, offset B and all, darkened; the outer fan
    walks that darkening back to nothing at its rim, so the moon ends in the sky and not on an
    edge, and the fog takes the moon down with the rest of the sky */
 {int line=viewCy+p.y;
  C3 sky;
  Uint16 c;
  if (layout>2 && line>=112)
     line-=112;
  if (line<0) line=0;
  if (line>=vaultRows) line=vaultRows-1;
  vaultBase(line,&sky);
  sky.r=clamp8(sky.r+lastB[0]);
  sky.g=clamp8(sky.g+lastB[1]);
  sky.b=clamp8(sky.b+lastB[2]);
  c=pack(&sky,0,0);
  moonFan(p.x,p.y,MOON_R+MOON_EDGE,c,DISC_LEVEL,16,NULL);
  moonFan(p.x,p.y,MOON_R,c,DISC_LEVEL,DISC_LEVEL,DISC_RELIEF);
  if (!CFG_SKY_SUN)
     moonSeas(p.x,p.y,MOON_R,c,DISC_LEVEL);
 }
 r[0].x=viewCx+viewXmin;   r[0].y=viewCy+viewYmin;
 r[1].x=viewCx+viewXmax-1; r[1].y=viewCy+viewYmax-1;
 EZ_userClip(r);
}
