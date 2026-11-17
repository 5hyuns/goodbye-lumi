/* ============================================================
   M2.  THE KIT
   Not "level geometry".  A short list of *parts*, each one modelled
   once with the operator machine and then instanced wherever the plan
   says it goes.

   A brush is an axis-aligned box.  A map built out of brushes can only
   ever be boxes, and no texture rescues that -- the silhouette is what
   the eye reads first, and a box has one silhouette.  A part, on the
   other hand, is a mesh graph: it can be chamfered, recessed, bolted,
   ribbed and creased, and it carries that shape into every one of the
   hundreds of places it is stamped.

   Rules for anything that goes in here:

     * TRUE SCALE.  Modelled in metres, in its own space, standing on
       y=0 with the origin at the mounting point.  Never rescaled to
       fit -- if it does not fit, the part is wrong, not the room.
     * NO PRIMITIVE MAY READ AS ITSELF.  A cube that still looks like a
       cube has not been worked.  Chamfer it, recess it, break its
       symmetry, or it is not a part.
     * ONE UV RULE.  proj 2 unwraps from the *world* normal by arc
       length, exactly as the mesher does for a brush face, so a part
       continues the texture of what it is bolted to instead of
       restarting its own run.
     * DECLARE THE CLIP.  A part is triangles, and triangles do not
       collide.  Whatever the player must not walk through is listed
       as boxes in `clip` and stamped as real brushes alongside it.
   ============================================================ */
const TRIS=[];                      // world triangles, baked like everything else
function mScale(m,sx,sy,sz){ for(let i=0;i<m.V.length;i+=3){ m.V[i]*=sx; m.V[i+1]*=sy; m.V[i+2]*=sz; } return m; }
function mMove(m,dx,dy,dz){ for(let i=0;i<m.V.length;i+=3){ m.V[i]+=dx; m.V[i+1]+=dy; m.V[i+2]+=dz; } return m; }
function mAddMesh(a,b){             // concatenate two meshes
  const off=a.V.length/3;
  for(let i=0;i<b.V.length;i++) a.V.push(b.V[i]);
  for(const f of b.F) a.F.push({v:f.v.map(v=>v+off),s:0});
  return a;
}
function mSpin(m,ang){              // turn a part about its own Y
  const c=Math.cos(ang), s=Math.sin(ang);
  return mDeform(m,p=>{ const x=p[0]*c-p[2]*s, z=p[0]*s+p[2]*c; p[0]=x; p[2]=z; });
}
function mTorus(seg,slices,r1,r2){  // Mesh_Torus(x,y,ri,ro,...) + their rot .25
  // Built in XY, not XZ: their chain rotates it a quarter turn, and without
  // that the passage ring is a hoop lying flat on the deck at head height.
  const m=meshNew();
  for(let i=0;i<seg;i++){
    const a=i/seg*6.2831853, ca=Math.cos(a), sa=Math.sin(a);
    for(let j=0;j<slices;j++){
      const b=j/slices*6.2831853;
      const rr=r1+Math.cos(b)*r2;
      m.V.push(ca*rr, sa*rr, Math.sin(b)*r2);
    }
  }
  for(let i=0;i<seg;i++){
    const i2=(i+1)%seg;
    for(let j=0;j<slices;j++){
      const j2=(j+1)%slices;
      m.F.push({v:[i*slices+j, i2*slices+j, i2*slices+j2, i*slices+j2],s:0});
    }
  }
  return mFixWinding(m);
}

/* ---- placement ------------------------------------------------------
   A part is unwrapped where it is built or where it lands, never from a
   world axis picked off the largest component of a smooth normal -- that
   is what tore the rings in half, because the axis flips wherever a curve
   crosses 45 degrees.
     proj 0  flat part: u,w are its own local x,y
     proj 1  ring of major radius `a`: u is arc length around the ring,
             w is arc length around the tube -- a seamless wrap
     proj 2  the one uv rule: T = cross(up,n), B = cross(n,T) on the
             *world* normal, so u and w are arc length in metres along
             the surface and the part shares the wall's texel density.
     proj 3  the part's own square: u,w run 0..1 across this group's own
             bounding box, with TEX_SCALE divided back out so the texture
             lands exactly once on it.

   proj 3 exists for exactly one thing.  Every other material in the kit is
   a FINISH -- grain, tool marks, mill scale, grime -- and a finish has to
   be world-projected or two pieces of the same steel will have different
   texel densities.  But a stencil, a placard, a warning decal is a
   PICTURE, and a picture sliced out of a world grid is a picture with its
   corners cut off.  So: proj 2 carries finishes, proj 3 carries pictures,
   and nothing else is allowed to be a picture. */
function wPlace(m,mat,ox,oy,oz,yaw,proj,a,bb,cr){
  const c=Math.cos(yaw), s=Math.sin(yaw);
  const out=[]; mEmit(m,cr===undefined?40:cr,out);
  for(let i=0;i<out.length;i+=6){
    const x=out[i], y=out[i+1], z=out[i+2];
    const nx=out[i+3], ny=out[i+4], nz=out[i+5];
    const wx= x*c+z*s+ox, wy= y+oy, wz=-x*s+z*c+oz;
    const gx=nx*c+nz*s,   gy=ny,    gz=-nx*s+nz*c;
    let u,w;
    if(proj===1){ const rr=Math.hypot(x,y);
                  u=Math.atan2(y,x)*a; w=Math.atan2(z,rr-a)*0.16; }
    else if(proj===2){
      let tx,ty,tz;
      if(Math.abs(gy)>0.999){ tx=1; ty=0; tz=0; }
      else { const L=Math.hypot(gz,gx)||1; tx=gz/L; ty=0; tz=-gx/L; }
      const bx=gy*tz-gz*ty, by=gz*tx-gx*tz, bz=gx*ty-gy*tx;
      u=wx*tx+wy*ty+wz*tz;
      w=wx*bx+wy*by+wz*bz;
    }
    else if(proj===3){
      /* The group's own square, taken across its two LARGEST axes.  Reading
         x,y unconditionally was right for a wall plate and silently wrong
         for a floor one: a deck panel's y extent is its 60 mm thickness, so
         a picture unwrapped that way came out stretched ten times up a face
         nobody ever sees.  The smallest extent is the plate's thickness --
         whichever axis that turns out to be -- and the other two are the
         face.  Nothing else in a plate can be ambiguous, because a plate
         that is not thin in exactly one direction is not a plate. */
      const ex=bb[3]-bb[0], ey=bb[4]-bb[1], ez=bb[5]-bb[2];
      let a,b,i,j;
      if(ey<=ex && ey<=ez){ a=x; b=z; i=0; j=2; }      // lying flat: a deck
      else if(ex<=ez)     { a=z; b=y; i=2; j=1; }      // standing, facing +-X
      else                { a=x; b=y; i=0; j=1; }      // standing, facing +-Z
      /* SQUARE CELLS.  Filling the plate in both directions is right for
         a square plate and wrong for every other kind: an 840 x 520 lid
         stretched the picture 1.6:1, so its bolts came out as ovals and
         its bezel was thicker at the ends than at the sides.  The long
         axis is divided into a whole number of cells the size of the short
         one instead, so a cell is always square, the picture still reaches
         all four edges, and a long plate simply shows more of it -- which
         is what a long plate does in the real world too. */
      let du=bb[i+3]-bb[i], dw=bb[j+3]-bb[j];
      if(du>dw) du/=Math.max(1,Math.round(du/dw));
      else      dw/=Math.max(1,Math.round(dw/du));
      u=(a-bb[i])/Math.max(1e-6,du*TEX_SCALE);
      w=(b-bb[j])/Math.max(1e-6,dw*TEX_SCALE);
    }
    else { u=x; w=y; }
    TRIS.push(wx,wy,wz, gx,gy,gz, u,w, mat);
  }
}

/* ---- the registry ---------------------------------------------------
   MODULES rows are [kit, x,y,z, yaw, arg, material].  `kit` indexes this
   list, so a part, once numbered, keeps its number. */
const KIT=[];
function kitDef(d){ KIT.push(d); return KIT.length-1; }

/* ---- a part is a LIST OF GROUPS ------------------------------------
   The first version of this file stamped a whole part with one material,
   because that is what a brush does and the kit grew out of brushes.  It
   is the wrong unit.  A wall bay is a rolled field, a sprayed panel, a cut
   stile, a rubber kick and a black reveal, and calling all five `PANEL`
   threw away every one of those distinctions -- then the PANEL texture
   drew its own panel grid on top of the modelled one, at an unrelated
   pitch, and the two fought.

   So a group is one mesh, one material, one projection, one crease angle,
   and `make` returns a list of them.  What finish a piece of a part wears
   is a property of the piece, not of where the part happens to be stamped.
   A group whose material is -1 falls back to the placement material, which
   is how the old single-mesh parts keep working unchanged. */
function mBounds(m){
  let a=1e9,b=1e9,c=1e9,d=-1e9,e=-1e9,f=-1e9;
  for(let i=0;i<m.V.length;i+=3){
    if(m.V[i]<a)a=m.V[i];     if(m.V[i]>d)d=m.V[i];
    if(m.V[i+1]<b)b=m.V[i+1]; if(m.V[i+1]>e)e=m.V[i+1];
    if(m.V[i+2]<c)c=m.V[i+2]; if(m.V[i+2]>f)f=m.V[i+2];
  }
  return m.V.length? [a,b,c,d,e,f] : [0,0,0,1,1,1];
}
function kGrp(m,t,p,cr){
  return {m:m, t:(t===undefined?-1:t), p:(p===undefined?2:p),
          cr:(cr===undefined?40:cr), b:mBounds(m)};
}
function kGrpAll(list,t,p,cr){           // several meshes that share one finish
  const m=meshNew();
  for(const q of list) mAddMesh(m,q);
  if(m.F.length) mCreaseAngle(m,cr||40);
  return kGrp(m,t,p,cr);
}
const KITCACHE={};
function kitGroups(k,a){
  const key=k+':'+a;
  let g=KITCACHE[key];
  if(!g){
    const r=KIT[k].make(a);
    g = r.V ? [kGrp(r,-1,KIT[k].proj)] : r;      // a bare mesh is still allowed
    KITCACHE[key]=g;
  }
  return g;
}

/* ====================================================================
   SLOTS 0 AND 1 ARE EMPTY.  Two early parts were retired here; the slots
   stay so every index below keeps meaning what it meant in MODULES, in
   the native kit table and in every list generated from them.
   ==================================================================== */
kitDef({ name:'reserved 0', size:[0,0,0], proj:0, make:function(){ return meshNew(); }});
kitDef({ name:'reserved 1', size:[0,0,0], proj:1, make:function(){ return meshNew(); }});


/* ====================================================================
   PRIMITIVES FOR PARTS
   Two rotations the machine did not have, and one real primitive.

   mPlate is the whole argument of this file in one function.  A slab
   whose front face is inset all round and joined to the body by a 45
   degree rim.  That rim is the only part of a flat panel that ever
   catches a lamp it is not facing: a brush wall returns one value
   across its whole face and reads as paint, a wall built out of plates
   draws a bright line around every panel in it.  Nothing else in the
   kit is allowed to be a bare box.

   Built about the origin so mFixWinding can see which way is out, then
   slid so the BACK sits on z=0 and the front stands proud at z=d.
   ==================================================================== */
function mTilt(m,a){                // about X: +Y leans toward +Z
  const c=Math.cos(a), s=Math.sin(a);
  return mDeform(m,p=>{ const y=p[1]*c-p[2]*s, z=p[1]*s+p[2]*c; p[1]=y; p[2]=z; });
}
function mRoll(m,a){                // about Z: stands a Y axis up along X
  const c=Math.cos(a), s=Math.sin(a);
  return mDeform(m,p=>{ const x=p[0]*c-p[1]*s, y=p[0]*s+p[1]*c; p[0]=x; p[1]=y; });
}
function mBox(w,h,d){ return mScale(mCube(),w,h,d); }

/* ---- THE SPLAY ------------------------------------------------------
   Every reference for this ship, from the Nostromo forward, has the same
   thing in common and it is not the panels: THE SECTION IS AN OCTAGON.
   Deck, splay, wall, splay, overhead -- five faces a side, eight in all,
   and no two planes anywhere in the corridor meet at a right angle.

   It is not decoration.  A pressure hull wants to be a cylinder and a
   deck wants to be flat, so the octagon is what you get when the two
   argue, which is why it reads as a SHIP rather than as a room; and the
   45 degree faces catch a lamp neither the wall nor the deck can catch,
   so a corridor lit only at its edges still has a legible section.

   Two builders, and every junction in the kit uses one of them.  A plate
   is authored flat and turned 45 degrees about X: `A` leans out as it
   rises (deck to wall), `B` leans in as it rises (wall to overhead).
   Both take the bottom edge at (a, b) and rise r, so the top edge lands
   at (a+r, b-r) and (a+r, b+r) respectively. */
function mSplayA(w,a,b,r,t,c,n){    // deck -> wall
  return mMove(mTilt(mPlate(w,r*1.4142136,t,c,n),-0.7853982), 0, a+r/2, b-r/2);
}
function mSplayB(w,a,b,r,t,c,n){    // wall -> overhead
  return mMove(mTilt(mPlate(w,r*1.4142136,t,c,n), 0.7853982), 0, a+r/2, b+r/2);
}
function mPipe(r,h,seg){ return mMove(mScale(mCylinder(seg||12,true),r*2,h,r*2),0,h/2,0); }

/* PLATE_ROUND is the kit's one aesthetic knob, and it is the difference
   between industrial and future.  1 gives the flat 45 degree chamfer this
   file started with: correct, machined, and slightly brutal -- it returns
   one value across its width and draws a hard line.  3 rolls the same
   chamfer over a quarter circle instead, and a rolled edge never draws a
   line at all: it sweeps a highlight along itself as you walk past, and
   it never goes fully dark, because some part of the roll is always
   facing the lamp.  That single property is what separates a moulded
   composite panel from a cut steel one, and it is why every reference
   for a ship built after about 2100 has no sharp arrises on it anywhere.

   The roll is smooth because the steps are 30 degrees and every group
   creases at 32 or more, so the three faces average into one curve.
   Cost: a plate goes from 10 quads to 18. */
const PLATE_ROUND=3;

function mPlate(w,h,d,c,n){
  n=n||PLATE_ROUND;
  const m=meshNew();
  const X=w/2, Y=h/2, D=d/2;
  const ring=(inset,z)=>{
    const x=Math.max(0.001,X-inset), y=Math.max(0.001,Y-inset);
    m.V.push(-x,-y,z, x,-y,z, x,y,z, -x,y,z);
  };
  ring(0,-D);                        // 0..3    the back
  ring(0, D-c);                      // 4..7    where the roll starts
  for(let k=1;k<=n;k++){             // the roll: a quarter circle of radius c
    const a=k*1.5707963/n;
    ring(c*(1-Math.cos(a)), D-c+c*Math.sin(a));
  }
  const F=(a,b,cc,dd)=>m.F.push({v:[a,b,cc,dd],s:0});
  F(0,1,2,3);
  for(let r=0;r<=n;r++)              // the sides, then every step of the roll
    for(let i=0;i<4;i++){ const j=(i+1)%4, o=r*4;
      F(o+i,o+j,o+4+j,o+4+i); }
  const t=(n+1)*4;
  F(t,t+1,t+2,t+3);
  mFixWinding(m);
  return mMove(m,0,0,D);
}
// the same plate lying down: w across, d deep, t thick, standing on y=0
function mSlab(w,d,t,c,n){ return mTilt(mPlate(w,d,t,c,n),-1.5707963); }
/* the same plate, turned the other way: face DOWN, back on y=0, so an
   overhead part is authored in the same numbers as a deck one and hangs
   instead of standing.  Both are rotations, so winding survives. */
function mSoffit(w,d,t,c,n){ return mTilt(mPlate(w,d,t,c,n),1.5707963); }
// a plate turned to face -Z, so a pair of them can face each other
function mFlip(m){ return mSpin(m,3.1415927); }

