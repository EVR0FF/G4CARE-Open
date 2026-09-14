//==============================================================================
// G4CARE
// @file    MaterialExtractor.hh
// @brief   Extracts material-related columns (density, temperature, pressure,
//          state, radiation/nuclear lengths, Zeff/Aeff, formula) from the
//          VolumeMaterialRegistry.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef MATERIAL_EXTRACTOR_HH
#define MATERIAL_EXTRACTOR_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include "VolumeMaterialRegistry.hh"
#include "TypedRegistry.hh"

/// @brief Extracts material properties from the registry.
class MaterialExtractor {
public:
    MaterialExtractor(VolumeMaterialRegistry* registry,
                      TypedRegistry<std::string>* materialNameReg = nullptr,
                      TypedRegistry<std::string>* chemicalFormulaReg = nullptr)
        : fRegistry(registry), fMaterialNameReg(materialNameReg), fChemicalFormulaReg(chemicalFormulaReg) {}

    [[nodiscard]] double Get(ColType type, const UnifiedSource& src, int matId) const;
    [[nodiscard]] std::string GetString(ColType type, const UnifiedSource& src) const;

private:
    VolumeMaterialRegistry* fRegistry;
    TypedRegistry<std::string>* fMaterialNameReg;
    TypedRegistry<std::string>* fChemicalFormulaReg;
};

#endif // MATERIAL_EXTRACTOR_HH