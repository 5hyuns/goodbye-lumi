/* ============================================================
   THE WALL PARTS.  A bay's variant is its HEIGHT in metres, not
   an index -- which is why the part cache is keyed on the float
   and not on (int)a.  A run of 3.057 m bays and a run of 2.697 m
   ones are two different meshes of the same part.
   ============================================================ */
#include "kit.h"
#include "kitwalls.h"

/* ====================================================================
   THE BAY GRAMMAR

   Three bays only read as one wall if they agree about where their lines
   are.  These constants are that agreement, and WALL 1, 2, 3 and 4 are all
   built out of them -- which is why they can be stamped in any order along
   a run and still look like one ship rather than four kits.

   THE DEPTH GRID.  Every plane a bay presents to the corridor is a
   multiple of U/100 = 12 mm, so no two parts can disagree about how far
   proud a thing sits -- and each plane wears exactly one finish:

     0.036  channel floor   CHAN   the lit line, deep in its own recess
     0.012  field           SKIN   rolled plate, untouched since the mill
     0.072  panel face      COAT   sprayed, chipped back at its edges
     0.132  kick face       DUCT   rubber, because that is where boots go
     0.480  stile face      MACH   cut steel, the brightest thing in the bay
     0.516  rail nose       MACH   and proud of all of it

   THE STEP IS THE POINT.  At 84 mm of reveal a lamp two metres away throws
   a shadow about as deep as a pencil line.  At 468 mm the whole bay is in
   shadow and only the pilaster is lit, which is what the references
   actually look like -- you read a corridor as a row of LIT COLUMNS with
   darkness between them, not as a wall with grooves in it.
   ==================================================================== */
#define B_W      U             /* an event bay is one module wide */
#define B_W2     (2*U)         /* 2.400 -- a bay sized by what is IN it */
#define B_W4     (4*U)         /* 4.800 -- and the FIELD bay is four */
#define B_STILE  (U/6)         /* 0.200 -- two bays make a 400 mm pilaster */
#define B_GAPW   (U/24)        /* 0.050 -- the black channel inside each stile */
#define B_KICK   U_8           /* rubber to 0.150 */
#define B_LINE   (2*U)         /* 2.400 -- THE LIGHT LINE */
#define B_SLOT   0.072f        /* its opening   (6 depth modules) */
#define B_LIPH   0.036f        /* and its lips  (3) */
#define B_HEAD   U_4           /* 0.300 -- the coated band under the overhead */
#define B_RAIL_Y (3*U/4)       /* 0.900 -- and a handrail really is 900 mm */

#define P_CHAN  0.036f
#define P_FIELD 0.012f
#define P_PANEL 0.072f
#define P_KICK  0.132f
#define P_STILE 0.480f
#define P_RAIL  0.516f

static float bayH(float h){ return h>0.01f ? h : WALL_H; }

/* THE FIELD IS PLATED, not one sheet.  A 4.8 m bay whose back is a single
   rolled sheet reads as LEATHER from any distance -- mill scale is a mottle
   with no edge anywhere in it, which is exactly right for what it is and
   exactly wrong for the largest surface in the room.  It gets the deck's
   treatment instead: plates about 1.2 m square on the module, a reveal on
   every seam, and the backing sheet behind them. */
static void band(PB *P,float x0,float y0,float x1,float y1){
  if(x1-x0<0.02f||y1-y0<0.02f) return;
  push(P->skin,at(mPlate(x1-x0,y1-y0,0.006f,0.004f,0),(x0+x1)/2,(y0+y1)/2,0));
  int FN=(int)((x1-x0)/1.200f+0.5f); if(FN<1) FN=1;
  int FM=(int)((y1-y0)/1.200f+0.5f); if(FM<1) FM=1;
  float FW=(x1-x0)/FN, FH=(y1-y0)/FM;
  for(int a=0;a<FN;a++) for(int b=0;b<FM;b++)
    push(P->skin,at(mPlate(FW-0.036f,FH-0.036f,P_FIELD,0.010f,0),
                    x0+FW*(a+0.5f), y0+FH*(b+0.5f), 0));
}

/* Every bay's shared half.  A bay calls this, then adds the one thing that
   makes it itself -- which is the only part that should ever differ. */
