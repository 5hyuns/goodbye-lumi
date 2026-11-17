/* Sum every channel of every viewmodel vertex, from map.html's own VMW
   section, so the C port can be diffed against it.

     node tools/vmwcheck.js                 the JS numbers
     native/build/playlumi.exe -vmw -log    the C ones, same table

   THE SECTION IS RUN, NOT PARSED.  A regex that pulled the ring counts out
   of buildRotary would be a second mesh builder to keep in step with the
   first -- the same reason nativepack.py hands its sections to node.  So
   the IIFE is lifted whole and given what it asks for at construction
   time: a WebGL context that remembers nothing but its buffer uploads, and
   the four engine tables applyWeaponTable() writes into.

   The eighteen bufferData calls arrive in build order -- pistol, rotary,
   shotgun, each stat/spin/hands/hmov/magm, then the two casings and the
   flash card -- which is the order playlumi.exe prints them in.

   Sums, not hashes: JS is double and C is float, so the last bits will
   never agree, but a wrong ring order, a dropped cap, a flipped tangent or
   one bit of drift in mulberry32 moves a sum in the fourth digit.         */
const fs = require('fs'), path = require('path');

const SRC = path.join(__dirname, '..', 'src', 'derelict', 'map.html');
const html = fs.readFileSync(SRC, 'utf8');
const a = html.indexOf('const VMW=(function(){');
const b = html.indexOf('window.VMW=VMW;');
if (a < 0 || b < 0) throw new Error('VMW section not found in map.html');
let section = html.slice(a, b);
/* Count the two corners finalize() has to invent a tangent for: a face
   whose UVs are degenerate, and a welded corner whose neighbours cancel.
   These are the only corners where the C port and this one may disagree --
   the axis either picks is arbitrary, and the two arbitrary choices are
   made either side of a threshold, so a float and a double land differently.
   The counts must still match exactly; that is what makes it a tie-break
   rather than a bug. */
const FB = [0, 0], FBLOG = [];
section = section
  .replace('function finalize(B){', 'function finalize(B){FB[0]=FB[1]=0;')
  .replace('if(Math.abs(r)<1e-12){', 'if(Math.abs(r)<1e-12){FB[0]++;')
  .replace('if(l<1e-6){const ax', 'if(l<1e-6){FB[1]++;const ax');
/* the last `return out;` in the section is finalize's -- resample has one too */
const RO = section.lastIndexOf('return out;');
section = section.slice(0, RO) + 'FBLOG.push([FB[0],FB[1]]); ' + section.slice(RO);

/* ---- a context that only remembers what was uploaded ---------------- */
const BUFS = [];
const noop = () => {};
const obj = () => ({});
const gl = {
  createTexture: obj, bindTexture: noop, texImage2D: noop,
  texParameteri: noop, texParameterf: noop, generateMipmap: noop,
  activeTexture: noop, getExtension: () => null,
  createShader: obj, shaderSource: noop, compileShader: noop,
  getShaderParameter: () => true, getShaderInfoLog: () => '',
  createProgram: obj, attachShader: noop, linkProgram: noop,
  getProgramParameter: () => true, getProgramInfoLog: () => '',
  getUniformLocation: obj, useProgram: noop,
  createFramebuffer: obj, bindFramebuffer: noop, framebufferTexture2D: noop,
  drawBuffers: noop, viewport: noop, enable: noop, disable: noop,
  uniform1f: noop, uniform1i: noop, uniform2f: noop, uniform3f: noop,
  uniform1fv: noop, uniform3fv: noop, uniform4fv: noop,
  uniformMatrix3fv: noop, uniformMatrix4fv: noop,
  createVertexArray: obj, bindVertexArray: noop, createBuffer: obj,
  bindBuffer: noop, bufferData: (t, d) => { BUFS.push(d); },
  enableVertexAttribArray: noop, vertexAttribPointer: noop,
  drawArrays: noop, clear: noop, depthMask: noop, blendFunc: noop,
};
for (const k of ['TEXTURE_2D','RGBA8','RGBA','UNSIGNED_BYTE','REPEAT',
  'TEXTURE_WRAP_S','TEXTURE_WRAP_T','TEXTURE_MIN_FILTER','TEXTURE_MAG_FILTER',
  'LINEAR_MIPMAP_LINEAR','LINEAR','VERTEX_SHADER','FRAGMENT_SHADER',
  'COMPILE_STATUS','LINK_STATUS','FRAMEBUFFER','COLOR_ATTACHMENT0',
  'DEPTH_TEST','TEXTURE0','TEXTURE3','TEXTURE6','ARRAY_BUFFER','STATIC_DRAW',
  'FLOAT','TRIANGLES','DEPTH_BUFFER_BIT','BLEND','CULL_FACE','ONE'])
  gl[k] = 0;

/* the four tables applyWeaponTable() writes into, with the base rows it
   overwrites -- ids only, because it replaces everything else */
const WEP = [{ id: 'crowbar' }, { id: 'glock', mag: 18 },
             { id: 'shotgun', mag: 8 }, { id: 'smg', mag: 50 },
             { id: 'grenade' }];
const AMMO = { glock: 170, shotgun: 44, smg: 300, grenade: 4 };
const MAGS = { glock: 18, shotgun: 8, smg: 50 };
const HAVE = { crowbar: true, glock: true, shotgun: false, smg: false };

new Function('gl', 'WEP', 'AMMO', 'MAGS', 'HAVE', 'FB', 'FBLOG',
             section + '\nreturn VMW;')(gl, WEP, AMMO, MAGS, HAVE, FB, FBLOG);

/* ---- the same five numbers playlumi.exe prints ---------------------- */
const NAMES = [];
for (const g of ['pistol', 'rotary', 'shotgun'])
  for (const p of ['stat', 'spin', 'hands', 'hmov', 'magm'])
    NAMES.push(g + '.' + p);
NAMES.push('shell', 'shotshell', 'flashcard');

if (BUFS.length !== NAMES.length)
  console.log('WARNING: ' + BUFS.length + ' uploads, expected ' + NAMES.length);

for (let i = 0; i < BUFS.length; i++) {
  const d = BUFS[i], n = d.length / 13;
  let sp = 0, sn = 0, su = 0, st = 0, sw = 0, sm = 0;
  for (let v = 0; v < n; v++) {
    const q = v * 13;
    sp += d[q] + d[q + 1] + d[q + 2];
    sn += d[q + 3] + d[q + 4] + d[q + 5];
    su += d[q + 6] + d[q + 7];
    st += d[q + 8] + d[q + 9] + d[q + 10];
    sw += d[q + 11];
    sm += d[q + 12];
  }
  const pad = (s, w) => String(s).padStart(w);
  console.log('vmw  ' + (NAMES[i] || '?').padEnd(16) + ' ' + pad(n, 6)
    + '  pos ' + pad(sp.toFixed(4), 12) + '  nrm ' + pad(sn.toFixed(4), 12)
    + '  uv '  + pad(su.toFixed(4), 14) + '  tan ' + pad(st.toFixed(4), 12)
    + '  hand ' + pad(sw.toFixed(0), 7) + '  mat ' + pad(sm.toFixed(0), 8)
    + '  fb ' + (FBLOG[i] ? FBLOG[i][0] + '/' + FBLOG[i][1] : '?'));
}
