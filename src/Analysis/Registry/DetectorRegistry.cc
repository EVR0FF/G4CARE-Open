//==============================================================================
// G4CARE
// @file    DetectorRegistry.cc
// @brief   Registry that maps G4VSensitiveDetector pointers and detector names
//          to unique integer IDs and stores per-detector properties.
// @details DetectorRegistry maintains a vector of (SD*, DetectorProperties)
//   pairs and a name-to-ID lookup.  It is used by DigitizerModule and
//   DictionaryWriter to produce the detector_dict ntuple.
//
//   Configuration keys read: none (registry data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "DetectorRegistry.hh"

/// @brief Registers a sensitive detector with its name and properties.
/// @param sd    Sensitive detector pointer.
/// @param name  Detector name.
/// @param props Detector properties (dead time, resolution, etc.).
/// @return Assigned detector ID, or -1 if sd is null.
int DetectorRegistry::RegisterDetector(G4VSensitiveDetector* sd, const std::string& name, const DetectorProperties& props) {
    if (!sd) return -1;
    auto it = fNameToID.find(name);
    if (it != fNameToID.end()) return it->second;
    int id = static_cast<int>(fDetectors.size());
    fNameToID[name] = id;
    fDetectorToID[sd] = id;
    fDetectors.emplace_back(sd, props);
    return id;
}

/// @brief Returns the detector ID for a given sensitive detector pointer.
/// @param sd Sensitive detector (may be nullptr).
/// @return Detector ID, or -1 if not found.
int DetectorRegistry::GetDetectorID(G4VSensitiveDetector* sd) const {
    if (!sd) return -1;
    auto it = fDetectorToID.find(sd);
    if (it != fDetectorToID.end()) return it->second;
    auto nameIt = fNameToID.find(sd->GetName());
    if (nameIt != fNameToID.end()) return nameIt->second;
    return -1;
}

/// @brief Returns the detector ID for a given detector name.
/// @param name Detector name.
/// @return Detector ID, or -1 if not found.
int DetectorRegistry::GetDetectorID(const std::string& name) const {
    auto it = fNameToID.find(name);
    return (it != fNameToID.end()) ? it->second : -1;
}

/// @brief Returns the properties for a given detector ID.
/// @param id Detector ID.
/// @return Reference to DetectorProperties; empty properties if ID is invalid.
const DetectorProperties& DetectorRegistry::GetProperties(int id) const {
    static DetectorProperties empty;
    if (id >= 0 && id < static_cast<int>(fDetectors.size())) {
        return fDetectors[id].second;
    }
    return empty;
}

/// @brief Clears all registered detectors.
void DetectorRegistry::Clear() {
    fDetectorToID.clear();
    fNameToID.clear();
    fDetectors.clear();
}