/* ====================================================================
   THE GRID

   Everything in this file is a whole number of U, and U is 1.200 m.

     U       1.200   the placement module.  A floor tile is one, a wall
                     bay is one wide, and -- since TEX_SCALE is 1/U --
                     ONE TILE OF EVERY MATERIAL IS EXACTLY ONE MODULE.
                     A texel is 4.6875 mm.
     U/2     0.600   the panel module.  Four to a floor tile, and it is
                     not an arbitrary halving: 600 mm is the size a real
                     raised-access floor panel is, in every data centre
                     and every ship afloat.  It is the one dimension a
                     player has actually stood on.
     U/4     0.300   the detail module: a column shaft, a head band, the
                     pilaster two wall bays make where they meet.
     U/8     0.150   the trim module: a stile, a kick, a rail.
     U/10    0.120   the depth module.  Every plane a bay presents to the
                     corridor is a multiple of 12 mm, so no two parts can
                     disagree about how far proud anything sits.
     2U      2.400   the light line, on every bay and every post.
     3U      3.600   the corridor.

   The proportions are the point, not the numbers.  Doom 3's kit worked
   the same way -- wall panels twice a floor plate, pillars half a wall
   panel, trims half a floor plate -- and it worked because the eye can
   see a 2:1 and cannot see a 1.7:1.  A part that is 1.13 modules long
   does not fail to fit; it fails to LOOK like it belongs to the same
   ship as the part next to it.
   ==================================================================== */
const U      = 1.200;
const U_2    = 0.600, U_4 = 0.300, U_8 = 0.150, U_10 = 0.120;
const SPL    = U_2;                 // 0.600 -- the splay that fills every
                                    // 90 degree junction in the ship.  It
                                    // has to live down here with the other
                                    // module constants and not up beside
                                    // mSplayA/B: a const read before its
                                    // declaration is a temporal dead zone
                                    // error, and it takes the whole file
                                    // with it.
const DECK_T = U;                   // the deck tile, and the kit's base unit
const WALL_H = 3*U;                 // 3.600 -- a corridor

/* a countersunk fastener, lying on a surface whose normal is +Y */
function mBolt(r,rise){
  const b=mMove(mScale(mCylinder(8,true),r*2,rise,r*2),0,rise/2,0);
  mSelNone(b); mSelNormal(b,0,1,0,0.20);
  mExtrude(b,rise*0.45,0.30);
  return b;
}

/* ====================================================================
   K2  FLOOR 1  --  ACCESS DECK, one module, four 600 mm panels

   THE SIZE QUESTION, which is the whole of this part.

   A deck laid from 1.20 m tiles that each LOOK like a 1.20 m tile reads
   as a chessboard: the eye finds the tile grid, the tile grid is the
   placement grid, and the floor announces exactly how the level was
   built.  No texture fixes that, and a bigger tile only makes it worse.

   The fix is to make the PANEL grid finer than the PLACEMENT grid, and
   then make the seam at the tile boundary identical to the seam inside
   the tile, so the boundary has nothing to give it away:

       placement module   U      1.200   = a wall bay, so floor and wall
                                           share one rhythm
       panel module       U/2    0.600   four to a tile
       seam               U/30   0.040   the SAME everywhere
       panel              0.560          = 0.600 - 0.040

   A panel's outer edge lands 20 mm inside the tile edge; the neighbour
   contributes the other 20; the seam at the join is 40 mm, exactly like
   the seam down the middle.  Nothing in this part is drawn at 1.20 m --
   the frame slab is flat to within 3 mm at its rim, deliberately, so it
   cannot draw a line there.  Walk a corridor of these and there is no
   tile grid to find, only a 600 mm panel grid, which is the size a floor
   panel actually is: 600 mm is the world standard for raised access
   floor, in every data centre and every ship afloat.  It is the one
   dimension a player has already stood on without knowing it.

   The seam is three things, not one: 12 mm of black either side of a
   16 mm machined land.  The land is what stops a seam reading as a drawn
   line -- it is lit, and the black beside it is not, so the seam has a
   section instead of a colour.
   ==================================================================== */
/* ====================================================================
   THE PLATE LANGUAGE  --  which finish a face wears, as a rule

   Nothing below chooses a material.  Four rules do, and the parts only
   say which class a face belongs to:

   1  THREE VALUES, AND NO FOURTH.  Every surface is one of
        VOID   ~0.03   GAP           recesses, reveals, pockets, channel backs
        FIELD  ~0.18   SKIN DUCT     the body, untouched: rolled plate, rubber
        PLATE  ~0.45   MACH ACC      anything cut, machined or sprayed that
                       LOUV PERF     faces you
      The references read because of that separation and for no other
      reason.  A bay whose plate and whose recess are both 0.2 is mud
      however well it is modelled.

   2  OCHRE MEANS "THIS COMES OFF".  Painted, never lit, and only on lids:
      access panels and hatches.  It is the ship's one warm note and it
      survives being one because nothing else is allowed to use it.

   3  LIT MEANS POWERED, AND IT IS RARE.  Amber for a lamp, cyan for a live
      system.  One bay type in three carries a readout; half the deck tiles
      carry a cartridge; the channel at 2U runs everywhere.  Nothing else
      in the kit emits at all.

   4  A PICTURE FILLS ONE PLATE.  Louvres, perforations and panel recesses
      are materials, not geometry -- unwrapped in the plate's own square so
      the picture and the plate are the same rectangle.  That is what pays
      for the reference look without modelling it: an open deck cell costs
      one material index where cutting its 256 holes costs 2,000 triangles,
      and there are ninety tiles.

   The 80 mm ceiling in shipmaterials.py still binds every WORLD-projected
   finish and always will.  Rule 4 is not an exception to it: a part-space
   picture has no world grid left to fall out of register with.
   ==================================================================== */
const DECK_P = U_2;                 // 0.600 -- the panel module
const DECK_S = U/30;                // 0.040 -- and the seam, everywhere
const DECK_PW = DECK_P - DECK_S;    // 0.560 -- so the panel itself

/* the four panel centres, on the quarter grid */
const DECK_AT = [[-1,-1],[1,-1],[1,1],[-1,1]];
function deckCell(k){ return [DECK_AT[k][0]*U_4, DECK_AT[k][1]*U_4]; }

/* Every floor part starts here: the frame slab and the black reveal each
   panel sits in.  Sharing it is the point -- FLOOR 1, 2 and 3 can be laid
   next to each other in any order and the seam never jumps. */
function deckFrame(P,cells){
  if(cells===undefined) cells=1;
  P.mach.push(mSlab(U,U,0.024,0.003,1));      // flat at its rim, on purpose
  if(cells) for(let k=0;k<4;k++){
    const c=deckCell(k);
    P.gap.push(mMove(mSlab(DECK_PW+0.024,DECK_PW+0.024,0.036,0.005), c[0],0,c[1]));
  }
  /* The machined land down the middle of every seam, and it has to stand
     ABOVE the black or it does not exist: the first pass put the frame at
     24 mm under a 36 mm reveal, so the land was 12 mm down a slot and in
     permanent shadow -- the seam came out as one flat black band instead
     of the three-part section it is drawn as.  4 mm proud is enough.

     Each tile owns the bar on its -x and -z boundary and the neighbour
     owns the other two, so a field of these has exactly one bar per seam
     and nothing is ever drawn twice. */
  for(const x of (cells?[0,-U_2]:[-U_2]))
    P.mach.push(mMove(mSlab(0.016,U,0.040,0.004), x,0,0));
  for(const z of (cells?[0,-U_2]:[-U_2]))
    P.mach.push(mMove(mSlab(U,0.016,0.040,0.004), 0,0,z));
}

/* A vertical light run, cut by a machined strap at every U/2 and by the
   collar at 2U.  A column carrying one unbroken three-metre line reads as
   a lamp standard, not as structure -- and the ticks land on the same
   600 mm pitch as the floor panels, so the eye gets the module twice. */
function litRun(y0,y1,g){
  const out=[]; let a=y0;
  for(let y=Math.ceil(y0/U_2)*U_2; y<y1; y+=U_2){
    if(y-g-a>0.10) out.push([a,y-g]);
    a=y+g;
  }
  if(y1-a>0.10) out.push([a,y1]);
  return out;
}

kitDef({ name:'FLOOR 1', sub:'access deck · 1.12 m plate', proj:2,
  // AMBER, not the wall's blue-white.  Warm on the deck against cool on
  // the wall is the contrast the whole corridor is lit by, and it is the
  // one thing the deck reference does that no amount of panel work can.
  emit:v=>((v|0)&1) ? [[-U/2+0.032,0.024,-0.010, U/2-0.032,0.042,0.010, T.GLOW]] : [],
  make:function(v){
  v=v|0;
  const P={mach:[],gap:[],coat:[],skin:[],duct:[]};
  deckFrame(P,0);                     // no cell reveals: there is one cell

  /* ONE PLATE, NOT FOUR.  Four 560 mm cells in a 1.2 m tile gave the
     deck a 600 mm grain, and at standing height that is small enough
     that the eye stops reading plates and starts reading pattern -- the
     floor turned into a chequer.  One 1.12 m plate per module reads as
     a PLATE: you can see its four fixings, you can see it is a thing
     somebody bolted down, and the module grid is still there in the
     seam because the seam is what the frame draws.

     The fixings are at the four CORNERS.  They were at the mid-edges,
     on the argument that a corner bolt fouls its neighbour's -- true of a
     plate that butts, false of one that sits in a rebate with a 40 mm
     reveal all round, which is what this is.  And the corner is where the
     eye looks for them: four bolts on the diagonal say "lift here" and
     four on the axes say nothing at all. */
  P.coat.push(mSlab(U-2*DECK_S, U-2*DECK_S, 0.060, 0.016));
  P.gap .push(mSlab(U-0.020, U-0.020, 0.036, 0.005));
  for(const d of [[-1,-1],[1,-1],[1,1],[-1,1]])
    P.mach.push(mMove(mBolt(0.028,0.014),
                      d[0]*(U/2-0.124), 0.060, d[1]*(U/2-0.124)));

  /* FLOOR 1 IS THE QUIET ONE, AND THAT IS ITS WHOLE JOB.

     It used to open a diagonal pair of cells into grates, which read
     beautifully on one tile and, laid out over a deck, came out as a
     random mosaic -- because a rule applied to EVERY tile is not a rule,
     it is a texture.  A deck is zoned, not speckled: a margin against
     each wall, a plain field down the middle, and a band of something
     else at intervals.  So the grates moved to FLOOR 4, which is the
     margin, the bands are FLOOR 2 laid across the full width, and this
     part is four plain plates and nothing else.

     The only variation left is the one that is not a pattern: one cell
     in some tiles was swapped out and never resprayed. */
  const face=[];

  /* The flush pull.  A deck panel lifted by proud bolt heads is a 1950s
     engine room; one lifted by a slot you can only find from directly
     above is a ship.  One per tile, and its cell and its direction both
     come off the variant, so no two neighbours are the same way up. */
  {
    const c=deckCell(v&3);            // still off the quarter grid, so the
    const s=mMove(mSlab(0.168,0.052,0.054,0.006), c[0]*0.72,0,c[1]*0.72);
    const l=mMove(mSlab(0.196,0.078,0.057,0.008), c[0]*0.72,0,c[1]*0.72);
    if(v&1){ mSpin(s,1.5707963); mSpin(l,1.5707963); }   // pull is never
    P.gap.push(s); P.mach.push(l);                        // twice in a place
  }

  /* The lit seam.  Half the tiles get it, so a corridor has a line down
     its floor that stops and starts instead of running like a runway --
     and the strip is stamped as a real brush (see `emit`), so the deck
     actually throws light up the walls beside it instead of merely
     appearing to. */
  if(v&1) P.gap.push(mMove(mSlab(U-0.06,0.030,0.030,0.004), 0,0,0));

  return [kGrpAll(P.mach,T.MACH,2,34), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.coat,T.COAT,2,32), kGrpAll(P.skin,T.SKIN,2,32),
          kGrpAll(P.duct,T.DUCT,2,36)].concat(face);
}});

/* ====================================================================
   K3  FLOOR 2  --  VENT DECK, one module, four 600 mm cells

   The opposite argument to FLOOR 1.  A panel closes the floor; a vent
   opens it, and what you see through it -- black, or a lit deck below,
   or something moving -- is worth more than any surface.

   It keeps FLOOR 1's frame and its 600 mm module exactly, which is the
   entire reason it is worth having: swap one tile in eight for this and
   the floor gains a hole without gaining a second grid.

   The blades are ON EDGE, and that is the whole effect: from directly
   above the deck is nearly closed, from four metres away it is nearly
   open, so the floor CHANGES as you cross it in a way no texture on a
   solid panel can imitate.  Under them, the services the void exists to
   carry, run visibly across -- one copper among the black, because a
   bundle in which every run is the same is a bundle nobody routed.
   ==================================================================== */
kitDef({ name:'FLOOR 2', sub:'vent deck · 4 × 600 mm', proj:2,
  clip:()=>[[-U/2,0,-U/2, U/2,0.048,U/2]],
  make:function(v){
  v=v|0;
  const P={mach:[],gap:[],coat:[],skin:[],duct:[]};
  const CU=[], H=0.048;
  deckFrame(P);

  for(let k=0;k<4;k++){
    const c=deckCell(k), W=DECK_PW;
    // the cell's kerb: four bars, so the vent is still a panel-shaped
    // thing and lines up with the panels either side of it
    for(const s of [-1,1]){
      P.mach.push(mMove(mSlab(W,0.028,H,0.008), c[0], 0, c[1]+s*(W-0.028)/2));
      P.mach.push(mMove(mSlab(0.028,W-0.056,H,0.008), c[0]+s*(W-0.028)/2, 0, c[1]));
    }
    const span=W-0.056, n=9, pitch=span/n;
    for(let i=0;i<n;i++)                              // the blades
      P.mach.push(mMove(mBox(span,H-0.016,0.009,0),
                        c[0], (H-0.016)/2+0.005, c[1]-span/2+pitch*(i+0.5)));
    for(let j=0;j<2;j++)                              // and their ties
      P.mach.push(mMove(mBox(0.010,0.012,span),
                        c[0]-span/4+span/2*j, H-0.020, c[1]));
  }

  // Shallow on purpose: it reads through the blades even where the deck
  // below is only 200 mm down, and buries harmlessly where it is not.
  CU.push(mMove(mRoll(mPipe(0.032,U+0.10,10),1.5707963), -(U+0.10)/2,-0.108, U_4-0.06));
  P.duct.push(mMove(mRoll(mPipe(0.048,U+0.10,10),1.5707963), -(U+0.10)/2,-0.124,-U_4+0.02));
  P.duct.push(mMove(mRoll(mPipe(0.028,U+0.10,10),1.5707963), -(U+0.10)/2,-0.100,-U_4+0.10));

  return [kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(CU,T.CU,2,36), kGrpAll(P.duct,T.DUCT,2,36)];
}});

/* ====================================================================
   K4  FLOOR 3  --  ACCESS HATCH, one module, the hatch is ONE PANEL

   Not a third floor so much as the punctuation for the other two.  A
   deck of identical panels has a rhythm and no sentence; one hatch every
   dozen tiles says the deck is a lid over machinery and gives the eye
   somewhere to stop.  It is also a promise the level can keep later: a
   hatch is a place something can come out of.

   THE HATCH IS EXACTLY ONE 600 mm PANEL.  Not a medallion dropped onto
   the floor, not a feature with its own footprint -- one cell of the same
   grid, with the other three still ordinary panels.  That is what makes
   it belong: you can see, without being told, that somebody lifted one
   panel out and put a hatch in its place.

   The industrial version of this part had proud hinge knuckles, two
   quarter-turn dogs and a lift ring, and every one of them is something
   that catches a boot.  A ship built after those problems were solved
   has a lid that sits FLUSH in its rebate, seals onto a gasket, and says
   what it is doing with four lights instead of a placard.  The lid is an
   octagon because a round thing in a square grid is the one place this
   kit is allowed to stop being square -- and it only reads as a relief
   because everything around it is not.
   ==================================================================== */