static void bayShell(float h,PB *P,float W){
  float IW=W-2*B_STILE;
  band(P,-W/2,0,W/2,h);
  for(int si=0;si<2;si++){
    float s=si?1.f:-1.f;
    float sx=s*(W/2-B_STILE/2);
    /* A REAL BEVEL.  16 mm of chamfer was right when the stile stood 120 mm
       proud; on a 480 mm pilaster it is a hairline, and the column read as a
       bare box with a scratch round it.  50 mm rolled over three steps
       catches the overhead lamp all the way down its length. */
    push(P->mach,at(mPlate(B_STILE,h,P_STILE,0.050f,0), sx,h/2,0));
    /* AND IT LANDS THROUGH A SPLAY.  A 480 mm pilaster that stops dead on
       the deck is a box standing on a floor; one that flares over its last
       200 mm is a column, and it is the same 45 degrees the deck and the
       overhead junctions already use. */
    push(P->mach,at(mSplayA(B_STILE,0,P_STILE+0.204f,0.204f,0.034f,0.026f,0),
                    sx,0,0));
    push(P->mach,at(mSplayB(B_STILE,h-0.204f,P_STILE,0.204f,0.034f,0.026f,0),
                    sx,0,0));
    push(P->gap ,at(mPlate(B_GAPW,h,0.018f,0.004f,0),
                    s*(W/2-B_STILE-B_GAPW/2),h/2,0));
  }
  /* ---- the kick, and the wash under its nosing ------------------------
     What stops a dark corridor being a black tube is not the ceiling: it is
     a warm line at ankle height that throws a POOL ON THE DECK in front of
     every cabinet.  It is the one light in the frame whose source you never
     see and whose effect you always do. */
  push(P->duct,at(mPlate(IW,0.118f,P_KICK,0.014f,0), 0,0.059f,0));
  push(P->gap ,at(mPlate(IW,0.034f,0.040f,0.006f,0), 0,0.135f,0));
  push(P->mach,at(mPlate(IW,0.034f,P_KICK+0.014f,0.008f,0), 0,B_KICK+0.017f,0));

  /* ---- the light line -------------------------------------------------
     Not a stripe painted on the wall.  A black recess, two machined lips
     proud of the diffuser at the bottom of it, and the diffuser itself
     stamped as a real brush so the bake actually lights the corridor off
     it.  You never see the source. */
  push(P->gap,at(mPlate(IW,B_SLOT+2*B_LIPH,0.020f,0.005f,0), 0,B_LINE,0));
  for(int ti=0;ti<2;ti++){
    float t=ti?1.f:-1.f;
    push(P->mach,at(mPlate(IW,B_LIPH,0.092f,0.010f,0),
                    0,B_LINE+t*(B_SLOT+B_LIPH)/2,0));
  }
  /* ---- the head band -------------------------------------------------- */
  if(h>B_LINE+0.55f){
    push(P->coat,at(mPlate(IW,B_HEAD,P_PANEL,0.014f,0), 0,h-B_HEAD/2,0));
    push(P->mach,at(mPlate(IW,0.030f,P_PANEL+0.018f,0.008f,0),
                    0,h-B_HEAD-0.015f,0));
  }
}

/* the clip for any bay: the wall, notched open at the light line.  The
   notch is not cosmetic -- the bake finds an emitter by ray-casting the
   brush set, so a diffuser buried inside a clip box lights nothing at all.
   Three boxes, not two: the kick pocket needs a notch for the same reason. */
static int bayClip(float h,float d,float W,float *o){
  h=bayH(h);
  float hw=W/2;
  float c[3][6]={
    {-hw,0,0,          hw,0.112f,        d},
    {-hw,0.160f,0,     hw,B_LINE-0.058f, d},
    {-hw,B_LINE+0.058f,0, hw,h,          d}};
  memcpy(o,c,sizeof c); return 3;
}
/* the two lit things in every bay: a COOL line at 2U that you read the
   corridor's length by, and a WARM one at 135 mm that you read the deck by.
   Warm below, cool above, on every bay in the ship -- so a passage has a
   colour gradient from floor to ceiling that no single lamp could give it,
   and the two never compete because they are two metres apart. */
static int bayEmit(float W,float *o){
  float x=W/2-B_STILE-0.012f;
  float e[2][7]={
    {-x,B_LINE-0.026f,P_CHAN,  x,B_LINE+0.026f,P_CHAN+0.016f, T_CHAN},
    {-x,0.124f,0.042f,         x,0.146f,       0.058f,        T_GLOW}};
  memcpy(o,e,sizeof e); return 2;
}

/* ====================================================================
   K5  WALL 1  --  SERVICE PANEL BAY

   The quiet one, and the one most of a ship should be.  A machined frame
   with two sprayed access panels sitting on it, a handrail across the
   middle, and the light line above.

   The handrail runs the full width on purpose.  It is the one element that
   crosses the pilaster, so a run of these reads as a corridor with a rail
   down it rather than as a row of separate panels.
   ==================================================================== */
