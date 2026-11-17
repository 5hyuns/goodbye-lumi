/* ============================================================
   THE PROP BENCH'S OWN BUILDERS, AS A HEADER.

   props/props_v02/ is the bench handing over a GENERATOR rather
   than a page of numbers -- propkit.c is propkit.js ported
   builder for builder, and props_gen.c is the 37 props as 703
   recorded calls into it.  Neither shipped with a header,
   because the exporter never needed one; this is it.

   THE CONTRACT IS A FACE LIST, NOT A MESH.  pkBuildProp(i)
   fills `pk` with quads -- four corners, a face normal, and
   optionally four vertex normals where the builder wanted a
   smooth shade -- and a REGION per face carrying what the
   bench's bake shader would have painted on it.  props.c turns
   the regions into this engine's own materials and the quads
   into instanced ranges; nothing here knows about GL.

   Sizes are per PROP, not for the whole kit: pkBegin() resets
   the buffers and the caller consumes them before the next
   build.  The largest prop in the kit is the Gas Cylinder Rack
   at 1,363 faces, so 4,096 is a factor of three of headroom and
   PK_MAX_FACES is the only number here that has to grow if the
   bench adds a bigger one.
   ============================================================ */
#ifndef PROPKIT_H
#define PROPKIT_H

/* ---- the surface generators of propkit.js, by their own numbers ------
   9 was K_FOLIAGE; the hydroponics deck was cut and the slot stayed
   empty rather than renumbering every recorded call below it. */
enum { PK_PANEL=0, PK_RIB=1, PK_SCREEN=2, PK_BUTTONS=3, PK_HAZARD=4,
       PK_VALVE=5, PK_SEAM=6, PK_PLAIN=7, PK_PIPE=8, PK_PERF=10 };

/* ---- the accent roles.  A role, not a colour: the bench decides what
   ORANGE and AMBER actually are, and so does props.c. */
enum { PK_A_NONE=0, PK_A_OCHRE=1, PK_A_AMBER=2, PK_A_LIVE_C=3,
       PK_A_LIVE_G=4, PK_A_ALARM=5 };

/* ---- how a ring band is shaded -------------------------------------- */
enum { PK_FLAT=0, PK_SMOOTH=1, PK_BEVELMODE=2, PK_NVFN=3 };

#define PK_BEVEL      0.014f     /* the kit's default corner cut */
#define PK_ROUND_SEG  3          /* segments in one rounded corner */
#define PK_CAP_ROLL   0.009f     /* the roll a lofted crown gets for free */

#define PK_MAX_FACES   4096
#define PK_MAX_REGIONS 4096
#define PK_MAX_RING      64
#define PK_MAX_SOCKETS   16

extern const float PK_AX[3], PK_AY[3], PK_AZ[3];
extern const float PK_NX[3], PK_NY[3], PK_NZ[3];

/* ---- what a builder says about one face ----------------------------- */
typedef struct {
  int   kind;                 /* -1: whatever the caller's default is */
  float seed;
  int   accent;
  const float *tint;          /* 0: bare machined alloy */
  int   paint;                /* a dielectric coat over it */
  int   rim;                  /* the region gets a bevelled border */
  int   share;                /* pool with an identical region (default 1) */
  int   tile;                 /* one small region, tiled -- trim, mitres */
  float chamfer;              /* -1: pkPanel's own 14 mm */
  const float (*nv)[3];       /* four vertex normals, or 0 */
  int   skip;                 /* the face is not built at all */
  int   hasParams;
  float params[4];            /* pitch, radius, count -- per kind */
} PkOpts;

PkOpts pkOpts(void);

/* ---- and what comes out --------------------------------------------- */
typedef struct {
  float w,h;                  /* the face's real size, in metres */
  float seed;
  float params[4];
  float tint[3];              /* -1,-1,-1 when the region had none */
  unsigned char kind, accent, paint, rim;
} PkRegion;

typedef struct {
  float p[4][3];
  float n[3];
  float nv[4][3];
  int   hasNv;
  int   ri;                   /* index into pk.reg */
} PkFace;

typedef struct { char name[16]; float p[3], d[3]; } PkSocket;

typedef struct {
  PkFace   face[PK_MAX_FACES];   int nFace;
  PkRegion reg [PK_MAX_REGIONS]; int nReg;
  int      shareKey[PK_MAX_REGIONS], tileKey[PK_MAX_REGIONS];
  int      mitre;
  PkSocket sock[PK_MAX_SOCKETS]; int nSock;
} PkBuild;
extern PkBuild pk;

void pkBegin(void);
void pkSocket(const char *name,const float p[3],const float d[3]);

/* ---- the primitives -------------------------------------------------
   Every one of these takes world coordinates: the kit has no parent
   frames, no local transforms and no instancing, so a repeat is a loop that computes coordinates and a
   mirror is a sign. */
