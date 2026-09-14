//==============================================================================
//
// G4CARE
//
// @file    IAEAReader.hh
// @brief   IAEA phase-space file reader for particle source data.
//
// @details
//   Reads IAEA-compliant binary phase-space files containing particle
//   records (type, energy, position, direction, weight).  Implements
//   the DataReader interface for use as a data source in expressions.
//
//   Thread-safe: each Geant4 worker thread maintains its own file
//   handle and read buffer via G4Cache<ThreadLocalState>.
//
//   Configuration keys read (via ConfigManager elsewhere):
//     DATA.<name>.file   (IAEA header file, e.g. *.IAEAheader)
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

#ifndef IAEREADER_HH
#define IAEREADER_HH

#include "DataReader.hh"
#include "G4Cache.hh"
#include <string>
#include <vector>
#include <cstdint>

/// @brief Reader for IAEA phase-space files.
///
/// Provides particle-by-particle access to IAEA binary phase-space data.
/// Each thread opens its own file handle for thread-safe reading.
class IAEAReader : public DataReader {
public:
    IAEAReader();
    ~IAEAReader() override;

    /// @brief Load IAEA phase-space data.
    /// @param filename Path to the .IAEAheader file.
    /// @return true on success.
    bool Load(const std::string& filename) override;

    /// @return Total number of particles (rows).
    size_t GetNumberOfRows() const noexcept override { return fNumParticles; }
    size_t GetNumberOfColumns() const noexcept override;
    std::vector<std::string> GetColumnNames() const override;

    /// @brief Get a single particle record field.
    /// @param row Particle index.
    /// @param col Field index.
    /// @return Field value or std::nullopt.
    std::optional<double> GetCell(size_t row, size_t col) const override;

    /// @brief Get all fields for a particle.
    /// @param row Particle index.
    /// @return All field values or std::nullopt.
    std::optional<std::vector<double>> GetRow(size_t row) const override;

    /// @brief Set verbosity level for diagnostics.
    void SetVerbose(int v) { fVerbose = v; }

private:
    /// @brief Per-thread state: file handle and read buffer.
    struct ThreadLocalState {
        FILE* dataFile = nullptr;
        size_t currentRow = static_cast<size_t>(-1);
        std::vector<char> currentBuffer;
        std::vector<double> currentValues;

        ~ThreadLocalState() {
            if (dataFile) fclose(dataFile);
        }
    };

    /// @brief Parse IAEA header file to extract metadata.
    bool ParseHeader(const std::string& headerFilename);
    /// @brief Read a single particle record into thread-local buffer.
    bool ReadRecord(size_t row) const;

    std::string fDataFilename;
    size_t      fNumParticles = 0;
    size_t      fRecordLength = 0;
    int         fRecordContents[9] = {0};
    float       fRecordConstant[7] = {0.0f};
    int         fNumExtraFloats = 0;
    int         fNumExtraLongs  = 0;
    int         fByteOrder = 1234;
    int         fVerbose = 0;

    mutable G4Cache<ThreadLocalState*> fStateCache;
};

#endif