void mkWall1(float a,PB *P){
  float h=bayH(a);
  float IW=B_W-2*B_STILE;
  bayShell(h,P,B_W);
  /* the machined frame the panels are set into.  One plate, so the visible
     machining is a 30 mm reveal around each panel -- a border that is cut
     metal next to sprayed metal, which is a boundary no texture can draw. */
  float f0=0.204f, f1=B_LINE-0.126f;
  push(P->mach,at(mPlate(IW,f1-f0,0.046f,0.010f,0), 0,(f0+f1)/2,0));

  /* The two lids.  They were COAT -- a sprayed finish with nothing on it --
     and a bay with two blank 840 mm rectangles in the middle of it is the
     single reason WALL 1 read as a grey box.  They are lids, so they carry
     the ochre, and they carry it as a picture in their own square rather
     than as anything modelled: bezel, recessed field, three bars, six
     bolts, no triangles. */
  Mesh *lid[2];
  float pan[2][2]={{0.246f,B_RAIL_Y-0.072f},{B_RAIL_Y+0.072f,f1-0.048f}};
  for(int q=0;q<2;q++){
    lid[q]=at(mPlate(IW-0.060f,pan[q][1]-pan[q][0],P_PANEL,0.016f,0),
              0,(pan[q][0]+pan[q][1])/2,0);
    /* a black reveal under each panel: the panel is a lid, and a lid has a
       gap under it.  This is the shadow the raking lamp is looking for. */
    push(P->gap,at(mPlate(IW-0.060f,0.022f,0.030f,0.006f,0),0,pan[q][0]-0.011f,0));
    for(int si=0;si<2;si++) for(int ti=0;ti<2;ti++){
      float s=si?1.f:-1.f, t=ti?1.f:-1.f;
      Mesh *b=mBolt(0.021f,0.011f); mTilt(b,PI_2);
      push(P->mach,at(b, s*(IW/2-0.086f), pan[q][ti]+t*0.070f, P_PANEL));
    }
  }
  /* the handrail, full width, over the pilaster and on to the next bay */
  push(P->mach,at(mPlate(B_W,0.060f,P_RAIL,0.012f,0), 0,B_RAIL_Y,0));
  for(int si=0;si<2;si++){                          /* and its brackets */
    float s=si?1.f:-1.f;
    push(P->mach,at(mPlate(0.072f,U_8,P_RAIL-0.012f,0.010f,0),
                    s*(B_W/2-B_STILE/2),B_RAIL_Y,0));
  }
  /* two stiffener straps in the upper field, so the run above the light
     line is not a blank metre */
  float sy[2]={B_LINE+U_4-0.06f, h-B_HEAD-U_4+0.06f};
  for(int i=0;i<2;i++)
    if(sy[i]>B_LINE+0.15f && sy[i]<h-B_HEAD-0.05f)
      push(P->mach,at(mPlate(IW-0.180f,0.048f,P_FIELD+0.026f,0.009f,0),0,sy[i],0));

  /* the placard: bay number, stencil block, ochre edge.  Its own group, its
     own projection -- the one picture in the bay. */
  Mesh *mark=at(mPlate(U_4,0.180f,0.020f,0.005f,0), -0.216f,1.320f,P_PANEL);

  grp(P,take(&P->skin),T_SKIN,2,32);
  grp(P,take(&P->coat),T_COAT,2,34);
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,34);
  grp(P,mark,T_MARK,3,40);
  for(int q=0;q<2;q++) grp(P,lid[q],T_ACC,3,34);
}
int clWall1(float a,float *o){ return bayClip(a,P_RAIL+0.010f,B_W,o); }
int emWall1(float a,float *o){ (void)a; return bayEmit(B_W,o); }

/* ====================================================================
   K6  WALL 2  --  RIBBED STRUCTURAL BAY, 2U

   The frame, admitted.  WALL 1 hides the structure behind a skin; this one
   puts two I-ribs in front of it and runs the services down the channel
   between them, where a real ship puts them because it is the one place
   nothing can knock them.

   The ribs stand 230 mm proud and the light line is behind their front
   face, so every rib CUTS the lit run and throws a hard vertical shadow
   across it.  That is an effect no wall texture can produce and it costs
   nothing -- the geometry and the light are the same object.
   ==================================================================== */
