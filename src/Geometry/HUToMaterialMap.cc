//==============================================================================
//
// G4CARE
//
// @file    HUToMaterialMap.cc
// @brief   HU -> material: ICRP tissue composition + continuous density
//          (Schneider/ICRU style, like OpenGATE).
//
// @details
//   A small set of ICRP tissue anchors (air / lung / adipose / muscle /
//   spongy bone / cortical bone) defines the *composition* (discrete tissue
//   switch, like GATE); the *density* is interpolated linearly between
//   anchors, so each HU step maps to its own material with the anchor tissue
//   composition but a scaled density.
//
//==============================================================================

#include "HUToMaterialMap.hh"

#include "G4Material.hh"
#include "G4NistManager.hh"
#include "G4SystemOfUnits.hh"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

HUToMaterialMap::HUToMaterialMap() {
    LoadDefault();
}

void HUToMaterialMap::LoadDefault() {
    G4NistManager* nist = G4NistManager::Instance();

    // Composition + density anchors (ICRP tissues).
    struct Anchor { short hu; const char* material; double density; };
    static const Anchor kAnchors[] = {
        { -1000, "G4_AIR",                  0.0012 },
        {  -600, "G4_LUNG_ICRP",            0.26   },
        {  -100, "G4_ADIPOSE_TISSUE_ICRP",  0.95   },
        {    50, "G4_MUSCLE_SKELETAL_ICRP", 1.05   },
        {   300, "G4_BONE_COMPACT_ICRU",    1.33   },
        {  1200, "G4_BONE_CORTICAL_ICRP",   1.92   },
    };
    const int nAnchor = static_cast<int>(sizeof(kAnchors) / sizeof(kAnchors[0]));

    // Cache base (composition) materials once.
    std::map<std::string, G4Material*> base;
    for (const auto& a : kAnchors) {
        G4Material* m = nist->FindOrBuildMaterial(a.material);
        if (!m) m = nist->FindOrBuildMaterial("G4_WATER");
        base[a.material] = m;
    }

    constexpr short kStep = 10;   // HU step for density interpolation

    fBins.clear();
    fMaterials.clear();
    for (short hu = kHuMin; hu <= kHuMax; hu += kStep) {
        // Surrounding anchors.
        int i = 0;
        while (i + 1 < nAnchor && hu > kAnchors[i + 1].hu) ++i;
        int j = (i + 1 < nAnchor) ? (i + 1) : i;
        if (hu <= kAnchors[0].hu) { i = 0; j = 0; }
        const Anchor& lo = kAnchors[i];
        const Anchor& hi = kAnchors[j];

        const double t = (hi.hu == lo.hu) ? 0.0
            : static_cast<double>(hu - lo.hu) / static_cast<double>(hi.hu - lo.hu);
        double dens = lo.density + t * (hi.density - lo.density);
        if (fDensityH.size() >= 2) {           // GATE-style density table override
            const double td = DensityAt(hu);
            if (td > 0.0) dens = td;
        }
        G4Material* comp = base[lo.material];  // discrete tissue (as GATE/Schneider)

        auto* m = new G4Material("HU_" + std::to_string(hu), dens * (g / cm3),
                                 static_cast<G4int>(comp->GetNumberOfElements()));
        for (std::size_t e = 0; e < comp->GetNumberOfElements(); ++e) {
            m->AddElement(const_cast<G4Element*>(comp->GetElement(static_cast<G4int>(e))),
                          comp->GetFractionVector()[e]);
        }
        fMaterials.push_back(m);
        fBins.push_back({ hu, static_cast<short>(hu + kStep - 1), m });
    }

    // Build an O(1) HU -> material-index LUT so that per-voxel lookups in
    // VoxelizedPhantom::Build (which can run over tens of millions of voxels)
    // don't do a linear scan over fBins on every voxel.
    fLutIndex.assign(static_cast<size_t>(kHuMax - kHuMin + 1), 0);
    for (std::size_t i = 0; i < fBins.size(); ++i) {
        const short lo = fBins[i].huMin;
        const short hi = (fBins[i].huMax > kHuMax) ? kHuMax : fBins[i].huMax;
        for (short hu = lo; hu <= hi; ++hu) {
            if (hu >= kHuMin) {
                fLutIndex[static_cast<size_t>(hu - kHuMin)] = static_cast<int>(i);
            }
        }
    }
}

