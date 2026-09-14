//==============================================================================
//
// G4CARE
//
// @file    MoleculeRecord.hh
// @brief   Binary record structure for chemistry molecule I/O.
//
// @details
//   Defines the MoleculeRecord struct used for binary serialization
//   of molecule state in SBS (step-by-step) chemistry checkpoints.
//   Uses `#pragma pack(push, 1)` to ensure portable binary layout.
//
//   File format magic and version constants are defined for validation
//   during read operations.  v2 uses int32 speciesIndex instead of
//   char[16] species name, reducing record size from 52 to 40 bytes.
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

#ifndef MOLECULE_RECORD_HH
#define MOLECULE_RECORD_HH

#include <cstdint>

/// @brief Magic number for molecule file identification ("GDNA").
constexpr uint32_t MOLECULE_FILE_MAGIC   = 0x47444E41;
/// @brief File format version (v2: compact speciesIndex, 40 bytes/record).
constexpr uint32_t MOLECULE_FILE_VERSION = 2;

#pragma pack(push, 1)  // disable alignment — critical for binary portability
/// @brief Binary record for a single molecule in a chemistry checkpoint.
struct MoleculeRecord {
    int32_t  speciesIndex;  ///< Species ID in ChemSpeciesRegistry.
    double   x, y, z;       ///< Position (mm).
    double   time;          ///< Global time (ps).
    int32_t  trackID;       ///< Geant4 track identifier.
};
#pragma pack(pop)

static_assert(sizeof(MoleculeRecord) == 4 + 3*8 + 8 + 4,
              "MoleculeRecord size mismatch – check packing");

#endif
