/* What a MODULES list actually costs, in faces.

   The kit is modelled in the browser, so the only honest way to size a map
   before building it is to run the same make() functions the renderer will.
   This slices the mesh operator machine and the kit registry straight out
   of the built derelict.html and replays a MODULES list through them.

   Usage:  node tools/kitcost.js [tools/_map_modules.js]                  */
const fs = require('fs'), path = require('path'), os = require('os');
const ROOT = path.dirname(__dirname);
const HTML = path.join(ROOT, 'src', 'derelict', 'derelict.html');
const L = fs.readFileSync(HTML, 'utf8').replace(/\r/g, '').split('\n');
const find = re => { for (let i = 0; i < L.length; i++) if (re.test(L[i])) return i;
                     throw new Error('anchor missing: ' + re); };

const a = find(/^   M\.  MESH OPERATORS/) - 1;          // the operator machine
let b = find(/^function buildShipModules\(\)\{/);       // .. through the kit
while (L[b] !== '}') b++;
const c = find(/^   0\.  MATH/) - 1;                    // and the maths it needs
const d = find(/^   2\.  LEVEL/) - 2;

const tmp = path.join(os.tmpdir(), 'kitcost_slice.js');
fs.writeFileSync(tmp,
  'const BR=[];function brush(){BR.push(1);}\n'
  + L.slice(c, d).join('\n') + '\n'
  + L.slice(a, b + 1).join('\n')
  + '\nmodule.exports={KIT,kitGroups};\n');
const M = require(tmp);

global.TT = new Proxy({}, { get: () => 0 });            // materials do not matter
const file = path.resolve(process.argv[2]
  || path.join(ROOT, 'tools', '_map_modules.js'));
const MODULES = require(file);

let tot = 0;
const per = {}, cnt = {};
for (const m of MODULES) {
  let n = 0;
  for (const g of M.kitGroups(m[0], m[5])) n += g.m.F.length;
  const k = M.KIT[m[0]].name;
  tot += n; per[k] = (per[k] || 0) + n; cnt[k] = (cnt[k] || 0) + 1;
}
console.log(path.basename(file) + ':  ' + MODULES.length + ' instances, '
  + tot.toLocaleString() + ' faces, ~' + (tot * 2).toLocaleString()
  + ' triangles');
for (const k of Object.keys(per).sort((x, y) => per[y] - per[x]))
  console.log('   ' + k.padEnd(10) + String(cnt[k]).padStart(5) + ' x '
    + String(per[k]).padStart(9) + '   (' + Math.round(per[k] / cnt[k])
    + ' each)');
