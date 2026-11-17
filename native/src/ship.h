/* ============================================================
   The ship, as the native build holds it.

   Everything the browser kept in JS object literals is a flat
   struct array here.  The one rule carried over intact: the
   BAKE ONLY SEES BRUSHES.  Part triangles are invisible to the
   radiosity gather, so a lit part must also stamp a real brush
   (see kitEmit) or it glows and lights nothing.
   ============================================================ */
#ifndef SHIP_H
#define SHIP_H
#include "m.h"

/* ---- material ids -- section 1 of map.html, value for value -------- */
enum {
  T_FLOOR=0, T_WALL, T_CEIL, T_PANEL, T_HAZARD, T_RUST, T_VENT, T_TILE,
  T_GRATE, T_CONSOLE, T_PAINT, T_CRATE, T_SCREEN, T_DARK, T_PIPE, T_LAMP,
  T_RIB, T_GLOW, T_COLD, T_TRIM,
  T_MACH, T_SKIN, T_COAT, T_GAP, T_CHAN, T_DUCT, T_CU, T_MARK,
  T_LOUV, T_PERF, T_ACC, T_READ, T_SAFE,
  T_BDECK, T_BPLATE, T_BHULL, T_BPAN,
  T_SKY, T_ORB, T_GLAS,
  TL                                       /* 40 materials, 40 layers */
};

/* surface class -- decides the draw range and how the shader lights it */
enum { SURF_ROOM=0, SURF_GLASS=1, SURF_SKY=2, SURF_SPRITE=3 };

#define TEX_SCALE  0.8333333333333334f     /* uv = worldPos * this */
#define AO_STEP    1.4f                    /* metres between lightmap samples */
#define VSTRIDE    24                      /* floats per scene vertex */

typedef struct { float x0,y0,z0,x1,y1,z1; unsigned char t,clip; } Brush;
typedef struct { float c[4][3]; float uo,wo; unsigned char t; } Face;
typedef struct { float p[3],c[3],r; unsigned char noFix; } Light;
typedef struct { const char *n,*s; float x0,x1,z0,z1,c[3]; } Zone;
typedef struct { float x,z,w,d; unsigned char ax; } DoorSlot;

/* ---- level.c ------------------------------------------------------- */
extern Brush *BRUSHES;  extern int nBrush;
extern Face  *FACES;    extern int nFace;
extern Light *LIGHTS;   extern int nLight;
extern Zone  *ZONES;    extern int nZone;
extern DoorSlot *DOORS; extern int nDoor;
extern const float *VOID_CUBE;

void  levelInit(void);                        /* plan_data.h -> the arrays */
int   addBrush(float x0,float y0,float z0,float x1,float y1,float z1,
               int t,int clip);
void  addQuad(const float *c0,const float *c1,const float *c2,const float *c3,
              int t,float uo,float wo);
int   zoneAt(float x,float z);
int   solidAt(float x,float y,float z);       /* inside any clip/solid brush */
/* what a person would be standing on here.  Not the first surface under the
   ceiling -- the first STANDABLE one, nearest the deck you name. */
int   deckNear(float x,float z,float want,float *y);
int   deckAt(float x,float z,float *y);

/* ---- geom.c -------------------------------------------------------- */
extern float *MESHV; extern int nMeshVert;    /* VSTRIDE floats per vertex */
extern int MESH_SPLIT[2];                     /* opaque | sprite | glass */
void  buildMesh(void);
void  uvFrame(const float *n,float *T,float *B);

/* ---- tris.c: the kit's own triangles ------------------------------- */
/* pos(3) nrm(3) uv(2) tex(1) -- 9 floats per VERTEX, appended by kit.c.
   Counting vertices rather than triangles is not pedantry: mEmit hands back
   a vertex stream and every off-by-three here is a torn part. */
extern float *TRIS; extern int nTriVert;
void  trisPush(const float *v9);              /* one vertex */
void  trisReserve(int verts);

