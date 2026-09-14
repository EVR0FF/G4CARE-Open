//==============================================================================
//
// G4CARE
//
// @file    MoleculeIO.hh
// @brief   Binary I/O class for chemistry molecule state vectors.
//
// @details
//   Static utility class for writing/reading molecule state checkpoints
//   used in SBS (step-by-step) chemistry simulations.  The file format
//   includes a header with unit metadata (time, length, energy) and
//   the number of species in ChemSpeciesRegistry.
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

#ifndef MOLECULE_IO_HH
#define MOLECULE_IO_HH

#include "MoleculeRecord.hh"
#include <vector>
#include <string>
#include <cstdint>

/// @brief Binary file header with unit metadata written after magic/version.
#pragma pack(push, 1)
struct MoleculeFileHeader {
    char     timeUnit[8];     ///< Time unit string (e.g. "ps").
    char     lengthUnit[8];   ///< Length unit string (e.g. "mm").
    char     energyUnit[8];   ///< Energy unit string (e.g. "MeV").
    uint32_t numSpecies;      ///< Number of species in ChemSpeciesRegistry.
    uint32_t reserved;        ///< Padding / future extension.
};
#pragma pack(pop)

static_assert(sizeof(MoleculeFileHeader) == 32,
              "MoleculeFileHeader size mismatch – check packing");

/// @brief Binary I/O for chemistry molecule state (SBS checkpoint files).
class MoleculeIO {
public:
    /// @brief Write molecule records to a binary checkpoint file.
    /// @param filename   Output file path.
    /// @param molecules  Vector of MoleculeRecord to write.
    /// @param numSpecies Number of species in the registry.
    /// @param timeUnit   Time unit label (default "ps").
    /// @param lengthUnit Length unit label (default "mm").
    /// @param energyUnit Energy unit label (default "MeV").
    /// @return true on success.
    static bool WriteBinary(const std::string& filename,
                            const std::vector<MoleculeRecord>& molecules,
                            uint32_t numSpecies = 0,
                            const char* timeUnit = "ps",
                            const char* lengthUnit = "mm",
                            const char* energyUnit = "MeV");

    /// @brief Read molecule records from a binary checkpoint file.
    /// @param filename        Input file path.
    /// @param versionMismatch If non-null, set to true when file version differs.
    /// @param headerOut       If non-null, filled with file header metadata.
    /// @return Vector of MoleculeRecord (empty on failure).
    static std::vector<MoleculeRecord> ReadBinary(const std::string& filename,
                                                   bool* versionMismatch = nullptr,
                                                   MoleculeFileHeader* headerOut = nullptr);
};

#endif
