/* ============================================================
   lumi_geometry.js — LUMI 형상 생성기 (동결본)

   의존성 없음. three.js도, 브라우저 API도 쓰지 않는다.
   buildLumi()가 파트 배열을 돌려주고, 각 파트는 그대로
   GPU에 올릴 수 있는 정점 버퍼다.

   ★ 이 파일의 숫자가 곧 디자인이다. 값이 바뀌면 다른 로봇이 된다.

   출력 형식:
     [{ name, material, positions:Float32Array, normals:Float32Array,
        uvs:Float32Array, indices:Uint16Array }, ...]

   좌표계: Y+ 위, Z+ 정면(바이저 방향), 오른손 좌표계. 단위 미터.
   모든 변환은 빌드 시점에 정점에 구워져 있다 — 런타임 트랜스폼 불필요.
   ============================================================ */

(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.LumiGeometry = factory();
})(typeof self !== "undefined" ? self : this, function () {
"use strict";

/* ============================================================
   PARAMS — 디자인 확정값
   ============================================================ */
var P = {
  R: 0.222,            /* 구체 반경 */
  YS: 0.97,            /* 세로 눌림 */
  BAND_INSET: 0.0085,  /* 다크 밴드가 들어간 깊이 */
  A1: 0.44, A2: 0.55,  /* 상단 밴드 극각 (rad, 0=정수리) */
  A3: 2.02, A4: 2.13,  /* 하단 밴드 극각 */
  A5: 2.34,            /* 플랜지 시작 */
  SEG: 64,             /* 회전체 분할 */

  VISOR: { tilt: -0.26, a: 0.134, b: 0.074, expo: 3.2,
           rBezel: 0.2268, rRim: 0.2242, rScreen: 0.2236 },

  DECAL: { rSurface: 0.2230,
           front: { tiltX: 0.258, a: 0.080, b: 0.036 },
           back:  { tiltX: -0.08, a: 0.082, b: 0.041 },
           side:  { yaw: 0.72,    a: 0.040, b: 0.040 } },

  POD: { x: 0.158, y: 0.010, z: 0.004,
         outerW: 0.116, outerH: 0.150, outerR: 0.055,
         innerW: 0.078, innerH: 0.110, innerR: 0.038,
         depth: 0.056, bevel: 0.005,
         capR0: 0.044, capR1: 0.046, capLen: 0.032, capZ: 0.042,
         ringR: 0.046, ringTube: 0.005, ringZ: 0.056,
         holeR: 0.0095, holeRing: 0.021, holeZ: 0.060, holePhase: 0.78,
         slit: [0.004, 0.032, 0.003], slitPos: [0, 0.064, 0.032] },

  LED_TOP: { size: [0.020, 0.010, 0.006], dir: [0, 0.66, 0.75], r: 0.2245 },
  LED_DOT: { r: 0.010, dir: [0.44, -0.44, 0.78], rad: 0.2238 },
  LED_BACK: { size: [0.014, 0.009, 0.005], count: 5,
              x0: -0.22, dx: 0.11, dy: -0.20, dz: -0.95, rad: 0.2248 },
  SLOT: { size: [0.090, 0.028, 0.012], dir: [0, -0.44, -0.90], rad: 0.222 },
  GRILL: { size: [0.076, 0.004, 0.004], scale: 1.02 },

  ANTENNA: { r0: 0.0035, r1: 0.0055, len: 0.092,
             pos: [0, 0.252, -0.020], tiltX: 0.16,
             tipR: 0.006, tipPos: [0, 0.297, -0.027] },

  EMITTER: { ringR: 0.104, ringTube: 0.008, ringDY: 0.004,
             discR: 0.132, discDY: -0.002 },

  BEAM: { height: 0.085,
          layers: [ { rTop: 0.086, rBot: 0.118, opacity: 0.16, fade: 2.4 },
                    { rTop: 0.100, rBot: 0.150, opacity: 0.11, fade: 1.8 } ] }
};

/* ============================================================
   수학 유틸
   ============================================================ */
function sgn(x) { return x < 0 ? -1 : 1; }
function lerp(a, b, t) { return a + (b - a) * t; }

/* 구체 프로파일 위의 한 점. a=0 정수리, a=PI 바닥. */
function spherePt(a, inset) {
  return [P.R * Math.sin(a) - (inset || 0), P.R * P.YS * Math.cos(a)];
}
var EMIT_Y = spherePt(P.A5)[1] - 0.055;

/* 메시 누산기 */
function Mesh() { this.pos = []; this.nrm = []; this.uv = []; this.idx = []; }
Mesh.prototype.vert = function (x, y, z, nx, ny, nz, u, v) {
  this.pos.push(x, y, z);
  this.nrm.push(nx, ny, nz);
  this.uv.push(u, v);
  return this.pos.length / 3 - 1;
};
Mesh.prototype.tri = function (a, b, c) { this.idx.push(a, b, c); };
Mesh.prototype.quad = function (a, b, c, d) { this.idx.push(a, b, c, a, c, d); };
Mesh.prototype.recomputeNormals = function () {
  var n = this.pos.length / 3, i;
  for (i = 0; i < this.nrm.length; i++) this.nrm[i] = 0;
  for (i = 0; i + 2 < this.idx.length; i += 3) {
    var a = this.idx[i] * 3, b = this.idx[i + 1] * 3, c = this.idx[i + 2] * 3;
    var ux = this.pos[b] - this.pos[a], uy = this.pos[b+1] - this.pos[a+1], uz = this.pos[b+2] - this.pos[a+2];
    var vx = this.pos[c] - this.pos[a], vy = this.pos[c+1] - this.pos[a+1], vz = this.pos[c+2] - this.pos[a+2];
    var nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    this.nrm[a] += nx; this.nrm[a+1] += ny; this.nrm[a+2] += nz;
    this.nrm[b] += nx; this.nrm[b+1] += ny; this.nrm[b+2] += nz;
    this.nrm[c] += nx; this.nrm[c+1] += ny; this.nrm[c+2] += nz;
  }
  for (i = 0; i < n; i++) {
    var k = i * 3, l = Math.hypot(this.nrm[k], this.nrm[k+1], this.nrm[k+2]);
    if (l > 1e-9) { this.nrm[k] /= l; this.nrm[k+1] /= l; this.nrm[k+2] /= l; }
  }
  return this;
};

/* 변환을 정점에 굽는다 (런타임 트랜스폼을 없애기 위해) */
function rotX(t) { var c=Math.cos(t), s=Math.sin(t);
  return [1,0,0, 0,c,-s, 0,s,c]; }
function rotY(t) { var c=Math.cos(t), s=Math.sin(t);
  return [c,0,s, 0,1,0, -s,0,c]; }
function rotZ(t) { var c=Math.cos(t), s=Math.sin(t);
  return [c,-s,0, s,c,0, 0,0,1]; }
function mul3(A, B) {
  var C = new Array(9);
  for (var r = 0; r < 3; r++) for (var c = 0; c < 3; c++) {
    C[r*3+c] = A[r*3]*B[c] + A[r*3+1]*B[3+c] + A[r*3+2]*B[6+c];
  }
  return C;
}
/* +Z를 dir 방향으로 향하게 하는 기저 */
function basisTowards(dir) {
  var l = Math.hypot(dir[0], dir[1], dir[2]);
  var z = [dir[0]/l, dir[1]/l, dir[2]/l];
  var up = Math.abs(z[1]) > 0.999 ? [0,0,1] : [0,1,0];
  var x = [ up[1]*z[2]-up[2]*z[1], up[2]*z[0]-up[0]*z[2], up[0]*z[1]-up[1]*z[0] ];
  var lx = Math.hypot(x[0], x[1], x[2]); x = [x[0]/lx, x[1]/lx, x[2]/lx];
  var y = [ z[1]*x[2]-z[2]*x[1], z[2]*x[0]-z[0]*x[2], z[0]*x[1]-z[1]*x[0] ];
  return [ x[0],y[0],z[0], x[1],y[1],z[1], x[2],y[2],z[2] ];
}
function bake(mesh, M, T) {
  var i, n = mesh.pos.length / 3;
  for (i = 0; i < n; i++) {
    var k = i * 3, x = mesh.pos[k], y = mesh.pos[k+1], z = mesh.pos[k+2];
    if (M) {
      mesh.pos[k]   = M[0]*x + M[1]*y + M[2]*z;
      mesh.pos[k+1] = M[3]*x + M[4]*y + M[5]*z;
      mesh.pos[k+2] = M[6]*x + M[7]*y + M[8]*z;
      var a = mesh.nrm[k], b = mesh.nrm[k+1], c = mesh.nrm[k+2];
      mesh.nrm[k]   = M[0]*a + M[1]*b + M[2]*c;
      mesh.nrm[k+1] = M[3]*a + M[4]*b + M[5]*c;
      mesh.nrm[k+2] = M[6]*a + M[7]*b + M[8]*c;
    }
    if (T) { mesh.pos[k] += T[0]; mesh.pos[k+1] += T[1]; mesh.pos[k+2] += T[2]; }
  }
  return mesh;
}

/* ============================================================
   프리미티브
   ============================================================ */

/* 회전체. profile = [[r, y, hard], ...]. hard=1이면 정점을 복제해
   노멀을 끊는다 — 이게 하드서페이스 에지를 만든다. */
function lathe(profile, seg, v0, v1) {
  var rings = [], i, j;
  for (i = 0; i < profile.length; i++) {
    rings.push(profile[i]);
    if (profile[i][2] && i > 0 && i < profile.length - 1) rings.push(profile[i]);
  }
  var m = new Mesh(), acc = [0], tot = 0;
  for (i = 1; i < rings.length; i++) {
    tot += Math.hypot(rings[i][0] - rings[i-1][0], rings[i][1] - rings[i-1][1]);
    acc.push(tot);
  }
  for (i = 0; i < rings.length; i++) {
    var r = rings[i][0], y = rings[i][1];
    for (j = 0; j <= seg; j++) {
      var u = j / seg, t = 2 * Math.PI * (u - 0.5);
      m.vert(r * Math.sin(t), y, r * Math.cos(t), 0, 0, 0,
             u, lerp(v0 === undefined ? 0 : v0, v1 === undefined ? 1 : v1,
                     tot > 0 ? acc[i] / tot : 0));
    }
  }
  var row = seg + 1;
  for (i = 0; i < rings.length - 1; i++) {
    if (rings[i][0] === rings[i+1][0] && rings[i][1] === rings[i+1][1]) continue;
    for (j = 0; j < seg; j++) {
      var a = i * row + j;
      m.quad(a, a + row, a + row + 1, a + 1);
    }
  }
  return m.recomputeNormals();
}

/* 슈퍼타원을 구면에 투영한 패치. 바이저와 데칼이 곡률을 따라가게 한다.
   f0..f1 은 반지름 방향 범위 (0=중심, 1=외곽, >1이면 링). */
function spherePatch(Rp, A, B, expo, aSeg, rSeg, f0, f1) {
  var m = new Mesh(), i, j;
  for (j = 0; j <= rSeg; j++) {
    var f = lerp(f0, f1, j / rSeg);
    for (i = 0; i <= aSeg; i++) {
      var th = 2 * Math.PI * i / aSeg, c = Math.cos(th), s = Math.sin(th);
      var px = A * sgn(c) * Math.pow(Math.abs(c), 2 / expo) * f;
      var py = B * sgn(s) * Math.pow(Math.abs(s), 2 / expo) * f;
      var l = Math.hypot(px, py, Rp), k = Rp / l;
      m.vert(px * k, py * k, Rp * k, 0, 0, 0,
             0.5 + px / (2 * A), 0.5 + py / (2 * B));
    }
  }
  var row = aSeg + 1;
  for (j = 0; j < rSeg; j++) for (i = 0; i < aSeg; i++) {
    var a = j * row + i;
    m.quad(a, a + row, a + row + 1, a + 1);
  }
  return m.recomputeNormals();
}

/* 하드 에지 박스 (면마다 정점 분리) */
function box(w, h, d) {
  var m = new Mesh(), x = w/2, y = h/2, z = d/2;
  var faces = [
    [[ x,-y,-z],[ x,-y, z],[ x, y, z],[ x, y,-z], [ 1, 0, 0]],
    [[-x,-y, z],[-x,-y,-z],[-x, y,-z],[-x, y, z], [-1, 0, 0]],
    [[-x, y,-z],[ x, y,-z],[ x, y, z],[-x, y, z], [ 0, 1, 0]],
    [[-x,-y, z],[ x,-y, z],[ x,-y,-z],[-x,-y,-z], [ 0,-1, 0]],
    [[-x,-y, z],[-x, y, z],[ x, y, z],[ x,-y, z], [ 0, 0, 1]],
    [[ x,-y,-z],[ x, y,-z],[-x, y,-z],[-x,-y,-z], [ 0, 0,-1]]
  ];
  var uvs = [[0,0],[1,0],[1,1],[0,1]];
  faces.forEach(function (f) {
    var n = f[4], base = m.pos.length / 3;
    for (var k = 0; k < 4; k++)
      m.vert(f[k][0], f[k][1], f[k][2], n[0], n[1], n[2], uvs[k][0], uvs[k][1]);
    m.quad(base, base + 1, base + 2, base + 3);
  });
  return m;
}

/* Y축 원통 (radiusTop, radiusBottom). open=true면 뚜껑 없음. */
function cylinder(rTop, rBot, h, seg, open) {
  var m = new Mesh(), i;
  var slope = (rBot - rTop) / h;
  for (var ring = 0; ring < 2; ring++) {
    var r = ring ? rBot : rTop, y = ring ? -h/2 : h/2;
    for (i = 0; i <= seg; i++) {
      var t = 2 * Math.PI * i / seg, cx = Math.cos(t), cz = Math.sin(t);
      var nl = Math.hypot(1, slope);
      m.vert(r*cx, y, r*cz, cx/nl, slope/nl, cz/nl, i/seg, 1 - ring);
    }
  }
  var row = seg + 1;
  for (i = 0; i < seg; i++) m.quad(i, i + row, i + row + 1, i + 1);
  if (!open) {
    [[rTop, h/2, 1], [rBot, -h/2, -1]].forEach(function (cap) {
      if (cap[0] <= 1e-6) return;
      var base = m.pos.length / 3;
      var c = m.vert(0, cap[1], 0, 0, cap[2], 0, 0.5, 0.5);
      for (i = 0; i <= seg; i++) {
        var t = 2 * Math.PI * i / seg;
        m.vert(cap[0]*Math.cos(t), cap[1], cap[0]*Math.sin(t), 0, cap[2], 0,
               0.5 + 0.5*Math.cos(t), 0.5 + 0.5*Math.sin(t));
      }
      for (i = 0; i < seg; i++) {
        if (cap[2] > 0) m.tri(c, base + 1 + i, base + 2 + i);
        else            m.tri(c, base + 2 + i, base + 1 + i);
      }
    });
  }
  return m;
}

/* XY 평면 원판, 법선 +Z */
function circle(r, seg) {
  var m = new Mesh(), i;
  var c = m.vert(0, 0, 0, 0, 0, 1, 0.5, 0.5);
  for (i = 0; i <= seg; i++) {
    var t = 2 * Math.PI * i / seg;
    m.vert(r*Math.cos(t), r*Math.sin(t), 0, 0, 0, 1,
           0.5 + 0.5*Math.cos(t), 0.5 + 0.5*Math.sin(t));
  }
  for (i = 0; i < seg; i++) m.tri(c, 1 + i, 2 + i);
  return m;
}

/* XY 평면 토러스 (축 = Z) */
function torus(R0, tube, radSeg, tubSeg) {
  var m = new Mesh(), i, j;
  for (i = 0; i <= radSeg; i++) {
    var u = 2 * Math.PI * i / radSeg, cu = Math.cos(u), su = Math.sin(u);
    for (j = 0; j <= tubSeg; j++) {
      var v = 2 * Math.PI * j / tubSeg, cvv = Math.cos(v), sv = Math.sin(v);
      var r = R0 + tube * cvv;
      m.vert(r*cu, r*su, tube*sv, cvv*cu, cvv*su, sv, i/radSeg, j/tubSeg);
    }
  }
  var row = tubSeg + 1;
  for (i = 0; i < radSeg; i++) for (j = 0; j < tubSeg; j++) {
    var a = i * row + j;
    m.quad(a, a + row, a + row + 1, a + 1);
  }
  return m;
}

function sphere(r, segU, segV) {
  var m = new Mesh(), i, j;
  for (j = 0; j <= segV; j++) {
    var a = Math.PI * j / segV, sa = Math.sin(a), ca = Math.cos(a);
    for (i = 0; i <= segU; i++) {
      var t = 2 * Math.PI * i / segU;
      var nx = sa*Math.sin(t), ny = ca, nz = sa*Math.cos(t);
      m.vert(r*nx, r*ny, r*nz, nx, ny, nz, i/segU, 1 - j/segV);
    }
  }
  var row = segU + 1;
  for (j = 0; j < segV; j++) for (i = 0; i < segU; i++) {
    var a2 = j * row + i;
    m.quad(a2, a2 + row, a2 + row + 1, a2 + 1);
  }
  return m;
}

/* 라운드 사각 경로를 따라 닫힌 단면을 스윕한다 (측면 포드 후프).
   path: 중심선, section: [[offset, z, hard], ...] 닫힌 루프 */
function roundRectPath(w, h, r, seg) {
  var pts = [], x = w/2 - r, y = h/2 - r, i;
  var corners = [[ x, y, 0], [-x, y, Math.PI/2], [-x,-y, Math.PI], [ x,-y, Math.PI*1.5]];
  var quarter = Math.max(2, Math.round(seg / 4));
  for (var c = 0; c < 4; c++) {
    var cx = corners[c][0], cy = corners[c][1], a0 = corners[c][2];
    for (i = 0; i < quarter; i++) {
      var a = a0 + Math.PI/2 * i / quarter;
      pts.push([cx + r*Math.cos(a), cy + r*Math.sin(a)]);
    }
  }
  return pts;
}
function sweepClosed(path, section) {
  var m = new Mesh(), n = path.length, s = section.length, i, j;
  var ring = [];
  for (i = 0; i < n; i++) {
    var p = path[i], pn = path[(i+1) % n], pp = path[(i-1+n) % n];
    var tx = pn[0]-pp[0], ty = pn[1]-pp[1], tl = Math.hypot(tx,ty) || 1;
    var ox = -(-ty/tl), oy = -(tx/tl);           /* 바깥 법선 */
    var out = Math.hypot(ox,oy) || 1; ox/=out; oy/=out;
    /* 중심에서 바깥으로 향하도록 부호 보정 */
    if (p[0]*ox + p[1]*oy < 0) { ox = -ox; oy = -oy; }
    ring.push([p, [ox, oy]]);
  }
  for (i = 0; i < n; i++) {
    var pr = ring[i][0], nr = ring[i][1];
    for (j = 0; j < s; j++) {
      var off = section[j][0], z = section[j][1];
      m.vert(pr[0] + nr[0]*off, pr[1] + nr[1]*off, z, 0, 0, 0, i/n, j/s);
    }
  }
  for (i = 0; i < n; i++) for (j = 0; j < s; j++) {
    var a = i*s + j, b = ((i+1)%n)*s + j;
    var a2 = i*s + (j+1)%s, b2 = ((i+1)%n)*s + (j+1)%s;
    m.quad(a, b, b2, a2);
  }
  return m.recomputeNormals();
}

/* 확산층 원통 — 정점 컬러에 세로 페이드를 굽는다 */
function beamLayer(rTop, rBot, h, seg, fadePow) {
  var m = cylinder(rTop, rBot, h, seg, true);
  var n = m.pos.length / 3, col = new Float32Array(n * 3);
  for (var i = 0; i < n; i++) {
    var y = m.pos[i*3+1];
    var t = (y + h/2) / h;
    var f = Math.pow(t < 0 ? 0 : t > 1 ? 1 : t, fadePow);
    col[i*3] = col[i*3+1] = col[i*3+2] = f;
  }
  m.colors = col;
  return m;
}

/* ============================================================
   존 프로파일
   ============================================================ */
function arc(a0, a1, n, inset) {
  var p = [];
  for (var i = 0; i <= n; i++) {
    var a = a0 + (a1 - a0) * i / n, q = spherePt(a, inset);
    p.push([q[0], q[1], 0]);
  }
  return p;
}
function buildZones() {
  var Z = [], D = P.BAND_INSET;

  var cap = arc(0, P.A1, 12, 0);
  cap.push([spherePt(P.A1)[0], spherePt(P.A1)[1], 1],
           [spherePt(P.A1, D)[0], spherePt(P.A1)[1], 1]);
  Z.push({ name: "shell_cap", material: "paint", p: cap });

  var b1 = arc(P.A1, P.A2, 4, D);
  b1[0][2] = 1; b1[b1.length-1][2] = 1;
  b1.push([spherePt(P.A2)[0], spherePt(P.A2)[1], 1]);
  Z.push({ name: "band_upper", material: "band", p: b1 });

  var sh = arc(P.A2, P.A3, 26, 0);
  sh[0][2] = 1;
  sh.push([spherePt(P.A3)[0], spherePt(P.A3)[1], 1],
          [spherePt(P.A3, D)[0], spherePt(P.A3)[1], 1]);
  Z.push({ name: "shell_main", material: "paint", p: sh });

  var b2 = arc(P.A3, P.A4, 4, D);
  b2[0][2] = 1; b2[b2.length-1][2] = 1;
  b2.push([spherePt(P.A4)[0], spherePt(P.A4)[1], 1]);
  Z.push({ name: "band_lower", material: "band", p: b2 });

  var lo = arc(P.A4, P.A5, 8, 0);
  lo[0][2] = 1;
  Z.push({ name: "shell_lower", material: "paint", p: lo });

  var e = spherePt(P.A5);
  Z.push({ name: "flange", material: "steel", p: [
    [e[0], e[1], 1], [e[0]+0.007, e[1]-0.008, 1],
    [e[0]+0.003, e[1]-0.020, 1], [0.136, e[1]-0.027, 1] ] });

  Z.push({ name: "emitter_housing", material: "dark", p: [
    [0.136, e[1]-0.027, 1], [0.131, e[1]-0.038, 1],
    [0.118, e[1]-0.045, 1], [0.118, e[1]-0.055, 1], [0, e[1]-0.057, 0] ] });

  return Z;
}

/* ============================================================
   조립
   ============================================================ */
function pack(name, material, mesh) {
  var out = {
    name: name, material: material,
    positions: new Float32Array(mesh.pos),
    normals:   new Float32Array(mesh.nrm),
    uvs:       new Float32Array(mesh.uv),
    indices:   (mesh.pos.length / 3 > 65535 ? new Uint32Array(mesh.idx)
                                            : new Uint16Array(mesh.idx))
  };
  if (mesh.colors) out.colors = mesh.colors;
  return out;
}

function buildLumi() {
  var parts = [], V = P.VISOR, D = P.DECAL, POD = P.POD;

  /* --- 몸통 존. v 좌표를 전체 길이에 대해 연속으로 배분한다. --- */
  var Z = buildZones();
  var lens = Z.map(function (z) {
    var t = 0;
    for (var i = 1; i < z.p.length; i++)
      t += Math.hypot(z.p[i][0]-z.p[i-1][0], z.p[i][1]-z.p[i-1][1]);
    return t;
  });
  var tot = lens.reduce(function (a, b) { return a + b; }, 0), acc = 0;
  Z.forEach(function (z, i) {
    var v0 = acc / tot; acc += lens[i];
    parts.push(pack(z.name, z.material, lathe(z.p, P.SEG, v0, acc / tot)));
  });

  /* --- 바이저 --- */
  var MV = rotX(V.tilt);
  parts.push(pack("visor_bezel", "panel",
    bake(spherePatch(V.rBezel, V.a, V.b, V.expo, 72, 3, 1.00, 1.19), MV)));
  parts.push(pack("visor_rim", "dark",
    bake(spherePatch(V.rRim, V.a, V.b, V.expo, 72, 2, 0.99, 1.02), MV)));
  parts.push(pack("visor_screen", "screen",
    bake(spherePatch(V.rScreen, V.a, V.b, V.expo, 64, 8, 0.002, 1.0), MV)));

  /* --- 데칼 --- */
  parts.push(pack("decal_front", "decal_front",
    bake(spherePatch(D.rSurface, D.front.a, D.front.b, 2.0, 40, 4, 0.002, 1.0),
         rotX(D.front.tiltX))));
  parts.push(pack("decal_back", "decal_back",
    bake(spherePatch(D.rSurface, D.back.a, D.back.b, 2.0, 40, 4, 0.002, 1.0),
         mul3(rotY(Math.PI), rotX(D.back.tiltX)))));
  [-1, 1].forEach(function (s) {
    parts.push(pack(s < 0 ? "decal_side_l" : "decal_side_r", "decal_greeble",
      bake(spherePatch(D.rSurface, D.side.a, D.side.b, 2.0, 24, 4, 0.002, 1.0),
           rotY(s * D.side.yaw))));
  });

  /* --- 정면 LED --- */
  var lt = P.LED_TOP, dl = Math.hypot(lt.dir[0], lt.dir[1], lt.dir[2]);
  parts.push(pack("led_top", "emissive_cyan",
    bake(box(lt.size[0], lt.size[1], lt.size[2]), basisTowards(lt.dir),
         [lt.dir[0]/dl*lt.r, lt.dir[1]/dl*lt.r, lt.dir[2]/dl*lt.r])));
  var ld = P.LED_DOT, dd = Math.hypot(ld.dir[0], ld.dir[1], ld.dir[2]);
  parts.push(pack("led_dot", "emissive_cyan",
    bake(circle(ld.r, 16), basisTowards(ld.dir),
         [ld.dir[0]/dd*ld.rad, ld.dir[1]/dd*ld.rad, ld.dir[2]/dd*ld.rad])));

  /* --- 측면 포드 --- */
  [-1, 1].forEach(function (s) {
    var tag = s < 0 ? "_l" : "_r";
    var M = rotY(s * Math.PI / 2), T = [s * POD.x, POD.y, POD.z];

    /* 후프: 라운드 사각 중심선을 따라 베벨 단면을 스윕 */
    var cw = (POD.outerW + POD.innerW) / 2, ch = (POD.outerH + POD.innerH) / 2;
    var cr = (POD.outerR + POD.innerR) / 2;
    var half = (POD.outerW - POD.innerW) / 4 + (POD.outerH - POD.innerH) / 4;
    var bv = POD.bevel, dz = POD.depth;
    var section = [
      [-half, bv, 1], [-half, dz - bv, 1], [-half + bv, dz, 1],
      [ half - bv, dz, 1], [ half, dz - bv, 1], [ half, bv, 1],
      [ half - bv, 0, 1], [-half + bv, 0, 1]
    ];
    parts.push(pack("pod_hoop" + tag, "panel",
      bake(sweepClosed(roundRectPath(cw, ch, cr, 48), section), M, T)));

    var cap = cylinder(POD.capR0, POD.capR1, POD.capLen, 22, false);
    bake(cap, rotX(Math.PI / 2), [0, 0, POD.capZ]);
    parts.push(pack("pod_cap" + tag, "dark", bake(cap, M, T)));

    var rg = torus(POD.ringR, POD.ringTube, 26, 6);
    bake(rg, null, [0, 0, POD.ringZ]);
    parts.push(pack("pod_ring" + tag, "steel", bake(rg, M, T)));

    var holes = new Mesh();
    var mkHole = function (hx, hy) {
      var h = circle(POD.holeR, 12);
      bake(h, null, [hx, hy, POD.holeZ]);
      var base = holes.pos.length / 3;
      for (var i = 0; i < h.pos.length; i++) holes.pos.push(h.pos[i]);
      for (i = 0; i < h.nrm.length; i++) holes.nrm.push(h.nrm[i]);
      for (i = 0; i < h.uv.length; i++) holes.uv.push(h.uv[i]);
      for (i = 0; i < h.idx.length; i++) holes.idx.push(h.idx[i] + base);
    };
    for (var k = 0; k < 4; k++) {
      var a = k / 4 * Math.PI * 2 + POD.holePhase;
      mkHole(POD.holeRing * Math.cos(a), POD.holeRing * Math.sin(a));
    }
    mkHole(0, 0);
    parts.push(pack("pod_holes" + tag, "dark", bake(holes, M, T)));

    var slit = box(POD.slit[0], POD.slit[1], POD.slit[2]);
    bake(slit, null, POD.slitPos);
    parts.push(pack("pod_slit" + tag, "emissive_dim", bake(slit, M, T)));
  });

  /* --- 후면 --- */
  var LB = P.LED_BACK, backLeds = new Mesh();
  for (var k2 = 0; k2 < LB.count; k2++) {
    var d = [LB.x0 + k2 * LB.dx, LB.dy, LB.dz];
    var dn = Math.hypot(d[0], d[1], d[2]);
    var b = box(LB.size[0], LB.size[1], LB.size[2]);
    bake(b, basisTowards(d), [d[0]/dn*LB.rad, d[1]/dn*LB.rad, d[2]/dn*LB.rad]);
    var base2 = backLeds.pos.length / 3, i2;
    for (i2 = 0; i2 < b.pos.length; i2++) backLeds.pos.push(b.pos[i2]);
    for (i2 = 0; i2 < b.nrm.length; i2++) backLeds.nrm.push(b.nrm[i2]);
    for (i2 = 0; i2 < b.uv.length;  i2++) backLeds.uv.push(b.uv[i2]);
    for (i2 = 0; i2 < b.idx.length; i2++) backLeds.idx.push(b.idx[i2] + base2);
  }
  parts.push(pack("back_leds", "emissive_dim", backLeds));

  var SL = P.SLOT, sn = Math.hypot(SL.dir[0], SL.dir[1], SL.dir[2]);
  var sPos = [SL.dir[0]/sn*SL.rad, SL.dir[1]/sn*SL.rad, SL.dir[2]/sn*SL.rad];
  parts.push(pack("back_slot", "dark",
    bake(box(SL.size[0], SL.size[1], SL.size[2]), basisTowards(SL.dir), sPos)));
  parts.push(pack("back_grill", "panel",
    bake(box(P.GRILL.size[0], P.GRILL.size[1], P.GRILL.size[2]),
         basisTowards(SL.dir),
         [sPos[0]*P.GRILL.scale, sPos[1]*P.GRILL.scale, sPos[2]*P.GRILL.scale])));

  /* --- 안테나 --- */
  var AN = P.ANTENNA;
  parts.push(pack("antenna", "panel",
    bake(cylinder(AN.r0, AN.r1, AN.len, 10, false), rotX(AN.tiltX), AN.pos)));
  parts.push(pack("antenna_tip", "steel",
    bake(sphere(AN.tipR, 10, 8), null, AN.tipPos)));

  /* --- 부양 이미터 --- */
  var EM = P.EMITTER;
  parts.push(pack("emit_ring", "emissive_cyan",
    bake(torus(EM.ringR, EM.ringTube, 44, 8), rotX(Math.PI / 2),
         [0, EMIT_Y + EM.ringDY, 0])));
  parts.push(pack("emit_disc", "glow_radial",
    bake(circle(EM.discR, 44), rotX(Math.PI / 2), [0, EMIT_Y + EM.discDY, 0])));

  P.BEAM.layers.forEach(function (L, i) {
    var m = beamLayer(L.rTop, L.rBot, P.BEAM.height, 36, L.fade);
    bake(m, null, [0, EMIT_Y - P.BEAM.height / 2, 0]);
    var p = pack("beam_" + i, "glow_beam", m);
    p.opacity = L.opacity;
    parts.push(p);
  });

  return parts;
}

return {
  PARAMS: P,
  EMIT_Y: EMIT_Y,
  buildLumi: buildLumi,
  /* 다른 형상을 만들지 않았는지 확인하기 위한 체크섬 */
  checksum: function (parts) {
    var v = 0, t = 0;
    parts.forEach(function (p) { v += p.positions.length / 3; t += p.indices.length / 3; });
    return { parts: parts.length, vertices: v, triangles: t };
  }
};
});
