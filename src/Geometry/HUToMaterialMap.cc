//==============================================================================
//
// G4CARE
//
// @file    HUToMaterialMap.cc
// @brief   Implementation of HUToMaterialMap.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#include "HUToMaterialMap.hh"

#include "G4NistManager.hh"
#include "G4Material.hh"

HUToMaterialMap::HUToMaterialMap() {
    LoadDefault();
}

void HUToMaterialMap::LoadDefault() {
    G4NistManager* nist = G4NistManager::Instance();

    // (huMin, huMax, NIST material name)
    struct Def { short lo; short hi; const char* name; };
    static const Def kDefaults[] = {
        { -30000,  -950, "G4_AIR" },
        {   -950,  -120, "G4_LUNG_ICRP" },
        {   -120,   100, "G4_TISSUE_SOFT_ICRP" },
        {    100, 30000, "G4_BONE_COMPACT_ICRU" },
    };

    fBins.clear();
    fMaterials.clear();
    for (const auto& d : kDefaults) {
        G4Material* m = nist->FindOrBuildMaterial(d.name);
        if (!m) m = nist->FindOrBuildMaterial("G4_WATER");  // safety fallback
        fBins.push_back({d.lo, d.hi, m});
        bool present = false;
        for (auto* e : fMaterials) {
            if (e == m) { present = true; break; }
        }
        if (!present) fMaterials.push_back(m);
    }
}

G4Material* HUToMaterialMap::GetMaterial(short hu) const {
    for (const auto& b : fBins) {
        if (hu >= b.huMin && hu < b.huMax) return b.material;
    }
    return fBins.empty() ? nullptr : fBins.back().material;
}
