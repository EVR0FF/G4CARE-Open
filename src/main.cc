//==============================================================================
// G4CARE - main.cc — Application entry point. Loads YAML config, initialises
// Geant4 run manager, geometry, physics, and actions. Supports chemistry
// (Geant4-DNA), scoring, macros, visualization, and multi-stage runs.
// Configuration keys: EVENTS, NTHREADS, VISUALIZATION, MACROS, SEED, CHEMISTRY.*,
// PHYSICS.*, GEOMETRY_FILE, SENSITIVE_VOLUMES, OUTPUT_FILE, DECAY_TIME_THRESHOLD.
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================
#include "G4RunManager.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"
#include "G4UIExecutive.hh"
#include "G4GeometryManager.hh"
#include "GeometryManager.hh"
#include "ActionInitialization.hh"
#include "BeamAnalysis.hh"
#include "PhysicsManager.hh"
#include "StageManager.hh"
#include "BEBDataSource.hh"
#include "UserCrossSectionModel.hh"
#include "ConvergenceValidator.hh"
#include "ConfigManager.hh"
#include "ExpressionEvaluator.hh"
#include "G4Electron.hh"
#include "G4Proton.hh"
#include "G4SystemOfUnits.hh"
#include "G4HadronicParameters.hh"
#include "G4VModularPhysicsList.hh"
#include "MaterialManager.hh"
#include "Randomize.hh"

#include <TROOT.h>
#include <string>
#ifdef G4MULTITHREADED
#include "G4MTRunManager.hh"
#endif

#include <iostream>

int main(int argc, char** argv) {
    ROOT::EnableThreadSafety();

    std::string configPath = "config.yaml";
    std::string macroPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            macroPath = argv[++i];
        } else if (arg[0] != '-') {
            configPath = arg;
        }
    }

    auto* cfg = ConfigManager::Instance();

    G4cout << "Loading configuration: " << configPath << G4endl;

    // ── Чтение MACROS и SEED из YAML ДО LoadConfig (fRootNode разрушается ExtractAllData) ──
    struct MacroEntry { std::string file, stage, condition; };
    std::vector<MacroEntry> allMacros;
    struct SeedEntry { std::vector<long> values; std::string condition; };
    std::vector<SeedEntry> seedEntries;
    try {
        YAML::Node rawConfig = YAML::LoadFile(configPath);

        // MACROS
        YAML::Node macrosNode = rawConfig["MACROS"];
        if (macrosNode.IsSequence()) {
            for (const auto& item : macrosNode) {
                MacroEntry m;
                if (item.IsScalar()) {
                    m.file = item.as<std::string>();
                    m.stage = "after";
                } else if (item.IsMap()) {
                    m.file      = item["file"]      ? item["file"].as<std::string>()      : "";
                    m.stage     = item["stage"]     ? item["stage"].as<std::string>()     : "after";
                    m.condition = item["condition"] ? item["condition"].as<std::string>()  : "";
                }
                if (!m.file.empty()) allMacros.push_back(std::move(m));
            }
        }
        G4cout << "[MACROS] Loaded " << allMacros.size() << " macros from YAML" << G4endl;
        for (size_t i = 0; i < allMacros.size(); ++i) {
            G4cout << "  [" << i << "] file=" << allMacros[i].file
                   << " stage=" << allMacros[i].stage << G4endl;
        }

        // SEED
        YAML::Node seedNode = rawConfig["SEED"];
        if (seedNode.IsSequence()) {
            if (seedNode.size() > 0 && seedNode[0].IsScalar()) {
                SeedEntry s;
                for (const auto& v : seedNode) {
                    if (v.IsScalar()) s.values.push_back(v.as<long>());
                }
                if (s.values.size() == 2) seedEntries.push_back(std::move(s));
            } else {
                for (const auto& item : seedNode) {
                    if (!item.IsMap()) continue;
                    SeedEntry s;
                    s.condition = item["condition"] ? item["condition"].as<std::string>() : "";
                    YAML::Node vals = item["values"];
                    if (vals.IsSequence()) {
                        for (const auto& v : vals)
                            if (v.IsScalar()) s.values.push_back(v.as<long>());
                    }
                    if (s.values.size() == 2) seedEntries.push_back(std::move(s));
                }
            }
        } else if (seedNode.IsMap()) {
            SeedEntry s;
            s.condition = seedNode["condition"] ? seedNode["condition"].as<std::string>() : "";
            YAML::Node vals = seedNode["values"];
            if (vals.IsSequence()) {
                for (const auto& v : vals)
                    if (v.IsScalar()) s.values.push_back(v.as<long>());
            }
            if (s.values.size() == 2) seedEntries.push_back(std::move(s));
        }
        if (!seedEntries.empty()) {
            G4cout << "[SEED] Loaded " << seedEntries.size() << " seed config(s)" << G4endl;
            for (size_t i = 0; i < seedEntries.size(); ++i) {
                G4cout << "  [" << i << "] seeds=[" << seedEntries[i].values[0]
                       << "," << seedEntries[i].values[1] << "]"
                       << " cond=" << seedEntries[i].condition << G4endl;
            }
            // Apply first unconditional seed
            for (const auto& se : seedEntries) {
                if (se.condition.empty()) {
                    long sarr[2] = {se.values[0], se.values[1]};
                    CLHEP::HepRandom::setTheSeeds(sarr);
                    G4cout << "[SEED] Applied: seeds=[" << se.values[0]
                           << "," << se.values[1] << "]" << G4endl;
                    break;
                }
            }
        }
    } catch (const YAML::Exception& e) {
        G4cerr << "[MACROS/SEED] Failed to parse: " << e.what() << G4endl;
    }

    if (!cfg->LoadConfig(configPath)) {
        G4cerr << "Failed to load configuration: " << configPath << G4endl;
        return 1;
    }

    bool visualization = cfg->GetBool("VISUALIZATION", false);

    if (!MaterialManager::Instance()->BuildMaterials()) {
        G4cerr << "Fatal: Failed to build custom materials." << G4endl;
        return 1;
    }

    G4RunManager* runManager;
