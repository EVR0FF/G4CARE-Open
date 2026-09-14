//==============================================================================
//
// G4CARE
//
// @file    GeometryPropertyRegistry.cc
// @brief   Volume property calculator (mass, density, radlen, etc.).
//
// @details
//   Builds a property map from GEOMETRY.OBJECTS configuration. For each
//   logical volume matched to a config object, computes: mass (kg),
//   volume (cm³), density (g/cm³), temperature (K), pressure (atm),
//   radiation length (cm), nuclear interaction length (cm).
//
//   Configuration keys read:
//     GEOMETRY.OBJECTS (subsection names)
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "GeometryPropertyRegistry.hh"
#include "ConfigManager.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4LogicalVolume.hh"
#include "G4VSolid.hh"
#include "G4Material.hh"
#include "G4ios.hh"
#include <algorithm>
#include <cctype>

GeometryPropertyRegistry* GeometryPropertyRegistry::fInstance = nullptr;

//------------------------------------------------------------------------------
// @brief Returns the singleton instance.
//------------------------------------------------------------------------------
GeometryPropertyRegistry* GeometryPropertyRegistry::Instance() {
    if (!fInstance) fInstance = new GeometryPropertyRegistry();
    return fInstance;
}

//------------------------------------------------------------------------------
// @brief Deletes the singleton instance.
//------------------------------------------------------------------------------
void GeometryPropertyRegistry::DeleteInstance() { delete fInstance; fInstance = nullptr; }

//------------------------------------------------------------------------------
// @brief Builds property map from GEOMETRY.OBJECTS configuration.
//
// @param cfg  ConfigManager instance.
//------------------------------------------------------------------------------
void GeometryPropertyRegistry::Build(ConfigManager* cfg) {
    fProperties.clear();
    auto* store = G4LogicalVolumeStore::GetInstance();
    if (!store || !cfg) return;

    // Get the list of configured object names from GEOMETRY.OBJECTS.
    auto objNames = cfg->GetSubsections("GEOMETRY.OBJECTS");

    for (auto* lv : *store) {
        if (!lv || !lv->GetSolid()) continue;
        std::string lvName = lv->GetName();
        // Match LV name against config object names (with or without _LV suffix).
        std::string matched;
        for (const auto& obj : objNames) {
            if (lvName == obj || lvName == obj + "_LV") { matched = obj; break; }
        }
        if (matched.empty()) continue;
        AddVolume(matched, lv);
    }
    G4cout << "[GeoPropReg] Registered " << fProperties.size() << " properties for "
           << (fProperties.size() / std::max(1, 3)) << " volumes." << G4endl;
}

//------------------------------------------------------------------------------
// @brief Retrieves a property value by key.
//
// @param key         Property key (e.g. "target_mass").
// @param defaultVal  Default value if key not found.
// @return            Property value or defaultVal.
//------------------------------------------------------------------------------
double GeometryPropertyRegistry::Get(const std::string& key, double d) const {
    auto it = fProperties.find(key); return (it != fProperties.end()) ? it->second : d;
}

//------------------------------------------------------------------------------
// @brief Computes and registers physical properties for a logical volume.
//
// @param prefix  Config object name used as property key prefix.
// @param lv      Pointer to the logical volume.
//------------------------------------------------------------------------------
void GeometryPropertyRegistry::AddVolume(const std::string& prefix, const G4LogicalVolume* lv) {
    double cubic = lv->GetSolid()->GetCubicVolume() / CLHEP::cm3;
    const G4Material* mat = lv->GetMaterial();
    double density_gcm3 = mat ? mat->GetDensity() / (CLHEP::g/CLHEP::cm3) : 1.0;
    double mass_kg = density_gcm3 * cubic / 1000.0;
    fProperties[prefix + "_mass"] = mass_kg;
    fProperties[prefix + "_volume"] = cubic;
    fProperties[prefix + "_density"] = density_gcm3;
    if (mat) {
        fProperties[prefix + "_temperature"] = mat->GetTemperature() / CLHEP::kelvin;
        fProperties[prefix + "_pressure"] = mat->GetPressure() / CLHEP::atmosphere;
        fProperties[prefix + "_radlen"] = mat->GetRadlen() / CLHEP::cm;
        fProperties[prefix + "_nuclen"] = mat->GetNuclearInterLength() / CLHEP::cm;
    }
}