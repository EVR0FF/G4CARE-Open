//==============================================================================
// G4CARE
// @file    VolumeMaterialRegistry.cc
// @brief   Registry that maps logical volumes and materials to integer IDs
//          and caches material properties (density, Z_eff, A_eff, optical
//          properties, etc.) for fast lookup.
// @details VolumeMaterialRegistry assigns sequential IDs to G4LogicalVolume
//   pointers and G4Material pointers.  It computes and stores material info
//   (Z_eff / A_eff mass-fraction-weighted), caches optical constants from the
//   G4MaterialPropertiesTable, and provides O(1) getters for all stored
//   properties.  The BuildCaches method pre-populates index-based lookup
//   vectors for use in event processing.
//
//   Configuration keys read: none (registry data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "VolumeMaterialRegistry.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4SystemOfUnits.hh"
#include <algorithm>

/// @brief Retrieves or creates a volume ID from a physical volume name and copy number.
/// @param pvName Physical volume name.
/// @param copyNo Copy number.
/// @return Volume ID.
int VolumeMaterialRegistry::GetVolumeID(const G4String& pvName, G4int copyNo) {
    std::string key = pvName + "_" + std::to_string(copyNo);
    auto it = fVolumeKeyToID.find(key);
    if (it != fVolumeKeyToID.end()) {
        return it->second;
    }
    int id = static_cast<int>(fVolumeNames.size());
    fVolumeKeyToID[key] = id;
    fVolumeNames.push_back(pvName);
    return id;
}

/// @brief Retrieves or creates a volume ID from a logical volume pointer.
/// @param lv Logical volume (may be nullptr).
/// @return Volume ID, or -1 if nullptr.
int VolumeMaterialRegistry::GetVolumeID(const G4LogicalVolume* lv) {
    if (!lv) return -1;
    auto it = fVolumeToID.find(lv);
    if (it != fVolumeToID.end()) return it->second;
    int id = static_cast<int>(fVolumeNames.size());
    fVolumeToID[lv] = id;
    fVolumeNames.push_back(lv->GetName());
    return id;
}

/// @brief Retrieves or creates a material ID from a G4Material pointer.
///        On first encounter the material info is computed and cached.
/// @param mat G4Material pointer (may be nullptr).
/// @return Material ID, or -1 if nullptr.
int VolumeMaterialRegistry::GetMaterialID(const G4Material* mat) {
    if (!mat) return -1;
    auto it = fMaterialToID.find(mat);
    if (it != fMaterialToID.end()) return it->second;
    int id = static_cast<int>(fMaterialNames.size());
    fMaterialToID[mat] = id;
    fMaterialNames.push_back(mat->GetName());

    // Store material info
    MaterialInfo info;
    ComputeMaterialInfo(mat, info);
    fMaterialInfo.push_back(info);

    return id;
}

/// @brief Returns the volume name for a given volume ID.
/// @param id Volume ID.
/// @return Volume name string; empty if ID is invalid.
const G4String& VolumeMaterialRegistry::GetVolumeName(int id) const {
    static G4String empty;
    if (id >= 0 && id < static_cast<int>(fVolumeNames.size()))
        return fVolumeNames[id];
    return empty;
}

/// @brief Returns the material name for a given material ID.
/// @param id Material ID.
/// @return Material name string; empty if ID is invalid.
const G4String& VolumeMaterialRegistry::GetMaterialName(int id) const {
    static G4String empty;
    if (id >= 0 && id < static_cast<int>(fMaterialNames.size()))
        return fMaterialNames[id];
    return empty;
}

