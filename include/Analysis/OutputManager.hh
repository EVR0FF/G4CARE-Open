//==============================================================================
// G4CARE
// @file    OutputManager.hh
// @brief   Columnar output manager: drives RNTuple streams defined in OUTPUT.
// @details Replaces the G4AnalysisManager NTuple path for the main data
//   streams.  Streams are configured in OUTPUT.streams[] with an arbitrary
//   name, a level (event|track|step|digit|chemistry), columns and an optional
//   filter expression.  Filters may reference SCRIPT runtime variables (in
//   addition to the standard filter variables and SCRIPT constants).
//
//   Configuration keys read: OUTPUT.file, OUTPUT.format, OUTPUT.streams[].
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef OUTPUT_MANAGER_HH
#define OUTPUT_MANAGER_HH

#include "ColumnTypes.hh"
#include "UnifiedSource.hh"
#include "DataIO/RNTupleStreamWriter.hh"
#include "ExpressionEvaluator.hh"
#include <TFile.h>
#include <memory>
#include <string>
#include <vector>

class ConfigManager;
class DataExtractor;

/// Data level of a stream — which Fill call site feeds it.
enum class StreamLevel { Event, Track, Step, Hit, Digit, Chemistry, Unknown };

/// Map a legacy Fill() tree name to its level.
StreamLevel LevelFromTreeName(const std::string& name);
/// Parse a level string from OUTPUT.streams[].level.
StreamLevel ParseLevel(const std::string& s);

/// Manages the RNTuple output streams.
class OutputManager {
public:
    ~OutputManager();
    /// Parse OUTPUT config and compile column specs + filters.
    void LoadFromConfig(ConfigManager* cfg, ExpressionEvaluator* eval);

    /// Open one per-thread RNTuple file per stream.
    void BeginOfRun(const std::string& outFile, int threadId);

    /// Close all writers.
    void EndOfRun();

    /// Write one row to every stream of the given level whose filter passes.
    void Fill(StreamLevel level, const UnifiedSource& src, DataExtractor* extractor,
              ExpressionEvaluator* eval, int eventId, int volId, int matId);

    bool IsEnabled() const { return !fStreams.empty(); }
    const std::string& GetOutFile() const { return fOutFile; }

    /// Merge per-thread RNTuple files into one output file.
    static void MergeFiles(const std::string& outFile, const std::vector<std::string>& perThreadFiles);

private:
    struct Stream {
        std::string name;
        StreamLevel level = StreamLevel::Unknown;
        std::vector<std::string> colNames;   // branch names (with unit suffix)
        std::vector<ColType> colTypes;
        std::vector<bool> colIsString;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> filter;
        std::vector<std::string> filterVolumes;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> filterCondition;
        std::vector<FilterVar> filterConditionVarIndices;
        std::vector<FilterVar> filterVarIndices;
        std::unique_ptr<RNTupleStreamWriter> writer;
        std::string fileName;
    };

    std::unique_ptr<TFile> fFile;
    std::vector<Stream> fStreams;
    std::string fOutFile = "output.root";
    std::string fCompressionAlgorithm = "zstd";
    int fCompressionLevel = 5;
    int fClusterSizeMb = 128;
    int fPageSizeKb = 1024;
};

#endif // OUTPUT_MANAGER_HH
