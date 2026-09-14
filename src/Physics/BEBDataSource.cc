//==============================================================================
// G4CARE
// @file    BEBDataSource.cc
// @brief   Implementation of BEB cross-section data source
// @details Implements linear interpolation lookup in per-material,
//   per-process cross-section tables loaded from external data files.
//   Uses std::lower_bound for binary search on energy values.
//   Returns the nearest table value at boundaries.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "BEBDataSource.hh"
#include "G4Material.hh"
#include <fstream>
#include <sstream>
#include <algorithm>

/// @brief Constructor
/// @param name     Source name identifier
/// @param dataFile Path to the cross-section data file
BEBDataSource::BEBDataSource(const G4String& name, const G4String& dataFile)
    : fName(name) {
    LoadData(dataFile);
}

/// @brief Get the source name
/// @return Source name string
G4String BEBDataSource::GetName() const { return fName; }

/// @brief Get cross section per atom with linear interpolation
///
/// Searches the loaded data tables for the given material and process,
/// then performs linear interpolation between the nearest energy values.
/// @param material    Pointer to the material
/// @param processName Process name (e.g., "ioni", "excit")
/// @param energy      Kinetic energy [MeV]
/// @return Cross section per atom [barn], or -1 if not found
G4double BEBDataSource::GetCrossSectionPerAtom(const G4Material* material,
                                               const G4String& processName,
                                               G4double energy) const {
    auto matIt = fData.find(material->GetName());
    if (matIt == fData.end()) return -1.0;
    auto procIt = matIt->second.find(processName);
    if (procIt == matIt->second.end()) return -1.0;
    const auto& table = procIt->second;
    if (table.empty()) return -1.0;

    // Linear interpolation using binary search
    auto upper = std::lower_bound(table.begin(), table.end(), energy,
        [](const auto& p, double e) { return p.first < e; });
    if (upper == table.begin()) return table.front().second;
    if (upper == table.end()) return table.back().second;
    auto lower = upper - 1;
    double t = (energy - lower->first) / (upper->first - lower->first);
    return lower->second + t * (upper->second - lower->second);
}

/// @brief Load cross-section data from file
///
/// Parses a data file with format: material process energy xs.
/// Currently a stub; implement parsing for the actual file format.
/// @param filename Path to the data file
void BEBDataSource::LoadData(const G4String& filename) {
    std::ifstream file(filename);
    // TODO: implement parsing of material process energy xs format
}