bool HUToMaterialMap::LoadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[HUToMaterialMap] Cannot open table: " << path << std::endl;
        return false;
    }

    G4NistManager* nist = G4NistManager::Instance();
    std::vector<Bin> bins;
    std::vector<G4Material*> materials;

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        std::istringstream ss(line);
        int lo = 0, hi = 0;
        std::string name;
        double dens = -1.0;
        if (!(ss >> lo >> hi >> name)) continue;  // blank / malformed line
        ss >> dens;                                // optional density (g/cm3)

        G4Material* base = nist->FindOrBuildMaterial(name);
        if (!base) {
            std::cerr << "[HUToMaterialMap] Unknown material '" << name
                      << "' at line " << lineno << "; skipping." << std::endl;
            continue;
        }

        G4Material* m = base;
        if (dens > 0.0 && std::fabs(dens - base->GetDensity() / (g / cm3)) > 1e-9) {
            m = new G4Material(name + "_HU_" + std::to_string(lo), dens * (g / cm3),
                               static_cast<G4int>(base->GetNumberOfElements()));
            for (std::size_t e = 0; e < base->GetNumberOfElements(); ++e) {
                m->AddElement(const_cast<G4Element*>(base->GetElement(static_cast<G4int>(e))),
                              base->GetFractionVector()[e]);
            }
        }

        materials.push_back(m);
        bins.push_back({ static_cast<short>(lo), static_cast<short>(hi), m });
    }

    if (bins.empty()) {
        std::cerr << "[HUToMaterialMap] No valid rows in table: " << path << std::endl;
        return false;
    }

    fBins = std::move(bins);
    fMaterials = std::move(materials);

    fLutIndex.assign(static_cast<size_t>(kHuMax - kHuMin + 1), 0);
    for (std::size_t i = 0; i < fBins.size(); ++i) {
        const short lo = (fBins[i].huMin < kHuMin) ? kHuMin : fBins[i].huMin;
        const short hi = (fBins[i].huMax > kHuMax) ? kHuMax : fBins[i].huMax;
        for (short hu = lo; hu <= hi; ++hu) {
            fLutIndex[static_cast<size_t>(hu - kHuMin)] = static_cast<int>(i);
        }
    }

    std::cout << "[HUToMaterialMap] Loaded " << fBins.size() << " HU ranges from "
              << path << std::endl;
    return true;
}

bool HUToMaterialMap::LoadDensityTable(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[HUToMaterialMap] Cannot open density table: " << path << std::endl;
        return false;
    }
    std::vector<double> h, d;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        std::istringstream ss(line);
        double hv = 0.0, dv = 0.0;
        if (!(ss >> hv >> dv)) continue;
        h.push_back(hv);
        d.push_back(dv);
    }
    if (h.size() < 2) {
        std::cerr << "[HUToMaterialMap] Density table needs >=2 points: " << path << std::endl;
        return false;
    }
    for (std::size_t i = 1; i < h.size(); ++i) {
        if (h[i] <= h[i - 1]) {
            std::cerr << "[HUToMaterialMap] Density table HU must be strictly ascending." << std::endl;
            return false;
        }
    }
    fDensityH = std::move(h);
    fDensityD = std::move(d);
    LoadDefault();  // rebuild using the density curve
    std::cout << "[HUToMaterialMap] Loaded " << fDensityH.size() << " density points from "
              << path << std::endl;
    return true;
}

double HUToMaterialMap::DensityAt(short hu) const {
    if (fDensityH.size() < 2) return -1.0;
    const double h = static_cast<double>(hu);
    if (h <= fDensityH.front()) return fDensityD.front();
    if (h >= fDensityH.back()) return fDensityD.back();
    std::size_t i = 0;
    while (i + 1 < fDensityH.size() && h > fDensityH[i + 1]) ++i;
    const double t = (h - fDensityH[i]) / (fDensityH[i + 1] - fDensityH[i]);
    return fDensityD[i] + t * (fDensityD[i + 1] - fDensityD[i]);
}

int HUToMaterialMap::MaterialIndex(short hu) const {
    if (fLutIndex.empty()) return 0;
    short h = hu;
    if (h < kHuMin) h = kHuMin;
    if (h > kHuMax) h = kHuMax;
    return fLutIndex[static_cast<size_t>(h - kHuMin)];
}

G4Material* HUToMaterialMap::GetMaterial(short hu) const {
    const int i = MaterialIndex(hu);
    if (i < 0 || i >= static_cast<int>(fMaterials.size())) return nullptr;
    return fMaterials[static_cast<size_t>(i)];
}