kitDef({ name:'FLOOR 3', sub:'flush hatch · one 600 mm cell', proj:2,
  emit:v=>{
    const c=[DECK_AT[(v|0)&3][0]*U_4, DECK_AT[(v|0)&3][1]*U_4], E=[], r=0.268;
    for(const s of [-1,1]){
      E.push([c[0]-0.034,0.036,c[1]+s*r-0.011, c[0]+0.034,0.047,c[1]+s*r+0.011, T.CHAN]);
      E.push([c[0]+s*r-0.011,0.036,c[1]-0.034, c[0]+s*r+0.011,0.047,c[1]+0.034, T.CHAN]);
    }
    return E; },
  make:function(v){
  v=v|0;
  const P={mach:[],gap:[],coat:[],skin:[],duct:[]};
  deckFrame(P);
  const hk=v&3;

  for(let k=0;k<4;k++){
    const c=deckCell(k);
    if(k!==hk){                                        // three ordinary panels
      P.coat.push(mMove(mSlab(DECK_PW,DECK_PW,0.060,0.012), c[0],0,c[1]));
      continue;
    }
    // ---- the fourth is the hatch ------------------------------------
    P.mach.push(mMove(mOctCap(0.278,0.038), c[0],0,c[1]));      // the seat
    P.gap .push(mMove(mOctCap(0.258,0.046), c[0],0,c[1]));      // the rebate
    P.duct.push(mMove(mOctCap(0.250,0.050), c[0],0,c[1]));      // the gasket

    const lid=mOctCap(0.244,0.060);                             // and the lid
    mSelNone(lid); mSelNormal(lid,0,1,0,0.20);
    mExtrude(lid,0.006,0.026);                                  // rolled, flush
    P.coat.push(mMove(lid, c[0],0,c[1]));

    // the release: a disc set INTO the lid, never standing on it
    P.gap .push(mMove(mScale(mCylinder(12,true),0.212,0.052,0.212), c[0],0.026,c[1]));
    P.mach.push(mMove(mScale(mCylinder(12,true),0.180,0.056,0.180), c[0],0.020,c[1]));
    for(let j=0;j<3;j++)                                        // three keys
      P.mach.push(mMove(mSpin(mBox(0.140,0.018,0.024),j*2.0943951),
                        c[0],0.058,c[1]));
    P.gap .push(mMove(mScale(mCylinder(10,true),0.056,0.064,0.056), c[0],0.018,c[1]));

    // the four status lights, in the seat, one to each cardinal
    for(const s of [-1,1]){
      P.gap.push(mMove(mSlab(0.086,0.030,0.044,0.005), c[0], 0, c[1]+s*0.268));
      P.gap.push(mMove(mSlab(0.030,0.086,0.044,0.005), c[0]+s*0.268, 0, c[1]));
    }
  }

  return [kGrpAll(P.mach,T.MACH,2,34), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.coat,T.COAT,2,32), kGrpAll(P.duct,T.DUCT,2,36),
          kGrpAll(P.skin,T.SKIN,2,32)];
}});

/* ====================================================================
   THE BAY GRAMMAR

   Three bays only read as one wall if they agree about where their lines
   are.  These constants are that agreement, and WALL 1, 2 and 3 are all
   built out of them -- which is why they can be stamped in any order along
   a run and still look like one ship rather than three kits.

   The DEPTHS matter more than the heights, because depth is the thing a
   texture can never fake and a lamp always finds:

     0.014  channel floor   CHAN   the lit line, deep in its own recess
     0.038  field           SKIN   rolled plate, never touched since the mill
     0.062  panel face      COAT   sprayed, and chipped back to metal at its edges
     0.100  kick face       DUCT   rubber, because that is where boots go
     0.128  stile face      MACH   cut steel: the brightest thing in the bay
     0.150  rail nose       MACH   and proud of all of it

   Six planes, six finishes, and every bay says what it IS by which planes
   it uses and how much of each.  None of them says it with a texture --
   that is the whole change.  The old bays were one material (`PANEL`) over
   twelve modelled pieces, and that material drew its own panel every
   1.61 m, its own rail every 0.80 m and its own rivets, none of which had
   anything to do with the twelve pieces underneath.  Two grids at unrelated
   pitches is why they read as wallpaper stretched over a model.
   ==================================================================== */
const B_W    = U;           // an event bay is one module wide
const B_W2   = 2*U;         // 2.400 -- a bay sized by what is IN it: the
                            // main run needs 2 m of clear width for five
                            // 300 mm trunks, so it gets two modules.
const B_W4   = 4*U;         // 4.800 -- and the FIELD bay is four of them.
                            // A pilaster every 1.2 m slices a corridor into
                            // vertical strips and nothing reads as a wall;
                            // one every 4.8 m gives the run a length, and
                            // the events in between finally read as events.
const B_STILE= U/6;         // 0.200.  Two bays side by side make a U/3
                            // pilaster -- 400 mm of it -- and the pilaster
                            // hides the join.
const B_GAPW = U/24;        // 0.050 -- the black channel inside each stile
const B_KICK = U_8;         // rubber to 0.150
const B_LINE = 2*U;         // 2.400.  THE LIGHT LINE -- and every column,
                            // post and collar in the kit is built to
                            // acknowledge this one height.
const B_SLOT = 0.072;       // its opening      (6 depth modules)
const B_LIPH = 0.036;       // and its lips     (3)
const B_HEAD = U_4;         // 0.300 -- the coated band under the overhead
const B_RAIL_Y = 3*U/4;     // 0.900 -- and a handrail really is 900 mm

/* THE DEPTH GRID.  Every plane a bay presents to the corridor is a
   multiple of U/100 = 12 mm, so no two parts can disagree about how far
   proud a thing sits -- and each plane wears exactly one finish:

     0.048  channel floor   CHAN   the lit line, deep in its own recess
     0.036  field           SKIN   rolled plate, untouched since the mill
     0.060  panel face      COAT   sprayed, chipped back at its edges
     0.096  kick face       DUCT   rubber, because that is where boots go
     0.120  stile face      MACH   cut steel, the brightest thing in the bay
     0.144  rail nose       MACH   and proud of all of it

   0.048 was 0.014 once, which put the diffuser 106 mm down a slot: exactly
   right and completely invisible from anywhere but dead in front. */
/* THE STEP IS THE POINT.  These were 36 mm of field against 120 mm of
   stile: an 84 mm reveal, which under a lamp two metres away throws a
   shadow about as deep as a pencil line, and in a dark corridor throws
   nothing at all.  A modelled recess only exists if it is deep enough to
   go black while the thing beside it is lit -- that is the whole reason
   to model it instead of painting it.

   So the field drops to 12 mm and the pilaster comes forward to 240, and
   the reveal between them is 228 mm.  At that depth a lamp anywhere along
   the corridor puts one face of every pilaster in light and the wall
   behind it in shadow, and the passage gets a section you can read from
   the far end.  Everything is still a multiple of U/100 = 12 mm, so no
   two parts can disagree about how far proud a thing sits. */
/* ...and then doubled again, for the same reason it was deepened the
   first time.  228 mm of reveal put a shadow on the wall; 468 mm puts the
   whole bay in shadow and leaves only the pilaster lit, which is what the
   references actually look like -- you read a corridor as a row of LIT
   COLUMNS with darkness between them, not as a wall with grooves in it.
   At 480 mm the pilaster is also deep enough to occlude the bay from an
   oblique angle, so the corridor gains real parallax as you walk it. */
const P_CHAN=0.036, P_FIELD=0.012, P_PANEL=0.072,
      P_KICK=0.132, P_STILE=0.480, P_RAIL=0.516;

/* Every bay's shared half.  A bay calls this, then adds the one thing that
   makes it itself -- which is the only part that should ever differ. */
function bayShell(h,P,W,hole){
  W=W||B_W;
  const IW=W-2*B_STILE;
  /* THE FIELD IS PLATED, not one sheet.  A 4.8 m bay whose back is a
     single rolled sheet reads as LEATHER from any distance -- mill scale
     is a mottle with no edge anywhere in it, which is exactly right for
     what it is and exactly wrong for the largest surface in the room.
     It gets the deck's treatment instead: plates about 1.2 m square on the
     module, a reveal on every seam, and the backing sheet behind them so
     the seams read as seams and not as holes. */
  /* ...and a bay with a HOLE in it lays the same field in four bands round
     the opening instead of one grid across it.  WALL 5 is the only part that
     asks for this and it is not optional there: the backing sheet sits 24 mm
     behind the glass, so without the hole the window looks out onto the wall
     it is a window in -- which is exactly what the first build of it did. */
  const band=(x0,y0,x1,y1)=>{
    if(x1-x0<0.02||y1-y0<0.02) return;
    P.skin.push(mMove(mPlate(x1-x0,y1-y0,0.006,0.004),(x0+x1)/2,(y0+y1)/2,0));
    const FN=Math.max(1,Math.round((x1-x0)/1.200)),
          FM=Math.max(1,Math.round((y1-y0)/1.200));
    const FW=(x1-x0)/FN, FH=(y1-y0)/FM;
    for(let a=0;a<FN;a++) for(let b=0;b<FM;b++)
      P.skin.push(mMove(mPlate(FW-0.036,FH-0.036,P_FIELD,0.010),
                        x0+FW*(a+0.5), y0+FH*(b+0.5), 0));
  };
  if(!hole) band(-W/2,0,W/2,h);
  else{
    band(-W/2, 0,       W/2,     hole[1]);      // under the opening
    band(-W/2, hole[3], W/2,     h);            // over it
    band(-W/2, hole[1], hole[0], hole[3]);      // and down each side
    band(hole[2], hole[1], W/2,  hole[3]);
  }
  for(const s of [-1,1]){
    const sx=s*(W/2-B_STILE/2);
    /* A REAL BEVEL.  16 mm of chamfer was right when the stile stood
       120 mm proud; on a 480 mm pilaster it is a hairline, and the column
       read as a bare box with a scratch round it.  50 mm rolled over three
       steps catches the overhead lamp all the way down its length and
       never goes fully dark, which is the whole reason PLATE_ROUND exists
       (see mPlate) and the one thing that separates a moulded column from
       an extruded one. */
    P.mach.push(mMove(mPlate(B_STILE,h,P_STILE,0.050), sx, h/2, 0));  // stile
    /* AND IT LANDS THROUGH A SPLAY.  A 240 mm pilaster that stops dead on
       the deck is a box standing on a floor; one that flares over its last
       130 mm is a column, and it is the same 45 degrees the deck and the
       overhead junctions already use -- so the eye reads one rule holding
       the whole section together rather than three separate details. */
    P.mach.push(mMove(mSplayA(B_STILE,0,P_STILE+0.204,0.204,0.034,0.026),
                      sx, 0, 0));
    P.mach.push(mMove(mSplayB(B_STILE,h-0.204,P_STILE,0.204,0.034,0.026),
                      sx, 0, 0));
    P.gap .push(mMove(mPlate(B_GAPW,h,0.018,0.004),
                      s*(W/2-B_STILE-B_GAPW/2), h/2, 0));         // its channel
  }
  /* ---- the kick, and the wash under its nosing ------------------------
     The reference corridors are dark, and what stops them being a black
     tube is not the ceiling: it is a warm line at ankle height that throws
     a POOL ON THE DECK in front of every cabinet.  It is the one light in
     the frame whose source you never see and whose effect you always do,
     and it is what makes a corridor read as lit from within rather than
     as a model under a lamp.

     So the kick is now three things instead of two: rubber to 118, a
     40 mm pocket above it, and the machined nosing overhanging the pocket
     by 70 mm.  The strip lies in the pocket (see `bayEmit`), the nosing
     hides it from anywhere above about waist height, and everything below
     that gets the wash. */
  P.duct.push(mMove(mPlate(IW,0.118,P_KICK,0.014), 0, 0.059, 0));
  P.gap .push(mMove(mPlate(IW,0.034,0.040,0.006), 0, 0.135, 0));
  P.mach.push(mMove(mPlate(IW,0.034,P_KICK+0.014,0.008), 0, B_KICK+0.017, 0));

  // ---- the light line ------------------------------------------------
  // Not a stripe painted on the wall.  A black recess, two machined lips
  // 90 mm proud of the diffuser at the bottom of it, and the diffuser
  // itself stamped as a real brush (see `emit`) so the radiosity bake
  // actually lights the corridor off it.  You never see the source.
  P.gap .push(mMove(mPlate(IW, B_SLOT+2*B_LIPH, 0.020, 0.005), 0, B_LINE, 0));
  for(const t of [-1,1])
    P.mach.push(mMove(mPlate(IW, B_LIPH, 0.092, 0.010),
                      0, B_LINE+t*(B_SLOT+B_LIPH)/2, 0));

  // ---- the head band --------------------------------------------------
  if(h>B_LINE+0.55){
    P.coat.push(mMove(mPlate(IW,B_HEAD,P_PANEL,0.014), 0, h-B_HEAD/2, 0));
    P.mach.push(mMove(mPlate(IW,0.030,P_PANEL+0.018,0.008), 0, h-B_HEAD-0.015, 0));
  }
}

/* the clip for any bay: the wall, notched open at the light line.  The
   notch is not cosmetic -- the bake finds an emitter by ray-casting the
   brush set, so a diffuser buried inside a clip box lights nothing at all. */
function bayClip(h,d,W){
  h=h||WALL_H;
  const hw=(W||B_W)/2;
  // three boxes, not two: the kick pocket needs a notch for exactly the
  // same reason the light line does -- the bake ray-casts the clip set
  // first, so a strip inside a clip box lights nothing whatever.
  return [[-hw,0,0, hw, 0.112, d],
          [-hw,0.160,0, hw, B_LINE-0.058, d],
          [-hw,B_LINE+0.058,0, hw, h, d]];
}
/* the two lit things in every bay, and the whole of rule 3 in four lines:
   a COOL line at 2U that you read the corridor's length by, and a WARM one
   at 135 mm that you read the deck by.  Warm below, cool above, on every
   bay in the ship -- so a passage has a colour gradient from floor to
   ceiling that no single lamp could give it, and the two never compete
   because they are two metres apart. */
function bayEmit(W){
  const x=(W||B_W)/2-B_STILE-0.012;
  return [[-x, B_LINE-0.026, P_CHAN, x, B_LINE+0.026, P_CHAN+0.016, T.CHAN],
          [-x, 0.124, 0.042, x, 0.146, 0.058, T.GLOW]];
}

/* ====================================================================
   K5  WALL 1  --  SERVICE PANEL BAY, 1.30 m

   The quiet one, and the one most of a ship should be.  A machined frame
   with two sprayed access panels sitting on it, a handrail across the
   middle, and the light line above.

   What it is NOT, any more, is one material.  It is six: the field is
   rolled plate that nobody has touched, the panels are sprayed and chipped
   back to bare metal at their corners, the frame and the rail are cut
   steel, the kick is rubber, the reveals are black, and the placard is the
   only thing in the bay allowed to be a picture -- so it is unwrapped in
   its own square (proj 3) and the stencil lands on the plate instead of
   being sliced out of a world grid.

   The handrail runs the full width on purpose.  It is the one element that
   crosses the pilaster, so a run of these reads as a corridor with a rail
   down it rather than as a row of separate panels.
   ==================================================================== */
kitDef({ name:'WALL 1', sub:'service panel bay · 1.30 m', proj:2,
  clip:h=>bayClip(h,P_RAIL+0.010), emit:bayEmit,
  make:function(h){
  h=h||WALL_H;
  const IW=B_W-2*B_STILE;
  const P={mach:[],gap:[],skin:[],coat:[],duct:[]};
  bayShell(h,P);

  // the machined frame the panels are set into.  One plate, so the visible
  // machining is a 30 mm reveal around each panel -- a border that is cut
  // metal next to sprayed metal, which is a boundary no texture can draw.
  const f0=0.204, f1=B_LINE-0.126;
  P.mach.push(mMove(mPlate(IW,f1-f0,0.046,0.010), 0,(f0+f1)/2, 0));

  /* The two lids.  They were COAT -- a sprayed finish with nothing on it --
     and a bay with two blank 840 mm rectangles in the middle of it is the
     single reason WALL 1 read as a grey box.  They are lids, so by rule 2
     they carry the ochre, and by rule 4 they carry it as a picture in their
     own square rather than as anything modelled: bezel, recessed field,
     three bars, six bolts, two slivers, no triangles. */
  const lid=[];
  const panels=[[0.246,B_RAIL_Y-0.072],[B_RAIL_Y+0.072,f1-0.048]];
  for(const q of panels){
    lid.push(kGrpAll([mMove(mPlate(IW-0.060,q[1]-q[0],P_PANEL,0.016),
                            0,(q[0]+q[1])/2,0)], T.ACC, 3, 34));
    // a black reveal under each panel: the panel is a lid, and a lid has a
    // gap under it.  This is the shadow the raking lamp is looking for.
    P.gap.push(mMove(mPlate(IW-0.060,0.022,0.030,0.006),0,q[0]-0.011,0));
    for(const s of [-1,1]) for(const t of [-1,1])
      P.mach.push(mMove(mTilt(mBolt(0.021,0.011),1.5707963),
                        s*(IW/2-0.086), q[t<0?0:1]+t*0.070, P_PANEL));
  }

  // the handrail, full width, over the pilaster and on to the next bay
  P.mach.push(mMove(mPlate(B_W,0.060,P_RAIL,0.012), 0, B_RAIL_Y, 0));
  for(const s of [-1,1])                            // and its brackets
    P.mach.push(mMove(mPlate(0.072,U_8,P_RAIL-0.012,0.010),
                      s*(B_W/2-B_STILE/2), B_RAIL_Y, 0));

  // two stiffener straps in the upper field, so the run above the light
  // line is not a blank metre
  for(const y of [B_LINE+U_4-0.06, h-B_HEAD-U_4+0.06])
    if(y>B_LINE+0.15 && y<h-B_HEAD-0.05)
      P.mach.push(mMove(mPlate(IW-0.180,0.048,P_FIELD+0.026,0.009), 0, y, 0));

  // the placard: bay number, stencil block, ochre edge.  Its own group,
  // its own projection -- the one picture in the kit.
  const mark=mMove(mPlate(U_4,0.180,0.020,0.005), -0.216, 1.320, P_PANEL);

  return [kGrpAll(P.skin,T.SKIN,2,32), kGrpAll(P.coat,T.COAT,2,34),
          kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,34), kGrp(mark,T.MARK,3)].concat(lid);
}});

