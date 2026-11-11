//==============================================================================
// G4CARE
// @file    RNTupleStreamWriter.hh
// @brief   IStreamWriter implementation backed by ROOT RNTuple (columnar).
// @details Writes one RNTuple into an already-open TFile.  Double columns use
//   RField<double>, string columns use RField<std::string> (native columnar
//   strings).  The writer does not own the TFile.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef RNTUPLE_STREAM_WRITER_HH
#define RNTUPLE_STREAM_WRITER_HH

#include "IStreamWriter.hh"
#include <map>
#include <memory>
#include <string>

namespace ROOT {
class RNTupleModel;
class RNTupleWriter;
}

/// ROOT RNTuple output backend.
class RNTupleStreamWriter : public IStreamWriter {
public:
    RNTupleStreamWriter();
    ~RNTupleStreamWriter() override;

    void Open(TFile* file, const std::string& streamName,
              const std::vector<ColumnSpec>& columns) override;
    void WriteRow(const Row& row) override;
    void Commit() override;
    void Close() override;

    /// Configure compression / cluster / page options applied in Open().
    void SetWriteOptions(const std::string& algorithm, int level,
                         int clusterMb, int pageKb);

private:
    std::unique_ptr<ROOT::RNTupleModel> fModel;
    std::unique_ptr<ROOT::RNTupleWriter> fWriter;
    std::map<std::string, std::shared_ptr<double>> fDoubleFields;
    std::map<std::string, std::shared_ptr<std::string>> fStringFields;
    std::string fCompressionAlgorithm = "zstd";
    int fCompressionLevel = 5;
    int fClusterSizeMb = 128;
    int fPageSizeKb = 1024;
};

#endif // RNTUPLE_STREAM_WRITER_HH
