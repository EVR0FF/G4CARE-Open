// gui/test-dvh.mjs — plain-Node sanity check of dvh.mjs (no vtk.js needed).
import { computeDvh } from './src/dicom/dvh.mjs';

function approx(a, b, eps = 1e-6) {
  return Math.abs(a - b) <= eps;
}

// 2x2x2 grid, spacing 1x1x1 mm -> voxel volume 0.001 cc.
const dims = [2, 2, 2];
const spacing = [1, 1, 1];
// Indexing (z*ny+y)*nx+x: z=0 -> [1,2,3,4], z=1 -> [5,6,7,8].
const dose = new Float32Array([1, 2, 3, 4, 5, 6, 7, 8]);
const mask = new Uint8Array(8).fill(1);

const r = computeDvh(dose, mask, dims, spacing);
if (!r) { console.error('FAIL: null result'); process.exit(1); }

const checks = [
  ['volumeCc', r.volumeCc, 0.008],
  ['minGy', r.minGy, 1],
  ['maxGy', r.maxGy, 8],
  ['meanGy', r.meanGy, 4.5],
  ['d50', r.d50, 5],
  ['d95', r.d95, 1],
];
let ok = true;
for (const [name, got, want] of checks) {
  const pass = approx(got, want);
  console.log(`${pass ? 'PASS' : 'FAIL'} ${name}: got=${got} want=${want}`);
  if (!pass) ok = false;
}

// Curve must be monotonically non-increasing (cumulative).
const c = r.curve;
if (c.dose.length !== 200) { console.log('FAIL curve length'); ok = false; }
for (let i = 1; i < c.volFrac.length; i++) {
  if (c.volFrac[i] > c.volFrac[i - 1] + 1e-9) { console.log('FAIL curve not monotonic at', i); ok = false; break; }
}
console.log('curve[0] volFrac =', c.volFrac[0], '(expect 1)');
console.log('curve[last] volFrac =', c.volFrac[c.volFrac.length - 1], '(expect 1/8=0.125)');
if (!approx(c.volFrac[0], 1) || !approx(c.volFrac[c.volFrac.length - 1], 1 / 8)) ok = false;

console.log(ok ? '\nALL PASS' : '\nSOME FAILED');
process.exit(ok ? 0 : 1);
