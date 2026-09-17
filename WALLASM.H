void rectTransform(Fixed32 wx,Fixed32 wy,Fixed32 wz,
		   int light,int h,int w,
		   Fixed32 px,Fixed32 py,Fixed32 pz,
		   Fixed32 hx,Fixed32 hy,Fixed32 hz,
		   void *output,
		   unsigned short (*lightFunc)(char vLightIndex,
					       MthXyz *pos),
		   int lightStepExtra,   /* GCC14: extra bytes after each light byte */
		   int lightRowExtra,    /* GCC14: extra bytes after each vertex row */
		   Fixed32 nearFloor);   /* GCC14: z floor of the projection, per wall */

void normTransform(sVertexType *firstVertex,MthMatrix *viewMatrix,
		   int nmVert,void *output,
		   unsigned short (*lightFunc)(char vLightIndex,
					       MthXyz *pos),
		   Fixed32 nearFloor);   /* GCC14: z floor of the projection, per wall */

/* SONDE -- ne modifie rien.  Les sommets de commande VDP1 tiennent sur 11 bits signes,
   -1024..1023, et au-dela "operation cannot be guaranteed" (HW_VDP1.md:184). */
static __inline__ XyInt *probeVDP1(XyInt *poly)
{int i,a;
 for (i=0;i<4;i++)
    {a=poly[i].x; if (a<0) a=-a;
     if (a>vdp1MaxX) vdp1MaxX=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
     a=poly[i].y; if (a<0) a=-a;
     if (a>vdp1MaxY) vdp1MaxY=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
    }
 return poly;
}

#define VDP1DIAG 0      /* 1 = disque de diagnostic : peint les cellules hors plage */
#define VDP1LIM  1023
#define VCLIPONE 1024   /* fractions de coupe en 1/1024 : (30000<<10) tient dans un int */

/* Emission d'une cellule de mur avec FENETRAGE EN V du motif quand le quad sort de la plage
   verticale du VDP1.

   L'axe V du motif court de l'arete (poly[0],poly[1]) vers l'arete (poly[3],poly[2]) -- vrai
   quelle que soit la permutation d'orientation de pattern[], qui a deja range les coins dans
   ces emplacements.  On coupe chaque arete verticale la ou elle entre dans la plage et on garde
   l'INTERSECTION des deux intervalles : conservateur, donc le resultat n'est jamais hors plage.

   FACE a un mur, les deux aretes sont a la meme profondeur, donnent la meme fraction, et la
   coupe est EXACTE -- c'est le cas ou il ne faut surtout rien degrader.  De biais elle est
   approchee (les deux aretes se coupent a des fractions differentes et on prend la plus
   serree), au prix d'un leger manque de texture sur une arete.

   LIMITES CONNUES : (1) l'axe U n'est PAS fenetrable -- pas de registre de pas, un
   sous-rectangle cisaille (HW_VDP1.md:160) ; un debordement horizontal reste donc non traite
   ici.  (2) la table gouraud n'est pas re-interpolee sur le quad coupe : l'eclairage des coins
   est celui des coins d'origine. */
static __inline__ void EZ_distSprVClip(short charNm,XyInt *poly,
				       struct gourTable *g)
{int i,a,over=0,t0,t1;
 XyInt src[4];
 for (i=0;i<4;i++)
    {a=poly[i].x; if (a<0) a=-a;
     if (a>vdp1MaxX) vdp1MaxX=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
     a=poly[i].y; if (a<0) a=-a;
     if (a>vdp1MaxY) vdp1MaxY=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
     if (a>VDP1LIM) over=1;
    }
 if (!over)
    {EZ_specialDistSpr2(charNm,poly,g);
     return;
    }
#if VDP1DIAG
 /* DISQUE DE DIAGNOSTIC -- ne corrige rien, MONTRE.  Toute cellule dont un sommet sort de la
    plage VDP1 est peinte a plat, bornee a la plage, au lieu d'etre texturee :
       ROUGE   debordement en X seulement
       BLEU    debordement en Y seulement
       MAGENTA les deux
    Un coup d'oeil donne l'axe, l'endroit et l'etendue -- ce qu'aucun compteur ne donne. */
 {int j,cx=0,cy=0;
  for (j=0;j<4;j++)
     {int a;
      a=poly[j].x; if (a<0) a=-a; if (a>VDP1LIM) cx=1;
      a=poly[j].y; if (a<0) a=-a; if (a>VDP1LIM) cy=1;
      if (poly[j].x> VDP1LIM) poly[j].x= VDP1LIM;
      if (poly[j].x<-VDP1LIM) poly[j].x=-VDP1LIM;
      if (poly[j].y> VDP1LIM) poly[j].y= VDP1LIM;
      if (poly[j].y<-VDP1LIM) poly[j].y=-VDP1LIM;
     }
  EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,
	     (cx&&cy)? RGB(31,0,31): (cx? RGB(31,0,0): RGB(0,0,31)),
	     poly,NULL);
  return;
 }
#endif
 t0=0; t1=VCLIPONE;
 for (i=0;i<2;i++)
    {int ya=poly[i].y,yb=poly[3-i].y,d=yb-ya,lo,hi,sp,sm;
     if (d==0)
	{if (ya>VDP1LIM || ya<-VDP1LIM)
	    {/* arete entierement hors plage a hauteur constante : rien a fenetrer en V,
		c'est un debordement horizontal.  Emettre tel quel, comme avant. */
	     EZ_specialDistSpr2(charNm,poly,g);
	     return;
	    }
	 continue;
	}
     sp=(( VDP1LIM-ya)<<10)/d;
     sm=((-VDP1LIM-ya)<<10)/d;
     lo=sp<sm? sp: sm;
     hi=sp<sm? sm: sp;
     if (lo>t0) t0=lo;
     if (hi<t1) t1=hi;
    }
 if (t0<0) t0=0;
 if (t1>VCLIPONE) t1=VCLIPONE;
 if (t1-t0<=0)
    return;   /* rien de representable : la cellule est a plus de 3 hauteurs d'ecran, hors champ */
 for (i=0;i<4;i++)
    src[i]=poly[i];
 for (i=0;i<2;i++)
    {int j=3-i;
     poly[i].x=src[i].x+(((src[j].x-src[i].x)*t0)>>10);
     poly[i].y=src[i].y+(((src[j].y-src[i].y)*t0)>>10);
     poly[j].x=src[i].x+(((src[j].x-src[i].x)*t1)>>10);
     poly[j].y=src[i].y+(((src[j].y-src[i].y)*t1)>>10);
    }
 EZ_specialDistSpr2V(charNm,t0,t1,poly,g);
}