/* ====================================================================
   K6  WALL 2  --  RIBBED STRUCTURAL BAY, 1.30 m

   The frame, admitted.  WALL 1 hides the structure behind a skin; this one
   puts two I-ribs in front of it and runs the services down the channel
   between them, where a real ship puts them because it is the one place
   nothing can knock them.

   Two things make it worth having.  The first is that the ribs stand
   230 mm proud and the light line is 210 mm behind their front face, so
   every rib CUTS the lit run and throws a hard vertical shadow across it.
   That is an effect no wall texture can produce and it costs nothing --
   the geometry and the light are the same object.

   The second is the copper.  One warm conductor in a bundle of black ones
   says the pipes are different services rather than more wall, and it is
   the only warm thing in the bay.
   ==================================================================== */
kitDef({ name:'WALL 2', sub:'main run · 2U, five flanged mains', proj:2,
  clip:h=>bayClip(h,0.480,B_W2), emit:()=>bayEmit(B_W2),
  make:function(h){
  h=h||WALL_H;
  const IW=B_W2-2*B_STILE;
  const P={mach:[],gap:[],skin:[],coat:[],duct:[]};
  bayShell(h,P,B_W2);

  const RX=U_2, FT=0.048;
  for(const s of [-1,1]){
    const x=s*RX;
    P.mach.push(mMove(mPlate(U_4+0.048,h,FT,0.012), x, h/2, P_FIELD)); // inner flange
    P.skin.push(mMove(mBox(0.064,h,0.190), x, h/2, P_FIELD+FT+0.095));  // the web
    P.mach.push(mMove(mPlate(U_4+0.064,h,FT,0.014), x, h/2, 0.330));    // outer flange
    // the web is rolled plate and the flanges are cut, which is true of a
    // real rolled section and is why the rib is legible from any angle
    for(const y of [0.42,1.32,2.04,3.12]){
      if(y>h-0.25) continue;
      P.mach.push(mMove(mTilt(mBolt(0.024,0.013),1.5707963), x, y, 0.378));
    }
  }

  /* FIVE MAINS, AND THEY ARE MAINS.  The last set were 30 to 40 mm --
     conduit, and conduit at four metres is a scratch on the wall.  These
     are 110 to 155 mm in the radius, so 220 to 310 across, which is what a
     ship's coolant and air trunks actually are: things you would have to
     climb over, that cast their own shadow on the wall behind them, and
     that read as PIPE and not as line from the far end of the corridor.
     The bay went to 2U to carry them, which is the right way round -- the
     part is sized by what is in it, not the other way about.

     AND THEY HAVE JOINTS.  A pipe with no coupling in twelve metres is
     extruded, not installed: a flanged collar every 900 mm is the single
     detail that makes a run read as pipework, and it costs one torus. */
  const MAINS=[[-0.800,0.140],[-0.400,0.110],[0.000,0.155],
               [0.400,0.115],[0.800,0.130]];
  for(let k=0;k<MAINS.length;k++){
    const x=MAINS[k][0], r=MAINS[k][1];
    if(k===1) continue;                            // this one is the copper
    P.duct.push(mMove(mPipe(r,h-0.30,12), x, B_KICK+0.030, P_FIELD+r+0.034));
    for(let y=B_KICK+0.46; y<h-0.34; y+=0.900){    // the flanged joints
      P.mach.push(mMove(mPipe(r+0.030,0.062,12), x, y, P_FIELD+r+0.034));
      P.gap .push(mMove(mPipe(r+0.008,0.014,12), x, y+0.062, P_FIELD+r+0.034));
    }
  }
  const cu=meshNew();
  {
    const x=MAINS[1][0], r=MAINS[1][1];
    mAddMesh(cu, mMove(mPipe(r,h-0.30,12), x, B_KICK+0.030, P_FIELD+r+0.034));
    for(let y=B_KICK+0.46; y<h-0.34; y+=0.900)
      mAddMesh(cu, mMove(mPipe(r+0.026,0.058,12), x, y, P_FIELD+r+0.034));
  }

  // clamps.  They are what makes a bundle read as installed rather than as
  // five pipes that happen to be next to each other.
  for(const y of [U_2,3*U_2,5*U_2]){
    if(y>h-0.20) continue;
    if(Math.abs(y-B_LINE)<0.14) continue;         // never over the light line
    P.mach.push(mMove(mPlate(IW-0.120,0.062,P_FIELD+0.130,0.012), 0, y, 0));
    P.gap .push(mMove(mPlate(IW-0.160,0.020,P_FIELD+0.104,0.005), 0, y-0.040, 0));
  }

  // the cross brace, at head height, tying the two ribs together
  const by=Math.min(h-0.50, 5*U_2);
  P.mach.push(mMove(mPlate(2*RX+0.240,0.180,0.070,0.016), 0, by, 0.330));
  P.gap  .push(mMove(mPlate(2*RX+0.200,0.030,0.048,0.006), 0, by-0.104, 0.330));

  /* WALL 2 is the bay that admits its services, so WALL 2 is the bay whose
     placard is a live one: this is the kit's ONE readout, and rule 3 keeps
     it that way.  One bay type in three means a corridor of nine has three
     boxes awake in it -- enough to say the ship still has power somewhere,
     few enough that it is still a derelict. */
  const mark=mMove(mPlate(0.300,0.192,0.020,0.006), -1.010, 1.500, 0.366);

  return [kGrpAll(P.skin,T.SKIN,2,32), kGrpAll(P.coat,T.COAT,2,34),
          kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,36), kGrpAll([cu],T.CU,2,36),
          kGrp(mark,T.READ,3)];
}});

/* ====================================================================
   K7  WALL 3  --  SERVICE & AIR BAY, 1.30 m

   The loud one, and the reason to have three.  Air has to come from
   somewhere, and a louvre bank is the one wall element that is
   unmistakably MACHINERY: a stack of tilted slats, each lit on its top
   edge and black underneath, so the panel reads as a stripe pattern that
   changes as you walk past it.  A texture cannot do that, because the
   pattern is parallax.

   The slats are cut steel and the pocket behind them is the shadow-gap
   black, which is the entire effect -- give the pocket the same finish as
   the slats and the bank fills in and disappears from four metres away.
   ==================================================================== */
kitDef({ name:'WALL 3', sub:'service & air bay · 1.30 m', proj:2,
  clip:h=>bayClip(h,0.150), emit:bayEmit,
  make:function(h){
  h=h||WALL_H;
  const IW=B_W-2*B_STILE;
  const P={mach:[],gap:[],skin:[],coat:[],duct:[]};
  bayShell(h,P);

  /* one louvre bank: a black pocket, four bars round it, and n slats.
     The frame is FOUR BARS, never a plate over an opening -- a plate here
     is a hole with a lid on it and the slats may as well not exist. */
  function bank(y0,y1,n){
    const FW=0.070, W2=IW-0.030;
    P.gap.push(mMove(mPlate(W2,y1-y0,0.018,0.005), 0,(y0+y1)/2, 0));
    P.mach.push(mMove(mPlate(W2,FW,P_KICK,0.012), 0, y0-FW/2, 0));       // sill
    P.mach.push(mMove(mPlate(W2,FW,P_KICK,0.012), 0, y1+FW/2, 0));       // head
    for(const s of [-1,1])
      P.mach.push(mMove(mPlate(FW,y1-y0+2*FW,P_KICK,0.012),
                        s*(W2/2-FW/2), (y0+y1)/2, 0));                   // jambs
    const pitch=(y1-y0)/n;
    for(let i=0;i<n;i++)
      P.mach.push(mMove(mTilt(mPlate(W2-0.150,0.052,0.058,0.008),-0.64),
                        0, y0+pitch*(i+0.5), 0.024));
  }
  bank(U_4-0.060, 5*U/4, 12);                     // the main intake
  bank(B_LINE+U_4-0.180, Math.min(h-B_HEAD-0.120, B_LINE+3*U_4), 6);  // its echo

  // the inspection hatch between them: sprayed, dogged, with a black
  // gasket line all round so it reads as a thing that opens
  const hy0=27*U/20, hy1=B_LINE-0.126;
  P.gap .push(mMove(mPlate(IW-0.020,hy1-hy0+0.040,0.026,0.006),0,(hy0+hy1)/2,0));
  // a hatch is a lid, so it wears what WALL 1's lids wear -- rule 2
  const lid=[kGrpAll([mMove(mPlate(IW-0.070,hy1-hy0,P_PANEL,0.016),
                            0,(hy0+hy1)/2,0)], T.ACC, 3, 34)];
  for(const s of [-1,1]) for(const t of [-1,1]){
    P.mach.push(mMove(mTilt(mPipe(0.026,0.026,8),1.5707963),
                      s*(IW/2-0.104), (hy0+hy1)/2+t*((hy1-hy0)/2-0.090), P_PANEL));
    P.mach.push(mMove(mPlate(0.088,0.014,P_PANEL+0.028,0.005),
                      s*(IW/2-0.104), (hy0+hy1)/2+t*((hy1-hy0)/2-0.090), P_PANEL));
  }
  P.duct.push(mMove(mPlate(IW-0.070,0.018,P_PANEL-0.030,0.004),0,hy0-0.009,0)); // gasket

  // a coolant run dropping down the side and turning into the hatch
  const cu=meshNew();
  mAddMesh(cu, mMove(mPipe(0.030, hy0-B_KICK-0.10, 10), -0.408, B_KICK+0.10, P_KICK));
  mAddMesh(cu, mMove(mTilt(mPipe(0.030,0.170,10),-1.5707963), -0.408, hy0+0.120, P_KICK));
  mAddMesh(cu, mMove(mTorus(14,6,0.030,0.014), -0.408, hy0+0.120, P_KICK+0.05));

  const mark=mMove(mPlate(0.216,0.144,0.018,0.005), U_4, 5*U/4+0.108, P_KICK);

  return [kGrpAll(P.skin,T.SKIN,2,32), kGrpAll(P.coat,T.COAT,2,34),
          kGrpAll(P.mach,T.MACH,2,38), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,36), kGrp(cu,T.CU,2),
          kGrp(mark,T.MARK,3)].concat(lid);
}});

/* ====================================================================
   THE COLUMNS

   Doom 3's kit worked because its parts were in whole ratios to each
   other -- wall panels twice a floor plate, pillars half a wall panel,
   trims half a floor plate -- and it worked because the eye reads a 2:1
   instantly and cannot read a 1.7:1 at all.  A part that is 1.13 modules
   wide does not fail to fit.  It fails to look like it came from the
   same ship as the part beside it.

   So every column here is the same two numbers:

       footprint   U/2   0.600   -- half a wall bay, exactly
       shaft       U/4   0.300   -- half its own footprint

   and every one of them acknowledges 2U, the height the bays run their
   light line at.  A stanchion whose collar is at 2.400 and a bay whose
   channel is at 2.400 are the same building; a stanchion with a collar
   at 2.36 is a prop somebody dropped in the room.
   ==================================================================== */
function mFace(m,theta,d){          // a +Z-facing plate, turned to face theta
  mSpin(m, theta-1.5707963);
  return mMove(m, Math.cos(theta)*d, 0, Math.sin(theta)*d);
}
const CARD=[0, 1.5707963, 3.1415927, 4.7123890];   // +X, +Z, -X, -Z

/* ====================================================================
   K8  COLUMN 1  --  STANCHION, 600 mm base on a 300 mm section

   The honest one.  A rolled section, a base it is bolted through, a cap
   it takes the deck on, and gussets at the foot where the moment
   actually is -- nothing clad, so the eye reads the load path in a
   single look and the section's own shadow does the modelling: the web
   is darker than the flanges from every angle there is.

   What is new is that the flange is not a face, it is a CHANNEL.  Two
   machined strips with a black slot between them, and the slot is lit.
   That turns the one part of a stanchion nobody looks at into the thing
   that lights the room, and it puts the same vertical line on a column
   that CORNER 1 puts on a corner -- so a hall of these and a corridor of
   bays are lit by the same language.
   ==================================================================== */
kitDef({ name:'COLUMN 1', sub:'stanchion · 300 mm section', proj:2,
  clip:h=>{ h=h||WALL_H;
    // the core stops 24 mm short of both flange faces, and the flange
    // strips close it back up either side of the channel -- so what stays
    // open is the 108 mm the channel occupies, which the diffuser fills
    return [[-U_8,0,-0.126, U_8,h,0.126],
            [-U_8,0, 0.126,-0.054,h, U_8],[ 0.054,0, 0.126, U_8,h, U_8],
            [-U_8,0,-U_8,  -0.054,h,-0.126],[ 0.054,0,-U_8,  U_8,h,-0.126],
            [-U_4,0,-U_4, U_4,0.06,U_4]]; },
  emit:h=>{ h=h||WALL_H;
    const E=[];
    for(const q of litRun(0.240, h-U_8-0.036, 0.036)){
      if(Math.abs((q[0]+q[1])/2-2*U)<0.16) continue;     // the collar's own gap
      for(const s of [-1,1])
        E.push([-0.024,q[0],s*0.150-s*0.028, 0.024,q[1],s*0.150-s*0.014, T.CHAN]);
    }
    return E; },
  make:function(h){
  h=h||WALL_H;
  const MACH=[], GAP=[], SKIN=[], COAT=[], DUCT=[];
  const y0=0.048, y1=h-0.048, hs=y1-y0, FT=0.048;

  MACH.push(mSlab(U_2,U_2,y0,0.014));                      // the base
  MACH.push(mMove(mSlab(0.480,0.480,0.048,0.014), 0,h-0.048,0));   // the cap
  SKIN.push(mMove(mBox(0.048,hs,U_4-2*FT), 0, y0+hs/2, 0));        // the web

  for(const s of [-1,1]){
    const th=s>0?1.5707963:4.7123890, zf=U_8-FT;
    // the flange, as two machined strips with a slot down the middle
    for(const t of [-1,1])
      MACH.push(mFace(mMove(mPlate(0.118,hs,FT,0.014), t*0.091,y0+hs/2,0), th, zf));
    // 64 mm wide and 40 mm deep.  It was 112 and 24, and at that size it
    // stopped being a slot with light in it and became a light panel:
    // 0.61 m2 of emitter on a 300 mm column, which is eleven times what a
    // whole wall bay puts out.
    GAP.push(mFace(mMove(mPlate(0.064,hs,0.020,0.005), 0,y0+hs/2,0), th, zf));
    for(const y of litRun(0.240,h-U_8-0.036,0.036).map(q=>q[1]+0.036))
      if(y<h-U_8-0.05 && Math.abs(y-2*U)>0.16)
        MACH.push(mFace(mMove(mPlate(0.108,0.056,FT+0.008,0.010),0,y,0), th, zf));
    for(const y of [0.42,1.32,3.06]){                      // and its fixings
      if(y>h-0.30) continue;
      if(Math.abs(y-2*U)<0.20) continue;
      for(const t of [-1,1])
        MACH.push(mFace(mMove(mTilt(mBolt(0.019,0.010),1.5707963),
                              t*0.102,y,0), th, U_8));
    }
  }

  // the collar, at 2U, where every bay in the ship runs its light line
  MACH.push(mMove(mSlab(0.360,0.360,0.216,0.018), 0, 2*U-0.108, 0));
  for(const t of [-1,1])
    GAP.push(mMove(mSlab(0.312,0.312,0.024,0.005), 0, 2*U+t*0.132-0.012, 0));

  DUCT.push(mMove(mSlab(0.336,0.336,U_8,0.014), 0, y0, 0));   // the kick wrap
  MACH.push(mMove(mSlab(0.360,0.360,U_8+0.024,0.012), 0, y0, 0));

  for(const s of [[-1,-1],[1,-1],[1,1],[-1,1]])              // holding down
    MACH.push(mMove(mBolt(0.028,0.014), s[0]*0.228, y0, s[1]*0.228));
  for(const s of [-1,1]){                                    // gussets
    const g=mBox(0.022,U_4,U_4);
    mDeform(g,p=>{ const t=(p[1]+U_8)/U_4; p[2]*=(1.0-t*0.86); });
    COAT.push(mMove(g, s*0.086, y0+U_8+U_8, 0));
  }
  const mark=mFace(mMove(mPlate(0.170,0.112,0.018,0.005),0,1.500,0), 1.5707963, U_8);

  return [kGrpAll(MACH,T.MACH,2,34), kGrpAll(GAP,T.GAP,2,40),
          kGrpAll(SKIN,T.SKIN,2,34), kGrpAll(COAT,T.COAT,2,32),
          kGrpAll(DUCT,T.DUCT,2,36), kGrp(mark,T.MARK,3)];
}});

