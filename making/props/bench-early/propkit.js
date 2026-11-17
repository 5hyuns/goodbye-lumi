/* ============================================================
   PROP KIT -- freestanding hero props for the derelict ship.

   Deliberately its OWN small system, not a graft onto tools/kit.js.
   That file's operator VM is load-bearing for the corridor build and
   took many sessions of bug-fixing to get right (see project memory:
   right-handed span bases, per-instance UV rules). A prop is a
   standalone object seen from every angle on a turntable, not a brush
   face continuing a wall's texture, so it earns a simpler model:

     * TRUE SCALE, metres, standing on y=0, origin at the mounting point
       -- same rule as the corridor kit.
     * NO RIGHT ANGLE IS LEFT OPEN -- the corridor's own standing rule,
       extended to props. Every major mass gets real 45-degree geometry:
       chamfered vertical edges (octBox), chamfered crowns (topCut,
       lidded hull8 lofts), sloped decks and hoods. The first prop pass
       put a 14 mm push-back mitre on flat boxes and called it a bevel;
       a silhouette cannot be rescued by a mitre it does not show.
     * GEOMETRY OWNS 80 MM AND UP -- a valve wheel is a ring of real
       quads (wheelZ), a pipe is an octagonal tube (tube), a door sits
       recessed inside a real proud frame. The bake shader keeps only
       what is under ~80 mm: bolts, grille lines, mill grain, screen
       scanlines. The first pass painted whole valve wheels and pipe
       domes onto flat faces, which is exactly "a primitive reading as
       itself" with a picture on it.
     * ONE ATLAS PER PROP, its own square texture, packed so every
       region keeps the SAME texels-per-metre as every other region on
       that prop (packAtlas below). Repeated undetailed surfaces (the
       eight walls of a tube, mitre strips, plain lofts) SHARE one
       region -- tiling by reuse -- so the added geometry does not
       starve the hero faces of texel density.
     * Height -> normal is baked at load time in propview.html, per
       ref/procedural-scifi-normal-spec.md section 10's own recommendation.
   ============================================================ */