typedef void (*PkNvFn)(int k,const float (*faceN)[3],int n,
                       float out[4][3],int *used,void *ud);

void pkFace4(const float p0[3],const float p1[3],const float p2[3],
             const float p3[3],const PkOpts *o);

void pkRectRing(float cx,float cz,float hw,float hd,float y,float out[4][3]);
void pkOctFoot (float cx,float cz,float hw,float hd,float c,float y,
                float out[8][3]);
int  pkRingAt  (const float c[3],const float U[3],const float V[3],
                float r,int n,float phase,float out[][3]);
void pkRevRing (const float in[][3],int n,float out[][3]);
void pkInsetRing(const float R[4][3],float c,float out[4][3]);
int  pkCutRing (const float R[4][3],float c,float out[8][3]);
int  pkRoundRing(const float R[4][3],float r,int seg,
                 float outP[][3],float outN[][3],int card[4]);

void pkHullRingF(const float A[][3],const float B[][3],int n,
                 const PkOpts *(*optFn)(int,void*),void *ud,
                 int smooth,PkNvFn nvFn,void *nvUd);
void pkHullRing (const float A[][3],const float B[][3],int n,
                 const PkOpts *o,int smooth);
void pkCapPoly  (const float ring[][3],int n,const PkOpts *o);
void pkCapRound (const float R[4][3],float r,int seg,
                 const PkOpts *cap,const PkOpts *trim);

void pkPanel(const float O[3],const float U[3],const float V[3],
             const float N[3],float w,float h,const PkOpts *o);
void pkBoxSides(float cx,float cy,float cz,float w,float h,float d,
                const PkOpts *px,const PkOpts *nx,const PkOpts *py,
                const PkOpts *ny,const PkOpts *pz,const PkOpts *nz);
void pkPlainBox(float cx,float cy,float cz,float w,float h,float d,
                float seed,const float *tint,int paint);

/* loft: a four-point ring to a four-point ring, with corner fillets and a
   rolled crown.  NOT a lathe -- pkStackDraw is the lathe. */
typedef struct {
  const PkOpts *px,*nx,*py,*ny,*pz,*nz;
  const PkOpts *diag,*trim,*topOpts,*topOptsAlt;
  float cut, topRound, topCut;
  int   seg, topSeg, facet;
} PkLoft;
PkLoft pkLoft0(void);
void pkLoft  (const float B[4][3],const float T[4][3],const PkLoft *S);
void pkOctBox(float cx,float cy,float cz,float w,float h,float d,
              float cut,const PkLoft *S);
void pkHull8 (const float B[4][3],const float T[4][3],const PkLoft *S);

typedef struct {
  int   n;
  float phase;
  const PkOpts *face,*capOpts,*collarOpts;
  int   capA,capB;
  float capRoll;
  int   collarA,collarB;
  float collarL,collarR;
} PkTube;
PkTube pkTube0(void);
void   pkTubeDraw(const float p0[3],const float p1[3],float r,const PkTube *O);

/* the lathe: a profile of (radius, length) sections swept about an axis */
typedef struct { float r,l; int hasFace; PkOpts face; } PkSect;
typedef struct {
  int n;
  float phase;
  const PkOpts *shoulder,*capOpts;
  int noCapA,noCapB;
} PkStack;
PkStack pkStack0(void);
void    pkStackDraw(const float base[3],const float axis[3],
                    const PkSect *S,int nS,const PkStack *O);

void pkCapBar(const float a[3],const float b[3],float wdt,float t,
              const float N[3],int seg,const PkOpts *o);
void pkXBrace(const float O[3],const float U[3],const float V[3],
              const float N[3],float w,float h,float wdt,float t,
              const PkOpts *o);
void pkRRFrame(const float O[3],const float U[3],const float V[3],
               const float N[3],float w,float h,float r,float wdt,
               float t,const PkOpts *o,int seg);

typedef struct {
  int   n;
  float fillet;
  int   filletSeg;
  float seed;
  const PkOpts *face,*cap;
} PkBoss;
PkBoss pkBoss0(void);
void   pkBossDraw(const float c[3],const float axis[3],float r,float h,
                  const PkBoss *O);

void pkWheelZ(float cx,float cy,float cz,float R,float t,float depth,
              float seed,const float *tint,int paint,float hub,float spoke);

/* ---- the 37, as recorded calls (props_gen.c) ------------------------ */
typedef void (*PkBuildFn)(void);
extern const PkBuildFn  PK_BUILD[37];
extern const char *const PK_NAME[37];
extern const int        PK_COUNT;
void pkBuildProp(int i);        /* -> pk.face[0..nFace), pk.reg[0..nReg) */

#endif
