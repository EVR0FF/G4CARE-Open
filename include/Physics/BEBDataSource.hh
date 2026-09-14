#ifndef BEB_DATA_SOURCE_HH
#define BEB_DATA_SOURCE_HH

//==============================================================================
// G4CARE
// @file    BEBDataSource.hh
// @brief   Cross-section data source loading tables from external files
// @details Implements ICrossSectionSource by loading per-material,
//   per-process cross-section tables (energy–xs pairs) from a data
//   file. Provides linear interpolation for energies between table
//   points. Used by UserCrossSectionModel to supply user-defined
//   cross sections overriding Geant4 defaults.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ICrossSectionSource.hh"
#include <map>
#include <string>
#include <vector>

class BEBDataSource : public ICrossSectionSource {
public:
    /// @brief Constructor
    /// @param name     Source name identifier
    /// @param dataFile Path to the cross-section data file
    BEBDataSource(const G4String& name, const G4String& dataFile);

    /// @brief Get the source name
    /// @return Source name string
    G4String GetName() const override;

    /// @brief Get cross section per atom with linear interpolation
    ///
    /// Looks up the material and process in the loaded data table,
    /// then performs linear interpolation between the nearest
    /// energy points.
    /// @param material    Pointer to the material
    /// @param processName Process name (e.g., "ioni", "excit")
    /// @param energy      Kinetic energy [MeV]
    /// @return Cross section per atom [barn], or -1 if not found
    G4double GetCrossSectionPerAtom(const G4Material* material,
                                    const G4String& processName,
                                    G4double energy) const override;

    /// @brief Priority: sources are queried in descending priority order
    /// @return Fixed priority of 10
    G4int GetPriority() const override { return 10; }

private:
    /// @brief Load cross-section data from file
    /// @param filename Path to the data file
    void LoadData(const G4String& filename);

    /// @brief Source name
    G4String fName;

    /// @brief Data storage: materialName → (processName → vector<energy, xs>)
    std::map<std::string, std::map<std::string, std::vector<std::pair<double,double>>>> fData;
};

#endif