// ---- vector helpers -------------------------------------------------
function vAdd(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
function vSub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function vScale(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }
function vMA(base, dir, s) { return vAdd(base, vScale(dir, s)); }   // base + dir*s
function vCross(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function vLen(a) { return Math.hypot(a[0], a[1], a[2]); }
function vNorm(a) { const l = vLen(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }
// outward normal of a quad wound p0->p1->p2->p3 counter-clockwise seen from outside
function quadN(p0, p1, p2, p3) { return vNorm(vCross(vSub(p1, p0), vSub(p3, p0))); }
// deterministic jitter for organic corner offsets (canopy lobes)
function jHash(s) { const x = Math.sin(s * 127.1 + 311.7) * 43758.545; return x - Math.floor(x) - 0.5; }

const AX = [1, 0, 0], AY = [0, 1, 0], AZ = [0, 0, 1];
const NX = [-1, 0, 0], NY = [0, -1, 0], NZ = [0, 0, -1];

// pattern kinds baked by propview.html's bake shader
const K_PANEL = 0, K_RIB = 1, K_SCREEN = 2, K_BUTTONS = 3, K_HAZARD = 4,
      K_VALVE = 5, K_SEAM = 6, K_PLAIN = 7, K_PIPE = 8, K_FOLIAGE = 9;

// accent roles -- see tools/shipmaterials.py: three roles, never a fourth.
const A_NONE = 0, A_OCHRE = 1, A_AMBER = 2, A_LIVE_C = 3, A_LIVE_G = 4;

const PROPS = [];

/* ---- one prop's build context ---------------------------------------
   Everything a prop function draws goes through addFace()/face4(),
   which store a *pending* face: real 3D corners plus a LOCAL 0..1
   rectangle uv, and register one atlas region (real-world w,h in
   metres + the pattern kind/params the bake shader should run for it).
   UVs are rewritten to final atlas coordinates in finalizeProp(), after
   every prop's regions are known and packed. */
function propDef(name, room, sub, size, buildFn) {
  const ctx = {
    faces: [],       // {p:[p0,p1,p2,p3], n, ri:regionIndex}
    regions: [],     // {w,h, kind, seed, params, accent}
    _mitre: -1,
    _share: {},      // (kind|w|h|seed|accent|params) -> region index
  };

  function addRegion(w, h, kind, opts) {
    opts = opts || {};
    const ri = ctx.regions.length;
    ctx.regions.push({
      w: Math.max(0.01, w), h: Math.max(0.01, h), kind: kind,
      seed: opts.seed || 1,
      params: opts.params || [0, 0, 0, 0],
      accent: opts.accent === undefined ? A_NONE : opts.accent,
      // paint scheme: tint multiplies the bake's albedo ramp; paint
      // additionally drops metalness (a painted shell is a dielectric).
      // Bare metal is the default -- paint is a per-face decision.
      tint: opts.tint || null,
      paint: !!opts.paint,
    });
    return ri;
  }

  // identical-looking repeated surfaces (tube walls, plain lofts, caps)
  // share one atlas region -- tiling by reuse, so real geometry stays
  // cheap in texels. Keyed on everything the bake reads.
  function sharedRegion(w, h, kind, opts) {
    opts = opts || {};
    const key = kind + '|' + Math.round(w * 500) + '|' + Math.round(h * 500) + '|'
      + (opts.seed || 1) + '|' + (opts.accent || 0) + '|' + (opts.params || []).join(',')
      + '|' + (opts.tint ? opts.tint.join(',') : '') + '|' + (opts.paint ? 1 : 0);
    if (ctx._share[key] !== undefined) return ctx._share[key];
    const ri = addRegion(w, h, kind, opts);
    ctx._share[key] = ri;
    return ri;
  }

  function addFace(p0, p1, p2, p3, n, w, h, kind, opts) {
    const ri = addRegion(w, h, kind, opts || {});
    ctx.faces.push({ p: [p0, p1, p2, p3], n: n, ri: ri });
    return ri;
  }

  /* face4(p0..p3, opts) -- one arbitrary quad, wound CCW seen from
     outside; normal and region size are computed from the corners, so
     sloped and tapered faces cost the caller nothing. opts.share pools
     the region with identical-looking faces (default true -- lofts and
     strips are the main users); pass share:false for a face that must
     own a unique unwrap (screens, door leaves, hazard bands). */
  function face4(p0, p1, p2, p3, opts) {
    opts = opts || {};
    const n = quadN(p0, p1, p2, p3);
    const w = (vLen(vSub(p1, p0)) + vLen(vSub(p2, p3))) * 0.5;
    const h = (vLen(vSub(p3, p0)) + vLen(vSub(p2, p1))) * 0.5;
    const kind = opts.kind === undefined ? K_PLAIN : opts.kind;
    const ri = (opts.share === false)
      ? addRegion(w, h, kind, opts)
      : sharedRegion(w, h, kind, opts);
    // opts.nv: per-corner normals for smooth shading (round surfaces);
    // without it the face is flat-shaded off its own plane normal.
    ctx.faces.push({ p: [p0, p1, p2, p3], n: n, nv: opts.nv || null, ri: ri });
  }

  // one shared small trim tile for the plain mitred bevel strips of
  // panel() -- cheap, repeated, undetailed trim never needs its own
  // unique unwrap.
  function mitreRegion() {
    if (ctx._mitre < 0) ctx._mitre = addRegion(0.05, 0.05, K_PLAIN, { seed: 5 });
    return ctx._mitre;
  }
  function addMitreFace(p0, p1, p2, p3, n) {
    ctx.faces.push({ p: [p0, p1, p2, p3], n: n, ri: mitreRegion() });
  }

  /* panel(O,U,V,N, w,h, opts) -- one rectangular face, U x V metres,
     U x V must already satisfy cross(U,V) == N. opts: {kind, seed,
     params, accent, chamfer}. chamfer=0 skips the bevel and just fills
     the flat rect. Still the right tool for SURFACE relief -- a door
     leaf, a drawer front, a top plate -- sitting on real geometry;
     no longer asked to fake the silhouette. NOTE the chamfer pushes the
     inner face back by ~chamfer, so a panel laid on a surface must
     stand proud of it by MORE than its chamfer or its centre z-buries. */
  function panel(O, U, V, N, w, h, opts) {
    opts = opts || {};
    const c = opts.chamfer === undefined ? 0.014 : opts.chamfer;
    if (c <= 0.0001) {
      const p0 = O, p1 = vMA(O, U, w), p2 = vMA(p1, V, h), p3 = vMA(O, V, h);
      addFace(p0, p1, p2, p3, N, w, h, opts.kind === undefined ? K_PANEL : opts.kind, opts);
      return;
    }
    const cw = Math.min(c, w * 0.32), ch = Math.min(c, h * 0.32);
    const p0 = O, p1 = vMA(O, U, w), p2 = vMA(p1, V, h), p3 = vMA(O, V, h);
    const bk = (cw + ch) * 0.5;
    const q0 = vMA(vMA(vMA(O, U, cw), V, ch), N, -bk);
    const q1 = vMA(vMA(vMA(O, U, w - cw), V, ch), N, -bk);
    const q2 = vMA(vMA(vMA(O, U, w - cw), V, h - ch), N, -bk);
    const q3 = vMA(vMA(vMA(O, U, cw), V, h - ch), N, -bk);
    addFace(q0, q1, q2, q3, N, w - 2 * cw, h - 2 * ch, opts.kind === undefined ? K_PANEL : opts.kind, opts);
    addMitreFace(p0, p1, q1, q0, N);
    addMitreFace(p1, p2, q2, q1, N);
    addMitreFace(p2, p3, q3, q2, N);
    addMitreFace(p3, p0, q0, q3, N);
  }

  /* box(cx,cy,cz, w,h,d, sides) -- axis-aligned box. Kept for SMALL
     hardware only (handles, spokes, clamps, hinge blocks) where a hard
     square edge is honest; anything the eye reads as a mass goes
     through octBox / hull8 and earns real 45-degree geometry. */
  function box(cx, cy, cz, w, h, d, sides) {
    const hw = w / 2, hh = h / 2, hd = d / 2;
    const S = sides || {};
    if (S.px) panel([cx + hw, cy - hh, cz + hd], NZ, AY, AX, d, h, S.px);
    if (S.nx) panel([cx - hw, cy - hh, cz - hd], AZ, AY, NX, d, h, S.nx);
    if (S.py) panel([cx - hw, cy + hh, cz + hd], AX, NZ, AY, w, d, S.py);
    if (S.ny) panel([cx - hw, cy - hh, cz - hd], AX, AZ, NY, w, d, S.ny);
    if (S.pz) panel([cx - hw, cy - hh, cz + hd], AX, AY, AZ, w, h, S.pz);
    if (S.nz) panel([cx + hw, cy - hh, cz - hd], NX, AY, NZ, w, h, S.nz);
  }

  // convenience: a fully plain box (all six faces, shared trim look);
  // extra merges e.g. {tint, paint} into every face
  function plainBox(cx, cy, cz, w, h, d, seed, extra) {
    const o = Object.assign({ kind: K_PLAIN, seed: seed || 7, chamfer: 0 }, extra || {});
    box(cx, cy, cz, w, h, d, { px: o, nx: o, py: o, ny: o, pz: o, nz: o });
  }

  // ---- rings ----------------------------------------------------------
  // All rings are wound so that hullRing(A_low, B_high) faces point
  // OUTWARD and capPoly(ring) faces along the loft direction (verified
  // by direct cross products on the rect case).

  // axis-aligned rectangle ring in the XZ plane, CCW seen from +Y
  function rectRing(cx, cz, hw, hd, y) {
    return [[cx + hw, y, cz + hd], [cx + hw, y, cz - hd], [cx - hw, y, cz - hd], [cx - hw, y, cz + hd]];
  }

  // rectangle with all four corners cut at 45 degrees by c -- the same
  // octagonal footprint the corridor section runs on. Face index map for
  // hullRing: 0=+x 2=-z 4=-x 6=+z, odd = the four diagonals.
  function octFoot(cx, cz, hw, hd, c, y) {
    c = Math.min(c, hw * 0.9, hd * 0.9);
    return [
      [cx + hw, y, cz + hd - c], [cx + hw, y, cz - hd + c],
      [cx + hw - c, y, cz - hd], [cx - hw + c, y, cz - hd],
      [cx - hw, y, cz - hd + c], [cx - hw, y, cz + hd - c],
      [cx - hw + c, y, cz + hd], [cx + hw - c, y, cz + hd],
    ];
  }

  // n-gon ring around an arbitrary axis: U x V must equal the loft
  // direction (right-handed), theta increasing.
  function ringAt(c, U, V, r, n, phase) {
    const pts = [];
    for (let k = 0; k < n; k++) {
      const t = phase + k * 2 * Math.PI / n;
      pts.push(vAdd(c, vAdd(vScale(U, Math.cos(t) * r), vScale(V, Math.sin(t) * r))));
    }
    return pts;
  }
  function revRing(r) { return r.slice().reverse(); }

  /* hullRing(A, B, opts, smooth) -- loft ring A to ring B, one quad per
     edge. opts: a single opts object for every face, or a function
     (k)=>opts returning null to skip face k. Identical faces pool one
     region. smooth=true averages neighbouring face normals across each
     shared edge, so an 8-sided tube SHADES round while staying 8 quads
     -- the fix for "각이 아직 좀 많네": a pipe's facets were real
     geometry but only flat shading made them read as facets. Boxes and
     wedges stay flat-shaded; smoothing is for what is meant to be round. */
  function hullRing(A, B, opts, smooth) {
    const n = A.length;
    let fns = null;
    if (smooth) {
      fns = [];
      for (let k = 0; k < n; k++) {
        const k1 = (k + 1) % n;
        fns.push(quadN(A[k], A[k1], B[k1], B[k]));
      }
    }
    for (let k = 0; k < n; k++) {
      const o = (typeof opts === 'function') ? opts(k) : opts;
      if (!o) continue;
      const k1 = (k + 1) % n;
      if (smooth) {
        const nP = vNorm(vAdd(fns[(k + n - 1) % n], fns[k]));
        const nN = vNorm(vAdd(fns[k], fns[k1]));
        face4(A[k], A[k1], B[k1], B[k], Object.assign({}, o, { nv: [nP, nN, nN, nP] }));
      } else {
        face4(A[k], A[k1], B[k1], B[k], o);
      }
    }
  }

  /* capPoly(ring, opts) -- flat cap over a ring, facing the ring's loft
     direction; pass revRing(ring) for the far side. Emitted as quads
     meeting at the centroid, which tile an octagon exactly. */
  function capPoly(ring, opts) {
    const n = ring.length;
    const C = ring.reduce((a, p) => vAdd(a, p), [0, 0, 0]).map(v => v / n);
    if (n === 4) { face4(ring[0], ring[1], ring[2], ring[3], opts); return; }
    for (let k = 0; k < n; k += 2) {
      face4(ring[k], ring[(k + 1) % n], ring[(k + 2) % n], C, opts);
    }
  }

  /* octBox(cx,cy,cz, w,h,d, cut, S) -- the workhorse mass. A box whose
     four vertical edges are cut at 45 degrees by `cut`, with an optional
     45-degree chamfer band at the top (S.topCut) before the cap. This is
     the corridor's no-right-angle rule as a prop primitive.
       S: {px,nx,pz,nz}  cardinal face opts (unique regions; null skips)
          diag           opts for the four 45-degree faces (shared; default plain)
          py             top cap opts (null skips); ny likewise
          topCut         metres of top chamfer; topOpts: single opts or
                         (k)=>opts for the chamfer band's eight faces */
  function octBox(cx, cy, cz, w, h, d, cut, S) {
    S = S || {};
    const hw = w / 2, hd = d / 2, y0 = cy - h / 2, y1 = cy + h / 2;
    const tc = Math.min(S.topCut || 0, h * 0.45);
    const diag = S.diag === undefined ? { kind: K_PLAIN, seed: 6 } : S.diag;
    const card = [S.px, null, S.nz, null, S.nx, null, S.pz, null];
    const ringB = octFoot(cx, cz, hw, hd, cut, y0);
    const ringT = octFoot(cx, cz, hw, hd, cut, y1 - tc);
    hullRing(ringB, ringT, k => {
      if (k % 2 === 1) return diag;
      const o = card[k];
      if (o === undefined || o === null) return null;
      return Object.assign({ share: false }, o);
    });
    let capRing = ringT;
    if (tc > 0.0001) {
      const ringC = octFoot(cx, cz, hw - tc, hd - tc, Math.max(0.005, cut - tc * 0.5), y1);
      hullRing(ringT, ringC, S.topOpts || { kind: K_PLAIN, seed: 8 });
      capRing = ringC;
    }
    if (S.py) capPoly(capRing, S.py);
    if (S.ny) capPoly(revRing(ringB), S.ny);
  }

  /* hull8(B, T, S) -- distorted box between two rectRings (bottom, top):
     wedges, tapers, sloped decks, flared hoods. Face map: S.px (+x),
     S.nz, S.nx, S.pz sides; S.py / S.ny caps. Side faces auto-slope; a
     console's keyboard deck is just the pz face of the right loft. */
  function hull8(B, T, S) {
    S = S || {};
    const card = [S.px, S.nz, S.nx, S.pz];
    hullRing(B, T, k => {
      const o = card[k];
      if (o === undefined || o === null) return null;
      return o.share === undefined ? Object.assign({ share: false }, o) : o;
    });
    if (S.py) face4(T[0], T[1], T[2], T[3], S.py);
    if (S.ny) face4(B[3], B[2], B[1], B[0], S.ny);
  }

  /* strap(a, b, wdt, t, N, opts) -- a flat diagonal bar lying on a
     plane of outward normal N, proud of it by t: the mood ref's
     X-bracing, which no axis-aligned box can draw. Ends are square --
     bury them in the rails or ribs that frame the bay. */
  function strap(a, b, wdt, t, N, opts) {
    opts = opts || {};
    const dir = vNorm(vSub(b, a));
    const side = vNorm(vCross(N, dir));
    const hw = wdt / 2;
    const a0 = vMA(a, side, -hw), a1 = vMA(a, side, hw);
    const b0 = vMA(b, side, -hw), b1 = vMA(b, side, hw);
    const A0 = vMA(a0, N, t), A1 = vMA(a1, N, t), B0 = vMA(b0, N, t), B1 = vMA(b1, N, t);
    face4(A0, B0, B1, A1, opts);            // front face, normal +N
    const eo = { kind: K_PLAIN, seed: opts.seed || 7 };
    face4(a0, b0, B0, A0, eo);              // -side edge
    face4(b1, a1, A1, B1, eo);              // +side edge
  }

  /* tube(p0, p1, r, opts) -- octagonal tube between two points, any
     direction. The prop kit's pipe: eight REAL faces sharing one atlas
     region, not a dome painted on a plank. opts: {face, capA, capB,
     capOpts, n, phase}. */
  function tube(p0, p1, r, opts) {
    opts = opts || {};
    const n = opts.n || 8;
    const dir = vNorm(vSub(p1, p0));
    let U = vCross([0, 1, 0], dir);
    if (vLen(U) < 0.01) U = vCross([1, 0, 0], dir);
    U = vNorm(U);
    const V = vCross(dir, U);            // U x V == dir (right-handed)
    const phase = opts.phase === undefined ? Math.PI / n : opts.phase;
    const A = ringAt(p0, U, V, r, n, phase);
    const B = ringAt(p1, U, V, r, n, phase);
    const fo = Object.assign({ kind: K_PLAIN, seed: 9 }, opts.face || {});
    hullRing(A, B, fo, true);   // a pipe shades round
    const co = opts.capOpts || { kind: K_PLAIN, seed: 10 };
    if (opts.capA) capPoly(revRing(A), co);
    if (opts.capB) capPoly(B, co);
    return { A, B, U, V, dir };
  }

  /* wheelZ(cx,cy,cz, R, t, depth, opts) -- a real hand-wheel facing +Z:
     octagonal rim (outer wall, inner wall, front and back ring faces),
     an octagonal hub proud both ways, and four square spokes. ~50
     quads. The first pass drew this as a picture; the picture is now
     only allowed on gauge dials under 80 mm. */
  function wheelZ(cx, cy, cz, R, t, depth, opts) {
    opts = opts || {};
    const n = 8, ph = Math.PI / n;
    const zB = cz - depth / 2, zF = cz + depth / 2;
    const oB = ringAt([cx, cy, zB], AX, AY, R, n, ph);
    const oF = ringAt([cx, cy, zF], AX, AY, R, n, ph);
    const iB = ringAt([cx, cy, zB], AX, AY, R - t, n, ph);
    const iF = ringAt([cx, cy, zF], AX, AY, R - t, n, ph);
    const mo = { kind: K_PLAIN, seed: opts.seed || 11, tint: opts.tint, paint: opts.paint };
    hullRing(oB, oF, mo, true);                      // outer wall, shades round
    hullRing(revRing(iB), revRing(iF), mo, true);    // inner wall, normals to axis
    for (let k = 0; k < n; k++) {                    // front + back ring faces
      const k1 = (k + 1) % n;
      face4(oF[k], oF[k1], iF[k1], iF[k], mo);
      face4(iB[k], iB[k1], oB[k1], oB[k], mo);
    }
    // hub, proud of the rim both ways
    const hr = opts.hub || R * 0.22;
    const hB = ringAt([cx, cy, zB - 0.012], AX, AY, hr, n, ph);
    const hF = ringAt([cx, cy, zF + 0.012], AX, AY, hr, n, ph);
    hullRing(hB, hF, mo, true);
    capPoly(hF, { kind: K_SEAM, seed: (opts.seed || 11) + 1, params: [1, 0, 0, 0] });
    // four spokes as two through-bars, painted with the rim
    const sw = opts.spoke || t * 0.9;
    plainBox(cx, cy, cz, (R - t * 0.5) * 2, sw, depth * 0.8, opts.seed, { tint: opts.tint, paint: opts.paint });
    plainBox(cx, cy, cz, sw, (R - t * 0.5) * 2, depth * 0.8, opts.seed, { tint: opts.tint, paint: opts.paint });
  }

  buildFn({
    box, plainBox, panel, addFace, face4, hullRing, capPoly, hull8, octBox,
    tube, wheelZ, strap, rectRing, octFoot, ringAt, revRing,
    K_PANEL, K_RIB, K_SCREEN, K_BUTTONS, K_HAZARD, K_VALVE, K_SEAM, K_PLAIN, K_PIPE, K_FOLIAGE,
    A_NONE, A_OCHRE, A_AMBER, A_LIVE_C, A_LIVE_G, AX, AY, AZ, NX, NY, NZ,
  });

  PROPS.push({ name, room, sub, size, faces: ctx.faces, regions: ctx.regions });
  return PROPS.length - 1;
}

/* ---- atlas packer -----------------------------------------------------
   Shelf-packs every region's real-world (w,h) into a square texture at
   the LARGEST uniform texels-per-metre that still fits -- one texel
   density for the whole prop, the same discipline TEX_SCALE enforces
   for the whole ship, just applied per-object. */
function shelfPack(regions, texelsPerMetre, texSize, pad) {
  const items = regions.map((r, i) => ({ i, w: Math.max(1, Math.ceil(r.w * texelsPerMetre)),
                                          h: Math.max(1, Math.ceil(r.h * texelsPerMetre)) }));
  items.sort((a, b) => b.h - a.h);
  let x = pad, y = pad, shelfH = 0;
  const rects = new Array(regions.length);
  for (const it of items) {
    if (x + it.w + pad > texSize) { x = pad; y += shelfH + pad; shelfH = 0; }
    if (y + it.h + pad > texSize) return null;
    rects[it.i] = { x, y, w: it.w, h: it.h };
    x += it.w + pad;
    shelfH = Math.max(shelfH, it.h);
  }
  return rects;
}

function packAtlas(regions, texSize) {
  texSize = texSize || 1024;
  const pad = 3;
  let lo = 4, hi = 4000, best = null;
  for (let iter = 0; iter < 24; iter++) {
    const mid = (lo + hi) / 2;
    const r = shelfPack(regions, mid, texSize, pad);
    if (r) { best = r; lo = mid; } else { hi = mid; }
  }
  const rects = best || shelfPack(regions, lo, texSize, pad);
  return rects.map(r => ({
    u0: r.x / texSize, v0: r.y / texSize,
    u1: (r.x + r.w) / texSize, v1: (r.y + r.h) / texSize,
    px: r.x, py: r.y, pw: r.w, ph: r.h,
  }));
}

/* ---- finalize: rewrite each pending face's local 0..1 uv into its
   region's packed atlas rect, and flatten to a GPU-ready vertex buffer
   (stride 8: pos3, normal3, uv2), matching tools/kit.js's TRIS layout. */
function finalizeProp(idx, texSize) {
  const p = PROPS[idx];
  const rects = packAtlas(p.regions, texSize || 1024);
  const V = [];
  for (const f of p.faces) {
    const rect = rects[f.ri];
    const nv = f.nv || [f.n, f.n, f.n, f.n];   // smooth corners when a loft asked for them
    const uv = [[rect.u0, rect.v0], [rect.u1, rect.v0], [rect.u1, rect.v1], [rect.u0, rect.v1]];
    const emit = i => {
      const P = f.p[i], N = nv[i], U = uv[i];
      V.push(P[0], P[1], P[2], N[0], N[1], N[2], U[0], U[1]);
    };
    emit(0); emit(1); emit(2);
    emit(0); emit(2); emit(3);
  }
  const regions = p.regions.map((r, i) => Object.assign({}, r, rects[i]));
  return { name: p.name, room: p.room, sub: p.sub, size: p.size,
           vertices: new Float32Array(V), regions, texSize: texSize || 1024 };
}

/* ============================================================
   THE PROPS.  Nine hero pieces off the deck plan's own room list
   (ref/ship_deck_plan_v6_fixed.svg / ref/derelict-ship-gdd.md section
   12's prop budget), plus a room-dressing set (props 10-19) so every
   playable room can be furnished, not just anchored. Every one built
   on the Doom-3 shape grammar: a stepped-in plinth (shadow line at
   the floor), a chamfered or sloped crown (never a bare flat top),
   and at least two real depths on any face the player is asked to
   read.
   ============================================================ */

// ---- paint palette ---------------------------------------------------
// Sampled off ref/mood_ref (the dark plant-deck shot): near-black
// armour green on the big shells, grimy pale steel on pipe runs, muted
// oxide red as the secondary pipe colour, near-black gunmetal on gear.
// Emissives stay the three accent roles (ochre hazard, amber, live
// green/cyan) -- the ref's own signage and LEDs. Spread {tint, paint}
// onto painted SHELL faces; frames, rails and machined trim stay bare
// metal -- the two-material split is the read, not either alone.
// (tints ride a darkened bake ramp -- HI is now 0.285 -- so pale
// surfaces carry tints above 1.0 and the dark ones sit near the ramp)
const P_HULL  = { tint: [0.42, 0.50, 0.43], paint: true };   // dark armour green (ref walls)
const P_STEEL = { tint: [1.60, 1.65, 1.55], paint: true };   // grimy pale pipe steel (ref overheads)
const P_OXIDE = { tint: [0.95, 0.28, 0.20], paint: true };   // muted oxide red (ref pipe runs)
const P_OCHRE = { tint: [1.30, 0.78, 0.26], paint: true };   // valve-wheel / gear paint
const P_DARK  = { tint: [0.42, 0.42, 0.41], paint: true };   // near-black gunmetal
const P_BONE  = { tint: [1.50, 1.55, 1.45], paint: true };   // clinical shell over the dark ramp

// ---- 1. CARGO CONTAINER -- 화물칸 (cargo bay) --------------------------
// Real container language: a 45-degree top chamfer wearing the ochre
// hazard band (where a rakelight finds it), proud vertical ribs and
// rails framing recessed panel bays, twin lock rods on the door end,
// corner feet floating the body 70 mm off the deck.
propDef('Cargo Container', 'cargo', '화물칸 · 2.4 x 1.2 x 1.2 m stackable', [2.4, 1.2, 1.2], (c) => {
  const { octBox, box, plainBox, panel, strap, K_PANEL, K_HAZARD, K_SEAM, K_PLAIN, A_OCHRE, AX, AZ, AY, NZ } = c;

  for (const x of [-1.05, 1.05]) for (const z of [-0.44, 0.44]) {
    plainBox(x, 0.035, z, 0.18, 0.07, 0.18, 21);
  }

  octBox(0, 0.635, 0, 2.4, 1.13, 1.2, 0.08, {
    px: { kind: K_PLAIN, seed: 22 },                                  // door end backing, bare
    nx: { kind: K_PANEL, seed: 23, params: [1, 0, 0, 0], ...P_HULL },  // blank end, painted plate
    pz: { kind: K_PANEL, seed: 24, params: [1, 0, 0, 0], ...P_HULL },  // painted bays between bare ribs
    nz: { kind: K_PANEL, seed: 25, params: [1, 0, 0, 0], ...P_HULL },
    diag: { kind: K_PLAIN, seed: 26 },
    topCut: 0.07,
    topOpts: k => (k % 2 === 0)
      ? { kind: K_HAZARD, seed: 27, params: [0.10, 0, 0, 0], accent: A_OCHRE, share: false }
      : { kind: K_PLAIN, seed: 26 },
    py: { kind: K_PLAIN, seed: 28 },
  });
  // roof detail plates, proud of the cap by more than their chamfer
  panel([-1.05, 1.212, -0.42], AX, AZ, AY, 1.00, 0.84, { kind: K_PANEL, seed: 29, params: [1, 0, 0, 0], chamfer: 0.010, ...P_HULL });
  panel([0.10, 1.212, -0.42], AX, AZ, AY, 1.00, 0.84, { kind: K_PANEL, seed: 30, params: [1, 0, 0, 0], chamfer: 0.010, ...P_HULL });

  // long sides: three proud ribs framing two wide bays, each bay
  // X-braced the way the mood ref's wall panels are, between the rails
  for (const s of [-1, 1]) {
    const z = s * 0.615;   // rib body buried 10 mm into the backing face
    for (const x of [-0.84, 0, 0.84]) {
      box(x, 0.60, z, 0.10, 0.80, 0.05, {
        pz: s > 0 ? { kind: K_PLAIN, seed: 31, chamfer: 0.008 } : null,
        nz: s < 0 ? { kind: K_PLAIN, seed: 31, chamfer: 0.008 } : null,
        px: { kind: K_PLAIN, seed: 31, chamfer: 0 }, nx: { kind: K_PLAIN, seed: 31, chamfer: 0 },
      });
    }
    for (const ry of [0.155, 1.045]) {
      box(0, ry, z, 2.20, 0.09, 0.05, {
        pz: s > 0 ? { kind: K_SEAM, seed: 32, params: [1, 0, 0, 0], chamfer: 0.008 } : null,
        nz: s < 0 ? { kind: K_SEAM, seed: 32, params: [1, 0, 0, 0], chamfer: 0.008 } : null,
        py: { kind: K_PLAIN, seed: 31, chamfer: 0 }, ny: { kind: K_PLAIN, seed: 31, chamfer: 0 },
      });
    }
    // X-brace per bay, ends buried in the ribs and rails
    const zN = s > 0 ? AZ : NZ, zP = s * 0.60;
    for (const [xa, xb] of [[-0.82, -0.07], [0.07, 0.82]]) {
      strap([xa, 0.21, zP], [xb, 0.99, zP], 0.10, 0.035, zN, { seed: 37 });
      strap([xb, 0.21, zP], [xa, 0.99, zP], 0.10, 0.035, zN, { seed: 37 });
    }
  }

  // door end (+x): proud frame, recessed leaf, twin lock rods with cams
  const fx = 1.225;   // frame centreline; backing face sits at x=1.20
  box(1.2125, 0.60, 0, 0.025, 0.94, 1.04, { px: { kind: K_PANEL, seed: 33, chamfer: 0.016, ...P_HULL } }); // door leaf, painted, recessed inside the bare frame
  for (const z of [-0.50, 0.50]) box(fx, 0.60, z, 0.05, 1.00, 0.06, { px: { kind: K_PLAIN, seed: 34, chamfer: 0.006 }, pz: { kind: K_PLAIN, seed: 34, chamfer: 0 }, nz: { kind: K_PLAIN, seed: 34, chamfer: 0 } });
  for (const y of [0.115, 1.085]) box(fx, y, 0, 0.05, 0.07, 1.04, { px: { kind: K_PLAIN, seed: 34, chamfer: 0.006 }, py: { kind: K_PLAIN, seed: 34, chamfer: 0 }, ny: { kind: K_PLAIN, seed: 34, chamfer: 0 } });
  for (const z of [-0.22, 0.22]) {
    box(1.245, 0.60, z, 0.04, 0.88, 0.04, { px: { kind: K_PLAIN, seed: 35, chamfer: 0 }, pz: { kind: K_PLAIN, seed: 35, chamfer: 0 }, nz: { kind: K_PLAIN, seed: 35, chamfer: 0 } });   // lock rod
    box(1.255, 0.72, z + 0.05, 0.03, 0.05, 0.14, { px: { kind: K_SEAM, seed: 36, chamfer: 0 }, py: { kind: K_PLAIN, seed: 36, chamfer: 0 } });  // handle cam
  }
});

// ---- 2. SUPPLY CRATE -- 창고 / 탄약 캐시 (storage, ammo caches) --------
// The lid overhangs the body and wears a 45-degree chamfer all round --
// hazard-striped fore and aft -- and four corner uprights armour the
// body's cut edges. Reads as a crate from silhouette alone.
propDef('Supply Crate', 'storage', '창고 · 탄약 캐시 · 0.66 x 0.5 x 0.44 m', [0.66, 0.5, 0.44], (c) => {
  const { octBox, hull8, rectRing, box, plainBox, K_PANEL, K_HAZARD, K_SEAM, K_PLAIN, A_OCHRE } = c;

  // inset base -- shadow gap under the body -- closed on top so the
  // body's open bottom ring never shows from a low angle
  hull8(rectRing(0, 0, 0.26, 0.16, 0.0), rectRing(0, 0, 0.28, 0.18, 0.035),
    { px: { kind: K_PLAIN, seed: 41 }, nx: { kind: K_PLAIN, seed: 41 }, pz: { kind: K_PLAIN, seed: 41 }, nz: { kind: K_PLAIN, seed: 41 },
      py: { kind: K_PLAIN, seed: 41 } });

  octBox(0, 0.1975, 0, 0.60, 0.325, 0.40, 0.035, {
    px: { kind: K_PANEL, seed: 42, params: [1, 0, 0, 0], ...P_HULL }, nx: { kind: K_PANEL, seed: 43, params: [1, 0, 0, 0], ...P_HULL },
    pz: { kind: K_PANEL, seed: 44, params: [1, 0, 0, 0], ...P_HULL }, nz: { kind: K_PANEL, seed: 45, params: [1, 0, 0, 0], ...P_HULL },
    diag: { kind: K_PLAIN, seed: 46 },
  });

  // corner uprights, proud of the cut edges, running up into the lid
  for (const [x, z] of [[-0.275, -0.175], [0.275, -0.175], [0.275, 0.175], [-0.275, 0.175]]) {
    plainBox(x, 0.20, z, 0.055, 0.37, 0.055, 47);
  }

  // lid: overhangs 20 mm, 45-degree chamfer all four ways, bolted top plate
  hull8(rectRing(0, 0, 0.32, 0.22, 0.36), rectRing(0, 0, 0.245, 0.145, 0.455), {
    px: { kind: K_PLAIN, seed: 48 }, nx: { kind: K_PLAIN, seed: 48 },
    pz: { kind: K_HAZARD, seed: 49, params: [0.16, 0, 0, 0], accent: A_OCHRE },
    nz: { kind: K_HAZARD, seed: 49, params: [0.16, 0, 0, 0], accent: A_OCHRE },
    py: { kind: K_PANEL, seed: 50, params: [1, 0, 0, 0], share: false },
  });
  // lid underside rim -- closes the overhang seen from below
  hull8(rectRing(0, 0, 0.32, 0.22, 0.355), rectRing(0, 0, 0.32, 0.22, 0.36),
    { px: { kind: K_PLAIN, seed: 48 }, nx: { kind: K_PLAIN, seed: 48 }, pz: { kind: K_PLAIN, seed: 48 }, nz: { kind: K_PLAIN, seed: 48 },
      ny: { kind: K_PLAIN, seed: 48 } });

  // handle bar on the lid + latch block on the front
  plainBox(0, 0.475, 0, 0.16, 0.035, 0.035, 51);
  for (const x of [-0.065, 0.065]) plainBox(x, 0.462, 0, 0.03, 0.03, 0.03, 51);
  box(0, 0.30, 0.215, 0.10, 0.13, 0.045, {
    pz: { kind: K_SEAM, seed: 52, params: [1, 0, 0, 0], chamfer: 0.008 },
    px: { kind: K_PLAIN, seed: 52, chamfer: 0 }, nx: { kind: K_PLAIN, seed: 52, chamfer: 0 },
    py: { kind: K_PLAIN, seed: 52, chamfer: 0 },
  });
});

// ---- 3. CONSOLE TERMINAL -- 로비 안내 단말 / 갤러리 슬롯 콘솔 -----------
// THE Doom-3 silhouette: an inset plinth, a vent band leaning OUT going
// up, a sloped keyboard deck, a screen tilted back under an amber brow,
// side cheeks proud of everything, and a cable conduit clamped down the
// back. Four stacked hull8 lofts SHARING their rings, so the profile is
// one continuous wedge, not stacked boxes with exposed ledges.
propDef('Console Terminal', 'lobby', '로비 안내 단말 · 갤러리 슬롯 콘솔 · 0.63 x 1.12 m', [0.63, 1.12, 0.60], (c) => {
  const { hull8, rectRing, box, plainBox, panel, tube, K_PANEL, K_RIB, K_SCREEN, K_BUTTONS, K_SEAM, K_PLAIN,
          A_AMBER, A_LIVE_C, AX, AZ, AY } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // profile rings, floor to crown; cz shifts push the front face out
  // then back
  const r0 = rectRing(0, 0.005, 0.30, 0.245, 0.00);   // floor plinth edge
  const r1 = rectRing(0, 0.005, 0.27, 0.220, 0.07);   // plinth top (inset = shadow)
  const r2 = rectRing(0, 0.005, 0.26, 0.205, 0.075);  // body shin start
  const r3 = rectRing(0, 0.050, 0.28, 0.250, 0.52);   // vent band top, front leaned OUT
  const r4 = rectRing(0, -0.035, 0.28, 0.160, 0.74);  // deck top: front pulled back = sloped keyboard
  const r5 = rectRing(0, -0.085, 0.26, 0.100, 1.06);  // crown: screen face tilted ~19 deg back

  hull8(r0, r1, { px: plain(53), nx: plain(53), pz: plain(53), nz: plain(53),
                  py: { kind: K_PLAIN, seed: 53, share: false } });
  hull8(r2, r3, {
    px: { kind: K_PANEL, seed: 54, params: [1, 0, 0, 0], ...P_DARK }, nx: { kind: K_PANEL, seed: 54, params: [1, 0, 0, 0], ...P_DARK },
    pz: { kind: K_RIB, seed: 55, params: [8, 0.08, 0, 0] },      // vent grille, 70 mm pitch: texture's side of the 80 mm line
    nz: plain(56),
  });
  hull8(r3, r4, {
    px: plain(57), nx: plain(57),
    pz: { kind: K_BUTTONS, seed: 58 },                            // the sloped face IS the keyboard
    nz: plain(56),
  });
  hull8(r4, r5, {
    px: plain(59), nx: plain(59),
    pz: { kind: K_SCREEN, seed: 60, accent: A_LIVE_C },           // tilted screen
    nz: plain(56),
    py: { kind: K_PLAIN, seed: 61, share: false },
  });
  // amber brow strip across the crown's front edge, on the cap
  panel([-0.24, 1.062, -0.06], AX, AZ, AY, 0.48, 0.055, { kind: K_SEAM, seed: 62, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });

  // side cheeks: one tapered slab each side, proud 20 mm, rooted in the
  // plinth's slope and running the whole profile -- the plate that makes
  // the silhouette read "console"
  for (const s of [-1, 1]) {
    const cx = s * 0.30;
    const rB = rectRing(cx, 0.02, 0.015, 0.250, 0.02);
    const rT = rectRing(cx, -0.075, 0.015, 0.115, 1.10);
    hull8(rB, rT, {
      px: s > 0 ? { kind: K_PANEL, seed: 63, params: [1, 0, 0, 0], ...P_HULL } : plain(64),
      nx: s < 0 ? { kind: K_PANEL, seed: 63, params: [1, 0, 0, 0], ...P_HULL } : plain(64),
      pz: plain(64), nz: plain(64),
      py: { kind: K_PLAIN, seed: 64, share: false },
    });
  }

  // grab bar across the deck's leading edge
  for (const x of [-0.20, 0.20]) plainBox(x, 0.555, 0.315, 0.035, 0.035, 0.05, 65);
  plainBox(0, 0.565, 0.335, 0.45, 0.032, 0.032, 65);

  // cable conduit down the back, clamped twice, into a junction box
  tube([0.10, 0.02, -0.245], [0.10, 0.84, -0.245], 0.028, { face: { kind: K_PLAIN, seed: 66, ...P_STEEL } });
  for (const y of [0.22, 0.62]) plainBox(0.10, y, -0.225, 0.09, 0.05, 0.05, 67);
  box(0.10, 0.90, -0.225, 0.14, 0.12, 0.08, {
    nz: { kind: K_SEAM, seed: 68, params: [1, 0, 0, 0], chamfer: 0.008 },
    px: { kind: K_PLAIN, seed: 68, chamfer: 0 }, nx: { kind: K_PLAIN, seed: 68, chamfer: 0 },
    py: { kind: K_PLAIN, seed: 68, chamfer: 0 }, ny: { kind: K_PLAIN, seed: 68, chamfer: 0 },
  });
});

// ---- 4. STORAGE LOCKER -- 창고 / 의무실 (storage, medbay) -------------
// Kick plinth stepped in under the body, doors recessed inside a real
// proud frame with bar handles and hinge blocks, and a sloped vent
// hood for a crown -- no bare flat top, no door drawn on a slab.
propDef('Storage Locker', 'storage', '창고 · 의무실 · 1.04 x 1.96 x 0.54 m', [1.04, 1.96, 0.54], (c) => {
  const { octBox, hull8, rectRing, box, plainBox, panel, strap, K_PANEL, K_RIB, K_HAZARD, K_PLAIN, A_OCHRE, AX, AY, AZ, NX } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // kick plinth: leans out slightly going up, hazard on the toe
  hull8(rectRing(0, 0, 0.44, 0.195, 0.0), rectRing(0, 0, 0.465, 0.215, 0.08), {
    px: plain(71), nx: plain(71), nz: plain(71),
    pz: { kind: K_HAZARD, seed: 72, params: [0.08, 0, 0, 0], accent: A_OCHRE },
    py: plain(71),
  });

  // body: front face is the plain recess backing the doors sit in
  octBox(0, 0.95, 0, 1.0, 1.74, 0.46, 0.05, {
    px: { kind: K_PANEL, seed: 73, params: [1, 0, 0, 0], ...P_DARK }, nx: { kind: K_PANEL, seed: 73, params: [1, 0, 0, 0], ...P_DARK },
    pz: { kind: K_PLAIN, seed: 74 }, nz: plain(74),
    diag: plain(75),
  });
  // X-brace on each flank, the mood ref's panel language
  for (const s of [-1, 1]) {
    const xP = s * 0.50, xN = s > 0 ? AX : NX;
    strap([xP, 0.15, -s * 0.15], [xP, 1.75, s * 0.15], 0.09, 0.03, xN, { seed: 86 });
    strap([xP, 0.15, s * 0.15], [xP, 1.75, -s * 0.15], 0.09, 0.03, xN, { seed: 86 });
  }

  // door frame, 30 mm proud of the backing (z 0.23 -> 0.26)
  const fz = 0.245, fd = 0.03;
  box(0, 1.77, fz, 0.90, 0.10, fd, { pz: plain(76), ny: plain(76) });                          // head rail (top closed by the hood)
  box(0, 0.18, fz, 0.90, 0.20, fd, { pz: { kind: K_HAZARD, seed: 77, params: [0.08, 0, 0, 0], accent: A_OCHRE, chamfer: 0.008 }, py: plain(76) }); // kick rail
  box(0, 0.95, fz, 0.05, 1.74, fd, { pz: plain(76), px: plain(76), nx: plain(76) });           // centre stile
  for (const s of [-1, 1]) box(s * 0.435, 0.95, fz, 0.03, 1.74, fd, { pz: plain(76), px: plain(76), nx: plain(76) });  // jambs

  // door leaves: 20 mm proud of the backing (chamfer clears it), louvre
  // band up top, bolted plate below, real bar handles, hinge blocks
  for (const s of [-1, 1]) {
    const x0 = s > 0 ? 0.04 : -0.42;
    panel([x0, 0.29, 0.25], AX, AY, AZ, 0.38, 1.28, { kind: K_PANEL, seed: 78 + s, params: [1, 0, 0, 0], chamfer: 0.014, ...P_HULL });
    panel([x0, 1.58, 0.25], AX, AY, AZ, 0.38, 0.13, { kind: K_RIB, seed: 80, params: [5, 0.06, 0, 0], chamfer: 0.008 });
    const hx = s * 0.10;
    for (const y of [0.98, 1.22]) plainBox(hx, y, 0.265, 0.03, 0.03, 0.035, 81);
    plainBox(hx, 1.10, 0.293, 0.032, 0.30, 0.028, 81);
    for (const y of [0.42, 0.95, 1.48]) plainBox(s * 0.47, y, 0.24, 0.045, 0.12, 0.05, 82);
  }

  // vent hood: overhangs everything, front slopes back carrying
  // real-scale louvres; its underside closes the body's open top
  hull8(rectRing(0, 0, 0.52, 0.27, 1.82), rectRing(0, -0.06, 0.50, 0.195, 1.96), {
    px: plain(83), nx: plain(83), nz: plain(83),
    pz: { kind: K_RIB, seed: 84, params: [6, 0.07, 0, 0] },
    py: { kind: K_PLAIN, seed: 85, share: false },
    ny: plain(83),
  });
});

// ---- 5. REACTOR VALVE STACK -- 기관실 (engine room) --------------------
// Real plumbing at last: an octagonal riser with bolted flange collars,
// a horizontal drum with end collars, a genuine eight-sided hand-wheel
// on a boss, a top offtake, and a conduit run with a junction cube.
// The K_VALVE picture survives only on the two gauge dials, 75 mm each.
propDef('Reactor Valve Stack', 'engine', '기관실 · 파이프 정션 · 0.62 x 1.47 x 0.66 m', [0.62, 1.47, 0.66], (c) => {
  const { octBox, tube, wheelZ, box, plainBox, K_SEAM, K_VALVE, K_PLAIN, K_HAZARD, A_AMBER, A_OCHRE, AY } = c;

  // base plate: chamfered top edge, hazard toe on the front
  octBox(0, 0.05, 0, 0.58, 0.10, 0.48, 0.06, {
    px: { kind: K_PLAIN, seed: 91, ...P_DARK }, nx: { kind: K_PLAIN, seed: 91, ...P_DARK },
    pz: { kind: K_HAZARD, seed: 92, params: [0.07, 0, 0, 0], accent: A_OCHRE }, nz: { kind: K_PLAIN, seed: 91, ...P_DARK },
    diag: { kind: K_PLAIN, seed: 91 },
    topCut: 0.03,
    py: { kind: K_PLAIN, seed: 93 },
  });

  // riser + flange collars (the collars' K_SEAM bolt row circles them):
  // pale painted steel between bare-metal collars, the ref's pipe read
  tube([0, 0.10, 0], [0, 0.96, 0], 0.155, { face: { kind: K_PLAIN, seed: 94, ...P_STEEL } });
  tube([0, 0.10, 0], [0, 0.17, 0], 0.20, { face: { kind: K_SEAM, seed: 95, params: [1, 0, 0, 0] }, capB: true });
  tube([0, 0.89, 0], [0, 0.96, 0], 0.20, { face: { kind: K_SEAM, seed: 95, params: [1, 0, 0, 0] }, capA: true, capB: true });

  // drum across the top, end collars, front boss
  tube([0, 1.10, -0.19], [0, 1.10, 0.19], 0.17, { face: { kind: K_PLAIN, seed: 96, ...P_STEEL }, capA: true, capB: true });
  tube([0, 1.10, -0.20], [0, 1.10, -0.13], 0.195, { face: { kind: K_SEAM, seed: 97, params: [1, 0, 0, 0] }, capA: true, capB: true });
  tube([0, 1.10, 0.13], [0, 1.10, 0.20], 0.195, { face: { kind: K_SEAM, seed: 97, params: [1, 0, 0, 0] }, capA: true, capB: true });
  tube([0, 1.10, 0.20], [0, 1.10, 0.27], 0.07, { face: { kind: K_PLAIN, seed: 98, ...P_STEEL } });

  // the wheel. R 220 mm -- geometry's job, and now it does it. Painted
  // ochre the way every real valve wheel is painted something.
  wheelZ(0, 1.10, 0.305, 0.22, 0.04, 0.05, { seed: 99, hub: 0.05, ...P_OCHRE });

  // offtake up out of the drum, capped with its own flange
  tube([0, 1.24, 0], [0, 1.42, 0], 0.10, { face: { kind: K_PLAIN, seed: 100, ...P_STEEL } });
  tube([0, 1.42, 0], [0, 1.47, 0], 0.13, { face: { kind: K_SEAM, seed: 101, params: [1, 0, 0, 0] }, capB: true });

  // conduit run: up the side, junction cube, across into the riser
  tube([0.24, 0.10, 0.05], [0.24, 0.86, 0.05], 0.045, { face: { kind: K_PLAIN, seed: 102, ...P_STEEL } });
  plainBox(0.24, 0.90, 0.05, 0.11, 0.11, 0.11, 103);
  tube([0.24, 0.90, 0.05], [0.11, 0.90, 0.05], 0.045, { face: { kind: K_PLAIN, seed: 102, ...P_STEEL } });

  // second run up the back in the ref's oxide red -- the one saturated
  // colour the mood board allows a pipe
  tube([-0.10, 0.10, -0.20], [-0.10, 0.70, -0.20], 0.04, { face: { kind: K_PLAIN, seed: 108, ...P_OXIDE } });
  plainBox(-0.10, 0.74, -0.20, 0.10, 0.10, 0.10, 109, P_OXIDE);
  tube([-0.10, 0.74, -0.20], [-0.04, 0.74, -0.04], 0.04, { face: { kind: K_PLAIN, seed: 108, ...P_OXIDE } });

  // junction box on the other flank, amber-lined seam, clamped to the riser
  octBox(-0.21, 0.52, 0.06, 0.17, 0.26, 0.14, 0.02, {
    px: { kind: K_PLAIN, seed: 104, ...P_DARK }, nx: { kind: K_PLAIN, seed: 104, ...P_DARK }, nz: { kind: K_PLAIN, seed: 104, ...P_DARK },
    pz: { kind: K_SEAM, seed: 105, params: [1, 0, 0, 0], accent: A_AMBER },
    diag: { kind: K_PLAIN, seed: 104, ...P_DARK },
    py: { kind: K_PLAIN, seed: 104, ...P_DARK }, ny: { kind: K_PLAIN, seed: 104, ...P_DARK },
  });
  plainBox(-0.145, 0.52, 0.03, 0.06, 0.10, 0.06, 106);

  // gauge dials on the drum front -- under 80 mm, the picture is legal
  for (const x of [-0.085, 0.085]) {
    box(x, 1.17, 0.21, 0.075, 0.075, 0.04, {
      pz: { kind: K_VALVE, seed: 107, params: [3, 0, 0, 0], chamfer: 0.006 },
      px: { kind: K_PLAIN, seed: 107, chamfer: 0 }, nx: { kind: K_PLAIN, seed: 107, chamfer: 0 },
      py: { kind: K_PLAIN, seed: 107, chamfer: 0 }, ny: { kind: K_PLAIN, seed: 107, chamfer: 0 },
    });
  }
});

// ---- 6. MEDBAY CABINET -- 의무실 (no combat, environmental storytelling) --
// A monitor recessed in a proud bezel over three real drawer fronts,
// a sloped cornice, and an oxygen bottle clamped to the flank with its
// regulator -- the "still running" green strip glowing under the hood.
propDef('Medbay Cabinet', 'medbay', '의무실 · 생명유지 캐비닛 · 0.7 x 1.6 x 0.46 m', [0.7, 1.6, 0.46], (c) => {
  const { octBox, hull8, rectRing, tube, box, plainBox, panel, K_PANEL, K_SCREEN, K_SEAM, K_RIB, K_PLAIN, A_LIVE_G, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // inset plinth, closed on top
  hull8(rectRing(0, 0, 0.30, 0.165, 0.0), rectRing(0, 0, 0.325, 0.19, 0.06),
    { px: plain(111), nx: plain(111), pz: plain(111), nz: plain(111), py: plain(111) });

  // body; front face is the recess backing
  octBox(0, 0.74, 0, 0.70, 1.36, 0.40, 0.045, {
    px: { kind: K_PANEL, seed: 112, params: [1, 0, 0, 0], ...P_BONE }, nx: { kind: K_PANEL, seed: 112, params: [1, 0, 0, 0], ...P_BONE },
    pz: { kind: K_PLAIN, seed: 113, ...P_BONE }, nz: plain(113),
    diag: { kind: K_PLAIN, seed: 114, ...P_BONE },
  });

  // bezel frame around the monitor, 25 mm proud
  const bz = 0.2125, bd = 0.025;
  box(0, 1.335, bz, 0.62, 0.07, bd, { pz: plain(115), py: plain(115), ny: plain(115) });
  box(0, 0.755, bz, 0.62, 0.07, bd, { pz: plain(115), py: plain(115), ny: plain(115) });
  for (const s of [-1, 1]) box(s * 0.285, 1.045, bz, 0.05, 0.65, bd, { pz: plain(115), px: plain(115), nx: plain(115) });
  // the monitor itself, proud of the backing but inside the bezel
  panel([-0.26, 0.79, 0.215], AX, AY, AZ, 0.52, 0.51, { kind: K_SCREEN, seed: 116, accent: A_LIVE_G, chamfer: 0.012 });

  // three drawer fronts below, each 20 mm proud with a real pull bar
  for (let i = 0; i < 3; i++) {
    const y0 = 0.10 + i * 0.21;
    panel([-0.28, y0, 0.22], AX, AY, AZ, 0.56, 0.185, { kind: K_PANEL, seed: 117 + i, chamfer: 0.012, ...P_BONE });
    plainBox(0, y0 + 0.093, 0.245, 0.26, 0.028, 0.028, 120);
    for (const x of [-0.11, 0.11]) plainBox(x, y0 + 0.093, 0.232, 0.026, 0.026, 0.026, 120);
  }
  // life-line strip, green, emissive, just under the hood
  panel([-0.26, 1.36, 0.202], AX, AY, AZ, 0.52, 0.045, { kind: K_SEAM, seed: 121, params: [1, 0, 0, 0], accent: A_LIVE_G, chamfer: 0 });

  // cornice hood: sits straight on the body's top, front slope, closes it
  hull8(rectRing(0, 0, 0.37, 0.215, 1.42), rectRing(0, -0.05, 0.35, 0.15, 1.56), {
    px: { kind: K_PLAIN, seed: 122, ...P_BONE }, nx: { kind: K_PLAIN, seed: 122, ...P_BONE }, nz: plain(122),
    pz: { kind: K_RIB, seed: 123, params: [5, 0.06, 0, 0], ...P_BONE },
    py: { kind: K_PLAIN, seed: 124, share: false, ...P_BONE },
    ny: plain(122),
  });

  // oxygen bottle on the flank: pale steel tube + neck + clamps + regulator
  tube([0.395, 0.16, 0.02], [0.395, 0.92, 0.02], 0.055, { face: { kind: K_PLAIN, seed: 125, ...P_STEEL }, capA: true });
  tube([0.395, 0.92, 0.02], [0.395, 0.99, 0.02], 0.032, { face: { kind: K_PLAIN, seed: 125, ...P_STEEL }, capB: true });
  for (const y of [0.32, 0.74]) plainBox(0.375, y, 0.02, 0.10, 0.05, 0.13, 126);
  plainBox(0.395, 1.03, 0.02, 0.09, 0.07, 0.07, 127);
});

// ---- 7. CREW BENCH -- 로비 / 식당 / 숙소 (hub, mess, quarters) --------
// Two tapered pedestals on foot flanges, a through-beam, and a thick
// chamfered seat slab with two raised pads -- furniture with a
// silhouette instead of a plank on sticks.
propDef('Crew Bench', 'lobby', '로비 · 식당 · 숙소 · 1.4 x 0.47 x 0.44 m', [1.4, 0.47, 0.44], (c) => {
  const { hull8, rectRing, box, plainBox, panel, K_PANEL, K_SEAM, K_PLAIN, AX, AZ, AY } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // pedestals: taper up, on wider foot slabs
  for (const x of [-0.55, 0.55]) {
    plainBox(x, 0.01, 0, 0.26, 0.02, 0.44, 131);
    hull8(rectRing(x, 0, 0.095, 0.20, 0.02), rectRing(x, 0, 0.07, 0.165, 0.36), {
      px: { kind: K_SEAM, seed: 132, params: [1, 0, 0, 0] }, nx: { kind: K_SEAM, seed: 132, params: [1, 0, 0, 0] },
      pz: plain(133), nz: plain(133),
    });
  }
  // through-beam
  box(0, 0.29, 0, 1.14, 0.07, 0.10, {
    pz: { kind: K_SEAM, seed: 134, params: [1, 0, 0, 0], chamfer: 0.008 }, nz: plain(133),
    py: plain(133), ny: plain(133),
  });

  // seat slab: 45-degree chamfer all round the top edge
  hull8(rectRing(0, 0, 0.70, 0.21, 0.36), rectRing(0, 0, 0.655, 0.165, 0.44), {
    px: plain(135), nx: plain(135), pz: plain(135), nz: plain(135),
    py: { kind: K_PLAIN, seed: 136, share: false },
    ny: plain(135),
  });
  // two seat pads, proud of the cap by more than their chamfer, painted
  panel([-0.62, 0.455, -0.15], AX, AZ, AY, 0.56, 0.30, { kind: K_PANEL, seed: 137, chamfer: 0.012, ...P_HULL });
  panel([0.06, 0.455, -0.15], AX, AZ, AY, 0.56, 0.30, { kind: K_PANEL, seed: 138, chamfer: 0.012, ...P_HULL });
});

// ---- 8. GROW RACK -- 수경 정원 (garden, the room's repeated asset) ------
// Octagonal posts, real tapered trough trays with the crop breaking
// their rim, and a flared luminaire hood hung under every tray above,
// whose downward face is the glowing grow-light -- A LIT PANEL MUST
// ALSO BE A LAMP, with the lamp now shaped like one.
propDef('Grow Rack', 'garden', '수경 정원 · 반복 배치 · 1.10 x 1.92 x 0.55 m', [1.10, 1.92, 0.55], (c) => {
  const { hull8, rectRing, tube, plainBox, panel, K_SCREEN, K_SEAM, K_PLAIN, K_FOLIAGE, A_LIVE_G, AX, AZ, AY } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // four octagonal posts on foot pads, tied by a top frame
  for (const x of [-0.50, 0.50]) for (const z of [-0.22, 0.22]) {
    plainBox(x, 0.01, z, 0.09, 0.02, 0.09, 141);
    tube([x, 0.02, z], [x, 1.86, z], 0.032, { face: { kind: K_PLAIN, seed: 142 } });
  }
  for (const z of [-0.22, 0.22]) plainBox(0, 1.885, z, 1.06, 0.05, 0.05, 143);
  for (const x of [-0.50, 0.50]) plainBox(x, 1.885, 0, 0.05, 0.05, 0.39, 143);

  const TIERS = [0.45, 0.95, 1.45];
  let seed = 150;
  for (const ty of TIERS) {
    // trough: flares outward going up, closed underneath
    hull8(rectRing(0, 0, 0.44, 0.16, ty - 0.12), rectRing(0, 0, 0.50, 0.21, ty), {
      px: { kind: K_PLAIN, seed: 144, ...P_DARK }, nx: { kind: K_PLAIN, seed: 144, ...P_DARK },
      pz: { kind: K_SEAM, seed: seed++, params: [1, 0, 0, 0], share: false, ...P_DARK },
      nz: { kind: K_PLAIN, seed: 144, ...P_DARK },
      ny: { kind: K_PLAIN, seed: 144, ...P_DARK },
    });
    // crop: two overlapping foliage sheets breaking the rim line
    panel([-0.46, ty - 0.005, -0.185], AX, AZ, AY, 0.92, 0.37, { kind: K_FOLIAGE, seed: seed++, chamfer: 0 });
    panel([-0.30, ty + 0.045, -0.11], AX, AZ, AY, 0.62, 0.24, { kind: K_FOLIAGE, seed: seed++, chamfer: 0 });
  }
  // luminaire hoods: wide mouth below narrowing up to the tray / frame
  // above; the downward face is the lamp
  for (const hy of [0.77, 1.27, 1.80]) {
    hull8(rectRing(0, 0, 0.50, 0.20, hy), rectRing(0, 0, 0.44, 0.15, hy + 0.06), {
      px: plain(145), nx: plain(145), pz: plain(145), nz: plain(145),
      py: plain(145),
      ny: { kind: K_SCREEN, seed: 146, accent: A_LIVE_G, share: false },
    });
  }

  // drip line: down the back-left post with a junction + feed stub per
  // tier, in the ref's oxide red so the run reads against the dark rack
  tube([-0.50, 0.04, -0.27], [-0.50, 1.80, -0.27], 0.022, { face: { kind: K_PLAIN, seed: 147, ...P_OXIDE } });
  for (const ty of TIERS) {
    plainBox(-0.50, ty + 0.03, -0.27, 0.06, 0.06, 0.06, 148, P_OXIDE);
    tube([-0.50, ty + 0.03, -0.27], [-0.50, ty + 0.03, -0.17], 0.022, { face: { kind: K_PLAIN, seed: 147, ...P_OXIDE } });
  }
});

// ---- 9. GARDEN TREE -- 수경 정원 centerpiece, once -----------------------
// Still no sculpted organic mesh -- the ship's vocabulary is the
// operator machine's -- but the trunk is now ONE continuous lofted
// octagonal stack: each ring smaller and off-centre from the last, so
// the taper and the lean are real surfaces, not stacked boxes with
// exposed ledges. Two lofted branches reach into a canopy of many
// small jittered lobes: every corner is pushed off the box before the
// bake's leaf bump ever runs, and a dense scatter of small lobes reads
// as a crown where a few big cubes would read as a few big cubes.
propDef('Garden Tree', 'garden', '수경 정원 · 중앙 식재 · 3.6 x 4.9 x 3.6 m', [3.6, 4.9, 3.6], (c) => {
  const { octBox, hullRing, capPoly, ringAt, tube, hull8, panel, K_RIB, K_HAZARD, K_FOLIAGE, K_PLAIN,
          A_OCHRE, AX, AZ, AY } = c;

  // planter curb: chamfered crown, hazard cardinal faces
  octBox(0, 0.21, 0, 1.90, 0.42, 1.90, 0.22, {
    px: { kind: K_HAZARD, seed: 161, params: [0.12, 0, 0, 0], accent: A_OCHRE },
    nx: { kind: K_HAZARD, seed: 161, params: [0.12, 0, 0, 0], accent: A_OCHRE },
    pz: { kind: K_HAZARD, seed: 162, params: [0.12, 0, 0, 0], accent: A_OCHRE },
    nz: { kind: K_HAZARD, seed: 162, params: [0.12, 0, 0, 0], accent: A_OCHRE },
    diag: { kind: K_PLAIN, seed: 163 },
    topCut: 0.08,
    py: { kind: K_PLAIN, seed: 164 },
  });
  // soil: foliage sheet just above the rim cap
  panel([-0.72, 0.425, -0.72], AX, AZ, AY, 1.44, 1.44, { kind: K_FOLIAGE, seed: 165, chamfer: 0 });

  // trunk: one continuous loft through five rings, bark = K_RIB fluting
  const RINGS = [
    [0.00, 0.40, 0.00, 0.50], [0.07, 1.30, 0.04, 0.36],
    [-0.03, 2.20, 0.09, 0.27], [0.06, 3.00, -0.03, 0.19],
    [0.02, 3.55, 0.02, 0.13],
  ];
  const rings = RINGS.map(([x, y, z, r]) => ringAt([x, y, z], AZ, AX, r, 8, Math.PI / 8));
  let seed = 170;
  for (let i = 0; i + 1 < rings.length; i++) {
    hullRing(rings[i], rings[i + 1], { kind: K_RIB, seed: seed++, params: [3, 0.10, 0, 0] });
  }
  capPoly(rings[rings.length - 1], { kind: K_PLAIN, seed: 179 });

  // two branches, lofted off the trunk into the canopy
  tube([0.15, 2.75, 0.05], [1.05, 3.55, 0.45], 0.11, { face: { kind: K_RIB, seed: 180, params: [2, 0.10, 0, 0] } });
  tube([-0.10, 2.35, -0.02], [-0.95, 3.25, -0.55], 0.09, { face: { kind: K_RIB, seed: 181, params: [2, 0.10, 0, 0] } });

  // canopy: eleven small jittered lobes scattered round the trunk's top
  // and over both branch tips
  const LOBES = [
    [0.00, 3.70, 0.00, 1.65, 0.85, 1.55], [0.85, 3.85, 0.35, 1.05, 0.75, 1.15],
    [-0.80, 3.80, -0.25, 1.10, 0.70, 1.00], [0.20, 3.75, 0.90, 1.15, 0.70, 1.05],
    [-0.35, 3.70, -0.95, 1.00, 0.65, 1.10], [0.55, 4.35, -0.55, 0.90, 0.65, 0.90],
    [-0.65, 4.30, 0.50, 0.85, 0.60, 0.95], [0.05, 4.55, 0.10, 0.95, 0.55, 0.90],
    [0.75, 4.05, -0.85, 0.75, 0.55, 0.75], [-0.85, 4.10, 0.75, 0.70, 0.55, 0.70],
    [-0.90, 3.45, -0.60, 0.85, 0.60, 0.85],
  ];
  seed = 190;
  for (const [cx, cy, cz, w, h, d] of LOBES) {
    const hw = w / 2, hh = h / 2, hd = d / 2;
    const amp = Math.min(hw, hh, hd) * 0.35;
    const jit = (k, ax) => jHash(seed * 8 + k * 3 + ax) * amp;
    const B = [], T = [];
    const bx = [hw, hw, -hw, -hw], bz = [hd, -hd, -hd, hd];
    for (let k = 0; k < 4; k++) {
      B.push([cx + bx[k] + jit(k, 0), cy - hh + jit(k, 1), cz + bz[k] + jit(k, 2)]);
      T.push([cx + bx[k] * 0.82 + jit(k + 4, 0), cy + hh + jit(k + 4, 1), cz + bz[k] * 0.82 + jit(k + 4, 2)]);
    }
    const fo = i => ({ kind: K_FOLIAGE, seed: seed + i, share: false });
    hull8(B, T, { px: fo(0), nz: fo(1), nx: fo(2), pz: fo(3), py: fo(4), ny: fo(5) });
    seed += 7;
  }
});

/* ============================================================
   ROOM-DRESSING SET (props 10-19). The first nine anchor their
   rooms; these fill them. Same grammar, same paint discipline:
   painted shells over bare machined frames, accents only where a
   role earns them (ochre = handling/hazard, amber = powered,
   live green/cyan = still running).
   ============================================================ */

// ---- 10. BUNK BED -- 승무원 숙소 (crew quarters, tutorial start) --------
// Two chamfered end pylons carry both bunk decks; every deck edge is a
// bolted seam rail, the mattresses are clinical-shell pads proud of the
// bare decks, and the space under the lower bunk is a real drawer unit
// -- the first thing the player sees, so it gets the full grammar.
propDef('Bunk Bed', 'quarters', '승무원 숙소 · 2단 침대 · 2.17 x 1.63 x 0.98 m', [2.17, 1.63, 0.98], (c) => {
  const { octBox, hull8, rectRing, tube, box, plainBox, panel, K_PANEL, K_SEAM, K_PLAIN, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // end pylons on foot slabs -- the whole bed hangs between them
  for (const s of [-1, 1]) {
    const x = s * 0.975;
    plainBox(x, 0.015, 0, 0.22, 0.03, 0.98, 201);
    octBox(x, 0.83, 0, 0.12, 1.60, 0.90, 0.03, {
      px: { kind: K_PANEL, seed: 202, params: [1, 0, 0, 0], ...P_HULL },
      nx: { kind: K_PANEL, seed: 202, params: [1, 0, 0, 0], ...P_HULL },
      pz: plain(203), nz: plain(203), diag: plain(203),
      topCut: 0.04,
      py: plain(204),
    });
  }

  // bunk decks, ends buried in the pylons; undersides close them
  const bone = { kind: K_PLAIN, seed: 209, share: true, ...P_BONE };
  for (const dy of [0.42, 1.12]) {
    hull8(rectRing(0, 0, 0.93, 0.44, dy - 0.10), rectRing(0, 0, 0.90, 0.41, dy), {
      pz: { kind: K_SEAM, seed: 205, params: [1, 0, 0, 0], share: false },
      nz: { ...plain(206), share: true },
      py: { kind: K_PLAIN, seed: 207, share: false },
      ny: { ...plain(206), share: true },
    });
    // mattress pad + pillow wedge, clinical shell over the bare deck
    panel([-0.80, dy + 0.02, -0.36], AX, AZ, AY, 1.60, 0.72, { kind: K_PANEL, seed: 208, params: [1, 0, 0, 0], chamfer: 0.012, ...P_BONE });
    hull8(rectRing(-0.70, 0, 0.16, 0.31, dy + 0.02), rectRing(-0.70, 0, 0.13, 0.27, dy + 0.095), {
      px: bone, nx: bone, pz: bone, nz: bone,
      py: { kind: K_PLAIN, seed: 209, ...P_BONE },
    });
  }

  // drawer unit under the lower bunk, twin fronts proud of its backing
  octBox(0, 0.17, 0, 1.86, 0.30, 0.84, 0.03, {
    pz: plain(210), nz: plain(210), diag: plain(210), ny: plain(210),
  });
  for (const s of [-1, 1]) {
    const x0 = s > 0 ? 0.08 : -0.86;
    panel([x0, 0.055, 0.44], AX, AY, AZ, 0.78, 0.23, { kind: K_PANEL, seed: 211, params: [1, 0, 0, 0], chamfer: 0.012, ...P_DARK });
    plainBox(s * 0.47, 0.17, 0.455, 0.24, 0.028, 0.028, 212);
    for (const hx of [s * 0.37, s * 0.57]) plainBox(hx, 0.17, 0.443, 0.026, 0.026, 0.026, 212);
  }

  // upper-bunk safety rail + ladder, small bare hardware
  for (const x of [-0.55, 0.55]) tube([x, 1.12, 0.40], [x, 1.32, 0.40], 0.016, { face: plain(213) });
  tube([-0.55, 1.31, 0.40], [0.55, 1.31, 0.40], 0.018, { face: plain(213), capA: true, capB: true });
  for (const x of [0.62, 0.86]) {
    plainBox(x, 0.01, 0.46, 0.06, 0.02, 0.06, 214);
    tube([x, 0.02, 0.46], [x, 1.34, 0.46], 0.016, { face: plain(214), capB: true });
  }
  for (let i = 0; i < 5; i++) plainBox(0.74, 0.22 + i * 0.26, 0.46, 0.26, 0.03, 0.03, 215);
});

// ---- 11. FOOT LOCKER -- 승무원 숙소 (per-berth stowage) ----------------
// Distinct from the Supply Crate on silhouette alone: a sloped bare
// lid overhanging a dark painted shell, latch blocks bridging the lid
// seam, and a bar handle on each end.
propDef('Foot Locker', 'quarters', '승무원 숙소 · 개인 로커 · 0.84 x 0.53 x 0.50 m', [0.84, 0.53, 0.50], (c) => {
  const { octBox, hull8, rectRing, tube, box, plainBox, panel, K_PANEL, K_SEAM, K_PLAIN, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // inset base, closed on top
  hull8(rectRing(0, 0, 0.34, 0.19, 0.0), rectRing(0, 0, 0.36, 0.21, 0.035),
    { px: plain(221), nx: plain(221), pz: plain(221), nz: plain(221), py: plain(221) });

  // body, painted gunmetal shell
  octBox(0, 0.2225, 0, 0.76, 0.375, 0.44, 0.035, {
    px: { kind: K_PANEL, seed: 222, params: [1, 0, 0, 0], ...P_DARK },
    nx: { kind: K_PANEL, seed: 222, params: [1, 0, 0, 0], ...P_DARK },
    pz: { kind: K_PANEL, seed: 223, params: [1, 0, 0, 0], ...P_DARK },
    nz: { kind: K_PANEL, seed: 224, params: [1, 0, 0, 0], ...P_DARK },
    diag: plain(225),
  });

  // sloped lid: overhangs the body, front face leans back, bare metal
  hull8(rectRing(0, 0.0, 0.40, 0.245, 0.41), rectRing(0, -0.03, 0.36, 0.185, 0.53), {
    px: plain(226), nx: plain(226), pz: plain(226), nz: plain(226),
    py: { kind: K_PANEL, seed: 227, params: [1, 0, 0, 0], share: false },
    ny: plain(226),
  });

  // latch blocks bridging the lid seam + stencil plate + end handles
  for (const x of [-0.20, 0.20]) box(x, 0.36, 0.235, 0.09, 0.10, 0.035, {
    pz: { kind: K_SEAM, seed: 228, params: [1, 0, 0, 0], chamfer: 0.006 },
    px: { ...plain(228), chamfer: 0 }, nx: { ...plain(228), chamfer: 0 },
    py: { ...plain(228), chamfer: 0 }, ny: { ...plain(228), chamfer: 0 },
  });
  panel([-0.14, 0.13, 0.235], AX, AY, AZ, 0.28, 0.10, { kind: K_SEAM, seed: 229, params: [1, 0, 0, 0], chamfer: 0.008 });
  for (const s of [-1, 1]) {
    for (const z of [-0.09, 0.09]) plainBox(s * 0.395, 0.24, z, 0.035, 0.035, 0.035, 230);
    tube([s * 0.415, 0.24, -0.09], [s * 0.415, 0.24, 0.09], 0.013, { face: plain(230), capA: true, capB: true });
  }
});

// ---- 12. MESS TABLE -- 식당 (mess hall, pairs with the Crew Bench) -----
// The bench's own language stretched to a table: tapered pedestals on
// foot slabs, a through-beam, a thick chamfered top, and two painted
// tray stations either side of a bare centre strip with a service block.
propDef('Mess Table', 'mess', '식당 · 4인 테이블 · 1.60 x 0.88 x 0.85 m', [1.60, 0.88, 0.85], (c) => {
  const { hull8, rectRing, octBox, box, plainBox, panel, K_PANEL, K_SEAM, K_PLAIN, AX, AZ, AY } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  for (const x of [-0.52, 0.52]) {
    plainBox(x, 0.01, 0, 0.30, 0.02, 0.62, 231);
    hull8(rectRing(x, 0, 0.105, 0.27, 0.02), rectRing(x, 0, 0.075, 0.20, 0.66), {
      px: { kind: K_SEAM, seed: 232, params: [1, 0, 0, 0] }, nx: { kind: K_SEAM, seed: 232, params: [1, 0, 0, 0] },
      pz: plain(233), nz: plain(233),
    });
  }
  box(0, 0.57, 0, 1.06, 0.08, 0.12, {
    pz: { kind: K_SEAM, seed: 234, params: [1, 0, 0, 0], chamfer: 0.008 }, nz: plain(233),
    py: plain(233), ny: plain(233),
  });

  // top slab, 45-degree chamfer all round, closed underneath
  hull8(rectRing(0, 0, 0.80, 0.425, 0.66), rectRing(0, 0, 0.755, 0.38, 0.78), {
    px: plain(235), nx: plain(235), pz: plain(235), nz: plain(235),
    py: { kind: K_PLAIN, seed: 236, share: false },
    ny: plain(235),
  });
  // two tray stations + centre service block
  panel([-0.70, 0.795, -0.33], AX, AZ, AY, 0.60, 0.66, { kind: K_PANEL, seed: 237, chamfer: 0.012, ...P_HULL });
  panel([0.10, 0.795, -0.33], AX, AZ, AY, 0.60, 0.66, { kind: K_PANEL, seed: 238, chamfer: 0.012, ...P_HULL });
  octBox(0, 0.83, 0, 0.16, 0.10, 0.30, 0.02, {
    px: plain(239), nx: plain(239),
    pz: { ...plain(239), ...P_DARK }, nz: { ...plain(239), ...P_DARK },
    diag: plain(239), py: plain(239),
  });
});

// ---- 13. RATION DISPENSER -- 식당 (the ship's vending machine) ---------
// A console-grade front on a locker-grade body: recessed menu screen
// and keypad behind a proud bezel, a shuttered dispense hatch low down
// inside its own frame, amber brow, vent hood crown, conduit down the
// back. Doubles as lobby dressing.
propDef('Ration Dispenser', 'mess', '식당 · 배식 자판기 · 0.94 x 1.76 x 0.68 m', [0.94, 1.76, 0.68], (c) => {
  const { octBox, hull8, rectRing, tube, box, plainBox, panel, K_PANEL, K_RIB, K_SCREEN, K_BUTTONS, K_SEAM, K_PLAIN,
          A_AMBER, A_LIVE_C, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  hull8(rectRing(0, 0, 0.38, 0.25, 0.0), rectRing(0, 0, 0.40, 0.27, 0.06),
    { px: plain(241), nx: plain(241), pz: plain(241), nz: plain(241), py: plain(241) });

  // body: painted flanks and back, bare front backing for the bays
  octBox(0, 0.83, 0, 0.88, 1.54, 0.60, 0.05, {
    px: { kind: K_PANEL, seed: 242, params: [1, 0, 0, 0], ...P_HULL },
    nx: { kind: K_PANEL, seed: 242, params: [1, 0, 0, 0], ...P_HULL },
    pz: plain(243),
    nz: { kind: K_PANEL, seed: 244, params: [1, 0, 0, 0], ...P_HULL },
    diag: plain(245),
  });

  // display bay: proud bezel frame, screen + keypad + card slot inside
  const fz = 0.3125, fd = 0.025;
  box(0, 1.52, fz, 0.80, 0.06, fd, { pz: plain(246), py: plain(246), ny: plain(246) });
  box(0, 0.94, fz, 0.80, 0.06, fd, { pz: plain(246), py: plain(246), ny: plain(246) });
  for (const s of [-1, 1]) box(s * 0.375, 1.23, fz, 0.05, 0.64, fd, { pz: plain(246), px: plain(246), nx: plain(246) });
  panel([-0.32, 1.10, 0.315], AX, AY, AZ, 0.64, 0.36, { kind: K_SCREEN, seed: 247, accent: A_LIVE_C, chamfer: 0.012 });
  panel([-0.32, 0.99, 0.315], AX, AY, AZ, 0.40, 0.09, { kind: K_BUTTONS, seed: 248, chamfer: 0.008 });
  box(0.22, 1.035, 0.315, 0.16, 0.09, 0.03, {
    pz: { kind: K_SEAM, seed: 249, params: [1, 0, 0, 0], chamfer: 0.006 },
    px: { ...plain(249), chamfer: 0 }, nx: { ...plain(249), chamfer: 0 },
    py: { ...plain(249), chamfer: 0 }, ny: { ...plain(249), chamfer: 0 },
  });

  // dispense hatch: its own frame, dark shuttered leaf proud of the
  // backing but recessed against the frame
  box(0, 0.64, fz, 0.72, 0.05, fd, { pz: plain(246), py: plain(246), ny: plain(246) });
  box(0, 0.22, fz, 0.72, 0.05, fd, { pz: plain(246), py: plain(246), ny: plain(246) });
  for (const s of [-1, 1]) box(s * 0.335, 0.43, fz, 0.05, 0.37, fd, { pz: plain(246), px: plain(246), nx: plain(246) });
  panel([-0.29, 0.26, 0.316], AX, AY, AZ, 0.58, 0.33, { kind: K_RIB, seed: 250, params: [8, 0, 0, 0], chamfer: 0.012, ...P_DARK });

  // amber brow + vent hood crown
  panel([-0.36, 1.545, 0.302], AX, AY, AZ, 0.72, 0.045, { kind: K_SEAM, seed: 251, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  hull8(rectRing(0, 0, 0.47, 0.33, 1.60), rectRing(0, -0.055, 0.44, 0.24, 1.76), {
    px: plain(252), nx: plain(252), nz: plain(252),
    pz: { kind: K_RIB, seed: 253, params: [6, 0, 0, 0] },
    py: { kind: K_PLAIN, seed: 254, share: false },
    ny: plain(252),
  });

  // conduit down the back, clamped, into a junction box
  tube([0.20, 0.02, -0.30], [0.20, 1.34, -0.30], 0.028, { face: { ...plain(255), ...P_STEEL } });
  for (const y of [0.35, 0.95]) plainBox(0.20, y, -0.285, 0.09, 0.05, 0.05, 256);
  box(0.20, 1.38, -0.28, 0.14, 0.12, 0.08, {
    nz: { kind: K_SEAM, seed: 257, params: [1, 0, 0, 0], chamfer: 0.008 },
    px: { ...plain(257), chamfer: 0 }, nx: { ...plain(257), chamfer: 0 },
    py: { ...plain(257), chamfer: 0 }, ny: { ...plain(257), chamfer: 0 },
  });
});

// ---- 14. MED BED -- 의무실 (medbay, environmental storytelling) --------
// A clinical-shell pedestal under a bare chamfered platform, mattress
// and pillow in bone shell, side rails, and an over-bed scanner whose
// hood's downward face is the lamp -- A LIT PANEL MUST ALSO BE A LAMP.
// The pedestal's green life-line strip says the bed is still powered.
propDef('Med Bed', 'medbay', '의무실 · 진료 베드 · 2.04 x 1.32 x 0.95 m', [2.04, 1.32, 0.95], (c) => {
  const { octBox, hull8, rectRing, tube, box, plainBox, panel, K_PANEL, K_SCREEN, K_SEAM, K_PLAIN,
          A_LIVE_G, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  hull8(rectRing(0, 0, 0.52, 0.27, 0.0), rectRing(0, 0, 0.55, 0.30, 0.06),
    { px: plain(261), nx: plain(261), pz: plain(261), nz: plain(261), py: plain(261) });

  octBox(0, 0.29, 0, 0.95, 0.46, 0.50, 0.06, {
    px: { kind: K_PANEL, seed: 262, params: [1, 0, 0, 0], ...P_BONE },
    nx: { kind: K_PANEL, seed: 262, params: [1, 0, 0, 0], ...P_BONE },
    pz: { kind: K_PANEL, seed: 263, params: [1, 0, 0, 0], ...P_BONE },
    nz: plain(264),
    diag: { ...plain(264), ...P_BONE },
  });
  panel([-0.30, 0.36, 0.252], AX, AY, AZ, 0.60, 0.045, { kind: K_SEAM, seed: 265, params: [1, 0, 0, 0], accent: A_LIVE_G, chamfer: 0 });

  // platform: overhangs the pedestal, chamfered all round, closed below
  hull8(rectRing(0, 0, 1.02, 0.44, 0.52), rectRing(0, 0, 0.98, 0.40, 0.62), {
    px: plain(266), nx: plain(266), nz: plain(266),
    pz: { kind: K_SEAM, seed: 267, params: [1, 0, 0, 0], share: false },
    py: { kind: K_PLAIN, seed: 268, share: false },
    ny: plain(266),
  });
  panel([-0.92, 0.64, -0.36], AX, AZ, AY, 1.84, 0.72, { kind: K_PANEL, seed: 269, params: [1, 0, 0, 0], chamfer: 0.012, ...P_BONE });
  const bone = { kind: K_PLAIN, seed: 270, share: true, ...P_BONE };
  hull8(rectRing(-0.72, 0, 0.16, 0.31, 0.64), rectRing(-0.72, 0, 0.13, 0.27, 0.71), {
    px: bone, nx: bone, pz: bone, nz: bone,
    py: { kind: K_PLAIN, seed: 270, ...P_BONE },
  });

  // side rails
  for (const s of [-1, 1]) {
    for (const x of [-0.50, 0.50]) tube([x, 0.62, s * 0.37], [x, 0.80, s * 0.37], 0.016, { face: plain(271) });
    tube([-0.50, 0.79, s * 0.37], [0.50, 0.79, s * 0.37], 0.018, { face: plain(271), capA: true, capB: true });
  }

  // over-bed scanner: mast, elbow, arm, hood; the hood's mouth glows
  tube([-0.92, 0.60, 0], [-0.92, 1.24, 0], 0.045, { face: { ...plain(272), ...P_STEEL } });
  plainBox(-0.92, 1.28, 0, 0.11, 0.11, 0.11, 273);
  tube([-0.92, 1.28, 0], [-0.28, 1.28, 0], 0.038, { face: { ...plain(272), ...P_STEEL }, capB: true });
  const hood = { kind: K_PLAIN, seed: 274, share: true, ...P_BONE };
  hull8(rectRing(-0.28, 0, 0.30, 0.22, 1.14), rectRing(-0.28, 0, 0.24, 0.17, 1.26), {
    px: hood, nx: hood, pz: hood, nz: hood,
    py: plain(274),
    ny: { kind: K_SCREEN, seed: 275, accent: A_LIVE_G, share: false },
  });

  // vitals monitor on a mast at the foot end
  tube([0.90, 0.60, 0.24], [0.90, 1.02, 0.24], 0.028, { face: plain(276) });
  octBox(0.90, 1.12, 0.24, 0.32, 0.26, 0.10, 0.02, {
    pz: { kind: K_SCREEN, seed: 277, accent: A_LIVE_G, share: false },
    nz: { kind: K_PANEL, seed: 278, params: [1, 0, 0, 0], ...P_BONE },
    px: { ...plain(278), ...P_BONE }, nx: { ...plain(278), ...P_BONE },
    diag: plain(278), py: plain(278), ny: plain(278),
  });
});

// ---- 15. AUXILIARY GENERATOR -- 기관실 (engine room machinery) ---------
// A machine that reads as a machine: cooling-fin flanks, a recessed
// amber control face behind a proud bezel, twin flanged exhaust stacks,
// hazard chamfers on the crown, an oxide feed pipe clamped to the
// front, lift lugs on top.
propDef('Auxiliary Generator', 'engine', '기관실 · 보조 발전기 · 1.75 x 1.43 x 0.98 m', [1.75, 1.43, 0.98], (c) => {
  const { octBox, tube, box, plainBox, panel, K_PANEL, K_RIB, K_SCREEN, K_BUTTONS, K_SEAM, K_HAZARD, K_PLAIN,
          A_AMBER, A_OCHRE, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // skid base, hazard toe on the front
  octBox(0, 0.06, 0, 1.70, 0.12, 0.92, 0.08, {
    px: { ...plain(281), ...P_DARK }, nx: { ...plain(281), ...P_DARK },
    pz: { kind: K_HAZARD, seed: 282, params: [0.08, 0, 0, 0], accent: A_OCHRE },
    nz: { ...plain(281), ...P_DARK },
    diag: plain(281),
    topCut: 0.03,
    py: plain(283),
  });

  // main block: fin flanks, painted shells, hazard crown chamfers
  octBox(0, 0.62, 0, 1.45, 1.00, 0.80, 0.07, {
    px: { kind: K_RIB, seed: 284, params: [11, 0, 0, 0] },
    nx: { kind: K_RIB, seed: 284, params: [11, 0, 0, 0] },
    pz: { kind: K_PANEL, seed: 285, params: [1, 0, 0, 0], ...P_DARK },
    nz: { kind: K_PANEL, seed: 286, params: [1, 0, 0, 0], ...P_DARK },
    diag: plain(287),
    topCut: 0.06,
    topOpts: k => (k % 2 === 0)
      ? { kind: K_HAZARD, seed: 288, params: [0.08, 0, 0, 0], accent: A_OCHRE, share: false }
      : plain(287),
    py: plain(289),
  });

  // control bay: bezel frame, amber status screen, keypad
  const fz = 0.4125, fd = 0.025;
  box(0, 0.98, fz, 0.74, 0.05, fd, { pz: plain(290), py: plain(290), ny: plain(290) });
  box(0, 0.55, fz, 0.74, 0.05, fd, { pz: plain(290), py: plain(290), ny: plain(290) });
  for (const s of [-1, 1]) box(s * 0.345, 0.765, fz, 0.05, 0.38, fd, { pz: plain(290), px: plain(290), nx: plain(290) });
  panel([-0.30, 0.66, 0.415], AX, AY, AZ, 0.42, 0.26, { kind: K_SCREEN, seed: 291, accent: A_AMBER, chamfer: 0.012 });
  panel([0.15, 0.66, 0.415], AX, AY, AZ, 0.15, 0.26, { kind: K_BUTTONS, seed: 292, chamfer: 0.008 });

  // twin exhaust stacks with flange collars
  for (const x of [-0.42, 0.42]) {
    tube([x, 1.05, -0.15], [x, 1.42, -0.15], 0.095, { face: { ...plain(293), ...P_STEEL } });
    tube([x, 1.36, -0.15], [x, 1.43, -0.15], 0.12, { face: { kind: K_SEAM, seed: 294, params: [1, 0, 0, 0] }, capA: true, capB: true });
  }

  // oxide feed pipe clamped to the front, elbow into the block
  tube([-0.58, 0.12, 0.42], [-0.58, 0.80, 0.42], 0.042, { face: { ...plain(295), ...P_OXIDE } });
  plainBox(-0.58, 0.84, 0.42, 0.10, 0.10, 0.10, 296, P_OXIDE);
  tube([-0.58, 0.84, 0.42], [-0.58, 0.84, 0.30], 0.042, { face: { ...plain(295), ...P_OXIDE } });
  for (const y of [0.30, 0.62]) plainBox(-0.58, y, 0.405, 0.09, 0.05, 0.05, 297);

  // lift lugs on the crown
  for (const s of [-1, 1]) plainBox(s * 0.55, 1.145, 0.20, 0.12, 0.05, 0.08, 298);
});

// ---- 16. COOLANT TANK -- 기관실 (engine room, also cargo overflow) -----
// A horizontal drum in painted pipe steel on two hazard-toed saddle
// cradles over a bare skid: girth collars with bolt rows, a flanged
// top hatch, a front offtake with a real ochre hand-wheel, a gauge
// dial under 80 mm, and an oxide drain dropping into the skid.
propDef('Coolant Tank', 'engine', '기관실 · 냉각제 탱크 · 1.86 x 1.05 x 0.94 m', [1.86, 1.05, 0.94], (c) => {
  const { hull8, rectRing, tube, wheelZ, box, plainBox, K_SEAM, K_HAZARD, K_VALVE, K_PLAIN, A_OCHRE } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // skid rails + cross members
  for (const z of [-0.26, 0.26]) plainBox(0, 0.035, z, 1.64, 0.07, 0.10, 301);
  for (const x of [-0.60, 0.60]) plainBox(x, 0.035, 0, 0.12, 0.07, 0.62, 301);

  // saddle cradles, hazard toe forward
  for (const x of [-0.55, 0.55]) {
    hull8(rectRing(x, 0, 0.13, 0.34, 0.07), rectRing(x, 0, 0.095, 0.27, 0.40), {
      px: { ...plain(302), ...P_DARK }, nx: { ...plain(302), ...P_DARK },
      pz: { kind: K_HAZARD, seed: 303, params: [0.08, 0, 0, 0], accent: A_OCHRE, share: false },
      nz: { ...plain(302), ...P_DARK },
      py: plain(302),
    });
  }

  // tank drum + girth collars
  tube([-0.80, 0.60, 0], [0.80, 0.60, 0], 0.355, { face: { ...plain(304), ...P_STEEL }, capA: true, capB: true });
  for (const x of [-0.72, 0, 0.72]) {
    tube([x - 0.035, 0.60, 0], [x + 0.035, 0.60, 0], 0.375, { face: { kind: K_SEAM, seed: 305, params: [1, 0, 0, 0] }, capA: true, capB: true });
  }

  // top hatch with flange, front offtake with hand-wheel, gauge, drain
  tube([0.30, 0.80, 0], [0.30, 1.00, 0], 0.10, { face: { ...plain(306), ...P_STEEL } });
  tube([0.30, 1.00, 0], [0.30, 1.05, 0], 0.13, { face: { kind: K_SEAM, seed: 307, params: [1, 0, 0, 0] }, capB: true });
  tube([-0.35, 0.60, 0.20], [-0.35, 0.60, 0.44], 0.065, { face: { ...plain(306), ...P_STEEL } });
  wheelZ(-0.35, 0.60, 0.47, 0.14, 0.028, 0.04, { seed: 308, hub: 0.04, ...P_OCHRE });
  box(0.10, 0.78, 0.30, 0.075, 0.075, 0.05, {
    pz: { kind: K_VALVE, seed: 309, params: [3, 0, 0, 0], chamfer: 0.006 },
    px: { ...plain(309), chamfer: 0 }, nx: { ...plain(309), chamfer: 0 },
    py: { ...plain(309), chamfer: 0 }, ny: { ...plain(309), chamfer: 0 },
  });
  tube([-0.30, 0.40, 0.14], [-0.30, 0.03, 0.14], 0.035, { face: { ...plain(310), ...P_OXIDE } });
  plainBox(-0.30, 0.05, 0.14, 0.09, 0.09, 0.09, 311, P_OXIDE);
});

// ---- 17. CARGO SHELF -- 창고 (storage, decorates itself) ---------------
// An open industrial rack: octagonal posts, chamfered shelf slabs with
// bolted edge rails, an X-braced back -- and the stowed cargo is part
// of the prop, so one placement dresses a wall: painted crates, steel
// and oxide drums, a hazard-fronted hazmat box, a long case up top.
propDef('Cargo Shelf', 'storage', '창고 · 선반 랙 · 1.80 x 1.98 x 0.64 m', [1.80, 1.98, 0.64], (c) => {
  const { hull8, rectRing, octBox, tube, plainBox, strap, K_PANEL, K_SEAM, K_HAZARD, K_PLAIN, A_OCHRE, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // posts on foot pads + top frame
  for (const x of [-0.85, 0.85]) for (const z of [-0.26, 0.26]) {
    plainBox(x, 0.01, z, 0.11, 0.02, 0.11, 321);
    tube([x, 0.02, z], [x, 1.93, z], 0.034, { face: plain(322) });
  }
  for (const z of [-0.26, 0.26]) plainBox(0, 1.955, z, 1.76, 0.05, 0.06, 323);
  for (const x of [-0.85, 0.85]) plainBox(x, 1.955, 0, 0.06, 0.05, 0.46, 323);

  // shelf slabs: chamfer up to the deck, bolted front edge
  for (const sy of [0.26, 0.88, 1.50]) {
    hull8(rectRing(0, 0, 0.88, 0.295, sy - 0.055), rectRing(0, 0, 0.85, 0.27, sy), {
      pz: { kind: K_SEAM, seed: 324, params: [1, 0, 0, 0], share: false },
      nz: { ...plain(325), share: true }, px: { ...plain(325), share: true }, nx: { ...plain(325), share: true },
      py: { kind: K_PLAIN, seed: 326, share: false },
      ny: { ...plain(325), share: true },
    });
  }
  // X-brace across the back
  strap([-0.83, 0.30, -0.295], [0.83, 1.46, -0.295], 0.09, 0.028, NZ, { seed: 327 });
  strap([0.83, 0.30, -0.295], [-0.83, 1.46, -0.295], 0.09, 0.028, NZ, { seed: 327 });

  // shelf 1: armour-green crate + steel drum with collar + oxide drum
  octBox(-0.42, 0.42, 0.02, 0.54, 0.32, 0.42, 0.035, {
    px: { kind: K_PANEL, seed: 328, params: [1, 0, 0, 0], ...P_HULL }, nx: { kind: K_PANEL, seed: 328, params: [1, 0, 0, 0], ...P_HULL },
    pz: { kind: K_PANEL, seed: 329, params: [1, 0, 0, 0], ...P_HULL }, nz: { ...plain(330), ...P_HULL },
    diag: plain(330), py: plain(331),
  });
  tube([0.30, 0.26, 0.04], [0.30, 0.70, 0.04], 0.14, { face: { ...plain(332), ...P_STEEL }, capB: true });
  tube([0.30, 0.54, 0.04], [0.30, 0.59, 0.04], 0.15, { face: { kind: K_SEAM, seed: 333, params: [1, 0, 0, 0] } });
  tube([0.62, 0.26, 0.00], [0.62, 0.62, 0.00], 0.115, { face: { ...plain(332), ...P_OXIDE }, capB: true });

  // shelf 2: stacked gunmetal boxes + hazard-fronted oxide hazmat crate
  plainBox(-0.50, 1.01, -0.02, 0.46, 0.26, 0.40, 334, P_DARK);
  plainBox(-0.50, 1.18, 0.04, 0.34, 0.08, 0.28, 335);
  octBox(0.28, 1.025, 0.02, 0.60, 0.29, 0.38, 0.03, {
    px: { kind: K_PANEL, seed: 336, params: [1, 0, 0, 0], ...P_OXIDE }, nx: { kind: K_PANEL, seed: 336, params: [1, 0, 0, 0], ...P_OXIDE },
    pz: { kind: K_HAZARD, seed: 337, params: [0.10, 0, 0, 0], accent: A_OCHRE }, nz: { ...plain(338), ...P_OXIDE },
    diag: plain(338), py: plain(338),
  });

  // shelf 3: long case + short steel drum
  octBox(-0.10, 1.615, 0.03, 0.92, 0.23, 0.36, 0.03, {
    px: { kind: K_PANEL, seed: 339, params: [1, 0, 0, 0], ...P_HULL }, nx: { kind: K_PANEL, seed: 339, params: [1, 0, 0, 0], ...P_HULL },
    pz: { kind: K_SEAM, seed: 340, params: [1, 0, 0, 0], ...P_HULL }, nz: { ...plain(341), ...P_HULL },
    diag: plain(341), py: plain(341),
  });
  tube([0.62, 1.50, -0.04], [0.62, 1.86, -0.04], 0.10, { face: { ...plain(332), ...P_STEEL }, capB: true });
});

// ---- 18. STORAGE DRUM -- 화물칸 / 창고 (the scatter piece) -------------
// One drum, placed in threes: oxide shell between bare rolling hoops,
// an ochre hazard band mid-height, a flanged crown rim with recessed
// lid and filler bung, and a bolted stencil plate bridging the shell.
propDef('Storage Drum', 'cargo', '화물칸 · 창고 · 드럼통 · 0.60 x 0.97 m', [0.60, 0.97, 0.60], (c) => {
  const { tube, box, K_SEAM, K_HAZARD, K_PLAIN, A_OCHRE } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  tube([0, 0, 0], [0, 0.05, 0], 0.295, { face: plain(351) });                       // base chime
  tube([0, 0.05, 0], [0, 0.88, 0], 0.28, { face: { ...plain(352), ...P_OXIDE } });  // shell
  for (const y of [0.30, 0.62]) tube([0, y - 0.03, 0], [0, y + 0.03, 0], 0.295, { face: { kind: K_SEAM, seed: 353, params: [1, 0, 0, 0] } });
  tube([0, 0.42, 0], [0, 0.55, 0], 0.287, { face: { kind: K_HAZARD, seed: 354, params: [0.10, 1, 0, 0], accent: A_OCHRE } });
  tube([0, 0.88, 0], [0, 0.93, 0], 0.295, { face: { kind: K_SEAM, seed: 353, params: [1, 0, 0, 0] }, capB: true });
  tube([0.13, 0.93, 0], [0.13, 0.97, 0], 0.045, { face: plain(355), capB: true });  // filler bung
  box(0, 0.72, 0.29, 0.16, 0.11, 0.04, {
    pz: { kind: K_SEAM, seed: 356, params: [1, 0, 0, 0], chamfer: 0.006 },
    px: { ...plain(356), chamfer: 0 }, nx: { ...plain(356), chamfer: 0 },
    py: { ...plain(356), chamfer: 0 }, ny: { ...plain(356), chamfer: 0 },
  });
});

// ---- 19. LOADED PALLET -- 화물칸 (cargo bay set dressing) --------------
// A pallet that is already loaded: three bearers under a hazard-edged
// chamfered deck (the fork gaps are real), a strapped armour-green
// crate with a gunmetal box on top, and a steel/oxide drum pair.
propDef('Loaded Pallet', 'cargo', '화물칸 · 팔레트 적재물 · 1.34 x 0.94 x 1.04 m', [1.34, 0.94, 1.04], (c) => {
  const { octBox, tube, plainBox, K_PANEL, K_HAZARD, K_PLAIN, A_OCHRE } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // pallet: three bearers + chamfered deck, hazard on the fork ends
  for (const x of [-0.56, 0, 0.56]) plainBox(x, 0.035, 0, 0.14, 0.07, 1.00, 361);
  octBox(0, 0.115, 0, 1.30, 0.09, 1.04, 0.05, {
    px: { kind: K_HAZARD, seed: 362, params: [0.07, 0, 0, 0], accent: A_OCHRE },
    nx: { kind: K_HAZARD, seed: 362, params: [0.07, 0, 0, 0], accent: A_OCHRE },
    pz: plain(363), nz: plain(363), diag: plain(363),
    py: plain(364), ny: plain(364),
  });

  // strapped crate + top box
  octBox(-0.33, 0.475, 0, 0.60, 0.63, 0.58, 0.045, {
    px: { kind: K_PANEL, seed: 365, params: [1, 0, 0, 0], ...P_HULL },
    nx: { kind: K_PANEL, seed: 365, params: [1, 0, 0, 0], ...P_HULL },
    pz: { kind: K_PANEL, seed: 366, params: [1, 0, 0, 0], ...P_HULL },
    nz: { kind: K_PANEL, seed: 366, params: [1, 0, 0, 0], ...P_HULL },
    diag: plain(367),
    topCut: 0.045,
    py: plain(368),
  });
  plainBox(-0.33, 0.86, 0.02, 0.36, 0.16, 0.32, 369, P_DARK);
  // girdle band round the crate at mid-height, gear-paint ochre; four
  // segments kept on the flat faces so the chamfered corners stay open
  for (const s of [-1, 1]) {
    plainBox(-0.33, 0.44, s * 0.296, 0.50, 0.07, 0.012, 370, P_OCHRE);
    plainBox(-0.33 + s * 0.305, 0.44, 0, 0.012, 0.07, 0.46, 370, P_OCHRE);
  }

  // drum pair on the other end, one steel, one oxide
  for (const z of [-0.24, 0.24]) {
    tube([0.36, 0.16, z], [0.36, 0.20, z], 0.185, { face: plain(371) });
    tube([0.36, 0.20, z], [0.36, 0.72, z], 0.175, { face: { ...plain(372), ...(z > 0 ? P_STEEL : P_OXIDE) } });
    tube([0.36, 0.72, z], [0.36, 0.76, z], 0.185, { face: { kind: K_SEAM, seed: 373, params: [1, 0, 0, 0] }, capB: true });
  }
});
