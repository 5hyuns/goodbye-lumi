/* ============================================================
   PROP KIT -- freestanding hero props for the derelict ship.

   Deliberately its OWN small system, not a graft onto tools/kit.js.
   That file's operator VM is load-bearing for the corridor build and
   has rules of its own (right-handed span bases, per-instance UV
   rules) that a prop does not need. A prop is a
   standalone object seen from every angle on a turntable, not a brush
   face continuing a wall's texture, so it earns a simpler model:

     * TRUE SCALE, metres, standing on y=0, origin at the mounting point
       -- same rule as the corridor kit.
     * EVERY MASS EDGE IS A MACHINED BEVEL -- 10 mm (BEVEL), on octBox
       AND hull8, smooth-shaded across the strip only (hullRing 'bevel')
       so the big faces stay flat and one highlight runs down each edge.
       The previous rule ("no right angle left open") cut every edge at
       45 degrees by 3-10% of the mass and shaded the cut flat, which
       turned every box into a faceted octagonal prism: two creases
       where there should be one highlight. Deliberate design chamfers
       (armour cuts, hazard crowns) are flagged facet and stay flat.
       Small hardware (box/panel) skips geometry and gets the same read
       from a rim bevel baked into its normal map (region.rim).
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
     * Height -> normal is baked at load time in propview.html.
   ============================================================ */

