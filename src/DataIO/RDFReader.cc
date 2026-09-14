//==============================================================================
//
// G4CARE
//
// @file    RDFReader.cc
// @brief   Implementation of RDFReader — loads CSV and ROOT files into memory.
//
// @details
//   Provides LoadROOTFile() and LoadCSVFile() methods that read tabular data
//   via ROOT::RDataFrame and store it in in-memory column vectors for
//   thread-safe access from multiple Geant4 worker threads.
//
//   Configuration keys read (via ConfigManager elsewhere):
//     DATA.<name>.file
//     DATA.<name>.tree
//     DATA.<name>.header
//     DATA.<name>.delimiter
//     DATA.<name>.skip_first_n
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

#include "RDFReader.hh"
#include "ConfigManager.hh"
#include <ROOT/RDataFrame.hxx>
#include <ROOT/RCsvDS.hxx>
#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>
#include <fstream>
#include <sstream>
#include <iostream>

RDFReader::RDFReader() = default;

RDFReader::~RDFReader() {
    if (fROOTFile && fROOTFile->IsOpen()) {
        fROOTFile->Close();
    }
}

/// @brief Load a ROOT TTree file into in-memory storage.
/// @param filenames List of file paths (only first is used).
/// @param treename Name of the TTree to read.
/// @return true on success, false on failure.
bool RDFReader::LoadROOTFile(const std::vector<std::string>& filenames, const std::string& treename) {
    if (filenames.empty()) return false;
    const std::string& filename = filenames[0];

    try {
        // 1. Open TFile/TTree to get branch names in TTree order (not alphabetical)
        auto file = std::make_unique<TFile>(filename.c_str(), "READ");
        if (!file->IsOpen()) {
            std::cerr << "[RDFReader] Cannot open file " << filename << std::endl;
            return false;
        }
        TTree* tree = static_cast<TTree*>(file->Get(treename.c_str()));
        if (!tree) {
            std::cerr << "[RDFReader] Tree '" << treename << "' not found in " << filename << std::endl;
            return false;
        }
        
        // Collect branch names in TTree order
        std::vector<std::string> orderedBranches;
        TObjArray* branches = tree->GetListOfBranches();
        for (int i = 0; i < branches->GetEntries(); ++i) {
            TBranch* br = static_cast<TBranch*>(branches->At(i));
            orderedBranches.push_back(br->GetName());
        }
        fNumRows = tree->GetEntries();
        file->Close();
        
        if (orderedBranches.empty()) {
            std::cerr << "[RDFReader] No branches found in tree '" << treename << "'" << std::endl;
            return false;
        }
        
        // 2. Use RDataFrame to read columns in TTree branch order
        ROOT::RDataFrame df(treename, filename);
        fColumns = orderedBranches;
        
        const size_t nCols = fColumns.size();
        fROOTData.resize(fNumRows, std::vector<double>(nCols, 0.0));
        
        for (size_t c = 0; c < nCols; ++c) {
            const std::string& colName = fColumns[c];
            try {
                auto result = df.Take<double>(colName);
                const auto& vec = *result;
                for (size_t r = 0; r < fNumRows; ++r) {
                    fROOTData[r][c] = vec[r];
                }
            } catch (const std::exception& e) {
                // Try as float
                try {
                    auto result = df.Take<float>(colName);
                    const auto& vec = *result;
                    for (size_t r = 0; r < fNumRows; ++r) {
                        fROOTData[r][c] = static_cast<double>(vec[r]);
                    }
                } catch (...) {
                    // Try as int
                    try {
                        auto result = df.Take<int>(colName);
                        const auto& vec = *result;
                        for (size_t r = 0; r < fNumRows; ++r) {
                            fROOTData[r][c] = static_cast<double>(vec[r]);
                        }
                    } catch (...) {
                        std::cerr << "[RDFReader] Warning: cannot read column '" << colName << "' as numeric, filling with zeros." << std::endl;
                    }
                }
            }
        }
        
        fMode = Mode::kROOT;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RDFReader] Error loading ROOT file '" << filename << "': " << e.what() << std::endl;
        return false;
    }
}

