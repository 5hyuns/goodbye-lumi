/* ============================================================
   PARTICLES, DECALS, DEBRIS -- G5 of map.html.

   Three lists and two instanced draws.  Everything the world
   throws off after a shot lands here: the sparks off the plate,
   the smoke off the muzzle, the crater the round left, and the
   loose boxes a body comes apart into.

   The dynamic light pool lives here too.  It used to be four
   slots inside vmw.c because the muzzle was the only source;
   a spark off steel is a second one, and an explosion will be a
   third, so the pool is where they can all reach it.
   ============================================================ */
#ifndef FX_H
#define FX_H
#include "ent.h"

/* the five sprite kinds the particle shader branches on */
enum { PK_BLOB=0, PK_SPARK, PK_SMOKE, PK_RING, PK_CHUNK };

int  fxInit(void);
void fxReset(void);

/* grav is positive DOWN; drag is per second; add 1 for an additive sprite */
void fxEmit(float x,float y,float z,float vx,float vy,float vz,
            float size,float life,int kind,
            float r,float g,float b,float a,
            float grav,float drag,int add,float spin);

/* kind 0 is a blood splat, kind 1 a bullet crater (which samples the light
   that was on the wall at the moment it arrived) */
void fxDecal(float x,float y,float z,float nx,float ny,float nz,
             float size,float r,float g,float b,float a,int kind);

void fxDebris(float x,float y,float z,float vx,float vy,float vz,
              float sx,float sy,float sz,float r,float g,float b,
              float life,int bleed,int ring);

void fxAddDynLight(float x,float y,float z,float r,float g,float b,
                   float rad,float life);
void fxLights(FrameState *F);        /* into the frame's eight slots */

/* one round arriving on a static brush: sparks, smoke, a crater, a light */
void fxSurfaceHit(int brush,float x,float y,float z,
                  float nx,float ny,float nz);
void fxTracer(const float *a,const float *b);

void fxStep(float dt);
void fxPushDebris(void);                      /* before entDraw */
void fxDrawDecals(const FrameState *F,float dt);
void fxDrawParticles(const FrameState *F);

#endif
