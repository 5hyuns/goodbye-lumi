/* ============================================================
   THE PARTS.  kit.c holds the machine; this file holds the
   twenty-one things it makes.  Split because the machine is
   stable and the parts are not: a part is reviewed one at a
   time on the bench, and a file that changes every time a
   chamfer changes should not also hold the placement loop.

   A hole in the registry is a part not yet ported: nothing is
   drawn there and the shell still stands.
   ============================================================ */
#include "kit.h"
#include "kitwalls.h"

/* ====================================================================
   THE DECK MODULE

     placement module   U      1.200   a wall bay, so floor and wall share
                                       one rhythm
     panel module       U/2    0.600   four to a tile
     seam               U/30   0.040   the SAME everywhere
     panel              0.560          = 0.600 - 0.040

   A panel's outer edge lands 20 mm inside the tile edge and the neighbour
   contributes the other 20, so the seam at the join is 40 mm, exactly like
   the seam down the middle.  Nothing is drawn at 1.20 m, so there is no
   tile grid to find -- only a 600 mm panel grid, which is the size a floor
   panel actually is in every data centre and every ship afloat.  It is the
   one dimension a player has already stood on without knowing it.
   ==================================================================== */
#define DECK_S  (U/30.f)                     /* 0.040 -- the seam, everywhere */
#define DECK_PW (U_2-DECK_S)                 /* 0.560 -- so the panel itself */

