//==============================================================================
// G4CARE
// @file    IStreamWriter.hh
// @brief   Abstract interface for columnar output streams (RNTuple, future CSV).
// @details A stream writer owns one output ntuple (one "stream") and its
//   column schema.  It is format-agnostic: the same Row/ColumnSpec is used
//   by all backends so the analysis core never depends on a concrete format.
//   The writer does NOT own the file: the caller opens a shared file and the
//   writer appends its ntuple into it.
//
//   Configuration keys read: none (interface definition).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef ISTREAM_WRITER_HH
#define ISTREAM_WRITER_HH

#include <map>
#include <string>
#include <vector>

class TFile;

/// Column data kind.
enum class ColumnKind { Double, String };

/// One column descriptor.
struct ColumnSpec {
    std::string name;
    ColumnKind kind = ColumnKind::Double;
};

/// One output row: numeric and string values keyed by column name.
struct Row {
    std::map<std::string, double> numbers;
    std::map<std::string, std::string> strings;
};

/// Abstract output stream backend.
class IStreamWriter {
public:
    virtual ~IStreamWriter() = default;

    /// Create the ntuple/schema inside the already-open @p file.
    virtual void Open(TFile* file, const std::string& streamName,
                      const std::vector<ColumnSpec>& columns) = 0;

    /// Append one row.  Missing values default to 0.0 / empty string.
    virtual void WriteRow(const Row& row) = 0;

    /// Flush the current cluster (memory control during long runs).
    virtual void Commit() = 0;

    /// Flush and release the ntuple (does NOT close the shared file).
    virtual void Close() = 0;
};

#endif // ISTREAM_WRITER_HH
