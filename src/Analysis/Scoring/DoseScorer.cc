//==============================================================================
// G4CARE
// @file    DoseScorer.cc
// @brief   Voxel dose-grid scorer implementation (RTDOSE export).
//==============================================================================
#include "DoseScorer.hh"

#include "CTSeries.hh"
#include "RTDose.hh"
#include "DicomService.hh"

#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"

#include <algorithm>
#include <cmath>

namespace {
double Dot(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
}  // namespace

DoseScorer* DoseScorer::Instance() {
    static DoseScorer s;
    return &s;
}

void DoseScorer::Configure(const CTSeries& ct) {
    fConfigured = false;
    if (!ct.valid || ct.nx <= 0 || ct.ny <= 0 || ct.nz <= 0) return;
    if (ct.dxMm <= 0.0 || ct.dyMm <= 0.0 || ct.dzMm <= 0.0) return;

    fNx = ct.nx; fNy = ct.ny; fNz = ct.nz;
    fDx = ct.dxMm; fDy = ct.dyMm; fDz = ct.dzMm;
    fCorner[0] = ct.originMm[0]; fCorner[1] = ct.originMm[1]; fCorner[2] = ct.originMm[2];
    {
        const double* woff = DicomService::Instance()->GetWorldOffset();
        for (int i = 0; i < 3; ++i) fCorner[i] -= woff[i];
    }
    // CTSeries.direction: [0..2] = row (+iy), [3..5] = column (+ix),
    // [6..8] = slice normal (+iz) — all in the DICOM LPS frame.
    fRow[0] = ct.direction[0]; fRow[1] = ct.direction[1]; fRow[2] = ct.direction[2];
    fCol[0] = ct.direction[3]; fCol[1] = ct.direction[4]; fCol[2] = ct.direction[5];
    fSlc[0] = ct.direction[6]; fSlc[1] = ct.direction[7]; fSlc[2] = ct.direction[8];
    // RTDOSE Image Position (Patient) = centre of the first voxel.
    for (int i = 0; i < 3; ++i) {
        fCenter[i] = fCorner[i] + 0.5 * (fDx * fCol[i] + fDy * fRow[i] + fDz * fSlc[i]);
    }

    fMergedGrid.assign(static_cast<std::size_t>(fNx) * fNy * fNz, 0.0);
    fMerged = false;
    fConfigured = true;
}

void DoseScorer::BeginOfRun(std::size_t nThreads) {
    if (!fConfigured) return;
    if (nThreads == 0) nThreads = 1;
    const std::size_t total = static_cast<std::size_t>(fNx) * fNy * fNz;
    fThreadGrids.assign(nThreads, std::vector<double>(total, 0.0));
    std::fill(fMergedGrid.begin(), fMergedGrid.end(), 0.0);
    fMerged = false;
}

void DoseScorer::Deposit(const G4ThreeVector& pos, double edepMeV) {
    if (!fConfigured || edepMeV <= 0.0 || fThreadGrids.empty()) return;

    // World (LPS, mm) -> fractional voxel indices.
    const double rel[3] = { pos.x() - fCorner[0], pos.y() - fCorner[1], pos.z() - fCorner[2] };
    const double fx = Dot(rel, fCol) / fDx;
    const double fy = Dot(rel, fRow) / fDy;
    const double fz = Dot(rel, fSlc) / fDz;
    const int ix = static_cast<int>(std::floor(fx));
    const int iy = static_cast<int>(std::floor(fy));
    const int iz = static_cast<int>(std::floor(fz));
    if (ix < 0 || ix >= fNx || iy < 0 || iy >= fNy || iz < 0 || iz >= fNz) return;

    const std::size_t idx = (static_cast<std::size_t>(iz) * fNy + iy) * fNx + ix;

    // Each thread writes ONLY to its own slot — lock-free.
    const G4int rawTid = G4Threading::G4GetThreadId();
    const std::size_t tid = (rawTid < 0) ? 0 : static_cast<std::size_t>(rawTid);
    if (tid >= fThreadGrids.size()) return;
    fThreadGrids[tid][idx] += edepMeV;
}

void DoseScorer::DepositVoxel(int voxelIndex, double edepMeV) {
    if (!fConfigured || edepMeV <= 0.0 || fThreadGrids.empty()) return;
    const std::size_t total = static_cast<std::size_t>(fNx) * fNy * fNz;
    if (voxelIndex < 0 || static_cast<std::size_t>(voxelIndex) >= total) return;

    // Each thread writes ONLY to its own slot — lock-free.
    const G4int rawTid = G4Threading::G4GetThreadId();
    const std::size_t tid = (rawTid < 0) ? 0 : static_cast<std::size_t>(rawTid);
    if (tid >= fThreadGrids.size()) return;
    fThreadGrids[tid][static_cast<std::size_t>(voxelIndex)] += edepMeV;
}

void DoseScorer::EndOfRun() {
    if (!fConfigured) return;
    const std::size_t total = fMergedGrid.size();
    for (std::size_t i = 0; i < total; ++i) {
        double s = 0.0;
        for (const auto& g : fThreadGrids) s += g[i];
        fMergedGrid[i] = s;
    }
    fMerged = true;
}

bool DoseScorer::BuildRTDose(RTDose& out) const {
    if (!fConfigured || !fMerged) return false;
    out = RTDose{};
    out.nx = fNx; out.ny = fNy; out.nz = fNz;
    out.dxMm = fDx; out.dyMm = fDy; out.dzMm = fDz;
    for (int i = 0; i < 3; ++i) out.originMm[i] = fCenter[i];
    out.direction[0] = fRow[0]; out.direction[1] = fRow[1]; out.direction[2] = fRow[2];
    out.direction[3] = fCol[0]; out.direction[4] = fCol[1]; out.direction[5] = fCol[2];
    out.direction[6] = fSlc[0]; out.direction[7] = fSlc[1]; out.direction[8] = fSlc[2];
    out.doseGridScaling = 1.0;
    out.doseUnits = "GY";
    out.valid = true;

    // dose[Gy] = E[J] / mass[kg]. Per-voxel water mass = volume * 1000 kg/m^3.
    const double volM3 = (fDx * CLHEP::mm) * (fDy * CLHEP::mm) * (fDz * CLHEP::mm);
    if (volM3 <= 0.0) return false;
    const double massKg = volM3 * 1000.0;  // water density
    const std::size_t total = fMergedGrid.size();
    out.doseGy.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        out.doseGy[i] = (fMergedGrid[i] * CLHEP::MeV) / massKg;
    }
    return true;
}
