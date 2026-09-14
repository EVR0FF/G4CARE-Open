//==============================================================================
// G4CARE
// @file    ProcessRegistry.hh
// @brief   Maps Geant4 process (type, subtype) pairs to unique packed 32-bit
//          IDs and writes a process dictionary ntuple.
// @details Packed ID = (type << 24) | (subtype & 0xFFFFFF).  GetPackedID
//   records the process name when first encountered; WriteDictionary outputs
//   a G4Analysis ntuple with packed_id, process_name, process_type, and
//   process_subtype.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef PROCESS_REGISTRY_HH
#define PROCESS_REGISTRY_HH

#include "G4VProcess.hh"
#include "G4String.hh"
#include "G4AnalysisManager.hh"
#include <map>
#include <vector>
#include <cstdint>

/// @brief Registers process (type,subtype) pairs and writes a dictionary ntuple.
class ProcessRegistry {
public:
    ProcessRegistry() = default;

    /// Returns or creates a packed ID for a (type, subtype) pair and records the name.
    uint32_t GetPackedID(G4ProcessType type, G4int subtype, const G4String& name);

    /// Writes the accumulated process list to a G4Analysis ntuple.
    void WriteDictionary(G4AnalysisManager* man, const std::string& ntupleName) const;

    /// Clears all registered processes.
    void Clear();

private:
    struct ProcessInfo {
        G4ProcessType type;
        G4int subtype;
        std::string name;
        uint32_t packed_id;
    };
    std::map<std::pair<G4ProcessType, G4int>, uint32_t> fKeyToID;
    std::vector<ProcessInfo> fProcesses;
};

#endif