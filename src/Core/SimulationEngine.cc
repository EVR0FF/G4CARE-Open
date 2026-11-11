//==============================================================================
// G4CARE - SimulationEngine.cc — implementation of the top-level engine.
// Loads YAML config, initialises Geant4 run manager, geometry, physics and
// actions, and drives single/multi-stage runs, GDML/VTK export and heatmap.
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-10-07  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================
#include "SimulationEngine.hh"

#include "G4RunManager.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"
#include "G4UIExecutive.hh"
#include "G4GeometryManager.hh"
#include "GeometryManager.hh"
#include "ActionInitialization.hh"
#include "SteppingAction.hh"
#include "TrackingAction.hh"
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
#include "AdaptiveScoringManager.hh"

#include <TROOT.h>
#include <yaml-cpp/yaml.h>
#ifdef G4MULTITHREADED
#include "G4MTRunManager.hh"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>

SimulationEngine* SimulationEngine::fInstance = nullptr;
std::atomic<long> SimulationEngine::sEventsCompleted{0};
std::atomic<long> SimulationEngine::sEventsTotal{0};

SimulationEngine* SimulationEngine::Instance() {
    if (!fInstance) fInstance = new SimulationEngine();
    return fInstance;
}

void SimulationEngine::DeleteInstance() {
    delete fInstance;
    fInstance = nullptr;
}

SimulationEngine::SimulationEngine() = default;
SimulationEngine::~SimulationEngine() { teardown(); }

void SimulationEngine::setError(const std::string& msg) { fLastError = msg; }

void SimulationEngine::setCommandLine(int argc, char** argv) {
    fArgc = argc;
    fArgv = argv;
}

// ── Progress (lock-free) ─────────────────────────────────────────────────────
void SimulationEngine::RecordEventCompleted() {
    sEventsCompleted.fetch_add(1, std::memory_order_relaxed);
}
long SimulationEngine::EventsCompleted() {
    return sEventsCompleted.load(std::memory_order_relaxed);
}
long SimulationEngine::EventsTotal() {
    return sEventsTotal.load(std::memory_order_relaxed);
}
void SimulationEngine::ResetProgress(long total) {
    sEventsCompleted.store(0, std::memory_order_relaxed);
    sEventsTotal.store(total, std::memory_order_relaxed);
}

// ── Configuration ────────────────────────────────────────────────────────────
bool SimulationEngine::loadConfig(const std::string& path) {
    fConfigPath = path;
    auto* cfg = ConfigManager::Instance();

    G4cout << "Loading configuration: " << path << G4endl;

    // MACROS/SEED must be read from the raw YAML node BEFORE LoadConfig
    // (fRootNode is destroyed by ExtractAllData inside LoadConfig).
    extractMacrosAndSeed();

    if (!cfg->LoadConfig(path)) {
        setError("Failed to load configuration: " + path);
        G4cerr << fLastError << G4endl;
        return false;
    }

    fVisualization = cfg->GetBool("VISUALIZATION", false);
    fConfigLoaded = true;
    return true;
}

bool SimulationEngine::loadConfigText(const std::string& text) {
    const std::string tmpPath = "/tmp/g4care_gui_config.yaml";
    std::ofstream ofs(tmpPath);
    if (!ofs) {
        setError("Failed to write temporary config file: " + tmpPath);
        return false;
    }
    ofs << text;
    ofs.close();
    return loadConfig(tmpPath);
}

