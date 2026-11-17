/* Runs the frozen generator -- props/lumi_v02/lumi_geometry.js -- and
   prints every part as its name, its vertex and triangle counts, and its
   bounding box, to diff against `playlumi.exe -lumimesh`.
 *
 * This is vmwcheck.js's job for LUMI, and it exists for the same reason:
 * LUMI-SPEC.md's own checksum is three totals, and three totals cannot
 * tell a correct mesh from one built with a transposed rotation or a
 * flipped sign -- neither moves a count.  Six numbers a part can.
 *
 *   node tools/lumicheck.js                 > a.txt
 *   native/build/playlumi.exe -lumimesh -log > b.txt   (the head of the log)
 *   diff a.txt b.txt
 *
 * The exe prints one extra row, `mark_lumi`: the nameplate lettering that
 * LUMI-SPEC.md §8 tells a native port to substitute for the decal's canvas
 * text.  It is not one of the generator's 35 and is not in this table.
 *
 * No dependencies, by house rule -- it requires the generator and nothing
 * else.
 */
"use strict";
var path = require("path");
var LG = require(path.join(__dirname, "..", "props", "lumi_v02", "lumi_geometry.js"));

var parts = LG.buildLumi();
var f = function (x) { return (x < 0 ? "" : " ") + x.toFixed(4); };

console.log(["part".padEnd(16), "verts".padStart(5), "tris".padStart(5), "",
             "minx".padStart(8), "miny".padStart(8), "minz".padStart(8), "",
             "maxx".padStart(8), "maxy".padStart(8), "maxz".padStart(8)].join(" "));

parts.forEach(function (p) {
  var lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
  for (var i = 0; i < p.positions.length; i++) {
    var k = i % 3, v = p.positions[i];
    if (v < lo[k]) lo[k] = v;
    if (v > hi[k]) hi[k] = v;
  }
  console.log([p.name.padEnd(16),
               String(p.positions.length / 3).padStart(5),
               String(p.indices.length / 3).padStart(5), "",
               f(lo[0]).padStart(8), f(lo[1]).padStart(8), f(lo[2]).padStart(8), "",
               f(hi[0]).padStart(8), f(hi[1]).padStart(8), f(hi[2]).padStart(8)].join(" "));
});

var c = LG.checksum(parts);
console.log("spec 6:  parts " + c.parts + "  vertices " + c.vertices +
            "  triangles " + c.triangles);