// ---- vector helpers -------------------------------------------------
function vAdd(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
function vSub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function vScale(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }
function vMA(base, dir, s) { return vAdd(base, vScale(dir, s)); }   // base + dir*s
function vCross(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function vDot(a, b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
function vLen(a) { return Math.hypot(a[0], a[1], a[2]); }
function vNorm(a) { const l = vLen(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }
// outward normal of a quad wound p0->p1->p2->p3 counter-clockwise seen from outside
function quadN(p0, p1, p2, p3) { return vNorm(vCross(vSub(p1, p0), vSub(p3, p0))); }
// deterministic jitter for organic corner offsets (canopy lobes)
function jHash(s) { const x = Math.sin(s * 127.1 + 311.7) * 43758.545; return x - Math.floor(x) - 0.5; }

// standard machined edge bevel for every mass, metres. Narrow on
// purpose: a bevel is read by the highlight it catches, not by its
// width, and past ~2% of a face a smooth-shaded strip turns into a
// pillow. Design chamfers (armour cuts, hazard crowns) are flagged
// facet and keep their own size.
const BEVEL = 0.014;
// segments per rounded corner. 3 is the knee of the curve: 1 is a
// chamfer, 2 still shows its facets in a grazing highlight, 5+ costs
// quads for a difference nobody sees at gameplay distance.
const ROUND_SEG = 3;
// every capped mass gets a small rolled top edge even when no crown
// chamfer was asked for. Without it the vertical edges were rounded but
// the cap still met the sides at a hard 90-degree crease, which is the
// edge most often facing the light.
const CAP_ROLL = 0.009;

const AX = [1, 0, 0], AY = [0, 1, 0], AZ = [0, 0, 1];
const NX = [-1, 0, 0], NY = [0, -1, 0], NZ = [0, 0, -1];

// pattern kinds baked by propview.html's bake shader
const K_PANEL = 0, K_RIB = 1, K_SCREEN = 2, K_BUTTONS = 3, K_HAZARD = 4,
      K_VALVE = 5, K_SEAM = 6, K_PLAIN = 7, K_PIPE = 8,
      // 9 was K_FOLIAGE. The hydroponics deck is cut, and it was the
      // only surface in the kit that was not a fabricated part -- three
      // octaves of un-anchored noise, sharing nothing with anything
      // else. Nothing else wanted it, so the slot is left open rather
      // than reused.
      K_PERF = 10;   // perforated hole grid: panels and deck plates

// accent roles. A_OCHRE is now the livery orange (structure/hazard) and
// A_AMBER is signal white (markings, general lighting); the live pair is
// unchanged. On K_SEAM every one of these bakes as a real recessed light
// run rather than a painted line.
const A_NONE = 0, A_OCHRE = 1, A_AMBER = 2, A_LIVE_C = 3, A_LIVE_G = 4,
      A_ALARM = 5;   // emergency red: the ship goes to red alert at S5

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
    /* sockets: named attachment points in the prop's own space, so the
       game can put a separate prop INTO this one at runtime. The energy
       pack interface needs this: its six slots fill one at a time over
       the course of the story, and a baked atlas cannot express a
       changing state. The console is baked empty; the game instances an
       Energy Core at socket 'slot1'..'slot6' as each is delivered. */
    sockets: [],     // {name, p:[x,y,z], d:[x,y,z]}
    _mitre: -1,
    _share: {},      // (kind|w|h|seed|accent|params) -> region index
  };

  function socket(name, p, d) { ctx.sockets.push({ name, p, d: vNorm(d || [0, 0, 1]) }); }

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
      // rim: bake a rounded-off bevel into the normal map along the
      // region's own border. Set for panel()/box() faces -- the small
      // hardware that never earns real chamfer geometry -- and left off
      // for lofts and tubes, whose faces continue into their neighbours.
      rim: !!opts.rim,
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
      + '|' + (opts.tint ? opts.tint.join(',') : '') + '|' + (opts.paint ? 1 : 0) + '|' + (opts.rim ? 1 : 0);
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
    const ri = opts.tile ? tileRegion(kind, opts)
      : (opts.share === false)
        ? addRegion(w, h, kind, opts)
        : sharedRegion(w, h, kind, opts);
    // opts.nv: per-corner normals for smooth shading (round surfaces);
    // without it the face is flat-shaded off its own plane normal.
    ctx.faces.push({ p: [p0, p1, p2, p3], n: n, nv: opts.nv || null, ri: ri });
  }

  /* tileRegion -- ONE pooled region per (kind, seed, tint, paint) for
     undetailed trim, whatever the face's real size.

     Rounded edges, rolled crowns and cap skirts generate a long tail of
     narrow strips at every conceivable width, and sharedRegion keys on
     size, so each distinct width earned its own unwrap. On the capsule
     hatch that was 38 regions under 8 texels -- strips too small to
     show anything, but big enough to drag the whole prop's texel
     density down, because the packer holds one density for all of it.
     Bevel trim is plain metal; a 60 mm tile stretched over it is
     invisible, and reclaiming those regions is worth more than the
     unwrap ever was. */
  const _tiles = {};
  function tileRegion(kind, opts) {
    const key = kind + '|' + (opts.seed || 1) + '|' + (opts.tint ? opts.tint.join(',') : '') + '|' + (opts.paint ? 1 : 0);
    if (_tiles[key] === undefined) _tiles[key] = addRegion(0.06, 0.06, kind, opts);
    return _tiles[key];
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
    const ro = Object.assign({ rim: true }, opts);
    if (c <= 0.0001) {
      const p0 = O, p1 = vMA(O, U, w), p2 = vMA(p1, V, h), p3 = vMA(O, V, h);
      addFace(p0, p1, p2, p3, N, w, h, opts.kind === undefined ? K_PANEL : opts.kind, ro);
      return;
    }
    const cw = Math.min(c, w * 0.32), ch = Math.min(c, h * 0.32);
    const p0 = O, p1 = vMA(O, U, w), p2 = vMA(p1, V, h), p3 = vMA(O, V, h);
    const bk = (cw + ch) * 0.5;
    const q0 = vMA(vMA(vMA(O, U, cw), V, ch), N, -bk);
    const q1 = vMA(vMA(vMA(O, U, w - cw), V, ch), N, -bk);
    const q2 = vMA(vMA(vMA(O, U, w - cw), V, h - ch), N, -bk);
    const q3 = vMA(vMA(vMA(O, U, cw), V, h - ch), N, -bk);
    addFace(q0, q1, q2, q3, N, w - 2 * cw, h - 2 * ch, opts.kind === undefined ? K_PANEL : opts.kind, ro);
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
     region.
     smooth: false    flat, every face off its own plane
             true     average neighbouring normals across every edge --
                      an 8-sided tube SHADES round while staying 8 quads
             'bevel'  ODD faces only get their two neighbours' normals
                      (corner 0/3 from face k-1, corner 1/2 from k+1).
                      This is the hard-surface bevel rule: the big
                      faces stay flat, the narrow cut strip alone
                      interpolates from one face normal to the other, so
                      a highlight runs down the edge instead of two
                      creases. Only reads right when the strip is narrow
                      (a few % of the face).
             fn       (k, faceNormals) => nv or null, for bands whose
                      normals come from outside this ring (crown chamfers). */
  function ringFaceNormals(A, B) {
    const n = A.length, f = [];
    for (let k = 0; k < n; k++) f.push(quadN(A[k], A[(k + 1) % n], B[(k + 1) % n], B[k]));
    return f;
  }
  function hullRing(A, B, opts, smooth) {
    const n = A.length;
    const fns = smooth ? ringFaceNormals(A, B) : null;
    for (let k = 0; k < n; k++) {
      const o = (typeof opts === 'function') ? opts(k) : opts;
      if (!o) continue;
      const k1 = (k + 1) % n, kp = (k + n - 1) % n;
      let nv = null;
      if (smooth === true) {
        const nP = vNorm(vAdd(fns[kp], fns[k])), nN = vNorm(vAdd(fns[k], fns[k1]));
        nv = [nP, nN, nN, nP];
      } else if (smooth === 'bevel') {
        if (k % 2 === 1) nv = [fns[kp], fns[k1], fns[k1], fns[kp]];
      } else if (typeof smooth === 'function') {
        nv = smooth(k, fns);
      }
      face4(A[k], A[k1], B[k1], B[k], nv ? Object.assign({}, o, { nv }) : o);
    }
  }

  /* roundRing(R, r, seg) -- 4-ring -> rounded-corner ring, plus the
     outward normal at every point. Each corner is a quadratic Bezier
     from the cut point on the incoming edge, through the corner, to the
     cut point on the outgoing edge; its end tangents ARE the two edge
     directions, so the fillet meets each flat face C1-continuously and
     a highlight rolls off the flat onto the curve instead of stopping
     at a crease. Works on any quad, so hull8's sloped and jittered
     rings round exactly like an axis-aligned box.

     This is the shape change the reference interiors are actually
     built on. A 45-degree cut says "plate, sawn and welded"; a radius
     says "cast, stamped or moulded", and every panel, frame, hatch
     surround and brace in those frames is the second thing. The kit was
     producing military plate because a chamfer is all it could make.

     Returns {p, n, card} -- card[i] is the face index of the i-th flat
     cardinal face, so the caller can still address px/nz/nx/pz. */
  function roundRing(R, r, seg) {
    const P = [], N = [], card = [];
    const cut = [], dir = [];
    for (let i = 0; i < 4; i++) {
      const A = R[i], B = R[(i + 1) % 4];
      const d = vSub(B, A), L = vLen(d) || 1;
      dir.push(vScale(d, 1 / L));
      cut.push(Math.min(r, L * 0.45));
    }
    // outward normal of edge i, taken in the RING'S OWN plane. Using a
    // hardcoded up axis collapses to zero the moment a ring stands
    // vertical (rrFrame on a wall), so the plane comes from the quad.
    const ctr = R.reduce((a, p) => vAdd(a, p), [0, 0, 0]).map(v => v / 4);
    let pn = vCross(vSub(R[1], R[0]), vSub(R[3], R[0]));
    if (vLen(pn) < 1e-9) pn = [0, 1, 0];
    pn = vNorm(pn);
    const eN = [];
    for (let i = 0; i < 4; i++) {
      let n = vNorm(vCross(dir[i], pn));
      const mid = vScale(vAdd(R[i], R[(i + 1) % 4]), 0.5);
      if (vLen(vSub(vMA(mid, n, 0.01), ctr)) < vLen(vSub(mid, ctr))) n = vScale(n, -1);
      eN.push(n);
    }
    for (let i = 0; i < 4; i++) {
      const prev = (i + 3) % 4;
      const V = R[i];
      const a = vMA(V, dir[prev], -cut[prev]);   // on the incoming edge
      const b = vMA(V, dir[i], cut[i]);          // on the outgoing edge
      for (let s = 0; s <= seg; s++) {
        const t = seg === 0 ? 0 : s / seg, u = 1 - t;
        P.push(vAdd(vAdd(vScale(a, u * u), vScale(V, 2 * u * t)), vScale(b, t * t)));
        // Bezier tangent -> in-plane outward normal; ends land exactly
        // on the two flat faces' normals
        N.push(vNorm(vAdd(vScale(eN[prev], u), vScale(eN[i], t))));
      }
      card.push(P.length - 1);   // face from this corner's last point to the next corner's first = flat edge i
    }
    return { p: P, n: N, card };
  }

  // 4-ring (rectRing order, or hull8's jittered quads) -> 8-ring with
  // every corner cut back by c along both of its edges. Output is in
  // octFoot's order: even faces of hullRing(out) are the four cardinals
  // in the input's edge order, odd faces are the corner cuts.
  function cutRing(R, c) {
    const out = [];
    for (let i = 0; i < 4; i++) {
      const P = R[i], Q = R[(i + 1) % 4];
      const d = vSub(Q, P), L = vLen(d) || 1;
      const cc = Math.min(c, L * 0.45);
      const u = vScale(d, 1 / L);
      out.push(vMA(P, u, cc), vMA(Q, u, -cc));
    }
    return out;
  }
  // 4-ring pulled in by c along both edge directions at every corner
  function insetRing(R, c) {
    const out = [];
    for (let i = 0; i < 4; i++) {
      const P = R[i], Q = R[(i + 1) % 4], S = R[(i + 3) % 4];
      const dq = vSub(Q, P), ds = vSub(S, P);
      const lq = vLen(dq) || 1, ls = vLen(ds) || 1;
      out.push(vAdd(P, vAdd(vScale(dq, Math.min(c, lq * 0.45) / lq), vScale(ds, Math.min(c, ls * 0.45) / ls))));
    }
    return out;
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

  /* capCut(ring4, c, capOpts, trimOpts) -- flat cap over a cut 4-ring:
     ONE full-size quad inset by c (a top plate's bolts and plaque land on
     the plate, not on a fan of wedges), plus a trim ring of four strips
     and four corner triangles that cover the bevel margin. Faces along
     the ring's own winding; pass revRing() for the far side. */
  function capCut(R, c, capOpts, trimOpts) {
    const I = insetRing(R, c);
    face4(I[0], I[1], I[2], I[3], capOpts);
    const to = trimOpts || { kind: K_PLAIN, seed: 8, tile: true };
    for (let i = 0; i < 4; i++) {
      const i1 = (i + 1) % 4, i2 = (i + 2) % 4;
      const P = R[i], Q = R[i1];
      const d = vSub(Q, P), L = vLen(d) || 1;
      const u = vScale(d, 1 / L), cc = Math.min(c, L * 0.45);
      const a = vMA(P, u, cc), b = vMA(Q, u, -cc);
      face4(I[i], a, b, I[i1], to);                       // margin strip along edge PQ
      const d2 = vSub(R[i2], Q), L2 = vLen(d2) || 1;
      const b2 = vMA(Q, d2, Math.min(c, L2 * 0.45) / L2);
      face4(I[i1], b, b2, b2, to);                       // corner triangle at Q
    }
  }

  /* capRound(ring4, r, seg, capOpts, trimOpts) -- flat cap over a
     rounded ring: one full-size inset quad (so a top plate's bolts and
     markings land on the plate) plus a trim skirt of quads following
     the rounded outline. Same job as capCut, rounded outline. */
  function capRound(R, r, seg, capOpts, trimOpts) {
    const I = insetRing(R, r);
    face4(I[0], I[1], I[2], I[3], capOpts);
    const O = roundRing(R, r, seg).p;
    const to = trimOpts || { kind: K_PLAIN, seed: 8, tile: true };
    const n = O.length, per = seg + 1;
    for (let i = 0; i < 4; i++) {
      const base = i * per;
      // fan the corner arc back to this corner's inner point
      for (let s = 0; s < seg; s++) {
        face4(O[base + s], O[base + s + 1], I[i], I[i], to);
      }
      // straight run of edge i: outer cut point -> next corner's first
      // arc point -> the two inner corners
      face4(O[base + seg], O[((i + 1) * per) % n], I[(i + 1) % 4], I[i], to);
    }
  }

  /* loft(B, T, S) -- the one mass builder behind octBox and hull8.
     B, T: 4-rings (rectRing order; jittered quads are fine).
     S: cut         bevel width on the four vertical edges (0 = sharp)
        facet       true: the cut is a DESIGN chamfer -- keep every strip
                    flat-shaded (armour chamfers, hazard crowns). Default:
                    the cut strip is smooth-shaded between its two
                    neighbours (hullRing 'bevel'), so the big faces stay
                    flat and one highlight line runs down the edge.
        px,nx,pz,nz cardinal faces (null skips); diag: the cut strips
        topCut      chamfer band under the top cap, inside the given
                    height. Smooth-shaded side->cap when it is a narrow
                    bevel (<= 20 mm) and not facet; flat otherwise.
        topOpts     opts or (k)=>opts for the band
        py, ny      caps; trim: opts for the caps' bevel-margin ring */
  function loft(B, T, S) {
    S = S || {};
    const facet = !!S.facet;
    // ROUND is the default edge treatment now; facet keeps the old
    // single flat 45-degree strip for deliberate armour chamfers.
    const seg = facet ? 1 : (S.seg === undefined ? ROUND_SEG : S.seg);
    const r = Math.max(0, S.cut === undefined ? BEVEL : S.cut);
    let tr = Math.max(0, S.topRound === undefined ? (S.topCut || 0) : S.topRound);
    let trSeg = facet ? 1 : (S.topSeg === undefined ? ROUND_SEG : S.topSeg);
    if (tr <= 0 && S.py && !facet && r > 0) { tr = Math.min(CAP_ROLL, r); trSeg = 2; }
    const up = vNorm(vCross(vSub(T[1], T[0]), vSub(T[3], T[0])));
    const Ts = (tr > 0 && S.py) ? T.map(p => vMA(p, up, -tr)) : T;
    const own = o => (o.share === undefined ? Object.assign({ share: false }, o) : o);
    const card = [S.px, S.nz, S.nx, S.pz];
    const diag = S.diag === undefined ? { kind: K_PLAIN, seed: 6, tile: true } : S.diag;

    let A, Bt, sideN = null, cardIdx = null;
    if (r > 0) {
      const rb = roundRing(B, r, seg), rt = roundRing(Ts, r, seg);
      A = rb.p; Bt = rt.p; cardIdx = rb.card;
      // per-point normals from the ring itself: the curve is smooth,
      // the flats stay flat, and the join is exact
      sideN = rb.n;
    } else { A = B; Bt = Ts; cardIdx = [0, 1, 2, 3]; }
    const isCard = {}; cardIdx.forEach((f, i) => { isCard[f] = i; });
    const nPts = A.length;
    hullRing(A, Bt, k => {
      const ci = isCard[k];
      if (ci !== undefined) return card[ci] ? own(card[ci]) : null;
      return diag;
    }, sideN ? (k) => {
      const k1 = (k + 1) % nPts;
      return [sideN[k], sideN[k1], sideN[k1], sideN[k]];
    } : false);

    let cap4 = Ts, capR = r;
    if (tr > 0 && S.py) {
      /* bullnose crown: a quarter-round rolled edge in trSeg steps
         instead of one chamfer band. The reference frames all have a
         thick rolled lip -- it is what makes a panel surround read as
         a formed section rather than a mitred picture frame. */
      const topO = S.topOpts || { kind: K_PLAIN, seed: 8, tile: true };
      let prevR = Bt, prevN = sideN;   // the rounded top of the side loft, not the 4-point rect
      for (let s = 1; s <= trSeg; s++) {
        const t = s / trSeg, ang = t * Math.PI * 0.5;
        const inset = tr * (1 - Math.cos(ang)), rise = tr * Math.sin(ang);
        const ringQ = insetRing(T, inset).map(p => vMA(p, up, -tr + rise));
        const rr = Math.max(0.002, r - inset * 0.5);
        const cur = r > 0 ? roundRing(ringQ, rr, seg) : { p: ringQ, n: null, card: [0, 1, 2, 3] };
        // normal tilts from the side's outward normal towards up
        const mkN = (base) => base ? base.map(nn => vNorm(vAdd(vScale(nn, Math.cos(ang)), vScale(up, Math.sin(ang))))) : null;
        const nPrev = prevN, nCur = mkN(cur.n);
        const m = prevR.length;
        hullRing(prevR, cur.p, (typeof topO === 'function') ? topO : topO,
          (nPrev && nCur) ? (k) => {
            const k1 = (k + 1) % m;
            return [nPrev[k], nPrev[k1], nCur[k1], nCur[k]];
          } : false);
        prevR = cur.p; prevN = nCur;
      }
      cap4 = insetRing(T, tr); capR = Math.max(0.002, r - tr * 0.5);
    } else if (tr > 0) {
      cap4 = Ts;
    }
    if (S.py) {
      if (capR > 0) capRound(cap4, capR, seg, S.py, S.trim);
      else face4(cap4[0], cap4[1], cap4[2], cap4[3], S.py);
    }
    if (S.ny) {
      if (r > 0) capRound(revRing(B), r, seg, S.ny, S.trim);
      else face4(B[3], B[2], B[1], B[0], S.ny);
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
    // A textured crown band (topOpts function) or an explicit facet flag
    // marks a DESIGN chamfer: the caller's cut stays and shades flat.
    // Everything else gets the standard machined bevel.
    // a textured crown band (topOpts as a function) is a deliberate
    // faceted armour chamfer; everything else gets the rolled edge
    const facet = !!S.facet || typeof S.topOpts === 'function';
    const c = facet ? cut : (S.cut !== undefined ? S.cut : BEVEL);
    loft(rectRing(cx, cz, w / 2, d / 2, cy - h / 2), rectRing(cx, cz, w / 2, d / 2, cy + h / 2),
      Object.assign({}, S, { cut: c, facet }));
  }

  /* hull8(B, T, S) -- distorted box between two rectRings (bottom, top):
     wedges, tapers, sloped decks, flared hoods. Face map: S.px (+x),
     S.nz, S.nx, S.pz sides; S.py / S.ny caps. Side faces auto-slope; a
     console's keyboard deck is just the pz face of the right loft.
     Vertical edges get the standard bevel unless S.cut says otherwise
     (cut: 0 for foliage lobes); S.topCut adds a crown bevel. */
  function hull8(B, T, S) {
    loft(B, T, Object.assign({ cut: BEVEL }, S || {}));
  }

  /* capBar(a, b, wdt, t, N, seg, opts) -- strap() with ROUND ENDS and a
     rolled edge: a capsule lying on a plane. The reference X-braces and
     rib straps are all formed sections with radiused ends; a square-cut
     flat bar is the single most "welded plate" shape in the kit. */
  function capBar(a, b, wdt, t, N, seg, opts) {
    opts = opts || {};
    seg = seg || 4;
    const dir = vNorm(vSub(b, a)), side = vNorm(vCross(N, dir));
    const hw = wdt / 2;
    // outline of the capsule in the plane, CCW seen along +N
    const out = [], nrm = [];
    for (let s = 0; s <= seg; s++) {           // cap at b
      const th = -Math.PI / 2 + s * Math.PI / seg;
      const d = vAdd(vScale(dir, Math.cos(th)), vScale(side, Math.sin(th)));
      out.push(vMA(b, d, hw)); nrm.push(d);
    }
    for (let s = 0; s <= seg; s++) {           // cap at a
      const th = Math.PI / 2 + s * Math.PI / seg;
      const d = vAdd(vScale(dir, Math.cos(th)), vScale(side, Math.sin(th)));
      out.push(vMA(a, d, hw)); nrm.push(d);
    }
    const n = out.length;
    const top = out.map(p => vMA(p, N, t));
    // side wall, smooth round
    const eo = { kind: K_PLAIN, seed: opts.seed || 7, tint: opts.tint, paint: opts.paint };
    for (let k = 0; k < n; k++) {
      const k1 = (k + 1) % n;
      face4(out[k], out[k1], top[k1], top[k], Object.assign({}, eo, { nv: [nrm[k], nrm[k1], nrm[k1], nrm[k]] }));
    }
    // face: two fans off the bar's own centreline, so the quad strip
    // stays well-shaped whatever the aspect ratio
    const ca = vMA(a, N, t), cb = vMA(b, N, t);
    for (let k = 0; k < n; k++) {
      const k1 = (k + 1) % n;
      const c0 = (k < seg + 1) ? cb : ca, c1 = (k1 <= seg) ? cb : ca;
      face4(top[k], top[k1], c1, c0, opts);
    }
  }

  /* xbrace(O, U, V, N, w, h, wdt, t, opts) -- the reference wall panel's
     X, as real geometry: two capsule bars crossing inside a w x h bay,
     lying on the plane (O; U,V) and proud of it by t. */
  function xbrace(O, U, V, N, w, h, wdt, t, opts) {
    const p = (x, y) => vMA(vMA(O, U, x), V, y);
    const m = wdt * 0.6;
    capBar(p(m, m), p(w - m, h - m), wdt, t, N, 4, opts);
    capBar(p(w - m, m), p(m, h - m), wdt, t, N, 4, opts);
  }

  /* rrFrame(O, U, V, N, w, h, r, wdt, t, opts) -- a rounded-rectangle
     proud frame: the hatch/porthole/panel surround the references put
     around every opening. Outer and inner outlines are both radiused
     and the outer wall is smooth-shaded, so it reads as a formed collar
     rather than four mitred strips. */
  function rrFrame(O, U, V, N, w, h, r, wdt, t, opts) {
    opts = opts || {};
    const seg = opts.seg || ROUND_SEG;
    const ring = (x0, y0, x1, y1, rr) => {
      let q = [vMA(vMA(O, U, x0), V, y0), vMA(vMA(O, U, x1), V, y0),
               vMA(vMA(O, U, x1), V, y1), vMA(vMA(O, U, x0), V, y1)];
      // the caller's U,V need not be right-handed about N; if they are
      // not, the extruded walls come out inside-out, so flip the ring
      if (vDot(vCross(vSub(q[1], q[0]), vSub(q[3], q[0])), N) < 0) q = [q[3], q[2], q[1], q[0]];
      return roundRing(q, rr, seg);
    };
    const outer = ring(0, 0, w, h, r);
    const inner = ring(wdt, wdt, w - wdt, h - wdt, Math.max(0.004, r - wdt));
    const lift = a => a.map(p => vMA(p, N, t));
    const oT = lift(outer.p), iT = lift(inner.p);
    const n = outer.p.length;
    const eo = { kind: K_PLAIN, seed: opts.seed || 7, tint: opts.tint, paint: opts.paint };
    for (let k = 0; k < n; k++) {
      const k1 = (k + 1) % n;
      face4(outer.p[k], outer.p[k1], oT[k1], oT[k],
        Object.assign({}, eo, { nv: [outer.n[k], outer.n[k1], outer.n[k1], outer.n[k]] }));   // outer wall
      face4(oT[k], oT[k1], iT[k1], iT[k], opts);                                              // top face
      // inner wall (the bore of the collar) faces INTO the opening, so
      // it winds opposite the outer wall and takes negated ring normals
      face4(iT[k], iT[k1], inner.p[k1], inner.p[k],
        Object.assign({}, eo, { nv: [vScale(inner.n[k], -1), vScale(inner.n[k1], -1), vScale(inner.n[k1], -1), vScale(inner.n[k], -1)] }));
    }
  }

  /* boss(c, axis, r, h, opts) -- a cylindrical boss with a FILLETED
     foot: the disc/hub that sits on the reference wall panels. The
     fillet is what stops it reading as a can glued to a board. */
  function boss(c, axis, r, h, opts) {
    opts = opts || {};
    const n = opts.n || 12, fil = opts.fillet === undefined ? r * 0.30 : opts.fillet;
    const steps = opts.filletSeg || 3;
    const dir = vNorm(axis);
    let U = vCross([0, 1, 0], dir); if (vLen(U) < 0.01) U = vCross([1, 0, 0], dir);
    U = vNorm(U);
    const V = vCross(dir, U), ph = Math.PI / n;
    const fo = Object.assign({ kind: K_PLAIN, seed: opts.seed || 9 }, opts.face || {});
    // concave quarter-torus: arc centred at (r+fil, fil) in the
    // (radial, axial) plane, so it leaves the plate flat and arrives at
    // the cylinder wall tangent
    let prev = null, prevN = null;
    for (let s = 0; s <= steps; s++) {
      const a = (s / steps) * Math.PI * 0.5;
      const rr = r + fil * (1 - Math.sin(a)), yy = fil * (1 - Math.cos(a));
      const ring = ringAt(vMA(c, dir, yy), U, V, rr, n, ph);
      const nn = ring.map(p => {
        const rad = vNorm(vSub(vMA(p, dir, -yy), c));
        return vNorm(vAdd(vScale(rad, Math.sin(a)), vScale(dir, Math.cos(a))));
      });
      if (prev) for (let k = 0; k < n; k++) {
        const k1 = (k + 1) % n;
        face4(prev[k], prev[k1], ring[k1], ring[k],
          Object.assign({}, fo, { nv: [prevN[k], prevN[k1], nn[k1], nn[k]] }));
      }
      prev = ring; prevN = nn;
    }
    const top = ringAt(vMA(c, dir, fil + h), U, V, r, n, ph);
    hullRing(prev, top, fo, true);
    capPoly(top, opts.cap || fo);
  }

  /* strap(a, b, wdt, t, N, opts) -- diagonal brace on a plane of
     outward normal N, proud of it by t. Kept as a name; capBar does
     the work. */
  function strap(a, b, wdt, t, N, opts) {
    // now a capsule: square-cut ends were the last flat-bar shape in
    // the kit, and the references have none. Ends still bury cleanly in
    // the rails they run between.
    capBar(a, b, wdt, t, N, 4, opts || {});
  }

  /* tube(p0, p1, r, opts) -- octagonal tube between two points, any
     direction. The prop kit's pipe: eight REAL faces sharing one atlas
     region, not a dome painted on a plank. opts: {face, capA, capB,
     capOpts, n, phase}. */
  function tube(p0, p1, r, opts) {
    opts = opts || {};
    // segment count follows radius: an 8-gon reads as a pipe at 30 mm
    // and as a nut at 300 mm
    const n = opts.n || (r > 0.16 ? 16 : r > 0.06 ? 12 : 8);
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
    /* caps get a rolled rim rather than a flat disc butted onto the
       wall at 90 degrees -- a pipe end, a flange face and a drum lid all
       read as machined only if the rim catches its own highlight. */
    const co = opts.capOpts || { kind: K_PLAIN, seed: 10 };
    const roll = opts.capRoll === undefined ? Math.min(r * 0.28, 0.010) : opts.capRoll;
    // rev: ringAt always winds one way, so a cap built on a REVERSED
    // ring (the A end) has to reverse each roll ring too or the strip
    // between them is built from mismatched vertex pairs
    const rimCap = (centre, out, ring, rev) => {
      if (roll <= 0.0005) { capPoly(ring, co); return; }
      const steps = 2;
      let prev = ring, prevN = ring.map(pt => vNorm(vSub(pt, vMA(centre, out, 0))));
      for (let s = 1; s <= steps; s++) {
        const a = (s / steps) * Math.PI * 0.5;
        const rr = r - roll * (1 - Math.cos(a)), yy = roll * Math.sin(a);
        let cur = ringAt(vMA(centre, out, yy), U, V, rr, n, phase);
        if (rev) cur = revRing(cur);
        const cn = cur.map(pt => vNorm(vAdd(vScale(vNorm(vSub(pt, vMA(centre, out, yy))), Math.cos(a)), vScale(out, Math.sin(a)))));
        for (let k = 0; k < n; k++) {
          const k1 = (k + 1) % n;
          face4(prev[k], prev[k1], cur[k1], cur[k], Object.assign({}, co, { nv: [prevN[k], prevN[k1], cn[k1], cn[k]] }));
        }
        prev = cur; prevN = cn;
      }
      capPoly(prev, co);
    };
    if (opts.capA) rimCap(p0, vScale(dir, -1), revRing(A), true);
    if (opts.capB) rimCap(p1, dir, B, false);
    // collar: a short wider ring where the pipe enters a body -- the
    // universal "this was fitted" cue; without it a pipe is a cylinder
    // stuck into a box. opts.collar: 'a' | 'b' | 'ab'. Bare metal.
    const col = opts.collar;
    if (col) {
      const cr = r * (opts.collarR || 1.32), cl = opts.collarL || Math.max(0.02, r * 0.7);
      const c2 = Object.assign({ kind: K_PLAIN, seed: (fo.seed || 9) + 40, tile: true }, opts.collarOpts || {});
      if (col.indexOf('a') >= 0) {
        const A2 = ringAt(p0, U, V, cr, n, phase), B2 = ringAt(vMA(p0, dir, cl), U, V, cr, n, phase);
        hullRing(A2, B2, c2, true); capPoly(B2, c2);
      }
      if (col.indexOf('b') >= 0) {
        const A2 = ringAt(vMA(p1, dir, -cl), U, V, cr, n, phase), B2 = ringAt(p1, U, V, cr, n, phase);
        hullRing(A2, B2, c2, true); capPoly(revRing(A2), c2);
      }
    }
    return { A, B, U, V, dir };
  }

  /* stack(base, axis, sections, opts) -- a body of revolution from a
     profile. sections: [{l, r, face}] laid end to end along `axis`.

     This exists because stacking tube() calls at different radii leaves
     a RING-SHAPED HOLE at every step. The energy core had seven of
     them; the drum, the gas bottles and the specimen tank all had the
     same gaps, because nothing was drawing the annular shoulder where
     the radius changed. tube() cannot fix it on its own -- it also
     picks its segment count from its own radius, so consecutive
     segments did not even share vertex counts.

     stack picks ONE segment count for the whole profile, lofts each
     section's wall smooth, and fills every radius change with a flat
     annulus. Facing works out for free: hullRing(ringAt(ra),ringAt(rb))
     normals come out along +axis when the radius shrinks and -axis when
     it grows, which is correct in both directions. */
  function stack(base, axis, sections, opts) {
    opts = opts || {};
    const dir = vNorm(axis);
    let U = vCross([0, 1, 0], dir); if (vLen(U) < 0.01) U = vCross([1, 0, 0], dir);
    U = vNorm(U);
    const V = vCross(dir, U);
    let maxR = 0; for (const s of sections) maxR = Math.max(maxR, s.r);
    const n = opts.n || (maxR > 0.16 ? 16 : maxR > 0.06 ? 12 : 8);
    const ph = opts.phase === undefined ? Math.PI / n : opts.phase;
    const sho = opts.shoulder || { kind: K_PLAIN, seed: 8, tile: true };
    const ringAtZ = (z, r) => ringAt(vMA(base, dir, z), U, V, r, n, ph);

    let z = 0, prevTop = null;
    for (const s of sections) {
      const A = ringAtZ(z, s.r);
      if (prevTop && Math.abs(prevTop.r - s.r) > 1e-5) hullRing(prevTop.ring, A, sho, false);
      const B = ringAtZ(z + s.l, s.r);
      hullRing(A, B, Object.assign({ kind: K_PLAIN, seed: 9 }, s.face || {}), true);
      z += s.l; prevTop = { ring: B, r: s.r };
    }
    // rolled end caps, same treatment tube() gives its own
    const co = opts.capOpts || { kind: K_PLAIN, seed: 10, tile: true };
    const rimCap = (zc, r, out, rev) => {
      const roll = Math.min(r * 0.28, 0.010);
      let ring = ringAtZ(zc, r); if (rev) ring = revRing(ring);
      if (roll <= 0.0005) { capPoly(ring, co); return; }
      let prev = ring, prevN = ring.map(p => vNorm(vSub(vMA(p, dir, -zc), base)));
      for (let k = 1; k <= 2; k++) {
        const a = (k / 2) * Math.PI * 0.5;
        const rr = r - roll * (1 - Math.cos(a));
        let cur = ringAtZ(zc + out * roll * Math.sin(a), rr); if (rev) cur = revRing(cur);
        const cn = cur.map(p => vNorm(vAdd(
          vScale(vNorm(vSub(vMA(p, dir, -(zc + out * roll * Math.sin(a))), base)), Math.cos(a)),
          vScale(vScale(dir, out), Math.sin(a)))));
        for (let j = 0; j < n; j++) {
          const j1 = (j + 1) % n;
          face4(prev[j], prev[j1], cur[j1], cur[j], Object.assign({}, co, { nv: [prevN[j], prevN[j1], cn[j1], cn[j]] }));
        }
        prev = cur; prevN = cn;
      }
      capPoly(prev, co);
    };
    if (opts.capA !== false) rimCap(0, sections[0].r, -1, true);
    if (opts.capB !== false) rimCap(z, sections[sections.length - 1].r, 1, false);
    return { U, V, dir, len: z };
  }

  /* wheelZ(cx,cy,cz, R, t, depth, opts) -- a real hand-wheel facing +Z:
     octagonal rim (outer wall, inner wall, front and back ring faces),
     an octagonal hub proud both ways, and four square spokes. ~50
     quads. The first pass drew this as a picture; the picture is now
     only allowed on gauge dials under 80 mm. */
  function wheelZ(cx, cy, cz, R, t, depth, opts) {
    opts = opts || {};
    const n = 12, ph = Math.PI / n;
    const zB = cz - depth / 2, zF = cz + depth / 2;
    const oB = ringAt([cx, cy, zB], AX, AY, R, n, ph);
    const oF = ringAt([cx, cy, zF], AX, AY, R, n, ph);
    const iB = ringAt([cx, cy, zB], AX, AY, R - t, n, ph);
    const iF = ringAt([cx, cy, zF], AX, AY, R - t, n, ph);
    const mo = { kind: K_PLAIN, seed: opts.seed || 11, tint: opts.tint, paint: opts.paint };
    hullRing(oB, oF, mo, true);                      // outer wall, shades round
    hullRing(revRing(iB), revRing(iF), mo, true);    // inner wall, normals to axis
    // front and back ring faces, with the outer corner rolled over so
    // the rim reads as turned stock rather than a washer
    const rl = Math.min(t * 0.30, 0.008);
    const roll = (ring, zc, out, inner, flip) => {
      const a1 = ringAt([cx, cy, zc + out * rl], AX, AY, R - rl * (1 - Math.cos(Math.PI / 4)), n, ph);
      const mid = ringAt([cx, cy, zc + out * rl * 0.707], AX, AY, R - rl * 0.293, n, ph);
      for (let k = 0; k < n; k++) {
        const k1 = (k + 1) % n;
        if (flip) { face4(ring[k1], ring[k], mid[k], mid[k1], mo); face4(mid[k1], mid[k], inner[k], inner[k1], mo); }
        else { face4(ring[k], ring[k1], mid[k1], mid[k], mo); face4(mid[k], mid[k1], inner[k1], inner[k], mo); }
      }
    };
    roll(oF, zF, 0, iF, false);
    roll(oB, zB, 0, iB, true);
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
    box, plainBox, panel, addFace, face4, hullRing, capPoly, hull8, octBox, loft, capCut, capRound,
    cutRing, roundRing, insetRing, capBar, xbrace, rrFrame, boss, stack,
    tube, wheelZ, strap, rectRing, octFoot, ringAt, revRing,
    K_PANEL, K_RIB, K_SCREEN, K_BUTTONS, K_HAZARD, K_VALVE, K_SEAM, K_PLAIN, K_PIPE, K_PERF,
    A_NONE, A_OCHRE, A_AMBER, A_LIVE_C, A_LIVE_G, A_ALARM, socket, AX, AY, AZ, NX, NY, NZ,
  });

  PROPS.push({ name, room, sub, size, faces: ctx.faces, regions: ctx.regions, sockets: ctx.sockets });
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
  const pad = 8;   // mip levels average across region borders; 8 texels keeps the first three levels clean
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
   (ref/ship_deck_plan_v6_fixed.svg), plus a room-dressing set (props 10-19) so every
   playable room can be furnished, not just anchored. Every one built
   on the Doom-3 shape grammar: a stepped-in plinth (shadow line at
   the floor), a chamfered or sloped crown (never a bare flat top),
   and at least two real depths on any face the player is asked to
   read.
   ============================================================ */

// ---- paint palette ---------------------------------------------------
// A dark palette (a blue-grey corridor, a cold hold): pale grey plates against near-black blue-grey armour,
// bare alloy on machined trim, orange only as an accent. The bright
// white-and-orange livery pass overshot -- that palette belongs to a
// lit, maintained ship, not this one. Two things did survive it:
//
//  1. These are TRUE ALBEDO values now. The bake used to put the value
//     in its height ramp (0.085-0.285, near-black) and treat tint as a
//     corrective multiplier, which is why pale surfaces needed tints
//     above 1.0 and why everything still read as dark weathered steel
//     whatever colour it was given. Height now only modulates.
//  2. The scheme is a livery, not camouflage: pale composite shells,
//     ONE saturated orange carrying the structure, bare bright alloy on
//     machined trim, charcoal reserved for recesses so the pale
//     surfaces have something to sit against. Armour green, ochre and
//     oxide red were the military read the props kept coming back to.
//
// The renderer's LOOK.value dial darkens all of these together for the
// derelict state, so one palette serves both. Values were picked by
// solving backwards from the target sRGB off the reference frames
// through the bake's own shade term -- an orange that looks right as a
// hex code comes out pastel once the ramp has multiplied it.
const P_SHELL = { tint: [0.300, 0.305, 0.300], paint: true };  // pale grey composite panel
const P_HULL  = { tint: [0.070, 0.088, 0.115], paint: true };  // blue-grey armour unit -- the big wall masses
const P_BONE  = { tint: [0.380, 0.380, 0.360], paint: true };  // clinical shell: medbay, galley, hydroponics
const P_ORANGE= { tint: [0.245, 0.085, 0.055], paint: true };  // muted oxide accent, used sparingly
const P_OXIDE = P_ORANGE;
const P_OCHRE = { tint: [0.320, 0.190, 0.078], paint: true };  // muted ochre for wheels and handles
const P_STEEL = { tint: [0.300, 0.310, 0.320] };               // bare pipe alloy -- metal, not painted
const P_TRIM  = { tint: [0.420, 0.430, 0.440] };               // bright machined alloy on rails and frames
const P_DARK  = { tint: [0.048, 0.050, 0.054], paint: true };  // charcoal recess: vents, bays, screen surrounds

// ---- 1. CARGO CONTAINER -- 화물칸 (cargo bay) --------------------------
// Real container language: a 45-degree top chamfer wearing the ochre
// hazard band (where a rakelight finds it), proud vertical ribs and
// rails framing recessed panel bays, twin lock rods on the door end,
// corner feet floating the body 70 mm off the deck.
propDef('Cargo Container', 'cargo', '화물칸 · 2.47 x 1.22 x 1.28 m stackable', [2.47, 1.22, 1.28], (c) => {
  const { octBox, box, plainBox, panel, capBar, boss, rrFrame, K_PANEL, K_HAZARD, K_SEAM, K_PLAIN, K_PERF, A_OCHRE, AX, AZ, AY, NZ } = c;

  for (const x of [-1.05, 1.05]) for (const z of [-0.44, 0.44]) {
    plainBox(x, 0.035, z, 0.18, 0.07, 0.18, 21);
  }

  octBox(0, 0.635, 0, 2.4, 1.13, 1.2, 0.08, {
    facet: true,                                                      // armour chamfer: a container's own silhouette
    px: { kind: K_PLAIN, seed: 22 },                                  // door end backing, bare
    nx: { kind: K_PANEL, seed: 23, params: [1, 0, 0, 0], ...P_SHELL },  // blank end, pale composite
    pz: { kind: K_PERF, seed: 24, params: [0.055, 0.30, 0, 0], ...P_HULL },   // perforated bay backing, X-braced over
    nz: { kind: K_PERF, seed: 25, params: [0.055, 0.30, 0, 0], ...P_HULL },
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
        pz: s > 0 ? { kind: K_PLAIN, seed: 31, chamfer: 0.008, ...P_ORANGE } : null,
        nz: s < 0 ? { kind: K_PLAIN, seed: 31, chamfer: 0.008, ...P_ORANGE } : null,
        px: { kind: K_PLAIN, seed: 31, chamfer: 0, ...P_ORANGE }, nx: { kind: K_PLAIN, seed: 31, chamfer: 0, ...P_ORANGE },
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
    // X-brace per bay as ROUND-ENDED capsule bars: the reference wall
    // panels' X is a formed section with radiused ends, never a
    // square-cut flat bar
    const zN = s > 0 ? AZ : NZ, zP = s * 0.60;
    for (const [xa, xb] of [[-0.82, -0.07], [0.07, 0.82]]) {
      capBar([xa, 0.21, zP], [xb, 0.99, zP], 0.11, 0.035, zN, 4, { seed: 37, ...P_SHELL });
      capBar([xb, 0.21, zP], [xa, 0.99, zP], 0.11, 0.035, zN, 4, { seed: 37, ...P_SHELL });
    }
    // filleted boss centred on each bay -- a disc on the panel
    for (const bx of [-0.445, 0.445]) boss([bx, 0.60, zP], zN, 0.055, 0.018, { seed: 38, fillet: 0.022, face: P_TRIM });
  }

  // door end (+x): proud frame, recessed leaf, twin lock rods with cams
  const fx = 1.225;   // frame centreline; backing face sits at x=1.20
  box(1.2125, 0.60, 0, 0.025, 0.94, 1.04, { px: { kind: K_PANEL, seed: 33, chamfer: 0.016, ...P_HULL } }); // door leaf, painted, recessed inside the bare frame
  // door surround: one rounded-rectangle collar, not four mitred bars
  rrFrame([1.20, 0.08, 0.55], AY, NZ, AX, 1.04, 1.10, 0.13, 0.055, 0.05,
    { kind: K_PLAIN, seed: 34, ...P_TRIM });
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
    px: { kind: K_PANEL, seed: 42, params: [1, 0, 0, 0], ...P_SHELL }, nx: { kind: K_PANEL, seed: 43, params: [1, 0, 0, 0], ...P_SHELL },
    pz: { kind: K_PANEL, seed: 44, params: [1, 0, 0, 0], ...P_SHELL }, nz: { kind: K_PANEL, seed: 45, params: [1, 0, 0, 0], ...P_SHELL },
    diag: { kind: K_PLAIN, seed: 46 },
  });

  // corner uprights, proud of the cut edges, running up into the lid
  for (const [x, z] of [[-0.275, -0.175], [0.275, -0.175], [0.275, 0.175], [-0.275, 0.175]]) {
    plainBox(x, 0.20, z, 0.055, 0.37, 0.055, 47);
  }

  // lid: overhangs 20 mm, 45-degree chamfer all four ways, bolted top plate
  hull8(rectRing(0, 0, 0.32, 0.22, 0.36), rectRing(0, 0, 0.245, 0.145, 0.455), {
    px: { kind: K_PLAIN, seed: 48, ...P_ORANGE }, nx: { kind: K_PLAIN, seed: 48, ...P_ORANGE },
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
propDef('Console Terminal', 'lobby', '로비 안내 단말 · 갤러리 슬롯 콘솔 · 0.63 x 1.11 x 0.64 m', [0.63, 1.11, 0.64], (c) => {
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
      px: s > 0 ? { kind: K_PANEL, seed: 63, params: [1, 0, 0, 0], ...P_ORANGE } : plain(64),
      nx: s < 0 ? { kind: K_PANEL, seed: 63, params: [1, 0, 0, 0], ...P_ORANGE } : plain(64),
      pz: plain(64), nz: plain(64),
      py: { kind: K_PLAIN, seed: 64, share: false },
    });
  }

  // light run under the deck's leading edge -- the reference interiors
  // put a continuous strip on every horizontal a hand would rest on
  panel([-0.235, 0.535, 0.322], AX, AY, AZ, 0.47, 0.030, { kind: K_SEAM, seed: 69, params: [1, 0, 0, 0], accent: A_LIVE_C, chamfer: 0 });

  // grab bar across the deck's leading edge
  for (const x of [-0.20, 0.20]) plainBox(x, 0.555, 0.315, 0.035, 0.035, 0.05, 65);
  plainBox(0, 0.565, 0.335, 0.45, 0.032, 0.032, 65);

  // cable conduit down the back, clamped twice, into a junction box
  tube([0.10, 0.02, -0.245], [0.10, 0.84, -0.245], 0.028, { face: { kind: K_PLAIN, seed: 66, ...P_STEEL }, collar: 'ab' });
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
propDef('Storage Locker', 'storage', '창고 · 의무실 · 1.06 x 1.97 x 0.58 m', [1.06, 1.97, 0.58], (c) => {
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
  tube([0, 1.10, 0.20], [0, 1.10, 0.27], 0.07, { face: { kind: K_PLAIN, seed: 98, ...P_STEEL }, collar: 'b' });

  // the wheel. R 220 mm -- geometry's job, and now it does it. Painted
  // ochre the way every real valve wheel is painted something.
  wheelZ(0, 1.10, 0.305, 0.22, 0.04, 0.05, { seed: 99, hub: 0.05, ...P_OCHRE });

  // offtake up out of the drum, capped with its own flange
  tube([0, 1.24, 0], [0, 1.42, 0], 0.10, { face: { kind: K_PLAIN, seed: 100, ...P_STEEL } });
  tube([0, 1.42, 0], [0, 1.47, 0], 0.13, { face: { kind: K_SEAM, seed: 101, params: [1, 0, 0, 0] }, capB: true });

  // conduit run: up the side, junction cube, across into the riser
  tube([0.24, 0.10, 0.05], [0.24, 0.86, 0.05], 0.045, { face: { kind: K_PLAIN, seed: 102, ...P_STEEL }, collar: 'a' });
  plainBox(0.24, 0.90, 0.05, 0.11, 0.11, 0.11, 103);
  tube([0.24, 0.90, 0.05], [0.11, 0.90, 0.05], 0.045, { face: { kind: K_PLAIN, seed: 102, ...P_STEEL } });

  // second run up the back in the ref's oxide red -- the one saturated
  // colour the mood board allows a pipe
  tube([-0.10, 0.10, -0.20], [-0.10, 0.70, -0.20], 0.04, { face: { kind: K_PLAIN, seed: 108, ...P_OXIDE }, collar: 'a' });
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
propDef('Medbay Cabinet', 'medbay', '의무실 · 생명유지 캐비닛 · 0.82 x 1.56 x 0.48 m', [0.82, 1.56, 0.48], (c) => {
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
  tube([0.20, 0.02, -0.30], [0.20, 1.34, -0.30], 0.028, { face: { ...plain(255), ...P_STEEL }, collar: 'ab' });
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
propDef('Med Bed', 'medbay', '의무실 · 진료 베드 · 2.08 x 1.34 x 0.88 m', [2.08, 1.34, 0.88], (c) => {
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
  tube([-0.92, 0.60, 0], [-0.92, 1.24, 0], 0.045, { face: { ...plain(272), ...P_STEEL }, collar: 'a' });
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
  tube([-0.58, 0.12, 0.42], [-0.58, 0.80, 0.42], 0.042, { face: { ...plain(295), ...P_OXIDE }, collar: 'a' });
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
  const { hull8, rectRing, tube, stack, wheelZ, box, plainBox, K_SEAM, K_HAZARD, K_VALVE, K_PLAIN, A_OCHRE, AX, AY } = c;
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
  // shell with its two proud girth bands as one closed profile
  stack([-0.80, 0.60, 0], AX, [
    { l: 0.42, r: 0.355, face: { ...plain(304), ...P_STEEL } },
    { l: 0.07, r: 0.375, face: { kind: K_SEAM, seed: 305, params: [1, 0, 0, 0] } },
    { l: 0.62, r: 0.355, face: { ...plain(304), ...P_STEEL } },
    { l: 0.07, r: 0.375, face: { kind: K_SEAM, seed: 305, params: [1, 0, 0, 0] } },
    { l: 0.42, r: 0.355, face: { ...plain(304), ...P_STEEL } },
  ], { n: 16 });

  // top hatch with flange, front offtake with hand-wheel, gauge, drain
  stack([0.30, 0.80, 0], AY, [
    { l: 0.20, r: 0.10, face: { ...plain(306), ...P_STEEL } },
    { l: 0.05, r: 0.13, face: { kind: K_SEAM, seed: 307, params: [1, 0, 0, 0] } },   // manway flange
  ], { n: 12, capA: false });
  tube([-0.35, 0.60, 0.20], [-0.35, 0.60, 0.44], 0.065, { face: { ...plain(306), ...P_STEEL }, collar: 'a' });
  wheelZ(-0.35, 0.60, 0.47, 0.14, 0.028, 0.04, { seed: 308, hub: 0.04, ...P_OCHRE });
  box(0.10, 0.78, 0.30, 0.075, 0.075, 0.05, {
    pz: { kind: K_VALVE, seed: 309, params: [3, 0, 0, 0], chamfer: 0.006 },
    px: { ...plain(309), chamfer: 0 }, nx: { ...plain(309), chamfer: 0 },
    py: { ...plain(309), chamfer: 0 }, ny: { ...plain(309), chamfer: 0 },
  });
  tube([-0.30, 0.40, 0.14], [-0.30, 0.03, 0.14], 0.035, { face: { ...plain(310), ...P_OXIDE }, collar: 'a' });
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
propDef('Storage Drum', 'cargo', '화물칸 · 창고 · 드럼통 · 0.58 x 0.99 x 0.60 m', [0.58, 0.99, 0.60], (c) => {
  const { tube, stack, box, K_SEAM, K_HAZARD, K_PLAIN, A_OCHRE, AY } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // one closed profile: stacking tubes at 0.295 / 0.280 / 0.287 left an
  // open ring at every step, including under both seam bands
  stack([0, 0, 0], AY, [
    { l: 0.05, r: 0.295, face: plain(351) },                                                        // base chime
    { l: 0.22, r: 0.280, face: { ...plain(352), ...P_OXIDE } },
    { l: 0.06, r: 0.295, face: { kind: K_SEAM, seed: 353, params: [1, 0, 0, 0] } },                 // lower rolling seam
    { l: 0.09, r: 0.280, face: { ...plain(352), ...P_OXIDE } },
    { l: 0.13, r: 0.287, face: { kind: K_HAZARD, seed: 354, params: [0.072, 1, 0, 0], accent: A_OCHRE } },
    { l: 0.04, r: 0.280, face: { ...plain(352), ...P_OXIDE } },
    { l: 0.06, r: 0.295, face: { kind: K_SEAM, seed: 353, params: [1, 0, 0, 0] } },                 // upper rolling seam
    { l: 0.23, r: 0.280, face: { ...plain(352), ...P_OXIDE } },
    { l: 0.05, r: 0.295, face: { kind: K_SEAM, seed: 353, params: [1, 0, 0, 0] } },                 // lid chime
  ], { n: 16 });
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

/* ======================================================================
   20-25. SF PROPS BUILT ON THE ROUNDED VOCABULARY
   These six exist to exercise what the kit gained: roundRing edges,
   rrFrame collars, capBar capsules, filleted bosses, K_PERF hole
   fields, and K_SEAM as a real light run rather than a painted line.
   Every one of them is a shape the first-pass kit could not have made.
   ====================================================================== */

// ---- 20. CRYO POD -- 의무실/승무원 구획 -------------------------------
// The one prop that is unmistakably science fiction and nothing else: a
// horizontal capsule on a service plinth, a rounded canopy collar over
// a lit interior, coolant lines running into the head end, and a light
// run along the skirt. Built almost entirely from rrFrame and boss.
propDef('Cryo Pod', 'medbay', '의무실 · 동면 캡슐 · 2.42 x 0.72 x 0.95 m', [2.42, 0.72, 0.95], (c) => {
  const { hull8, octBox, rectRing, panel, plainBox, tube, boss, rrFrame, capBar,
          K_PANEL, K_SCREEN, K_SEAM, K_BUTTONS, K_PERF, K_PLAIN, A_LIVE_C, A_AMBER,
          AX, AY, AZ, NZ, NX } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // service plinth: dark, recessed, so the pale capsule floats on it
  hull8(rectRing(0, 0, 0.98, 0.34, 0.00), rectRing(0, 0, 0.94, 0.30, 0.20), {
    px: { kind: K_PERF, seed: 401, params: [0.042, 0.30, 0, 0], ...P_DARK },
    nx: { kind: K_PERF, seed: 401, params: [0.042, 0.30, 0, 0], ...P_DARK },
    pz: { kind: K_PERF, seed: 402, params: [0.042, 0.30, 0, 0], ...P_DARK },
    nz: { kind: K_PERF, seed: 402, params: [0.042, 0.30, 0, 0], ...P_DARK },
    ny: plain(403),
  });

  // capsule body: two lofts, wider at the head (-x) than the foot
  hull8(rectRing(0.02, 0, 1.06, 0.40, 0.20), rectRing(0.00, 0, 1.10, 0.43, 0.42), {
    px: { kind: K_PANEL, seed: 404, params: [1, 0, 0, 0], ...P_SHELL },
    nx: { kind: K_PANEL, seed: 404, params: [1, 0, 0, 0], ...P_SHELL },
    pz: { kind: K_PANEL, seed: 405, params: [1, 0, 0, 0], ...P_SHELL },
    nz: { kind: K_PANEL, seed: 405, params: [1, 0, 0, 0], ...P_SHELL },
  });
  hull8(rectRing(0.00, 0, 1.10, 0.43, 0.42), rectRing(-0.02, 0, 1.02, 0.36, 0.62), {
    px: plain(406), nx: plain(406), pz: plain(407), nz: plain(407),
    // py is not optional: without a cap the capsule has no top face and
    // the canopy sits over an open hole. topRound is also a no-op
    // without it, since loft only rolls a crown onto a cap that exists.
    py: { kind: K_PANEL, seed: 417, params: [0, 0, 0, 0], ...P_SHELL, share: false },
    topRound: 0.030,
  });

  // canopy: a rounded collar over a lit interior. rrFrame is the whole
  // point -- the reference hatches all have a formed collar, never four
  // mitred strips, and a cryo canopy is the same part.
  panel([-0.62, 0.622, 0.20], AX, NZ, AY, 1.24, 0.40,
    { kind: K_SCREEN, seed: 408, accent: A_LIVE_C, chamfer: 0, share: false });
  rrFrame([-0.72, 0.618, 0.28], AX, NZ, AY, 1.44, 0.56, 0.16, 0.075, 0.050,
    { kind: K_PANEL, seed: 409, params: [1, 0, 0, 0], ...P_TRIM });

  // head end: coolant lines into a manifold, every one collared
  for (const z of [-0.22, 0, 0.22]) {
    tube([-1.02, 0.30, z], [-1.18, 0.30, z], 0.036,
      { face: { ...plain(410), ...P_STEEL }, collar: 'a', capB: true });
  }
  boss([-1.06, 0.50, 0], NX, 0.10, 0.035, { seed: 411, fillet: 0.038, face: P_TRIM });

  // service bosses down each flank -- the ports a pod is plugged into
  for (const s of [-1, 1]) for (const x of [-0.55, 0.05, 0.62]) {
    boss([x, 0.40, s * 0.435], s > 0 ? AZ : NZ, 0.055, 0.020,
      { seed: 412, fillet: 0.022, face: P_TRIM });
  }

  // foot end: status readout and keypad on a slightly proud plate
  panel([0.88, 0.30, 0.44], AX, AY, AZ, 0.34, 0.18,
    { kind: K_SCREEN, seed: 413, accent: A_AMBER, chamfer: 0, share: false });
  panel([0.88, 0.10, 0.44], AX, AY, AZ, 0.34, 0.16, { kind: K_BUTTONS, seed: 414, chamfer: 0 });

  // light run along the skirt, both sides: a pod is lit from underneath
  for (const s of [-1, 1]) {
    panel([-0.92, 0.225, s * 0.415], AX, AY, s > 0 ? AZ : NZ, 1.84, 0.045,
      { kind: K_SEAM, seed: 415, params: [1, 0, 0, 0], accent: A_LIVE_C, chamfer: 0 });
  }
  // lift handles, capsule section
  for (const s of [-1, 1]) capBar([-0.70, 0.52, s * 0.44], [-0.28, 0.52, s * 0.44], 0.06, 0.030,
    s > 0 ? AZ : NZ, 4, { ...plain(416), ...P_TRIM });
});

// ---- 21. AIRLOCK HATCH -- 통로/기관실 ---------------------------------
// Reference 3's door, near enough: a rounded collar set into a wall
// slab, a dogged leaf with a hand-wheel, X-braced flanking panels and a
// hazard band over the lintel.
propDef('Airlock Hatch', 'corridor', '통로 · 에어록 해치 · 1.62 x 2.16 x 0.40 m', [1.62, 2.16, 0.40], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, boss, rrFrame, xbrace, wheelZ, tube,
          K_PANEL, K_HAZARD, K_BUTTONS, K_SEAM, K_PERF, K_PLAIN,
          A_OCHRE, A_AMBER, A_LIVE_G, AX, AY, AZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // wall slab the whole thing is set into
  octBox(0, 1.08, -0.13, 1.62, 2.16, 0.14, 0, {
    pz: { kind: K_PANEL, seed: 421, params: [1, 0, 0, 0], ...P_HULL },
    px: plain(422), nx: plain(422), py: plain(422), ny: plain(422),
  });
  // flanking bays, X-braced over a perforated backing
  for (const s of [-1, 1]) {
    panel([s > 0 ? 0.58 : -0.72, 0.30, -0.055], AX, AY, AZ, 0.14, 1.56,
      { kind: K_PERF, seed: 423, params: [0.040, 0.30, 0, 0], chamfer: 0, ...P_DARK });
  }
  for (const s of [-1, 1]) {
    xbrace([s > 0 ? 0.585 : -0.715, 0.31, -0.050], AX, AY, AZ, 0.13, 1.54, 0.055, 0.022,
      { ...plain(424), ...P_SHELL });
  }

  // the collar: the single most characteristic shape in the reference set
  rrFrame([-0.56, 0.10, -0.055], AX, AY, AZ, 1.12, 1.92, 0.26, 0.10, 0.115,
    { kind: K_PANEL, seed: 425, params: [1, 0, 0, 0], ...P_TRIM });

  // leaf, sitting inside the collar
  octBox(0, 1.06, 0.005, 0.90, 1.70, 0.10, 0, {
    pz: { kind: K_PANEL, seed: 426, params: [1, 0, 0, 0], ...P_SHELL },
    px: plain(427), nx: plain(427), py: plain(427), ny: plain(427),
  });
  // four locking dogs around the leaf, on the collar
  for (const [dx, dy] of [[-0.505, 0.62], [0.505, 0.62], [-0.505, 1.50], [0.505, 1.50]]) {
    boss([dx, dy, 0.060], AZ, 0.062, 0.028, { seed: 428, fillet: 0.026, face: P_TRIM });
  }
  wheelZ(0, 1.00, 0.115, 0.235, 0.055, 0.070, { seed: 429, ...P_OCHRE });

  // hazard band over the lintel, status strip under it
  panel([-0.56, 2.02, -0.052], AX, AY, AZ, 1.12, 0.10,
    { kind: K_HAZARD, seed: 430, params: [0.09, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  panel([-0.44, 1.955, -0.052], AX, AY, AZ, 0.88, 0.035,
    { kind: K_SEAM, seed: 431, params: [1, 0, 0, 0], accent: A_LIVE_G, chamfer: 0 });

  // control plate beside the frame
  hull8(rectRing(0.66, 0.02, 0.10, 0.055, 1.02), rectRing(0.66, 0.02, 0.10, 0.055, 1.42), {
    pz: { kind: K_BUTTONS, seed: 432 }, px: plain(433), nx: plain(433),
    py: plain(433), ny: plain(433), nz: null,
  });
  panel([0.57, 1.44, 0.070], AX, AY, AZ, 0.18, 0.10,
    { kind: K_SEAM, seed: 434, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  // conduit dropping into the mechanism
  tube([0.72, 2.10, -0.06], [0.72, 1.46, -0.06], 0.035,
    { face: { ...plain(435), ...P_STEEL }, collar: 'ab' });
});

// ---- 22. DATA CORE RACK -- 기관실/함교 --------------------------------
// A tall stack of perforated modules, each with its own light run. This
// is the prop that leans hardest on K_PERF: a hole field is the cheapest
// convincing "this is machinery" signal the kit has.
propDef('Data Core Rack', 'engine', '기관실 · 데이터 코어 랙 · 0.84 x 2.06 x 0.72 m', [0.84, 2.06, 0.72], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, capBar,
          K_PANEL, K_PERF, K_SCREEN, K_SEAM, K_RIB, K_PLAIN,
          A_LIVE_C, A_LIVE_G, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // plinth and crown
  octBox(0, 0.055, 0, 0.84, 0.11, 0.72, 0, {
    px: plain(441), nx: plain(441), pz: plain(441), nz: plain(441), ny: plain(441),
  });
  octBox(0, 1.955, 0, 0.80, 0.13, 0.68, 0, {
    px: plain(442), nx: plain(442), nz: plain(442),
    pz: { kind: K_RIB, seed: 443, params: [7, 0.09, 0, 0] },
    py: { kind: K_PERF, seed: 444, params: [0.048, 0.32, 0, 0], ...P_DARK },
    topRound: 0.024,
  });

  // corner uprights, orange: the reference racks all read as a coloured
  // frame carrying neutral modules
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    octBox(sx * 0.365, 1.02, sz * 0.305, 0.075, 1.80, 0.075, 0, {
      px: { ...plain(445), ...P_ORANGE }, nx: { ...plain(445), ...P_ORANGE },
      pz: { ...plain(445), ...P_ORANGE }, nz: { ...plain(445), ...P_ORANGE },
    });
  }

  // five modules, each a shallow drawer: perforated front, light run
  // under it, and one screen in the stack
  const ACC = [A_LIVE_C, A_LIVE_G, A_LIVE_C, A_AMBER, A_LIVE_C];
  for (let i = 0; i < 5; i++) {
    const y = 0.20 + i * 0.345;
    hull8(rectRing(0, 0, 0.325, 0.30, y), rectRing(0, 0, 0.325, 0.30, y + 0.285), {
      pz: i === 3
        ? { kind: K_SCREEN, seed: 450 + i, accent: ACC[i], share: false }
        : { kind: K_PERF, seed: 450 + i, params: [0.030, 0.32, 0, 0], ...P_DARK },
      px: { kind: K_PANEL, seed: 460 + i, params: [1, 0, 0, 0], ...P_SHELL },
      nx: { kind: K_PANEL, seed: 460 + i, params: [1, 0, 0, 0], ...P_SHELL },
      nz: plain(470),
    });
    panel([-0.30, y + 0.295, 0.315], AX, AY, AZ, 0.60, 0.032,
      { kind: K_SEAM, seed: 480 + i, params: [1, 0, 0, 0], accent: ACC[i], chamfer: 0 });
    // pull handle
    capBar([-0.20, y + 0.14, 0.325], [0.20, y + 0.14, 0.325], 0.045, 0.026, AZ, 4,
      { ...plain(490), ...P_TRIM });
  }

  // cable loom down the back, collared where it enters the crown
  for (const x of [-0.20, 0, 0.20]) {
    tube([x, 0.14, -0.325], [x, 1.90, -0.325], 0.028,
      { face: { ...plain(495), ...P_STEEL }, collar: 'b' });
  }
  for (const sx of [-1, 1]) boss([sx * 0.30, 1.955 + 0.065, 0.20], AY, 0.05, 0.018,
    { seed: 496, fillet: 0.020, face: P_TRIM });
});

// ---- 23. DECK GRATING -- 통로/화물칸 ----------------------------------
// A floor section, because reference 1 and 3 both spend half their frame
// on the deck and the kit had nothing to put there. Perforated plate in
// a capsule-edged frame with a recessed strip light down the middle.
propDef('Deck Grating', 'corridor', '통로 · 데크 그레이팅 · 1.66 x 0.13 x 1.22 m', [1.66, 0.13, 1.22], (c) => {
  const { octBox, panel, plainBox, capBar, boss, tube,
          K_PERF, K_SEAM, K_HAZARD, K_PLAIN, A_AMBER, A_OCHRE, AX, AZ, AY, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // structural tray
  octBox(0, 0.045, 0, 1.62, 0.09, 1.22, 0, {
    px: plain(501), nx: plain(501), pz: plain(501), nz: plain(501), ny: plain(501),
  });
  // two perforated deck plates either side of a light channel
  for (const s of [-1, 1]) {
    panel([-0.74, 0.095, s > 0 ? 0.54 : -0.09], AX, NZ, AY, 1.48, 0.45,
      { kind: K_PERF, seed: 502, params: [0.034, 0.30, 0, 0], chamfer: 0, ...P_HULL });
  }
  // recessed strip light down the centre channel
  panel([-0.70, 0.072, 0.055], AX, NZ, AY, 1.40, 0.11,
    { kind: K_SEAM, seed: 503, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  // capsule edging: the lip a boot actually meets
  for (const s of [-1, 1]) {
    capBar([-0.72, 0.098, s * 0.575], [0.72, 0.098, s * 0.575], 0.075, 0.026, AY, 4,
      { ...plain(504), ...P_TRIM });
    capBar([s * 0.775, 0.098, -0.52], [s * 0.775, 0.098, 0.52], 0.075, 0.026, AY, 4,
      { ...plain(504), ...P_TRIM });
  }
  // hazard edging on the two short ends, lift bosses at the corners
  for (const s of [-1, 1]) {
    panel([s > 0 ? 0.775 : -0.815, 0.0, s > 0 ? 0.61 : -0.61], s > 0 ? NZ : AZ, AY,
      s > 0 ? AX : [-1, 0, 0], 1.22, 0.09,
      { kind: K_HAZARD, seed: 505, params: [0.08, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  }
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    boss([sx * 0.66, 0.098, sz * 0.46], AY, 0.045, 0.012, { seed: 506, fillet: 0.018, face: P_TRIM });
  }
  // a conduit running under the tray, visible at both ends
  tube([-0.80, 0.045, 0.30], [0.80, 0.045, 0.30], 0.038,
    { face: { ...plain(507), ...P_STEEL }, capA: true, capB: true });
});

// ---- 24. GAS CYLINDER RACK -- 기관실/화물칸 ---------------------------
// Four bottles in an orange frame, strapped with capsule bars, hazard
// banded, valves on top. Exercises tube collars and the seam-continuous
// hazard band on curved geometry -- the drum's problem, four more times.
propDef('Gas Cylinder Rack', 'engine', '기관실 · 가스 실린더 랙 · 1.14 x 1.66 x 0.46 m', [1.14, 1.66, 0.46], (c) => {
  const { octBox, tube, stack, panel, plainBox, capBar, boss, wheelZ,
          K_HAZARD, K_SEAM, K_PLAIN, K_PERF, K_PANEL,
          A_OCHRE, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // base and two uprights, orange frame
  octBox(0, 0.055, 0, 1.14, 0.11, 0.46, 0, {
    px: { ...plain(511), ...P_ORANGE }, nx: { ...plain(511), ...P_ORANGE },
    pz: { kind: K_PERF, seed: 512, params: [0.038, 0.30, 0, 0], ...P_DARK },
    nz: { ...plain(511), ...P_ORANGE }, ny: plain(511),
  });
  for (const sx of [-1, 1]) {
    octBox(sx * 0.535, 0.86, -0.16, 0.07, 1.50, 0.10, 0, {
      px: { ...plain(513), ...P_ORANGE }, nx: { ...plain(513), ...P_ORANGE },
      pz: { ...plain(513), ...P_ORANGE }, nz: { ...plain(513), ...P_ORANGE },
      topRound: 0.018,
    });
  }
  // back rails
  for (const y of [0.62, 1.24]) {
    octBox(0, y, -0.16, 1.00, 0.075, 0.075, 0, {
      px: { ...plain(514), ...P_ORANGE }, nx: { ...plain(514), ...P_ORANGE },
      pz: { ...plain(514), ...P_ORANGE }, nz: { ...plain(514), ...P_ORANGE },
    });
  }

  // four bottles. Each is three tube segments so the hazard band is its
  // own wrapped ring, exactly like the storage drum's.
  const XS = [-0.39, -0.13, 0.13, 0.39];
  XS.forEach((x, i) => {
    const z = 0.02;
    // one profile per bottle: the 0.112 -> 0.086 shoulder was a hole
    stack([x, 0.11, z], AY, [
      { l: 0.19, r: 0.112, face: { ...plain(521), ...P_STEEL } },
      { l: 0.14, r: 0.116, face: { kind: K_HAZARD, seed: 522, params: [0.058, 1, 0, 0], accent: A_OCHRE } },
      { l: 0.80, r: 0.112, face: { ...plain(523), ...(i % 2 ? P_HULL : P_OCHRE) } },
      { l: 0.10, r: 0.086, face: { ...plain(524), ...P_STEEL } },     // shoulder
      { l: 0.10, r: 0.042, face: { ...plain(525), ...P_STEEL } },     // neck
    ], { n: 12 });
    boss([x, 1.42, z], AY, 0.030, 0.026, { seed: 526, fillet: 0.014, face: P_TRIM, n: 8 });
    wheelZ(x, 1.50, z, 0.062, 0.020, 0.026, { seed: 527, ...P_OCHRE, hub: 0.018, spoke: 0.016 });
  });

  // retaining straps across all four, capsule section
  for (const y of [0.66, 1.20]) {
    capBar([-0.50, y, 0.135], [0.50, y, 0.135], 0.085, 0.028, AZ, 4, { ...plain(528), ...P_TRIM });
  }
  // placard and a small status strip on the left upright
  panel([-0.565, 1.00, 0.055], AX, AY, AZ, 0.10, 0.24,
    { kind: K_PANEL, seed: 529, params: [1, 0, 0, 0], chamfer: 0, ...P_SHELL });
  panel([-0.565, 0.72, 0.055], AX, AY, AZ, 0.10, 0.030,
    { kind: K_SEAM, seed: 530, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
});

// ---- 25. CEILING LIGHT -- 통로 ----------------------------------------
// Small, cheap, and the thing the corridor is actually missing: every
// reference frame is organised around a run of ceiling lights. Rolled
// housing, capsule lens, mounting bosses.
propDef('Ceiling Light', 'corridor', '통로 · 천장 조명 · 1.42 x 0.31 x 0.34 m', [1.42, 0.31, 0.34], (c) => {
  const { hull8, rectRing, panel, capBar, boss, plainBox, tube,
          K_SEAM, K_RIB, K_PLAIN, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // housing: wider at the ceiling, tapering down to the lens
  // B must be the LOWER ring: loft() takes its up axis from the rings'
  // winding, so passing the ceiling ring first inverts the whole solid
  hull8(rectRing(0, 0, 0.64, 0.115, 0.06), rectRing(0, 0, 0.71, 0.17, 0.26), {
    px: { kind: K_RIB, seed: 541, params: [4, 0.09, 0, 0], ...P_SHELL },
    nx: { kind: K_RIB, seed: 541, params: [4, 0.09, 0, 0], ...P_SHELL },
    pz: { kind: K_RIB, seed: 542, params: [14, 0.09, 0, 0], ...P_SHELL },
    nz: { kind: K_RIB, seed: 542, params: [14, 0.09, 0, 0], ...P_SHELL },
    ny: plain(543), py: plain(543), topRound: 0.020,
  });
  // the lens: one long run, the brightest thing in the kit
  panel([-0.62, 0.055, 0.10], AX, NZ, AY, 1.24, 0.20,
    { kind: K_SEAM, seed: 544, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0, share: false });
  // lens retainer, capsule section on all four sides
  for (const s of [-1, 1]) {
    capBar([-0.63, 0.058, s * 0.105], [0.63, 0.058, s * 0.105], 0.035, 0.018, AY, 4,
      { ...plain(545), ...P_TRIM });
  }
  // mounting bosses and the feed conduit
  for (const sx of [-1, 1]) boss([sx * 0.52, 0.26, 0], AY, 0.048, 0.030,
    { seed: 546, fillet: 0.020, face: P_TRIM });
  tube([0.20, 0.24, 0], [0.20, 0.34, 0], 0.030,
    { face: { ...plain(547), ...P_STEEL }, collar: 'a', capB: true });
});

/* ======================================================================
   26-35. SECOND SF SET
   Wall furniture, ceiling runs, and the vertical circulation the kit had
   nothing for. Same rules as 20-25: rounded masses, formed collars,
   capsule sections, perforation for machinery, and A_AMBER for anything
   that glows warm.
   ====================================================================== */

// ---- 26. EVA SUIT LOCKER -- 통로/기관실 -------------------------------
propDef('EVA Suit Locker', 'corridor', '통로 · 우주복 로커 · 0.92 x 2.12 x 0.62 m', [0.92, 2.12, 0.62], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, rrFrame, capBar,
          K_PANEL, K_SCREEN, K_SEAM, K_PERF, K_RIB, K_PLAIN,
          A_AMBER, A_LIVE_G, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  octBox(0, 0.06, 0, 0.92, 0.12, 0.56, 0, {
    px: plain(601), nx: plain(601), pz: plain(601), nz: plain(601), ny: plain(601),
  });
  // body, tapering slightly back as it rises
  hull8(rectRing(0, 0, 0.44, 0.26, 0.12), rectRing(0, -0.01, 0.42, 0.24, 1.96), {
    px: { kind: K_RIB, seed: 602, params: [3, 0.10, 0, 0], ...P_HULL },
    nx: { kind: K_RIB, seed: 602, params: [3, 0.10, 0, 0], ...P_HULL },
    nz: plain(603),
    pz: { kind: K_PANEL, seed: 604, params: [1, 0, 0, 0], ...P_SHELL },
  });
  octBox(0, 2.03, 0, 0.86, 0.14, 0.52, 0, {
    px: plain(605), nx: plain(605), nz: plain(605),
    pz: { kind: K_PERF, seed: 606, params: [0.034, 0.30, 0, 0], ...P_DARK },
    py: plain(605), topRound: 0.022,
  });

  // the door: a tall rounded window into the bay, with a formed collar
  panel([-0.26, 0.42, 0.262], AX, AY, AZ, 0.52, 1.28,
    { kind: K_SCREEN, seed: 607, accent: A_LIVE_G, chamfer: 0, share: false });
  rrFrame([-0.33, 0.35, 0.258], AX, AY, AZ, 0.66, 1.42, 0.20, 0.075, 0.045,
    { kind: K_PANEL, seed: 608, params: [1, 0, 0, 0], ...P_TRIM });
  // handle and latch
  capBar([0.30, 0.90, 0.300], [0.30, 1.28, 0.300], 0.055, 0.030, AZ, 4, { ...plain(609), ...P_OCHRE });
  boss([0.30, 0.72, 0.290], AZ, 0.048, 0.022, { seed: 610, fillet: 0.020, face: P_TRIM });
  // status strip and umbilical
  panel([-0.30, 1.84, 0.262], AX, AY, AZ, 0.60, 0.034,
    { kind: K_SEAM, seed: 611, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  tube([-0.30, 0.30, 0.30], [-0.30, 1.72, 0.30], 0.030,
    { face: { ...plain(612), ...P_STEEL }, collar: 'ab' });
});

// ---- 27. WALL JUNCTION BOX -- 기관실 ----------------------------------
propDef('Wall Junction Box', 'engine', '기관실 · 배선 정션 박스 · 0.62 x 0.90 x 0.30 m', [0.62, 0.90, 0.30], (c) => {
  const { octBox, panel, plainBox, tube, boss, rrFrame, capBar,
          K_PANEL, K_BUTTONS, K_SCREEN, K_SEAM, K_HAZARD, K_PLAIN,
          A_AMBER, A_OCHRE, A_LIVE_C, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  octBox(0, 0.37, 0.09, 0.62, 0.74, 0.18, 0, {
    pz: { kind: K_PANEL, seed: 621, params: [1, 0, 0, 0], ...P_HULL },
    px: plain(622), nx: plain(622), py: plain(622), ny: plain(622),
    topRound: 0.016,
  });
  // hinged cover, standing proud, with a formed lip
  rrFrame([-0.24, 0.13, 0.180], AX, AY, AZ, 0.48, 0.48, 0.09, 0.045, 0.040,
    { kind: K_PANEL, seed: 623, params: [1, 0, 0, 0], ...P_TRIM });
  panel([-0.20, 0.17, 0.182], AX, AY, AZ, 0.40, 0.20, { kind: K_BUTTONS, seed: 624, chamfer: 0 });
  panel([-0.20, 0.39, 0.182], AX, AY, AZ, 0.40, 0.16,
    { kind: K_SCREEN, seed: 625, accent: A_AMBER, chamfer: 0, share: false });
  // hazard strip along the top, status line under the cover
  panel([-0.28, 0.66, 0.182], AX, AY, AZ, 0.56, 0.055,
    { kind: K_HAZARD, seed: 626, params: [0.06, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  panel([-0.22, 0.075, 0.182], AX, AY, AZ, 0.44, 0.028,
    { kind: K_SEAM, seed: 627, params: [1, 0, 0, 0], accent: A_LIVE_C, chamfer: 0 });
  // conduits in and out
  for (const [x, y0, y1] of [[-0.22, 0.74, 0.62], [0.22, 0.74, 0.62]]) {
    tube([x, y0 + 0.06, 0.09], [x, y1, 0.09], 0.032, { face: { ...plain(628), ...P_STEEL }, collar: 'b' });
  }
  tube([0, 0.00, 0.09], [0, -0.08, 0.09], 0.040, { face: { ...plain(628), ...P_STEEL }, collar: 'a', capB: true });
  for (const sx of [-1, 1]) boss([sx * 0.26, 0.68, 0.182], AZ, 0.034, 0.014,
    { seed: 629, fillet: 0.014, face: P_TRIM, n: 8 });
});

// ---- 28. SPECIMEN TANK -- 의무실 --------------------------------------
propDef('Specimen Tank', 'medbay', '의무실 · 표본 격리 탱크 · 0.72 x 1.86 x 0.72 m', [0.72, 1.86, 0.72], (c) => {
  const { octBox, tube, stack, panel, plainBox, boss, capBar, rrFrame,
          K_SCREEN, K_SEAM, K_PERF, K_BUTTONS, K_PLAIN, K_HAZARD,
          A_LIVE_C, A_AMBER, A_OCHRE, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // pedestal
  octBox(0, 0.10, 0, 0.72, 0.20, 0.72, 0, {
    px: { kind: K_PERF, seed: 631, params: [0.036, 0.30, 0, 0], ...P_DARK },
    nx: { kind: K_PERF, seed: 631, params: [0.036, 0.30, 0, 0], ...P_DARK },
    pz: { kind: K_PERF, seed: 632, params: [0.036, 0.30, 0, 0], ...P_DARK },
    nz: { kind: K_PERF, seed: 632, params: [0.036, 0.30, 0, 0], ...P_DARK },
    ny: plain(633),
  });
  octBox(0, 0.27, 0, 0.66, 0.14, 0.66, 0, {
    px: plain(634), nx: plain(634), pz: plain(634), nz: plain(634), py: plain(634), topRound: 0.018,
  });
  // the vessel: one closed profile, banded top and bottom, so the
  // 0.278 / 0.275 / 0.265 steps do not leave rings open
  // start 5 mm inside the pedestal: the first shoulder annulus used to
  // land at y = 0.34, exactly on the pedestal's top cap
  stack([0, 0.335, 0], AY, [
    { l: 0.045, r: 0.278, face: { kind: K_HAZARD, seed: 643, params: [0.072, 1, 0, 0], accent: A_OCHRE } },
    { l: 0.10, r: 0.275, face: { ...plain(635), ...P_TRIM } },
    { l: 0.98, r: 0.265, face: { kind: K_SCREEN, seed: 636, accent: A_LIVE_C } },   // the glass
    { l: 0.12, r: 0.275, face: { ...plain(637), ...P_TRIM } },
  ], { n: 16, capA: false });
  // four ribs standing off the glass
  for (let k = 0; k < 4; k++) {
    const a = Math.PI / 4 + k * Math.PI / 2;
    const x = Math.cos(a) * 0.272, z = Math.sin(a) * 0.272;
    capBar([x, 0.42, z], [x, 1.46, z], 0.075, 0.028, [Math.cos(a), 0, Math.sin(a)], 4,
      { ...plain(638), ...P_ORANGE });
  }
  // head assembly
  octBox(0, 1.66, 0, 0.60, 0.22, 0.60, 0, {
    px: plain(639), nx: plain(639), nz: plain(639),
    pz: { kind: K_BUTTONS, seed: 640 },
    py: plain(639), topRound: 0.020,
  });
  panel([-0.24, 1.78, 0.302], AX, AY, AZ, 0.48, 0.030,
    { kind: K_SEAM, seed: 641, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  boss([0, 1.79, 0], AY, 0.075, 0.030, { seed: 642, fillet: 0.030, face: P_TRIM });
});

// ---- 29. FIRE SUPPRESSION STATION -- 통로 -----------------------------
propDef('Suppression Station', 'corridor', '통로 · 소화 설비 스테이션 · 0.80 x 1.28 x 0.40 m', [0.80, 1.28, 0.40], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, rrFrame, capBar, wheelZ,
          K_PANEL, K_HAZARD, K_SEAM, K_PERF, K_PLAIN, A_OCHRE, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // recessed cabinet, orange because this is the one prop that must be
  // findable from the far end of a dark corridor
  octBox(0, 0.64, 0.08, 0.80, 1.28, 0.16, 0, {
    pz: { kind: K_PANEL, seed: 651, params: [1, 0, 0, 0], ...P_ORANGE },
    px: { ...plain(652), ...P_ORANGE }, nx: { ...plain(652), ...P_ORANGE },
    py: { ...plain(652), ...P_ORANGE }, ny: { ...plain(652), ...P_ORANGE },
    topRound: 0.016,
  });
  rrFrame([-0.34, 0.20, 0.160], AX, AY, AZ, 0.68, 0.88, 0.12, 0.055, 0.055,
    { kind: K_PANEL, seed: 653, params: [1, 0, 0, 0], ...P_TRIM });
  panel([-0.27, 0.27, 0.162], AX, AY, AZ, 0.54, 0.74,
    { kind: K_PERF, seed: 654, params: [0.030, 0.32, 0, 0], chamfer: 0, ...P_DARK });
  // bottle and hose reel behind the grille
  tube([-0.16, 0.30, 0.10], [-0.16, 0.86, 0.10], 0.085, { face: { ...plain(655), ...P_STEEL } });
  tube([-0.16, 0.86, 0.10], [-0.16, 0.94, 0.10], 0.034,
    { face: { ...plain(656), ...P_STEEL }, collar: 'a', capB: true });
  wheelZ(0.18, 0.58, 0.13, 0.135, 0.032, 0.048, { seed: 657, ...P_OCHRE, hub: 0.038, spoke: 0.028 });
  // hazard band and call strip
  panel([-0.38, 1.12, 0.162], AX, AY, AZ, 0.76, 0.08,
    { kind: K_HAZARD, seed: 658, params: [0.062, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  panel([-0.30, 0.13, 0.162], AX, AY, AZ, 0.60, 0.032,
    { kind: K_SEAM, seed: 659, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  for (const sx of [-1, 1]) boss([sx * 0.30, 1.02, 0.162], AZ, 0.038, 0.016,
    { seed: 660, fillet: 0.016, face: P_TRIM, n: 8 });
});

// ---- 30. OVERHEAD PIPE RUN -- 통로/기관실 -----------------------------
// Reference 1 gives a third of its frame to a pipe bundle on brackets.
// Straight tubes, real collars, capsule clamps, one lagged line.
propDef('Overhead Pipe Run', 'corridor', '통로 · 천장 배관 다발 · 2.44 x 0.58 x 0.62 m', [2.44, 0.58, 0.62], (c) => {
  const { octBox, tube, panel, plainBox, boss, capBar, hull8, rectRing,
          K_PIPE, K_SEAM, K_PLAIN, K_HAZARD, A_AMBER, A_OCHRE, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // two hangers dropping from the ceiling
  for (const x of [-0.78, 0.78]) {
    octBox(x, 0.42, 0, 0.09, 0.20, 0.58, 0, {
      px: plain(671), nx: plain(671), pz: plain(671), nz: plain(671), py: plain(671),
    });
    for (const s of [-1, 1]) {
      capBar([x, 0.34, s * 0.27], [x, 0.10, s * 0.20], 0.075, 0.028, s > 0 ? AZ : NZ, 4,
        { ...plain(672), ...P_TRIM });
    }
  }
  // four lines at different diameters -- a bundle reads by CONTRAST of
  // gauge, not by count
  const LINES = [[-0.22, 0.062, P_STEEL], [-0.04, 0.045, P_HULL], [0.11, 0.085, P_OXIDE], [0.26, 0.034, P_STEEL]];
  for (const [z, r, pal] of LINES) {
    tube([-1.20, 0.20, z], [1.20, 0.20, z], r,
      { face: { ...plain(673), ...pal }, capA: true, capB: true });
    // collars where each line passes its hangers
    for (const x of [-0.78, 0.78]) {
      tube([x - 0.055, 0.20, z], [x + 0.055, 0.20, z], r * 1.30, { face: { ...plain(674), ...P_TRIM } });
    }
  }
  // one lagged line below, banded
  tube([-1.20, 0.055, 0.02], [1.20, 0.055, 0.02], 0.105,
    { face: { kind: K_PIPE, seed: 675, params: [0, 0, 0, 0], ...P_HULL }, capA: true, capB: true });
  for (const x of [-0.50, 0.50]) {
    tube([x - 0.045, 0.055, 0.02], [x + 0.045, 0.055, 0.02], 0.112,
      { face: { kind: K_HAZARD, seed: 676, params: [0.056, 1, 0, 0], accent: A_OCHRE } });
  }
  // a strip light clipped to the underside of the bundle
  panel([-1.00, 0.005, 0.14], AX, NZ, AY, 2.00, 0.06,
    { kind: K_SEAM, seed: 677, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
});

// ---- 31. ACCESS LADDER -- 기관실/통로 ---------------------------------
propDef('Access Ladder', 'engine', '기관실 · 점검 사다리 · 0.68 x 2.40 x 0.34 m', [0.68, 2.40, 0.34], (c) => {
  const { octBox, tube, panel, plainBox, boss, capBar, rrFrame,
          K_SEAM, K_HAZARD, K_PLAIN, K_PERF, A_OCHRE, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // two stiles, orange, standing off the wall on brackets
  for (const sx of [-1, 1]) {
    octBox(sx * 0.25, 1.20, 0.06, 0.075, 2.40, 0.11, 0, {
      px: { ...plain(681), ...P_ORANGE }, nx: { ...plain(681), ...P_ORANGE },
      pz: { ...plain(681), ...P_ORANGE }, nz: { ...plain(681), ...P_ORANGE },
      py: { ...plain(681), ...P_ORANGE }, ny: { ...plain(681), ...P_ORANGE },
      topRound: 0.018,
    });
    for (const y of [0.22, 1.20, 2.18]) {
      octBox(sx * 0.25, y, -0.07, 0.065, 0.10, 0.16, 0, {
        px: plain(682), nx: plain(682), pz: plain(682), nz: plain(682), py: plain(682), ny: plain(682),
      });
      boss([sx * 0.25, y, -0.15], NZ, 0.042, 0.014, { seed: 683, fillet: 0.016, face: P_TRIM, n: 8 });
    }
  }
  // rungs: capsule bars, which is what a real rung section is
  for (let k = 0; k < 8; k++) {
    const y = 0.24 + k * 0.28;
    capBar([-0.235, y, 0.115], [0.235, y, 0.115], 0.055, 0.030, AZ, 5, { ...plain(684), ...P_TRIM });
  }
  // hazard on the bottom stile faces, guide light at the top
  for (const sx of [-1, 1]) {
    panel([sx > 0 ? 0.2875 : -0.2125, 0.02, 0.115], AX, AY, AZ, 0.075, 0.30,
      { kind: K_HAZARD, seed: 685, params: [0.05, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  }
  panel([-0.20, 2.35, 0.116], AX, AY, AZ, 0.40, 0.028,
    { kind: K_SEAM, seed: 686, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
});

// ---- 32. WALL MONITOR BANK -- 함교/기관실 -----------------------------
propDef('Monitor Bank', 'lounge', '관측 라운지 · 벽면 모니터 뱅크 · 1.52 x 0.96 x 0.34 m', [1.52, 0.96, 0.34], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, rrFrame, capBar,
          K_PANEL, K_SCREEN, K_BUTTONS, K_SEAM, K_RIB, K_PLAIN,
          A_LIVE_C, A_AMBER, A_LIVE_G, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // backing plate, leaning out at the bottom
  hull8(rectRing(0, 0.06, 0.76, 0.08, 0.00), rectRing(0, -0.02, 0.76, 0.08, 0.96), {
    pz: { kind: K_PANEL, seed: 691, params: [1, 0, 0, 0], ...P_HULL },
    px: plain(692), nx: plain(692), py: plain(692), ny: plain(692),
  });
  // three screens in formed collars, the centre one larger
  const S = [[-0.50, 0.34, 0.40, 0.34, A_LIVE_C], [0, 0.30, 0.52, 0.44, A_AMBER], [0.50, 0.34, 0.40, 0.34, A_LIVE_G]];
  S.forEach(([x, y, w, h, acc], i) => {
    panel([x - w / 2, y, 0.098], AX, AY, AZ, w, h,
      { kind: K_SCREEN, seed: 693 + i, accent: acc, chamfer: 0, share: false });
    rrFrame([x - w / 2 - 0.045, y - 0.045, 0.094], AX, AY, AZ, w + 0.09, h + 0.09, 0.075, 0.042, 0.035,
      { kind: K_PANEL, seed: 700 + i, params: [1, 0, 0, 0], ...P_TRIM });
  });
  // keypad rail under the screens, vent above
  panel([-0.60, 0.10, 0.098], AX, AY, AZ, 1.20, 0.14, { kind: K_BUTTONS, seed: 703, chamfer: 0 });
  panel([-0.66, 0.84, 0.098], AX, AY, AZ, 1.32, 0.10,
    { kind: K_RIB, seed: 704, params: [16, 0.09, 0, 0], chamfer: 0, ...P_DARK });
  panel([-0.62, 0.795, 0.100], AX, AY, AZ, 1.24, 0.026,
    { kind: K_SEAM, seed: 705, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  // grab rail and cable drop
  capBar([-0.66, 0.045, 0.115], [0.66, 0.045, 0.115], 0.05, 0.028, AZ, 4, { ...plain(706), ...P_TRIM });
  tube([0.70, 0.00, 0.02], [0.70, 0.90, 0.02], 0.030, { face: { ...plain(707), ...P_STEEL }, collar: 'ab' });
  for (const sx of [-1, 1]) boss([sx * 0.70, 0.92, 0.090], AZ, 0.036, 0.014,
    { seed: 708, fillet: 0.014, face: P_TRIM, n: 8 });
});

// ---- 33. CONTROL PEDESTAL -- 함교 -------------------------------------
propDef('Control Pedestal', 'lounge', '관측 라운지 · 제어 페데스탈 · 0.72 x 1.18 x 0.66 m', [0.72, 1.18, 0.66], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, capBar, rrFrame,
          K_PANEL, K_SCREEN, K_BUTTONS, K_SEAM, K_PERF, K_PLAIN,
          A_LIVE_C, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  octBox(0, 0.05, 0, 0.72, 0.10, 0.66, 0, {
    px: plain(711), nx: plain(711), pz: plain(711), nz: plain(711), ny: plain(711),
  });
  // a single column, waisted: narrow at the middle, flaring to the head
  hull8(rectRing(0, 0, 0.24, 0.22, 0.10), rectRing(0, 0, 0.15, 0.14, 0.52), {
    px: plain(712), nx: plain(712), pz: plain(712),
    nz: { kind: K_PERF, seed: 713, params: [0.030, 0.30, 0, 0], ...P_DARK },
  });
  hull8(rectRing(0, 0, 0.15, 0.14, 0.52), rectRing(0, 0.03, 0.34, 0.26, 0.86), {
    px: { kind: K_PANEL, seed: 714, params: [1, 0, 0, 0], ...P_SHELL },
    nx: { kind: K_PANEL, seed: 714, params: [1, 0, 0, 0], ...P_SHELL },
    pz: plain(715), nz: plain(715),
  });
  // the head: a sloped deck, keys on the near half, screen on the far
  const hB = rectRing(0, 0.03, 0.34, 0.26, 0.86);
  const hT = [[0.34, 1.10, 0.20], [0.34, 1.10, -0.32], [-0.34, 1.10, -0.32], [-0.34, 1.10, 0.20]];
  hull8(hB, hT, {
    pz: { kind: K_BUTTONS, seed: 716 },
    px: plain(717), nx: plain(717), nz: plain(717),
    py: { kind: K_SCREEN, seed: 718, accent: A_LIVE_C, share: false },
    topRound: 0.022,
  });
  // hand rail across the front of the deck, strip light under the head
  capBar([-0.28, 1.07, 0.235], [0.28, 1.07, 0.235], 0.05, 0.028, AZ, 4, { ...plain(719), ...P_TRIM });
  panel([-0.30, 0.845, 0.295], AX, AY, AZ, 0.60, 0.030,
    { kind: K_SEAM, seed: 720, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  for (const sx of [-1, 1]) boss([sx * 0.30, 0.70, 0], sx > 0 ? AX : [-1, 0, 0], 0.050, 0.018,
    { seed: 721, fillet: 0.020, face: P_TRIM });
  tube([0, 0.10, -0.24], [0, 0.60, -0.24], 0.034, { face: { ...plain(722), ...P_STEEL }, collar: 'ab' });
});

// ---- 34. FUEL CELL STACK -- 기관실 ------------------------------------
propDef('Fuel Cell Stack', 'engine', '기관실 · 연료 전지 스택 · 0.96 x 1.34 x 0.78 m', [0.96, 1.34, 0.78], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, capBar,
          K_PERF, K_RIB, K_SEAM, K_HAZARD, K_PANEL, K_PLAIN,
          A_LIVE_G, A_OCHRE, A_AMBER, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  octBox(0, 0.075, 0, 0.96, 0.15, 0.78, 0, {
    px: plain(731), nx: plain(731), pz: plain(731), nz: plain(731), ny: plain(731),
  });
  // seven cell plates, alternating finned and perforated: a stack reads
  // by rhythm, and a rhythm needs two states
  for (let k = 0; k < 7; k++) {
    const y = 0.17 + k * 0.135;
    const fin = k % 2 === 0;
    octBox(0, y + 0.055, 0, 0.86, 0.11, 0.70, 0, {
      pz: fin ? { kind: K_RIB, seed: 740 + k, params: [12, 0.09, 0, 0], ...P_HULL }
              : { kind: K_PERF, seed: 740 + k, params: [0.026, 0.32, 0, 0], ...P_DARK },
      nz: fin ? { kind: K_RIB, seed: 750 + k, params: [12, 0.09, 0, 0], ...P_HULL }
              : { kind: K_PERF, seed: 750 + k, params: [0.026, 0.32, 0, 0], ...P_DARK },
      px: { ...plain(760), ...(fin ? P_SHELL : P_TRIM) },
      nx: { ...plain(760), ...(fin ? P_SHELL : P_TRIM) },
    });
  }
  // tie rods clamping the stack, with real nuts
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    tube([sx * 0.40, 0.14, sz * 0.32], [sx * 0.40, 1.18, sz * 0.32], 0.022,
      { face: { ...plain(761), ...P_STEEL } });
    boss([sx * 0.40, 1.17, sz * 0.32], AY, 0.036, 0.024, { seed: 762, fillet: 0.010, face: P_TRIM, n: 8 });
  }
  // head plate, bus bars, and the coolant pair
  octBox(0, 1.24, 0, 0.92, 0.14, 0.74, 0, {
    px: plain(763), nx: plain(763), nz: plain(763),
    pz: { kind: K_PANEL, seed: 764, params: [1, 0, 0, 0], ...P_SHELL },
    py: plain(763), topRound: 0.020,
  });
  for (const z of [-0.16, 0.16]) {
    tube([-0.30, 1.31, z], [0.30, 1.31, z], 0.032, { face: { ...plain(765), ...P_OXIDE }, capA: true, capB: true });
  }
  panel([-0.36, 1.18, 0.372], AX, AY, AZ, 0.72, 0.030,
    { kind: K_SEAM, seed: 766, params: [1, 0, 0, 0], accent: A_LIVE_G, chamfer: 0 });
  panel([-0.40, 0.02, 0.392], AX, AY, AZ, 0.80, 0.07,
    { kind: K_HAZARD, seed: 767, params: [0.058, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
});

// ---- 35. DRONE DOCK -- 기관실/화물칸 ----------------------------------
propDef('Drone Dock', 'engine', '기관실 · 정비 드론 도크 · 1.04 x 0.86 x 0.62 m', [1.04, 0.86, 0.62], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, capBar, rrFrame,
          K_PANEL, K_PERF, K_SEAM, K_SCREEN, K_HAZARD, K_PLAIN,
          A_LIVE_C, A_AMBER, A_OCHRE, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // an alcove: back wall, floor tray, and a hood over the opening
  octBox(0, 0.43, -0.24, 1.04, 0.86, 0.14, 0, {
    pz: { kind: K_PERF, seed: 771, params: [0.032, 0.30, 0, 0], ...P_DARK },
    px: plain(772), nx: plain(772), nz: plain(772), py: plain(772), ny: plain(772),
  });
  hull8(rectRing(0, 0.02, 0.46, 0.16, 0.06), rectRing(0, 0.06, 0.44, 0.12, 0.18), {
    pz: { kind: K_PANEL, seed: 773, params: [1, 0, 0, 0], ...P_SHELL },
    px: plain(774), nx: plain(774), nz: plain(774),
    py: { kind: K_PERF, seed: 775, params: [0.030, 0.30, 0, 0], ...P_DARK, share: false },
  });
  hull8(rectRing(0, -0.06, 0.48, 0.14, 0.72), rectRing(0, -0.02, 0.50, 0.18, 0.86), {
    pz: { ...plain(776), ...P_ORANGE }, px: { ...plain(776), ...P_ORANGE },
    nx: { ...plain(776), ...P_ORANGE }, nz: plain(777),
    py: { ...plain(776), ...P_ORANGE }, topRound: 0.020,
  });
  // charging contacts on the back wall, in a formed surround
  rrFrame([-0.22, 0.34, -0.165], AX, AY, AZ, 0.44, 0.30, 0.08, 0.040, 0.030,
    { kind: K_PANEL, seed: 778, params: [1, 0, 0, 0], ...P_TRIM });
  for (const sx of [-1, 1]) boss([sx * 0.10, 0.49, -0.160], AZ, 0.042, 0.026,
    { seed: 779, fillet: 0.016, face: P_TRIM, n: 8 });
  panel([-0.16, 0.20, -0.166], AX, AY, AZ, 0.32, 0.11,
    { kind: K_SCREEN, seed: 780, accent: A_LIVE_C, chamfer: 0, share: false });
  // guide lights down both sides of the bay, hazard on the lip
  for (const sx of [-1, 1]) {
    panel([sx > 0 ? 0.445 : -0.475, 0.20, -0.14], AX, AY, AZ, 0.03, 0.50,
      { kind: K_SEAM, seed: 781, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  }
  panel([-0.44, 0.075, 0.185], AX, AY, AZ, 0.88, 0.055,
    { kind: K_HAZARD, seed: 782, params: [0.055, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  capBar([-0.40, 0.115, 0.175], [0.40, 0.115, 0.175], 0.05, 0.024, AY, 4, { ...plain(783), ...P_TRIM });
  tube([0.46, 0.86, -0.22], [0.46, 0.50, -0.22], 0.030, { face: { ...plain(784), ...P_STEEL }, collar: 'ab' });
});

/* ======================================================================
   36-38. STORY PROPS -- 탈출 갤러리
   The three objects the script actually turns on: the core the player
   carries three of, the six-slot interface that counts them, and the
   capsule door at the end of it.

   The state problem, and how it is solved here. Over the story the
   interface goes 3 charged -> 4 -> 5 -> 6, and the sixth stops at 51%.
   A baked atlas cannot express that. So the console is baked EMPTY --
   six dark receptacles -- and carries six named sockets; the game
   instances an Energy Core prop into slot1..slot6 as each is delivered.
   The per-slot readouts stay dark in the bake for the same reason and
   are meant to be driven at runtime.
   ====================================================================== */

// ---- 36. ENERGY CORE -- 픽업 ------------------------------------------
// Lying on its side, axis along +Z, so the same mesh works as a floor
// pickup and as the thing socketed into the console.
propDef('Energy Core', 'cargo', '픽업 · 표준 규격 에너지 코어 · 0.16 x 0.16 x 0.39 m', [0.16, 0.16, 0.39], (c) => {
  const { stack, boss, panel, capBar, socket,
          K_SEAM, K_RIB, K_PLAIN, K_HAZARD, A_LIVE_G, A_OCHRE, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });
  const y = 0.076;   // resting height: the widest collar (r=0.078,
                     // 12-gon) touches the floor at r*cos(15deg)
  stack([0, y, 0], AZ, [
    { l: 0.045, r: 0.040, face: { ...plain(801), ...P_TRIM } },                                  // contact pin
    { l: 0.040, r: 0.072, face: { kind: K_RIB, seed: 802, params: [8, 0.09, 0, 0], ...P_TRIM } }, // seating collar
    { l: 0.043, r: 0.058, face: { ...plain(803), ...P_STEEL } },
    { l: 0.104, r: 0.062, face: { kind: K_SEAM, seed: 804, params: [1, 0, 0, 0], accent: A_LIVE_G } }, // the cell
    { l: 0.043, r: 0.058, face: { ...plain(805), ...P_STEEL } },
    { l: 0.040, r: 0.072, face: { kind: K_RIB, seed: 806, params: [8, 0.09, 0, 0], ...P_TRIM } },
    { l: 0.030, r: 0.078, face: { kind: K_HAZARD, seed: 807, params: [0.048, 1, 0, 0], accent: A_OCHRE } },
    { l: 0.015, r: 0.058, face: { ...plain(808), ...P_TRIM } },
  ], { n: 12 });
  boss([0, y, 0.360], AZ, 0.024, 0.010, { seed: 809, fillet: 0.010, face: P_TRIM, n: 8 });
  // grab bars: the player carries this in both hands
  for (const s of [-1, 1]) {
    capBar([s * 0.056, y + 0.052, 0.150], [s * 0.056, y + 0.052, 0.252], 0.028, 0.014,
      AY, 4, { ...plain(810), ...P_TRIM });
  }
  socket('contact', [0, y, 0.00], NZ);   // the face that seats in a slot
});

// ---- 37. ENERGY PACK INTERFACE -- 탈출 갤러리 -------------------------
propDef('Energy Pack Interface', 'gallery', '탈출 갤러리 · 에너지팩 인터페이스 · 2.31 x 1.06 x 0.86 m', [2.31, 1.06, 0.86], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, rrFrame, capBar, socket,
          hullRing, capPoly, ringAt,
          K_PANEL, K_SCREEN, K_SEAM, K_PERF, K_HAZARD, K_RIB, K_PLAIN,
          A_LIVE_G, A_AMBER, A_ALARM, A_OCHRE, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // plinth, body leaning back, and a label plate across the crown
  octBox(0, 0.055, 0, 2.28, 0.11, 0.76, 0, {
    px: plain(821), nx: plain(821), nz: plain(821), ny: plain(821),
    pz: { kind: K_PERF, seed: 822, params: [0.038, 0.30, 0, 0], ...P_DARK },
  });
  /* the slot band is vertical now. It used to lean: the front face ran
     from z = 0.36 at y = 0.11 to z = 0.31 at y = 0.82, so a flat slot
     collar placed at one z was buried at the bottom of its own travel
     and floating at the top. Hardware that has to accept a straight
     plug needs a straight face to sit on. */
  hull8(rectRing(0, 0, 1.10, 0.34, 0.11), rectRing(0, 0, 1.06, 0.34, 0.82), {
    pz: { kind: K_PANEL, seed: 823, params: [0, 0, 0, 0], ...P_SHELL, share: false },
    px: { kind: K_RIB, seed: 824, params: [3, 0.10, 0, 0], ...P_HULL },
    nx: { kind: K_RIB, seed: 824, params: [3, 0.10, 0, 0], ...P_HULL },
    nz: plain(825),
  });
  hull8(rectRing(0, 0, 1.06, 0.34, 0.82), rectRing(0, -0.06, 1.02, 0.26, 1.06), {
    pz: { kind: K_PANEL, seed: 826, params: [1, 0, 0, 0], ...P_SHELL, share: false },  // "ENERGY PACK INTERFACE"
    px: plain(827), nx: plain(827), nz: plain(825),
    py: { kind: K_PANEL, seed: 828, params: [1, 0, 0, 0], ...P_SHELL, share: false },
    topRound: 0.022,
  });

  /* six slots. Everything here is baked in the EMPTY state: dark
     receptacle, dark readout. The lit core is a separate prop and the
     readout is meant to be driven at runtime -- see the socket note at
     the top of this block. */
  const FZ = 0.34;   // the slot face, now vertical
  for (let i = 0; i < 6; i++) {
    const x = -0.85 + i * 0.34;
    // number plate above the slot
    panel([x - 0.14, 0.665, FZ + 0.002], AX, AY, AZ, 0.28, 0.115,
      { kind: K_PANEL, seed: 830 + i, params: [0, 0, 0, 0], chamfer: 0, ...P_TRIM });

    /* A lit surround behind the collar. The slot used to be a dark
       plate behind a grey collar on a grey face -- correct hardware,
       invisible from two metres in a dark hold, which is exactly where
       the player has to find it three times. A glow border reads as an
       aiming point at any distance and costs one face per slot. */
    panel([x - 0.165, 0.315, FZ + 0.001], AX, AY, AZ, 0.33, 0.35,
      { kind: K_SEAM, seed: 845, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
    rrFrame([x - 0.145, 0.335, FZ + 0.003], AX, AY, AZ, 0.29, 0.30, 0.075, 0.038, 0.034,
      { kind: K_PANEL, seed: 840, params: [1, 0, 0, 0], ...P_TRIM });

    /* a REAL bore, so the socket is a hole and not a sticker. Rings
       wound CCW about +Z and lofted backwards give inward-facing walls
       (same construction as the capsule portal). */
    const rf = ringAt([x, 0.485, FZ + 0.006], AX, AY, 0.086, 12, Math.PI / 12);
    const rb = ringAt([x, 0.485, FZ - 0.048], AX, AY, 0.086, 12, Math.PI / 12);
    hullRing(rf, rb, { kind: K_RIB, seed: 861, params: [10, 0.09, 0, 0], ...P_DARK });
    capPoly(rb, { kind: K_SCREEN, seed: 850, accent: A_ALARM });   // dark backplate, deep in the hole
    // contact ring standing proud inside the bore
    tube([x, 0.485, FZ - 0.040], [x, 0.485, FZ - 0.014], 0.046,
      { face: { kind: K_RIB, seed: 860, params: [8, 0.09, 0, 0], ...P_TRIM }, capB: true });

    // readout strip under the slot, wider than before
    panel([x - 0.155, 0.165, FZ + 0.002], AX, AY, AZ, 0.31, 0.125,
      { kind: K_SCREEN, seed: 870, accent: A_LIVE_G, chamfer: 0 });
    socket('slot' + (i + 1), [x, 0.485, FZ + 0.006], AZ);
  }

  // system status block on the right, alarm pillars on both ends
  panel([0.60, 0.755, FZ + 0.002], AX, AY, AZ, 0.34, 0.11,
    { kind: K_SCREEN, seed: 880, accent: A_AMBER, chamfer: 0, share: false });
  for (const sx of [-1, 1]) {
    octBox(sx * 1.10, 0.55, 0.06, 0.10, 0.98, 0.50, 0, {
      px: { ...plain(881), ...P_ORANGE }, nx: { ...plain(881), ...P_ORANGE },
      pz: { ...plain(881), ...P_ORANGE }, nz: plain(881),
      py: plain(881), topRound: 0.018,
    });
    panel([sx * 1.10 - 0.03, 0.72, 0.322], AX, AY, AZ, 0.06, 0.20,
      { kind: K_SEAM, seed: 882, params: [1, 0, 0, 0], accent: A_ALARM, chamfer: 0 });
    panel([sx > 0 ? 1.11 : -1.15, 0.12, 0.322], AX, AY, AZ, 0.04, 0.34,
      { kind: K_HAZARD, seed: 883, params: [0.05, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  }
  /* the guard rail is gone. Its two posts stood at x = +/-0.92, z =
     0.44 -- 70 mm from the centres of slots 1 and 6 and well inside
     their 145 mm collars, so the fence was standing in front of the two
     things the player most needs to see. A rail in front of a working
     face is the platform's job, and the platform has one. */
  // floor wash under the console
  panel([-1.00, 0.005, 0.30], AX, NZ, AY, 2.00, 0.10,
    { kind: K_SEAM, seed: 885, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
});

// ---- 38. ESCAPE CAPSULE HATCH -- 탈출 갤러리 --------------------------
propDef('Escape Capsule Hatch', 'gallery', '탈출 갤러리 · 비상 탈출 캡슐 입구 · 2.60 x 2.82 x 2.55 m', [2.60, 2.82, 2.55], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, capBar, hullRing, revRing, socket,
          K_PANEL, K_SCREEN, K_SEAM, K_HAZARD, K_PERF, K_RIB, K_PLAIN,
          A_LIVE_G, A_AMBER, A_ALARM, A_OCHRE, AX, AY, AZ, NX, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  /* octagonal portal ring. rx/ry differ, so this is a TALL octagon --
     a regular one reads as a manhole. Wound CCW seen from +Z, which is
     what hullRing needs to face the frame forward and the bore inward. */
  const oct = (cy, rx, ry, cut, z) => [
    [rx, cy - ry + cut, z], [rx, cy + ry - cut, z],
    [rx - cut, cy + ry, z], [-rx + cut, cy + ry, z],
    [-rx, cy + ry - cut, z], [-rx, cy - ry + cut, z],
    [-rx + cut, cy - ry, z], [rx - cut, cy - ry, z],
  ];
  const CY = 1.34, ZF = 0.62, ZB = -0.18;

  // capsule body behind the face, armour-chamfered on purpose (facet)
  octBox(0, 1.42, -0.62, 2.60, 2.72, 1.30, 0.26, {
    facet: true,
    px: { kind: K_PANEL, seed: 901, params: [1, 0, 0, 0], ...P_HULL },
    nx: { kind: K_PANEL, seed: 901, params: [1, 0, 0, 0], ...P_HULL },
    nz: { kind: K_PANEL, seed: 902, params: [1, 0, 0, 0], ...P_HULL },
    py: { kind: K_PERF, seed: 903, params: [0.046, 0.30, 0, 0], ...P_DARK },
    diag: plain(904),
  });
  // face block the portal is cut through
  octBox(0, 1.40, 0.30, 2.40, 2.64, 0.64, 0, {
    px: { kind: K_RIB, seed: 905, params: [4, 0.10, 0, 0], ...P_SHELL },
    nx: { kind: K_RIB, seed: 905, params: [4, 0.10, 0, 0], ...P_SHELL },
    py: plain(906), topRound: 0.024, pz: null, nz: null,
  });

  // portal: front frame face, glow run, bore, and a sill
  /* the outer ring now matches the face block exactly (2.40 x 2.64).
     It used to stop at 1.16 x 1.28 while the block ran to 1.20 x 1.32
     and had its own pz face suppressed, so a 40 mm border of the front
     face was simply never drawn -- a hole all the way round the portal.
     A near-zero corner cut keeps the ring 8-point (matching the rest of
     the chain) without degenerate quads. */
  const outer = oct(CY, 1.20, 1.32, 0.008, ZF);
  const glowO = oct(CY, 0.86, 1.06, 0.36, ZF + 0.004);
  const glowI = oct(CY, 0.79, 0.99, 0.33, ZF + 0.004);
  const inner = oct(CY, 0.72, 0.92, 0.30, ZF);
  const innerB = oct(CY, 0.72, 0.92, 0.30, ZB);
  hullRing(outer, glowO, { kind: K_PANEL, seed: 907, params: [1, 0, 0, 0], ...P_SHELL });
  hullRing(glowO, glowI, { kind: K_SEAM, seed: 908, params: [1, 0, 0, 0], accent: A_LIVE_G });
  hullRing(glowI, inner, { ...plain(909), ...P_TRIM });
  hullRing(inner, innerB, { kind: K_PANEL, seed: 910, params: [1, 0, 0, 0], ...P_HULL });  // bore, faces inward
  // inner door at the back of the bore
  // panel() needs cross(U,V) == N. This was AX x AY = +Z with N = -Z,
  // so the inner door faced away and you saw straight through it.
  panel([0.66, CY - 0.86, ZB - 0.005], NX, AY, NZ, 1.32, 1.72,
    { kind: K_PANEL, seed: 911, params: [1, 0, 0, 0], ...P_SHELL, share: false });
  panel([-0.40, CY + 0.62, ZB + 0.002], AX, AY, AZ, 0.80, 0.05,
    { kind: K_SEAM, seed: 912, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });

  // READY sign over the portal, bay number on the flank
  panel([-0.62, 2.44, 0.625], AX, AY, AZ, 1.24, 0.24,
    { kind: K_SCREEN, seed: 913, accent: A_LIVE_G, chamfer: 0, share: false });
  for (const sx of [-1, 1]) {
    panel([sx > 0 ? 1.205 : -1.245, 1.30, sx > 0 ? 0.44 : -0.44,], sx > 0 ? NZ : AZ, AY,
      sx > 0 ? AX : [-1, 0, 0], 0.52, 0.80,
      { kind: K_PANEL, seed: 914, params: [1, 0, 0, 0], chamfer: 0, ...P_SHELL, share: false });
  }
  // alarm beacons on the top corners
  for (const sx of [-1, 1]) {
    boss([sx * 0.92, 2.74, 0.30], AY, 0.070, 0.050, { seed: 915, fillet: 0.026, face: P_TRIM });
    panel([sx * 0.92 - 0.055, 2.79, 0.30 + 0.06], AX, NZ, AY, 0.11, 0.11,
      { kind: K_SEAM, seed: 916, params: [1, 0, 0, 0], accent: A_ALARM, chamfer: 0 });
  }
  // grab handles either side of the opening
  for (const sx of [-1, 1]) {
    capBar([sx * 1.00, 0.80, 0.635], [sx * 1.00, 1.90, 0.635], 0.075, 0.032, AZ, 4,
      { ...plain(917), ...P_OCHRE });
  }

  // threshold: a stepped platform with hazard edging and two rails
  octBox(0, 0.055, 0.94, 2.20, 0.11, 0.68, 0, {
    px: plain(918), nx: plain(918), nz: plain(918), ny: plain(918),
    pz: { kind: K_HAZARD, seed: 919, params: [0.07, 0, 0, 0], accent: A_OCHRE },
    py: { kind: K_PERF, seed: 920, params: [0.032, 0.30, 0, 0], ...P_HULL, share: false },
  });
  panel([-0.90, 0.115, 1.24], AX, NZ, AY, 1.80, 0.09,
    { kind: K_HAZARD, seed: 921, params: [0.06, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
  for (const sx of [-1, 1]) {
    tube([sx * 1.02, 0.11, 1.20], [sx * 1.02, 0.98, 1.20], 0.030,
      { face: { ...plain(922), ...P_TRIM }, collar: 'a' });
    tube([sx * 1.02, 0.11, 0.68], [sx * 1.02, 0.98, 0.68], 0.030,
      { face: { ...plain(922), ...P_TRIM }, collar: 'a' });
    // end the top rail ON the post centre line, not 20 mm past it: its
    // cap face was landing exactly on one of the post's own facets
    tube([sx * 1.02, 0.98, 0.68], [sx * 1.02, 0.98, 1.20], 0.030,
      { face: { ...plain(922), ...P_TRIM } });
  }
  // umbilicals into the capsule
  for (const sx of [-1, 1]) {
    tube([sx * 1.22, 0.40, -0.30], [sx * 1.22, 0.40, -1.10], 0.048,
      { face: { ...plain(923), ...P_OXIDE }, collar: 'a' });
  }
  socket('board', [0, 0.12, 1.20], NZ);      // where the player steps in
  socket('interior', [0, CY, ZB], NZ);
});

// ---- 39. ESCAPE BAY PLATFORM -- 탈출 갤러리 ---------------------------
// The deck the last capsule and its interface stand on. Grating panels
// over a structural tray, a stepped nosing with hazard edging and
// recessed step lights, and a rail either side of the approach.
// Carries sockets so the level can seat the capsule and the console on
// it without measuring anything by hand.
propDef('Escape Bay Platform', 'gallery', '탈출 갤러리 · 비상 탈출 플랫폼 · 5.60 x 1.37 x 4.17 m', [5.60, 1.37, 4.17], (c) => {
  const { octBox, hull8, rectRing, panel, plainBox, tube, boss, capBar, socket,
          K_PERF, K_SEAM, K_HAZARD, K_RIB, K_PANEL, K_PLAIN,
          A_AMBER, A_OCHRE, A_LIVE_G, AX, AY, AZ, NZ } = c;
  const plain = s => ({ kind: K_PLAIN, seed: s });

  // structural tray, raised 300 mm off the hold floor
  octBox(0, 0.15, -0.30, 5.60, 0.30, 2.40, 0, {
    px: { kind: K_RIB, seed: 941, params: [3, 0.10, 0, 0], ...P_HULL },
    nx: { kind: K_RIB, seed: 941, params: [3, 0.10, 0, 0], ...P_HULL },
    nz: { kind: K_RIB, seed: 942, params: [22, 0.10, 0, 0], ...P_HULL },
    ny: plain(943),
    // the hazard band IS the tray's front face. It used to be a separate
    // panel placed at z = 0.90, exactly coplanar with this face, which
    // is a z-fight by construction.
    pz: { kind: K_HAZARD, seed: 944, params: [0.075, 0, 0, 0], accent: A_OCHRE, share: false },
  });
  // deck: eight grating panels with capsule-section dividers between
  // them, which is how the reference deck is actually laid
  for (let ix = 0; ix < 4; ix++) for (let iz = 0; iz < 2; iz++) {
    const x0 = -2.72 + ix * 1.36, z0 = -1.46 + iz * 1.16;
    panel([x0, 0.305, z0 + 1.12], AX, NZ, AY, 1.28, 1.08,
      { kind: K_PERF, seed: 945, params: [0.036, 0.30, 0, 0], chamfer: 0, ...P_HULL });
  }
  // deck framing: five longitudinal members run through, and the cross
  // member is split into the four bays between them. Running both
  // continuous put two capsule bars on the same top plane at every
  // crossing -- sixteen coplanar overlapping faces.
  for (let ix = 0; ix <= 4; ix++) {
    capBar([-2.76 + ix * 1.36, 0.308, -1.48], [-2.76 + ix * 1.36, 0.308, 0.86], 0.065, 0.022,
      AY, 4, { ...plain(946), ...P_TRIM });
  }
  for (let ix = 0; ix < 4; ix++) {
    // inset so each capsule END clears the longitudinal member it runs
    // up to -- a capBar extends half its width past its endpoint
    capBar([-2.66 + ix * 1.36, 0.308, -0.30], [-1.50 + ix * 1.36, 0.308, -0.30], 0.065, 0.022,
      AY, 4, { ...plain(946), ...P_TRIM });
  }

  /* front nosing. The steps used to start at z = 1.94 while the deck's
     front face is at z = 0.90 -- a 1.04 m gap, so the flight was
     floating a metre clear of the platform it was supposed to serve.
     They now butt against the tray and descend in equal 100 mm risers:
     deck 0.30 -> 0.20 -> 0.10 -> floor. */
  const STEP = [[0.20, 1.10, 0.90], [0.10, 1.26, 1.26]];
  STEP.forEach(([y, hw, zBack], i) => {
    octBox(0, y / 2, zBack + 0.18, hw * 2, y, 0.36, 0, {
      px: plain(947), nx: plain(947), nz: null, ny: plain(947),
      pz: { kind: K_HAZARD, seed: 948, params: [0.07, 0, 0, 0], accent: A_OCHRE },
      py: { kind: K_PERF, seed: 949, params: [0.032, 0.30, 0, 0], ...P_HULL, share: false },
    });
    panel([-hw + 0.10, y - 0.035, zBack + 0.362], AX, AY, AZ, (hw - 0.10) * 2, 0.030,
      { kind: K_SEAM, seed: 950 + i, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });
  });

  /* rails. These used to run down x = +/-1.30 the full depth of the
     deck -- straight through the capsule (x -0.15..2.45) and through
     the interface (x -2.75..-0.45). A rail belongs on the EDGE the
     player can fall off, which is the front, in two runs flanking the
     stair opening. */
  for (const [x0, x1] of [[-2.68, -1.24], [1.24, 2.68]]) {
    for (const x of [x0, (x0 + x1) / 2, x1]) {
      // 34 mm, not 32: the posts and the rails they carry are both
      // centred on z = 0.82, so an equal radius put a post facet and a
      // rail facet on the same plane at every crossing
      tube([x, 0.30, 0.82], [x, 1.34, 0.82], 0.034,
        { face: { ...plain(953), ...P_TRIM }, collar: 'a' });
      panel([x - 0.035, 0.34, 0.855], AX, AY, AZ, 0.07, 0.26,
        { kind: K_HAZARD, seed: 954, params: [0.05, 0, 0, 0], accent: A_OCHRE, chamfer: 0 });
    }
    tube([x0 - 0.05, 1.32, 0.82], [x1 + 0.05, 1.32, 0.82], 0.032,
      { face: { ...plain(953), ...P_TRIM }, capA: true, capB: true });
    tube([x0 - 0.05, 0.74, 0.82], [x1 + 0.05, 0.74, 0.82], 0.026,
      { face: { ...plain(953), ...P_TRIM }, capA: true, capB: true });
  }
  // corner bollards with status lamps, and a marshalling strip down the
  // centre of the approach
  for (const sx of [-1, 1]) {
    // moved back off z = 0.72: the front rail run now ends at x = 2.68,
    // z = 0.82, which the bollard used to sit inside
    octBox(sx * 2.62, 0.44, 0.10, 0.22, 0.58, 0.22, 0, {
      px: { ...plain(955), ...P_ORANGE }, nx: { ...plain(955), ...P_ORANGE },
      pz: { ...plain(955), ...P_ORANGE }, nz: { ...plain(955), ...P_ORANGE },
      py: plain(955), topRound: 0.020,
    });
    panel([sx * 2.62 - 0.06, 0.66, 0.212], AX, AY, AZ, 0.12, 0.05,
      { kind: K_SEAM, seed: 956, params: [1, 0, 0, 0], accent: A_LIVE_G, chamfer: 0 });
    boss([sx * 2.62, 0.74, 0.10], AY, 0.055, 0.020, { seed: 957, fillet: 0.020, face: P_TRIM });
  }
  panel([-0.16, 0.312, 0.84], AX, NZ, AY, 0.32, 1.80,
    { kind: K_SEAM, seed: 958, params: [1, 0, 0, 0], accent: A_AMBER, chamfer: 0 });

  // where the other two props stand. The capsule's own threshold sits
  // on the deck, so it seats at deck height; the interface stands to
  // its left with clearance for a person between them.
  // measured against both props' own footprints: capsule -0.15..2.45,
  // interface -2.76..-0.45, both inside the deck's +/-2.80
  socket('capsule', [1.15, 0.30, -0.60], AZ);
  socket('interface', [-1.60, 0.30, -0.10], AZ);
  socket('approach', [0, 0.00, 2.90], NZ);
});
