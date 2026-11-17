/* ============================================================
   M.  MESH OPERATORS

   A model is not a pile of primitives.  It is one surface that
   starts as a cube and is sculpted: select faces, extrude them,
   lock the edges that must stay sharp, then subdivide.  Catmull-
   Clark rounds everything a crease has not pinned, which is why
   the results read as machined rather than assembled.

   Faces are n-gons, so their vertex lists live in one shared
   index pool rather than in the face: a quad becomes four quads
   under subdivision and the pool just grows.
   ============================================================ */
#ifndef MESH_H
#define MESH_H
#include "m.h"

typedef struct { int off,n,s; } MFace;        /* s: selected */

typedef struct {
  float *V; int nV,capV;                      /* 3 floats per vertex */
  MFace *F; int nF,capF;
  int   *FI; int nFI,capFI;                   /* the index pool */
  long long *C; int capC,nC;                  /* creased edge keys, open set */
} Mesh;

#define MFV(m,f,i) ((m)->FI[(f)->off+(i)])

Mesh *mNew(void);
void  mFree(Mesh *m);
Mesh *mClone(const Mesh *m);

/* primitives */
Mesh *mCube(void);
Mesh *mCylinder(int seg,int capped);
Mesh *mGrid(int nx,int nz);

/* inspection */
void  mFaceNormal(const Mesh *m,const MFace *f,float *out);
void  mFaceCentre(const Mesh *m,const MFace *f,float *out);

/* selection */
void  mSelAll(Mesh *m,int v);
void  mSelNone(Mesh *m);
void  mSelInvert(Mesh *m);
enum { MSEL_SET=0, MSEL_ADD, MSEL_SUB, MSEL_AND };
void  mSelBox(Mesh *m,float x0,float y0,float z0,float x1,float y1,float z1,int mode);
void  mSelNormal(Mesh *m,float nx,float ny,float nz,float tolDeg,int mode);
void  mSelGrow(Mesh *m);
void  mSelEvery(Mesh *m,int n,int off);

/* creases */
void  mCreaseAngle(Mesh *m,float deg,int keep);
void  mUncreaseBox(Mesh *m,float x0,float y0,float z0,float x1,float y1,float z1);

/* modelling */
void  mExtrude(Mesh *m,float dist,float inset);
void  mInsetFaces(Mesh *m,float t);
Mesh *mSplit(Mesh *m,int times);
Mesh *mSubdivide(Mesh *m,int times);
void  mOrient(Mesh *m);

/* deformers -- fn(p, ctx) edits the point in place */
void  mDeform(Mesh *m,void (*fn)(float*,void*),void *ctx);
void  mTransformSel(Mesh *m,void (*fn)(float*,void*),void *ctx);

/* output */
void  mFitUniform(Mesh *m);
void  mFitBox(Mesh *m,float pad);
/* triangulate and shade into a growable pos/nrm array (6 floats per vertex) */
void  mEmit(const Mesh *m,float angleDeg,float **out,int *n,int *cap);

/* convenience used all over the kit */
void  mAddMesh(Mesh *a,const Mesh *b);        /* GenMesh Add: concatenate */
void  mFixWinding(Mesh *m);
void  mBounds(const Mesh *m,float *box6);
void  mSpin(Mesh *m,float ang);               /* about Y */
void  mTilt(Mesh *m,float ang);               /* about X: +Y leans toward +Z */
void  mRoll(Mesh *m,float ang);               /* about Z */
Mesh *mTorus(int seg,int slices,float r1,float r2);
void  mAddFace(Mesh *m,const int *idx,int n,int sel);
int   mAddVert(Mesh *m,float x,float y,float z);
void  mScale(Mesh *m,float sx,float sy,float sz);
void  mTranslate(Mesh *m,float tx,float ty,float tz);

#endif
