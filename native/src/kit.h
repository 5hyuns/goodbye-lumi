/* The kit's machine: the grid it is measured in, the primitives a part is
   allowed to be built from, and the group model that decides what finish a
   piece of a part wears.  kitparts.c is the parts themselves. */
#ifndef KIT_H
#define KIT_H
#include "ship.h"
#include "mesh.h"
#include <string.h>

#define PI_2  1.5707963f
#define PI_4  0.7853982f
#define SQRT2 1.4142136f

/* ---- THE GRID ------------------------------------------------------
   Everything here is a whole number of U, and U is 1.200 m -- and since
   TEX_SCALE is 1/U, ONE TILE OF EVERY MATERIAL IS EXACTLY ONE MODULE.

     U      1.200   the placement module.  A floor tile is one, a wall bay
                    is one wide.  A texel is 4.6875 mm.
     U/2    0.600   the panel module.  Four to a floor tile, and it is not
                    an arbitrary halving: 600 mm is the size a real raised
                    access floor panel is.
     U/4    0.300   the detail module: a column shaft, a head band.
     U/8    0.150   the trim module: a stile, a kick, a rail.
     U/10   0.120   the depth module.  Every plane a bay presents to the
                    corridor is a multiple of 12 mm.
     3U     3.600   the corridor.

   The proportions are the point, not the numbers.  The eye can see a 2:1
   and cannot see a 1.7:1; a part that is 1.13 modules long does not fail to
   fit, it fails to LOOK like it belongs to the same ship as its neighbour. */
#define U     1.200f
#define U_2   0.600f
#define U_4   0.300f
#define U_8   0.150f
#define U_10  0.120f
#define SPL   U_2                    /* the splay that fills every 90 deg */
#define WALL_H (3*U)

/* ---- a part is a LIST OF GROUPS -------------------------------------
   The first version of this stamped a whole part with one material, because
   that is what a brush does and the kit grew out of brushes.  It is the
   wrong unit.  A wall bay is a rolled field, a sprayed panel, a cut stile, a
   rubber kick and a black reveal, and calling all five PANEL threw away
   every one of those distinctions -- then the PANEL texture drew its own
   panel grid on top of the modelled one, at an unrelated pitch, and the two
   fought.

   So a group is one mesh, one material, one projection, one crease angle.
   A group whose material is -1 falls back to the placement material. */
typedef struct { Mesh *m; int t,proj; float cr; float bb[6]; } Grp;

#define MAXG 12
typedef struct {
  Mesh *mach,*gap,*coat,*skin,*duct;   /* the five standard finishes */
  Grp   g[MAXG]; int ng;
} PB;

/* THE VARIANT IS A FLOAT, and for a wall bay it is its HEIGHT IN METRES.
   Keying the part cache on (int)a would collapse a 3.057 m bay and a
   3.6 m one into the same mesh and stand it in both places. */
typedef struct {
  const char *name;
  void (*make)(float a,PB *P);
  int  (*clip)(float a,float *o);      /* 6 floats a box */
  int  (*emit)(float a,float *o);      /* 6 floats a box, then its material */
} KitDef;

extern const KitDef KIT[];
extern const int NKIT;

/* the wall parts live in kitwalls.c; the registry is in kitparts.c */
extern const KitDef KIT_WALL1, KIT_WALL2, KIT_WALL3, KIT_WALL4,
                    KIT_FILLET1, KIT_CORNER1;

/* ---- primitives for parts ------------------------------------------- */
Mesh *mBox(float w,float h,float d);
Mesh *mPlate(float w,float h,float d,float c,int n);
Mesh *mSlab(float w,float d,float t,float c,int n);
Mesh *mSoffit(float w,float d,float t,float c,int n);
Mesh *mSplayA(float w,float a,float b,float r,float t,float c,int n);
Mesh *mSplayB(float w,float a,float b,float r,float t,float c,int n);
Mesh *mPipe(float r,float h,int seg);
Mesh *mBolt(float r,float rise);
Mesh *at(Mesh *m,float x,float y,float z);
Mesh *mFace(Mesh *m,float theta,float d);   /* a +Z plate, turned to face theta */
Mesh *mOct(float a,float h);                /* an eight-sided shaft on y=0 */
Mesh *mOctCap(float a,float t);             /* and a capped drum of it */
/* A vertical light run, cut by a machined strap at every U/2.  A column
   carrying one unbroken three-metre line reads as a lamp standard, not as
   structure -- and the ticks land on the same 600 mm pitch as the floor
   panels, so the eye gets the module twice.  Writes y0,y1 pairs.

   A COLUMN IS NOT ALWAYS 3.6 m.  The plan stands these up to 8.4 m tall
   through a two-deck space, which is fourteen segments -- so the caller's
   array is LITRUN_MAX pairs and this stops there rather than writing off
   the end of it. */
#define LITRUN_MAX 32
int   litRun(float y0,float y1,float g,float *out);

/* ---- building a part ------------------------------------------------ */
void  push(Mesh *dst,Mesh *src);       /* concatenate, then free src */
Mesh *take(Mesh **slot);               /* hand a bucket over, leaving none */
void  grp(PB *P,Mesh *m,int t,int proj,float cr);
void  pbFinish(PB *P,float crMach,float crGap,float crCoat,
               float crSkin,float crDuct);

#endif
