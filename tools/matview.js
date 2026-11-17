/* Compile a material headlessly and write it out as a PNG, so a recipe can be
   looked at without waiting three minutes for a radiosity bake first.

   Slices the operator machine and the recipe table straight out of the built
   derelict.html, so what it draws is what the engine will draw -- including
   the shader's own albedo x (1 + emissive x 3.4) for a SKY or SPRITE
   material, which is why a starfield looks black in an image viewer and
   right here.  Writes matNN.png (what you see) and matNN_a.png (the alpha
   channel, which for a sprite is its coverage and for glass its opacity).

     node tools/matview.js <out-dir> [37,38,39]                             */
const fs = require('fs'), path = require('path'), os = require('os'),
      zlib = require('zlib');
const ROOT = path.dirname(__dirname);
const HTML = path.join(ROOT, 'src', 'derelict', 'derelict.html');
const L = fs.readFileSync(HTML, 'utf8').replace(/\r/g, '').split('\n');
const find = re => { for (let i = 0; i < L.length; i++) if (re.test(L[i])) return i;
                     throw new Error('anchor ' + re); };
const m0 = find(/^   0\.  MATH/) - 1, m1 = find(/^   2\.  LEVEL/) - 2;
const c0 = find(/^   4\.  MATERIAL COMPILER/) - 1, c1 = find(/^   5\.  RADIOSITY/) - 2;

const tmp = path.join(os.tmpdir(), 'matview_slice.js');
fs.writeFileSync(tmp,
  L.slice(m0, m1).join('\n') + '\n'
  + L.slice(c0, c1).join('\n') + '\n'
  + 'module.exports={compileMaterials,TN,TL,MAT_NAME,MAT_EMIS,MAT_ALBEDO,SURF};\n');
const M = require(tmp);

const N = M.TN;
const alb = new Uint8Array(M.TL * N * N * 4);
const nrm = new Uint8Array(M.TL * N * N * 4);

function png(w, h, rgba, file) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    rgba.copy ? rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4)
              : Buffer.from(rgba.buffer, y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  }
  const crcT = [];
  for (let n = 0; n < 256; n++) { let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    crcT[n] = c >>> 0; }
  const crc = b => { let c = 0xFFFFFFFF;
    for (const x of b) c = crcT[(c ^ x) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(td));
    return Buffer.concat([len, td, cc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  fs.writeFileSync(file, Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

const OUT = process.argv[2] || '.';
const want = (process.argv[3] || '37,38,39').split(',').map(Number);
for (const layer of want) {
  const t0 = Date.now();
  M.compileMaterials(layer, alb, nrm);
  const off = layer * N * N * 4;
  // what the shader would put on screen for this material, roughly
  const view = Buffer.alloc(N * N * 4);
  const surf = M.SURF[layer];
  for (let i = 0; i < N * N; i++) {
    const o = off + i * 4;
    const e = nrm[o + 3] / 255;
    let r = alb[o] / 255, g = alb[o + 1] / 255, b = alb[o + 2] / 255;
    if (surf >= 2) { const k = 1 + e * 3.4; r *= k; g *= k; b *= k; }
    else { r = r * 0.2 + r * e * 2.1; g = g * 0.2 + g * e * 2.1; b = b * 0.2 + b * e * 2.1; }
    const tm = v => Math.min(255, Math.round(255 * Math.pow(v / (v + 0.35), 1 / 2.2)));
    view[i * 4] = tm(r); view[i * 4 + 1] = tm(g); view[i * 4 + 2] = tm(b);
    view[i * 4 + 3] = 255;
  }
  png(N, N, view, path.join(OUT, 'mat' + layer + '.png'));
  // the alpha/coverage channel, for the sprite and the pane
  const cov = Buffer.alloc(N * N * 4);
  for (let i = 0; i < N * N; i++) {
    const a = alb[off + i * 4 + 3];
    cov[i * 4] = cov[i * 4 + 1] = cov[i * 4 + 2] = a; cov[i * 4 + 3] = 255;
  }
  png(N, N, cov, path.join(OUT, 'mat' + layer + '_a.png'));
  console.log(layer, M.MAT_NAME[layer], 'surf', surf,
    'emis', M.MAT_EMIS[layer].map(v => v.toFixed(4)).join(','),
    'alb', M.MAT_ALBEDO[layer].map(v => v.toFixed(4)).join(','),
    (Date.now() - t0) + 'ms');
}
