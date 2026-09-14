//==============================================================================
//
// G4CARE
//
// @file    RDFReader.hh
// @brief   Tabular data reader using ROOT RDataFrame (CSV and ROOT files).
//
// @details
//   Concrete implementation of DataReader that loads CSV and ROOT TTree
//   files into in-memory column-major storage for thread-safe access.
//   Uses ROOT::RDataFrame for CSV parsing and TTree reading.
//
//   Supports:
//   - CSV: custom delimiter, optional header row, skip-first-N-lines.
//   - ROOT: TTree with numeric branches (double, float, int).
//
//   All data is copied to in-memory vectors to avoid threading issues
//   with RDataFrame's lazy evaluation in multi-threaded Geant4.
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

#ifndef RDFREADER_HH
#define RDFREADER_HH

#include "DataReader.hh"
#include <TFile.h>
#include <TTreeReader.h>
#include <TTreeReaderValue.h>
#include <memory>
#include <vector>
#include <string>

/// @brief Data reader using ROOT RDataFrame for CSV and ROOT TTree input.
///
/// Loads data into in-memory column vectors for thread-safe access.
class RDFReader : public DataReader {
public:
    RDFReader();
    ~RDFReader() override;

    RDFReader(const RDFReader&) = delete;
    RDFReader& operator=(const RDFReader&) = delete;

    /// @brief Load a ROOT TTree file.
    /// @param filenames List of file paths (only first used).
    /// @param treename Name of the TTree.
    /// @return true on success.
    bool LoadROOTFile(const std::vector<std::string>& filenames, const std::string& treename);

    /// @brief Load a CSV file via RDataFrame.
    /// @param filename CSV file path.
    /// @param header Whether the first row contains column names.
    /// @param delimiter Column separator character.
    /// @param skip Number of lines to skip before reading.
    /// @return true on success.
    bool LoadCSVFile(const std::string& filename, bool header, char delimiter, int skip);

    bool Load(const std::string& /*filename*/) override { return false; }

    /// @return Number of rows (entries).
    size_t GetNumRows() const { return fNumRows; }
    size_t GetNumberOfRows() const noexcept override { return fNumRows; }
    size_t GetNumberOfColumns() const noexcept override { return fColumns.size(); }
    std::vector<std::string> GetColumnNames() const override { return fColumns; }

    /// @brief Get a single cell value.
    /// @param row Row index (0-based).
    /// @param col Column index (0-based).
    /// @return Cell value or std::nullopt if out of bounds.
    std::optional<double> GetCell(size_t row, size_t col) const override;

    /// @brief Get an entire row.
    /// @param row Row index (0-based).
    /// @return Row values or std::nullopt if out of bounds.
    std::optional<std::vector<double>> GetRow(size_t row) const override;

private:
    enum class Mode { kNone, kROOT, kCSV };
    Mode fMode = Mode::kNone;

    std::vector<std::string> fColumns;
    size_t fNumRows = 0;

    // ROOT TTree direct read resources (legacy, unused for RDataFrame path)
    std::unique_ptr<TFile> fROOTFile;
    std::unique_ptr<TTreeReader> fROOTReader;
    std::vector<std::unique_ptr<TTreeReaderValue<double>>> fROOTValueReaders;

    // In-memory column-major storage for thread-safe access
    std::vector<std::vector<double>> fROOTData;
    std::vector<std::vector<double>> fCSVData;
    mutable size_t fCurrentRow = std::numeric_limits<size_t>::max();
    
};

#endif
