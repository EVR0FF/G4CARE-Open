#ifndef I_CROSS_SECTION_SOURCE_HH
#define I_CROSS_SECTION_SOURCE_HH

//==============================================================================
// G4CARE
// @file    ICrossSectionSource.hh
// @brief   Abstract interface for custom cross-section data sources
// @details Defines the interface for providing per-atom cross sections
//   for specific materials and processes. Sources are prioritized:
//   higher priority values are queried first by UserCrossSectionModel.
//   Returns -1 if the source cannot provide a cross section for the
//   given combination, allowing fallback to the next source.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4String.hh"
#include "G4Types.hh"

class G4Material;

class ICrossSectionSource {
public:
    /// @brief Virtual destructor
    virtual ~ICrossSectionSource() = default;

    /// @brief Source name for identification in configuration
    /// @return Unique source name
    virtual G4String GetName() const = 0;

    /// @brief Return the per-atom cross section for a given material, process, and energy
    /// @param material    Pointer to the material
    /// @param processName Process name: "ioni", "excit", "elastic", "ionis"
    /// @param energy      Kinetic energy of the particle [MeV]
    /// @return Cross section per atom [barn], or -1 if not applicable
    virtual G4double GetCrossSectionPerAtom(const G4Material* material,
                                            const G4String& processName,
                                            G4double energy) const = 0;

    /// @brief Source priority: higher values are queried first
    /// @return Priority value
    virtual G4int GetPriority() const = 0;
};

#endif