//==============================================================================
//
// G4CARE
//
// @file    PrimaryGeneratorAction.cc
// @brief   Source manager factory — creates all particle sources from YAML.
//
// @details
//   Reads the DATA and SOURCE configuration blocks and instantiates the
//   appropriate source types:
//   - GPS (General Particle Source) — Geant4 GPS macro execution.
//   - ParticleGun — ExprTK-based configurable particle gun.
//   - Radioactive — radioactive decay ion source.
//   - Activation — data-driven activation source.
//   - Mixture — weighted blend of multiple sources.
//
//   Also loads external data files (CSV, ROOT, IAEA) referenced in the
//   DATA block and registers them as aliases for ExprTK expressions.
//   Sets the SOURCE_MODE (flow or event) on the SourceManager.
//
//   Configuration keys read:
//     DATA.<name>.file, .tree, .header, .delimiter, .skip_first_n
//     SOURCE.<name>.type, .weight, .particle, .energy, .macro, etc.
//     SOURCE_MODE
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

#include "PrimaryGeneratorAction.hh"
#include "SourceManager.hh"
#include "GPSSource.hh"
#include "ParticleGunSource.hh"
#include "RadioactiveSource.hh"
#include "ObjectManager.hh"
#include "ConfigManager.hh"
#include "G4UImanager.hh"
#include "G4ios.hh"
#include "ExpressionEvaluator.hh"
#include "MixtureSource.hh"
#include "ActivationSource.hh"

#include <map>
#include <algorithm>
#include <cctype>

