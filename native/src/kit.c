/* ============================================================
   M2.  THE KIT  --  the machine.  The parts are in kitparts.c.

   Not "level geometry".  A short list of PARTS, each one modelled
   once with the operator machine and then instanced wherever the
   plan says it goes.

   A brush is an axis-aligned box.  A map built out of brushes can
   only ever be boxes, and no texture rescues that -- the
   silhouette is what the eye reads first, and a box has one
   silhouette.  A part, on the other hand, is a mesh graph: it can
   be chamfered, recessed, bolted, ribbed and creased, and it
   carries that shape into every one of the hundreds of places it
   is stamped.

   Rules for anything that goes in the kit:

     * TRUE SCALE.  Modelled in metres, in its own space, standing
       on y=0 with the origin at the mounting point.  Never
       rescaled to fit -- if it does not fit, the part is wrong,
       not the room.
     * NO PRIMITIVE MAY READ AS ITSELF.  A cube that still looks
       like a cube has not been worked.
     * ONE UV RULE.  proj 2 unwraps from the WORLD normal by arc
       length, exactly as the mesher does for a brush face, so a
       part continues the texture of what it is bolted to instead
       of restarting its own run.
     * DECLARE THE CLIP.  A part is triangles, and triangles do
       not collide.
     * DECLARE THE EMITTER.  The bake ray-casts the BRUSH set, so
       a lit channel modelled as part geometry glows in the shader
       and lights nothing.
   ============================================================ */
#include "kit.h"
#include "app.h"
#include <stdlib.h>
#include "plan_data.h"

/* ---- primitives ------------------------------------------------------ */
Mesh *mBox(float w,float h,float d){
  Mesh *m=mCube(); mScale(m,w,h,d); return m;
}
Mesh *at(Mesh *m,float x,float y,float z){ mTranslate(m,x,y,z); return m; }

/* PLATE_ROUND is the kit's one aesthetic knob, and it is the difference
   between industrial and future.  1 gives the flat 45 degree chamfer this
   file started with: correct, machined, and slightly brutal -- it returns
   one value across its width and draws a hard line.  3 rolls the same
   chamfer over a quarter circle instead, and a rolled edge never draws a
   line at all: it sweeps a highlight along itself as you walk past, and it
   never goes fully dark, because some part of the roll is always facing the
   lamp.  That single property is what separates a moulded composite panel
   from a cut steel one.  Cost: a plate goes from 10 quads to 18. */
#define PLATE_ROUND 3

/* mPlate is the whole argument of the kit in one function.  A slab whose
   front face is inset all round and joined to the body by a rolled rim.
   That rim is the only part of a flat panel that ever catches a lamp it is
   not facing: a brush wall returns one value across its whole face and
   reads as paint, a wall built out of plates draws a bright line around
   every panel in it.  Nothing in the kit is allowed to be a bare box.

   Built about the origin so mFixWinding can see which way is out, then slid
   so the BACK sits on z=0 and the front stands proud at z=d. */
Mesh *mPlate(float w,float h,float d,float c,int n){
  if(n<=0) n=PLATE_ROUND;
  Mesh *m=mNew();
  float X=w/2, Y=h/2, D=d/2;
  #define RING(inset,z) do{ \
    float x_=X-(inset); if(x_<0.001f) x_=0.001f; \
    float y_=Y-(inset); if(y_<0.001f) y_=0.001f; \
    mAddVert(m,-x_,-y_,(z)); mAddVert(m, x_,-y_,(z)); \
    mAddVert(m, x_, y_,(z)); mAddVert(m,-x_, y_,(z)); }while(0)
  RING(0,-D);                        /* 0..3  the back */
  RING(0, D-c);                      /* 4..7  where the roll starts */
  for(int k=1;k<=n;k++){             /* the roll: a quarter circle radius c */
    float a=k*PI_2/n;
    RING(c*(1-cosf(a)), D-c+c*sinf(a));
  }
  #undef RING
  { int q[4]={0,1,2,3}; mAddFace(m,q,4,0); }
  for(int r=0;r<=n;r++)              /* the sides, then every step of the roll */
    for(int i=0;i<4;i++){
      int j=(i+1)%4, o=r*4;
      int q[4]={o+i,o+j,o+4+j,o+4+i};
      mAddFace(m,q,4,0);
    }
  { int t=(n+1)*4, q[4]={t,t+1,t+2,t+3}; mAddFace(m,q,4,0); }
  mFixWinding(m);
  mTranslate(m,0,0,D);
  return m;
}
/* the same plate lying down: w across, d deep, t thick, standing on y=0 */
Mesh *mSlab(float w,float d,float t,float c,int n){
  Mesh *m=mPlate(w,d,t,c,n); mTilt(m,-PI_2); return m;
}
/* the same plate, turned the other way: face DOWN, back on y=0, so an
   overhead part is authored in the same numbers as a deck one and hangs
   instead of standing.  Both are rotations, so winding survives. */
