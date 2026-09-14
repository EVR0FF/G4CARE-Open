//==============================================================================
// G4CARE
// @file    ProcessRegistry.cc
// @brief   Registry that maps Geant4 process (type, subtype) pairs to unique
//          packed 32-bit IDs and writes a process dictionary ntuple.
// @details ProcessRegistry creates a packed ID from the process type (top 8
//   bits) and subtype (lower 24 bits).  The GetPackedID method records the
//   process in an internal list, and WriteDictionary outputs a G4Analysis
//   ntuple with packed_id, process_name, process_type, and process_subtype
//   columns for later lookup.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ProcessRegistry.hh"

/// @brief Returns or creates a packed 32-bit process ID for the given
///        (type, subtype) pair and records the process name.
/// @param type    Geant4 process type.
/// @param subtype Process sub-type.
/// @param name    Process name string.
/// @return Packed process ID.
uint32_t ProcessRegistry::GetPackedID(G4ProcessType type, G4int subtype, const G4String& name) {
    auto key = std::make_pair(type, subtype);
    auto it = fKeyToID.find(key);
    if (it != fKeyToID.end()) return it->second;

    uint32_t packed = (static_cast<uint32_t>(type) << 24) | (static_cast<uint32_t>(subtype) & 0xFFFFFF);
    fKeyToID[key] = packed;
    fProcesses.push_back({type, subtype, static_cast<std::string>(name), packed});
    return packed;
}

/// @brief Writes the accumulated process list to a G4Analysis ntuple.
/// @param man        G4AnalysisManager instance.
/// @param ntupleName Name of the output dictionary ntuple.
void ProcessRegistry::WriteDictionary(G4AnalysisManager* man, const std::string& ntupleName) const {
    if (fProcesses.empty()) return;

    int ntupleId = man->CreateNtuple(ntupleName, ntupleName + " Dictionary");
    int idCol = man->CreateNtupleIColumn(ntupleId, "packed_id");
    int nameCol = man->CreateNtupleSColumn(ntupleId, "process_name");
    int typeCol = man->CreateNtupleIColumn(ntupleId, "process_type");
    int subCol = man->CreateNtupleIColumn(ntupleId, "process_subtype");
    man->FinishNtuple(ntupleId);

    for (const auto& info : fProcesses) {
        man->FillNtupleIColumn(ntupleId, idCol, static_cast<int>(info.packed_id));
        man->FillNtupleSColumn(ntupleId, nameCol, info.name);
        man->FillNtupleIColumn(ntupleId, typeCol, static_cast<int>(info.type));
        man->FillNtupleIColumn(ntupleId, subCol, info.subtype);
        man->AddNtupleRow(ntupleId);
    }
}

/// @brief Clears all registered processes.
void ProcessRegistry::Clear() {
    fKeyToID.clear();
    fProcesses.clear();
}