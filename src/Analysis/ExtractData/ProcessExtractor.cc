//==============================================================================
// G4CARE
// @file    ProcessExtractor.cc
// @brief   Extracts Geant4 process information (sub-type, type, packed ID,
//          name) from the step-defining or creator process.
// @details ProcessExtractor resolves the responsible G4VProcess depending on
//   the source type (Hit → post-step defining process; Secondary → creator
//   process).  Numeric columns return process sub-type, process type, or a
//   packed process ID via ProcessUtils.  The string overload returns the
//   process name.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "ProcessExtractor.hh"
#include "G4VProcess.hh"
#include "ProcessUtils.hh"

/// @brief Extracts a numeric process column value from the given source.
/// @param type Column type (ProcessSubType, ProcessType, ProcessID,
///        ProcessName, CreatorProcessSubType, CreatorProcessName,
///        CreatorProcessID, StepLimitingProcess).
/// @param src  UnifiedSource providing step or track data.
/// @return Extracted value, or 0.0 if process is not resolved.
double ProcessExtractor::Get(ColType type, const UnifiedSource& src) const {
    const G4VProcess* proc = nullptr;

    switch (type) {
        case ColType::ProcessSubType:
        case ColType::ProcessType:
        case ColType::ProcessID:
        case ColType::ProcessName:
            if (src.GetType() == UnifiedSource::Type::Hit) {
                proc = src.GetPostStepPoint()->GetProcessDefinedStep();
            } else {
                proc = src.GetCreatorProcess();
            }
            break;

        case ColType::CreatorProcessSubType:
        case ColType::CreatorProcessName:
        case ColType::CreatorProcessID:
            if (src.GetType() == UnifiedSource::Type::Secondary) {
                proc = src.GetCreatorProcess();
            }
            break;

        case ColType::StepLimitingProcess:
            if (src.GetType() == UnifiedSource::Type::Hit) {
                proc = src.GetPostStepPoint()->GetProcessDefinedStep();
            }
            break;

        default:
            return 0.0;
    }

    if (!proc) return 0.0;

    switch (type) {
        case ColType::ProcessSubType:
        case ColType::CreatorProcessSubType:
        case ColType::StepLimitingProcess:
            return static_cast<double>(proc->GetProcessSubType());

        case ColType::ProcessType:
            return static_cast<double>(proc->GetProcessType());

        case ColType::ProcessID:
        case ColType::CreatorProcessID:
            return static_cast<double>(ProcessUtils::PackProcessID(*proc));

        case ColType::ProcessName:
        case ColType::CreatorProcessName:
            // Return process sub-type as identifier for process name
            // (full string registry would require TypedRegistry<std::string>)
            return static_cast<double>(proc->GetProcessSubType());
    }
    return 0.0;
}

/// @brief Extracts a string process column value (process name) from the source.
/// @param type Column type (ProcessName, CreatorProcessName, StepLimitingProcess).
/// @param src  UnifiedSource providing step or track data.
/// @return Process name, or empty string if not resolved.
std::string ProcessExtractor::GetString(ColType type, const UnifiedSource& src) const {
    const G4VProcess* proc = nullptr;

    switch (type) {
        case ColType::ProcessName:
            if (src.GetType() == UnifiedSource::Type::Hit) {
                proc = src.GetPostStepPoint()->GetProcessDefinedStep();
            } else {
                proc = src.GetCreatorProcess();
            }
            break;

        case ColType::CreatorProcessName:
            if (src.GetType() == UnifiedSource::Type::Secondary) {
                proc = src.GetCreatorProcess();
            }
            break;

        case ColType::StepLimitingProcess:
            if (src.GetType() == UnifiedSource::Type::Hit) {
                proc = src.GetPostStepPoint()->GetProcessDefinedStep();
            }
            break;

        default:
            return "";
    }

    if (!proc) return "";
    return proc->GetProcessName();
}