/// @brief Computes and stores all material properties (density, state, Z_eff,
///        A_eff, radiation/nuclear lengths, and optical constants) into the
///        MaterialInfo struct.
/// @param mat  Source G4Material.
/// @param[out] info MaterialInfo struct to fill.
void VolumeMaterialRegistry::ComputeMaterialInfo(const G4Material* mat, MaterialInfo& info) const {
    info.name = mat->GetName();
    info.density = mat->GetDensity();
    info.temperature = mat->GetTemperature();
    info.pressure = mat->GetPressure();
    info.state = mat->GetState();
    info.chemicalFormula = mat->GetChemicalFormula();
    info.radlen = mat->GetRadlen();
    info.nucintlen = mat->GetNuclearInterLength();

    // Compute Zeff and Aeff as weighted by atomic fractions (not mass fractions)
    const G4ElementVector* elements = mat->GetElementVector();
    const G4double* fractions = mat->GetFractionVector(); // mass fractions
    if (elements && mat->GetNumberOfElements() > 0) {
        double totalMass = 0.0;
        double zeff_num = 0.0, aeff_num = 0.0;
        for (size_t i = 0; i < mat->GetNumberOfElements(); ++i) {
            const G4Element* el = (*elements)[i];
            double Z = el->GetZ();
            double A = el->GetA() / (g/mole); // atomic mass in g/mol
            double massFraction = fractions[i];
            totalMass += massFraction;
            zeff_num += massFraction * Z;
            aeff_num += massFraction * A;
        }
        if (totalMass > 0.0) {
            info.zeff = zeff_num / totalMass;
            info.aeff = aeff_num / totalMass;
        } else {
            info.zeff = 0.0;
            info.aeff = 0.0;
        }
    } else {
        info.zeff = 0.0;
        info.aeff = 0.0;
    }
    // Optical properties
    G4MaterialPropertiesTable* mpt = mat->GetMaterialPropertiesTable();
    if (mpt) {
        info.rindex = mpt->GetConstProperty("RINDEX");
        info.scintillationYield = mpt->GetConstProperty("SCINTILLATIONYIELD");
        info.fastTimeConstant = mpt->GetConstProperty("FASTTIMECONSTANT");
        info.slowTimeConstant = mpt->GetConstProperty("SLOWTIMECONSTANT");
        info.yieldRatio = mpt->GetConstProperty("YIELDRATIO");
        info.resolutionScale = mpt->GetConstProperty("RESOLUTIONSCALE");
        info.absorptionLength = mpt->GetConstProperty("ABSLENGTH");
        info.wlsComponent = mpt->GetConstProperty("WLSCOMPONENT");
        info.wlsTimeConstant = mpt->GetConstProperty("WLSTIMECONSTANT");
        info.rayleighLength = mpt->GetConstProperty("RAYLEIGH");
    } else {
        // default values
        info.rindex = 1.0;
        info.scintillationYield = 0.0;
        info.fastTimeConstant = 0.0;
        info.slowTimeConstant = 0.0;
        info.yieldRatio = 1.0;
        info.resolutionScale = 1.0;
        info.absorptionLength = -1.0;
        info.wlsComponent = 0.0;
        info.wlsTimeConstant = 0.0;
        info.rayleighLength = -1.0;
    }

}