void mkWall2(float a,PB *P){
  float h=bayH(a);
  float IW=B_W2-2*B_STILE;
  bayShell(h,P,B_W2);

  const float RX=U_2, FT=0.048f;
  for(int si=0;si<2;si++){
    float s=si?1.f:-1.f, x=s*RX;
    push(P->mach,at(mPlate(U_4+0.048f,h,FT,0.012f,0), x,h/2,P_FIELD));
    push(P->skin,at(mBox(0.064f,h,0.190f), x,h/2,P_FIELD+FT+0.095f));
    push(P->mach,at(mPlate(U_4+0.064f,h,FT,0.014f,0), x,h/2,0.330f));
    /* the web is rolled plate and the flanges are cut, which is true of a
       real rolled section and is why the rib is legible from any angle */
    static const float by[4]={0.42f,1.32f,2.04f,3.12f};
    for(int i=0;i<4;i++){
      if(by[i]>h-0.25f) continue;
      Mesh *b=mBolt(0.024f,0.013f); mTilt(b,PI_2);
      push(P->mach,at(b, x,by[i],0.378f));
    }
  }

  /* FIVE MAINS, AND THEY ARE MAINS.  110 to 155 mm in the radius, so 220 to
     310 across, which is what a ship's coolant and air trunks actually are:
     things you would have to climb over, that cast their own shadow on the
     wall behind them, and that read as PIPE and not as line from the far
     end of the corridor.  The bay went to 2U to carry them, which is the
     right way round -- the part is sized by what is in it.

     AND THEY HAVE JOINTS.  A pipe with no coupling in twelve metres is
     extruded, not installed: a flanged collar every 900 mm is the single
     detail that makes a run read as pipework, and it costs one torus. */
  static const float MAINS[5][2]={{-0.800f,0.140f},{-0.400f,0.110f},
                                  {0.000f,0.155f},{0.400f,0.115f},
                                  {0.800f,0.130f}};
  for(int k=0;k<5;k++){
    if(k==1) continue;                            /* this one is the copper */
    float x=MAINS[k][0], r=MAINS[k][1];
    push(P->duct,at(mPipe(r,h-0.30f,12), x,B_KICK+0.030f,P_FIELD+r+0.034f));
    for(float y=B_KICK+0.46f; y<h-0.34f; y+=0.900f){
      push(P->mach,at(mPipe(r+0.030f,0.062f,12), x,y,P_FIELD+r+0.034f));
      push(P->gap ,at(mPipe(r+0.008f,0.014f,12), x,y+0.062f,P_FIELD+r+0.034f));
    }
  }
  /* One warm conductor in a bundle of black ones says the pipes are
     different services rather than more wall, and it is the only warm thing
     in the bay. */
  Mesh *cu=mNew();
  { float x=MAINS[1][0], r=MAINS[1][1];
    push(cu,at(mPipe(r,h-0.30f,12), x,B_KICK+0.030f,P_FIELD+r+0.034f));
    for(float y=B_KICK+0.46f; y<h-0.34f; y+=0.900f)
      push(cu,at(mPipe(r+0.026f,0.058f,12), x,y,P_FIELD+r+0.034f)); }

  /* clamps.  They are what makes a bundle read as installed rather than as
     five pipes that happen to be next to each other. */
  static const float cy[3]={U_2,3*U_2,5*U_2};
  for(int i=0;i<3;i++){
    float y=cy[i];
    if(y>h-0.20f) continue;
    if(fabsf(y-B_LINE)<0.14f) continue;           /* never over the light line */
    push(P->mach,at(mPlate(IW-0.120f,0.062f,P_FIELD+0.130f,0.012f,0),0,y,0));
    push(P->gap ,at(mPlate(IW-0.160f,0.020f,P_FIELD+0.104f,0.005f,0),0,y-0.040f,0));
  }
  /* the cross brace, at head height, tying the two ribs together */
  float by2 = h-0.50f < 5*U_2 ? h-0.50f : 5*U_2;
  push(P->mach,at(mPlate(2*RX+0.240f,0.180f,0.070f,0.016f,0), 0,by2,0.330f));
  push(P->gap ,at(mPlate(2*RX+0.200f,0.030f,0.048f,0.006f,0), 0,by2-0.104f,0.330f));

  /* WALL 2 is the bay that admits its services, so WALL 2 is the bay whose
     placard is a live one: this is the kit's ONE readout. */
  Mesh *mark=at(mPlate(0.300f,0.192f,0.020f,0.006f,0), -1.010f,1.500f,0.366f);

  grp(P,take(&P->skin),T_SKIN,2,32);
  grp(P,take(&P->coat),T_COAT,2,34);
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,36);
  grp(P,cu,T_CU,2,36);
  grp(P,mark,T_READ,3,40);
}
int clWall2(float a,float *o){ return bayClip(a,0.480f,B_W2,o); }
int emWall2(float a,float *o){ (void)a; return bayEmit(B_W2,o); }

