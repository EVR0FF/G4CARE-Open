//==============================================================================
// G4CARE
// @file    OutputManager.cc
// @brief   Columnar output manager implementation (RNTuple streams).
//==============================================================================

#include "OutputManager.hh"
#include "ConfigManager.hh"
#include "DataExtractor.hh"
#include "TreeManager.hh"
#include "FilterVarRegistry.hh"
#include "BeamAnalysis.hh"
#include <TFileMerger.h>
#include <TFile.h>
#include <iostream>

StreamLevel LevelFromTreeName(const std::string& name) {
    if (name == "primary") return StreamLevel::Event;
    if (name == "secondary" || name == "ion_production") return StreamLevel::Track;
    if (name == "g4care") return StreamLevel::Step;
    if (name == "digits") return StreamLevel::Digit;
    if (name == "hits") return StreamLevel::Hit;
    if (name == "chem_tracks") return StreamLevel::Chemistry;
    return StreamLevel::Unknown;
}

StreamLevel ParseLevel(const std::string& s) {
    if (s == "event") return StreamLevel::Event;
    if (s == "track") return StreamLevel::Track;
    if (s == "step") return StreamLevel::Step;
    if (s == "hit") return StreamLevel::Hit;
    if (s == "digit") return StreamLevel::Digit;
    if (s == "chemistry") return StreamLevel::Chemistry;
    return StreamLevel::Unknown;
}

void OutputManager::LoadFromConfig(ConfigManager* cfg, ExpressionEvaluator* eval) {
    fStreams.clear();
    auto spec = cfg->GetOutputSpec();
    fOutFile = spec.file;
    fCompressionAlgorithm = spec.compressionAlgorithm;
    fCompressionLevel = spec.compressionLevel;
    fClusterSizeMb = spec.clusterSizeMb;
    fPageSizeKb = spec.pageSizeKb;

    // SCRIPT runtime variable names are made available inside filters.
    std::vector<std::string> runtimeVarNames;
    for (const auto& [name, val] : ConfigManager::GetAllRuntime()) runtimeVarNames.push_back(name);

    for (const auto& s : spec.streams) {
        Stream st;
        st.name = s.name;
        st.level = ParseLevel(s.level);
        if (st.level == StreamLevel::Unknown) {
            std::cerr << "OutputManager: unknown level '" << s.level << "' for stream '" << s.name << "'" << std::endl;
            continue;
        }
        for (const auto& col : s.columns) {
            ColType type = ColType::None;
            bool isStr = false;
            std::string branch;
            if (!TreeManager::ResolveColumn(col, type, isStr, branch)) {
                std::cerr << "OutputManager: unknown column '" << col << "' in stream '" << s.name << "'" << std::endl;
                continue;
            }
            st.colNames.push_back(branch);
            st.colTypes.push_back(type);
            st.colIsString.push_back(isStr);
        }
        // Auto-append run_id.
        st.colNames.push_back("run_id");
        st.colTypes.push_back(ColType::RunID);
        st.colIsString.push_back(false);

        if (!s.filter.empty()) {
            st.filter = eval->PrecompileForFilter(s.filter, GetAllFilterVarIndices(), GetAllFilterVarNames(),
                                                  {}, {}, runtimeVarNames);
            if (st.filter) st.filterVarIndices = eval->GetUsedVariables(st.filter.get());
            else std::cerr << "OutputManager: failed to compile filter '" << s.filter << "' for stream '" << s.name << "'" << std::endl;
        }
        st.filterVolumes = s.filterVolumes;
        if (!s.filterCondition.empty()) {
            st.filterCondition = eval->PrecompileForFilter(s.filterCondition, GetAllFilterVarIndices(), GetAllFilterVarNames(),
                                                           {}, {}, runtimeVarNames);
            if (st.filterCondition) st.filterConditionVarIndices = eval->GetUsedVariables(st.filterCondition.get());
            else std::cerr << "OutputManager: failed to compile filter condition '" << s.filterCondition << "' for stream '" << s.name << "'" << std::endl;
        }
        fStreams.push_back(std::move(st));
    }
}
OutputManager::~OutputManager() { EndOfRun(); }

