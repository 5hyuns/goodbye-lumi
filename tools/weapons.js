/* ============================================================
   PROCEDURAL WEAPONS  --  ported from the standalone weapon bench
   the three guns were first built on.

   Spliced INTO the engine's main IIFE by tools/weapons.py (after
   tools/splice.py, before tools/deckmap.py).  Everything lives in
   one inner IIFE (const VMW); the only other top-level names are
   the three re-declared engine functions at the bottom --
   drawViewmodel / tryFire / updateWeapons -- which shadow the
   originals (duplicate function declarations: the last one wins).
   The original drawViewmodel is renamed drawViewmodelBoxes by
   weapons.py and still serves the crowbar and the grenade.

   Meshes: loft/lathe/chamferBox, 50-degree normal smoothing,
   13-float interleave (pos/nrm/uv/tan4/mat).  Textures: 3 sets of
   512^2 albedo/normal/RMA generated on the GPU in one MRT pass.
   Weapons: pistol (slide, kind 2) / rotary cannon (spin-up + heat,
   kind 0) / double-barrel (break action, kind 1), mapped onto the
   engine's glock / smg / shotgun slots so pickups, ammo stores and
   the HUD keep working.
   ============================================================ */
const VMW=(function(){

/* ---------------------------------------------------------------- math */
function mI(){return new Float32Array([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);}
function mMul2(a,b){const o=new Float32Array(16);
  for(let c=0;c<4;c++)for(let r=0;r<4;r++){let s=0;
    for(let k=0;k<4;k++)s+=a[k*4+r]*b[c*4+k];o[c*4+r]=s;}return o;}
function mT(x,y,z){const m=mI();m[12]=x;m[13]=y;m[14]=z;return m;}
function mRX(a){const c=Math.cos(a),s=Math.sin(a),m=mI();m[5]=c;m[6]=s;m[9]=-s;m[10]=c;return m;}
function mRY(a){const c=Math.cos(a),s=Math.sin(a),m=mI();m[0]=c;m[2]=-s;m[8]=s;m[10]=c;return m;}
function mRZ(a){const c=Math.cos(a),s=Math.sin(a),m=mI();m[0]=c;m[1]=s;m[4]=-s;m[5]=c;return m;}
function mS(x,y,z){const m=mI();m[0]=x;m[5]=y;m[10]=z;return m;}
function mPersp(f,a,n,fa){const t=1/Math.tan(f/2);return new Float32Array(
  [t/a,0,0,0, 0,t,0,0, 0,0,(fa+n)/(n-fa),-1, 0,0,2*fa*n/(n-fa),0]);}
function m3of(m){return new Float32Array([m[0],m[1],m[2],m[4],m[5],m[6],m[8],m[9],m[10]]);}
function xfp(m,p){const x=p[0],y=p[1],z=p[2];
  return [m[0]*x+m[4]*y+m[8]*z+m[12], m[1]*x+m[5]*y+m[9]*z+m[13], m[2]*x+m[6]*y+m[10]*z+m[14]];}
function rot3(M,v){return [M[0]*v[0]+M[4]*v[1]+M[8]*v[2],
                           M[1]*v[0]+M[5]*v[1]+M[9]*v[2],
                           M[2]*v[0]+M[6]*v[1]+M[10]*v[2]];}
function mulberry32(a){return function(){a|=0;a=a+0x6D2B79F5|0;
  let t=Math.imul(a^a>>>15,1|a);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};}
function rnd(a){return (Math.random()-0.5)*2*a;}
function lin(hex){const n=parseInt(hex.slice(1),16);
  return [Math.pow(((n>>16)&255)/255,2.2),Math.pow(((n>>8)&255)/255,2.2),Math.pow((n&255)/255,2.2)];}

/* ------------------------------------------- mesh builder (face soup) */
let CM=mI(); const XSTACK=[];
function push(m){XSTACK.push(CM);CM=mMul2(CM,m);}
function pop(){CM=XSTACK.pop();}
function newB(){return {v:[],t:[],m:[]};}
function tri(B,a,b,c,ta,tb,tc,mat){
  const A=xfp(CM,a),Bp=xfp(CM,b),C=xfp(CM,c);
  B.v.push(A[0],A[1],A[2],Bp[0],Bp[1],Bp[2],C[0],C[1],C[2]);
  B.t.push(ta[0],ta[1],tb[0],tb[1],tc[0],tc[1]);
  B.m.push(mat);
}
function quad(B,a,b,c,d,ta,tb,tc,td,mat,flip){
  if(flip){tri(B,a,d,c,ta,td,tc,mat);tri(B,a,c,b,ta,tc,tb,mat);}
  else {tri(B,a,b,c,ta,tb,tc,mat);tri(B,a,c,d,ta,tc,td,mat);}
}
function loft(B,rings,mat,ts,opt){
  opt=opt||{};
  const n=rings[0].length, R=rings.length, flip=!!opt.flip;
  for(let k=1;k<R;k++) if(rings[k].length!==n)
    throw new Error("loft: ring point count mismatch ("+n+" vs "+rings[k].length+")");
  const us=[0];
  for(let i=1;i<=n;i++){const a=rings[0][(i-1)%n],b=rings[0][i%n];
    us.push(us[i-1]+Math.hypot(b[0]-a[0],b[1]-a[1],b[2]-a[2]));}
  const vs=[0];
  for(let k=1;k<R;k++){let s=0;
    for(let i=0;i<n;i++){const a=rings[k-1][i],b=rings[k][i];
      s+=Math.hypot(b[0]-a[0],b[1]-a[1],b[2]-a[2]);}
    vs.push(vs[k-1]+Math.max(s/n,1e-4));}
  for(let k=0;k<R-1;k++)for(let i=0;i<n;i++){
    const j=(i+1)%n;
    quad(B,rings[k][i],rings[k][j],rings[k+1][j],rings[k+1][i],
      [us[i]*ts,vs[k]*ts],[us[i+1]*ts,vs[k]*ts],
      [us[i+1]*ts,vs[k+1]*ts],[us[i]*ts,vs[k+1]*ts],mat,flip);
  }
  if(opt.capStart)cap(B,rings[0],mat,ts,!flip);
  if(opt.capEnd)cap(B,rings[R-1],mat,ts,flip);
}
function cap(B,ring,mat,ts,flip){
  const n=ring.length;let cx=0,cy=0,cz=0;
  for(const p of ring){cx+=p[0];cy+=p[1];cz+=p[2];}
  cx/=n;cy/=n;cz/=n;
  const C=[cx,cy,cz];
  for(let i=0;i<n;i++){const a=ring[i],b=ring[(i+1)%n];
    const ua=[a[0]*ts,a[1]*ts],ub=[b[0]*ts,b[1]*ts],uc=[cx*ts,cy*ts];
    if(flip)tri(B,C,b,a,uc,ub,ua,mat); else tri(B,C,a,b,uc,ua,ub,mat);}
}
function resample(ring,n){
  const m=ring.length, cum=[0];
  for(let i=1;i<=m;i++){const a=ring[(i-1)%m],b=ring[i%m];
    cum.push(cum[i-1]+Math.hypot(b[0]-a[0],b[1]-a[1],b[2]-a[2]));}
  const total=cum[m], out=[];
  let seg=0;
  for(let i=0;i<n;i++){
    const d=total*i/n;
    while(seg<m-1&&cum[seg+1]<d)seg++;
    const t=(d-cum[seg])/Math.max(cum[seg+1]-cum[seg],1e-9);
    const a=ring[seg%m],b=ring[(seg+1)%m];
    out.push([a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t]);
  }
  return out;
}
function ringCircle(cx,cy,z,r,seg){const o=[];
  for(let i=0;i<seg;i++){const a=i/seg*Math.PI*2;o.push([cx+Math.cos(a)*r,cy+Math.sin(a)*r,z]);}
  return o;}
function ringTri(cx,cy,z,R,c){
  const p=[],V=[];
  for(let i=0;i<3;i++){const a=Math.PI/2+i*2*Math.PI/3;
    V.push([cx+Math.cos(a)*R,cy+Math.sin(a)*R]);}
  for(let i=0;i<3;i++){
    const v=V[i],pv=V[(i+2)%3],nv=V[(i+1)%3];
    const l1=Math.hypot(pv[0]-v[0],pv[1]-v[1]),l2=Math.hypot(nv[0]-v[0],nv[1]-v[1]);
    const k1=Math.min(c,l1*0.45)/l1,k2=Math.min(c,l2*0.45)/l2;
    p.push([v[0]+(pv[0]-v[0])*k1,v[1]+(pv[1]-v[1])*k1,z]);
    p.push([v[0]+(nv[0]-v[0])*k2,v[1]+(nv[1]-v[1])*k2,z]);
  }
  return p;
}
function ringRect(cx,cy,z,sx,sy,c){
  const hx=Math.max(sx/2,1e-4),hy=Math.max(sy/2,1e-4);
  c=Math.min(c,hx*0.9,hy*0.9);
  return [[cx-hx+c,cy-hy,z],[cx+hx-c,cy-hy,z],[cx+hx,cy-hy+c,z],[cx+hx,cy+hy-c,z],
          [cx+hx-c,cy+hy,z],[cx-hx+c,cy+hy,z],[cx-hx,cy+hy-c,z],[cx-hx,cy-hy+c,z]];}
function chamferBox(B,cx,cy,cz,sx,sy,sz,c,mat,ts){
  const hz=sz/2;
  loft(B,[ringRect(cx,cy,cz-hz,sx-2*c,sy-2*c,c*0.6),
          ringRect(cx,cy,cz-hz+c,sx,sy,c),
          ringRect(cx,cy,cz+hz-c,sx,sy,c),
          ringRect(cx,cy,cz+hz,sx-2*c,sy-2*c,c*0.6)],mat,ts,{capStart:1,capEnd:1});
}
function lathe(B,cx,cy,profile,seg,mat,ts){
  for(let i=0;i<profile.length-1;i++){
    const a=profile[i],b=profile[i+1];
    const m=(a[2]!==undefined)?a[2]:mat;
    loft(B,[ringCircle(cx,cy,a[1],a[0],seg),ringCircle(cx,cy,b[1],b[0],seg)],m,ts);
  }
}
/* face normals/tangents + 50-degree smoothing groups */
function finalize(B){
  const nt=B.m.length;
  const fN=new Float32Array(nt*3), fT=new Float32Array(nt*3), fBt=new Float32Array(nt*3);
  for(let f=0;f<nt;f++){
    const o=f*9,u=f*6;
    const p0=[B.v[o],B.v[o+1],B.v[o+2]],p1=[B.v[o+3],B.v[o+4],B.v[o+5]],p2=[B.v[o+6],B.v[o+7],B.v[o+8]];
    const e1=[p1[0]-p0[0],p1[1]-p0[1],p1[2]-p0[2]],e2=[p2[0]-p0[0],p2[1]-p0[1],p2[2]-p0[2]];
    let nx=e1[1]*e2[2]-e1[2]*e2[1],ny=e1[2]*e2[0]-e1[0]*e2[2],nz=e1[0]*e2[1]-e1[1]*e2[0];
    const l=Math.hypot(nx,ny,nz)||1;fN[f*3]=nx/l;fN[f*3+1]=ny/l;fN[f*3+2]=nz/l;
    const d1=[B.t[u+2]-B.t[u],B.t[u+3]-B.t[u+1]],d2=[B.t[u+4]-B.t[u],B.t[u+5]-B.t[u+1]];
    let r=d1[0]*d2[1]-d2[0]*d1[1];
    let tx,ty,tz,bx,by,bz;
    if(Math.abs(r)<1e-12){
      const ax=Math.abs(fN[f*3])<0.9?[1,0,0]:[0,1,0];
      tx=ax[1]*fN[f*3+2]-ax[2]*fN[f*3+1];ty=ax[2]*fN[f*3]-ax[0]*fN[f*3+2];tz=ax[0]*fN[f*3+1]-ax[1]*fN[f*3];
      bx=fN[f*3+1]*tz-fN[f*3+2]*ty;by=fN[f*3+2]*tx-fN[f*3]*tz;bz=fN[f*3]*ty-fN[f*3+1]*tx;
    }else{
      r=1/r;
      tx=(e1[0]*d2[1]-e2[0]*d1[1])*r;ty=(e1[1]*d2[1]-e2[1]*d1[1])*r;tz=(e1[2]*d2[1]-e2[2]*d1[1])*r;
      bx=(e2[0]*d1[0]-e1[0]*d2[0])*r;by=(e2[1]*d1[0]-e1[1]*d2[0])*r;bz=(e2[2]*d1[0]-e1[2]*d2[0])*r;
    }
    const tl=Math.hypot(tx,ty,tz)||1;fT[f*3]=tx/tl;fT[f*3+1]=ty/tl;fT[f*3+2]=tz/tl;
    const bl=Math.hypot(bx,by,bz)||1;fBt[f*3]=bx/bl;fBt[f*3+1]=by/bl;fBt[f*3+2]=bz/bl;
  }
  const map=new Map();
  for(let f=0;f<nt;f++)for(let k=0;k<3;k++){
    const o=f*9+k*3;
    const key=(Math.round(B.v[o]*4096))+"|"+(Math.round(B.v[o+1]*4096))+"|"+(Math.round(B.v[o+2]*4096));
    let a=map.get(key); if(!a){a=[];map.set(key,a);} a.push(f);
  }
  const COS=0.64; // 50 degrees
  const out=new Float32Array(nt*3*13);
  for(let f=0;f<nt;f++)for(let k=0;k<3;k++){
    const o=f*9+k*3,u=f*6+k*2;
    const key=(Math.round(B.v[o]*4096))+"|"+(Math.round(B.v[o+1]*4096))+"|"+(Math.round(B.v[o+2]*4096));
    const grp=map.get(key);
    let nx=0,ny=0,nz=0,tx=0,ty=0,tz=0;
    for(const g of grp){
      const d=fN[g*3]*fN[f*3]+fN[g*3+1]*fN[f*3+1]+fN[g*3+2]*fN[f*3+2];
      if(d>COS){nx+=fN[g*3];ny+=fN[g*3+1];nz+=fN[g*3+2];tx+=fT[g*3];ty+=fT[g*3+1];tz+=fT[g*3+2];}
    }
    let l=Math.hypot(nx,ny,nz)||1;nx/=l;ny/=l;nz/=l;
    const dt=tx*nx+ty*ny+tz*nz;tx-=nx*dt;ty-=ny*dt;tz-=nz*dt;
    l=Math.hypot(tx,ty,tz);
    if(l<1e-6){const ax=Math.abs(nx)<0.9?[1,0,0]:[0,1,0];
      tx=ax[1]*nz-ax[2]*ny;ty=ax[2]*nx-ax[0]*nz;tz=ax[0]*ny-ax[1]*nx;l=Math.hypot(tx,ty,tz)||1;}
    tx/=l;ty/=l;tz/=l;
    const ccx=ny*tz-nz*ty,ccy=nz*tx-nx*tz,ccz=nx*ty-ny*tx;
    const w=(ccx*fBt[f*3]+ccy*fBt[f*3+1]+ccz*fBt[f*3+2])<0?-1:1;
    const q=(f*3+k)*13;
    out[q]=B.v[o];out[q+1]=B.v[o+1];out[q+2]=B.v[o+2];
    out[q+3]=nx;out[q+4]=ny;out[q+5]=nz;
    out[q+6]=B.t[u];out[q+7]=B.t[u+1];
    out[q+8]=tx;out[q+9]=ty;out[q+10]=tz;out[q+11]=w;
    out[q+12]=B.m[f];
  }
  return out;
}

/* --------------------------------------------------- gun assemblies */
const TSG=3.2, TSB=7.2, SEG=20;

function barrelProfile(r,z0,z1,rng){
  const p=[];
  p.push([0,z0+0.055,5]);
  p.push([r*0.44,z0+0.055,5]);
  p.push([r*0.44,z0,5]);
  p.push([r*1.34,z0,2]);
  p.push([r*1.42,z0+0.007,2]);
  p.push([r*1.42,z0+0.030,2]);
  p.push([r*1.34,z0+0.037,1]);
  p.push([r*1.06,z0+0.037,1]);
  p.push([r*1.06,z0+0.052,1]);
  p.push([r,z0+0.060,1]);
  let z=z0+0.060;
  const n=5,seg=(z1-0.055-z)/n;
  for(let i=0;i<n;i++){
    const g=0.55+rng()*0.25;
    p.push([r,z+seg*g,1]);
    p.push([r*1.18,z+seg*(g+0.06),1]);
    p.push([r*1.18,z+seg*(g+0.22),1]);
    p.push([r,z+seg*(g+0.28),1]);
    z+=seg;
  }
  p.push([r,z1-0.055,1]);
  p.push([r*1.30,z1-0.048,1]);
  p.push([r*1.30,z1,1]);
  p.push([0,z1,1]);
  return p;
}
function handAt(TG,x,y,z,rot,scale){
  push(mMul2(mMul2(mT(x,y,z),mRZ(rot)),mRX(0.15)));
  chamferBox(TG,0,-0.055*scale,0.02,0.085*scale,0.105*scale,0.115*scale,0.014,6,TSG);
  chamferBox(TG,0,-0.130*scale,0.05,0.078*scale,0.075*scale,0.150*scale,0.012,6,TSG);
  for(let i=0;i<4;i++)
    chamferBox(TG,0,0.020*scale,-0.030+i*0.028,0.090*scale,0.026*scale,0.024,0.006,6,TSG*1.6);
  chamferBox(TG,-0.046*scale,-0.006*scale,-0.010,0.020,0.030,0.030,0.006,6,TSG*1.6);
  pop();
}

/* rotary cannon */
function buildRotary(seed){
  const rng=mulberry32(seed);
  const S=newB(), P=newB(), Hd=newB();
  const BR=0.023, OFF=0.036, Z0=-0.68, Z1=-0.055;
  const RBMAX=BR*1.18, CLR=0.004;
  const CCI=0.036, CCO=0.051;
  const CRI=Math.max(OFF+2.0*(RBMAX+CLR), OFF+RBMAX+CLR+0.866*CCI);
  const CRO=CRI+0.020;
  for(let i=0;i<3;i++){
    const a=Math.PI/2+i*2*Math.PI/3;
    lathe(P,Math.cos(a)*OFF,Math.sin(a)*OFF,barrelProfile(BR,Z0,Z1,rng),16,1,TSG*1.6);
  }
  function clampTri(z,th,accent){
    const RI=CRI, RO=CRO, CI=CCI, CO=CCO, bev=Math.min(0.014,th*0.35);
    const z0=z-th/2, z1=z+th/2;
    const O=(zz,r)=>ringTri(0,0,zz,r,CO), I=(zz,r)=>ringTri(0,0,zz,r,CI);
    const outer=[O(z0,RO-bev),O(z0+bev,RO),O(z1-bev,RO),O(z1,RO-bev)];
    const inner=[I(z0,RI+bev),I(z0+bev,RI),I(z1-bev,RI),I(z1,RI+bev)];
    const M=accent?2:0;
    loft(P,outer,M,TSG);
    loft(P,inner,0,TSG,{flip:true});
    loft(P,[inner[0],outer[0]],M,TSG);
    loft(P,[outer[3],inner[3]],M,TSG);
  }
  clampTri(-0.545,0.040,false);
  clampTri(-0.300,0.036,true);
  lathe(P,0,0,[[0,-0.118],[0.052,-0.118],[0.072,-0.106],[0.072,-0.062],
               [0.058,-0.055],[0,-0.055]],SEG,0,TSG);
  for(let i=0;i<6;i++){
    const a=Math.PI/2+i/6*Math.PI*2;
    push(mMul2(mRZ(a),mT(0.047,0,-0.118)));
    chamferBox(P,0,0,0,0.016,0.024,0.014,0.004,rng()>0.65?2:0,TSG*2);
    pop();
  }
  loft(S,[ringCircle(0,0,-0.060,0.070,SEG),ringCircle(0,0,-0.060,0.078,SEG),
          ringCircle(0,0, 0.030,0.092,SEG),ringCircle(0,0, 0.030,0.082,SEG)],0,TSB);
  for(let i=0;i<7;i++){
    const a=-0.9+i*0.30;
    push(mMul2(mRZ(a),mT(0,0.085,-0.014)));
    chamferBox(S,0,0,0,0.030,0.012,0.052,0.003,0,TSG*2);
    pop();
  }
  loft(S,[ringCircle(0,0,0.030,0.092,SEG),
          resample(ringRect(0,0,0.052,0.176,0.156,0.020),SEG)],0,TSB);
  chamferBox(S,0,0,0.185,0.176,0.156,0.270,0.018,0,TSB);
  chamferBox(S,-0.090,0.005,0.190,0.010,0.116,0.200,0.004,0,TSB);
  chamferBox(S, 0.090,0.005,0.190,0.010,0.116,0.200,0.004,0,TSB);
  chamferBox(S,0,0.090,0.170,0.050,0.024,0.230,0.006,0,TSB);
  for(let i=0;i<6;i++)chamferBox(S,0,0.104,0.075+i*0.038,0.038,0.010,0.020,0.003,0,TSG*2);
  chamferBox(S,0,0,0.072,0.182,0.162,0.030,0.008,8,TSG);
  push(mT(-0.064,0.098,0.074));
  chamferBox(S,0,0,-0.013,0.050,0.034,0.022,0.005,0,TSB);
  chamferBox(S,0,-0.026,-0.018,0.024,0.020,0.014,0.004,0,TSG);
  quad(S,[-0.020,-0.012,0.0012],[0.020,-0.012,0.0012],
         [0.020, 0.012,0.0012],[-0.020, 0.012,0.0012],
         [0,0],[1,0],[1,1],[0,1],9);
  pop();
  chamferBox(S,-0.096,0.030,0.200,0.005,0.014,0.120,0.002,4,TSG);
  chamferBox(S, 0.096,0.030,0.200,0.005,0.014,0.120,0.002,4,TSG);
  for(let i=0;i<14;i++){
    const side=rng()<0.5?-1:1;
    const sx=0.006+rng()*0.008, sy=0.012+rng()*0.040, sz=0.020+rng()*0.060;
    chamferBox(S,side*0.096,-0.045+rng()*0.09,0.085+rng()*0.20,sx,sy,sz,0.0025,rng()<0.18?2:0,TSG*2);
  }
  for(let i=0;i<6;i++){
    chamferBox(S,-0.05+rng()*0.10,0.081,0.090+rng()*0.20,0.010+rng()*0.02,0.008,0.014+rng()*0.03,0.002,0,TSG*2);
  }
  chamferBox(S,0.128,-0.040,0.235,0.100,0.130,0.150,0.012,0,TSB);
  chamferBox(S,0.128,-0.040,0.162,0.084,0.110,0.014,0.005,8,TSG);
  for(let i=0;i<7;i++){
    const t=i/6;
    push(mMul2(mT(0.075+0.045*t,-0.088+0.024*Math.sin(t*3.1),0.150-0.055*t),mRZ(-0.35+0.5*t)));
    chamferBox(S,0,0,0,0.014,0.026,0.016,0.003,i%2?2:0,TSG*2);
    pop();
  }
  chamferBox(S,0,-0.072,-0.290,0.086,0.048,0.170,0.010,0,TSB);
  for(let i=0;i<5;i++)chamferBox(S,0,-0.098,-0.355+i*0.033,0.062,0.010,0.016,0.003,0,TSG*2);
  push(mMul2(mT(0,-0.140,0.240),mRX(0.22)));
  chamferBox(S,0,0,0,0.046,0.135,0.062,0.012,3,TSG*2);
  pop();
  chamferBox(S,0,-0.078,0.240,0.056,0.030,0.076,0.010,0,TSB);
  handAt(Hd,0.0,-0.150,0.245,0.0,1.0);
  handAt(Hd,0.0,-0.128,-0.300,0.0,0.92);
  return {stat:finalize(S),spin:finalize(P),hands:finalize(Hd),
          hmov:finalize(newB()),magm:finalize(newB())};
}

/* pistol */
function buildPistol(seed){
  const S=newB(), P=newB(), Hd=newB(), Mg=newB();
  const T=6.0;
  chamferBox(P,0,0.012,-0.045,0.046,0.042,0.250,0.007,14,T);
  chamferBox(P,0,0.036,-0.045,0.030,0.010,0.230,0.004,14,T);
  for(let i=0;i<5;i++)
    chamferBox(P,0.024,0.014,-0.130+i*0.019,0.006,0.024,0.010,0.002,14,T*1.6);
  for(let i=0;i<5;i++)
    chamferBox(P,-0.024,0.014,-0.130+i*0.019,0.006,0.024,0.010,0.002,14,T*1.6);
  chamferBox(P,0.024,0.006,0.030,0.006,0.020,0.070,0.002,5,T);
  chamferBox(P,0,0.040,-0.158,0.010,0.014,0.016,0.003,14,T*1.6);
  chamferBox(P,0,0.040,0.062,0.024,0.012,0.012,0.003,14,T*1.6);
  for(let i=0;i<4;i++){
    chamferBox(P, 0.024,0.012,0.036+i*0.010,0.005,0.030,0.005,0.0015,14,T*2);
    chamferBox(P,-0.024,0.012,0.036+i*0.010,0.005,0.030,0.005,0.0015,14,T*2);
  }
  lathe(P,0,0.012,[[0,-0.166,5],[0.0090,-0.166,5],[0.0090,-0.176,5],
                   [0.0150,-0.176,12],[0.0160,-0.170,12],[0.0160,-0.150,12],
                   [0,-0.150,12]],16,12,40);
  chamferBox(S,0,-0.026,-0.030,0.048,0.038,0.250,0.007,13,T);
  chamferBox(S,0,-0.040,-0.098,0.044,0.020,0.100,0.005,13,T);
  chamferBox(S,0,-0.088,-0.060,0.042,0.098,0.108,0.009,13,T);
  chamferBox(S,0.022,-0.086,-0.060,0.004,0.070,0.086,0.003,8,T);
  chamferBox(S,-0.022,-0.086,-0.060,0.004,0.070,0.086,0.003,8,T);
  chamferBox(S,0,-0.140,-0.060,0.046,0.014,0.104,0.005,13,T);
  chamferBox(S,0,-0.070,0.012,0.030,0.012,0.062,0.005,13,T);
  chamferBox(S,0,-0.052,0.006,0.010,0.030,0.010,0.003,14,T*1.6);
  push(mMul2(mT(0,-0.098,0.066),mRX(0.30)));
  chamferBox(S,0,0,0,0.040,0.118,0.052,0.010,13,T);
  for(let i=0;i<6;i++)
    chamferBox(S,0,-0.048+i*0.019,-0.028,0.034,0.008,0.006,0.002,3,T*2);
  pop();
  chamferBox(S,0,-0.020,0.098,0.044,0.048,0.014,0.006,13,T);
  {
    const Z=0.1062, x0=-0.0155, x1=0.0155, y0=-0.0325, y1=-0.0075;
    chamferBox(S,0,y0-0.004,Z-0.0016,0.040,0.005,0.005,0.0015,14,T*2);
    chamferBox(S,0,y1+0.004,Z-0.0016,0.040,0.005,0.005,0.0015,14,T*2);
    chamferBox(S,x0-0.004,-0.020,Z-0.0016,0.005,0.034,0.005,0.0015,14,T*2);
    chamferBox(S,x1+0.004,-0.020,Z-0.0016,0.005,0.034,0.005,0.0015,14,T*2);
    quad(S,[x0,y0,Z],[x1,y0,Z],[x1,y1,Z],[x0,y1,Z],[0,0],[1,0],[1,1],[0,1],9);
  }
  chamferBox(Mg,0,-0.106,-0.060,0.034,0.092,0.094,0.007,13,T);
  chamferBox(Mg,0,-0.156,-0.060,0.038,0.014,0.098,0.005,14,T);
  for(let i=0;i<4;i++)
    lathe(Mg,0,-0.075-i*0.021,[[0,-0.020],[0.0125,-0.014],[0.0135,0.014],[0,0.020]],
          12,10,26);
  handAt(Hd,0,-0.112,0.072,0,0.86);
  return {stat:finalize(S),spin:finalize(P),hands:finalize(Hd),
          hmov:finalize(newB()),magm:finalize(Mg)};
}

/* double-barrel, break action */
function buildShotgun(seed){
  const S=newB(), P=newB(), Hd=newB(), Hm=newB();
  const BX=0.032, Z0=-0.66, ZB=-0.030;
  for(const sx of [-BX,BX])
    lathe(P,sx,0,[[0,Z0+0.050,5],[0.0165,Z0+0.050,5],[0.0165,Z0,5],
                  [0.0252,Z0,12],[0.0266,Z0+0.009,12],[0.0266,Z0+0.026,12],
                  [0.0232,Z0+0.036,12],[0.0214,Z0+0.036,12],
                  [0.0214,-0.120,12],[0.0244,-0.112,12],[0.0244,ZB-0.020,12],
                  [0.0222,ZB,12],[0,ZB,12]],22,12,TSG*1.6);
  chamferBox(P,0,0,-0.350,0.020,0.032,0.560,0.005,0,TSB);
  chamferBox(P,0,0.026,Z0+0.048,0.014,0.014,0.022,0.004,0,TSG);
  chamferBox(P,0,0,ZB-0.030,0.104,0.062,0.062,0.011,0,TSB);
  for(const sx of [-BX,BX])
    lathe(P,sx,0,[[0,ZB-0.001,5],[0.0170,ZB-0.001,5],[0.0170,ZB-0.030,5]],14,5,40);
  chamferBox(P,0,-0.040,-0.300,0.098,0.058,0.205,0.015,0,TSB);
  for(let i=0;i<6;i++)
    chamferBox(P,0,-0.070,-0.378+i*0.031,0.088,0.010,0.017,0.004,0,TSG*2);
  chamferBox(P,0,-0.040,-0.201,0.104,0.064,0.014,0.005,0,TSG);
  chamferBox(P,0,-0.040,-0.399,0.104,0.064,0.012,0.005,0,TSG);
  const RG=[[-0.030,0.106,0.066,0.022,-0.002],
            [ 0.004,0.112,0.100,0.032,-0.004],
            [ 0.050,0.114,0.122,0.040,-0.006],
            [ 0.118,0.108,0.118,0.040,-0.009],
            [ 0.182,0.096,0.108,0.037,-0.012],
            [ 0.242,0.084,0.102,0.034,-0.015],
            [ 0.298,0.076,0.100,0.032,-0.018],
            [ 0.348,0.068,0.094,0.028,-0.021]];
  loft(S,RG.map(r=>ringRect(0,r[4],r[0],r[1],r[2],r[3])),0,TSB,{capStart:1,capEnd:1});
  chamferBox(S,0,0.0475,-0.022,0.028,0.021,0.044,0.007,0,TSG);
  chamferBox(S,0,-0.004,-0.012,0.116,0.106,0.026,0.010,8,TSG);
  for(const sd of [-1,1]){
    chamferBox(S,sd*0.048,-0.008,0.208,0.008,0.074,0.110,0.007,0,TSB);
    chamferBox(S,sd*0.054,-0.022,0.078,0.006,0.020,0.124,0.003,0,TSG*2);
    for(let i=0;i<3;i++)
      chamferBox(S,sd*0.048,0.012,0.152+i*0.040,0.006,0.011,0.013,0.002,0,TSG*2);
    chamferBox(S,sd*0.049,-0.002,0.196,0.005,0.011,0.082,0.002,4,TSG);
  }
  chamferBox(S,0,0.0505,0.030,0.050,0.015,0.034,0.006,0,TSB);
  {
    const Y=0.0582, x0=-0.0205, x1=0.0205, z0=0.019, z1=0.041;
    chamferBox(S,0,Y-0.0035,z0-0.004,0.050,0.006,0.005,0.0015,0,TSG*2);
    chamferBox(S,0,Y-0.0035,z1+0.004,0.050,0.006,0.005,0.0015,0,TSG*2);
    chamferBox(S,x0-0.004,Y-0.0035,0.030,0.005,0.006,0.032,0.0015,0,TSG*2);
    chamferBox(S,x1+0.004,Y-0.0035,0.030,0.005,0.006,0.032,0.0015,0,TSG*2);
    quad(S,[x0,Y,z1],[x1,Y,z1],[x1,Y,z0],[x0,Y,z0],
           [0,0],[1,0],[1,1],[0,1],9);
  }
  chamferBox(S,0,-0.068,0.124,0.046,0.026,0.112,0.010,0,TSB);
  chamferBox(S,0,-0.056,0.106,0.008,0.030,0.015,0.003,0,TSG*2);
  chamferBox(S,0,-0.056,0.134,0.008,0.030,0.015,0.003,0,TSG*2);
  push(mMul2(mT(0,-0.120,0.222),mRX(0.32)));
  chamferBox(S,0,0,0,0.046,0.130,0.060,0.013,3,TSG*2);
  pop();
  handAt(Hd,0,-0.130,0.228,0,1.0);
  handAt(Hm,0,-0.098,-0.298,0,0.94);
  return {stat:finalize(S),spin:finalize(P),hands:finalize(Hd),
          hmov:finalize(Hm),magm:finalize(newB())};
}

function buildShell(){
  const B=newB();
  lathe(B,0,0,[[0.0086,-0.025],[0.0114,-0.025],[0.0114,0.0132],
               [0.0134,0.0166],[0.0134,0.0223],[0.0100,0.0252],[0,0.0252]],12,10,28);
  return finalize(B);
}
function buildShotshell(){
  const B=newB();
  lathe(B,0,0,[[0.0112,-0.028,11],[0.0138,-0.028,11],[0.0138,0.008,10],
               [0.0154,0.012,10],[0.0154,0.020,10],[0.0116,0.025,10],[0,0.025,10]],
        14,11,26);
  return finalize(B);
}
/* Muzzle flash: one quad, drawn twice, always facing the camera (mat 20).
   A rigid star of crossed blades reads as the same cage on every shot, and
   a cone down the bore is worse -- from behind the gun you look along its
   axis, so it projects as a filled disc with a hard silhouette.  A card has
   no shape of its own; the petals are cut in the shader from uMuzS, which
   is redrawn every shot. */
function buildFlashCard(){
  const B=newB();
  quad(B,[-1,-1,0],[1,-1,0],[1,1,0],[-1,1,0],
         [0,0],[1,0],[1,1],[0,1],20);
  return finalize(B);
}

/* ------------------------------------------------- procedural textures */
const NZ=`
float hash21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vnoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  float a=hash21(i),b=hash21(i+vec2(1,0)),c=hash21(i+vec2(0,1)),d=hash21(i+vec2(1,1));
  return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);}
float fbm(vec2 p){float s=0.0,a=0.5;for(int i=0;i<5;i++){s+=a*vnoise(p);p=p*2.03+7.1;a*=0.5;}return s;}
float sdBox(vec2 p,vec2 b){vec2 d=abs(p)-b;return length(max(d,0.0))+min(max(d.x,d.y),0.0);}
`;
const GEN_VS=`#version 300 es
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}`;
const GEN_FS=`#version 300 es
precision highp float;
uniform vec2 uSeed; uniform int uMode; uniform float uRes;
uniform float uFlat,uMicro,uCells,uRustAmt,uRghBase,uRghVar;
uniform vec3 uSteel,uRust,uWorn;
layout(location=0) out vec4 oAlb;
layout(location=1) out vec4 oNrm;
layout(location=2) out vec4 oRMA;
${NZ}
float hPlate(vec2 uv){
  vec2 P=uv*uCells; vec2 id=floor(P), f=fract(P)-0.5;
  float r=hash21(id+uSeed);
  float h=0.0;
  float d=sdBox(f,vec2(0.445-0.06*step(0.72,r)));
  h+=smoothstep(0.022,-0.028,d)*(0.52+0.26*step(0.5,r));
  if(r>0.36){
    vec2 bp=abs(f)-vec2(0.345);
    float db=length(bp);
    h+=smoothstep(0.046,0.020,db)*0.34;
    h-=smoothstep(0.020,0.008,db)*0.22;
  }
  if(r<0.24){
    vec2 vp=f; vp.y=fract(vp.y*6.0+0.5)-0.5;
    float dv=sdBox(vec2(vp.x,vp.y/6.0),vec2(0.27,0.030));
    h-=smoothstep(0.012,-0.010,dv)*0.55;
  }
  if(r>0.55&&r<0.72){
    float ds=sdBox(f-vec2(0.0,0.30),vec2(0.30,0.055));
    h+=smoothstep(0.014,-0.012,ds)*0.30;
  }
  h+=(fbm(uv*90.0)-0.5)*0.075*uMicro;
  h+=(fbm(uv*260.0)-0.5)*0.035*uMicro;
  return h;
}
float hMach(vec2 uv){
  float h=0.0;
  float g=abs(fract(uv.y*22.0)-0.5)*2.0;
  h+=smoothstep(0.22,0.78,g)*0.26;
  float b=floor(uv.y*5.0);
  float rb=hash21(vec2(b,3.0)+uSeed);
  if(rb>0.58){
    vec2 k=uv*vec2(52.0,52.0);
    float kn=abs(fract(k.x+k.y)-0.5)+abs(fract(k.x-k.y)-0.5);
    h+=(0.5-kn)*0.38;
  }
  if(rb<0.20){ float d=abs(fract(uv.y*5.0)-0.5); h-=smoothstep(0.42,0.18,d)*0.30; }
  h+=(fbm(uv*vec2(70.0,140.0))-0.5)*0.09*uMicro;
  h+=(fbm(uv*vec2(220.0,420.0))-0.5)*0.04*uMicro;
  return h;
}
float hHard(vec2 uv){
  vec2 P=uv*2.0; vec2 id=floor(P), f=fract(P)-0.5;
  float r=hash21(id+uSeed*1.7);
  float h=0.0;
  h+=smoothstep(0.055,-0.020,sdBox(f,vec2(0.425,0.405)))*0.55;
  if(r>0.30&&abs(f.x)<0.36){
    float xr=fract(f.x*5.0+0.5)-0.5;
    float dl=sdBox(vec2(xr*0.2,f.y-0.17),vec2(0.034,0.125));
    h-=smoothstep(0.014,-0.008,dl)*0.72;
  }
  if(r<0.62){
    for(int i=0;i<2;i++){
      vec2 sp=f-vec2(i==0?-0.30:0.30,-0.29);
      h+=smoothstep(0.018,-0.010,length(sp)-0.082)*0.30;
      h-=smoothstep(0.010,-0.006,sdBox(sp,vec2(0.072,0.015)))*0.36;
    }
  }
  if(r>0.72){
    h-=smoothstep(0.012,-0.006,sdBox(f-vec2(0.0,-0.05),vec2(0.34,0.014)))*0.30;
  }
  h+=(fbm(uv*70.0)-0.5)*0.055*uMicro;
  h+=(fbm(uv*200.0)-0.5)*0.030*uMicro;
  return h;
}
float H(vec2 uv){ return uMode==0 ? hPlate(uv) : (uMode==1 ? hMach(uv) : hHard(uv)); }
void main(){
  vec2 uv=gl_FragCoord.xy/uRes;
  float e=1.0/uRes;
  float h =H(uv);
  float hx=H(uv+vec2(e,0.0));
  float hy=H(uv+vec2(0.0,e));
  float gx=(hx-h)/e, gy=(hy-h)/e;
  vec3 n=normalize(vec3(-gx,-gy,uFlat));
  float hw=(H(uv+vec2(4.0*e,0.0))+H(uv-vec2(4.0*e,0.0))
           +H(uv+vec2(0.0,4.0*e))+H(uv-vec2(0.0,4.0*e)))*0.25;
  float ao=clamp(1.0-(hw-h)*1.9,0.25,1.0);
  float grime=fbm(uv*6.0+uSeed*0.3);
  float ox=smoothstep(0.56,0.88,grime);
  vec3 alb=mix(uSteel,uRust,ox*uRustAmt);
  alb=mix(alb,uWorn,smoothstep(0.46,0.92,h)*0.40);
  alb*=0.66+0.44*ao;
  float rgh=uRghBase+uRghVar*ox-0.12*smoothstep(0.5,0.95,h)+0.07*(1.0-ao);
  rgh=clamp(rgh,0.05,0.92);
  oAlb=vec4(alb,1.0);
  oNrm=vec4(n*0.5+0.5,1.0);
  oRMA=vec4(rgh,1.0,ao,h*0.5+0.5);
}`;

/* --------------------------------------------------- viewmodel shader
   Rendered in VIEW SPACE: uView is identity, uModel places the gun in
   front of the camera, so the camera sits at the origin.  The scene's
   sampled light (VM.lit / VM.ldir) drives a key + fill rig; envCol is
   the ref's dark-interior probe scaled to the room's brightness. */
const VM_VS=`#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aTan;
layout(location=4) in float aMat;
uniform mat4 uProj,uModel; uniform mat3 uNM;
out vec3 vP; out vec3 vN; out vec2 vUV; out vec3 vT; out float vW; flat out float vM;
void main(){
  vec4 wp=uModel*vec4(aPos,1.0);
  vP=wp.xyz; vN=normalize(uNM*aNrm); vT=normalize(uNM*aTan.xyz); vW=aTan.w;
  vUV=aUV; vM=aMat;
  gl_Position=uProj*wp;
}`;
const VM_FS=`#version 300 es
precision highp float;
in vec3 vP; in vec3 vN; in vec2 vUV; in vec3 vT; in float vW; flat in float vM;
out vec4 o;
uniform sampler2D tA0,tA1,tA2,tB0,tB1,tB2,tC0,tC1,tC2;
uniform vec3 uAmb;            // sampled room light at the player
uniform vec3 uLDir;           // its dominant direction, view space
uniform mat3 uV2W;            // view -> world rotation (for the env probe)
uniform vec3 uMuzP,uMuzC;     // muzzle point light, view space
uniform float uFlashI;        // player flashlight (cone down -Z from origin)
uniform float uMuzI;          // flash intensity (mats 20, 21)
uniform float uMuzS;          // flash shape seed, redrawn every shot
uniform float uMuzG;          // 1 = the soft bloom pass, 0 = the burst itself
uniform float uGain;          // ref-tuned constants -> engine light units
uniform float uTime; uniform int uAmmo; uniform float uHeat; uniform vec3 uAccent,uPaint;
const vec4 SEGS[7]=vec4[7](
  vec4(0.15,0.92,0.85,0.92), vec4(0.90,0.87,0.90,0.55), vec4(0.90,0.45,0.90,0.13),
  vec4(0.15,0.08,0.85,0.08), vec4(0.10,0.13,0.10,0.45), vec4(0.10,0.55,0.10,0.87),
  vec4(0.15,0.50,0.85,0.50));
const int DMASK[10]=int[10](63,6,91,79,102,109,125,7,127,111);
float sdSeg(vec2 p,vec2 a,vec2 b){vec2 pa=p-a,ba=b-a;
  float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0);return length(pa-ba*h);}
vec3 F_S(vec3 f0,float u){return f0+(1.0-f0)*pow(1.0-u,5.0);}
float D_GGX(float NoH,float a){float a2=a*a;float d=NoH*NoH*(a2-1.0)+1.0;return a2/(3.14159265*d*d);}
float V_S(float NoV,float NoL,float a){float a2=a*a;
  float gv=NoL*sqrt(NoV*NoV*(1.0-a2)+a2), gg=NoV*sqrt(NoL*NoL*(1.0-a2)+a2);
  return 0.5/max(gv+gg,1e-5);}
vec3 envBRDF(vec3 f0,float r,float NoV){
  vec4 c0=vec4(-1.0,-0.0275,-0.572,0.022), c1=vec4(1.0,0.0425,1.04,-0.04);
  vec4 rr=r*c0+c1;
  float a=min(rr.x*rr.x,exp2(-9.28*NoV))*rr.x+rr.y;
  vec2 AB=vec2(-1.04,1.04)*a+rr.zw;
  return f0*AB.x+AB.y;}
/* the current room's ambient cube, in engine light units -- the same probe
   the world shader reflects, so gun metal and wall metal share one room */
uniform vec3 uCube[6];
vec3 ambW(vec3 d){
  vec3 s=d*d;
  return s.x*(d.x>0.0?uCube[0]:uCube[1])
       + s.y*(d.y>0.0?uCube[2]:uCube[3])
       + s.z*(d.z>0.0?uCube[4]:uCube[5]);}
/* value noise for the flash.  Sampled on the unit circle so the angular
   bands close on themselves -- a seam would be its own kind of regularity. */
float h21(vec2 p){p=fract(p*vec2(127.1,311.7));p+=dot(p,p+34.53);return fract(p.x*p.y);}
float vn2(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),
             mix(h21(i+vec2(0,1)),h21(i+vec2(1,1)),f.x),f.y);}
float fb2(vec2 p){float s=0.0,a=0.5;
  for(int i=0;i<4;i++){s+=a*vn2(p);p=p*2.07+3.7;a*=0.5;}return s;}
vec3 shade(vec3 N,vec3 V,vec3 L,vec3 rad,vec3 alb,float met,float rgh){
  vec3 H=normalize(V+L);
  float NoL=max(dot(N,L),0.0), NoV=max(dot(N,V),1e-4);
  float NoH=max(dot(N,H),0.0), VoH=max(dot(V,H),0.0);
  float a=max(rgh*rgh,0.008);
  vec3 f0=mix(vec3(0.04),alb,met);
  vec3 F=F_S(f0,VoH);
  vec3 spec=F*D_GGX(NoH,a)*V_S(NoV,NoL,a);
  vec3 kd=(1.0-F)*(1.0-met);
  return (kd*alb/3.14159265+spec)*rad*NoL;
}
void main(){
  int m=int(vM+0.5);
  if(m==20){                     // flash card, camera-facing, unlit additive
    vec2 p=vUV*2.0-1.0;
    float r=length(p);
    if(r>1.0){ o=vec4(0.0); return; }
    /* The bloom pass: a round glow with no structure, drawn without the
       depth test.  A flash this bright bleeds over the barrel in any real
       camera -- what looked pasted on before was the petals doing it. */
    if(uMuzG>0.5){
      float g=pow(1.0-r,2.6);
      o=vec4(vec3(1.0,0.55,0.18)*g*uMuzI,1.0); return;
    }
    vec2 dir=p/max(r,1e-4);      // on the unit circle: the bands have no seam
    // a few directions carry most of the fire -- pow() turns the soft noise
    // into spikes, which is what stops the burst reading as a disc
    float a1=fb2(dir*2.60+vec2(uMuzS,uMuzS*1.7));
    float a2=fb2(dir*6.10-vec2(uMuzS*2.1,uMuzS));
    float lobe=0.26+0.60*pow(clamp(a1*1.18,0.0,1.0),2.0)+0.16*a2;
    float e=r/max(lobe,1e-3);                      // 0 centre .. 1 petal tip
    float body=smoothstep(1.0,0.45,e)*(1.0-min(e,1.0));
    body*=0.35+0.65*fb2(dir*12.0+vec2(uMuzS*3.3,e*4.0));   // ragged rim
    float core=exp(-r*r*26.0);   // the heart is a fixed size, not a petal
    // saturated, or the tonemap washes the whole burst to grey
    vec3 c=mix(vec3(1.0,0.58,0.14),vec3(0.95,0.13,0.02),clamp(e,0.0,1.0));
    c=mix(c,vec3(1.0,0.96,0.86),core);
    o=vec4(c*(body*2.2+core*2.4)*uMuzI,1.0); return;
  }
  vec3 aA=texture(tA0,vUV).rgb, nA=texture(tA1,vUV).xyz*2.0-1.0; vec4 rA=texture(tA2,vUV);
  vec3 aB=texture(tB0,vUV).rgb, nB=texture(tB1,vUV).xyz*2.0-1.0; vec4 rB=texture(tB2,vUV);
  vec3 aC=texture(tC0,vUV).rgb, nC=texture(tC1,vUV).xyz*2.0-1.0; vec4 rC=texture(tC2,vUV);
  vec3 alb; vec3 nT; float rgh,met,ao; vec3 emis=vec3(0.0);
  if(m==1){ alb=aB; nT=nB; rgh=rB.x; met=0.78; ao=rB.z; }
  else    { alb=aA; nT=nA; rgh=rA.x; met=0.78; ao=rA.z; }
  if(m==2){
    alb=mix(uAccent,uAccent*0.23,1.0-rA.z)*(0.6+0.8*rA.z);
    met=0.0; rgh=clamp(0.36+0.35*(1.0-rA.z),0.2,0.85); nT=nA;
  } else if(m==3){
    alb=vec3(0.016,0.017,0.019); met=0.0; rgh=0.86; nT=nA*vec3(1.6,1.6,1.0);
  } else if(m==4){
    alb=vec3(0.02); met=0.0; rgh=0.35;
    float fl=0.86+0.14*sin(uTime*11.0+vP.z*7.0);
    emis=vec3(0.16,0.86,1.0)*2.6*fl;
  } else if(m==5){
    alb=vec3(0.006); met=1.0; rgh=0.55; ao=0.25;
  } else if(m==13){
    alb=mix(uPaint,uPaint*0.26,1.0-rC.z)*(0.62+0.76*rC.z);
    alb=mix(alb,vec3(0.155,0.150,0.140),smoothstep(0.60,0.95,rC.w)*0.34);
    met=0.06; rgh=clamp(0.46+0.28*(1.0-rC.z),0.26,0.90); nT=nC;
  } else if(m==14){
    alb=aC*1.30; met=0.80; ao=rC.z;
    rgh=clamp(rC.x*0.72,0.10,0.55); nT=nC;
  } else if(m==12){
    alb=aB*1.10; met=1.0; ao=rB.z;
    rgh=clamp(rB.x*0.42+0.04,0.05,0.34);
    nT=normalize(mix(vec3(0.0,0.0,1.0),nB,0.16));
  } else if(m==11){
    alb=mix(vec3(0.44,0.055,0.030),vec3(0.15,0.020,0.012),1.0-rB.z);
    met=0.0; rgh=clamp(0.32+0.26*(1.0-rB.z),0.18,0.80); nT=nB;
  } else if(m==10){
    alb=mix(vec3(0.60,0.40,0.13),vec3(0.26,0.16,0.05),1.0-rB.z);
    met=1.0; rgh=clamp(0.16+0.34*(1.0-rB.z),0.10,0.62); nT=nB;
  } else if(m==8){
    float tt=fract((vUV.x+vUV.y)*3.2);
    float w=max(fwidth(tt)*1.2,0.012);
    float st=smoothstep(0.5-w,0.5+w,tt)*(1.0-smoothstep(1.0-w,1.0,tt));
    vec3 yl=vec3(0.52,0.34,0.020), bk=vec3(0.012,0.012,0.013);
    alb=mix(bk,yl,st);
    alb=mix(alb,vec3(0.16,0.15,0.13),smoothstep(0.55,0.95,rA.w)*0.35);
    alb*=0.55+0.55*rA.z;
    met=0.0; rgh=clamp(0.44+0.30*(1.0-rA.z),0.25,0.9); nT=nA;
  } else if(m==6){
    alb=mix(vec3(0.030,0.033,0.028),vec3(0.075,0.070,0.055),rA.z)*0.9;
    met=0.12; rgh=clamp(0.52+0.25*(1.0-rA.z),0.3,0.9); nT=nA;
  }
  /* barrel heat: red must saturate while green/blue stay on the linear
     part of the tonemap curve, or the glow turns white */
  if((m==1||m==12)&&uHeat>0.24){
    float h=clamp((uHeat-0.24)/0.76,0.0,1.0);
    float v=0.68+0.32*rB.w;
    float fl=0.90+0.10*sin(uTime*23.0+vUV.y*9.0);
    emis+=mix(vec3(1.0,0.035,0.004),vec3(1.0,0.095,0.020),h)*pow(h,1.6)*2.5*v*fl;
    rgh=clamp(rgh*0.85,0.08,0.90);
  }
  vec3 N0=normalize(vN);
  vec3 T=normalize(vT-N0*dot(N0,vT));
  vec3 B=cross(N0,T)*vW;
  vec3 N=normalize(mat3(T,B,N0)*normalize(nT));
  /* specular AA: where the bump normals churn within a pixel, GGX highlights
     shrink below pixel size and sparkle -- widen roughness to match */
  { vec3 du=dFdx(N),dv=dFdy(N);
    float varN=0.25*(dot(du,du)+dot(dv,dv));
    rgh=sqrt(min(rgh*rgh+min(2.0*varN,0.18),1.0)); }
  vec3 V=normalize(-vP);
  if(m==9){                                // digital ammo counter
    vec2 p=vUV;
    vec3 c=vec3(0.008,0.011,0.013);
    if(p.x>0.035&&p.x<0.965&&p.y>0.07&&p.y<0.93){
      float lit=0.0,gh=0.0;
      for(int k=0;k<3;k++){
        vec2 q=vec2((p.x-(0.09+float(k)*0.28))/0.24,(p.y-0.16)/0.68);
        if(q.x<-0.25||q.x>1.25) continue;
        int dv = k==0?100:(k==1?10:1);
        int mk = uAmmo<0 ? 64 : DMASK[(uAmmo/dv)%10];
        for(int i=0;i<7;i++){
          float dd=sdSeg(q,SEGS[i].xy,SEGS[i].zw)-0.052;
          float a=smoothstep(0.022,-0.012,dd);
          gh+=a*0.05;
          if(((mk>>i)&1)==1) lit+=a;
        }
      }
      vec3 on=vec3(0.18,1.0,0.82);
      float bl=uAmmo<0?(0.35+0.65*step(0.0,sin(uTime*18.0))):1.0;
      c+=on*(min(lit,1.0)*3.0+gh*0.6)*bl;
      c*=0.90+0.10*sin(p.y*180.0);
    }
    c+=ambW(uV2W*reflect(-V,normalize(vN)))*0.12;
    o=vec4(c*uGain*2.2,1.0); return;
  }
  if(dot(N,V)<0.0) N=normalize(N-V*dot(N,V)*1.2);

  float lum=dot(uAmb,vec3(0.35,0.5,0.15));
  vec3 col=vec3(0.0);
  // key: the room's dominant light
  col+=shade(N,V,normalize(uLDir),uAmb*2.8,alb,met,rgh);
  // fill from up-left of the camera, rim from the right (scaled to the room)
  col+=shade(N,V,normalize(vec3(-0.35,0.55,0.75)),uAmb*0.9+vec3(0.008),alb,met,rgh);
  col+=shade(N,V,normalize(vec3(0.80,0.25,-0.40)),uAmb*0.55,alb,met,rgh);
  { vec3 d=uMuzP-vP; float dist=length(d);
    vec3 rad=uMuzC/(1.0+3.2*dist*dist);
    col+=shade(N,V,d/max(dist,1e-4),rad,alb,met,rgh); }
  if(uFlashI>0.0){
    vec3 d=-vP; float dist=length(d); vec3 L=d/max(dist,1e-4);
    float cone=smoothstep(0.72,0.92,dot(-L,vec3(0.0,0.0,-1.0)));
    vec3 rad=vec3(1.0,0.94,0.86)*uFlashI*cone/(1.0+0.18*dist*dist);
    col+=shade(N,V,L,rad,alb,met,rgh);
  }
  float NoV=max(dot(N,V),1e-4);
  vec3 R=reflect(-V,N);
  vec3 f0=mix(vec3(0.04),alb,met);
  // the same weighting the world shader gives its own metal, or the gun
  // reads the room's probe and still comes back darker than the wall
  col+=ambW(uV2W*R)*envBRDF(f0,rgh,NoV)*ao
       *mix(1.0,2.0,met)*mix(1.0,3.0,pow(1.0-rgh,3.0));
  col+=alb*(1.0-met)*(uAmb*0.55+ambW(uV2W*N)*0.45)*ao;
  col+=emis*uGain*1.6;
  o=vec4(col,1.0);
}`;

/* --------------------------------------------------------------- GL */
const glW=gl;   // the engine's context, already created
function shW(type,src){const s=glW.createShader(type);glW.shaderSource(s,src);glW.compileShader(s);
  if(!glW.getShaderParameter(s,glW.COMPILE_STATUS))
    console.error('weapons shader:',glW.getShaderInfoLog(s));return s;}
function progW(vs,fs){const p=glW.createProgram();
  glW.attachShader(p,shW(glW.VERTEX_SHADER,vs));glW.attachShader(p,shW(glW.FRAGMENT_SHADER,fs));
  glW.linkProgram(p);
  if(!glW.getProgramParameter(p,glW.LINK_STATUS))console.error('weapons link:',glW.getProgramInfoLog(p));
  return p;}

const RESW=512;
function makeTex(){const t=glW.createTexture();glW.bindTexture(glW.TEXTURE_2D,t);
  glW.texImage2D(glW.TEXTURE_2D,0,glW.RGBA8,RESW,RESW,0,glW.RGBA,glW.UNSIGNED_BYTE,null);
  glW.texParameteri(glW.TEXTURE_2D,glW.TEXTURE_WRAP_S,glW.REPEAT);
  glW.texParameteri(glW.TEXTURE_2D,glW.TEXTURE_WRAP_T,glW.REPEAT);
  glW.texParameteri(glW.TEXTURE_2D,glW.TEXTURE_MIN_FILTER,glW.LINEAR_MIPMAP_LINEAR);
  glW.texParameteri(glW.TEXTURE_2D,glW.TEXTURE_MAG_FILTER,glW.LINEAR);return t;}

const TP={str:1.0,micro:0.2,cells:4.0,rust:0.52,rgh:0.23,rghv:0.25,
          steel:"#55575a",rustc:"#5d4b3c",worn:"#838588",accent:"#d47e31",
          paint:"#4c5533"};
const TEXW=[[makeTex(),makeTex(),makeTex()],[makeTex(),makeTex(),makeTex()],
            [makeTex(),makeTex(),makeTex()]];
const genProg=progW(GEN_VS,GEN_FS);
const genFBO=glW.createFramebuffer();
const anisoW=glW.getExtension("EXT_texture_filter_anisotropic");

function generateTextures(seed){
  const rng=mulberry32(seed);
  glW.bindVertexArray(null);
  glW.bindFramebuffer(glW.FRAMEBUFFER,genFBO);
  glW.viewport(0,0,RESW,RESW);
  glW.useProgram(genProg);
  glW.disable(glW.DEPTH_TEST);
  const gu=k=>glW.getUniformLocation(genProg,k);
  glW.uniform1f(gu("uFlat"),52.0/TP.str);
  glW.uniform1f(gu("uMicro"),TP.micro);
  glW.uniform1f(gu("uCells"),TP.cells);
  glW.uniform1f(gu("uRustAmt"),TP.rust);
  glW.uniform1f(gu("uRghBase"),TP.rgh);
  glW.uniform1f(gu("uRghVar"),TP.rghv);
  glW.uniform3fv(gu("uSteel"),lin(TP.steel));
  glW.uniform3fv(gu("uRust"),lin(TP.rustc));
  glW.uniform3fv(gu("uWorn"),lin(TP.worn));
  for(let mode=0;mode<3;mode++){
    for(let i=0;i<3;i++)
      glW.framebufferTexture2D(glW.FRAMEBUFFER,glW.COLOR_ATTACHMENT0+i,glW.TEXTURE_2D,TEXW[mode][i],0);
    glW.drawBuffers([glW.COLOR_ATTACHMENT0,glW.COLOR_ATTACHMENT1,glW.COLOR_ATTACHMENT2]);
    glW.uniform2f(gu("uSeed"),rng()*40,rng()*40);
    glW.uniform1i(gu("uMode"),mode);
    glW.uniform1f(gu("uRes"),RESW);
    glW.drawArrays(glW.TRIANGLES,0,3);
    for(let i=0;i<3;i++){glW.bindTexture(glW.TEXTURE_2D,TEXW[mode][i]);glW.generateMipmap(glW.TEXTURE_2D);
      if(anisoW)glW.texParameterf(glW.TEXTURE_2D,anisoW.TEXTURE_MAX_ANISOTROPY_EXT,8);}
  }
  glW.bindFramebuffer(glW.FRAMEBUFFER,null);
  glW.enable(glW.DEPTH_TEST);
  glW.activeTexture(glW.TEXTURE0);
}

const vmProg=progW(VM_VS,VM_FS);
const UW={};
["uProj","uModel","uNM","uAmb","uLDir","uV2W","uCube","uMuzP","uMuzC","uFlashI","uMuzI","uMuzS","uMuzG",
 "uGain","uTime","uAmmo","uHeat","uAccent","uPaint",
 "tA0","tA1","tA2","tB0","tB1","tB2","tC0","tC1","tC2"]
 .forEach(k=>UW[k]=glW.getUniformLocation(vmProg,k));

function upload(data){
  const vao=glW.createVertexArray();glW.bindVertexArray(vao);
  const b=glW.createBuffer();glW.bindBuffer(glW.ARRAY_BUFFER,b);
  glW.bufferData(glW.ARRAY_BUFFER,data,glW.STATIC_DRAW);
  const S=13*4;
  const spec=[[0,3,0],[1,3,12],[2,2,24],[3,4,32],[4,1,48]];
  for(const [loc,size,off] of spec){glW.enableVertexAttribArray(loc);
    glW.vertexAttribPointer(loc,size,glW.FLOAT,false,S,off);}
  glW.bindVertexArray(null);
  return {vao,count:data.length/13,buf:b};
}

const GUNSEED=7;
const WMESH=[buildPistol(GUNSEED^0x2c1b),buildRotary(GUNSEED),buildShotgun(GUNSEED^0x9e37)]
  .map(g=>({stat:upload(g.stat),spin:upload(g.spin),hands:upload(g.hands),
            hmov:upload(g.hmov),magm:upload(g.magm)}));
const mShell=upload(buildShell());
const mShot=upload(buildShotshell());
const mCard=upload(buildFlashCard());
generateTextures(1337);

/* ------------------------------------------------- state + weapon data */
const A={ heat:0, over:false, spin:0, spinV:0, recoil:0, jolt:0, climb:0, idle:0,
  muzzle:0, flashRoll:0, flashSeed:0, slideZ:0, magDrop:0, brk:0, rlp:0, relU:0,
  spent:0, shotSide:0, dryT:0, clickT:0, prevReload:0,
  ejd:false, ldd:false, snB:false, shells:[], previzOn:false,
  gunM:mI(), gunW:mI(), tune:{gain:0.34, flash:1.0} };

const COOL_IDLE=0.17, COOL_FIRE=0.02, RESET_AT=0.45;

function brkCurve(u){
  if(u<0.16){ const t=u/0.16;      return 1.06*(1-Math.pow(1-t,3)); }
  if(u<0.26){ const t=(u-0.16)/0.10; return 1.06-0.06*(t*t*(3-2*t)); }
  if(u<0.52) return 1.0;
  if(u<0.68){ const t=(u-0.52)/0.16; return 1.0-t*t*t; }
  if(u<0.82){ const t=(u-0.68)/0.14; return 0.11*Math.sin(t*Math.PI)*(1-t); }
  return 0;
}

/* fold the ref weapon table into the engine's WEP slots.
   ids stay glock/shotgun/smg so HAVE / AMMO / MAGS / pickups keep working. */
function applyWeaponTable(){
  Object.assign(WEP[1],{ name:'PISTOL', kind:2, wmesh:0,
    rate:0.16, mag:12, reload:1.35, reldly:0.10, spread:0.008,
    kickA:0.020, kickR:0.008, shake:0.30, vrec:1.6, vpush:0.040, vdec:8.0,
    vm:[0.15,-0.145,-0.42], muz:-0.19, twin:0,
    ejP:[0.028,0.018,0.030], ejV:[1.9,2.6,-0.6], shell:0, ejShot:1,
    heatAdd:0.006, mfl:1.3, flash:0.15, smoke:0.05 });
  delete WEP[1].casing;
  Object.assign(WEP[2],{ name:'DOUBLE-BARREL', kind:1, wmesh:2,
    rate:0.30, mag:2, reload:1.30, reldly:0.55, pellets:12, spread:0.062,
    kickA:0.075, kickR:0.022, shake:0.95, vrec:4.0, vpush:0.062, vdec:5.0,
    vm:[0.19,-0.21,-0.31], muz:-0.68, twin:0.032,
    ejP:[0.032,0.010,-0.050], ejV:[0.35,3.2,-0.7], shell:1, ejShot:0,
    heatAdd:0.030, mfl:2.8, flash:0.46, smoke:0.20 });
  delete WEP[2].shellwise; delete WEP[2].casing;
  Object.assign(WEP[3],{ name:'ROTARY CANNON', kind:0, wmesh:1,
    rate:0.055, mag:0, reload:0, reldly:0, spread:0.014, auto:true, spinUp:true,
    kickA:0.0090, kickR:0.0050, shake:0.10, vrec:1.0, vpush:0.034, vdec:9.0,
    vm:[0.20,-0.20,-0.34], muz:-0.70, twin:0,
    ejP:[0.080,0.082,0.140], ejV:[0.75,3.9,-2.6], shell:0, ejShot:1,
    heatAdd:0.0105, mfl:1.0, flash:0.21, smoke:0.055 });
  delete WEP[3].casing;
  MAGS.glock=Math.min(MAGS.glock,WEP[1].mag);
  MAGS.shotgun=Math.min(MAGS.shotgun,WEP[2].mag);
  MAGS.smg=0;
}
applyWeaponTable();

/* the camera matrix in vmToWorld's convention: vm x right, y up, -z forward */
function camMat(){
  return new Float32Array([camR[0],camR[1],camR[2],0, camU[0],camU[1],camU[2],0,
    -camF[0],-camF[1],-camF[2],0, eyeP[0],eyeP[1],eyeP[2],1]);
}

function ejectShell3D(w,po,ve){
  if(A.shells.length>34)A.shells.shift();
  const M=A.gunW, EP=po||w.ejP, EV=ve||w.ejV;
  A.shells.push({p:xfp(M,EP),
    v:rot3(M,[(EP[0]<0?-EV[0]:EV[0])+rnd(0.35),EV[1]+Math.random()*0.7,EV[2]+rnd(0.5)]),
    r:[Math.random()*6,Math.random()*6,Math.random()*6],
    w:[rnd(20),rnd(20),rnd(20)],t:0,s:w.shell});
}

/* one shot: recoil, heat, muzzle light, casings, world particles.
   Ammo bookkeeping stays in tryFire; the hitscan stays in fireHitscan. */
function fireVMW(w){
  A.heat=Math.min(1,A.heat+(w.heatAdd||0));
  if(A.heat>=1) A.over=true;
  A.muzzle=1; A.flashRoll=Math.random()*6.2832; A.flashSeed=Math.random()*64.0;
  A.recoil=w.vrec;
  A.shotSide^=1;
  const k=w.kickA+Math.random()*w.kickR;
  P.pitch=Math.min(1.45,P.pitch+k);
  P.yaw+=rnd(w.kickA*0.6);
  A.climb+=k*0.65;
  VM.flash=1; VM.flashRoll=A.flashRoll;
  A.gunW=mMul2(camMat(),A.gunM);
  const mx=w.twin?(A.shotSide?w.twin:-w.twin):0;
  const q=xfp(A.gunW,[mx,0,w.muz]);
  muzW[0]=q[0];muzW[1]=q[1];muzW[2]=q[2];
  if(w.light) addDynLight(muzW[0],muzW[1],muzW[2],
    w.light[0]*2.4*w.mfl,w.light[1]*2.4*w.mfl,w.light[2]*2.4*w.mfl,w.light[3],0.075);
  const nspark=w.pellets>1?9:4;
  for(let i=0;i<nspark;i++){
    const a=Math.random()*6.28, r=Math.random()*0.5;
    emit(muzW[0],muzW[1],muzW[2],
      camF[0]*(5+Math.random()*7)+camR[0]*Math.cos(a)*r+camU[0]*Math.sin(a)*r,
      camF[1]*(5+Math.random()*7)+camU[1]*Math.sin(a)*r,
      camF[2]*(5+Math.random()*7)+camR[2]*Math.cos(a)*r+camU[2]*Math.sin(a)*r,
      0.045,0.12+Math.random()*0.1,PK.SPARK,1.0,0.80,0.42,1.0,3,3.5,true);
  }
  if(w.mag>0||((AMMO[w.id]&3)===0))
    emit(muzW[0]+camF[0]*0.3,muzW[1]+camF[1]*0.3,muzW[2]+camF[2]*0.3,
      camF[0]*1.2,camF[1]*1.2+0.4,camF[2]*1.2, w.smoke>0.1?0.30:0.18,
      0.55,PK.SMOKE,0.44,0.42,0.40,0.32,-0.3,2.0);
  if(w.ejShot) ejectShell3D(w);
  else A.spent=Math.min(w.mag,A.spent+1);
}

/* per-frame weapon dynamics, called from updateWeapons */
function stepVMW(w,dt,rdt){
  if(A.previzOn){
    HAVE.shotgun=true; HAVE.smg=true;
    if(AMMO.glock<400)AMMO.glock=999;
    if(AMMO.shotgun<400)AMMO.shotgun=999;
    if(AMMO.smg<400)AMMO.smg=999;
  }
  const gun=w.wmesh!==undefined;
  // heat / overheat (all guns feed it; only the cannon really moves it)
  A.heat=Math.max(0,A.heat-rdt*((holdFire&&!A.over&&gun)?COOL_FIRE:COOL_IDLE));
  if(A.over&&A.heat<RESET_AT) A.over=false;
  // barrel spin-up
  const hasAmmo=gun?(w.mag>0?MAGS[w.id]>0:AMMO[w.id]>0):false;
  const wantSpin=gun&&w.spinUp&&holdFire&&hasAmmo&&!A.over&&reloadT<=0&&!gameOver;
  const target=wantSpin?36:0;
  A.spinV+=(target-A.spinV)*Math.min(rdt*(wantSpin?2.6:3.4),1);
  A.spin+=A.spinV*rdt;
  if(A.spin>62.83)A.spin-=62.83;
  // reload staging
  if(gun&&reloadT>0&&w.mag>0){
    if(A.prevReload<=0){ A.ejd=false; A.ldd=false; A.snB=false; }
    A.relU=1-reloadT/w.reload;
    A.rlp=Math.min(1,A.relU/0.10)*Math.min(1,(1-A.relU)/0.14);
    if(w.kind===1) A.brk=brkCurve(A.relU);
    const ejAt=w.kind===2?0.18:0.12;
    if(!A.ejd&&A.relU>ejAt){ A.ejd=true;
      if(A.spent>0){
        A.jolt=Math.max(A.jolt,0.60);
        for(let i=0;i<A.spent;i++)
          ejectShell3D(w,[(i?1:-1)*0.032,0.012,-0.048],[0.40,4.6,-1.0]);
        A.spent=0;
        sfxClick(360,0.10,0.08,'square');
      } else { A.jolt=Math.max(A.jolt,0.38); sfxClick(300,0.08,0.07,'square'); }
    }
    const ldAt=w.kind===2?0.76:0.42;
    if(!A.ldd&&A.relU>ldAt){ A.ldd=true;
      if(w.kind===2) A.jolt=Math.max(A.jolt,0.70);
      sfxClick(520,0.10,0.07,'square');
    }
    if(w.kind===2&&!A.snB&&A.relU>0.88){ A.snB=true; A.jolt=Math.max(A.jolt,0.85);
      A.recoil=Math.max(A.recoil,0.5); sfxClick(760,0.12,0.09,'square'); }
    if(w.kind!==2&&!A.snB&&A.relU>0.68){ A.snB=true; A.jolt=1.0;
      A.recoil=Math.max(A.recoil,0.85); sfxClick(700,0.13,0.10,'square');
      A.gunW=mMul2(camMat(),A.gunM);
      const bp=xfp(A.gunW,[0,0.015,-0.048]);
      for(let i=0;i<6;i++)
        emit(bp[0],bp[1],bp[2],rnd(1.6),0.4+Math.random()*1.1,rnd(1.6),
          0.011,0.12+Math.random()*0.12,PK.SPARK,1.0,0.80,0.46,0.9,8,2.4,true);
    }
  } else { A.relU=0; A.rlp=Math.max(0,A.rlp-rdt*7); A.brk=w.kind===1?A.brk*Math.max(0,1-rdt*10):0; }
  A.prevReload=reloadT;
  // pistol slide (fire cycle 35% back / 65% forward, locks when empty)
  if(gun&&w.kind===2){
    const empty=MAGS[w.id]<=0&&(reloadT<=0||A.relU<0.86);
    let c=0;
    if(fireT>0){ const u=1-fireT/w.rate;
      c = u<0.35 ? u/0.35 : Math.max(0,1-(u-0.35)/0.65); }
    A.slideZ=(empty?1:c)*0.046;
    A.magDrop = reloadT<=0 ? 0
      : A.relU<0.22 ? 0.30*(A.relU/0.22)
      : A.relU<0.56 ? 0.30
      : A.relU<0.80 ? 0.30*(1-(A.relU-0.56)/0.24) : 0;
  } else { A.slideZ=0; A.magDrop=0; }
  // delayed auto-reload after the last round (reldly keeps it off the recoil)
  if(A.dryT>0){ A.dryT=Math.max(0,A.dryT-rdt);
    if(A.dryT<=0&&gun&&w.mag>0&&MAGS[w.id]<=0&&AMMO[w.id]>0&&reloadT<=0) startReload();
  }
  // recoil recovery: only 65% of the climb comes back (fireVMW banked it)
  if(!holdFire&&A.climb>0){ const r=Math.min(A.climb,rdt*0.95); P.pitch-=r; A.climb-=r; }
  A.muzzle=Math.max(0,A.muzzle-rdt*16);
  A.recoil=Math.max(0,A.recoil-rdt*(w.vdec||8));
  A.jolt=Math.max(0,A.jolt-rdt*13);
  // idle sway lives while standing still, dies fast on any action
  const act=holdFire||fireT>0||reloadT>0||moveSpeedNow>0.4;
  A.idle=Math.max(0,Math.min(1,A.idle+(act?-rdt*4.0:rdt*1.2)));
  // overheated trigger: a dry clunk and chamber sparks
  if(A.over&&holdFire&&gun){
    A.clickT-=rdt;
    if(A.clickT<=0){
      A.clickT=0.13; A.recoil=Math.max(A.recoil,0.22);
      sfxClick(180+Math.random()*60,0.10,0.10,'triangle');
      A.gunW=mMul2(camMat(),A.gunM);
      const bp=xfp(A.gunW,[0,0.02,-0.10]);
      for(let i=0;i<3;i++)
        emit(bp[0],bp[1],bp[2],rnd(1.2),0.5+Math.random()*0.9,rnd(1.2),
          0.010,0.14+Math.random()*0.12,PK.SPARK,1.0,0.74,0.34,0.85,7,2,true);
    }
  }
  // hot housing leaks smoke
  if(gun&&A.heat>RESET_AT&&Math.random()<rdt*A.heat*16){
    A.gunW=mMul2(camMat(),A.gunM);
    const hp=xfp(A.gunW,[rnd(0.06),0.09,-0.10+rnd(0.06)]);
    emit(hp[0],hp[1],hp[2],rnd(0.25),0.55+Math.random()*0.5,rnd(0.25),
      0.06,0.6+Math.random()*0.5,PK.SMOKE,0.36,0.33,0.32,0.26*A.heat,-0.5,2.0);
  }
  // spent casings: gravity, floor bounce, tumbling
  const floorY=P.y+0.012;
  for(let i=A.shells.length-1;i>=0;i--){
    const c=A.shells[i]; c.t+=rdt;
    if(c.t>4.2){A.shells.splice(i,1);continue;}
    c.v[1]-=12*rdt;
    c.p[0]+=c.v[0]*rdt;c.p[1]+=c.v[1]*rdt;c.p[2]+=c.v[2]*rdt;
    c.r[0]+=c.w[0]*rdt;c.r[1]+=c.w[1]*rdt;c.r[2]+=c.w[2]*rdt;
    if(c.p[1]<floorY&&c.v[1]<0){c.p[1]=floorY;c.v[1]*=-0.34;
      c.v[0]*=0.62;c.v[2]*=0.62;c.w[0]*=0.5;c.w[1]*=0.5;c.w[2]*=0.5;}
  }
}

/* --------------------------------------------------------------- draw */
function bindCommon(){
  glW.useProgram(vmProg);
  for(let i=0;i<3;i++){glW.activeTexture(glW.TEXTURE0+i);glW.bindTexture(glW.TEXTURE_2D,TEXW[0][i]);
    glW.uniform1i(UW["tA"+i],i);}
  for(let i=0;i<3;i++){glW.activeTexture(glW.TEXTURE3+i);glW.bindTexture(glW.TEXTURE_2D,TEXW[1][i]);
    glW.uniform1i(UW["tB"+i],3+i);}
  for(let i=0;i<3;i++){glW.activeTexture(glW.TEXTURE6+i);glW.bindTexture(glW.TEXTURE_2D,TEXW[2][i]);
    glW.uniform1i(UW["tC"+i],6+i);}
  glW.uniform3fv(UW.uAmb,VM.lit);
  const zc=zoneAt(P.x,P.z)*18;
  glW.uniform3fv(UW.uCube,AMBCUBE.subarray(zc,zc+18));
  // the sampled dominant light direction, world -> view
  const d=VM.ldir;
  glW.uniform3f(UW.uLDir,
    d[0]*camR[0]+d[1]*camR[1]+d[2]*camR[2],
    d[0]*camU[0]+d[1]*camU[1]+d[2]*camU[2],
    -(d[0]*camF[0]+d[1]*camF[1]+d[2]*camF[2]));
  glW.uniformMatrix3fv(UW.uV2W,false,new Float32Array([
    camR[0],camR[1],camR[2], camU[0],camU[1],camU[2], -camF[0],-camF[1],-camF[2]]));
  glW.uniform1f(UW.uGain,A.tune.gain);
  glW.uniform1f(UW.uTime,elapsed);
  glW.uniform1f(UW.uFlashI,flashlight?0.9:0.0);
  glW.uniform3fv(UW.uAccent,lin(TP.accent));
  glW.uniform3fv(UW.uPaint,lin(TP.paint));
}
function drawMesh(mesh,M){
  glW.uniformMatrix4fv(UW.uModel,false,M);
  glW.uniformMatrix3fv(UW.uNM,false,m3of(M));
  glW.bindVertexArray(mesh.vao);
  glW.drawArrays(glW.TRIANGLES,0,mesh.count);
}

function drawVMW(w){
  const G=WMESH[w.wmesh];
  // gun matrix in view space: rest offset + bob + sway + recoil + jolt + idle
  const rw=A.over?0.10+0.06*Math.sin(elapsed*30.0):0;
  const bobx=Math.sin(stepPhase*2.6)*0.024*VM.bob;
  const boby=-Math.abs(Math.cos(stepPhase*2.6))*0.021*VM.bob;
  const t=elapsed, idle=A.idle;
  const iX =(Math.sin(t*0.91)*0.0055+Math.sin(t*1.73+1.2)*0.0021)*idle;
  const iY =(Math.sin(t*1.27+0.7)*0.0042+Math.sin(t*2.61)*0.0015)*idle;
  const iZ = Math.sin(t*0.63+2.1)*0.0030*idle;
  const iRX=(Math.sin(t*1.09+1.4)*0.0105+Math.sin(t*2.33)*0.0035)*idle;
  const iRY= Math.sin(t*0.77)*0.0130*idle;
  const iRZ= Math.sin(t*0.85+0.5)*0.0150*idle;
  const gunM=mMul2(mMul2(mMul2(
    mT(w.vm[0]+bobx+VM.swayX*0.05-0.03*rw-0.012*A.rlp+iX,
       w.vm[1]+boby+VM.swayY*0.05-0.085*rw+A.recoil*0.012-0.040*A.rlp+A.jolt*0.016+iY
              -(1-VM.draw)*0.66,
       w.vm[2]+A.recoil*w.vpush+0.04*rw+0.026*A.rlp+iZ),
    mRY(-0.055+0.10*A.rlp+VM.swayX*0.15+iRY)),
    mRX(0.02+A.recoil*0.055-0.50*rw-0.20*A.rlp+A.jolt*0.13+iRX)),
    mRZ(0.35*rw+0.20*A.rlp+iRZ));
  A.gunM=gunM;
  A.gunW=mMul2(camMat(),gunM);
  const spinM = w.kind===0 ? mMul2(gunM,mRZ(A.spin))
    : w.kind===2 ? mMul2(gunM,mT(0,0,A.slideZ))
    : mMul2(gunM,mMul2(mMul2(mT(0,-0.045,-0.015),mRX(-A.brk*0.78)),mT(0,0.045,0.015)));
  const magM = mMul2(gunM,mT(0,-A.magDrop,0));

  bindCommon();
  const proj=mPersp(1.19,canvas.width/canvas.height,0.012,30);
  glW.uniformMatrix4fv(UW.uProj,false,proj);
  const mx=w.twin?(A.shotSide?w.twin:-w.twin):0;
  const mq=xfp(gunM,[mx,0,w.muz]);
  glW.uniform3fv(UW.uMuzP,new Float32Array(mq));
  const mi=A.muzzle*(0.7+0.3*Math.random());
  glW.uniform3f(UW.uMuzC,26*mi*w.mfl*A.tune.gain,17*mi*w.mfl*A.tune.gain,7*mi*w.mfl*A.tune.gain);
  glW.uniform1i(UW.uAmmo,(A.over||reloadT>0)?-1:
    (w.mag>0?MAGS[w.id]:Math.min(999,AMMO[w.id])));
  glW.uniform1f(UW.uHeat,A.heat);
  glW.uniform1f(UW.uMuzI,0.0);

  glW.clear(glW.DEPTH_BUFFER_BIT);
  glW.disable(glW.CULL_FACE);
  drawMesh(G.stat,gunM);
  drawMesh(G.spin,spinM);
  if(G.magm.count)drawMesh(G.magm,magM);
  if(G.hands.count)drawMesh(G.hands,gunM);
  if(G.hmov.count)drawMesh(G.hmov,spinM);
  /* The flash keeps the depth test, so the barrel it leaves occludes it --
     drawn on top it read as a decal pasted over the gun. */
  if(A.muzzle>0.02){
    const f=A.muzzle*A.muzzle;
    const S=w.flash*A.tune.flash;
    const I=f*7.0*A.tune.gain*3.0;
    glW.enable(glW.BLEND); glW.blendFunc(glW.ONE,glW.ONE);
    glW.depthMask(false);
    const bd=rot3(gunM,[0,0,-1]);
    const card=(off,sz,roll)=>drawMesh(mCard,mMul2(mMul2(
      mT(mq[0]+bd[0]*off,mq[1]+bd[1]*off,mq[2]+bd[2]*off),
      mRZ(roll)),mS(sz,sz,sz)));
    // the burst: two seeds, a wide ragged one and a tight heart inside it.
    // Both face the camera, so there is no silhouette to recognise and no
    // edge to catch as the gun swings.  The barrel occludes them.
    glW.uniform1f(UW.uMuzG,0); glW.uniform1f(UW.uMuzI,I);
    glW.uniform1f(UW.uMuzS,A.flashSeed);
    card(S*0.55,S*(1.15+0.50*f),A.flashRoll);
    glW.uniform1f(UW.uMuzS,A.flashSeed+11.7);
    card(S*0.28,S*(0.52+0.22*f),A.flashRoll+2.2);
    // the bloom it throws, which does cross the gun
    glW.uniform1f(UW.uMuzG,1); glW.uniform1f(UW.uMuzI,I*0.30);
    glW.disable(glW.DEPTH_TEST);
    card(S*0.40,S*(1.9+0.7*f),0);
    glW.enable(glW.DEPTH_TEST);
    glW.depthMask(true); glW.disable(glW.BLEND);
  }
  glW.enable(glW.CULL_FACE);
  glW.bindVertexArray(null);
  glW.activeTexture(glW.TEXTURE0);
}

/* world-space casings, drawn in the world pass (before the viewmodel's
   depth clear) by composing view * world so the shader stays view-space.

   The call site sits between the dust/corona and the particles, which run
   additive (SRC_ALPHA,ONE) with depth writes off.  Brass is solid, so it
   takes the blend down and the depth mask back up for its own draw, then
   returns the state the particles after it expect. */
function drawShells(proj,view){
  if(!A.shells.length) return;
  glW.disable(glW.BLEND); glW.depthMask(true);
  bindCommon();
  glW.uniformMatrix4fv(UW.uProj,false,proj);
  glW.uniform3f(UW.uMuzP,0,0,-9);
  glW.uniform3f(UW.uMuzC,0,0,0);
  glW.uniform1i(UW.uAmmo,0);
  glW.uniform1f(UW.uHeat,0);
  glW.uniform1f(UW.uMuzI,0);
  glW.disable(glW.CULL_FACE);
  for(const c of A.shells){
    const M=mMul2(view,
      mMul2(mMul2(mMul2(mT(c.p[0],c.p[1],c.p[2]),mRY(c.r[1])),mRX(c.r[0])),mRZ(c.r[2])));
    drawMesh(c.s?mShot:mShell,M);
  }
  glW.enable(glW.CULL_FACE);
  glW.bindVertexArray(null);
  glW.activeTexture(glW.TEXTURE0);
  glW.depthMask(false); glW.enable(glW.BLEND);
  glW.blendFunc(glW.SRC_ALPHA,glW.ONE);
}

return { A, fire:fireVMW, step:stepVMW, draw:drawVMW, drawShells,
  generateTextures, TP,
  get over(){return A.over;}, get heat(){return A.heat;},
  get spinV(){return A.spinV;}, set dryT(v){A.dryT=v;}, get dryT(){return A.dryT;},
  set spent(v){A.spent=v;}, get spent(){return A.spent;},
  previz(){ A.previzOn=true; HAVE.shotgun=true; HAVE.smg=true;
    AMMO.glock=999; AMMO.shotgun=999; AMMO.smg=999;
    MAGS.glock=WEP[1].mag; MAGS.shotgun=WEP[2].mag;
    wantW=1; } };
})();
window.VMW=VMW;

/* ---- engine overrides: these shadow the earlier declarations -------- */

function drawViewmodel(){
  const w=WEP[curW];
  if(w.wmesh===undefined){ drawViewmodelBoxes(); return; }
  VMW.draw(w);
}

function tryFire(){
  const w=WEP[curW];
  if(fireT>0||reloadT>0||VM.draw<0.6||gameOver) return;
  if(w.melee){
    fireT=w.rate; VM.kickV+=w.kick*0.5; VM.rotV+=0.5;
    fireMelee(w);
    return;
  }
  if(w.thrown){
    if(AMMO.grenade<=0){ sfxClick(160,0.09,0.08,'square'); return; }
    AMMO.grenade--; fireT=w.rate; VM.kickV+=1.2; throwGrenade();
    return;
  }
  if(VMW.over) return;                       // the trigger just clunks; VMW.step does it
  if(w.spinUp&&VMW.spinV<26) return;         // barrels still spinning up
  if(w.mag>0){
    if(MAGS[w.id]<=0){
      sfxClick(150,0.10,0.07,'square'); fireT=0.22;
      if(AMMO[w.id]>0) VMW.dryT=Math.max(VMW.dryT,0.05);
      else hev('OUT OF AMMUNITION');
      return;
    }
    MAGS[w.id]--;
    if(MAGS[w.id]<=0&&AMMO[w.id]>0) VMW.dryT=w.reldly||0.15;
  } else {
    if(AMMO[w.id]<=0){ sfxClick(150,0.10,0.07,'square'); fireT=0.22;
      hev('OUT OF AMMUNITION'); return; }
    AMMO[w.id]--;
  }
  fireT=w.rate; stats.shots++;
  sfxGun(w.id==='shotgun'?'shot':w.id==='smg'?'smg':'glock');
  VMW.fire(w);                               // recoil, heat, light, casings, FX
  shake(w.shake);
  if(fireHitscan(w)) stats.hits++;
}

function updateWeapons(dt,rdt){
  if(wantW!==curW&&VM.draw<=0.02){ curW=wantW; }
  const targetDraw=(wantW===curW)?1:0;
  VM.draw+=(targetDraw-VM.draw)*Math.min(1,rdt*11);
  if(VM.draw<0.02&&wantW!==curW) curW=wantW;

  fireT=Math.max(0,fireT-rdt);
  if(reloadT>0){ reloadT-=rdt; if(reloadT<=0) finishReload(); }
  if(VM.shellT>0){ VM.shellT-=rdt; if(VM.shellT<=0){ VM.shellT=0; ejectShell(); } }
  VM.pump=Math.max(0,VM.pump-rdt*3.4);
  VM.flash=Math.max(0,VM.flash-rdt*15);

  const w=WEP[curW];
  if(holdFire&&w.auto) tryFire();

  // spring the viewmodel back
  VM.kickV-=VM.kick*90*rdt; VM.kickV*=Math.max(0,1-rdt*13); VM.kick+=VM.kickV*rdt;
  VM.rotV-=VM.rot*80*rdt;  VM.rotV*=Math.max(0,1-rdt*12);  VM.rot+=VM.rotV*rdt;
  VM.swayX+=(VM.tgtX-VM.swayX)*Math.min(1,rdt*9); VM.tgtX*=Math.max(0,1-rdt*7);
  VM.swayY+=(VM.tgtY-VM.swayY)*Math.min(1,rdt*9); VM.tgtY*=Math.max(0,1-rdt*7);
  VM.bob+=((moveSpeedNow>0.4&&P.grounded?1:0)-VM.bob)*Math.min(1,rdt*7);
  if(!gameOver) sampleLightThrottled(rdt);

  VMW.step(w,dt,rdt);
}