#ifdef G4MULTITHREADED
    if (visualization) {
        // При визуализации используем однопоточный RunManager —
        // OGL не может рисовать из worker-потоков G4MTRunManager.
        runManager = new G4RunManager;
        G4cout << "Using G4RunManager (sequential) for visualization mode." << G4endl;
    } else {
        runManager = new G4MTRunManager;
    }
#else
    runManager = new G4RunManager;
#endif

    int nThreads = cfg->GetInt("NTHREADS", 1);
    nThreads = std::max(1, nThreads);
#ifdef G4MULTITHREADED
    runManager->SetNumberOfThreads(nThreads);
    G4cout << "Using G4MTRunManager with " << nThreads << " threads." << G4endl;
#else
    if (nThreads > 1) {
        G4cout << "WARNING: Multithreading requested but Geant4 built without G4MULTITHREADED." << G4endl;
    }
#endif

    auto* geomManager = new GeometryManager();
    PhysicsManager physMgr;
    G4VModularPhysicsList* physicsList = physMgr.CreatePhysicsList();

    if (!physicsList) {
        G4cerr << "Fatal: cannot create physics list." << G4endl;
        return 1;
    }

    runManager->SetUserInitialization(geomManager);
    runManager->SetUserInitialization(static_cast<G4VUserPhysicsList*>(physicsList));
    runManager->SetUserInitialization(new ActionInitialization(geomManager, &physMgr));

    double decayThreshold = cfg->GetValueWithUnits("DECAY_TIME_THRESHOLD", 0.0);
    if (decayThreshold > 0.0) {
        G4HadronicParameters::Instance()->SetTimeThresholdForRadioactiveDecay(decayThreshold);
        G4cout << "Radioactive decay time threshold set to " << decayThreshold/CLHEP::year << " years" << G4endl;
    } else {
        G4HadronicParameters::Instance()->SetTimeThresholdForRadioactiveDecay(0.0);
        G4cout << "Radioactive decay: NO time threshold (all decays enabled)" << G4endl;
    }

    bool chemEnabled = cfg->GetBool("CHEMISTRY.ENABLE", false);
    if (chemEnabled) {
        auto* ui = G4UImanager::GetUIpointer();
        auto speciesList = cfg->GetStringVector("CHEMISTRY.SPECIES");
        if (speciesList.empty()) {
            speciesList = cfg->GetStringVector("CHEMISTRY.CHEM_LIST");
        }

        static const std::map<std::string, std::string> kCanonical = {
            {"eaq","e_aq"},   {"aq","e_aq"},   {"e_aq","e_aq"}, {"e_aq^-1","e_aq"},
            {"OH","OH"},      {"°OH","OH"},     {"°OH^0","OH"}, {"OH^0","OH"},
            {"H","H"},        {"H^0","H"},
            {"H3O","H3O"},    {"H3O^1","H3O"},
            {"H2O2","H2O2"},  {"H2O2^0","H2O2"},
            {"H2","H2"},      {"H_2","H2"},     {"H_2^0","H2"},
            {"HO2","HO2"},    {"HO2^0","HO2"},
            {"O2","O2"},      {"O2^0","O2"},
            {"OHm","OHm"},    {"OH^-1","OHm"},
            {"O","O"},        {"O^0","O"},
            {"O-","O-"},       {"O^-1","O-"}
        };

        bool useAllSpecies = (!speciesList.empty() && speciesList[0] == "All");
        if (useAllSpecies) {
            speciesList.clear();
            for (const auto& pair : kCanonical) {
                if (pair.first == pair.second) {
                    speciesList.push_back(pair.second);
                }
            }
            G4cout << "[main] SPECIES=All — using full set of "
                   << speciesList.size() << " species" << G4endl;
        }
        auto canonical = [](const std::string& n) {
            auto it = kCanonical.find(n);
            return (it != kCanonical.end()) ? it->second : n;
        };

        if (!speciesList.empty()) {
            for (const auto& mol : speciesList) {
                ui->ApplyCommand("/chem/chemistry/addMolecule " + canonical(mol));
            }
            G4cout << "[main] Loaded " << speciesList.size()
                   << " species into G4DNA chemistry manager" << G4endl;
        }

        ui->ApplyCommand("/chem/stage/activate True");
        G4cout << "[main] Chemistry stage activated" << G4endl;

        std::string modelStr = cfg->GetString("CHEMISTRY.TIME_STEP_MODEL", "SBS");
        ui->ApplyCommand("/process/chem/TimeStepModel " + modelStr);

        int schedVerbose = cfg->GetInt("CHEMISTRY.SCHEDULER_VERBOSE", 0);
        if (schedVerbose > 0) {
            ui->ApplyCommand("/scheduler/verbose " + std::to_string(schedVerbose));
            G4cout << "[main] Scheduler verbose=" << schedVerbose << G4endl;
        }

        double temperature = cfg->GetValueWithUnits("CHEMISTRY.TEMPERATURE", 300.0 * CLHEP::kelvin);
        ui->ApplyCommand("/chem/diffusion/temperature "
                         + std::to_string(temperature / CLHEP::kelvin) + " kelvin");

        double pressure = cfg->GetValueWithUnits("CHEMISTRY.PRESSURE", 1.0 * CLHEP::atmosphere);
        ui->ApplyCommand("/chem/diffusion/pressure "
                         + std::to_string(pressure / CLHEP::atmosphere) + " atmosphere");

        if (cfg->HasKey("CHEMISTRY.DENSITY")) {
            double density = cfg->GetValueWithUnits("CHEMISTRY.DENSITY", 1.0 * CLHEP::g / CLHEP::cm3);
            ui->ApplyCommand("/chem/diffusion/density "
                             + std::to_string(density / (CLHEP::g / CLHEP::cm3)) + " g/cm3");
        }
        if (cfg->HasKey("CHEMISTRY.START_TIME")) {
            double t = cfg->GetValueWithUnits("CHEMISTRY.START_TIME", 1.0 * CLHEP::ps);
            ui->ApplyCommand("/chem/time/start " + std::to_string(t / CLHEP::ps) + " ps");
        }
        if (cfg->HasKey("CHEMISTRY.END_TIME")) {
            double t = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * CLHEP::ns);
            ui->ApplyCommand("/chem/time/end " + std::to_string(t / CLHEP::ns) + " ns");
        }

        auto speciesDefs = cfg->GetStringVector("CHEMISTRY.SPECIES_DEFINITIONS");
        if (!speciesDefs.empty()) {
            for (const auto& def : speciesDefs) {
                ui->ApplyCommand("/chem/species " + def);
            }
            G4cout << "[main] Loaded " << speciesDefs.size()
                   << " custom species definitions (D/Radius)" << G4endl;
        }

        std::string solvationModel = cfg->GetString("CHEMISTRY.SOLVATION_MODEL", "");
        if (!solvationModel.empty()) {
            ui->ApplyCommand("/process/dna/e-SolvationSubType " + solvationModel);
            G4cout << "[main] Solvation model: " << solvationModel << G4endl;
        }

        bool resetReactionTable = cfg->GetBool("CHEMISTRY.RESET_REACTION_TABLE", false);
        auto reactionKeys = cfg->GetSectionKeys("CHEMISTRY.REACTION");
        if (resetReactionTable && !reactionKeys.empty()) {
            ui->ApplyCommand("/chem/reaction/UI");
            G4cout << "[main] Reaction table reset via /chem/reaction/UI" << G4endl;
        }
        for (const auto& key : reactionKeys) {
            double rate = cfg->GetValueWithUnits("CHEMISTRY.REACTION." + key, 0.0);
            ui->ApplyCommand("/chem/reaction/modify " + key + " " + std::to_string(rate));
        }
        G4cout << "[main] Chemistry UI commands applied BEFORE Initialize()" << G4endl;
    }

    G4UImanager* UImanager = G4UImanager::GetUIpointer();

    if (visualization) {
        UImanager->ApplyCommand("/tracking/storeTrajectory 1");
        G4cout << "[main] Trajectory storage enabled for visualization" << G4endl;
    }

    runManager->Initialize();

    G4UImanager::GetUIpointer()->ApplyCommand(
        "/process/had/rdm/thresholdForVeryLongDecayTime 1.0e+60 year");
    G4cout << "Applied: /process/had/rdm/thresholdForVeryLongDecayTime 1.0e+60 year" << G4endl;

    if (cfg->HasKey("CROSS_SECTION.BEB_FILE")) {
        std::string bebFile = cfg->GetString("CROSS_SECTION.BEB_FILE");
        auto* bebSource = new BEBDataSource("MineralBEB", bebFile);
        auto* electronIoniModel = new UserCrossSectionModel("MineralIoni",
                                                    G4Electron::Electron(),
                                                    "ioni",
                                                    {bebSource});
        physMgr.RegisterUserCrossSection(electronIoniModel,
                                         G4Electron::Electron(),
                                         "eIoni");
    }

    // ── Выполнение макросов ──
    auto execMacros = [&](const std::string& filterStage) {
        G4cout << "[MACROS] stage=" << filterStage << " executing " 
               << std::count_if(allMacros.begin(), allMacros.end(),
                   [&](const MacroEntry& m){ return m.stage == filterStage || m.stage == "both"; })
               << " macros" << G4endl;
        for (const auto& m : allMacros) {
            if (m.stage == filterStage || m.stage == "both") {
                G4cout << "  execute: " << m.file << G4endl;
                UImanager->ApplyCommand("/control/execute " + m.file);
            }
        }
    };

    // Визуализация
    G4VisManager* vis = nullptr;
    G4UIExecutive* ui = nullptr;
    if (visualization) {
        vis = new G4VisExecutive();
        vis->Initialize();
        ui = new G4UIExecutive(argc, argv);

        UImanager->ApplyCommand("/vis/open OGL 800x600-0+0");

        double startTime = cfg->GetValueWithUnits("CHEMISTRY.START_TIME", 1.0 * CLHEP::ps);
        double endTime   = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * CLHEP::ns);
        UImanager->ApplyCommand("/control/alias startTime " + std::to_string(startTime / CLHEP::ps));
        UImanager->ApplyCommand("/control/alias endTime " + std::to_string(endTime / CLHEP::ns));

        // Макросы настройки сцены — ДО RunAll
        execMacros("before");
    }

    G4cout << "Running StageManager..." << G4endl;

    StageManager stageMgr(runManager);
    stageMgr.LoadConfiguration(configPath);
    ConvergenceValidator validator;
    stageMgr.RunAll();

    auto results = validator.CheckAllVoxels();
    if (!results.empty()) {
        validator.WriteReport("convergence_report.txt", results);
    }

    if (visualization) {
        // Макросы после RunAll (если есть stage: after)
        execMacros("after");

        ui->SessionStart();
        delete ui;
        delete vis;
    }

    BeamAnalysis::Instance()->EndRun();
    // Write/CloseFile уже выполняется в RunAction::EndOfRunAction()

    bool heatmapExport = cfg->GetBool("HEATMAP_EXPORT", false);
    if (heatmapExport) {
        std::string heatmapFile = cfg->GetString("HEATMAP_FILE", "scoring.vtu");
        execMacros("after");
        // Heatmap: все макросы уже выполнены, повторно не вызываем
        if (false) for (const auto& file : std::vector<std::string>{}) {
            UImanager->ApplyCommand("/control/execute " + file);
        }
        bool autoLaunch = cfg->GetBool("HEATMAP_AUTOLAUNCH", true);
        if (autoLaunch) {
            std::string cmd = "paraview " + heatmapFile + " &";
            int ret = system(cmd.c_str());
            if (ret != 0) {
                G4cerr << "WARNING: Failed to launch ParaView" << G4endl;
            }
        }
    }

    BeamAnalysis::DeleteInstance();
    ExpressionEvaluator::ClearAll();
    return 0;
}