/// @brief Construct the primary generator action and create all sources.
///
/// @param objMgr Pointer to ObjectManager for volume-based source placement.
///
/// Reads DATA and SOURCE blocks from ConfigManager.  For each DATA subsection
/// a data file is loaded (CSV via RDFReader, ROOT via RDataFrame, IAEA header).
/// For each SOURCE subsection an appropriate source object is created
/// (GPS, ParticleGun, Radioactive, Activation, Mixture) and registered
/// in SourceManager.  The SOURCE_MODE (flow / event) is applied to
/// SourceManager.
PrimaryGeneratorAction::PrimaryGeneratorAction(ObjectManager* objMgr) {
    fSourceManager = std::make_unique<SourceManager>();

    auto* cfg = ConfigManager::Instance();
    std::vector<std::string> dataSections = cfg->GetSubsections("DATA");

    for (const auto& sec : dataSections) {
        std::string filename = cfg->GetString("DATA." + sec + ".file", "");
        if (filename.empty()) continue;
        
        size_t dotPos = filename.find_last_of('.');
        if (dotPos == std::string::npos) {
            G4cerr << "File has no extension, skipping." << G4endl;
            continue;
        }
        std::string ext = filename.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        G4cout << "[PrimaryGeneratorAction] Loading global data file: " << filename << " (extension: " << ext << ")" << G4endl;

        int idx = -1;
        if (ext == "csv" || ext == "txt") {
        bool hasHeader = cfg->GetBool("DATA." + sec + ".header", true);
        std::string delimStr = cfg->GetString("DATA." + sec + ".delimiter", ",");
        char delimiter = delimStr.empty() ? ',' : delimStr[0];
        int skip = cfg->GetInt("DATA." + sec + ".skip_first_n", 0);
        idx = ExpressionEvaluator::LoadCSVFile(filename, hasHeader, delimiter, skip);
        } else if (ext == "root") {
            std::string treename = cfg->GetString("DATA." + sec + ".tree", "tree");
            std::string spec = filename + ":" + treename;
            idx = ExpressionEvaluator::LoadROOTFile(spec);
        } else if (ext == "iaeaheader" || filename.find(".IAEAheader") != std::string::npos) {
            idx = ExpressionEvaluator::LoadIAEAFile(filename);
        } else {
            G4cerr << "Unknown file extension '" << ext << "' for data file " << filename << G4endl;
            continue;
        }

        if (idx >= 0) {
            ExpressionEvaluator::RegisterDataAlias(sec, idx);
            G4cout << "[PrimaryGeneratorAction] Registered data alias: " << sec << " -> " << idx << G4endl;
        } else {
            G4cerr << "ERROR: Failed to load data file: " << filename << G4endl;
        }
    }

    std::vector<std::string> srcIds = cfg->GetSubsections("SOURCE");
    std::vector<std::string> mixtureIds;
    G4cout << "PrimaryGeneratorAction: found " << srcIds.size() << " source sections." << G4endl;

    for (const auto& srcId : srcIds) {
        std::string baseKey = "SOURCE." + srcId + ".";
        std::string type = cfg->GetString(baseKey + "type", "");
        double weight = cfg->GetDouble(baseKey + "weight", 1.0);
        std::unique_ptr<Source> source;
        
        if (type == "mixture") {
            mixtureIds.push_back(srcId);
            continue;
        }
                
        if (type == "radioactive" || type == "Radioactive") {
            source = std::make_unique<RadioactiveSource>("SOURCE." + srcId, objMgr);
            source->SetWeight(weight);
            fSourceManager->AddSource(std::move(source), weight, srcId);
            G4cout << "PrimaryGeneratorAction: added radioactive source '" << srcId << "'" << G4endl;
            continue;
        }

        if (type == "activation") {
            auto actSource = std::make_unique<ActivationSource>("SOURCE." + srcId);
            // Initialize global data before moving the source
            std::string dataAlias = cfg->GetString("SOURCE." + srcId + ".data", "");
            if (!dataAlias.empty()) {
                ActivationSource::PrepareGlobalData(dataAlias);
            }
            actSource->SetWeight(weight);
            fSourceManager->AddSource(std::move(actSource), weight, srcId);
            G4cout << "PrimaryGeneratorAction: added activation source '" << srcId << "'" << G4endl;
        }

        else if (type == "GPS") {
            std::string macroFile = cfg->GetString(baseKey + "macro", "");
            if (!macroFile.empty()) {
                source = std::make_unique<GPSSource>(macroFile);
            } else {
                G4cerr << "PrimaryGeneratorAction: GPS source '" << srcId << "' missing 'macro'. Skipping." << G4endl;
                continue;
            }
        }

        else if (type == "particleGun") {
            source = std::make_unique<ParticleGunSource>("SOURCE." + srcId);
        }

        else {
            G4cerr << "PrimaryGeneratorAction: unknown source type '" << type << "' in section '" << srcId << "'. Skipping." << G4endl;
            continue;
        }

        if (source) {
            source->SetWeight(weight);
            fSourceManager->AddSource(std::move(source), weight, srcId);
            G4cout << "PrimaryGeneratorAction: added source of type '" << type << "' with weight " << weight << G4endl;
        }
    }

        for (const auto& srcId : mixtureIds) {
        std::string baseKey = "SOURCE." + srcId + ".";
        std::vector<std::pair<std::string, double>> components;
        
        std::vector<std::string> compIndices = cfg->GetSubsections(baseKey + "components");
        for (const auto& idx : compIndices) {
            std::string compPath = baseKey + "components." + idx + ".";
            std::string sourceName = cfg->GetString(compPath + "source", "");
            double weight = cfg->GetDouble(compPath + "weight", 1.0);
            if (!sourceName.empty()) {
                components.emplace_back(sourceName, weight);
            }
        }
        
        if (components.empty()) {
            G4cerr << "PrimaryGeneratorAction: mixture source '" << srcId << "' has no components. Skipping." << G4endl;
            continue;
        }
        
        auto mixture = std::make_unique<MixtureSource>(srcId, components, fSourceManager.get());
        mixture->SetWeight(cfg->GetDouble(baseKey + "weight", 1.0));
        fSourceManager->AddSource(std::move(mixture), cfg->GetDouble(baseKey + "weight", 1.0), srcId);
        G4cout << "PrimaryGeneratorAction: added mixture source '" << srcId << "' with " << components.size() << " components." << G4endl;
    }

    std::string modeStr = cfg->GetString("SOURCE_MODE", "flow");
    SourceManager::SourceMode mode = SourceManager::SourceMode::kFlow;
    if (modeStr == "event") {
        mode = SourceManager::SourceMode::kEvent;
    } else if (modeStr != "flow") {
        G4cerr << "PrimaryGeneratorAction: WARNING - unknown SOURCE_MODE '" << modeStr
               << "'. Valid values: flow, event. Using 'flow'." << G4endl;
    }
    fSourceManager->SetMode(mode);
    G4cout << "PrimaryGeneratorAction: source mode set to " << modeStr << G4endl;

    if (fSourceManager->Empty()) {
        G4cerr << "PrimaryGeneratorAction: WARNING - no sources defined!" << G4endl;
    }
}

PrimaryGeneratorAction::~PrimaryGeneratorAction() = default;

/// @brief Delegate primary vertex generation to SourceManager.
///
/// For each event, SourceManager selects the appropriate set of sources
/// (all in flow mode, one by weight in event mode) and calls their
/// respective GeneratePrimaries methods.
///
/// @param event Current Geant4 event to populate with primary tracks.
void PrimaryGeneratorAction::GeneratePrimaries(G4Event* event) {
    fSourceManager->GeneratePrimaries(event);
}