/* ====================================================================
   K7  WALL 3  --  SERVICE & AIR BAY

   The loud one, and the reason to have three.  Air has to come from
   somewhere, and a louvre bank is the one wall element that is
   unmistakably MACHINERY: a stack of tilted slats, each lit on its top
   edge and black underneath, so the panel reads as a stripe pattern that
   changes as you walk past it.  A texture cannot do that, because the
   pattern is parallax.
   ==================================================================== */
static void bank(PB *P,float IW,float y0,float y1,int n){
  /* a black pocket, four bars round it, and n slats.  The frame is FOUR
     BARS, never a plate over an opening -- a plate here is a hole with a
     lid on it and the slats may as well not exist. */
  const float FW=0.070f, W2=IW-0.030f;
  push(P->gap ,at(mPlate(W2,y1-y0,0.018f,0.005f,0), 0,(y0+y1)/2,0));
  push(P->mach,at(mPlate(W2,FW,P_KICK,0.012f,0), 0,y0-FW/2,0));       /* sill */
  push(P->mach,at(mPlate(W2,FW,P_KICK,0.012f,0), 0,y1+FW/2,0));       /* head */
  for(int si=0;si<2;si++){
    float s=si?1.f:-1.f;
    push(P->mach,at(mPlate(FW,y1-y0+2*FW,P_KICK,0.012f,0),
                    s*(W2/2-FW/2),(y0+y1)/2,0));                      /* jambs */
  }
  float pitch=(y1-y0)/n;
  for(int i=0;i<n;i++){
    Mesh *sl=mPlate(W2-0.150f,0.052f,0.058f,0.008f,0);
    mTilt(sl,-0.64f);
    push(P->mach,at(sl, 0,y0+pitch*(i+0.5f),0.024f));
  }
}
void mkWall3(float a,PB *P){
  float h=bayH(a);
  float IW=B_W-2*B_STILE;
  bayShell(h,P,B_W);
  bank(P,IW,U_4-0.060f,5*U/4,12);                 /* the main intake */
  { float y1=h-B_HEAD-0.120f; float y1b=B_LINE+3*U_4;
    bank(P,IW,B_LINE+U_4-0.180f, y1<y1b?y1:y1b, 6); }   /* its echo */

  /* the inspection hatch between them: sprayed, dogged, with a black gasket
     line all round so it reads as a thing that opens */
  float hy0=27*U/20, hy1=B_LINE-0.126f;
  push(P->gap,at(mPlate(IW-0.020f,hy1-hy0+0.040f,0.026f,0.006f,0),0,(hy0+hy1)/2,0));
  Mesh *lid=at(mPlate(IW-0.070f,hy1-hy0,P_PANEL,0.016f,0),0,(hy0+hy1)/2,0);
  for(int si=0;si<2;si++) for(int ti=0;ti<2;ti++){
    float s=si?1.f:-1.f, t=ti?1.f:-1.f;
    float yy=(hy0+hy1)/2+t*((hy1-hy0)/2-0.090f);
    Mesh *dg=mPipe(0.026f,0.026f,8); mTilt(dg,PI_2);
    push(P->mach,at(dg, s*(IW/2-0.104f), yy, P_PANEL));
    push(P->mach,at(mPlate(0.088f,0.014f,P_PANEL+0.028f,0.005f,0),
                    s*(IW/2-0.104f), yy, P_PANEL));
  }
  push(P->duct,at(mPlate(IW-0.070f,0.018f,P_PANEL-0.030f,0.004f,0),0,hy0-0.009f,0));

  /* a coolant run dropping down the side and turning into the hatch */
  Mesh *cu=mNew();
  push(cu,at(mPipe(0.030f,hy0-B_KICK-0.10f,10), -0.408f,B_KICK+0.10f,P_KICK));
  { Mesh *el=mPipe(0.030f,0.170f,10); mTilt(el,-PI_2);
    push(cu,at(el, -0.408f,hy0+0.120f,P_KICK)); }
  push(cu,at(mTorus(14,6,0.030f,0.014f), -0.408f,hy0+0.120f,P_KICK+0.05f));

  Mesh *mark=at(mPlate(0.216f,0.144f,0.018f,0.005f,0), U_4,5*U/4+0.108f,P_KICK);

  grp(P,take(&P->skin),T_SKIN,2,32);
  grp(P,take(&P->coat),T_COAT,2,34);
  grp(P,take(&P->mach),T_MACH,2,38);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,36);
  grp(P,cu,T_CU,2,40);
  grp(P,mark,T_MARK,3,40);
  grp(P,lid,T_ACC,3,34);
}
int clWall3(float a,float *o){ return bayClip(a,0.150f,B_W,o); }
int emWall3(float a,float *o){ (void)a; return bayEmit(B_W,o); }