Mesh *mSoffit(float w,float d,float t,float c,int n){
  Mesh *m=mPlate(w,d,t,c,n); mTilt(m,PI_2); return m;
}

/* ---- THE SPLAY ------------------------------------------------------
   Every reference for this ship, from the Nostromo forward, has the same
   thing in common and it is not the panels: THE SECTION IS AN OCTAGON.
   Deck, splay, wall, splay, overhead -- five faces a side, eight in all,
   and no two planes anywhere in the corridor meet at a right angle.

   It is not decoration.  A pressure hull wants to be a cylinder and a deck
   wants to be flat, so the octagon is what you get when the two argue,
   which is why it reads as a SHIP rather than as a room; and the 45 degree
   faces catch a lamp neither the wall nor the deck can catch, so a corridor
   lit only at its edges still has a legible section.

   A plate is authored flat and turned 45 degrees about X: A leans out as it
   rises (deck to wall), B leans in as it rises (wall to overhead).  Both
   take the bottom edge at (a, b) and rise r. */
Mesh *mSplayA(float w,float a,float b,float r,float t,float c,int n){
  Mesh *m=mPlate(w,r*SQRT2,t,c,n);
  mTilt(m,-PI_4); mTranslate(m,0,a+r/2,b-r/2);
  return m;
}
Mesh *mSplayB(float w,float a,float b,float r,float t,float c,int n){
  Mesh *m=mPlate(w,r*SQRT2,t,c,n);
  mTilt(m,PI_4); mTranslate(m,0,a+r/2,b+r/2);
  return m;
}
Mesh *mPipe(float r,float h,int seg){
  Mesh *m=mCylinder(seg?seg:12,1);
  mScale(m,r*2,h,r*2); mTranslate(m,0,h/2,0);
  return m;
}
/* a countersunk fastener, lying on a surface whose normal is +Y */
Mesh *mBolt(float r,float rise){
  Mesh *b=mCylinder(8,1);
  mScale(b,r*2,rise,r*2); mTranslate(b,0,rise/2,0);
  mSelNone(b); mSelNormal(b,0,1,0,0.20f,MSEL_SET);
  mExtrude(b,rise*0.45f,0.30f);
  return b;
}

/* a +Z-facing plate, turned to face theta and pushed out to d */
Mesh *mFace(Mesh *m,float theta,float d){
  mSpin(m,theta-PI_2);
  mTranslate(m,cosf(theta)*d,0,sinf(theta)*d);
  return m;
}
/* an eight-sided shaft, standing on y=0, a flat facing +X, +Z and both
   diagonals.  0.46194 is cos(pi/8)/2, so `a` is the apothem and the flats
   land where the wall planes do. */
Mesh *mOct(float a,float h){
  Mesh *m=mCylinder(8,0);
  mScale(m,a/0.46194f,h,a/0.46194f);
  mTranslate(m,0,h/2,0);
  mSpin(m,-0.3926991f);
  return m;
}
Mesh *mOctCap(float a,float t){
  Mesh *m=mCylinder(8,1);
  mScale(m,a/0.46194f,t,a/0.46194f);
  mTranslate(m,0,t/2,0);
  mSpin(m,-0.3926991f);
  return m;
}
int litRun(float y0,float y1,float g,float *out){
  int n=0; float a=y0;
  float first=ceilf(y0/U_2)*U_2;
  for(float y=first; y<y1 && n<LITRUN_MAX-1; y+=U_2){
    if(y-g-a>0.10f){ out[n*2]=a; out[n*2+1]=y-g; n++; }
    a=y+g;
  }
  if(y1-a>0.10f && n<LITRUN_MAX){ out[n*2]=a; out[n*2+1]=y1; n++; }
  return n;
}

/* ---- building a part ------------------------------------------------ */
void push(Mesh *dst,Mesh *src){ mAddMesh(dst,src); mFree(src); }
Mesh *take(Mesh **slot){ Mesh *m=*slot; *slot=0; return m; }

void grp(PB *P,Mesh *m,int t,int proj,float cr){
  if(!m) return;
  if(!m->nF || P->ng>=MAXG){ mFree(m); return; }
  mCreaseAngle(m,cr,0);
  Grp *g=&P->g[P->ng++];
  g->m=m; g->t=t; g->proj=proj; g->cr=cr;
  mBounds(m,g->bb);
}
void pbFinish(PB *P,float crMach,float crGap,float crCoat,
              float crSkin,float crDuct){
  grp(P,take(&P->mach),T_MACH,2,crMach);
  grp(P,take(&P->gap ),T_GAP ,2,crGap);
  grp(P,take(&P->coat),T_COAT,2,crCoat);
  grp(P,take(&P->skin),T_SKIN,2,crSkin);
  grp(P,take(&P->duct),T_DUCT,2,crDuct);
}

