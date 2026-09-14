//==============================================================================
// G4CARE
// @file    MaterialExtractor.cc
// @brief   Extracts material-related column values (ID, name, density,
//          temperature, pressure, state, radiation/nuclear lengths, Z_eff,
//          A_eff, chemical formula) from the VolumeMaterialRegistry.
// @details MaterialExtractor resolves numeric material properties via the
//   VolumeMaterialRegistry and associated TypedRegistries (MaterialName,
//   ChemicalFormula).  The string overload builds a human-readable elemental
//   fraction string for the ChemicalFormula column.
//
//   Configuration keys read: none (registry-driven).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "MaterialExtractor.hh"
#include "G4SystemOfUnits.hh"

/// @brief Extracts a numeric material column value for the given material ID.
/// @param type  Column type (MaterialID, MaterialName-as-ID, Density,
///              Temperature, Pressure, State, RadiationLength,
///              NuclearInteractionLength, Zeff, Aeff, ChemicalFormula-as-ID).
/// @param src   UnifiedSource (unused for numeric lookup).
/// @param matId Material ID in the VolumeMaterialRegistry.
/// @return Extracted value, or -1.0/0.0 if invalid.
double MaterialExtractor::Get(ColType type, const UnifiedSource& src, int matId) const {
    if (matId < 0 || !fRegistry) return -1.0;

    switch (type) {
        case ColType::MaterialID:
            return static_cast<double>(matId);

        case ColType::MaterialName:
            if (fMaterialNameReg) {
                return static_cast<double>(fMaterialNameReg->GetID(fRegistry->GetMaterialName(matId)));
            }
            return 0.0;

        case ColType::Density:
            return fRegistry->GetMaterialDensity(matId) / (g/cm3);

        case ColType::Temperature:
            return fRegistry->GetMaterialTemperature(matId) / kelvin;

        case ColType::Pressure:
            return fRegistry->GetMaterialPressure(matId) / atmosphere;

        case ColType::State:
            return static_cast<double>(fRegistry->GetMaterialState(matId));

        case ColType::RadiationLength:
            return fRegistry->GetMaterialRadiationLength(matId) / cm;

        case ColType::NuclearInteractionLength:
            return fRegistry->GetMaterialNuclearInteractionLength(matId) / cm;

        case ColType::Zeff:
            return fRegistry->GetMaterialZeff(matId);

        case ColType::Aeff:
            return fRegistry->GetMaterialAeff(matId);

        case ColType::ChemicalFormula:
            if (fChemicalFormulaReg) {
                return static_cast<double>(fChemicalFormulaReg->GetID(fRegistry->GetMaterialChemicalFormula(matId)));
            }
            return 0.0;

        default:
            return 0.0;
    }
}

/// @brief Extracts a string material column value from the source.
/// @param type Column type (MaterialName, ChemicalFormula).
/// @param src  UnifiedSource providing a G4Material pointer.
/// @return Extracted string, or empty string if not available.
std::string MaterialExtractor::GetString(ColType type, const UnifiedSource& src) const {
    switch (type) {
        case ColType::MaterialName: {
            auto* mat = src.GetMaterial();
            return mat ? mat->GetName() : "";
        }
        case ColType::ChemicalFormula: {
            auto* mat = src.GetMaterial();
            if (!mat) return "";
            size_t n = mat->GetNumberOfElements();
            auto* elemV = mat->GetElementVector();
            auto* fracV = mat->GetFractionVector();
            if (!elemV || !fracV) return "";
            std::string formula;
            for (size_t i = 0; i < n; ++i) {
                if (i > 0) formula += " ";
                char buf[64];
                snprintf(buf, sizeof(buf), "%s:%.2f", (*elemV)[i]->GetName().c_str(), fracV[i]);
                formula += buf;
            }
            return formula;
        }
        default:
            return "";
    }
}