/// @brief Returns material density (internal Geant4 units).
/// @param matId Material ID.
/// @return Density, or -1.0 if invalid.
double VolumeMaterialRegistry::GetMaterialDensity(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].density;
}
/// @brief Returns material temperature (internal Geant4 units).
double VolumeMaterialRegistry::GetMaterialTemperature(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].temperature;
}
/// @brief Returns material pressure (internal Geant4 units).
double VolumeMaterialRegistry::GetMaterialPressure(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].pressure;
}
/// @brief Returns material state (solid, liquid, gas, undefined).
G4State VolumeMaterialRegistry::GetMaterialState(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return kStateUndefined;
    return fMaterialInfo[matId].state;
}
/// @brief Returns chemical formula string.
const G4String& VolumeMaterialRegistry::GetMaterialChemicalFormula(int matId) const {
    static G4String empty;
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return empty;
    return fMaterialInfo[matId].chemicalFormula;
}
/// @brief Returns radiation length (internal Geant4 units).
double VolumeMaterialRegistry::GetMaterialRadiationLength(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].radlen;
}
/// @brief Returns nuclear interaction length (internal Geant4 units).
double VolumeMaterialRegistry::GetMaterialNuclearInteractionLength(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].nucintlen;
}
/// @brief Returns mass-fraction-weighted effective atomic number Z_eff.
double VolumeMaterialRegistry::GetMaterialZeff(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].zeff;
}
/// @brief Returns mass-fraction-weighted effective atomic mass A_eff.
double VolumeMaterialRegistry::GetMaterialAeff(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].aeff;
}
/// @brief Returns refractive index (unitless, or 1.0 default).
double VolumeMaterialRegistry::GetMaterialRIndex(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].rindex;
}
/// @brief Returns scintillation yield (photons/MeV).
double VolumeMaterialRegistry::GetMaterialScintillationYield(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].scintillationYield;
}
/// @brief Returns fast time constant (ns).
double VolumeMaterialRegistry::GetMaterialFastTimeConstant(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].fastTimeConstant;
}
/// @brief Returns slow time constant (ns).
double VolumeMaterialRegistry::GetMaterialSlowTimeConstant(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].slowTimeConstant;
}
/// @brief Returns yield ratio (fast/slow).
double VolumeMaterialRegistry::GetMaterialYieldRatio(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].yieldRatio;
}
/// @brief Returns resolution scale.
double VolumeMaterialRegistry::GetMaterialResolutionScale(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].resolutionScale;
}
/// @brief Returns absorption length (internal Geant4 units, or -1 if absent).
double VolumeMaterialRegistry::GetMaterialAbsorptionLength(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].absorptionLength;
}
/// @brief Returns WLS component value.
double VolumeMaterialRegistry::GetMaterialWLSComponent(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].wlsComponent;
}
/// @brief Returns WLS time constant (ns).
double VolumeMaterialRegistry::GetMaterialWLSTimeConstant(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].wlsTimeConstant;
}
/// @brief Returns Rayleigh scattering length (internal Geant4 units).
double VolumeMaterialRegistry::GetMaterialRayleighLength(int matId) const {
    if (matId < 0 || matId >= static_cast<int>(fMaterialInfo.size())) return -1.0;
    return fMaterialInfo[matId].rayleighLength;
}

/// @brief Pre-populates index-based cache vectors for fast lookup during
///        event processing.
/// @param[out] volIdCache Volume instance ID → volume ID mapping.
/// @param[out] matIdCache Material index → material ID mapping.
void VolumeMaterialRegistry::BuildCaches(std::vector<int>& volIdCache,
                                         std::vector<int>& matIdCache) const {
    // Determine max instance ID for volumes
    int maxVolInst = 0;
    for (const auto& pair : fVolumeToID) {
        G4int inst = pair.first->GetInstanceID();
        if (inst > maxVolInst) maxVolInst = inst;
    }
    volIdCache.assign(maxVolInst + 1, -1);
    for (const auto& pair : fVolumeToID) {
        G4int inst = pair.first->GetInstanceID();
        if (inst >= 0 && inst < (G4int)volIdCache.size())
            volIdCache[inst] = pair.second;
    }

    // For materials, we use index
    int maxMatIdx = 0;
    for (const auto& pair : fMaterialToID) {
        size_t idx = pair.first->GetIndex();
        if (idx > (size_t)maxMatIdx) maxMatIdx = idx;
    }
    matIdCache.assign(maxMatIdx + 1, -1);
    for (const auto& pair : fMaterialToID) {
        size_t idx = pair.first->GetIndex();
        if (idx < matIdCache.size())
            matIdCache[idx] = pair.second;
    }
}

/// @brief Records the parent-child relationship between logical volumes.
/// @param child  Child logical volume.
/// @param parent Parent logical volume.
void VolumeMaterialRegistry::SetParentLogicalVolume(const G4LogicalVolume* child,
                                                    const G4LogicalVolume* parent) {
    if (child && parent) fLogicalToParent[child] = parent;
}

/// @brief Returns the parent logical volume of the given child.
/// @param lv Child logical volume.
/// @return Parent logical volume pointer, or nullptr if not set.
const G4LogicalVolume* VolumeMaterialRegistry::GetParentLogicalVolume(const G4LogicalVolume* lv) const {
    auto it = fLogicalToParent.find(lv);
    if (it != fLogicalToParent.end()) return it->second;
    return nullptr;
}

/// @brief Clears all registered volumes, materials, and cached data.
void VolumeMaterialRegistry::Clear() {
    fVolumeToID.clear();
    fVolumeNames.clear();
    fMaterialToID.clear();
    fMaterialNames.clear();
    fMaterialInfo.clear();
    fLogicalToParent.clear();
}