static const float DECK_AT[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
static void deckCell(int k,float *c){
  c[0]=DECK_AT[k][0]*U_4; c[1]=DECK_AT[k][1]*U_4;
}

/* Every floor part starts here: the frame slab and the black reveal each
   panel sits in.  Sharing it is the point -- FLOOR 1, 2 and 3 can be laid
   next to each other in any order and the seam never jumps. */
static void deckFrame(PB *P,int cells){
  push(P->mach,mSlab(U,U,0.024f,0.003f,1));    /* flat at its rim, on purpose */
  if(cells) for(int k=0;k<4;k++){
    float c[2]; deckCell(k,c);
    push(P->gap,at(mSlab(DECK_PW+0.024f,DECK_PW+0.024f,0.036f,0.005f,0),
                   c[0],0,c[1]));
  }
  /* The machined land down the middle of every seam, and it has to stand
     ABOVE the black or it does not exist: at 24 mm under a 36 mm reveal the
     land sat in permanent shadow and the seam came out as one flat black
     band instead of the three-part section it is drawn as.  4 mm proud is
     enough.  Each tile owns the bar on its -x and -z boundary and the
     neighbour owns the other two, so a field has one bar per seam. */
  if(cells) push(P->mach,mSlab(0.016f,U,0.040f,0.004f,0));
  push(P->mach,at(mSlab(0.016f,U,0.040f,0.004f,0), -U_2,0,0));
  if(cells) push(P->mach,mSlab(U,0.016f,0.040f,0.004f,0));
  push(P->mach,at(mSlab(U,0.016f,0.040f,0.004f,0), 0,0,-U_2));
}

/* ====================================================================
   K2  FLOOR 1  --  ACCESS DECK, one module

   ONE PLATE, NOT FOUR.  Four 560 mm cells in a 1.2 m tile gave the deck a
   600 mm grain, and at standing height that is small enough that the eye
   stops reading plates and starts reading pattern -- the floor turned into
   a chequer.  One 1.12 m plate per module reads as a PLATE: you can see
   its four fixings, you can see it is a thing somebody bolted down, and
   the module grid is still there in the seam because the seam is what the
   frame draws.
   ==================================================================== */
static void mkFloor1(float a,PB *P){
  int v=(int)a;
  deckFrame(P,0);                     /* no cell reveals: there is one cell */
  push(P->coat,mSlab(U-2*DECK_S,U-2*DECK_S,0.060f,0.016f,0));
  push(P->gap ,mSlab(U-0.020f,U-0.020f,0.036f,0.005f,0));
  /* The fixings are at the four CORNERS.  They were at the mid-edges, on
     the argument that a corner bolt fouls its neighbour's -- true of a
     plate that butts, false of one sitting in a rebate with a 40 mm reveal
     all round, which is what this is.  Four bolts on the diagonal say
     "lift here"; four on the axes say nothing at all. */
  for(int d=0;d<4;d++)
    push(P->mach,at(mBolt(0.028f,0.014f),
                    DECK_AT[d][0]*(U/2-0.124f),0.060f,
                    DECK_AT[d][1]*(U/2-0.124f)));
  /* The flush pull.  A deck panel lifted by proud bolt heads is a 1950s
     engine room; one lifted by a slot you can only find from directly above
     is a ship.  One per tile, and its cell and its direction both come off
     the variant, so no two neighbours are the same way up. */
  { float c[2]; deckCell(v&3,c);
    Mesh *sm=at(mSlab(0.168f,0.052f,0.054f,0.006f,0), c[0]*0.72f,0,c[1]*0.72f);
    Mesh *lm=at(mSlab(0.196f,0.078f,0.057f,0.008f,0), c[0]*0.72f,0,c[1]*0.72f);
    if(v&1){ mSpin(sm,PI_2); mSpin(lm,PI_2); }
    push(P->gap,sm); push(P->mach,lm); }
  /* The lit seam.  Half the tiles get it, so a corridor has a line down its
     floor that stops and starts instead of running like a runway -- and the
     strip is stamped as a real brush (see emFloor1), so the deck actually
     throws light up the walls beside it. */
  if(v&1) push(P->gap,mSlab(U-0.06f,0.030f,0.030f,0.004f,0));
  pbFinish(P,34,40,32,32,36);
}
/* AMBER, not the wall's blue-white.  Warm on the deck against cool on the
   wall is the contrast the whole corridor is lit by, and it is the one
   thing the deck reference does that no amount of panel work can. */
static int emFloor1(float a,float *o){
  int v=(int)a;
  if(!(v&1)) return 0;
  static const float e[7]={-U/2+0.032f,0.024f,-0.010f,
                            U/2-0.032f,0.042f, 0.010f, T_GLOW};
  memcpy(o,e,sizeof e); return 1;
}

/* ====================================================================
   K3  FLOOR 2  --  VENT DECK, one module, four 600 mm cells

   The opposite argument to FLOOR 1.  A panel closes the floor; a vent
   opens it, and what you see through it is worth more than any surface.
   It keeps FLOOR 1's frame and its 600 mm module exactly, which is the
   entire reason it is worth having: swap one tile in eight for this and
   the floor gains a hole without gaining a second grid.

   The blades are ON EDGE, and that is the whole effect: from directly
   above the deck is nearly closed, from four metres away it is nearly
   open, so the floor CHANGES as you cross it in a way no texture on a
   solid panel can imitate.
   ==================================================================== */
static void mkFloor2(float a,PB *P){
  (void)a;
  const float H=0.048f;
  Mesh *CU=mNew();
  deckFrame(P,1);
  for(int k=0;k<4;k++){
    float c[2]; deckCell(k,c);
    const float W=DECK_PW;
    /* the cell's kerb: four bars, so the vent is still a panel-shaped thing
       and lines up with the panels either side of it */
    for(int si=0;si<2;si++){
      float s=si?1.f:-1.f;
      push(P->mach,at(mSlab(W,0.028f,H,0.008f,0), c[0],0,c[1]+s*(W-0.028f)/2));
      push(P->mach,at(mSlab(0.028f,W-0.056f,H,0.008f,0),
                      c[0]+s*(W-0.028f)/2,0,c[1]));
    }
    float span=W-0.056f, pitch=span/9;
    for(int i=0;i<9;i++)                              /* the blades */
      push(P->mach,at(mBox(span,H-0.016f,0.009f),
                      c[0],(H-0.016f)/2+0.005f,c[1]-span/2+pitch*(i+0.5f)));
    for(int j=0;j<2;j++)                              /* and their ties */
      push(P->mach,at(mBox(0.010f,0.012f,span),
                      c[0]-span/4+span/2*j,H-0.020f,c[1]));
  }
  /* Shallow on purpose: it reads through the blades even where the deck
     below is only 200 mm down, and buries harmlessly where it is not.  One
     copper among the black, because a bundle in which every run is the same
     is a bundle nobody routed. */
  { Mesh *p=mPipe(0.032f,U+0.10f,10); mRoll(p,PI_2);
    push(CU,at(p,-(U+0.10f)/2,-0.108f,U_4-0.06f)); }
  { Mesh *p=mPipe(0.048f,U+0.10f,10); mRoll(p,PI_2);
    push(P->duct,at(p,-(U+0.10f)/2,-0.124f,-U_4+0.02f)); }
  { Mesh *p=mPipe(0.028f,U+0.10f,10); mRoll(p,PI_2);
    push(P->duct,at(p,-(U+0.10f)/2,-0.100f,-U_4+0.10f)); }
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,CU,T_CU,2,36);
  grp(P,take(&P->duct),T_DUCT,2,36);
}
static int clFloor2(float a,float *o){
  (void)a;
  static const float c[6]={-U/2,0,-U/2, U/2,0.048f,U/2};
  memcpy(o,c,sizeof c); return 1;
}

