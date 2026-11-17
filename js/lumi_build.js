/* LUMI, built on the page by her own generator (props/lumi_v02/lumi_geometry.js).

   Four steps, the same four the caption names:
     0  a line      -- the body's side profile, the numbers in PARAMS
     1  turned      -- the seven body zones swept round, 64 steps (P.SEG)
     2  parts       -- the other 28 parts appear one at a time
     3  materials   -- clay gives way to paint, steel, the screen and her lamp
   No library: one small WebGL program, and a 2D overlay for the profile line. */
(function () {
  var root = document.querySelector('.proc');
  if (!root) return;
  var cv = root.querySelector('canvas.gl'), ov = root.querySelector('canvas.ov');
  var gl = cv.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: true });
  if (!gl || !window.LumiGeometry) { root.classList.add('nogl'); return; }
  root.classList.add('live');

  var LG = window.LumiGeometry, P = LG.PARAMS, parts = LG.buildLumi();
  var reduce = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------------------------------------------------------------- materials
     colours are the bench's own (lumi_v6.html MAT / matPanel / matCy) */
  var M = {
    paint:   [0.871, 0.855, 0.808, 0.22, 0.04, 0],
    band:    [0.141, 0.157, 0.173, 0.72, 0.28, 0],
    steel:   [0.576, 0.604, 0.627, 0.26, 0.96, 0],
    dark:    [0.051, 0.063, 0.075, 0.58, 0.40, 0],
    panel:   [0.082, 0.098, 0.114, 0.30, 0.62, 0],
    screen:  [0.020, 0.030, 0.035, 0.12, 0.00, 1]
  };
  M.decal_front = M.decal_back = M.decal_greeble = M.paint;
  var CY = [0.384, 0.847, 1.0], CYD = [0.184, 0.498, 0.620];

  function shader(type, src) {
    var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    return s;
  }
  var VS = [
    'attribute vec3 aP; attribute vec3 aN; attribute vec2 aU; attribute float aC;',
    'uniform mat4 uVP; uniform mat3 uR;',
    'varying vec3 vN; varying vec3 vW; varying vec2 vU; varying float vC;',
    'void main(){ vec3 p=uR*aP; vW=p; vN=uR*aN; vU=aU; vC=aC; gl_Position=uVP*vec4(p,1.0); }'
  ].join('\n');
  var FS = [
    'precision highp float;',
    'varying vec3 vN; varying vec3 vW; varying vec2 vU; varying float vC;',
    'uniform vec3 uEye, uAlb, uEmit; uniform float uRough, uMetal, uClay, uFlash, uSweep, uKind, uAlpha;',
    'const float PI=3.14159265;',
    'float sdBox(vec2 p, vec2 b, float r){ vec2 q=abs(p)-b+r; return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r; }',
    'vec3 lit(vec3 N, vec3 V, vec3 L, vec3 C, vec3 dif, vec3 F0, float r){',
    '  float nl=max(dot(N,L),0.0); if(nl<=0.0) return vec3(0.0);',
    '  vec3 H=normalize(L+V); float nh=max(dot(N,H),0.0), a=max(r*r,0.02), a2=a*a;',
    '  float d=nh*nh*(a2-1.0)+1.0; float D=a2/(PI*d*d);',
    '  vec3 F=F0+(1.0-F0)*pow(clamp(1.0-dot(V,H),0.0,1.0),5.0);',
    '  return C*nl*(dif/PI*3.0 + D*F*0.25);',
    '}',
    'void main(){',
    '  if(vU.x>uSweep) discard;',
    '  if(uKind>1.5){ float a=uAlpha*(1.0-uClay)*(uKind>2.5 ? pow(max(1.0-length(vU-0.5)*2.0,0.0),2.0) : vC);',
    '    gl_FragColor=vec4(uEmit*a,a); return; }',
    '  vec3 N=normalize(vN); if(!gl_FrontFacing) N=-N;',
    '  vec3 V=normalize(uEye-vW);',
    '  vec3 alb=mix(uAlb, vec3(0.42,0.43,0.45), uClay);',
    '  float rough=mix(uRough,0.6,uClay), metal=mix(uMetal,0.0,uClay);',
    '  vec3 emit=uEmit*(1.0-uClay);',
    '  if(uKind>0.5){',
    '    vec2 p=vU-0.5; p.x*=1.8;',
    '    float e=min(sdBox(p-vec2(-0.25,0.0),vec2(0.105,0.105),0.045), sdBox(p-vec2(0.25,0.0),vec2(0.105,0.105),0.045));',
    '    float eye=smoothstep(0.012,-0.012,e), halo=exp(-max(e,0.0)*22.0)*0.30;',
    '    emit+=vec3(0.384,0.847,1.0)*(eye*1.6+halo)*(1.0-uClay);',
    '  }',
    '  vec3 F0=mix(vec3(0.04),alb,metal), dif=alb*(1.0-metal);',
    '  vec3 col=dif*mix(vec3(0.035,0.037,0.045),vec3(0.20,0.21,0.23),N.y*0.5+0.5);',
    '  col+=lit(N,V,normalize(vec3(-0.55,0.70,0.65)),vec3(1.45,1.38,1.26),dif,F0,rough);',
    '  col+=lit(N,V,normalize(vec3(0.85,0.10,0.35)),vec3(0.36,0.46,0.58),dif,F0,rough);',
    '  col+=lit(N,V,normalize(vec3(0.15,0.35,-1.0)),vec3(0.70,0.86,1.0),dif,F0,rough);',
    '  vec3 R=reflect(-V,N); float ndv=max(dot(N,V),0.0);',
    '  vec3 env=mix(vec3(0.015,0.016,0.02),vec3(0.42,0.44,0.48),smoothstep(-0.25,0.7,R.y))',
    '          +vec3(1.4)*smoothstep(0.90,0.985,dot(R,normalize(vec3(-0.55,0.70,0.65))))*(1.0-rough);',
    '  vec3 Fr=F0+(1.0-F0)*pow(1.0-ndv,5.0);',
    '  col+=env*Fr*mix(1.0,0.18,rough);',
    '  col+=emit+vec3(0.384,0.847,1.0)*uFlash*0.55;',
    '  col=(col*(2.51*col+0.03))/(col*(2.43*col+0.59)+0.14);',
    '  gl_FragColor=vec4(pow(clamp(col,0.0,1.0),vec3(1.0/2.2)),1.0);',
    '}'
  ].join('\n');
  var prog = gl.createProgram();
  gl.attachShader(prog, shader(gl.VERTEX_SHADER, VS));
  gl.attachShader(prog, shader(gl.FRAGMENT_SHADER, FS));
  gl.linkProgram(prog);
  gl.useProgram(prog);
  var A = {}, U = {};
  ['aP', 'aN', 'aU', 'aC'].forEach(function (n) { A[n] = gl.getAttribLocation(prog, n); });
  ['uVP', 'uR', 'uEye', 'uAlb', 'uEmit', 'uRough', 'uMetal', 'uClay', 'uFlash', 'uSweep', 'uKind', 'uAlpha']
    .forEach(function (n) { U[n] = gl.getUniformLocation(prog, n); });

  function buf(data, target) {
    var b = gl.createBuffer(); gl.bindBuffer(target || gl.ARRAY_BUFFER, b);
    gl.bufferData(target || gl.ARRAY_BUFFER, data, gl.STATIC_DRAW); return b;
  }
  var nTri = 0;
  var G = parts.map(function (p, i) {
    var n = p.positions.length / 3;
    var c = p.colors || new Float32Array(n).fill(1);
    if (p.colors) { c = new Float32Array(n); for (var k = 0; k < n; k++) c[k] = p.colors[k * 3]; }
    nTri += p.indices.length / 3;
    var mat = p.material, glow = mat === 'glow_beam' ? 2 : mat === 'glow_radial' ? 3 : 0;
    var m = M[mat] || M.dark;
    var emit = mat === 'emissive_cyan' ? CY : mat === 'emissive_dim' ? CYD : glow ? CY : [0, 0, 0];
    if (glow || mat.indexOf('emissive') === 0) m = [0.03, 0.05, 0.06, 0.4, 0, 0];
    return {
      name: p.name, i: i, lathe: i < 7, glow: glow, kind: glow ? glow : (mat === 'screen' ? 1 : 0),
      alb: m.slice(0, 3), rough: m[3], metal: m[4], emit: emit, alpha: p.opacity || 1,
      tris: p.indices.length / 3, count: p.indices.length,
      pos: buf(p.positions), nrm: buf(p.normals), uv: buf(p.uvs), col: buf(c),
      idx: buf(p.indices, gl.ELEMENT_ARRAY_BUFFER)
    };
  });
  var LATHE_N = 7;

  /* ---------------------------------------------------------------- matrices */
  function persp(fov, asp, n, f) {
    var t = 1 / Math.tan(fov / 2);
    return [t / asp, 0, 0, 0, 0, t, 0, 0, 0, 0, (f + n) / (n - f), -1, 0, 0, 2 * f * n / (n - f), 0];
  }
  function mul(a, b) {
    var o = new Array(16);
    for (var c = 0; c < 4; c++) for (var r = 0; r < 4; r++) {
      var s = 0; for (var k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s;
    }
    return o;
  }
  function look(e, t) {
    var z = norm([e[0] - t[0], e[1] - t[1], e[2] - t[2]]), x = norm(cross([0, 1, 0], z)), y = cross(z, x);
    return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
      -dot(x, e), -dot(y, e), -dot(z, e), 1];
  }
  function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  function norm(a) { var l = Math.hypot(a[0], a[1], a[2]); return [a[0] / l, a[1] / l, a[2] / l]; }
  function rotY(t) { var c = Math.cos(t), s = Math.sin(t); return [c, 0, -s, 0, 1, 0, s, 0, c]; }

  /* ---------------------------------------------------------------- state */
  var STEP_T = [2.4, 3.0, 4.2, 1.2];        // seconds each step takes when it plays
  var step = 0, t = 0, playing = false, yaw = 0.0, spin = 0.32, drag = null, idle = 0, visible = false;
  var clay = 1, sweep = 0, shown = LATHE_N, lastFlash = -1, flashT = 0;
  var steps = [].slice.call(root.querySelectorAll('.proc-steps button'));
  var codes = [].slice.call(root.querySelectorAll('.proc-code > div'));
  var count = root.querySelector('.proc-count');
  var replay = root.querySelector('.proc-replay');

  function setStep(s, keepT) {
    step = s; if (!keepT) t = 0;
    steps.forEach(function (b, i) { b.setAttribute('aria-pressed', i === s ? 'true' : 'false'); b.classList.toggle('done', i < s); });
    codes.forEach(function (c, i) { c.hidden = i !== s; });
    root.dataset.step = s;
  }
  function finish(s) {            // jump straight to the end of a step
    setStep(s); playing = false; t = STEP_T[s];
    root.classList.add('paused');
  }
  steps.forEach(function (b, i) { b.addEventListener('click', function () { setStep(i); playing = true; root.classList.remove('paused'); }); });
  replay.addEventListener('click', function () { setStep(0); playing = true; root.classList.remove('paused'); });

  /* drag to turn her */
  cv.addEventListener('pointerdown', function (e) { drag = { x: e.clientX, y: yaw }; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove', function (e) { if (drag) { yaw = drag.y + (e.clientX - drag.x) * 0.012; idle = 0; } });
  ['pointerup', 'pointercancel'].forEach(function (n) { cv.addEventListener(n, function () { drag = null; idle = 0; }); });

  function resize() {
    var d = Math.min(window.devicePixelRatio || 1, 2), w = cv.clientWidth, h = cv.clientHeight;
    if (cv.width !== Math.round(w * d)) { cv.width = ov.width = Math.round(w * d); cv.height = ov.height = Math.round(h * d); }
  }

  /* the side profile of the body: the zones' points, as drawn in buildZones() */
  function profile() {
    var D = P.BAND_INSET, pts = [], n = 48;
    for (var i = 0; i <= n; i++) {
      var a = P.A5 * i / n;
      pts.push([P.R * Math.sin(a), P.R * P.YS * Math.cos(a)]);
    }
    var e = [P.R * Math.sin(P.A5), P.R * P.YS * Math.cos(P.A5)];
    pts.push([0.136, e[1] - 0.027], [0.118, e[1] - 0.045], [0.118, e[1] - 0.055], [0, e[1] - 0.057]);
    return pts;
  }
  var PROF = profile();

  function project(vp, p) {
    var x = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
    var y = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
    var w = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
    return [(x / w * 0.5 + 0.5) * ov.width, (0.5 - y / w * 0.5) * ov.height];
  }

  var last = 0;
  function frame(now) {
    requestAnimationFrame(frame);
    if (!visible) { last = now; return; }
    var dt = Math.min((now - last) / 1000 || 0, 0.05); last = now;
    resize();

    /* --- advance the story of the four steps */
    if (playing) {
      t += dt;
      if (t >= STEP_T[step]) {
        if (step < 3) setStep(step + 1); else { playing = false; root.classList.add('paused'); }
      }
    }
    var k = Math.min(t / STEP_T[step], 1);
    sweep = step === 0 ? 0 : step === 1 ? k : 1;
    shown = step < 2 ? LATHE_N : step === 2 ? LATHE_N + Math.floor(k * (G.length - LATHE_N) + 0.0001) : G.length;
    if (step === 2 && k >= 1) shown = G.length;
    clay = step < 3 ? 1 : 1 - Math.min(k * 1.2, 1);
    if (shown - 1 !== lastFlash && step === 2) { lastFlash = shown - 1; flashT = 0.35; }
    flashT = Math.max(flashT - dt, 0);

    if (!drag) { idle += dt; if (!reduce && (step >= 2 || idle > 2)) yaw += spin * dt * (step === 0 ? 0 : 1); }
    var tilt = step === 0 ? 0.0 : 0.28;

    /* --- camera */
    var asp = cv.width / cv.height, dist = asp >= 1.3 ? 1.25 : 1.6 / Math.min(1, asp);
    var eye = [0, Math.sin(tilt) * dist, Math.cos(tilt) * dist];
    var vp = mul(persp(0.52, asp, 0.05, 10), look(eye, [0, -0.01, 0]));
    var R = step === 0 ? rotY(0) : rotY(yaw);

    gl.viewport(0, 0, cv.width, cv.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.uniformMatrix4fv(U.uVP, false, new Float32Array(vp));
    gl.uniformMatrix3fv(U.uR, false, new Float32Array(R));
    gl.uniform3fv(U.uEye, eye);
    gl.uniform1f(U.uClay, clay);

    function draw(g) {
      gl.bindBuffer(gl.ARRAY_BUFFER, g.pos); gl.enableVertexAttribArray(A.aP); gl.vertexAttribPointer(A.aP, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, g.nrm); gl.enableVertexAttribArray(A.aN); gl.vertexAttribPointer(A.aN, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, g.uv); gl.enableVertexAttribArray(A.aU); gl.vertexAttribPointer(A.aU, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, g.col); gl.enableVertexAttribArray(A.aC); gl.vertexAttribPointer(A.aC, 1, gl.FLOAT, false, 0, 0);
      gl.uniform3fv(U.uAlb, g.alb); gl.uniform3fv(U.uEmit, g.emit);
      gl.uniform1f(U.uRough, g.rough); gl.uniform1f(U.uMetal, g.metal);
      gl.uniform1f(U.uKind, g.kind); gl.uniform1f(U.uAlpha, g.alpha);
      gl.uniform1f(U.uSweep, g.lathe ? sweep : 2.0);
      gl.uniform1f(U.uFlash, g.i === lastFlash && step === 2 ? flashT / 0.35 : 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.idx);
      gl.drawElements(gl.TRIANGLES, g.count, gl.UNSIGNED_SHORT, 0);
    }
    if (step >= 1) {
      gl.disable(gl.BLEND); gl.depthMask(true);
      for (var i = 0; i < shown; i++) if (!G[i].glow) draw(G[i]);
      if (clay < 1) {
        gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE); gl.depthMask(false);
        for (i = 0; i < shown; i++) if (G[i].glow) draw(G[i]);
        gl.depthMask(true); gl.disable(gl.BLEND);
      }
    }

    /* --- the profile line, on the overlay: drawn on in step 0, riding the sweep in step 1 */
    var c = ov.getContext('2d');
    c.clearRect(0, 0, ov.width, ov.height);
    if (step <= 1) {
      var d = Math.min(window.devicePixelRatio || 1, 2);
      var ang = step === 0 ? Math.PI / 2 : 2 * Math.PI * (sweep - 0.5);
      var upto = step === 0 ? Math.max(2, Math.floor(PROF.length * Math.min(k * 1.25, 1))) : PROF.length;
      var sides = step === 0 ? [ang] : [ang];
      sides.forEach(function (a) {
        c.beginPath();
        for (var j = 0; j < upto; j++) {
          var p = PROF[j], w = [p[0] * Math.sin(a), p[1], p[0] * Math.cos(a)];
          var q = R; w = [q[0] * w[0] + q[3] * w[1] + q[6] * w[2], q[1] * w[0] + q[4] * w[1] + q[7] * w[2], q[2] * w[0] + q[5] * w[1] + q[8] * w[2]];
          var s = project(vp, w);
          if (j) c.lineTo(s[0], s[1]); else c.moveTo(s[0], s[1]);
        }
        c.strokeStyle = '#7fd4d8'; c.lineWidth = 2.2 * d; c.lineJoin = 'round'; c.stroke();
        for (j = 0; j < upto; j += 4) {
          var p2 = PROF[j], w2 = [p2[0] * Math.sin(a), p2[1], p2[0] * Math.cos(a)];
          w2 = [R[0] * w2[0] + R[3] * w2[1] + R[6] * w2[2], R[1] * w2[0] + R[4] * w2[1] + R[7] * w2[2], R[2] * w2[0] + R[5] * w2[1] + R[8] * w2[2]];
          var s2 = project(vp, w2);
          c.fillStyle = '#e6a74a'; c.beginPath(); c.arc(s2[0], s2[1], 2.6 * d, 0, 6.2832); c.fill();
        }
      });
      if (step === 0) {          /* the axis it will turn round */
        var top = project(vp, [0, 0.27, 0]), bot = project(vp, [0, -0.25, 0]);
        c.setLineDash([4 * d, 5 * d]); c.strokeStyle = 'rgba(143,138,129,.7)'; c.lineWidth = 1 * d;
        c.beginPath(); c.moveTo(top[0], top[1]); c.lineTo(bot[0], bot[1]); c.stroke(); c.setLineDash([]);
      }
    }

    var tris = 0;
    for (var m = 0; m < (step === 0 ? 0 : shown); m++) tris += G[m].tris;
    var LT = window.LT || {}, fmt = LT['{0} / {1} 부품 · 삼각형 {2}개'] || '{0} / {1} 부품 · 삼각형 {2}개';
    count.textContent = fmt.replace('{0}', step === 0 ? 0 : shown).replace('{1}', G.length)
                           .replace('{2}', tris.toLocaleString(document.documentElement.lang || 'ko'));
  }

  /* play once the first time she scrolls into view; run only while she is visible */
  new IntersectionObserver(function (es) {
    es.forEach(function (e) {
      visible = e.isIntersecting;
      if (visible && !root.dataset.seen) {
        root.dataset.seen = '1';
        if (reduce) finish(3); else { setStep(0); playing = true; }
      }
    });
  }, { threshold: 0.35 }).observe(root);
  setStep(0);
  requestAnimationFrame(frame);
})();