/* ====================================================================
   K16  WALL 4  --  ASSEMBLED PANEL FIELD, 4U

   The connective tissue, and the part the kit most needed: WALL 1, 2 and 3
   are all EVENTS.  A corridor built out of nothing but events has no
   rhythm, because rhythm is what you notice against a background and there
   was no background -- every bay was as loud as the one beside it.

   This one is the background.  Identical bolted panels on a strict grid,
   one reveal between them, and nothing else: no rail, no bank, no bundle,
   no placard.  Lay six of these and one WALL 3 and the louvre bank finally
   reads as air intake rather than as more wall.
   ==================================================================== */
void mkWall4(float a,PB *P){
  float h=bayH(a);
  float IW=B_W4-2*B_STILE;
  bayShell(h,P,B_W4);

  /* The panel COUNT comes off the width, not the other way round, so the
     panel size is fixed at about 520 mm whatever the bay is -- which is
     what makes a 4.8 m field and a 1.2 m event bay read as the same wall
     rather than as the same wall drawn at two scales. */
  const float G=0.036f, PH=0.520f;
  int NC=(int)(IW/0.560f+0.5f); if(NC<2) NC=2;
  float PW=(IW-0.080f-G*(NC-1))/NC, y0=B_KICK+0.070f;
  for(int r=0;;r++){
    float yc=y0+PH/2+r*(PH+G);
    if(yc+PH/2 > h-B_HEAD-0.040f) break;
    if(fabsf(yc-B_LINE) < (PH+B_SLOT)/2+0.060f) continue;   /* clear the line */
    for(int c2=0;c2<NC;c2++){
      float xc=-IW/2+0.040f+PW/2+c2*(PW+G);
      push(P->gap,at(mPlate(PW+0.022f,PH+0.022f,0.038f,0.005f,0), xc,yc,0));
      grp(P,at(mPlate(PW,PH,P_PANEL,0.014f,0), xc,yc,0),T_ACC,3,34);
      for(int si=0;si<2;si++){                    /* two fixings a panel */
        float s=si?1.f:-1.f;
        Mesh *b=mBolt(0.019f,0.010f); mTilt(b,PI_2);
        push(P->mach,at(b, xc+s*(PW/2-0.040f), yc, P_PANEL));
      }
    }
  }
  /* one strap the full width, at the rail height every other bay uses, so
     the field still lines up with the event bays either side of it */
  push(P->mach,at(mPlate(B_W4,0.044f,P_FIELD+0.052f,0.009f,0), 0,B_RAIL_Y,0));

  grp(P,take(&P->skin),T_SKIN,2,32);
  grp(P,take(&P->coat),T_COAT,2,34);
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,34);
}
int clWall4(float a,float *o){ return bayClip(a,P_STILE+0.012f,B_W4,o); }
int emWall4(float a,float *o){ (void)a; return bayEmit(B_W4,o); }

/* ====================================================================
   K15  FILLET 1  --  the vertical splay, for where two walls meet

   The last right angle in the kit, and the cheapest part in it.  It is here
   because the rule has to be absolute to be worth having: a corridor whose
   deck and overhead junctions are splayed and whose corners are square
   looks like a corridor with a mistake in it.
   ==================================================================== */
void mkFillet1(float a,PB *P){
  float h=bayH(a);
  const float W=SPL*SQRT2;                        /* 0.424 across the flats */
  push(P->skin,mFace(at(mPlate(W,h,0.030f,0.008f,0),0,h/2,0), PI_4,0.150f));
  for(int si=0;si<2;si++){                        /* its two arrises */
    float s=si?1.f:-1.f;
    push(P->mach,mFace(at(mPlate(0.034f,h,0.040f,0.008f,0),
                          s*(W/2-0.017f),h/2,0), PI_4,0.150f));
  }
  /* the reveal behind it, so the fillet is a fitted piece and not a bevel */
  push(P->gap,mFace(at(mPlate(W+0.030f,h,0.008f,0.004f,0),0,h/2,0),
                    PI_4,0.128f));
  /* and it acknowledges 2U, because everything does */
  float ry[3]={B_LINE,B_LINE-U_2,B_LINE+U_2};
  for(int i=0;i<3;i++)
    if(ry[i]<h-0.10f)
      push(P->mach,mFace(at(mPlate(W-0.030f,0.038f,0.052f,0.008f,0),0,ry[i],0),
                         PI_4,0.150f));
  grp(P,take(&P->skin),T_SKIN,2,34);
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
}
int clFillet1(float a,float *o){
  float h=bayH(a);
  float c[6]={-0.212f,0,-0.212f, 0.212f,h,0.212f};
  memcpy(o,c,sizeof c); return 1;
}