/* ====================================================================
   K12  CEILING 1  --  OVERHEAD, one module, a lit run and a return

   The third surface, and the one the kit had nothing for at all.  A
   corridor whose deck and walls are modelled and whose overhead is a
   painted brush is a corridor with a lid on it: the eye reads the two good
   surfaces, finds the third has no scale in it, and settles on the whole
   room being a box after all.

   THE LIT RUN.  A 240 mm channel down the full length of the tile, two
   blades hanging either side of it, and the diffuser at the top where you
   cannot see it.  Because it runs edge to edge, a column of these makes
   ONE unbroken line the length of the corridor -- which is the single
   thing every reference overhead is drawn with, and the thing that gives a
   passage its perspective.
   ==================================================================== */
#define C_SLOT (U/5)                     /* 0.240 -- the lit channel */
#define C_LIP  0.036f                    /* and the blades either side */
#define C_PANW 0.400f
#define C_PAND (U-0.200f)

static void mkCeiling1(float a,PB *P){
  int v=(int)a;
  /* the soffit itself: rolled plate.  Nobody has ever touched a ceiling,
     and it is the one surface in the kit with no reason to be sprayed. */
  push(P->skin,mSoffit(U,U,0.024f,0.003f,1));
  /* the seam lands: this tile owns its -x and -z boundary */
  push(P->mach,at(mSoffit(0.016f,U,0.040f,0.004f,0), -U_2,0,0));
  push(P->mach,at(mSoffit(U,0.016f,0.040f,0.004f,0), 0,0,-U_2));

  if(v&1){
    push(P->gap,mSoffit(C_SLOT,U,0.010f,0.003f,0));      /* the channel floor */
    for(int si=0;si<2;si++){                             /* and its blades */
      float s=si?1.f:-1.f;
      push(P->mach,at(mSoffit(C_LIP,U,0.060f,0.008f,0),
                      s*(C_SLOT/2+C_LIP/2),0,0));
    }
  } else {
    /* unlit tiles close the channel with a plain strip, so the run reads as
       a fitting that is OUT rather than as a hole in the ceiling */
    push(P->coat,mSoffit(C_SLOT+2*C_LIP,U,0.048f,0.008f,0));
  }

  /* the two panels either side: a return-air louvre or a plain sprayed lid,
     both of them PICTURES on a plate that already exists */
  Mesh *pic[2]={0,0};
  for(int k=0;k<2;k++){
    float x=(k?1.f:-1.f)*(C_SLOT/2+C_LIP+0.022f+C_PANW/2);
    push(P->gap,at(mSoffit(C_PANW+0.024f,C_PAND+0.024f,0.036f,0.005f,0),x,0,0));
    Mesh *pl=at(mSoffit(C_PANW,C_PAND,0.060f,0.012f,0), x,0,0);
    if((v>>(1+k))&1) pic[k]=pl; else push(P->coat,pl);
  }
  grp(P,take(&P->skin),T_SKIN,2,32);
  grp(P,take(&P->mach),T_MACH,2,34);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->coat),T_COAT,2,32);
  for(int k=0;k<2;k++) if(pic[k]) grp(P,pic[k],T_LOUV,3,32);
}
/* The diffuser sits 20 mm up inside the channel, so what you see is the LIT
   SLOT between two blades and never the source -- the wall's light line,
   turned through ninety degrees, which is why they read as one building
   rather than as a wall kit and a ceiling kit. */
static int emCeiling1(float a,float *o){
  int v=(int)a;
  if(!(v&1)) return 0;
  static const float e[7]={-C_SLOT/2+0.014f,-0.042f,-U/2,
                            C_SLOT/2-0.014f,-0.026f, U/2, T_CHAN};
  memcpy(o,e,sizeof e); return 1;
}