/* ---- placement ------------------------------------------------------
   A part is unwrapped where it is built or where it lands, never from a
   world axis picked off the largest component of a smooth normal -- that is
   what tore the rings in half, because the axis flips wherever a curve
   crosses 45 degrees.

     proj 0  flat part: u,w are its own local x,y
     proj 1  ring of major radius `a`: u is arc length around the ring
     proj 2  the one uv rule: T = cross(up,n), B = cross(n,T) on the WORLD
             normal, so u and w are arc length in metres along the surface
             and the part shares the wall's texel density
     proj 3  the part's own square: u,w run across this group's own bounding
             box, with TEX_SCALE divided back out so the texture lands
             exactly once on it

   proj 3 exists for exactly one thing.  Every other material in the kit is
   a FINISH -- grain, tool marks, mill scale, grime -- and a finish has to
   be world-projected or two pieces of the same steel will have different
   texel densities.  But a stencil, a placard, a warning decal is a PICTURE,
   and a picture sliced out of a world grid is a picture with its corners
   cut off. */
static float *EMITBUF; static int nEmitBuf, capEmitBuf;

static void wPlace(const Mesh *m,int mat,float ox,float oy,float oz,float yaw,
                   int proj,float a,const float *bb,float cr){
  float c=cosf(yaw), s=sinf(yaw);
  nEmitBuf=0;
  mEmit(m,cr,&EMITBUF,&nEmitBuf,&capEmitBuf);
  trisReserve(nEmitBuf/6);
  for(int i=0;i<nEmitBuf;i+=6){
    float x=EMITBUF[i], y=EMITBUF[i+1], z=EMITBUF[i+2];
    float nx=EMITBUF[i+3], ny=EMITBUF[i+4], nz=EMITBUF[i+5];
    float wx= x*c+z*s+ox, wy= y+oy, wz=-x*s+z*c+oz;
    float gx=nx*c+nz*s,   gy=ny,    gz=-nx*s+nz*c;
    float u,w;
    if(proj==1){
      float rr=hypot2(x,y);
      u=atan2f(y,x)*a; w=atan2f(z,rr-a)*0.16f;
    } else if(proj==2){
      float tx,ty,tz;
      if(fabsf(gy)>0.999f){ tx=1; ty=0; tz=0; }
      else { float L=hypot2(gz,gx); if(L<1e-9f) L=1; tx=gz/L; ty=0; tz=-gx/L; }
      float bx=gy*tz-gz*ty, by=gz*tx-gx*tz, bz=gx*ty-gy*tx;
      u=wx*tx+wy*ty+wz*tz;
      w=wx*bx+wy*by+wz*bz;
    } else if(proj==3){
      /* The group's own square, taken across its two LARGEST axes.  Reading
         x,y unconditionally was right for a wall plate and silently wrong
         for a floor one: a deck panel's y extent is its 60 mm thickness, so
         a picture unwrapped that way came out stretched ten times up a face
         nobody ever sees.  The smallest extent is the plate's thickness --
         whichever axis that turns out to be -- and the other two are the
         face.

         SQUARE CELLS.  Filling the plate in both directions is right for a
         square plate and wrong for every other kind: an 840 x 520 lid
         stretched the picture 1.6:1, so its bolts came out as ovals.  The
         long axis is divided into a whole number of cells the size of the
         short one instead, so a cell is always square, the picture still
         reaches all four edges, and a long plate simply shows more of it --
         which is what a long plate does in the real world too. */
      float ex=bb[3]-bb[0], ey=bb[4]-bb[1], ez=bb[5]-bb[2];
      float aa,bv; int ii,jj;
      if(ey<=ex&&ey<=ez){ aa=x; bv=z; ii=0; jj=2; }   /* lying flat: a deck */
      else if(ex<=ez)   { aa=z; bv=y; ii=2; jj=1; }   /* standing, +-X */
      else              { aa=x; bv=y; ii=0; jj=1; }   /* standing, +-Z */
      float du=bb[ii+3]-bb[ii], dw=bb[jj+3]-bb[jj];
      if(du>dw){ int r=(int)(du/dw+0.5f); if(r<1) r=1; du/=r; }
      else     { int r=(int)(dw/du+0.5f); if(r<1) r=1; dw/=r; }
      float d1=du*TEX_SCALE, d2=dw*TEX_SCALE;
      if(d1<1e-6f) d1=1e-6f;
      if(d2<1e-6f) d2=1e-6f;
      u=(aa-bb[ii])/d1; w=(bv-bb[jj])/d2;
    } else { u=x; w=y; }
    float v9[9]={wx,wy,wz, gx,gy,gz, u,w, (float)mat};
    trisPush(v9);
  }
}