bool RDFReader::LoadCSVFile(const std::string& filename, bool header, char delimiter, int skip) {
    try {
    
        std::vector<std::string> originalNames;
        if (header) {
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "[RDFReader] Cannot open file " << filename << " to read header." << std::endl;
                return false;
            }
        
            std::string line;
            for (int i = 0; i < skip; ++i) {
                if (!std::getline(file, line)) break;
            }
        
            if (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string cell;
                while (std::getline(ss, cell, delimiter)) {
                
                    if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"')
                        cell = cell.substr(1, cell.size() - 2);
                    originalNames.push_back(cell);
                }
            } else {
                std::cerr << "[RDFReader] Could not read header from " << filename << std::endl;
                return false;
            }
            file.close();
        }

        ROOT::RDF::RCsvDS::ROptions opts;
        opts.fDelimiter = delimiter;
        opts.fSkipFirstNLines = skip;
        opts.fHeaders = header;
        auto df = ROOT::RDF::FromCSV(filename, opts);
        auto rdfColumns = df.GetColumnNames();
        fNumRows = *df.Count();

        if (!header) {
            originalNames.clear();
            for (size_t i = 0; i < rdfColumns.size(); ++i) {
                originalNames.push_back("col" + std::to_string(i));
            }
        }

        fColumns = originalNames;
        fCSVData.resize(fNumRows, std::vector<double>(fColumns.size(), 0.0));

        std::vector<std::string> targetNames(fColumns.size());
        ROOT::RDF::RNode currentDF = df;

        for (size_t userCol = 0; userCol < fColumns.size(); ++userCol) {
            const std::string& name = fColumns[userCol];

            auto it = std::find(rdfColumns.begin(), rdfColumns.end(), name);
            if (it == rdfColumns.end()) {
                std::cerr << "[RDFReader] Warning: column '" << name << "' not found in CSV file. Filling with zeros." << std::endl;
                targetNames[userCol] = "";
                continue;
            }

            std::string type = df.GetColumnType(name);
            std::string type_lower = type;
            std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(), ::tolower);

            bool isNumeric = (type_lower.find("double") != std::string::npos ||
                              type_lower.find("float") != std::string::npos ||
                              type_lower.find("int") != std::string::npos ||
                              type_lower.find("long") != std::string::npos ||
                              type_lower.find("short") != std::string::npos ||
                              type_lower == "bool");

            if (!isNumeric) {
                std::cerr << "[RDFReader] Warning: column '" << name << "' is non-numeric. Filling with zeros." << std::endl;
                targetNames[userCol] = "";
                continue;
            }

            if (type_lower.find("double") != std::string::npos || type_lower.find("float") != std::string::npos) {
                targetNames[userCol] = name;
            } else {
                std::string castedName = name + "_as_double";
                currentDF = currentDF.Define(castedName, "static_cast<double>(" + name + ")");
                targetNames[userCol] = castedName;
            }
        }

        for (size_t userCol = 0; userCol < fColumns.size(); ++userCol) {
            if (targetNames[userCol].empty()) continue;
            auto vecResult = currentDF.Take<double>(targetNames[userCol]);
            const auto& vec = *vecResult;
            if (vec.size() != fNumRows) {
                std::cerr << "[RDFReader] Error: column size mismatch for column " << fColumns[userCol] << std::endl;
                return false;
            }
            for (size_t j = 0; j < fNumRows; ++j) {
                fCSVData[j][userCol] = vec[j];
            }
        }

        fMode = Mode::kCSV;

        const size_t totalCells = fNumRows * fColumns.size();
        if (totalCells > 10'000'000) {
            std::cerr << "[RDFReader] Warning: large CSV file (" << totalCells << " cells) loaded entirely into memory. "
                      << "Consider using ROOT format for better performance and memory usage." << std::endl;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RDFReader] Error loading CSV file: " << e.what() << std::endl;
        return false;
    }
}

std::optional<double> RDFReader::GetCell(size_t row, size_t col) const {
    if (row >= fNumRows || col >= fColumns.size()) return std::nullopt;

    if (fMode == Mode::kROOT) {
        if (row >= fROOTData.size() || col >= fROOTData[row].size()) return std::nullopt;
        return fROOTData[row][col];
    }
    else if (fMode == Mode::kCSV) {
        if (row >= fCSVData.size() || col >= fCSVData[row].size()) return std::nullopt;
        return fCSVData[row][col];
    }
    else {
        return std::nullopt;
    }
}

std::optional<std::vector<double>> RDFReader::GetRow(size_t row) const {
    if (row >= fNumRows) return std::nullopt;

    if (fMode == Mode::kROOT) {
        if (row >= fROOTData.size()) return std::nullopt;
        return fROOTData[row];
    }
    else if (fMode == Mode::kCSV) {
        if (row >= fCSVData.size()) return std::nullopt;
        return fCSVData[row];
    }
    else {
        return std::nullopt;
    }
}