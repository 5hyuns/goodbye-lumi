/* The instanced solids.  Every moving thing in the game -- a door leaf, a
   barrel, a zombie's forearm, the shotgun in your hands -- is one of these
   with a 3x4 transform on it, and they all live in one vertex buffer drawn
   by range.  Adding a solid costs a range, not a draw call. */
#ifndef PRIM_H
#define PRIM_H

enum {
  PRIM_BOX=0, PRIM_BEVEL, PRIM_CAP, PRIM_DOME, PRIM_POD, PRIM_LENS,
  PRIM_SETA, PRIM_SEG, PRIM_HORN, PRIM_CYL, PRIM_DRUM, PRIM_CRATE,
  PRIM_CARA,
  /* 32 was the thirteen solids here plus LUMI's thirteen groups.  props.c
     now adds the bench's own 37 meshes, grouped by material -- see
     propMeshInit -- and a range is still the whole cost of one. */
  PRIM_N_MAX = 384
};

extern float *PRIM_V; extern int PRIM_VN;     /* 6 floats a vertex */
/* and eight more a vertex, in step with it: u,v,ww,wh then
   kind,seed,px,py -- the bench's region, for the faces that have one.
   kind is -1 everywhere else.  See prim.c. */
extern float *PRIM_R;
extern int PRIM_OFF[PRIM_N_MAX], PRIM_CNT[PRIM_N_MAX];
extern int PRIM_N;

void primInit(void);
/* A solid built somewhere else, appended as one more range.  lumimesh.c
   is the only caller: LUMI's hull is a frozen generator's output rather
   than a profile this file could lathe, and a range is all the entity
   pass ever wanted.  Six floats a vertex -- position, then normal, the
   same triangle soup PRIM_V already holds.  Returns the primitive's
   index, or PRIM_BEVEL if there is no room left for one. */
int  primAdd(const float *v6,int nverts);
/* ... and with a region per vertex; r8 may be null */
int  primAddR(const float *v6,const float *r8,int nverts);

#endif