/* ====================================================================
   K13  SKIRT 1  --  the deck-to-wall splay, 1U long

   The junction the whole corridor is read by, and the reason the reference
   images have a warm line along the bottom of both walls:

     - the splay face leans out over the deck, 456 mm of 45 degree plate,
       so it catches the overhead lamp at grazing incidence and is the
       brightest horizontal thing in the frame;
     - it stops short of the deck and stands on a recessed plinth, which
       leaves a slot;
     - the amber strip lives in that slot, and what you see is not the strip
       but a POOL ON THE DECK the length of the corridor.

   That is the entire lighting design of the reference in one part.
   ==================================================================== */
static void mkSkirt1(float a,PB *P){
  (void)a;
  /* the plinth the splay stands on, set back so the splay overhangs it */
  push(P->gap ,at(mPlate(U,0.144f,0.488f,0.010f,0), 0,0.072f,0));
  push(P->mach,at(mSlab(U,0.052f,0.014f,0.006f,0), 0,0,0.516f));  /* deck toe */
  /* the splay itself, and the machined nose along its top edge where it
     dies into the wall -- the nose is the bright line, not the face */
  Mesh *sk=mSplayA(U,0.144f,SPL,0.456f,0.036f,0.010f,0);
  push(P->mach,at(mBox(U,0.048f,0.076f), 0,SPL,0.106f));
  /* a service run tucked in the angle, because the angle is the one place
     on a ship where a pipe is out of everybody's way -- and at 600 mm of
     splay there is finally room for a main rather than a conduit */
  { Mesh *p=mPipe(0.082f,U,14); mRoll(p,PI_2);
    push(P->duct,at(p,-U/2,0.190f,0.300f)); }
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,36);
  /* The face is a louvre PICTURE, not modelled slots -- a skirt at ankle
     height is never seen closely enough for parallax to matter, and this is
     exactly the trade the plate set exists to make. */
  grp(P,sk,T_LOUV,3,34);
}
/* NOTCHED AT THE STRIP.  The bake ray-casts the clip set before it looks
   for an emitter, so a strip inside a clip box lights nothing at all -- and
   because the brush still draws, the part looks correct and the corridor
   stays dark.  y 0.044..0.108 is left open for it. */
static int clSkirt1(float a,float *o){
  (void)a;
  static const float c[3][6]={
    {-U/2,0,     0, U/2,0.044f,SPL},
    {-U/2,0.108f,0, U/2,0.144f,SPL},
    {-U/2,0.144f,0, U/2,SPL,   0.352f}};
  memcpy(o,c,sizeof c); return 3;
}
static int emSkirt1(float a,float *o){
  (void)a;
  static const float e[7]={-U/2+0.024f,0.052f,0.470f,
                            U/2-0.024f,0.100f,0.546f, T_GLOW};
  memcpy(o,e,sizeof e); return 1;
}

/* ====================================================================
   K14  COVE 1  --  the wall-to-overhead splay, 1U long

   The same junction, upside down, and it does the opposite job: the skirt
   throws light DOWN onto the deck and this one runs the cool line the
   length of the corridor at the top of the wall.  Warm below, cool above --
   the accent rule -- except that now both are on a 45 degree face, so both
   are visible from the whole length of the passage instead of only from
   directly in front.

   It also carries the services.  Every ship has to put its cable runs
   somewhere, and the top angle is where a real one puts them: out of reach,
   out of the walkway, and inspectable from a ladder.
   ==================================================================== */
