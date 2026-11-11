// gui/src/dicom/dvh.mjs
// Pure dose-volume histogram (DVH) computation. No vtk.js dependency, so the
// math can be unit-tested with plain Node.
//
// doseGy and mask share the same flat indexing: (z * ny + y) * nx + x.

export function computeDvh(doseGy, mask, dims, spacing) {
  const [nx, ny, nz] = dims;
  const total = nx * ny * nz;
  const dvCc = (spacing[0] * spacing[1] * spacing[2]) / 1000.0;  // mm^3 -> cc

  const vals = [];
  for (let i = 0; i < total; i++) {
    if (mask[i]) vals.push(doseGy[i]);
  }
  if (vals.length === 0) return null;
  vals.sort((a, b) => a - b);

  const volumeCc = vals.length * dvCc;
  let sum = 0;
  for (const v of vals) sum += v;
  const minGy = vals[0];
  const maxGy = vals[vals.length - 1];
  const meanGy = sum / vals.length;

  // Dx = dose received by at least x% of the volume (x% receive >= Dx),
  // i.e. the (1 - x) percentile of the dose distribution.
  const percentile = (frac) => {
    const idx = Math.max(0, Math.min(vals.length - 1,
      Math.round((1 - frac) * (vals.length - 1))));
    return vals[idx];
  };
  const d98 = percentile(0.98);
  const d95 = percentile(0.95);
  const d50 = percentile(0.50);

  // Dose to 2 cc: the dose level at the top of the hottest 2 cc. When the
  // structure is smaller than 2 cc this is clamped to the minimum dose.
  const n2cc = Math.min(vals.length, Math.max(1, Math.round(2.0 / dvCc)));
  const d2cc = vals[vals.length - n2cc];

  // Cumulative DVH curve: fraction of volume receiving >= dose, over NBINS.
  const NBINS = 200;
  const dose = new Float64Array(NBINS);
  const volFrac = new Float64Array(NBINS);
  for (let b = 0; b < NBINS; b++) {
    const D = maxGy * b / (NBINS - 1);
    dose[b] = D;
    // First index where vals[idx] >= D (binary search).
    let lo = 0, hi = vals.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (vals[mid] >= D) hi = mid; else lo = mid + 1;
    }
    volFrac[b] = (vals.length - lo) / vals.length;
  }

  return { volumeCc, minGy, maxGy, meanGy, d98, d95, d50, d2cc, curve: { dose, volFrac } };
}
