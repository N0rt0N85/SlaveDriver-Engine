#ifndef __INCLUDEDhwfloorh
#define __INCLUDEDhwfloorh

/* GCC14: the dominant floor on VDP2 -- see HWFLOOR.C for what is elected and why. */

#define HWFLOOR_MAXPLANES  128      /* distinct (height, tile) couples a level may have       */
#define HWFLOOR_LINES      240      /* coefficient table: one longword a raster line          */
#define HWFLOOR_NEAR       2        /* lines under the horizon before the floor may show      */
#define HWFLOOR_FOCAL      160      /* the projection's focal, in dots (WALLS.C rectTransform)*/

extern int hwFloorOn;                            /* a plane is elected in this image          */
extern int hwFloorPlane;                         /* which one, -1 = none                      */
extern int hwFloorCells;                         /* cells it took off the VDP1 (overlay)      */
extern unsigned char hwFloorSector[];            /* this sector's floor is the plane's        */
extern short hwFloorBBxmin,hwFloorBBymin,hwFloorBBxmax,hwFloorBBymax;

void hwFloorLevelStart(void);                    /* after the loading screen: VRAM and tables */
void hwFloorOff(void);
void hwFloorBegin(void);                         /* a new image: empty the tallies            */
void hwFloorSee(int sectorNm,int w,int h);       /* ... this sector is in it, that big        */
void hwFloorChoose(int viewSector);
void hwFloorQuad(int x0,int y0,int x1,int y1);   /* a dropped quad's screen box       */                        /* ... and this is the plane                 */
int hwFloorPlaneCount(void);            /* the level's distinct planes (overlay)      */
int hwFloorY(void);                      /* the held plane's world height (overlay)    */
void hwFloorFrame(Fixed32 yaw,int camx,int camy,int camz,int horizon);

#endif
