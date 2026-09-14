//==============================================================================
//
// G4CARE
//
// @file    ProcessUtils.hh
// @brief   Bit-packing utilities for Geant4 process type/subtype IDs.
//
// @details
//   Pure header-only utility functions with no internal state — safe
//   for multi-threaded lock-free architectures.  Used uniformly by
//   ProcessExtractor, DataExtractor, and TreeManager.
//
//   Provides:
//   - PackProcessID() — encode (G4ProcessType, subtype) into uint32_t.
//   - UnpackProcessType() / UnpackProcessSubType() — decode.
//
//   Encoding format: [type:8 bits | subtype:24 bits].
//
//   Configuration keys read: none.
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#ifndef PROCESS_UTILS_HH
#define PROCESS_UTILS_HH

#include "G4VProcess.hh"
#include <cstdint>

/// @brief Pure header-only utilities for packing/unpacking process IDs.
///
/// All functions are inline and stateless — safe for multi-threaded
/// lock-free (TLS-compatible) use.
namespace ProcessUtils {

/// @brief Pack (G4ProcessType, subtype) into a 32-bit identifier.
///
/// Format: [type:8 bits | subtype:24 bits].
/// Used uniformly by ProcessExtractor, DataExtractor, and TreeManager.
/// @param proc Geant4 process reference.
/// @return Packed 32-bit process ID.
inline uint32_t PackProcessID(const G4VProcess& proc) {
    uint32_t type = static_cast<uint32_t>(proc.GetProcessType());
    uint32_t subtype = static_cast<uint32_t>(proc.GetProcessSubType());
    return (type << 24) | (subtype & 0xFFFFFFu);
}

/// @brief Pack process ID from explicit type and subtype values.
/// @param type    G4ProcessType enum value.
/// @param subtype Process subtype integer.
/// @return Packed 32-bit process ID.
inline uint32_t PackProcessID(G4ProcessType type, G4int subtype) {
    return (static_cast<uint32_t>(type) << 24) |
           (static_cast<uint32_t>(subtype) & 0xFFFFFFu);
}

/// @brief Extract the process type from a packed ID.
/// @param packed 32-bit packed process ID.
/// @return G4ProcessType enum value.
inline G4ProcessType UnpackProcessType(uint32_t packed) {
    return static_cast<G4ProcessType>(packed >> 24);
}

/// @brief Extract the process subtype from a packed ID.
/// @param packed 32-bit packed process ID.
/// @return Subtype integer (lower 24 bits).
inline G4int UnpackProcessSubType(uint32_t packed) {
    return static_cast<G4int>(packed & 0xFFFFFFu);
}

} // namespace ProcessUtils

#endif // PROCESS_UTILS_HH