/* ====================================================================
   K9  COLUMN 2  --  SERVICE STACK, 600 mm base

   Not structure at all: four runs held off the deck in collars, which is
   what most of the vertical clutter in a real machinery space turns out
   to be.  It earns its place by being the only column that is
   IRREGULAR -- four diameters at four offsets, stopping at four heights
   -- so a room with two of them never looks laid out on a grid, even
   though its base and its collars are.

   That is the trick worth keeping: the module is rigid and what sits on
   it is not.  The collars are at 0.600, 1.800 and 3.000 -- U/2 apart,
   the same pitch as the floor panels -- and the runs between them are at
   nothing in particular.

   One run is copper.  A bundle in which every line is the same black is
   a bundle nobody routed, and the copper is the only warm thing in the
   part.
   ==================================================================== */
kitDef({ name:'COLUMN 2', sub:'service stack · 4 runs', proj:2,
  clip:h=>[[-0.216,0,-0.216, 0.216,(h||WALL_H),0.216],
           [-U_4,0,-U_4, U_4,0.06,U_4]],
  emit:h=>[[-0.048,2*U-0.075,0.222, 0.048,2*U+0.075,0.236, T.CHAN]],
  make:function(h){
  h=h||WALL_H;
  const MACH=[], GAP=[], DUCT=[], COAT=[], CU=[];
  const y0=0.048;
  MACH.push(mSlab(U_2,U_2,y0,0.014));
  COAT.push(mMove(mSlab(0.432,0.432,0.084,0.014), 0,y0,0));

  const runs=[[ 0.000,-0.030,0.084,1.00],[ 0.150, 0.060,0.048,0.78],
              [-0.120, 0.135,0.042,1.00],[-0.075,-0.150,0.033,0.62]];
  runs.forEach((r,i)=>{
    (i===2?CU:DUCT).push(mMove(mPipe(r[2],(h-0.132)*r[3],12), r[0],0.132,r[1]));
    if(r[3]<1)                                   // a flange where a run stops
      MACH.push(mMove(mPipe(r[2]*1.55,0.030,12), r[0],0.132+(h-0.132)*r[3]-0.03,r[1]));
  });

  for(const y of [U_2,3*U_2,5*U_2]){             // collars, on the panel pitch
    if(y>h-0.20) continue;
    MACH.push(mMove(mTilt(mTorus(20,6,0.204,0.030),1.5707963), 0,y,0));
    MACH.push(mMove(mPlate(0.456,0.078,0.048,0.012), 0,y,0.204));
    GAP  .push(mMove(mPlate(0.408,0.020,0.020,0.005), 0,y-0.054,0.204));
  }

  DUCT.push(mMove(mPipe(0.106,Math.min(0.90,Math.max(0.2,h-2.05)),12), 0,2.05,0));
  MACH.push(mMove(mTilt(mTorus(16,6,0.106,0.018),1.5707963),0,2.05,0));

  MACH.push(mMove(mTorus(20,6,0.160,0.024), 0.0, 3*U_2, 0.180));   // the handwheel
  for(let k=0;k<4;k++)
    MACH.push(mMove(mRoll(mBox(0.276,0.024,0.024),k*0.7853982), 0.0,3*U_2,0.180));
  MACH.push(mMove(mTilt(mPipe(0.044,0.09,10),-1.5707963), 0,3*U_2,0.126));

  // the indicator, at 2U, so even the pipe stack agrees where the line is
  MACH.push(mMove(mPlate(0.140,0.190,0.230,0.014), 0, 2*U, 0));
  GAP  .push(mMove(mPlate(0.104,0.150,0.222,0.006), 0, 2*U, 0));

  return [kGrpAll(MACH,T.MACH,2,36), kGrpAll(GAP,T.GAP,2,40),
          kGrpAll(DUCT,T.DUCT,2,36), kGrpAll(COAT,T.COAT,2,32),
          kGrpAll(CU,T.CU,2,36)];
}});

/* ====================================================================
   K10  COLUMN 3  --  FLUTED DRUM, 600 mm plinth on a 300 mm shaft

   The only round thing in the kit, and that is the entire point: in a
   world of plates and right angles a cylinder is the shape the eye finds
   first, so this is the column for the middle of a hall and never for a
   corridor.

   The flutes are CUT, not drawn -- alternate faces of a sixteen-sided
   drum pushed 24 mm in, which gives eight vertical highlights that
   travel round the column as you walk past it.  That is the one effect
   that proves at a glance the geometry is real and not a normal map,
   because a normal map's highlight cannot move independently of the
   surface it is on.

   Four of the eight flutes -- the cardinals -- are lit.  A round column
   cannot carry an axis-aligned emitter anywhere else, and it does not
   need to: four is what makes it read as a beacon at the end of a hall
   rather than as decoration.
   ==================================================================== */
kitDef({ name:'COLUMN 3', sub:'fluted drum · 300 mm shaft', proj:2,
  clip:h=>{ h=h||WALL_H;
    return [[-0.126,0,-0.126, 0.126,h,0.126],
            [-U_4,0,-U_4, U_4,0.216,U_4],
            [-U_4,h-0.216,-U_4, U_4,h,U_4]]; },
  emit:h=>{ h=h||WALL_H;
    const E=[];
    for(const q of litRun(0.264, h-0.264, 0.040)){
      if(Math.abs((q[0]+q[1])/2-2*U)<0.20) continue;     // the collar sits here
      E.push([ 0.132,q[0],-0.016, 0.146,q[1], 0.016, T.CHAN]);
      E.push([-0.146,q[0],-0.016,-0.132,q[1], 0.016, T.CHAN]);
      E.push([-0.016,q[0], 0.132, 0.016,q[1], 0.146, T.CHAN]);
      E.push([-0.016,q[0],-0.146, 0.016,q[1],-0.132, T.CHAN]);
    }
    return E; },
  make:function(h){
  h=h||WALL_H;
  const MACH=[], GAP=[], COAT=[];
  MACH.push(mSlab(U_2,U_2,0.048,0.014));                    // the pad, U/2

  const plinth=mSpin(mMove(mScale(mCylinder(16,true),0.490,U_8,0.490),0,0.048+U_8/2,0),-0.19634954);
  mSelNone(plinth); mSelNormal(plinth,0,1,0,0.20); mExtrude(plinth,0.010,0.050);
  MACH.push(plinth);

  const y0=0.048+U_8, hs=h-y0-0.216;
  const sh=mSpin(mMove(mScale(mCylinder(16,false),0.306,hs,0.306),0,y0+hs/2,0),-0.19634954);
  mSelEvery(sh,2,0); mExtrude(sh,-0.024,0.0);               // the flutes, cut
  COAT.push(sh);

  // the collar, at 2U -- the same height the bays and the corner use
  const col=mSpin(mMove(mScale(mCylinder(16,true),0.408,0.216,0.408),0,2*U-0.108,0),-0.19634954);
  mSelNone(col); mSelNormal(col,0,1,0,0.20); mExtrude(col,0.008,0.044);
  MACH.push(col);

  const cap=mSpin(mMove(mScale(mCylinder(16,true),0.470,U_8,0.470),0,h-0.216,0),-0.19634954);
  mSelNone(cap); mSelNormal(cap,0,1,0,0.20); mExtrude(cap,0.009,0.052);
  MACH.push(cap);
  MACH.push(mMove(mSlab(U_2-0.048,U_2-0.048,0.066,0.016), 0, h-0.066, 0));

  // the four lit flutes get a black floor, so the diffuser sits in a slot
  for(const a of CARD){
    for(const q of litRun(0.264,h-0.264,0.040)){
      if(Math.abs((q[0]+q[1])/2-2*U)<0.20) continue;
      GAP.push(mFace(mMove(mPlate(0.056,q[1]-q[0],0.016,0.005),0,(q[0]+q[1])/2,0),
                     a, 0.118));
      MACH.push(mFace(mMove(mPlate(0.092,0.052,0.030,0.008),0,q[1]+0.040,0),
                      a, 0.126));                        // the strap above it
    }
  }
  for(let k=0;k<4;k++){                                      // pad fixings
    const a=k*1.5707963+0.7853982;
    MACH.push(mMove(mBolt(0.026,0.013), Math.cos(a)*0.232, 0.048, Math.sin(a)*0.232));
  }

  return [kGrpAll(MACH,T.MACH,2,32), kGrpAll(GAP,T.GAP,2,40),
          kGrpAll(COAT,T.COAT,2,30)];
}});

/* ====================================================================
   K11  CORNER 1  --  OUTSIDE CORNER POST, 560 mm

   The part the kit was missing, and the reason the walls could not be
   assembled into anything.  Two bay runs meeting at 90 degrees leave two
   raw ends and a butt joint, and no amount of surface detail hides a butt
   joint -- the eye goes straight to it.

   A corner post does three jobs at once, and it has to do all three or it
   is just a box in the way:

     * IT COVERS.  It stands 280 mm off both wall planes, which is 130 mm
       proud of the proudest thing a bay has (WALL 1's handrail, at 150).
       So a run can end anywhere within 280 mm of the arris and the post
       eats the difference.  That is what makes the kit assemblable: the
       bays never have to divide evenly into the corridor.
     * IT TURNS.  Its section is an octagon, not a square, so walking round
       it the silhouette changes four times instead of once, and the two
       45-degree chamfers catch a lamp neither wall face can.
     * IT LIGHTS.  A full-height channel down each of the two corridor-
       facing flats, cut by the collar at 2.42 m where the bays' horizontal
       lines arrive.  A corridor built from this kit is lit almost entirely
       by its own corners: horizontals down the runs, verticals at every
       turn, and the turn is where the two meet.

   LOCAL FRAME.  The origin is the ARRIS -- the vertical line where the two
   wall face planes cross.  Wall A's face is the plane z=0 and looks toward
   +Z; wall B's face is x=0 and looks toward +X; the solid is behind both,
   at x<0 and z<0.  So at yaw 0 this post fits the corner whose two rooms
   are +X and +Z, and the other three orientations are yaw 90, 180, 270.
   ==================================================================== */
const C_A = U_4;                   // apothem, so U/2 = 600 mm across flats
const C_SHOE=U_8, C_CAP=0.186;

function mOct(a,h){                // an eight-sided shaft, standing on y=0
  const m=mMove(mScale(mCylinder(8,false), a/0.46194, h, a/0.46194),0,h/2,0);
  return mSpin(m,-0.3926991);      // a flat facing +X, +Z and both diagonals
}
function mOctCap(a,t){             // a capped drum of the same section
  const m=mMove(mScale(mCylinder(8,true), a/0.46194, t, a/0.46194),0,t/2,0);
  return mSpin(m,-0.3926991);
}
/* a +Z-facing plate, turned onto one of the post's flats and pushed out
   to it.  d is the distance from the axis to the plate's BACK. */
function mOnZ(m,d){ return mMove(m, 0,0,d); }
function mOnX(m,d){ return mMove(mSpin(m,-1.5707963), d,0,0); }
function mOnD(m,d){ return mMove(mSpin(m,-0.7853982), d*0.7071068,0,d*0.7071068); }

kitDef({ name:'CORNER 1', sub:'outside corner post · 560 mm', proj:2,
  clip:h=>{ h=h||WALL_H;
    // The body stops 80 mm short of both corridor flats, because the bake
    // finds an emitter by ray-casting the BRUSH set: a diffuser sealed
    // inside a clip box lights nothing.  The lips either side of each
    // channel are closed back up, so what is left open is the 140 mm the
    // channel itself occupies -- and the diffuser brush stands in it and
    // stops you there anyway.
    return [[-C_A,0,-C_A, 0.220,h,0.220],
            [ 0.220,0,-C_A,  C_A,h,-0.075],
            [ 0.220,0, 0.075, C_A,h, 0.220],
            [-C_A,0, 0.220,-0.075,h, C_A],
            [ 0.075,0, 0.220, 0.220,h, C_A]]; },
  emit:h=>{ h=h||WALL_H;
    const y0=C_SHOE+0.030, y1=h-C_CAP-0.030, E=[];
    for(const q of [[y0,B_LINE-0.088],[B_LINE+0.088,y1]]){
      if(q[1]-q[0]<0.15) continue;
      E.push([-0.072,q[0],0.242,  0.072,q[1],0.260, T.CHAN]);   // down corridor A
      E.push([ 0.242,q[0],-0.072, 0.260,q[1],0.072, T.CHAN]);   // down corridor B
    }
    return E; },
  make:function(h){
  h=h||WALL_H;
  const MACH=[], GAP=[], COAT=[], DUCT=[];
  const y0=C_SHOE, y1=h-C_CAP;

  // ---- the shaft ------------------------------------------------------
  // Sprayed, not bare: the post has to be the darker thing so the machined
  // work on it reads.  Tapered 14 mm over its height, which is below the
  // threshold at which anyone notices a taper and well above the one at
  // which a column stops looking like an extrusion.
  const sh=mOct(C_A,y1-y0);
  mDeform(sh,p=>{ const t=p[1]/(y1-y0); p[0]*=1-t*0.050; p[2]*=1-t*0.050; });
  COAT.push(mMove(sh,0,y0,0));

  // ---- the shoe and the cap -------------------------------------------
  // Both flared, both machined.  A post that meets the deck at the same
  // width it has at head height is a pipe; a post with a foot is a post.
  const shoe=mOctCap(C_A+0.062,C_SHOE);
  mSelNone(shoe); mSelNormal(shoe,0,1,0,0.20); mExtrude(shoe,0.012,0.048);
  MACH.push(shoe);
  GAP.push(mMove(mOctCap(C_A+0.008,0.030),0,C_SHOE-0.001,0));   // the reveal over it

  const cap=mOctCap(C_A+0.050,C_CAP);
  mSelNone(cap); mSelNormal(cap,0,1,0,0.20); mExtrude(cap,0.010,0.052);
  MACH.push(mMove(cap,0,h-C_CAP,0));
  GAP.push(mMove(mOctCap(C_A+0.006,0.028),0,h-C_CAP-0.027,0));

  // ---- the collar, where the horizontals arrive ------------------------
  // The bays' light lines run in at 2.42 from both sides.  If the post's
  // vertical channel ran straight past them the two would simply cross,
  // and a crossing reads as an accident.  A machined collar at exactly
  // that height is the junction: both lines die into it, and the eye reads
  // the corner as the thing that JOINS the runs rather than interrupts.
  const col=mOctCap(C_A+0.038,0.176);
  mSelNone(col); mSelNormal(col,0,1,0,0.20); mExtrude(col,0.008,0.044);
  MACH.push(mMove(col,0,B_LINE-0.088,0));
  for(const t of [-1,1])
    GAP.push(mMove(mOctCap(C_A+0.004,0.022),0,B_LINE+t*0.110-0.011,0));

  // ---- the two light channels -----------------------------------------
  // One down each corridor, on the flats that are parallel to the wall
  // planes -- so from inside either corridor the vertical line sits
  // exactly on the axis the horizontal line is running along.
  const segs=[[y0+0.030,B_LINE-0.088],[B_LINE+0.088,y1-0.030]];
  for(const put of [mOnZ,mOnX]) for(const q of segs){
    const len=q[1]-q[0];
    if(len<0.15) continue;
    // the black recess...
    GAP.push(put(mMove(mPlate(0.150,len,0.020,0.005),0,(q[0]+q[1])/2,0), 0.232));
    // ...and the two machined lips that hide the tube in it
    for(const s of [-1,1])
      MACH.push(put(mMove(mPlate(0.052,len,0.062,0.010),
                          s*0.101,(q[0]+q[1])/2,0), 0.238));
    for(const s of [-1,1])                       // and the ends, capped
      MACH.push(put(mMove(mPlate(0.254,0.048,0.062,0.010),
                          0,q[s<0?0:1]-s*0.024,0), 0.238));
  }

  // ---- the nose -------------------------------------------------------
  // The 45 degree flat is the only face you see from BOTH corridors at
  // once, so it is the one that gets the writing, and a machined arris rib
  // so the corner has a hard vertical edge to catch a lamp on.
  MACH.push(mOnD(mMove(mPlate(0.086,y1-y0-0.060,0.038,0.010),0,(y0+y1)/2,0), C_A-0.004));
  for(const s of [-1,1])
    GAP.push(mOnD(mMove(mPlate(0.020,y1-y0-0.060,0.014,0.004),
                        s*0.056,(y0+y1)/2,0), C_A-0.006));
  const mark=mOnD(mMove(mPlate(0.180,0.120,0.020,0.005),0,1.500,0), C_A+0.030);

  // ---- where the handrail dies ----------------------------------------
  // WALL 1's rail crosses its pilasters and runs the length of a corridor.
  // It has to STOP somewhere, and stopping in mid-air is what makes a kit
  // look like a kit.  A machined boss on each corridor flat, at rail
  // height, is where it stops.
  for(const put of [mOnZ,mOnX]){
    MACH.push(put(mMove(mPlate(0.230,U_8,0.048,0.012),0,B_RAIL_Y,0), C_A-0.002));
    GAP .push(put(mMove(mPlate(0.196,0.020,0.020,0.005),0,B_RAIL_Y-0.090,0), C_A-0.004));
  }

  // ---- a jacketed drop, off-centre, so the post is not symmetric -------
  const dz=Math.min(B_LINE-0.20, h-1.2);
  DUCT.push(mOnD(mMove(mPipe(0.042,dz-C_SHOE,10),0.148,C_SHOE,0), C_A-0.030));
  for(const y of [U_2,3*U/2])
    if(y<dz-0.10)
      MACH.push(mOnD(mMove(mPlate(0.120,0.046,0.052,0.008),0.148,y,0), C_A-0.010));

  // ---- holding-down bolts ---------------------------------------------
  for(let k=0;k<4;k++){
    const a=k*1.5707963+0.7853982;
    MACH.push(mMove(mBolt(0.030,0.015), Math.cos(a)*0.272, C_SHOE, Math.sin(a)*0.272));
  }

  return [kGrpAll(COAT,T.COAT,2,30), kGrpAll(MACH,T.MACH,2,34),
          kGrpAll(GAP,T.GAP,2,40), kGrpAll(DUCT,T.DUCT,2,36), kGrp(mark,T.MARK,3)];
}});