/* A part is built once per (part, variant) and stamped everywhere it
   occurs.  The browser cached on the string k+':'+a, and that is exactly
   the right key: for a floor part the variant is a small integer, but for
   a wall bay it is the bay's HEIGHT IN METRES, and 3.057 and 3.6 must not
   collapse into one mesh.  A linear table is plenty -- the plan uses
   fifty-odd distinct pairs and asks 1,365 times. */
#define MAXPART 256
static struct { int k; float a; PB *pb; } CACHE[MAXPART];
static int nCache;

static PB *kitGroups(int k,float a){
  for(int i=0;i<nCache;i++)
    if(CACHE[i].k==k && CACHE[i].a==a) return CACHE[i].pb;
  if(!KIT[k].make) return 0;
  PB *P=(PB*)malloc(sizeof(PB));
  memset(P,0,sizeof *P);
  P->mach=mNew(); P->gap=mNew(); P->coat=mNew();
  P->skin=mNew(); P->duct=mNew();
  KIT[k].make(a,P);
  /* whatever the part did not claim -- a part is allowed to use three of
     the five buckets and say nothing about the others */
  mFree(P->mach); mFree(P->gap); mFree(P->coat);
  mFree(P->skin); mFree(P->duct);
  P->mach=P->gap=P->coat=P->skin=P->duct=0;
  if(nCache<MAXPART){ CACHE[nCache].k=k; CACHE[nCache].a=a; CACHE[nCache].pb=P; nCache++; }
  return P;
}

int KIT_PLACED;

void kitBuildModules(void){
  double t0=platTime();
  int placed=0, missing=0, v0=nTriVert, b0=nBrush;
  static float box[64*7];   /* a callback that overruns this smashes the loop */
  for(int i=0;i<PLAN_NMOD;i++){
    int k=PLAN_MOD_K[i];
    if(k<0||k>=NKIT) continue;
    const float *p=PLAN_MOD+i*5;
    float x=p[0], y=p[1], z=p[2], yaw=p[3], a=p[4];
    int mat=PLAN_MOD_T[i];
    PB *P=kitGroups(k,a);
    if(!P){ missing++; continue; }
    placed++;
    for(int g=0;g<P->ng;g++){
      Grp *G=&P->g[g];
      wPlace(G->m, G->t<0?mat:G->t, x,y,z,yaw, G->proj, a, G->bb, G->cr);
    }
    float c=cosf(yaw), s=sinf(yaw);
    #define BOX(q,o) do{ \
      float ax=(q)[0]*c+(q)[2]*s+x, az=-(q)[0]*s+(q)[2]*c+z; \
      float bx=(q)[3]*c+(q)[5]*s+x, bz=-(q)[3]*s+(q)[5]*c+z; \
      (o)[0]=ax; (o)[1]=(q)[1]+y; (o)[2]=az; \
      (o)[3]=bx; (o)[4]=(q)[4]+y; (o)[5]=bz; }while(0)
    if(KIT[k].clip){
      int n=KIT[k].clip(a,box);
      if(n<0||n>64) platLog("kit    !! %s clip returned %d",KIT[k].name,n);
      for(int j=0;j<n;j++){
        float o[6]; BOX(box+j*6,o);
        addBrush(o[0],o[1],o[2],o[3],o[4],o[5],mat,1);  /* stops you, undrawn */
      }
    }
    /* What a part declares in `emit` is stamped as a real, drawn brush lying
       in the modelled recess -- one object, seen and felt by both the
       renderer and the bake.  It also has to clear the part's own clip
       boxes, or the ray hits the clip first and finds no light at all. */
    if(KIT[k].emit){
      int n=KIT[k].emit(a,box);
      if(n<0||n>64) platLog("kit    !! %s emit returned %d",KIT[k].name,n);
      for(int j=0;j<n;j++){
        float o[6]; BOX(box+j*7,o);
        addBrush(o[0],o[1],o[2],o[3],o[4],o[5],(int)box[j*7+6],0);
      }
    }
    #undef BOX
  }
  KIT_PLACED=placed;
  platLog("kit    %d placed / %d unported  %d verts  %d brushes  %.0f ms",
          placed,missing,nTriVert-v0,nBrush-b0,(platTime()-t0)*1000.0);
}