// ── MACROS / SEED extraction from the raw YAML node ──────────────────────────
void SimulationEngine::extractMacrosAndSeed() {
    auto* cfg = ConfigManager::Instance();
    fMacros.clear();
    fSeeds.clear();

    try {
        YAML::Node rawConfig = cfg->LoadConfigFile(fConfigPath);

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
                if (!m.file.empty()) fMacros.push_back(std::move(m));
            }
        }
        G4cout << "[MACROS] Loaded " << fMacros.size() << " macros from YAML" << G4endl;
        for (size_t i = 0; i < fMacros.size(); ++i) {
            G4cout << "  [" << i << "] file=" << fMacros[i].file
                   << " stage=" << fMacros[i].stage << G4endl;
        }

        // SEED
        YAML::Node seedNode = rawConfig["SEED"];
        if (seedNode.IsSequence()) {
            if (seedNode.size() > 0 && seedNode[0].IsScalar()) {
                SeedEntry s;
                for (const auto& v : seedNode) {
                    if (v.IsScalar()) s.values.push_back(v.as<long>());
                }
                if (s.values.size() == 2) fSeeds.push_back(std::move(s));
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
                    if (s.values.size() == 2) fSeeds.push_back(std::move(s));
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
            if (s.values.size() == 2) fSeeds.push_back(std::move(s));
        }

        if (!fSeeds.empty()) {
            G4cout << "[SEED] Loaded " << fSeeds.size() << " seed config(s)" << G4endl;
            for (size_t i = 0; i < fSeeds.size(); ++i) {
                G4cout << "  [" << i << "] seeds=[" << fSeeds[i].values[0]
                       << "," << fSeeds[i].values[1] << "]"
                       << " cond=" << fSeeds[i].condition << G4endl;
            }
            // Apply first unconditional seed
            for (const auto& se : fSeeds) {
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
}

// ── Initialization ───────────────────────────────────────────────────────────
bool SimulationEngine::init() {
    if (!fConfigLoaded) {
        setError("Configuration not loaded. Call loadConfig() first.");
        G4cerr << fLastError << G4endl;
        return false;
    }

    auto* cfg = ConfigManager::Instance();

    if (!MaterialManager::Instance()->BuildMaterials()) {
        setError("Fatal: Failed to build custom materials.");
        G4cerr << fLastError << G4endl;
        return false;
    }

    G4RunManager* runManager;
#ifdef G4MULTITHREADED
    if (fVisualization) {
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
    fRunManager = runManager;

    int nThreads = cfg->GetInt("NTHREADS", 1);
    nThreads = std::max(1, nThreads);
#ifdef G4MULTITHREADED
    runManager->SetNumberOfThreads(nThreads);
    G4cout << "Using G4MTRunManager with " << nThreads << " threads." << G4endl;
    // SEED_ONCE_PER_COMMUNICATION: 0 = Geant4 default (re-seed every event,
    // which calls the expensive MixMaxRng::apply_bigskip per event);
    // 1 = seed once per worker per run (faster, documented performance mode,
    // but event reproducibility then depends on the thread count).
    const int seedOnce = cfg->GetInt("SEED_ONCE_PER_COMMUNICATION", 0);
    if (seedOnce > 0) G4MTRunManager::SetSeedOncePerCommunication(1);
    G4cout << "SeedOncePerCommunication = "
           << G4MTRunManager::SeedOncePerCommunication() << G4endl;
#else
    if (nThreads > 1) {
        G4cout << "WARNING: Multithreading requested but Geant4 built without G4MULTITHREADED." << G4endl;
    }
#endif

    fGeomManager = new GeometryManager();
    fGeomManager->LoadCustomGeometryFromConfig();
    fPhysMgr = new PhysicsManager();
    G4VModularPhysicsList* physicsList = fPhysMgr->CreatePhysicsList();

    if (!physicsList) {
        setError("Fatal: cannot create physics list.");
        G4cerr << fLastError << G4endl;
        return false;
    }

    runManager->SetUserInitialization(fGeomManager);
    runManager->SetUserInitialization(static_cast<G4VUserPhysicsList*>(physicsList));
    runManager->SetUserInitialization(new ActionInitialization(fGeomManager, fPhysMgr));

    double decayThreshold = cfg->GetValueWithUnits("DECAY_TIME_THRESHOLD", 0.0);
    if (decayThreshold > 0.0) {
        G4HadronicParameters::Instance()->SetTimeThresholdForRadioactiveDecay(decayThreshold);
        G4cout << "Radioactive decay time threshold set to " << decayThreshold/CLHEP::year << " years" << G4endl;
    } else {
        G4HadronicParameters::Instance()->SetTimeThresholdForRadioactiveDecay(0.0);
        G4cout << "Radioactive decay: NO time threshold (all decays enabled)" << G4endl;
    }

    configureChemistry();

    G4UImanager* UImanager = G4UImanager::GetUIpointer();
    if (fVisualization) {
        UImanager->ApplyCommand("/tracking/storeTrajectory 1");
        G4cout << "[main] Trajectory storage enabled for visualization" << G4endl;
    }

    runManager->Initialize();

    G4UImanager::GetUIpointer()->ApplyCommand(
        "/process/had/rdm/thresholdForVeryLongDecayTime 1.0e+60 year");
    G4cout << "Applied: /process/had/rdm/thresholdForVeryLongDecayTime 1.0e+60 year" << G4endl;

    configureCrossSections();

    fInitialized = true;
    return true;
}

// ── Chemistry setup (UI commands applied BEFORE Initialize) ──────────────────
void SimulationEngine::configureChemistry() {
    auto* cfg = ConfigManager::Instance();
    bool chemEnabled = cfg->GetBool("CHEMISTRY.ENABLE", false);
    if (!chemEnabled) return;

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

// ── User cross-section (BEB) registration, after Initialize ──────────────────
void SimulationEngine::configureCrossSections() {
    auto* cfg = ConfigManager::Instance();
    if (cfg->HasKey("CROSS_SECTION.BEB_FILE")) {
        std::string bebFile = cfg->GetString("CROSS_SECTION.BEB_FILE");
        auto* bebSource = new BEBDataSource("MineralBEB", bebFile);
        auto* electronIoniModel = new UserCrossSectionModel("MineralIoni",
                                                    G4Electron::Electron(),
                                                    "ioni",
                                                    {bebSource});
        fPhysMgr->RegisterUserCrossSection(electronIoniModel,
                                           G4Electron::Electron(),
                                           "eIoni");
    }
}

// ── Running ──────────────────────────────────────────────────────────────────
bool SimulationEngine::run() {
    return runStages(-1, false);
}

bool SimulationEngine::run(int nEvents) {
    if (nEvents < 0) return run();
    return runStages(nEvents, true);
}

// ── Macro execution helper ───────────────────────────────────────────────────
void SimulationEngine::execMacros(const std::string& filterStage) {
    G4UImanager* UImanager = G4UImanager::GetUIpointer();
    G4cout << "[MACROS] stage=" << filterStage << " executing "
           << std::count_if(fMacros.begin(), fMacros.end(),
                [&](const MacroEntry& m){ return m.stage == filterStage || m.stage == "both"; })
           << " macros" << G4endl;
    for (const auto& m : fMacros) {
        if (m.stage == filterStage || m.stage == "both") {
            G4cout << "  execute: " << m.file << G4endl;
            UImanager->ApplyCommand("/control/execute " + m.file);
        }
    }
}

// ── Core run: single stage (preview) or configured stages (hybrid) ───────────
bool SimulationEngine::runStages(int nEvents, bool forceSingleStage) {
    auto* cfg = ConfigManager::Instance();
    G4UImanager* UImanager = G4UImanager::GetUIpointer();

    // Визуализация (настраивается ДО RunAll; SessionStart — ПОСЛЕ RunAll)
    if (fVisualization) {
        fVis = new G4VisExecutive();
        fVis->Initialize();
        fUI = new G4UIExecutive(fArgc, fArgv);

        UImanager->ApplyCommand("/vis/open TSGZB 800x600-0+0");
        // Draw the world + its direct daughters (target, phantom container) at
        // depth 1 and add trajectories. Do NOT descend into the parameterised
        // voxel phantom: even Geant4's "special mesh rendering (surfaces)" has to
        // do a full per-voxel descent to collect material positions, which for a
        // 74M-voxel phantom takes minutes/GBs and stalls the scene BEFORE the
        // trajectory commands run (tracks vanish). The full voxel shape is shown
        // in the Geometry tab (marching-cubes mesh).
        UImanager->ApplyCommand("/vis/scene/create");
        UImanager->ApplyCommand("/vis/scene/add/volume world -1 1");
        UImanager->ApplyCommand("/vis/sceneHandler/attach");
        UImanager->ApplyCommand("/vis/scene/add/trajectories rich");
        UImanager->ApplyCommand("/vis/scene/endOfEventAction accumulate");

        double startTime = cfg->GetValueWithUnits("CHEMISTRY.START_TIME", 1.0 * CLHEP::ps);
        double endTime   = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 1.0 * CLHEP::ns);
        UImanager->ApplyCommand("/control/alias startTime " + std::to_string(startTime / CLHEP::ps));
        UImanager->ApplyCommand("/control/alias endTime " + std::to_string(endTime / CLHEP::ns));

        // Макросы настройки сцены — ДО RunAll
        execMacros("before");
    }

    G4cout << "Running StageManager..." << G4endl;

    fStageMgr = new StageManager(fRunManager);
    if (forceSingleStage) {
        fStageMgr->AddStage(nullptr, nEvents);
    } else {
        fStageMgr->LoadConfiguration(fConfigPath);
    }

    // Compute total events for progress reporting (lock-free counters).
    long total = 0;
    if (forceSingleStage) {
        total = nEvents;
    } else {
        auto stageKeys = cfg->GetSubsections("STAGES");
        if (stageKeys.empty()) {
            total = static_cast<long>(cfg->GetDouble("EVENTS", 1000));
        } else {
            for (const auto& key : stageKeys) {
                total += static_cast<long>(cfg->GetDouble("STAGES." + key + ".EVENTS", 100));
            }
        }
    }
    ResetProgress(total);

    ConvergenceValidator validator;
    const auto runT0 = std::chrono::steady_clock::now();
    fStageMgr->RunAll();
    const auto runT1 = std::chrono::steady_clock::now();
    const double runMs = std::chrono::duration<double, std::milli>(runT1 - runT0).count();
    const double runSec = runMs / 1000.0;
    const unsigned long long nSteps = SteppingAction::GetStepCount();
    const unsigned long long nTracks = TrackingAction::GetTrackCount();
    G4cout << "[SimulationEngine] RunAll: " << static_cast<long>(runMs)
           << " ms for " << total << " events ("
           << (runSec > 0.0 ? total / runSec : 0.0) << " events/s, "
           << (runSec > 0.0 ? nTracks / runSec : 0.0) << " tracks/s, "
           << (runSec > 0.0 ? nSteps / runSec : 0.0) << " steps/s)" << G4endl;

    // GDML export (GEOMETRY.EXPORT.*) — headless only, after the run.
    if (!fVisualization) {
        fGeomManager->ExportGdml();
    }

    auto results = validator.CheckAllVoxels();
    if (!results.empty()) {
        validator.WriteReport("convergence_report.txt", results);
    }

    if (fVisualization) {
        // Макросы после RunAll (если есть stage: after)
        execMacros("after");

        fUI->SessionStart();
        delete fUI; fUI = nullptr;
        delete fVis; fVis = nullptr;
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

    return true;
}

// ── Export ───────────────────────────────────────────────────────────────────
bool SimulationEngine::exportGdml() {
    if (!fGeomManager) {
        setError("Geometry is not initialized.");
        return false;
    }
    fGeomManager->ExportGdml();
    return true;
}

bool SimulationEngine::exportVtk(const std::string& file) {
    auto* cfg = ConfigManager::Instance();
    std::string out = file.empty() ? cfg->GetString("HEATMAP_FILE", "scoring.vtu") : file;

    auto* scoring = AdaptiveScoringManager::Instance();
    if (!scoring || !scoring->IsInitialized()) {
        setError("Scoring manager is not initialized (no scoring run).");
        return false;
    }
    scoring->ExportResults(G4String(out));
    return true;
}

bool SimulationEngine::exportSceneVtk(const std::string& file) {
    if (!fRunManager) {
        setError("Engine is not initialized. Call init() first.");
        return false;
    }

    G4VisExecutive vis;
    vis.Initialize();

    // Strip any extension: G4Vtk's vtp exporter appends ".vtp" to the base name.
    std::string base = file;
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    G4UImanager* ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/vis/open VtkOffscreen");
    // Depth-1 draw: a full descent would enumerate every voxel of the
    // parameterised phantom (and the "surfaces" special-mesh path still does a
    // per-voxel descent), which is minutes/GBs for a 74M-voxel phantom. Depth 1
    // stops at the phantom container box.
    ui->ApplyCommand("/vis/scene/create");
    ui->ApplyCommand("/vis/scene/add/volume world -1 1");
    ui->ApplyCommand("/vis/sceneHandler/attach");
    ui->ApplyCommand("/vis/vtk/export vtp " + base);
    ui->ApplyCommand("/vis/disable");

    return true;
}

// ── Lifecycle / reset ────────────────────────────────────────────────────────
void SimulationEngine::reset() {
    teardown();

    // Clear the process-wide singletons so a subsequent loadConfig()+init()
    // starts from a clean slate. NOTE: Geant4 static managers (material store,
    // state manager) are not fully reset in-process — a guaranteed clean re-init
    // is achieved by re-exec of the sidecar.
    ConfigManager::DeleteInstance();
    MaterialManager::DeleteInstance();
    BeamAnalysis::DeleteInstance();
    ExpressionEvaluator::ClearAll();

    cleanupChemStateFiles();

    fMacros.clear();
    fSeeds.clear();
    fConfigLoaded = false;
    fInitialized = false;
    fVisualization = false;
    fLastError.clear();
    fConfigPath.clear();
    ResetProgress(0);
}

void SimulationEngine::teardown() {
    delete fStageMgr; fStageMgr = nullptr;
    delete fVis; fVis = nullptr;
    delete fUI; fUI = nullptr;

    // fRunManager owns fGeomManager, the physics list and the ActionInitialization.
    delete fRunManager; fRunManager = nullptr;
    fGeomManager = nullptr;

    // PhysicsManager is owned here (ActionInitialization held a raw pointer).
    delete fPhysMgr; fPhysMgr = nullptr;

    fInitialized = false;
}

void SimulationEngine::cleanupChemStateFiles() {
    // Remove hybrid-chemistry state files so a subsequent run does not load
    // stale molecule/RNG state (StageManager writes these with fixed names).
    std::remove("chem_stage_0.dat");
    std::remove("chem_stage_1.dat");
    std::remove("chem_stage_2.dat");
    std::remove("chem_stage_3.dat");
    std::remove("chem_stage_4.dat");
    std::remove("rng_sbs.dat");
    std::remove("sbs_molecules.bin");
}