/* ---- stamping -------------------------------------------------------
   MODULES is the placement list the map generator writes.  Anything a
   part declares in `clip` is stamped as a real brush so the player has
   something to stand on and something to walk into. */

/* ====================================================================
   K12  CEILING 1  --  OVERHEAD, one module, a lit run and a return

   The third surface, and the one the kit had nothing for at all.  A
   corridor whose deck and walls are modelled and whose overhead is a
   painted brush is a corridor with a lid on it: the eye reads the two
   good surfaces, finds the third has no scale in it, and settles on the
   whole room being a box after all.

   It is the deck's module, hung, and that is deliberate -- the same 1.2 m
   tile, the same 16 mm seam land on the -x and -z boundary, so an overhead
   and the deck under it agree about where the ship's grid is.  What makes
   it a ceiling and not a mirror is what it does with the middle:

     THE LIT RUN.  A 240 mm channel down the full length of the tile, two
     blades hanging 72 mm either side of it, and the diffuser at the top
     where you cannot see it.  Because it runs edge to edge, a column of
     these makes ONE unbroken line the length of the corridor -- which is
     the single thing every reference overhead is drawn with, and the thing
     that gives a passage its perspective.  It is on when bit 0 of the
     variant is, so the placement decides which runs are lit and a corridor
     gets lines at a spacing rather than a lit ceiling.

   Either side of it, one panel each: a return-air louvre or a plain
   sprayed lid, both of them PICTURES on a plate that already exists (see
   the plate language above).  Nothing here is modelled that a material
   could carry, which is the whole point of the part: nine plates, and it
   reads as an engineered overhead.
   ==================================================================== */
const C_SLOT = U/5;                     // 0.240 -- the lit channel
const C_LIP  = 0.036;                   // and the blades either side of it
const C_PANW = 0.400, C_PAND = U - 0.200;

kitDef({ name:'CEILING 1', sub:'overhead \u00b7 lit run & return', proj:2,
  // The diffuser sits 20 mm up inside the channel, so what you see is the
  // LIT SLOT between two blades and never the source -- the wall's light
  // line, turned through ninety degrees, which is why they read as one
  // building rather than as a wall kit and a ceiling kit.
  emit:v=>((v|0)&1)
    ? [[-C_SLOT/2+0.014,-0.042,-U/2, C_SLOT/2-0.014,-0.026,U/2, T.CHAN]] : [],
  make:function(v){
  v=v|0;
  const P={mach:[],gap:[],skin:[],coat:[]};
  const face=[];

  // the soffit itself: rolled plate.  Nobody has ever touched a ceiling,
  // and it is the one surface in the kit that has no reason to be sprayed.
  P.skin.push(mSoffit(U,U,0.024,0.003,1));

  // the seam lands: this tile owns its -x and -z boundary and the
  // neighbour owns the other two, so a field has one land per seam
  P.mach.push(mMove(mSoffit(0.016,U,0.040,0.004), -U_2,0,0));
  P.mach.push(mMove(mSoffit(U,0.016,0.040,0.004), 0,0,-U_2));

  // ---- the lit run ----------------------------------------------------
  if(v&1){
    P.gap.push(mSoffit(C_SLOT,U,0.010,0.003));            // the channel floor
    for(const s of [-1,1])                                 // and its blades
      P.mach.push(mMove(mSoffit(C_LIP,U,0.060,0.008),
                        s*(C_SLOT/2+C_LIP/2), 0, 0));
  } else {
    // unlit tiles close the channel with a plain strip, so the run reads
    // as a fitting that is OUT rather than as a hole in the ceiling
    P.coat.push(mSoffit(C_SLOT+2*C_LIP,U,0.048,0.008));
  }

  // ---- the two panels either side -------------------------------------
  for(let k=0;k<2;k++){
    const x=(k?1:-1)*(C_SLOT/2+C_LIP+0.022+C_PANW/2);
    P.gap.push(mMove(mSoffit(C_PANW+0.024,C_PAND+0.024,0.036,0.005), x,0,0));
    const pl=mMove(mSoffit(C_PANW,C_PAND,0.060,0.012), x,0,0);
    if((v>>(1+k))&1) face.push(kGrpAll([pl], T.LOUV, 3, 32));
    else P.coat.push(pl);
  }

  return [kGrpAll(P.skin,T.SKIN,2,32), kGrpAll(P.mach,T.MACH,2,34),
          kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.coat,T.COAT,2,32)].concat(face);
}});


/* ====================================================================
   K13  SKIRT 1  --  the deck-to-wall splay, 1U long

   The junction the whole corridor is read by, and the reason the
   reference images have a warm line along the bottom of both walls:

     - the splay face leans out over the deck, 228 mm of 45 degree
       plate, so it catches the overhead lamp at grazing incidence and
       is the brightest horizontal thing in the frame;
     - it stops 72 mm short of the deck and stands on a recessed
       plinth, which leaves a slot;
     - the amber strip lives in that slot, and what you see is not the
       strip but a POOL ON THE DECK the length of the corridor.

   That is the entire lighting design of the reference in one part.  A
   wall bay lit from a lamp in the room reads as a model on a table; a
   wall bay whose own skirt is glowing onto the floor in front of it
   reads as a ship with the power still on.

   The face is a louvre PICTURE, not modelled slots -- a skirt at ankle
   height is never seen closely enough for parallax to matter, and this
   is exactly the trade the plate set exists to make.
   ==================================================================== */
kitDef({ name:'SKIRT 1', sub:'deck splay \u00b7 600 mm', proj:2,
  // NOTCHED AT THE STRIP.  The bake ray-casts the clip set before it
  // looks for an emitter, so a strip inside a clip box lights nothing at
  // all -- and because the brush still draws, the part looks correct and
  // the corridor stays dark.  y 0.044..0.108 is left open for it.
  clip:()=>[[-U/2,0,0, U/2,0.044,SPL],
            [-U/2,0.108,0, U/2,0.144,SPL],
            [-U/2,0.144,0, U/2,SPL,0.352]],
  emit:()=>[[-U/2+0.024,0.052,0.470, U/2-0.024,0.100,0.546, T.GLOW]],
  make:function(){
  const P={mach:[],gap:[],duct:[]};

  // the plinth the splay stands on, set back so the splay overhangs it
  P.gap .push(mMove(mPlate(U,0.144,0.488,0.010), 0,0.072,0));
  P.mach.push(mMove(mSlab(U,0.052,0.014,0.006), 0,0,0.516));   // the deck toe

  // the splay itself, and the machined nose along its top edge where it
  // dies into the wall -- the nose is the bright line, not the face
  const sk=mSplayA(U, 0.144, SPL, 0.456, 0.036, 0.010);
  P.mach.push(mMove(mBox(U,0.048,0.076), 0, SPL, 0.106));

  // a service run tucked in the angle, because the angle is the one place
  // on a ship where a pipe is out of everybody's way -- and at 600 mm of
  // splay there is finally room for a main rather than a conduit
  P.duct.push(mMove(mRoll(mPipe(0.082,U,14),1.5707963), -U/2,0.190,0.300));

  return [kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,36), kGrpAll([sk],T.LOUV,3,34)];
}});

/* ====================================================================
   K14  COVE 1  --  the wall-to-overhead splay, 1U long

   The same junction, upside down, and it does the opposite job: the
   skirt throws light DOWN onto the deck and this one runs the cool line
   the length of the corridor at the top of the wall.  Warm below, cool
   above -- the accent rule -- except that now both of them are on a
   45 degree face, so both are visible from the whole length of the
   passage instead of only from directly in front.

   It also carries the services.  Every ship has to put its cable runs
   somewhere, and the top angle is where a real one puts them: out of
   reach, out of the walkway, and inspectable from a ladder.
   ==================================================================== */
kitDef({ name:'COVE 1', sub:'overhead splay \u00b7 300 mm', proj:2,
  clip:()=>[[-U/2,0,0, U/2,0.050,SPL],        // notched at the strip,
            [-U/2,0.116,0, U/2,SPL,SPL]],      // same as SKIRT 1
  emit:()=>[[-U/2+0.024,0.056,0.112, U/2-0.024,0.106,0.172, T.CHAN]],
  make:function(){
  const P={mach:[],gap:[],skin:[],duct:[]};

  // the channel at the foot of the cove: a black recess, a machined lip
  // under it, and the diffuser lying in it
  P.gap .push(mMove(mPlate(U,0.106,0.086,0.010), 0,0.080,0.100));
  P.mach.push(mMove(mBox(U,0.048,0.104), 0, 0.028, 0.136));

  // the splay, starting above the channel
  const cv=mSplayB(U, 0.144, 0.128, 0.456, 0.032, 0.010);
  P.mach.push(mMove(mBox(U,0.050,0.084), 0, SPL, 0.562));   // its top nose

  // the loom, and it is the biggest thing in the angle on purpose: a
  // corridor whose services you cannot see is a corridor nobody built
  for(let i=0;i<3;i++)
    P.duct.push(mMove(mRoll(mPipe(0.092-i*0.018,U,14),1.5707963),
                      -U/2, 0.252+i*0.112, 0.230+i*0.108));
  for(const x of [-U/4, U/4])                    // and the straps clipping it
    P.mach.push(mMove(mSplayB(0.076,0.196,0.180,0.392,0.104,0.010), x,0,0));

  return [kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,36), kGrpAll([cv],T.COAT,3,34)];
}});

/* ====================================================================
   K15  FILLET 1  --  the vertical splay, for where two walls meet

   The last right angle in the kit, and the cheapest part in it: one
   45 degree plate across the arris, two machined arrises down its
   edges, and a black reveal behind so it reads as a fitted fillet
   rather than as a chamfer somebody modelled onto the wall.

   It is here because the rule has to be absolute to be worth having.
   A corridor whose deck and overhead junctions are splayed and whose
   corners are square looks like a corridor with a mistake in it.
   ==================================================================== */
kitDef({ name:'FILLET 1', sub:'corner splay \u00b7 300 mm', proj:2,
  clip:h=>[[-0.212,0,-0.212, 0.212,(h||WALL_H),0.212]],
  make:function(h){
  h=h||WALL_H;
  const P={mach:[],gap:[],skin:[]};
  const W=SPL*1.4142136;                          // 0.424 across the flats

  // the face, on the diagonal.  mFace turns a +Z plate to any heading, so
  // the fillet is the same primitive every column nose in the kit uses.
  P.skin.push(mFace(mMove(mPlate(W,h,0.030,0.008),0,h/2,0), 0.7853982, 0.150));
  for(const s of [-1,1])                          // its two arrises
    P.mach.push(mFace(mMove(mPlate(0.034,h,0.040,0.008),
                            s*(W/2-0.017),h/2,0), 0.7853982, 0.150));
  // the reveal behind it, so the fillet is a fitted piece and not a bevel
  P.gap.push(mFace(mMove(mPlate(W+0.030,h,0.008,0.004),0,h/2,0),
                   0.7853982, 0.128));
  // and it acknowledges 2U, because everything does
  for(const y of [B_LINE, B_LINE-U_2, B_LINE+U_2])
    if(y<h-0.10)
      P.mach.push(mFace(mMove(mPlate(W-0.030,0.038,0.052,0.008),0,y,0),
                        0.7853982, 0.150));

  return [kGrpAll(P.skin,T.SKIN,2,34), kGrpAll(P.mach,T.MACH,2,36),
          kGrpAll(P.gap,T.GAP,2,40)];
}});

/* ====================================================================
   K16  WALL 4  --  ASSEMBLED PANEL FIELD, 1.20 m

   The connective tissue, and the part the kit most needed: WALL 1, 2 and
   3 are all EVENTS.  A corridor built out of nothing but events has no
   rhythm, because rhythm is what you notice against a background and
   there was no background -- every bay was as loud as the one beside it.

   This one is the background.  Eight identical bolted panels on a strict
   two-by-four grid, one reveal between them, and nothing else: no rail,
   no bank, no bundle, no placard.  It is the wall equivalent of the deck
   field, and like the deck field its whole value is that it repeats.
   Lay six of these and one WALL 3 and the louvre bank finally reads as
   air intake rather than as more wall.

   Eight panels, eight material lookups, no triangles beyond the plates
   themselves -- the panel picture is the ACCESS material in each plate's
   own square.
   ==================================================================== */