void OutputManager::BeginOfRun(const std::string& outFile, int threadId) {
    // Per-thread file naming: output_t0.root, output_t1.root, ... (master thread keeps the base name).
    std::string base = outFile;
    auto dot = base.rfind('.');
    std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
    std::string ext  = (dot == std::string::npos) ? ""    : base.substr(dot);
    std::string fileName = (threadId < 0) ? base : (stem + "_t" + std::to_string(threadId) + ext);

    fFile.reset(TFile::Open(fileName.c_str(), "RECREATE"));
    if (!fFile || fFile->IsZombie()) {
        std::cerr << "OutputManager: cannot open " << fileName << std::endl;
        fFile.reset();
        return;
    }

    for (auto& s : fStreams) {
        s.fileName = fileName;
        s.writer = std::make_unique<RNTupleStreamWriter>();
        std::vector<ColumnSpec> cols;
        for (size_t i = 0; i < s.colNames.size(); ++i) {
            ColumnSpec cs;
            cs.name = s.colNames[i];
            cs.kind = s.colIsString[i] ? ColumnKind::String : ColumnKind::Double;
            cols.push_back(cs);
        }
        s.writer->SetWriteOptions(fCompressionAlgorithm, fCompressionLevel,
                                  fClusterSizeMb, fPageSizeKb);
        s.writer->Open(fFile.get(), s.name, cols);
    }
}

void OutputManager::EndOfRun() {
    for (auto& s : fStreams) {
        if (s.writer) { s.writer->Close(); s.writer.reset(); }
    }
    if (fFile) { fFile->Close(); fFile.reset(); }
}
void OutputManager::Fill(StreamLevel level, const UnifiedSource& src, DataExtractor* extractor,
                         ExpressionEvaluator* eval, int eventId, int volId, int matId) {
    for (auto& s : fStreams) {
        if (s.level != level || !s.writer) continue;

        const bool hasVolFilter = !s.filterVolumes.empty();
        const bool hasCond = (s.filterCondition != nullptr);
        const bool hasScalar = (s.filter != nullptr);

        if (hasVolFilter || hasCond || hasScalar) {
            std::vector<FilterVar> needed;
            needed.insert(needed.end(), s.filterVarIndices.begin(), s.filterVarIndices.end());
            needed.insert(needed.end(), s.filterConditionVarIndices.begin(), s.filterConditionVarIndices.end());

            FilterVars vars{};
            extractor->BuildFilterVars(vars, src, eventId, volId, matId, &needed);

            if (hasVolFilter) {
                // Volume name only exists for step/track sources; a Digit has
                // no G4Step, so resolving VolumeName would dereference a null
                // step pointer. Digits are matched via their detector name.
                std::string vn;
                if (src.GetType() != UnifiedSource::Type::Digit) {
                    vn = extractor->GetString(ColType::VolumeName, src);
                }
                std::string dn = extractor->GetString(ColType::DetectorName, src);
                bool found = false;
                for (const auto& fv : s.filterVolumes) {
                    const bool vMatch = !vn.empty() && (fv == vn || vn.find(fv) != std::string::npos);
                    const bool dMatch = !dn.empty() && (fv == dn || dn.find(fv) != std::string::npos);
                    if (vMatch || dMatch) { found = true; break; }
                }
                if (!found) continue;
            }

            if (hasCond) {
                for (const auto& [name, val] : ConfigManager::GetAllRuntime()) {
                    auto it = s.filterCondition->varPtrs.find(name);
                    if (it != s.filterCondition->varPtrs.end()) *(it->second) = val;
                }
                if (eval->Execute(s.filterCondition.get(), vars) == 0.0) continue;
            }

            if (hasScalar) {
                for (const auto& [name, val] : ConfigManager::GetAllRuntime()) {
                    auto it = s.filter->varPtrs.find(name);
                    if (it != s.filter->varPtrs.end()) *(it->second) = val;
                }
                if (eval->Execute(s.filter.get(), vars) == 0.0) continue;
            }
        }

        Row row;
        for (size_t i = 0; i < s.colNames.size(); ++i) {
            if (s.colNames[i] == "run_id") {
                row.numbers["run_id"] = static_cast<double>(BeamAnalysis::Instance()->GetCurrentRunId());
            } else if (s.colIsString[i]) {
                row.strings[s.colNames[i]] = extractor->GetString(s.colTypes[i], src);
            } else {
                row.numbers[s.colNames[i]] = extractor->GetDouble(s.colTypes[i], src, eventId);
            }
        }
        s.writer->WriteRow(row);
    }
}

void OutputManager::MergeFiles(const std::string& outFile, const std::vector<std::string>& perThreadFiles) {
    if (perThreadFiles.empty()) return;

    TFileMerger merger;
    if (!merger.OutputFile(outFile.c_str(), "RECREATE")) {
        std::cerr << "OutputManager: cannot create merge output " << outFile << std::endl;
        return;
    }
    for (const auto& f : perThreadFiles) {
        if (!merger.AddFile(f.c_str()))
            std::cerr << "OutputManager: cannot add " << f << " for merge" << std::endl;
    }
    if (!merger.Merge())
        std::cerr << "OutputManager: merge failed into " << outFile << std::endl;
}