static void mkCove1(float a,PB *P){
  (void)a;
  /* the channel at the foot of the cove: a black recess, a machined lip
     under it, and the diffuser lying in it */
  push(P->gap ,at(mPlate(U,0.106f,0.086f,0.010f,0), 0,0.080f,0.100f));
  push(P->mach,at(mBox(U,0.048f,0.104f), 0,0.028f,0.136f));
  Mesh *cv=mSplayB(U,0.144f,0.128f,0.456f,0.032f,0.010f,0);
  push(P->mach,at(mBox(U,0.050f,0.084f), 0,SPL,0.562f));     /* its top nose */
  /* the loom, and it is the biggest thing in the angle on purpose: a
     corridor whose services you cannot see is a corridor nobody built */
  for(int i=0;i<3;i++){
    Mesh *p=mPipe(0.092f-i*0.018f,U,14); mRoll(p,PI_2);
    push(P->duct,at(p,-U/2,0.252f+i*0.112f,0.230f+i*0.108f));
  }
  for(int j=0;j<2;j++){                        /* and the straps clipping it */
    float x=j?U_4:-U_4;
    push(P->mach,at(mSplayB(0.076f,0.196f,0.180f,0.392f,0.104f,0.010f,0),
                    x,0,0));
  }
  grp(P,take(&P->mach),T_MACH,2,36);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->duct),T_DUCT,2,36);
  grp(P,cv,T_COAT,3,34);
}
static int clCove1(float a,float *o){
  (void)a;
  static const float c[2][6]={
    {-U/2,0,     0, U/2,0.050f,SPL},
    {-U/2,0.116f,0, U/2,SPL,   SPL}};
  memcpy(o,c,sizeof c); return 2;
}
static int emCove1(float a,float *o){
  (void)a;
  static const float e[7]={-U/2+0.024f,0.056f,0.112f,
                            U/2-0.024f,0.106f,0.172f, T_CHAN};
  memcpy(o,e,sizeof e); return 1;
}

/* ====================================================================
   K17  FLOOR 4  --  DECK MARGIN, one module

   A deck is not one material with things sprinkled on it.  It is a margin,
   a field and a band, and you can read all three off any ship photograph
   ever taken: the margin is where the water, the swarf and the cable go,
   so it is grated and it is dark; the field is where you walk, so it is
   plain and light; the band is where a service crosses, so it happens at
   intervals and never in between.

   This is the margin.  Its outer half is a drain channel with a machined
   kerb and its inner half is ordinary field plate, so the transition
   happens INSIDE one module and the two zones never need a trim piece.
   ==================================================================== */
static void mkFloor4(float a,PB *P){
  (void)a;
  deckFrame(P,1);
  Mesh *grate=mNew();
  for(int k=0;k<4;k++){
    float c[2]; deckCell(k,c);
    if(DECK_AT[k][0]<0){                       /* the two cells outboard */
      /* the channel: a deep black pan, a grate over it, a kerb inboard */
      push(P->gap,at(mSlab(DECK_PW,DECK_PW,0.018f,0.004f,0), c[0],0,c[1]));
      push(grate,at(mSlab(DECK_PW-0.052f,DECK_PW-0.052f,0.044f,0.008f,0),
                    c[0],0,c[1]));
      push(P->mach,at(mSlab(0.030f,DECK_PW,0.062f,0.006f,0),
                      c[0]+DECK_PW/2-0.015f,0,c[1]));
    } else {
      push(P->coat,at(mSlab(DECK_PW,DECK_PW,0.060f,0.012f,0), c[0],0,c[1]));
    }
  }
  pbFinish(P,34,40,32,32,36);
  /* An open deck cell costs one material index where cutting its 256 holes
     costs 2,000 triangles, and there are ninety tiles. */
  grp(P,grate,T_PERF,3,32);
}

/* ====================================================================
   K18  BAND 1  --  SAFETY BAND, 1U of it

   A hazard band painted onto the deck plate was the wrong answer twice
   over.  It is not paint on this ship -- it is a bolted-down nosing, the
   thing you actually find at the edge of a hatch or in front of a vent,
   and it stands 60 mm proud with a machined kerb down each side.

   It also fixes the picture.  A band drawn onto a floor tile is a strip cut
   out of a world grid, and it landed as flat colour; a band that is its own
   part owns its own bounding box, which is what a picture needs.  The block
   is 1.2 x 0.3, so the square-cell rule gives it four cells of four bars
   each: 75 mm bars, which is what a real one is.
   ==================================================================== */
static void mkBand1(float a,PB *P){
  (void)a;
  push(P->mach,at(mSlab(U,0.036f,0.072f,0.008f,0), 0,0,-0.132f));
  push(P->mach,at(mSlab(U,0.036f,0.072f,0.008f,0), 0,0, 0.132f));
  Mesh *top=mSlab(U-0.006f,0.228f,0.066f,0.010f,0);
  grp(P,take(&P->mach),T_MACH,2,34);
  grp(P,top,T_SAFE,3,34);
}


