/* A box and a drum, built on the page in the same four steps as LUMI:
     0  a line      -- a rectangle (box) or a side profile (drum), from three numbers
     1  turned / pushed up -- the drum's line swept round, the box's rectangle extruded
     2  parts       -- straps and handles, rims and a cap
     3  materials
   The sliders change the numbers and the shape follows.  Canvas 2D only: a few
   hundred triangles, sorted back to front, so it works where WebGL does not.
   The code beside it is a simplified illustration, not the game's own. */
(function () {
  var root = document.querySelector('.sb');
  if (!root) return;
  var cv = root.querySelector('canvas.gl'), ov = root.querySelector('canvas.ov'), ctx = ov.getContext('2d');
  var bg = root.querySelector('canvas.bg'), bctx = bg.getContext('2d');
  /* A DEPTH BUFFER, NOT A SORT.  Sorting triangles by their mean depth draws
     a strap behind the face it stands on whenever the face is the larger of
     the two -- which is every part on this box -- so the 2D path is only the
     fallback where there is no WebGL. */
  var gl = cv.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: true });
  var deriv = gl && gl.getExtension('OES_standard_derivatives');
  if (!deriv) gl = null;
  var reduce = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* page language: the builder hands its translations over as window.LT */
  var LT = window.LT || {};
  function tr(s) { return LT[s] || s; }

  var OBJ = {
    box: {
      knobs: [['w', tr('가로'), 0.6, 1.8, 1.2], ['h', tr('높이'), 0.3, 1.4, 0.8], ['d', tr('깊이'), 0.4, 1.4, 0.8]],
      code: function (v) {
        var B = tr('상자'), W = tr('가로'), H = tr('높이'), D = tr('깊이');
        return ['const ' + B + ' = { ' + W + ': ' + f(v.w) + ', ' + H + ': ' + f(v.h) + ', ' + D + ': ' + f(v.d) + ' };',
                'rect(' + B + '.' + W + ', ' + B + '.' + D + ');          // ' + tr('네모 하나'),
                'extrude(' + B + '.' + H + ');                 // ' + tr('위로 밀어 올리기'),
                'add(strap() × 4, handle() × 2);     // ' + tr('부품 붙이기'),
                'material("painted_steel");           // ' + tr('재질 입히기')];
      }
    },
    drum: {
      knobs: [['r', tr('반지름'), 0.18, 0.45, 0.30], ['h', tr('높이'), 0.4, 1.2, 0.9], ['seg', tr('나누는 수'), 3, 48, 24]],
      code: function (v) {
        var B = tr('드럼통'), R = tr('반지름'), H = tr('높이'), N = tr('나누는수'), S = tr('옆선'), L = tr('선');
        return ['const ' + B + ' = { ' + R + ': ' + f(v.r) + ', ' + H + ': ' + f(v.h) + ', ' + N + ': ' + v.seg + ' };',
                'line(' + S + '(' + B + '.' + R + ', ' + B + '.' + H + '));   // ' + tr('선 하나'),
                'lathe(' + L + ', ' + B + '.' + N + ');             // ' + tr('한 바퀴 돌리기'),
                'add(rim() × 2, cap());               // ' + tr('부품 붙이기'),
                'material("paint", "steel");          // ' + tr('재질 입히기')];
      }
    }
  };
  var STEP_NAME = { box: [tr('네모 하나'), tr('위로 밀어 올리기'), tr('부품 붙이기'), tr('재질 입히기')],
                    drum: [tr('선 하나'), tr('한 바퀴 돌리기'), tr('부품 붙이기'), tr('재질 입히기')] };
  function f(x) { return (Math.round(x * 100) / 100).toFixed(2); }

  var kind = 'box', step = 0, t = 1, val = {}, auto = true, yaw = 0.7, drag = null;
  Object.keys(OBJ).forEach(function (k) { val[k] = {}; OBJ[k].knobs.forEach(function (q) { val[k][q[0]] = q[4]; }); });

  /* ------------------------------------------------------------ geometry */
  var CLAY = [0.62, 0.62, 0.60];
  var COL = { paint: [0.46, 0.50, 0.54], steel: [0.62, 0.64, 0.66], ochre: [0.80, 0.56, 0.18],
              amber: [0.95, 0.60, 0.20], dark: [0.12, 0.13, 0.14] };
  // `s` names the corner opposite the quad's diagonal, so the wireframe can
  // leave the diagonal out and draw the quad's own four edges
  function quad(T, a, b, c, d, col) { T.push({ p: [a, b, c], c: col, s: 1 }); T.push({ p: [a, c, d], c: col, s: 2 }); }
  function box(T, x0, y0, z0, x1, y1, z1, col) {
    var P = function (x, y, z) { return [x ? x1 : x0, y ? y1 : y0, z ? z1 : z0]; };
    quad(T, P(0,0,1), P(1,0,1), P(1,1,1), P(0,1,1), col);   // +z
    quad(T, P(1,0,0), P(0,0,0), P(0,1,0), P(1,1,0), col);   // -z
    quad(T, P(1,0,1), P(1,0,0), P(1,1,0), P(1,1,1), col);   // +x
    quad(T, P(0,0,0), P(0,0,1), P(0,1,1), P(0,1,0), col);   // -x
    quad(T, P(0,1,1), P(1,1,1), P(1,1,0), P(0,1,0), col);   // +y
    quad(T, P(0,0,0), P(1,0,0), P(1,0,1), P(0,0,1), col);   // -y
  }
  function lathe(T, prof, seg, sweep, col, ox, oz) {
    ox = ox || 0; oz = oz || 0;
    var n = Math.max(1, Math.round(seg * sweep));
    for (var i = 0; i < n; i++) {
      var a0 = i / seg * Math.PI * 2, a1 = (i + 1) / seg * Math.PI * 2;
      for (var j = 0; j < prof.length - 1; j++) {
        var p = prof[j], q = prof[j + 1];
        var A = [ox + p[0] * Math.cos(a0), p[1], oz + p[0] * Math.sin(a0)], B = [ox + p[0] * Math.cos(a1), p[1], oz + p[0] * Math.sin(a1)];
        var C = [ox + q[0] * Math.cos(a1), q[1], oz + q[0] * Math.sin(a1)], D = [ox + q[0] * Math.cos(a0), q[1], oz + q[0] * Math.sin(a0)];
        quad(T, A, D, C, B, col);
      }
    }
  }
  function drumProfile(r, h) {
    var b = 0.02;
    return [[0, 0], [r, 0], [r, h * 0.3 - b], [r + b, h * 0.3], [r, h * 0.3 + b],
            [r, h * 0.7 - b], [r + b, h * 0.7], [r, h * 0.7 + b], [r, h], [0, h]];
  }
  function ease(x) { x = Math.min(Math.max(x, 0), 1); return x * x * (3 - 2 * x); }

  // returns { tris, lines, size }
  function build() {
    var v = val[kind], T = [], L = [], mat = step >= 3, k = step === 3 ? ease(t) : 1;
    function mix(c) { return mat ? CLAY.map(function (x, i) { return x + (c[i] - x) * k; }) : CLAY; }
    if (kind === 'box') {
      var w = v.w / 2, d = v.d / 2, h = v.h;
      if (step === 0) {
        var e = ease(t), pts = [[-w, 0, -d], [w, 0, -d], [w, 0, d], [-w, 0, d], [-w, 0, -d]];
        L.push(pts.slice(0, 1 + Math.ceil(e * 4)));
      } else {
        box(T, -w, 0, -d, w, step === 1 ? h * ease(t) : h, d, mix(COL.paint));
        if (step >= 2) {
          var n = step === 2 ? Math.ceil(ease(t) * 6) : 6, s = 0.05;
          var parts = [[-w, -d], [w, -d], [w, d], [-w, d]];
          for (var i = 0; i < Math.min(n, 4); i++) {
            var c = parts[i];
            box(T, c[0] - s, -0.005, c[1] - s, c[0] + s, h + 0.01, c[1] + s, mix(COL.steel));
          }
          if (n > 4) box(T, -0.16, h * 0.55, d, 0.16, h * 0.68, d + 0.05, mix(COL.dark));
          if (n > 5) box(T, -0.16, h * 0.55, -d - 0.05, 0.16, h * 0.68, -d, mix(COL.dark));
          if (step >= 3) box(T, -w - 0.006, h * 0.15, -d - 0.006, w + 0.006, h * 0.25, d + 0.006, mix(COL.ochre));
        }
      }
      return { tris: T, lines: L, size: Math.max(v.w, v.h, v.d) };
    }
    var r = v.r, hh = v.h, prof = drumProfile(r, hh);
    if (step === 0) {
      var m = Math.max(2, Math.ceil(ease(t) * prof.length));
      L.push(prof.slice(0, m).map(function (p) { return [p[0], p[1], 0]; }));
    } else {
      lathe(T, prof, v.seg, step === 1 ? ease(t) : 1, mix(COL.amber));
      if (step === 1) L.push(prof.map(function (p) { var a = ease(t) * Math.PI * 2; return [p[0] * Math.cos(a), p[1], p[0] * Math.sin(a)]; }));
      if (step >= 2) {
        var q = step === 2 ? Math.ceil(ease(t) * 3) : 3, rr = 0.022;
        var rim = function (y) { return [[r - 0.004, y - rr], [r + rr, y - rr], [r + rr, y + rr], [r - 0.004, y + rr]]; };
        if (q > 0) lathe(T, rim(hh), v.seg, 1, mix(COL.steel));
        if (q > 1) lathe(T, rim(0.012), v.seg, 1, mix(COL.steel));
        if (q > 2) lathe(T, [[0, hh], [0.05, hh], [0.05, hh + 0.04], [0, hh + 0.04]], 12, 1, mix(COL.dark), r * 0.5, 0);
      }
    }
    return { tris: T, lines: L, size: Math.max(r * 2.2, hh) };
  }

  /* ------------------------------------------------------------ render */
  var W = 0, H = 0, dpr = 1;
  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    var b = ov.getBoundingClientRect(); W = b.width; H = b.height;
    cv.width = ov.width = bg.width = Math.round(W * dpr); cv.height = ov.height = bg.height = Math.round(H * dpr);
  }

  /* the WebGL path: flat shading per triangle, and a faint wire along each
     quad's edges (barycentric, with the diagonal's channel lifted out of reach) */
  var prog = null, A = {}, U = {}, vbo = null;
  if (gl) {
    var sh = function (type, src) {
      var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    var VS = [
      'attribute vec3 aP; attribute vec3 aN; attribute vec3 aC; attribute vec3 aB;',
      'uniform vec4 uRot; uniform vec4 uCam;',     // cos yaw, sin yaw, cos pitch, sin pitch | ymid, dist, kx, ky
      'varying vec3 vC; varying vec3 vB; varying float vL;',
      'vec3 view(vec3 p){ float x=p.x*uRot.x-p.z*uRot.y, z=p.x*uRot.y+p.z*uRot.x;',
      '  return vec3(x, p.y*uRot.z+z*uRot.w, -p.y*uRot.w+z*uRot.z); }',
      'void main(){',
      '  vec3 q=view(aP-vec3(0.0,uCam.x,0.0)); q.z+=uCam.y;',
      '  vec3 n=normalize(view(aN)); vec3 L=normalize(vec3(-0.45,0.75,-0.48));',
      '  vL=0.45+0.75*abs(dot(n,L)); vC=aC; vB=aB;',
      '  float nr=0.05, fr=40.0;',
      '  gl_Position=vec4(q.x*uCam.z, q.y*uCam.w, q.z*(fr+nr)/(fr-nr)-2.0*fr*nr/(fr-nr), q.z);',
      '}'].join('\n');
    var FS = [
      '#extension GL_OES_standard_derivatives : enable',
      'precision mediump float;',
      'varying vec3 vC; varying vec3 vB; varying float vL;',
      'void main(){',
      '  vec3 c=min(vC*vL, vec3(1.0));',
      '  vec3 e=vB/max(fwidth(vB),vec3(1e-4)); float m=min(min(e.x,e.y),e.z);',
      '  float w=1.0-smoothstep(0.4,1.4,m);',
      '  gl_FragColor=vec4(mix(c, vec3(1.0), w*0.28), 1.0);',
      '}'].join('\n');
    prog = gl.createProgram();
    gl.attachShader(prog, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(prog);
    ['aP', 'aN', 'aC', 'aB'].forEach(function (n) { A[n] = gl.getAttribLocation(prog, n); });
    ['uRot', 'uCam'].forEach(function (n) { U[n] = gl.getUniformLocation(prog, n); });
    vbo = gl.createBuffer();
  }
  function drawGL(g, cyw, syw, cp, sp, ymid, dist, foc) {
    var n = g.tris.length, data = new Float32Array(n * 3 * 12), o = 0;
    g.tris.forEach(function (tr) {
      var a = tr.p[0], b = tr.p[1], c = tr.p[2];
      var u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
      var nn = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
      var ln = Math.hypot(nn[0], nn[1], nn[2]) || 1;
      for (var k = 0; k < 3; k++) {
        var p = tr.p[k], bc = [k === 0 ? 1 : 0, k === 1 ? 1 : 0, k === 2 ? 1 : 0];
        bc[tr.s] += 1;                               // the diagonal never reaches zero
        data.set([p[0], p[1], p[2], nn[0] / ln, nn[1] / ln, nn[2] / ln, tr.c[0], tr.c[1], tr.c[2], bc[0], bc[1], bc[2]], o);
        o += 12;
      }
    });
    gl.viewport(0, 0, cv.width, cv.height);
    gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    gl.useProgram(prog);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo); gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
    [['aP', 0], ['aN', 3], ['aC', 6], ['aB', 9]].forEach(function (q) {
      gl.enableVertexAttribArray(A[q[0]]); gl.vertexAttribPointer(A[q[0]], 3, gl.FLOAT, false, 48, q[1] * 4);
    });
    gl.uniform4f(U.uRot, cyw, syw, cp, sp);
    gl.uniform4f(U.uCam, ymid, dist, foc / (W / 2), foc / (H / 2));
    if (n) gl.drawArrays(gl.TRIANGLES, 0, n * 3);
  }
  function draw() {
    if (!W) resize();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, W, H);
    bctx.setTransform(dpr, 0, 0, dpr, 0, 0); bctx.clearRect(0, 0, W, H);
    var g = build(), cy = Math.cos(yaw), sy = Math.sin(yaw), pitch = 0.42, cp = Math.cos(pitch), sp = Math.sin(pitch);
    var ymid = (kind === 'box' ? val.box.h : val.drum.h) / 2, dist = g.size * 2.0 + 0.5, foc = Math.min(W, H) * 1.05;
    function xf(p) {
      var x = p[0] * cy - p[2] * sy, z = p[0] * sy + p[2] * cy, y = p[1] - ymid;
      var y2 = y * cp + z * sp, z2 = -y * sp + z * cp;   // looking down at it
      return [x, y2, z2 + dist];
    }
    function pr(q) { return [W / 2 + q[0] / q[2] * foc, H / 2 - q[1] / q[2] * foc]; }
    // the floor
    bctx.strokeStyle = 'rgba(255,255,255,.06)'; bctx.lineWidth = 1;
    for (var i = -4; i <= 4; i++) {
      var a = pr(xf([i * 0.25, 0, -1])), b = pr(xf([i * 0.25, 0, 1]));
      var c = pr(xf([-1, 0, i * 0.25])), d = pr(xf([1, 0, i * 0.25]));
      bctx.beginPath(); bctx.moveTo(a[0], a[1]); bctx.lineTo(b[0], b[1]); bctx.moveTo(c[0], c[1]); bctx.lineTo(d[0], d[1]); bctx.stroke();
    }
    var Ld = [0.45, 0.75, -0.48], list = [];
    if (gl) drawGL(g, cy, sy, cp, sp, ymid, dist, foc);
    else g.tris.forEach(function (tr) {
      var A = xf(tr.p[0]), B = xf(tr.p[1]), C = xf(tr.p[2]);
      var u = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], v = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
      var n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
      var ln = Math.hypot(n[0], n[1], n[2]); if (!ln) return;
      n = [n[0] / ln, n[1] / ln, n[2] / ln];
      if (n[0] * A[0] + n[1] * A[1] + n[2] * A[2] >= 0) return;          // facing away
      var l = Math.max(0, -(n[0] * Ld[0] + n[1] * Ld[1] + n[2] * Ld[2]));
      var s = 0.45 + 0.75 * l;
      list.push({ z: (A[2] + B[2] + C[2]) / 3, a: pr(A), b: pr(B), c: pr(C),
                  col: 'rgb(' + tr.c.map(function (x) { return Math.round(Math.min(1, x * s) * 255); }).join(',') + ')' });
    });
    list.sort(function (p, q) { return q.z - p.z; });
    list.forEach(function (p) {
      ctx.fillStyle = p.col; ctx.strokeStyle = p.col; ctx.lineWidth = 0.6;
      ctx.beginPath(); ctx.moveTo(p.a[0], p.a[1]); ctx.lineTo(p.b[0], p.b[1]); ctx.lineTo(p.c[0], p.c[1]); ctx.closePath();
      ctx.fill(); ctx.stroke();
    });
    ctx.strokeStyle = '#f0a03a'; ctx.lineWidth = 2.2; ctx.lineJoin = 'round';
    g.lines.forEach(function (ln) {
      ctx.beginPath();
      ln.forEach(function (p, i) { var q = pr(xf(p)); if (i) ctx.lineTo(q[0], q[1]); else ctx.moveTo(q[0], q[1]); });
      ctx.stroke();
      ln.forEach(function (p) { var q = pr(xf(p)); ctx.fillStyle = '#f0a03a'; ctx.beginPath(); ctx.arc(q[0], q[1], 3, 0, 7); ctx.fill(); });
    });
    count.textContent = tr('삼각형 {0}개').replace('{0}', g.tris.length);
  }

  /* ------------------------------------------------------------ UI */
  var tabs = [].slice.call(root.querySelectorAll('.sb-tabs button'));
  var steps = [].slice.call(root.querySelectorAll('.sb-steps button'));
  var knobs = root.querySelector('.sb-knobs'), code = root.querySelector('.sb-code code'), count = root.querySelector('.sb-count');

  function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
  function showCode() {
    var lines = OBJ[kind].code(val[kind]);
    code.innerHTML = lines.map(function (l, i) {
      return '<span class="' + (i === 0 ? 'nums' : (i - 1 === step ? 'on' : (i - 1 < step ? 'done' : ''))) + '">' + esc(l) + '</span>';
    }).join('\n');
  }
  function buildKnobs() {
    knobs.innerHTML = '';
    OBJ[kind].knobs.forEach(function (q) {
      var int = q[0] === 'seg', id = 'sb-' + kind + '-' + q[0];
      var row = document.createElement('label'); row.className = 'sb-knob'; row.htmlFor = id;
      row.innerHTML = '<span>' + q[1] + '</span><input type="range" id="' + id + '" min="' + q[2] + '" max="' + q[3] +
        '" step="' + (int ? 1 : 0.01) + '" value="' + val[kind][q[0]] + '"><b>' + (int ? val[kind][q[0]] : f(val[kind][q[0]]) + 'm') + '</b>';
      var inp = row.querySelector('input'), out = row.querySelector('b');
      inp.addEventListener('input', function () {
        var x = parseFloat(inp.value); val[kind][q[0]] = x; out.textContent = int ? x : f(x) + 'm';
        if (step < 1) { step = 3; t = 1; markSteps(); }   // a number moved: show the finished thing
        auto = false; showCode(); draw();
      });
      knobs.appendChild(row);
    });
  }
  function markSteps() {
    steps.forEach(function (b, i) {
      b.setAttribute('aria-pressed', i === step ? 'true' : 'false'); b.classList.toggle('done', i < step);
      b.querySelector('.t').textContent = STEP_NAME[kind][i];
    });
    showCode();
  }
  function go(s, animate) { step = s; t = animate && !reduce ? 0 : 1; markSteps(); }
  tabs.forEach(function (b) {
    b.addEventListener('click', function () {
      kind = b.dataset.k; tabs.forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
      buildKnobs(); auto = true; go(0, true); kick();
    });
  });
  steps.forEach(function (b, i) { b.addEventListener('click', function () { auto = false; go(i, true); kick(); }); });
  cv.addEventListener('pointerdown', function (e) { drag = { x: e.clientX, y: yaw }; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove', function (e) { if (drag) { yaw = drag.y + (e.clientX - drag.x) * 0.012; draw(); } });
  ['pointerup', 'pointercancel'].forEach(function (n) { cv.addEventListener(n, function () { drag = null; }); });
  window.addEventListener('resize', function () { resize(); draw(); });

  var running = false, last = 0, hold = 0, seen = false;
  function tick(now) {
    var dt = last ? Math.min((now - last) / 1000, 0.05) : 0; last = now;
    if (t < 1) t = Math.min(1, t + dt / 0.9);
    else if (auto && step < 3) { hold += dt; if (hold > 0.7) { hold = 0; go(step + 1, true); } }
    if (!drag && !reduce) yaw += dt * 0.25;
    draw();
    if (seen && (t < 1 || (auto && step < 3) || !reduce)) requestAnimationFrame(tick); else running = false;
  }
  function kick() { if (!running && seen) { running = true; last = 0; requestAnimationFrame(tick); } else if (!seen) draw(); }
  new IntersectionObserver(function (es) {
    es.forEach(function (e) { seen = e.isIntersecting; if (seen) kick(); });
  }).observe(root);
  root.classList.add('live');
  buildKnobs(); go(reduce ? 3 : 0, !reduce); resize(); draw();
})();