/* ====================================================================
   K11  CORNER 1  --  OUTSIDE CORNER POST

     * IT TURNS.  Its section is an octagon, not a square, so walking round
       it the silhouette changes four times instead of once, and the two
       45-degree chamfers catch a lamp neither wall face can.
     * IT LIGHTS.  A full-height channel down each of the two corridor-
       facing flats, cut by the collar at 2.4 m where the bays' horizontal
       lines arrive.  A corridor built from this kit is lit almost entirely
       by its own corners.

   LOCAL FRAME.  The origin is the ARRIS -- the vertical line where the two
   wall face planes cross.  Wall A's face is the plane z=0 looking toward
   +Z; wall B's is x=0 looking toward +X; the solid is behind both.
   ==================================================================== */
#define C_A    U_4                 /* apothem, so U/2 = 600 mm across flats */
#define C_SHOE U_8
#define C_CAP  0.186f

/* a +Z-facing plate, turned onto one of the post's flats and pushed out to
   it.  d is the distance from the axis to the plate's BACK. */
static Mesh *onZ(Mesh *m,float d){ mTranslate(m,0,0,d); return m; }
static Mesh *onX(Mesh *m,float d){ mSpin(m,-PI_2); mTranslate(m,d,0,0); return m; }
static Mesh *onD(Mesh *m,float d){
  mSpin(m,-PI_4); mTranslate(m,d*0.7071068f,0,d*0.7071068f); return m;
}
static Mesh *onFlat(Mesh *m,int which,float d){
  return which? onX(m,d) : onZ(m,d);
}