/* ====================================================================
   K8  COLUMN 1  --  STANCHION, 600 mm base on a 300 mm section

   Doom 3's kit worked because its parts were in whole ratios to each
   other, and this is the wall bay's ratio made vertical: a 300 mm shaft
   is U/4, its base is U/2, and its collar lands at 2U where every bay in
   the ship runs its light line.  The section is an I, not a box -- a web
   between two flanges -- because that is what a stanchion actually is,
   and because the slot down the middle of each flange is somewhere to put
   the light.
   ==================================================================== */
#define K_LINE  (2*U)
#define K_U8    U_8

static void mkColumn1(float a,PB *P){
  float h = a>0.01f ? a : WALL_H;
  float y0=0.048f, y1=h-0.048f, hs=y1-y0;
  const float FT=0.048f;
  float run[LITRUN_MAX*2];

  push(P->mach,mSlab(U_2,U_2,y0,0.014f,0));                        /* base */
  push(P->mach,at(mSlab(0.480f,0.480f,0.048f,0.014f,0),0,h-0.048f,0));/* cap */
  push(P->skin,at(mBox(0.048f,hs,U_4-2*FT), 0,y0+hs/2,0));         /* the web */

  int nr=litRun(0.240f,h-K_U8-0.036f,0.036f,run);
  for(int si=0;si<2;si++){
    float s=si?1.f:-1.f;
    float th = s>0 ? PI_2 : 4.7123890f, zf=K_U8-FT;
    /* the flange, as two machined strips with a slot down the middle */
    for(int ti=0;ti<2;ti++){
      float t=ti?1.f:-1.f;
      push(P->mach,mFace(at(mPlate(0.118f,hs,FT,0.014f,0),t*0.091f,y0+hs/2,0),
                         th,zf));
    }
    /* 64 mm wide and 40 mm deep.  It was 112 and 24, and at that size it
       stopped being a slot with light in it and became a light panel:
       0.61 m2 of emitter on a 300 mm column, which is eleven times what a
       whole wall bay puts out. */
    push(P->gap,mFace(at(mPlate(0.064f,hs,0.020f,0.005f,0),0,y0+hs/2,0),th,zf));
    for(int i=0;i<nr;i++){
      float y=run[i*2+1]+0.036f;
      if(y<h-K_U8-0.05f && fabsf(y-K_LINE)>0.16f)
        push(P->mach,mFace(at(mPlate(0.108f,0.056f,FT+0.008f,0.010f,0),0,y,0),
                           th,zf));
    }
    static const float fy[3]={0.42f,1.32f,3.06f};
    for(int i=0;i<3;i++){                                  /* and its fixings */
      float y=fy[i];
      if(y>h-0.30f) continue;
      if(fabsf(y-K_LINE)<0.20f) continue;
      for(int ti=0;ti<2;ti++){
        float t=ti?1.f:-1.f;
        Mesh *b=mBolt(0.019f,0.010f); mTilt(b,PI_2);
        push(P->mach,mFace(at(b,t*0.102f,y,0), th,K_U8));
      }
    }
  }
  /* the collar, at 2U, where every bay in the ship runs its light line */
  push(P->mach,at(mSlab(0.360f,0.360f,0.216f,0.018f,0), 0,K_LINE-0.108f,0));
  for(int ti=0;ti<2;ti++){
    float t=ti?1.f:-1.f;
    push(P->gap,at(mSlab(0.312f,0.312f,0.024f,0.005f,0),0,K_LINE+t*0.132f-0.012f,0));
  }
  push(P->duct,at(mSlab(0.336f,0.336f,K_U8,0.014f,0), 0,y0,0));   /* kick wrap */
  push(P->mach,at(mSlab(0.360f,0.360f,K_U8+0.024f,0.012f,0), 0,y0,0));
  for(int d=0;d<4;d++)                                            /* holding down */
    push(P->mach,at(mBolt(0.028f,0.014f),
                    DECK_AT[d][0]*0.228f,y0,DECK_AT[d][1]*0.228f));
  for(int si=0;si<2;si++){                                        /* gussets */
    float s=si?1.f:-1.f;
    Mesh *g=mBox(0.022f,U_4,U_4);
    for(int vi=0;vi<g->nV;vi++){
      float t=(g->V[vi*3+1]+K_U8)/U_4;
      g->V[vi*3+2]*=(1.0f-t*0.86f);
    }
    push(P->coat,at(g, s*0.086f, y0+K_U8+K_U8, 0));
  }
  Mesh *mark=mFace(at(mPlate(0.170f,0.112f,0.018f,0.005f,0),0,1.500f,0),
                   PI_2,K_U8);
  grp(P,take(&P->mach),T_MACH,2,34);
  grp(P,take(&P->gap ),T_GAP ,2,40);
  grp(P,take(&P->skin),T_SKIN,2,34);
  grp(P,take(&P->coat),T_COAT,2,32);
  grp(P,take(&P->duct),T_DUCT,2,36);
  grp(P,mark,T_MARK,3,40);
}
/* the core stops 24 mm short of both flange faces, and the flange strips
   close it back up either side of the channel -- so what stays open is the
   108 mm the channel occupies, which the diffuser fills */