kitDef({ name:'WALL 4', sub:'assembled panel field \u00b7 4U', proj:2,
  clip:h=>bayClip(h,P_STILE+0.012,B_W4), emit:()=>bayEmit(B_W4),
  make:function(h){
  h=h||WALL_H;
  const IW=B_W4-2*B_STILE;
  const P={mach:[],gap:[],skin:[],coat:[],duct:[]};
  bayShell(h,P,B_W4);

  /* the grid: two columns, and as many rows of 520 mm as the bay is tall.
     The pitch is fixed, so a 3 m bay and a 3.6 m bay show a different
     NUMBER of panels at the same size rather than the same number at
     different sizes -- which is what a real panelled wall does, and what
     stops two bays of unequal height reading as two different ships. */
  const lid=[];
  /* The panel COUNT comes off the width, not the other way round, so the
     panel size is fixed at about 520 mm whatever the bay is -- which is
     what makes a 4.8 m field and a 1.2 m event bay read as the same wall
     rather than as the same wall drawn at two scales. */
  const G=0.036, NC=Math.max(2,Math.round(IW/0.560));
  const PW=(IW-0.080-G*(NC-1))/NC, PH=0.520, y0=B_KICK+0.070;
  for(let r=0;;r++){
    const yc=y0+PH/2+r*(PH+G);
    if(yc+PH/2 > h-B_HEAD-0.040) break;
    if(Math.abs(yc-B_LINE) < (PH+B_SLOT)/2+0.060) continue;   // clear the line
    for(let c2=0;c2<NC;c2++){
      const xc=-IW/2+0.040+PW/2+c2*(PW+G);
      P.gap.push(mMove(mPlate(PW+0.022,PH+0.022,0.038,0.005), xc,yc,0));
      lid.push(kGrpAll([mMove(mPlate(PW,PH,P_PANEL,0.014), xc,yc,0)],
                       T.ACC, 3, 34));
      for(const s of [-1,1])                     // two fixings a panel
        P.mach.push(mMove(mTilt(mBolt(0.019,0.010),1.5707963),
                          xc+s*(PW/2-0.040), yc, P_PANEL));
    }
  }
  // one strap the full width, at the rail height every other bay uses, so
  // the field still lines up with the event bays either side of it
  P.mach.push(mMove(mPlate(B_W4,0.044,P_FIELD+0.052,0.009), 0, B_RAIL_Y, 0));

  return [kGrpAll(P.skin,T.SKIN,2,32), kGrpAll(P.coat,T.COAT,2,34),
          kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,34)].concat(lid);
}});

/* ====================================================================
   K17  FLOOR 4  --  DECK MARGIN, one module

   The zone against the wall, and the part that fixes the deck.

   A deck is not one material with things sprinkled on it.  It is a
   margin, a field and a band, and you can read all three off any ship
   photograph ever taken: the margin is where the water, the swarf and
   the cable go, so it is grated and it is dark; the field is where you
   walk, so it is plain and it is light; the band is where a service
   crosses, so it happens at intervals and never in between.

   This is the margin.  Its outer half -- the two cells on -x -- is a
   drain channel with a machined kerb, and its inner half is ordinary
   field plate, so the transition happens INSIDE one module and the two
   zones never need a trim piece between them.  Laid with the channel
   outboard down both sides of a corridor, it puts a dark line hard
   against each wall and makes the light field in the middle read as a
   walkway rather than as the whole floor.
   ==================================================================== */
kitDef({ name:'FLOOR 4', sub:'deck margin \u00b7 drain channel', proj:2,
  make:function(v){
  v=v|0;
  const P={mach:[],gap:[],coat:[],skin:[],duct:[]};
  deckFrame(P);
  const face=[];

  for(let k=0;k<4;k++){
    const c=deckCell(k);
    if(DECK_AT[k][0]<0){                       // the two cells outboard
      // the channel: a deep black pan, a grate over it, a kerb inboard
      P.gap.push(mMove(mSlab(DECK_PW,DECK_PW,0.018,0.004), c[0],0,c[1]));
      face.push(kGrpAll([mMove(mSlab(DECK_PW-0.052,DECK_PW-0.052,0.044,0.008),
                               c[0],0,c[1])], T.PERF, 3, 32));
      P.mach.push(mMove(mSlab(0.030,DECK_PW,0.062,0.006),
                        c[0]+DECK_PW/2-0.015,0,c[1]));      // the kerb
    } else {
      P.coat.push(mMove(mSlab(DECK_PW,DECK_PW,0.060,0.012), c[0],0,c[1]));
    }
  }
  return [kGrpAll(P.mach,T.MACH,2,34), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.coat,T.COAT,2,32), kGrpAll(P.skin,T.SKIN,2,32),
          kGrpAll(P.duct,T.DUCT,2,36)].concat(face);
}});


/* ====================================================================
   K18  BAND 1  --  SAFETY BAND, 1U of it

   A hazard band painted onto the deck plate was the wrong answer twice
   over.  It is not paint on this ship -- it is a bolted-down nosing, the
   thing you actually find at the edge of a hatch or in front of a vent,
   and it stands 60 mm proud with a machined kerb down each side.  So it
   is a PART, laid in a line across the passage, and the line is exactly
   as long as the number of blocks you lay.

   It also fixes the picture.  A band drawn onto a floor tile is a strip
   cut out of a world grid, and it landed as flat colour; a band that is
   its own part owns its own bounding box, which is what a picture needs
   (see the plate set) -- so the diagonals are drawn in the block's own
   square and there is nothing left for them to fall out of register with.
   The block is 1.2 x 0.3, so the square-cell rule gives it four cells of
   four bars each: 75 mm bars, which is what a real one is.
   ==================================================================== */
kitDef({ name:'BAND 1', sub:'safety band \u00b7 1U \u00d7 300 mm', proj:2,
  make:function(){
  const P={mach:[]};
  for(const z of [-0.132,0.132])                     // the kerbs
    P.mach.push(mMove(mSlab(U,0.036,0.072,0.008), 0,0,z));
  const top=mMove(mSlab(U-0.006,0.228,0.066,0.010), 0,0,0);
  return [kGrpAll(P.mach,T.MACH,2,34), kGrpAll([top],T.SAFE,3,34)];
}});

/* ====================================================================
   K19  WALL 5  --  VIEWPORT BAY, 3U

   The one bay in the kit you can see out of, and the only place in the
   ship where the eye is given something further away than four metres.
   That is the whole reason it exists: a corridor whose longest sightline
   is its own far end reads as an interior however well it is lit, and one
   window turns the same corridor into a place that is somewhere.

   IT IS A HOLE, AND THAT IS THE WHOLE PART.  The first cut of this bay
   painted the sky onto the glass, because the renderer was opaque-only and
   a window therefore could not be a hole.  It was wrong for one reason and
   the reason is fatal: a sky at 60 mm does not MOVE against the frame as
   you walk past, and a sky that does not move is a poster.  Everything a
   window does it does by parallax, so the renderer grew a transparent pass
   (`SURF` in shipmaterials.py, 8f in splice.py) and this part grew three
   layers at three real distances:

      0.084 m   THE PANE       48 mm of glass, blended, and its alpha is a
                               Fresnel term -- a window when you face it,
                               a mirror of the corridor when you do not
      4.800 m   THE PLANET     one sprite, one picture, unlit and unfogged,
                               its coverage in the roughness channel so the
                               sky behind shows through everything it is not
      7.200 m   THE CHAMBER    five plates of tiling starfield: a void box
                               hung outside the hull, which is where a sky
                               belongs

   The two sky layers move against each other and against the frame at
   three different rates, and that difference is the only thing the eye
   ever reads as distance.  It is also what hides the chamber's tiling:
   37 repeats every 1.2 m, and it would show that lattice against nothing
   -- but it never appears against nothing, only behind a nebula and forty
   nearer stars that repeat never.

   THE LEVEL HAS TO COOPERATE.  This is the only part in the kit that needs
   an opening cut in the brush it is stamped on -- see `wallPorts` in
   kitview.py.  Without one the hull's own face draws over the chamber and
   the window goes black; the part's own clip box, not the brush, is what
   stops you walking out of it.

   Everything else is what a pressure viewport actually is.  A 288 mm
   machined frame bolted to the shell, corbelled out of the wall at its
   sill and stepped back to a mounting flange at its head and jambs; four
   45 degree gussets so the opening is an octagon like every other opening
   on this ship; a rubber gasket the glass sits in; a cold channel down
   each jamb -- the only light in the bay, and the reason the frame reads
   at all in a dark passage; and a blast shutter parked in a slot across
   the head, showing 60 mm of its bottom rail, because a window on a ship
   is a hole in the hull and somebody has to be able to close it.

   There are no mullions.  There were three, and they were there to force
   the glass into two groups so a part-space picture could not tile -- a
   real reason for a fake problem, and the moment the sky moved outside the
   hull it stopped being a problem at all.  One light, 2.52 by 1.32.
   ==================================================================== */
const V_W    = 3*U;          // 3.600 -- the bay.  Sized by what is in it.
const V_AW   = 2.520;        // the aperture: 21 depth modules across ...
const V_AH   = 1.320;        //               11 up
const V_AY0  = 0.708, V_AY1 = V_AY0+V_AH;     // 0.708 .. 2.028
const V_BORD = U_8+0.030;    // 0.180 -- the frame's face all round
const V_FX   = V_AW/2+V_BORD;                 // 1.440
const V_FY0  = V_AY0-V_BORD, V_FY1 = V_AY1+V_BORD;
const V_CH   = 0.120;        // and the corner the aperture cuts off
/* AND WHAT IS BEHIND IT.  The chamber is a closed box, so its size does not
   decide whether it covers the view -- whatever angle you look at, there is
   a face of it there.  What its size decides is how far away the sky FEELS,
   and how much room it needs outboard of the hull without surfacing inside
   whatever is on the other side.  6.0 x 4.8 x 6.0 is the largest that fits
   between the bench's hall and its corridor, and 6 m of parallax against a
   frame 0.3 m from your eye is already a hundred to one. */
const V_VD   = 6.000;        // the chamber's back wall, outboard of the hull
const V_VW   = 6.000;        // 5U across ...
const V_VH   = 4.800;        // ... 4U high
const V_SPZ  = 4.200;        // the planet sprite, most of the way out
const V_SPW  = 4.200;        // and 3.5U square
/* THE DEPTH STACK, on the same U/100 = 12 mm grid as every other plane a
   bay shows the corridor.  The frame is 288 mm of it, which puts 216 mm
   of reveal between the glass and the frame's face -- enough that from
   anywhere but square on, the frame occludes a third of the sky and the
   window gains the parallax that says there is a wall thickness here. */
const V_ZB   = P_FIELD;      // 0.012  the frame's back, on the field
const V_ZF   = 0.300;        //        and its face
const V_ZFL  = 0.156;        //        the mounting flange, half way
const V_ZG   = 0.036;        // the glass sits here ...
const V_ZGT  = 0.048;        // ... and is 48 mm thick, because it is a hull
const V_RET  = 0.288;        // the corbel under the sill: a full 45 return

/* the eight edges of an octagonal opening -- a rectangle with its corners
   cut -- as [cx, cy, length, theta], where theta turns a +Y-facing plate
   to point OUT along that edge and a -Y-facing one to point IN.  Both the
   reveal and the frame ring are walked with it, which is the only way the
   two can be guaranteed to agree about where a corner is. */
function octEdges(hw,hh,ch){
  const d=ch*1.4142136, cx=hw-ch/2, cy=hh-ch/2;
  return [[0, hh, 2*(hw-ch), 0],                    // head
          [0,-hh, 2*(hw-ch), 3.1415927],            // sill
          [ hw,0, 2*(hh-ch),-1.5707963],            // jambs
          [-hw,0, 2*(hh-ch), 1.5707963],
          [ cx, cy, d,-0.7853982], [-cx, cy, d, 0.7853982],
          [ cx,-cy, d,-2.3561945], [-cx,-cy, d, 2.3561945]];
}