static void taper(float *p,void *ctx){
  float H=*(float*)ctx;
  float t=p[1]/H;
  p[0]*=1-t*0.050f; p[2]*=1-t*0.050f;
}
void mkCorner1(float a,PB *P){
  float h=bayH(a);
  float y0=C_SHOE, y1=h-C_CAP;

  /* ---- the shaft.  Sprayed, not bare: the post has to be the darker
     thing so the machined work on it reads.  Tapered over its height, which
     is below the threshold at which anyone notices a taper and well above
     the one at which a column stops looking like an extrusion. */
  { float H=y1-y0;
    Mesh *sh=mOct(C_A,H);
    mDeform(sh,taper,&H);
    push(P->coat,at(sh,0,y0,0)); }

  /* ---- the shoe and the cap.  A post that meets the deck at the same
     width it has at head height is a pipe; a post with a foot is a post. */
  { Mesh *shoe=mOctCap(C_A+0.062f,C_SHOE);
    mSelNone(shoe); mSelNormal(shoe,0,1,0,0.20f,MSEL_SET);
    mExtrude(shoe,0.012f,0.048f);
    push(P->mach,shoe); }
  push(P->gap,at(mOctCap(C_A+0.008f,0.030f),0,C_SHOE-0.001f,0));
  { Mesh *cap=mOctCap(C_A+0.050f,C_CAP);
    mSelNone(cap); mSelNormal(cap,0,1,0,0.20f,MSEL_SET);
    mExtrude(cap,0.010f,0.052f);
    push(P->mach,at(cap,0,h-C_CAP,0)); }
  push(P->gap,at(mOctCap(C_A+0.006f,0.028f),0,h-C_CAP-0.027f,0));

  /* ---- the collar, where the horizontals arrive.  If the post's vertical
     channel ran straight past them the two would simply cross, and a
     crossing reads as an accident.  A machined collar at exactly that
     height is the junction: both lines die into it. */
  { Mesh *col=mOctCap(C_A+0.038f,0.176f);
    mSelNone(col); mSelNormal(col,0,1,0,0.20f,MSEL_SET);
    mExtrude(col,0.008f,0.044f);
    push(P->mach,at(col,0,B_LINE-0.088f,0)); }
  for(int ti=0;ti<2;ti++){
    float t=ti?1.f:-1.f;
    push(P->gap,at(mOctCap(C_A+0.004f,0.022f),0,B_LINE+t*0.110f-0.011f,0));
  }

  /* ---- the two light channels, one down each corridor, on the flats that
     are parallel to the wall planes -- so from inside either corridor the
     vertical line sits exactly on the axis the horizontal is running
     along. */
  float segs[2][2]={{y0+0.030f,B_LINE-0.088f},{B_LINE+0.088f,y1-0.030f}};
  for(int f=0;f<2;f++) for(int q=0;q<2;q++){
    float len=segs[q][1]-segs[q][0];
    if(len<0.15f) continue;
    float yc=(segs[q][0]+segs[q][1])/2;
    push(P->gap,onFlat(at(mPlate(0.150f,len,0.020f,0.005f,0),0,yc,0),f,0.232f));
    for(int si=0;si<2;si++){
      float s=si?1.f:-1.f;
      push(P->mach,onFlat(at(mPlate(0.052f,len,0.062f,0.010f,0),s*0.101f,yc,0),
                          f,0.238f));
    }
    for(int si=0;si<2;si++){                     /* and the ends, capped */
      float s=si?1.f:-1.f;
      push(P->mach,onFlat(at(mPlate(0.254f,0.048f,0.062f,0.010f,0),
                             0,segs[q][si]-s*0.024f,0), f,0.238f));
    }
  }

  /* ---- the nose.  The 45 degree flat is the only face you see from BOTH
     corridors at once, so it is the one that gets the writing, and a
     machined arris rib so the corner has a hard vertical edge. */
  push(P->mach,onD(at(mPlate(0.086f,y1-y0-0.060f,0.038f,0.010f,0),0,(y0+y1)/2,0),
                   C_A-0.004f));
  for(int si=0;si<2;si++){
    float s=si?1.f:-1.f;
    push(P->gap,onD(at(mPlate(0.020f,y1-y0-0.060f,0.014f,0.004f,0),
                       s*0.056f,(y0+y1)/2,0), C_A-0.006f));
  }
  Mesh *mark=onD(at(mPlate(0.180f,0.120f,0.020f,0.005f,0),0,1.500f,0),C_A+0.030f);

  /* ---- where the handrail dies.  WALL 1's rail runs the length of a
     corridor and has to STOP somewhere; stopping in mid-air is what makes a
     kit look like a kit. */
  for(int f=0;f<2;f++){
    push(P->mach,onFlat(at(mPlate(0.230f,U_8,0.048f,0.012f,0),0,B_RAIL_Y,0),
                        f,C_A-0.002f));
    push(P->gap ,onFlat(at(mPlate(0.196f,0.020f,0.020f,0.005f,0),
                           0,B_RAIL_Y-0.090f,0), f,C_A-0.004f));
  }

  /* ---- a jacketed drop, off-centre, so the post is not symmetric ------ */
  float dz = B_LINE-0.20f < h-1.2f ? B_LINE-0.20f : h-1.2f;
  push(P->duct,onD(at(mPipe(0.042f,dz-C_SHOE,10),0.148f,C_SHOE,0), C_A-0.030f));
  { float jy[2]={U_2,3*U/2};
    for(int i=0;i<2;i++) if(jy[i]<dz-0.10f)
      push(P->mach,onD(at(mPlate(0.120f,0.046f,0.052f,0.008f,0),0.148f,jy[i],0),
                       C_A-0.010f)); }

  /* ---- holding-down bolts -------------------------------------------- */
  for(int k=0;k<4;k++){
    float ang=k*PI_2+PI_4;
    push(P->mach,at(mBolt(0.030f,0.015f),
                    cosf(ang)*0.272f,C_SHOE,sinf(ang)*0.272f));
  }

  grp(P,take(&P->coat),T_COAT,2,30);
  grp(P,take(&P->mach),T_MACH,2,34);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,36);
  grp(P,mark,T_MARK,3,40);
}
/* The body stops short of both corridor flats, because the bake finds an
   emitter by ray-casting the BRUSH set: a diffuser sealed inside a clip box
   lights nothing.  What is left open is the 140 mm the channel itself
   occupies -- and the diffuser brush stands in it and stops you anyway. */
int clCorner1(float a,float *o){
  float h=bayH(a);
  float c[5][6]={
    {-C_A,  0,-C_A,    0.220f,h, 0.220f},
    { 0.220f,0,-C_A,   C_A,   h,-0.075f},
    { 0.220f,0, 0.075f,C_A,   h, 0.220f},
    {-C_A,  0, 0.220f,-0.075f,h, C_A},
    { 0.075f,0, 0.220f,0.220f,h, C_A}};
  memcpy(o,c,sizeof c); return 5;
}
int emCorner1(float a,float *o){
  float h=bayH(a);
  float y0=C_SHOE+0.030f, y1=h-C_CAP-0.030f;
  float q[2][2]={{y0,B_LINE-0.088f},{B_LINE+0.088f,y1}};
  int n=0;
  for(int i=0;i<2;i++){
    if(q[i][1]-q[i][0]<0.15f) continue;
    float a1[7]={-0.072f,q[i][0],0.242f,  0.072f,q[i][1],0.260f, T_CHAN};
    float a2[7]={ 0.242f,q[i][0],-0.072f, 0.260f,q[i][1],0.072f, T_CHAN};
    memcpy(o+n*7,a1,sizeof a1); n++;
    memcpy(o+n*7,a2,sizeof a2); n++;
  }
  return n;
}