static int clColumn1(float a,float *o){
  float h = a>0.01f ? a : WALL_H;
  float c[6][6]={
    {-K_U8,0,-0.126f,  K_U8,h, 0.126f},
    {-K_U8,0, 0.126f,-0.054f,h, K_U8},{ 0.054f,0, 0.126f, K_U8,h, K_U8},
    {-K_U8,0,-K_U8,  -0.054f,h,-0.126f},{ 0.054f,0,-K_U8,  K_U8,h,-0.126f},
    {-U_4,0,-U_4, U_4,0.06f,U_4}};
  memcpy(o,c,sizeof c); return 6;
}
static int emColumn1(float a,float *o){
  float h = a>0.01f ? a : WALL_H;
  float run[LITRUN_MAX*2];
  int nr=litRun(0.240f,h-K_U8-0.036f,0.036f,run), n=0;
  for(int i=0;i<nr;i++){
    if(fabsf((run[i*2]+run[i*2+1])/2-K_LINE)<0.16f) continue;  /* the collar */
    for(int si=0;si<2;si++){
      float s=si?1.f:-1.f;
      float e[7]={-0.024f,run[i*2],  s*0.150f-s*0.028f,
                   0.024f,run[i*2+1],s*0.150f-s*0.014f, T_CHAN};
      memcpy(o+n*7,e,sizeof e); n++;
    }
  }
  return n;
}

/* ====================================================================
   K4  FLOOR 3  --  ACCESS HATCH, one module, the hatch is ONE PANEL

   Not a third floor so much as the punctuation for the other two.  A deck
   of identical panels has a rhythm and no sentence; one hatch every dozen
   tiles says the deck is a lid over machinery and gives the eye somewhere
   to stop.  It is also a promise the level can keep later: a hatch is a
   place something can come out of.

   THE HATCH IS EXACTLY ONE 600 mm PANEL.  Not a medallion dropped onto the
   floor -- one cell of the same grid, with the other three still ordinary
   panels.  That is what makes it belong: you can see, without being told,
   that somebody lifted one panel out and put a hatch in its place.
   ==================================================================== */
