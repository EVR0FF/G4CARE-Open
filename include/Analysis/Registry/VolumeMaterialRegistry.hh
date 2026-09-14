//==============================================================================
// G4CARE
// @file    VolumeMaterialRegistry.hh
// @brief   Registry that maps logical volumes and materials to integer IDs
//          and caches material properties (density, Z_eff, A_eff, optical
//          constants, etc.) for fast O(1) lookup.
// @details Assigns sequential IDs to G4LogicalVolume and G4Material pointers,
//   computes Z_eff / A_eff mass-fraction-weighted, caches optical constants
//   from G4MaterialPropertiesTable, and provides BuildCaches for index-based
//   lookup vectors.
//
//   Configuration keys read: none (registry data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef VOLUME_MATERIAL_REGISTRY_HH
#define VOLUME_MATERIAL_REGISTRY_HH

#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include <vector>
#include <map>
#include <string>

/// @brief Maps volumes and materials to IDs with cached property lookup.
class VolumeMaterialRegistry {
public:
    VolumeMaterialRegistry() = default;
    ~VolumeMaterialRegistry() = default;

    // Get or create ID for logical volume
    int GetVolumeID(const G4LogicalVolume* lv);
    int GetVolumeID(const G4String& pvName, G4int copyNo);
    // Get or create ID for material
    int GetMaterialID(const G4Material* mat);
    // Get volume name by ID
    const G4String& GetVolumeName(int id) const;
    // Get material name by ID
    const G4String& GetMaterialName(int id) const;

    // Material properties (by material ID)
    double GetMaterialDensity(int matId) const;
    double GetMaterialTemperature(int matId) const;
    double GetMaterialPressure(int matId) const;
    G4State GetMaterialState(int matId) const;
    const G4String& GetMaterialChemicalFormula(int matId) const;
    double GetMaterialRadiationLength(int matId) const;
    double GetMaterialNuclearInteractionLength(int matId) const;
    double GetMaterialZeff(int matId) const;
    double GetMaterialAeff(int matId) const;
    double GetMaterialRIndex(int matId) const;
    double GetMaterialScintillationYield(int matId) const;
    double GetMaterialFastTimeConstant(int matId) const;
    double GetMaterialSlowTimeConstant(int matId) const;
    double GetMaterialYieldRatio(int matId) const;
    double GetMaterialResolutionScale(int matId) const;
    double GetMaterialAbsorptionLength(int matId) const;
    double GetMaterialWLSComponent(int matId) const;
    double GetMaterialWLSTimeConstant(int matId) const;
    double GetMaterialRayleighLength(int matId) const;

    // Build caches for thread-local access (call after all volumes/materials are known)
    void BuildCaches(std::vector<int>& volIdCache, std::vector<int>& matIdCache) const;

    // Register parent relationship
    void SetParentLogicalVolume(const G4LogicalVolume* child, const G4LogicalVolume* parent);
    const G4LogicalVolume* GetParentLogicalVolume(const G4LogicalVolume* lv) const;

    // Clear all data (e.g., at the end of run)
    void Clear();

private:
    struct MaterialInfo {
        G4String name;
        double density;
        double temperature;
        double pressure;
        G4State state;
        G4String chemicalFormula;
        double radlen;
        double nucintlen;
        double zeff;
        double aeff;
        // Optical properties
        double rindex;                 // refractive index
        double scintillationYield;     // photons/MeV
        double fastTimeConstant;       // fast component (ns)
        double slowTimeConstant;       // slow component (ns)
        double yieldRatio;             // fast-component fraction
        double resolutionScale;        // smearing scale
        double absorptionLength;       // absorption length (mm)
        double wlsComponent;           // WLS component (0/1)
        double wlsTimeConstant;        // WLS time (ns)
        double rayleighLength;         // Rayleigh length (mm)
    };
    void ComputeMaterialInfo(const G4Material* mat, MaterialInfo& info) const;

    std::map<const G4LogicalVolume*, int> fVolumeToID;
    std::map<std::string, int> fVolumeKeyToID;
    std::vector<G4String> fVolumeNames;
    std::map<const G4Material*, int> fMaterialToID;
    std::vector<G4String> fMaterialNames;
    std::vector<MaterialInfo> fMaterialInfo;
    std::map<const G4LogicalVolume*, const G4LogicalVolume*> fLogicalToParent;
};

#endif