kitDef({ name:'WALL 5', sub:'viewport bay \u00b7 3U \u00b7 four lights', proj:2,
  clip:h=>{
    h=h||WALL_H;
    const hw=V_W/2, d=P_RAIL+0.010;
    /* Seven boxes, and the shape of them is the whole reason the window
       lights anything.  The bake ray-casts the CLIP set before it looks at
       anything else, so a lamp inside a clip box lights nothing whatever
       -- the same trap bayClip opens itself at the light line and the kick
       for.  Here the aperture's own row is 168 mm deep and the two jamb
       channels sit at 192, so they are outside it and every ray in the
       corridor can reach them; the pilasters either side still need the
       full depth, so at window height the row is three boxes, not one.
       168 mm rather than the 36 the pane needs, because a clip thinner
       than a frame's worth of movement is a clip you can run through. */
    const aw=V_AW/2;
    return [[-hw,0,0,               hw,0.112,        d],
            [-hw,0.160,0,           hw,V_AY0,        d],
            [-hw,V_AY0,0,          -aw,V_AY1,        d],
            [-aw,V_AY0,0,           aw,V_AY1,        0.168],
            [ aw,V_AY0,0,           hw,V_AY1,        d],
            [-hw,V_AY1,0,           hw,B_LINE-0.058, d],
            [-hw,B_LINE+0.058,0,    hw,h,            d]];
  },
  emit:()=>{
    const x=V_AW/2;
    return bayEmit(V_W).concat([
      // the two jamb channels.  Cold, and the only light in the bay: warm
      // below and cool above is the ship's rule, and a window is the one
      // place the cool is allowed to come from the side.
      [-x+0.030, V_AY0+0.072, 0.192, -x+0.072, V_AY1-0.072, 0.228, T.CHAN],
      [ x-0.072, V_AY0+0.072, 0.192,  x-0.030, V_AY1-0.072, 0.228, T.CHAN]]);
  },
  make:function(h){
  h=h||WALL_H;
  const P={mach:[],gap:[],skin:[],coat:[],duct:[]};
  /* the hole is the APERTURE, not the frame: the frame's own members lap
     30 mm over the field all round, so the seam has something on top of it
     and there is no sliver of chamber showing round the outside. */
  bayShell(h,P,V_W,[-V_AW/2+0.030, V_AY0+0.030, V_AW/2-0.030, V_AY1-0.030]);

  const FD=V_ZF-V_ZB;                  // 0.288 -- the frame's own thickness
  const FW=2*V_FX, FH=V_FY1-V_FY0;

  /* ---- the frame: four members, and they ARE the reveal ---------------
     The top and bottom members run the full width and the jambs fill
     between them, so the opening's four faces are the members' own sides
     and nothing extra has to be modelled to line it.  mPlate rolls a
     50 mm chamfer round each front face, so the arris the eye actually
     meets -- the front edge of the opening -- is eased, and the flat
     inside of the reveal is what stays dark. */
  P.mach.push(mMove(mPlate(FW,V_BORD,FD,0.050), 0, V_FY1-V_BORD/2, V_ZB));
  P.mach.push(mMove(mPlate(FW,V_BORD,FD,0.050), 0, V_FY0+V_BORD/2, V_ZB));
  for(const s of [-1,1])
    P.mach.push(mMove(mPlate(V_BORD,V_AH,FD,0.050),
                      s*(V_AW/2+V_BORD/2), (V_AY0+V_AY1)/2, V_ZB));

  /* ---- and four gussets, so no right angle is left open ---------------
     A square hole in a ship is the one thing every reference agrees is
     wrong: the section is an octagon and so is every opening in it.  A
     plate 170 mm square rolled 45 degrees sits half inside the border and
     half across the corner, and its inner face is the chamfer -- one
     piece per corner instead of a reveal plate and a filler. */
  for(const sx of [-1,1]) for(const sy of [-1,1])
    P.mach.push(mMove(mRoll(mPlate(V_CH*1.4142136,V_CH*1.4142136,FD,0.014),
                            0.7853982),
                      sx*V_AW/2, sy<0?V_AY0:V_AY1, V_ZB));

  /* ---- the mounting flange -------------------------------------------
     288 mm of frame standing on a 12 mm field is a box glued to a wall.
     The head and the two jambs step back through a 144 mm flange first,
     which is how a pressure frame is actually bolted down -- and the sill
     does something else entirely, below. */
  P.mach.push(mMove(mPlate(FW+2*0.084,0.084,V_ZFL-V_ZB,0.024),
                    0, V_FY1+0.042, V_ZB));
  for(const s of [-1,1])
    P.mach.push(mMove(mPlate(0.084,FH+0.084,V_ZFL-V_ZB,0.024),
                      s*(V_FX+0.042), (V_FY0+V_FY1)/2+0.042, V_ZB));

  /* ---- the corbel ------------------------------------------------------
     Under the sill the frame does return all the way to the wall, over a
     full 45 degrees and 288 mm of it, because that is the one edge of the
     block you stand under and look up at.  It is the same angle as the
     deck splay, the cove and the pilaster's foot: one rule holding the
     section together rather than four separate details. */
  P.mach.push(mMove(mSplayB(FW,V_FY0-V_RET,V_ZB,V_RET,0.034,0.026), 0,0,0));

  /* ---- the sill --------------------------------------------------------
     Laid ON the bottom member and running 72 mm past the frame's face, so
     it has a nose to catch the overhead lamp and a groove under it to go
     black.  A window you can put your hands on the sill of is a window;
     one flush with the wall is a picture. */
  P.mach.push(mMove(mSlab(V_AW+0.096,V_ZF+0.084,0.036,0.012),
                    0, V_AY0, (V_ZF+0.084)/2-0.012));
  P.gap .push(mMove(mSoffit(V_AW+0.060,0.072,0.024,0.006),
                    0, V_AY0, V_ZF+0.036));

  /* ---- the glass -------------------------------------------------------
     One light, and it is a FINISH, not a picture: world-projected like the
     steel around it, so the dirt on the pane has the ship's texel density
     and the same haze runs across the frame's shadow without a seam.  The
     chamfer is 8 mm and no more -- 48 mm of glass has a cut edge, and a
     rolled one would read as acrylic. */
  const pane=mMove(mPlate(V_AW-0.024,V_AH-0.024,V_ZGT,0.008),
                   0, (V_AY0+V_AY1)/2, V_ZG);

  /* the gasket the glass sits in.  Rubber, so it is the one dark, matte,
     non-metallic line in a frame that is otherwise all cut steel -- and it
     is what stops the pane reading as a panel painted black. */
  for(const t of [-1,1]){
    P.duct.push(mMove(mPlate(V_AW,0.048,0.096,0.010),
                      0, t<0?V_AY0+0.024:V_AY1-0.024, V_ZG));
    P.duct.push(mMove(mPlate(0.048,V_AH-0.096,0.096,0.010),
                      t*(V_AW/2-0.024), (V_AY0+V_AY1)/2, V_ZG));
  }

  /* ---- and, outside the hull, the two things there are to look at ------
     Both hang at NEGATIVE z, which is inside the brush the bay is stamped
     on -- so neither one exists unless the level cuts the wall open here.
     That is deliberate: a part cannot cut a brush, and a window that drew
     its own sky whether or not there was a hole would be a decal again.

     The sprite first.  4.5U square at 4.8 m, unwrapped in its own square
     (proj 3) so the planet, the gas and the forty near stars land exactly
     once and never repeat -- and offset off the bay's centreline, because
     a planet centred in a window is a logo. */
  const sprite=mMove(mPlate(V_SPW,V_SPW,0.048,0.010),
                     0.12, (V_AY0+V_AY1)/2-0.18, -V_SPZ);

  /* and the chamber: five plates, world-projected, tiling on the module.
     A box and not a dome, for one reason -- proj 2 gives every face its own
     arc-length uv, so a star on the side wall is the same size as a star on
     the back wall and there is no stretch anywhere to give the box away.
     Unlit and unfogged, so the seams where the faces meet carry no shading
     difference at all and simply cannot be seen. */
  const voidG=[];
  {
    const hw=V_VW/2, y0=(V_AY0+V_AY1)/2-V_VH/2, y1=y0+V_VH;
    voidG.push(mMove(mPlate(V_VW,V_VH,0.12,0.02),
                     0, (y0+y1)/2, -V_VD));                       // the back
    for(const s of [-1,1])
      voidG.push(mMove(mSpin(mPlate(V_VD,V_VH,0.12,0.02), s*1.5707963),
                       s*hw, (y0+y1)/2, -V_VD/2));                // the sides
    voidG.push(mMove(mSlab(V_VW,V_VD,0.12,0.02), 0, y0, -V_VD/2));
    voidG.push(mMove(mSoffit(V_VW,V_VD,0.12,0.02), 0, y1, -V_VD/2));
  }

  /* ---- the jamb channels ----------------------------------------------
     A black recess in each jamb with two machined lips, and the diffuser
     at the bottom of it stamped as a real brush (see `emit`) so the bake
     lights the reveal, the mullions and the sill off it.  Vertical, and
     deliberately: a cold line down each side of a window is the single
     most legible thing you can put in a dark corridor, because it stays
     lit at exactly the angles the frame is hiding the sky at. */
  for(const s of [-1,1]){
    const put = s<0 ? (m,d)=>mOnX(m,d)
                    : (m,d)=>mMove(mSpin(m,1.5707963), d,0,0);
    const y=(V_AY0+V_AY1)/2, hh=V_AH-0.144;
    P.gap .push(mMove(put(mPlate(0.132,hh,0.024,0.005), s*V_AW/2), 0,y,0.210));
    for(const t of [-1,1])
      P.mach.push(mMove(put(mPlate(0.036,hh,0.084,0.010), s*V_AW/2),
                        0, y, 0.210+t*0.084));
  }

  /* ---- the blast shutter, parked ---------------------------------------
     A slot across the head of the reveal and 60 mm of the shutter's bottom
     rail hanging out of it.  Nothing moves and nothing has to: what the
     detail buys is the answer to the question the window asks, which is
     "what happens when something hits this".  It also gives the head of
     the reveal a dark line, which is where the eye wants one. */
  P.gap .push(mMove(mSoffit(V_AW-0.048,0.144,0.030,0.006),
                    0, V_AY1, 0.216));
  P.mach.push(mMove(mSoffit(V_AW-0.084,0.096,0.060,0.014),
                    0, V_AY1-0.030, 0.216));

  /* ---- the bolts -------------------------------------------------------
     A pressure frame is held by its flange and the flange is held by
     bolts, so they go where the load is: a row along the head and the
     sill member, three down each jamb, and none anywhere decorative. */
  for(const x of [-1.350,-0.900,-0.450,0,0.450,0.900,1.350])
    for(const y of [V_FY0+V_BORD/2, V_FY1-V_BORD/2])
      P.mach.push(mMove(mTilt(mBolt(0.022,0.012),1.5707963), x, y, V_ZF));
  for(const s of [-1,1]) for(const y of [0.960,1.368,1.776])
    P.mach.push(mMove(mTilt(mBolt(0.022,0.012),1.5707963),
                      s*(V_AW/2+V_BORD/2), y, V_ZF));

  // the frame's plate, and the bay's one picture -- pressure rating, hull
  // frame number, the things that are stencilled on a real one
  const mark=mMove(mPlate(U_4,0.120,0.018,0.005), -0.960, V_FY0+V_BORD/2, V_ZF);

  return [kGrpAll(P.skin,T.SKIN,2,32), kGrpAll(P.coat,T.COAT,2,34),
          kGrpAll(P.mach,T.MACH,2,36), kGrpAll(P.gap,T.GAP,2,40),
          kGrpAll(P.duct,T.DUCT,2,34),
          kGrpAll(voidG,T.SKY,2,90), kGrp(sprite,T.ORB,3,90),
          kGrp(pane,T.GLAS,2,34), kGrp(mark,T.MARK,3)];
}});

/* ====================================================================
   K20  SKYLIGHT 1  --  CEILING OCULUS, 6U OCTAGON

   WALL 5 turned up: the same transparent-pass trick -- glass, then a
   sprite, then a tiling starfield chamber -- but punched through a
   ceiling instead of a wall, because a ROOM asks a different question
   of it than a corridor does.  A corridor's viewport is judged walking
   past it; this one is judged standing under it, and what a room needs
   from its one hole in the roof is scale -- big enough that the sky
   reads as OVER the room, not framed IN it.

   IT IS STILL A HOLE.  The level has to leave the ceiling brush open
   where this lands, the same bargain WALL 5 makes with the wall it
   stamps onto -- see the caller.

   And it still cannot be one pane.  WALL 5 had no mullions because its
   window was small enough that 48 mm glass reads as glass without help;
   at 7.2 m across a bare sheet would either look paper-thin or have to
   thicken until it stopped reading as glass at all.  So it carries two,
   crossed, sitting proud on the space-side face where they read in
   silhouette against the chamber instead of getting in the room's way.
   ==================================================================== */
const SK_W    = 6*U;         // 7.200 -- flat-to-flat, the opening's width
const SK_HW   = SK_W/2;
const SK_CH   = 0.420;       // the corner cut -- the octagon, at this scale
const SK_ZF   = 0.420;       // the curb: how far it stands proud of the ceiling
const SK_ZG   = 0.100;       // the glass, this far up inside the curb
const SK_ZGT  = 0.060;       // glass thickness -- a hull plate this size runs thicker
const SK_MARG = 1.15;        // the chamber's footprint, beyond the curb's own --
                              // grazing coverage, the same margin WALL 5 keeps
const SK_CHW  = SK_HW*SK_MARG;
const SK_VH   = 6.000;       // the chamber's rise above the curb -- 5U of it
/* THE SPRITE IS SMALL ON PURPOSE.  WALL 5 never had to answer for a
   viewer standing directly under its sprite and looking straight at it --
   a corridor window is always seen at a walking angle.  A ceiling oculus
   is seen dead-on from right below, so the same ORB material read as a
   coin-sized planet through a small aperture reads as a blown-out sun
   filling the whole opening here unless it is both smaller and further
   away: near the chamber's own cap, not its middle. */
const SK_SPY  = SK_VH*0.86;
const SK_SPW  = SK_W*0.30;

kitDef({ name:'SKYLIGHT 1', sub:'ceiling oculus · 6U octagon', proj:2,
  /* THE BAKE CANNOT SEE THE CHAMBER -- it ray-casts the CLIP set, same as
     everywhere else in the kit -- the bake only sees brushes -- and a part
     with no clip at all leaves a hole with nothing behind it for a ray to
     find.  So: the curb ring (really is solid, so clipping it costs
     nothing) and one thin cap at the chamber's far end, standing in for
     the mesh the bake will never trace, keeping the AO and bounce light
     on the surrounding ceiling honest right up to the opening's edge. */
  clip:()=>{
    const hw=SK_HW, b=0.30, cf=SK_CHW;
    return [
      [-hw,0,hw-b,       hw,SK_ZF,hw],
      [-hw,0,-hw,        hw,SK_ZF,-hw+b],
      [-hw,0,-hw+b,      -hw+b,SK_ZF,hw-b],
      [hw-b,0,-hw+b,     hw,SK_ZF,hw-b],
      [-cf,SK_ZF+SK_VH-0.05,-cf,  cf,SK_ZF+SK_VH,cf],
    ];
  },
  make:function(){
  const P={mach:[]};
  /* ---- the curb: four walls standing off the ceiling, each facing the
     opening it stands round -- the exact technique WALL 5's frame uses,
     turned from a vertical wall's XY plane to the ceiling's horizontal
     XZ one.  Plain mPlate already faces +Z, which is what the z=-SK_HW
     wall needs; the other three are mSpin turns of the same plate. */
  P.mach.push(mMove(mPlate(2*SK_HW,SK_ZF,0.020,0.030), 0,SK_ZF/2,-SK_HW));
  P.mach.push(mMove(mSpin(mPlate(2*SK_HW,SK_ZF,0.020,0.030),3.1415927),
                    0,SK_ZF/2,SK_HW));
  P.mach.push(mMove(mSpin(mPlate(2*SK_HW,SK_ZF,0.020,0.030),-1.5707963),
                    -SK_HW,SK_ZF/2,0));
  P.mach.push(mMove(mSpin(mPlate(2*SK_HW,SK_ZF,0.020,0.030),1.5707963),
                    SK_HW,SK_ZF/2,0));
  // four corner gussets, 45 degrees across the corner cut -- NO RIGHT
  // ANGLE IS LEFT OPEN, the same rule WALL 5's own gussets keep
  for(const sx of [-1,1]) for(const sz of [-1,1])
    P.mach.push(mMove(mSpin(mPlate(SK_CH*1.4142136,SK_ZF,0.020,0.030),
                            sx*sz>0?0.7853982:-0.7853982),
                      sx*SK_HW, SK_ZF/2, sz*SK_HW));
  // the cap, where the curb meets the flat ceiling -- a flange so the
  // seam has something standing proud of it, the same reason WALL 5's
  // frame laps 30 mm over the field all round
  P.mach.push(mMove(mSoffit(2*SK_HW+0.16,2*SK_HW+0.16,0.026,0.012),
                    0,0.001,0));

  /* ---- the glass -- one pane, world-projected like the steel around
     it, chamfered 10 mm the way a thicker hull plate wants */
  const pane=mMove(mSoffit(2*SK_HW-0.10,2*SK_HW-0.10,SK_ZGT,0.010),
                   0,SK_ZG,0);

  // two mullions, crossed, sitting proud on the SPACE side -- silhouette
  // against the chamber rather than clutter in the room below
  const mullY=SK_ZG+SK_ZGT+0.020;
  const mull=[
    mMove(mSoffit(2*SK_HW-0.30,0.150,0.090,0.020), 0,mullY,0),
    mMove(mSoffit(0.150,2*SK_HW-0.30,0.090,0.020), 0,mullY,0),
  ];

  /* ---- the chamber: five plates above the curb, world-projected and
     tiling on the module, exactly WALL 5's void box turned from "away
     from the wall" (its own Z) to "up, away from the room" (this one's
     Y).  Four side walls instead of WALL 5's two, because the open face
     here is Y, not Z, and both of the other axes need a wall each. */
  const voidG=[];
  {
    const hx=SK_CHW, hz=SK_CHW, y0=SK_ZF, y1=SK_ZF+SK_VH;
    voidG.push(mMove(mSoffit(2*hx,2*hz,0.12,0.02), 0,y1,0));           // the cap
    voidG.push(mMove(mPlate(2*hx,SK_VH,0.12,0.02), 0,(y0+y1)/2,-hz));  // z=-hz
    voidG.push(mMove(mSpin(mPlate(2*hx,SK_VH,0.12,0.02),3.1415927),
                     0,(y0+y1)/2,hz));                                  // z=+hz
    voidG.push(mMove(mSpin(mPlate(2*hz,SK_VH,0.12,0.02),-1.5707963),
                     -hx,(y0+y1)/2,0));                                 // x=-hx
    voidG.push(mMove(mSpin(mPlate(2*hz,SK_VH,0.12,0.02),1.5707963),
                     hx,(y0+y1)/2,0));                                  // x=+hx
  }
  // the nebula, one sprite facing down at the viewer below -- proj 3 so
  // it lands exactly once, the same reason WALL 5's planet does
  const sprite=mMove(mSoffit(SK_SPW,SK_SPW,0.048,0.010), 0.30,SK_ZF+SK_SPY,-0.20);

  return [kGrpAll(P.mach,T.MACH,2,36),
          kGrpAll(voidG,T.SKY,2,90), kGrp(sprite,T.ORB,3,90),
          kGrp(pane,T.GLAS,2,34), kGrpAll(mull,T.MACH,2,34)];
}});

function buildShipModules(){
  if(typeof MODULES==='undefined') return;
  for(const M of MODULES){
    const k=M[0], x=M[1], y=M[2], z=M[3], yaw=M[4], a=M[5], mat=M[6];
    const D=KIT[k];
    const co=Math.cos(yaw), si=Math.sin(yaw);
    const box=c=>[c[0]*co+c[2]*si+x, c[1]+y, -c[0]*si+c[2]*co+z,
                  c[3]*co+c[5]*si+x, c[4]+y, -c[3]*si+c[5]*co+z];
    for(const g of kitGroups(k,a)){
      if(!g.m.F.length) continue;
      wPlace(g.m, g.t<0?mat:g.t, x,y,z,yaw, g.p, a, g.b, g.cr);
    }
    if(D.clip) for(const c of D.clip(a)){
      const b=box(c);
      brush(b[0],b[1],b[2], b[3],b[4],b[5], mat, 1);  // clip: stops you, undrawn
    }
    // ---- emitters -----------------------------------------------------
    // A part's triangles do not exist as far as the radiosity bake is
    // concerned: it gathers by ray-casting the BRUSH set and reads the
    // material off whatever it hits.  So a lit channel modelled as part
    // geometry would glow in the shader and light absolutely nothing.
    // What a part declares in `emit` is stamped as a real, drawn brush
    // lying in the modelled recess -- one object, seen and felt by both
    // the renderer and the bake.  It also has to clear the part's own clip
    // boxes, or the ray hits the clip first and finds no light at all.
    if(D.emit) for(const c of D.emit(a)){
      const b=box(c);
      brush(b[0],b[1],b[2], b[3],b[4],b[5], c[6], 0);
    }
  }
}