static void mkFloor3(float a,PB *P){
  int v=(int)a;
  int hk=v&3;
  deckFrame(P,1);
  for(int k=0;k<4;k++){
    float c[2]; deckCell(k,c);
    if(k!=hk){                                    /* three ordinary panels */
      push(P->coat,at(mSlab(DECK_PW,DECK_PW,0.060f,0.012f,0), c[0],0,c[1]));
      continue;
    }
    push(P->mach,at(mOctCap(0.278f,0.038f), c[0],0,c[1]));      /* the seat */
    push(P->gap ,at(mOctCap(0.258f,0.046f), c[0],0,c[1]));      /* the rebate */
    push(P->duct,at(mOctCap(0.250f,0.050f), c[0],0,c[1]));      /* the gasket */

    Mesh *lid=mOctCap(0.244f,0.060f);                           /* and the lid */
    mSelNone(lid); mSelNormal(lid,0,1,0,0.20f,MSEL_SET);
    mExtrude(lid,0.006f,0.026f);                                /* rolled, flush */
    push(P->coat,at(lid, c[0],0,c[1]));

    /* the release: a disc set INTO the lid, never standing on it */
    { Mesh *d=mCylinder(12,1); mScale(d,0.212f,0.052f,0.212f);
      push(P->gap,at(d, c[0],0.026f,c[1])); }
    { Mesh *d=mCylinder(12,1); mScale(d,0.180f,0.056f,0.180f);
      push(P->mach,at(d, c[0],0.020f,c[1])); }
    for(int j=0;j<3;j++){                                       /* three keys */
      Mesh *kk=mBox(0.140f,0.018f,0.024f); mSpin(kk,j*2.0943951f);
      push(P->mach,at(kk, c[0],0.058f,c[1]));
    }
    { Mesh *d=mCylinder(10,1); mScale(d,0.056f,0.064f,0.056f);
      push(P->gap,at(d, c[0],0.018f,c[1])); }

    /* the four status lights, in the seat, one to each cardinal */
    for(int si=0;si<2;si++){
      float s=si?1.f:-1.f;
      push(P->gap,at(mSlab(0.086f,0.030f,0.044f,0.005f,0), c[0],0,c[1]+s*0.268f));
      push(P->gap,at(mSlab(0.030f,0.086f,0.044f,0.005f,0), c[0]+s*0.268f,0,c[1]));
    }
  }
  pbFinish(P,34,40,32,32,36);
}
static int emFloor3(float a,float *o){
  int v=(int)a;
  float c[2]; deckCell(v&3,c);
  const float r=0.268f;
  int n=0;
  for(int si=0;si<2;si++){
    float s=si?1.f:-1.f;
    float e1[7]={c[0]-0.034f,0.036f,c[1]+s*r-0.011f,
                 c[0]+0.034f,0.047f,c[1]+s*r+0.011f, T_CHAN};
    float e2[7]={c[0]+s*r-0.011f,0.036f,c[1]-0.034f,
                 c[0]+s*r+0.011f,0.047f,c[1]+0.034f, T_CHAN};
    memcpy(o+n*7,e1,sizeof e1); n++;
    memcpy(o+n*7,e2,sizeof e2); n++;
  }
  return n;
}

/* ---- the registry ---------------------------------------------------
   MODULES rows are [kit, x,y,z, yaw, arg, material].  `kit` indexes this
   list, so the numbers the Python chain already generated keep meaning
   what they meant. */
/* C will not let one static initialiser copy another, so the wall rows
   arrive as macros over the same four fields. */
#define KIT_WALL1_ROW   {"WALL 1",  mkWall1,  clWall1,  emWall1}
#define KIT_WALL2_ROW   {"WALL 2",  mkWall2,  clWall2,  emWall2}
#define KIT_WALL3_ROW   {"WALL 3",  mkWall3,  clWall3,  emWall3}
#define KIT_WALL4_ROW   {"WALL 4",  mkWall4,  clWall4,  emWall4}
#define KIT_FILLET1_ROW {"FILLET 1",mkFillet1,clFillet1,0}
#define KIT_CORNER1_ROW {"CORNER 1",mkCorner1,clCorner1,emCorner1}

const KitDef KIT[]={
  {"reserved 0",    0,          0,        0},
  {"reserved 1",    0,          0,        0},
  {"FLOOR 1",       mkFloor1,   0,        emFloor1},
  {"FLOOR 2",       mkFloor2,   clFloor2, 0},
  {"FLOOR 3",       mkFloor3,   0,        emFloor3},
  KIT_WALL1_ROW,
  KIT_WALL2_ROW,
  KIT_WALL3_ROW,
  {"COLUMN 1",      mkColumn1,  clColumn1,emColumn1},
  {"COLUMN 2",      0,          0,        0},
  {"COLUMN 3",      0,          0,        0},
  KIT_CORNER1_ROW,
  {"CEILING 1",     mkCeiling1, 0,        emCeiling1},
  {"SKIRT 1",       mkSkirt1,   clSkirt1, emSkirt1},
  {"COVE 1",        mkCove1,    clCove1,  emCove1},
  KIT_FILLET1_ROW,
  KIT_WALL4_ROW,
  {"FLOOR 4",       mkFloor4,   0,        0},
  {"BAND 1",        mkBand1,    0,        0},
  {"WALL 5",        0,          0,        0},
  {"SKYLIGHT 1",    0,          0,        0},
};
const int NKIT = (int)(sizeof KIT / sizeof KIT[0]);