/* ---- bake.c -------------------------------------------------------- */
/* sty/styG: the LIGHTSTYLE channel, which was baked as zero and unused
   until the ship needed to lose its lighting a bank at a time.  A lamp is
   either steady -- its light lands in lm[] and is fixed forever -- or it
   belongs to one of STY_N switchable groups, and then its light lands in
   sty[] instead and styG says whose.  One triple, so a sample carries the
   DOMINANT group's light and nothing else's; lamps are local enough that
   the second-brightest group at a given texel is nearly always nothing.
   Bit 3 of styG means "this face's own EMISSIVE belongs to that group too"
   -- it is a lamp fixture, and a lamp that is off has a dark lens. */
/* SIX, AND THREE OF THEM FLICKER.  Four banks put every broken fixture
   in the ship on ONE schedule, so the whole ship stuttered in unison --
   which reads as the renderer having a fault rather than as the ship
   having broken tubes in it.  Three flicker banks on three clocks is
   what makes it happen in PLACES.  render.c packs the index in three
   bits (`sgi & 7`), so seven was always the ceiling and four was a
   choice, and seven is now what it is: bank 6 is the one that goes out
   for the LAST STAND.  Everything above is the ship losing power a
   compartment at a time over an hour; sink 3 is ninety seconds long and
   is meant to be the darkest the game gets, and no amount of ambient
   grading can make a room dark while half its ceiling strips are still
   lit.  So a fifth of the ship's lamps are on a breaker that only that
   moment throws.  It costs nothing before then: bank 6 is 1.0 for the
   whole rest of the run. */
#define STY_N 7
typedef struct { float ao; float lm[9]; float sty[3]; float styG; } BakeSample;
extern float STY_C[STY_N*3];    /* tint x level; (1,1,1) while a group is on */
int  lampStyle(int i);          /* -1 steady, else 0..STY_N-1 */
void  bakeInit(void);
void  bakeAt(const float *p,const float *n,int bi,BakeSample *out);
void  solveCubes(int bounces);
extern float *CUBES; extern int CUBE_COUNT;   /* 6 vec3 per zone */

/* the specular half of the same solution: one octahedral radiance map per
   zone, per roughness level, bordered so bilinear crosses the seam */
#define PROBE_LEV 6                           /* roughness 0, .2, .4, .6, .8, 1 */
void  solveProbes(void);
extern float *PROBES;                         /* CUBE_COUNT*PROBE_LEV planes */
extern int    PROBE_N, PROBE_E;               /* inner edge, and edge+border */

/* ---- tex.c --------------------------------------------------------- */
#define TN 256                                 /* texture edge */
extern unsigned char *ALB_DATA, *NRM_DATA;     /* TN*TN*4*TL each */
extern float MAT_METAL[TL];
extern int   MAT_SURF[TL];
void  texCompileAll(void);
void  texCompileLayer(int layer);

/* ---- kit.c --------------------------------------------------------- */
void  kitBuildModules(void);
extern int KIT_PLACED;      /* how many of the plan's modules stood up */

/* ---- render.c ------------------------------------------------------ */
struct FrameState;
int   renderInit(void);
void  renderUpload(void);
void  renderBeginScene(const struct FrameState *F);
void  renderGlass(void);                      /* before the viewmodel */
void  renderEndScene(const struct FrameState *F);
extern int  R_DEBUG;
extern int  R_FLASH;
/* THE NEAREST STATIC LAMPS, PICKED ONCE A FRAME AND SHARED.  The world
   shader and the entity shader both want per-pixel highlights from the
   ship's own 257 lamps and both want the same twelve, so the pick happens
   in renderBeginScene -- which the frame runs first -- and entDraw reads
   what it left.  xyz+radius in SL_P, colour times the bank's flicker in
   SL_C.  See render.c's MAXSL for why there is a cap at all. */
#define MAXSL 16
extern float SL_P[MAXSL*4], SL_C[MAXSL*3];
extern int   SL_N;
int  renderPickLamps(const float *camPos);
/* the two lookups the world shader owns, lent to the viewmodel and the
   entities so the gun and the crew reflect the room the walls reflect */
unsigned renderProbeTex(void);                /* GLuint, without gl33.h */
unsigned renderDFGTex(void);

/* ---- shared switches ----------------------------------------------- */
extern int FASTLIGHT;                          /* -fast: a ninth of the samples */

#endif
