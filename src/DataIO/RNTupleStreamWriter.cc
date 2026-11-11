//==============================================================================
// G4CARE
// @file    RNTupleStreamWriter.cc
// @brief   ROOT RNTuple output backend implementation.
//==============================================================================

#include "RNTupleStreamWriter.hh"
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleWriter.hxx>
#include <Compression.h>
#include <TFile.h>
#include <iostream>

namespace {
ROOT::RCompressionSetting::EAlgorithm::EValues CompressAlgo(const std::string& s) {
    using A = ROOT::RCompressionSetting::EAlgorithm::EValues;
    if (s == "zlib") return A::kZLIB;
    if (s == "lzma") return A::kLZMA;
    if (s == "lz4")  return A::kLZ4;
    return A::kZSTD;
}
}

RNTupleStreamWriter::RNTupleStreamWriter() = default;

RNTupleStreamWriter::~RNTupleStreamWriter() {
    Close();
}

void RNTupleStreamWriter::Open(TFile* file, const std::string& streamName,
                               const std::vector<ColumnSpec>& columns) {
    Close();
    if (!file || file->IsZombie()) {
        std::cerr << "RNTupleStreamWriter: invalid file for stream \"" << streamName << "\"" << std::endl;
        return;
    }

    fModel = ROOT::RNTupleModel::Create();
    for (const auto& col : columns) {
        if (col.kind == ColumnKind::String)
            fStringFields[col.name] = fModel->MakeField<std::string>(col.name);
        else
            fDoubleFields[col.name] = fModel->MakeField<double>(col.name);
    }

    ROOT::RNTupleWriteOptions options;
    options.SetCompression(CompressAlgo(fCompressionAlgorithm), fCompressionLevel);
    options.SetApproxZippedClusterSize(static_cast<std::size_t>(fClusterSizeMb) * 1024u * 1024u);
    options.SetMaxUnzippedPageSize(static_cast<std::size_t>(fPageSizeKb) * 1024u);
    fWriter = ROOT::RNTupleWriter::Append(std::move(fModel), streamName, *file, options);
    if (!fWriter) {
        std::cerr << "RNTupleStreamWriter: failed to create RNTuple \"" << streamName << "\"" << std::endl;
    }
}

void RNTupleStreamWriter::WriteRow(const Row& row) {
    if (!fWriter) return;
    for (const auto& [name, field] : fDoubleFields) {
        auto it = row.numbers.find(name);
        *field = (it != row.numbers.end()) ? it->second : 0.0;
    }
    for (const auto& [name, field] : fStringFields) {
        auto it = row.strings.find(name);
        *field = (it != row.strings.end()) ? it->second : std::string();
    }
    fWriter->Fill();
}

void RNTupleStreamWriter::Commit() {
    if (fWriter) fWriter->CommitCluster();
}

void RNTupleStreamWriter::SetWriteOptions(const std::string& algorithm, int level,
                                          int clusterMb, int pageKb) {
    fCompressionAlgorithm = algorithm;
    fCompressionLevel = level;
    fClusterSizeMb = clusterMb;
    fPageSizeKb = pageKb;
}

void RNTupleStreamWriter::Close() {
    fWriter.reset();
    fModel.reset();
    fDoubleFields.clear();
    fStringFields.clear();
}
