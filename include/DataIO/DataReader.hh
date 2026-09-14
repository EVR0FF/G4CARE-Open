//==============================================================================
//
// G4CARE
//
// @file    DataReader.hh
// @brief   Abstract interface for tabular data readers (CSV, ROOT, IAEA).
//
// @details
//   Base class providing a uniform API for loading external data files
//   (CSV, ROOT TTree, IAEA phase-space) and accessing them by row/column.
//   Implementations: RDFReader, IAEAReader.
//
//   Used by ExpressionEvaluator to register data tables accessible
//   from ExprTk expressions via user-defined aliases (DATA block).
//
//   Configuration keys read (via ConfigManager elsewhere):
//     DATA.<name>.file
//     DATA.<name>.tree
//     DATA.<name>.header
//     DATA.<name>.delimiter
//     DATA.<name>.skip_first_n
//     DATA.<name>.columns
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

#ifndef DATAREADER_HH
#define DATAREADER_HH

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstddef>

/// @brief Abstract interface for tabular data readers.
///
/// Provides row/column access to external data files (CSV, ROOT, IAEA).
/// Concrete implementations handle format-specific loading.
class DataReader {
public:
    virtual ~DataReader() = default;

    /// @brief Load data from a file.
    /// @param filename Path to the data file.
    /// @return true on success, false on failure.
    virtual bool Load(const std::string& filename) = 0;

    /// @return Total number of rows in the loaded data.
    virtual size_t GetNumberOfRows() const noexcept = 0;

    /// @return Number of columns available.
    virtual size_t GetNumberOfColumns() const noexcept = 0;

    /// @return Ordered list of column names.
    virtual std::vector<std::string> GetColumnNames() const = 0;

    /// @brief Get a single cell value.
    /// @param row Row index (0-based).
    /// @param col Column index (0-based).
    /// @return Cell value or std::nullopt if out of bounds.
    virtual std::optional<double> GetCell(size_t row, size_t col) const = 0;

    /// @brief Get an entire row as a vector.
    /// @param row Row index (0-based).
    /// @return Row values or std::nullopt if out of bounds.
    virtual std::optional<std::vector<double>> GetRow(size_t row) const = 0;

    /// @return true if the data has at least 2 columns (tabular).
    virtual bool IsTable() const noexcept final { return GetNumberOfColumns() >= 2; }

    /// @brief Sample a random X value from the distribution (optional feature).
    /// @return Sampled value or std::nullopt if not supported.
    virtual std::optional<double> SampleX() const { return std::nullopt; }

    /// @brief Interpolate Y for a given X (optional feature).
    /// @param x The X coordinate.
    /// @return Interpolated Y or std::nullopt if not supported.
    virtual std::optional<double> InterpolateY(double /*x*/) const { return std::nullopt; }
};

#endif
