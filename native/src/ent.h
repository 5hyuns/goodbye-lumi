/* ============================================================
   ENTITIES.  Everything that moves is a solid with a 3x4
   transform on it.  A zombie is fourteen of them; so is the
   shotgun in your hands; so is a door leaf.  One instanced draw
   per primitive covers the lot.

   Direct light is resolved on the CPU against the same shadow
   tracer the lightmap used, so a zombie standing in a doorway is
   lit by the room it is actually standing in -- the GPU only
   interpolates.
   ============================================================ */
#ifndef ENT_H
#define ENT_H
#include "m.h"

/* 4800 was the animals, the doors, the debris and the items.  props.c adds
   up to sixty pieces of furniture in view at up to forty instances each,
   and pushEnt drops silently past the cap -- a room that quietly loses its
   last crate has no error anywhere.  8192 costs 1.0 MB of upload buffer. */
#define MAX_ENT    8192
#define ENT_STRIDE 32
#define MAXD       8            /* unbaked light slots */

/* what an instance wants to say beyond its box and its colour */
typedef struct {
  float flash;                  /* hit flash, 0..1 */
  float alpha;                  /* 1 unless it is fading */
  float zone;                   /* which room's ambient cube lights it */
  float rim;                    /* silhouette gain */
  float lit[3], ao;             /* the CPU's shadowed key light */
  float ldir[3], rough;
  float sk,sf,si,sp;            /* surface kind, detail scale, sheen, phase */
} EntFx;

extern const EntFx ENT_PLAIN;   /* the sensible defaults */

/* ---- 3x4 row-major transforms, which is all the buffer wants -------- */
void mIdent34(float *o);
void mMul34(const float *a,const float *b,float *o);
void mTrans34(float x,float y,float z,float *o);
void mRotY34(float a,float *o);
void mRotX34(float a,float *o);
void mRotZ34(float a,float *o);

void entReset(void);
void pushEnt(const float *M,float sx,float sy,float sz,
             float r,float g,float b,float emis,const EntFx *fx,int prim);
void pushBox(float x,float y,float z,float sx,float sy,float sz,
             float r,float g,float b,float emis,const EntFx *fx,int prim);
int  entCount(void);

/* the same shadow tracer the lightmap used, run for a handful of points a
   frame.  A zombie in a doorway is lit by the room it is standing in, and
   goes dark when it steps behind the reactor. */
void sampleLight(float x,float y,float z,float *out,float *outDir);

/* ---- what the frame hands to every program ------------------------- */
typedef struct FrameState {
  float proj[16], view[16];
  float camPos[3], camDir[3];
  float minLight[3], fogCol[3];
  float fogDensity, time, style;
  int   flash;                  /* the torch */
  int   dynN;
  float dynP[MAXD*3], dynC[MAXD*3], dynR[MAXD];
  float vmFlash;                /* the muzzle, for the viewmodel pass */
} FrameState;

int  entInit(int cubeCount);
void entDraw(const FrameState *F,int viewModel);

#endif
