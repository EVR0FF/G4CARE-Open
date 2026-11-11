//==============================================================================
// G4CARE
// @file    SimulationEngine.hh
// @brief   Top-level simulation engine: config -> init -> run -> export.
//
// @details
//   Encapsulates the whole application lifecycle that previously lived in
//   src/main.cc: YAML configuration loading (including MACROS/SEED extraction
//   from the raw YAML node), material building, Geant4 run-manager creation
//   (sequential vs MT), geometry/physics/action wiring, chemistry setup,
//   multi-stage runs (StageManager), GDML/VTK export and heatmap handling.
//
//   This class is a process-wide singleton. It is driven either by the thin
//   CLI wrapper (G4CARE) or by the JSON-RPC sidecar (g4care-engine --rpc).
//
//   Progress accounting is lock-free (std::atomic) so worker threads can
//   increment the completed-event counter without any mutex/lock.
//
// @author  I. I. Everstov  @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-10-07  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef SIMULATION_ENGINE_HH
#define SIMULATION_ENGINE_HH

#include <atomic>
#include <functional>
#include <string>
#include <vector>

class G4RunManager;
class G4VisManager;
class G4UIExecutive;
class G4VModularPhysicsList;
class GeometryManager;
class PhysicsManager;
class StageManager;

/// @brief Top-level simulation engine (process-wide singleton).
class SimulationEngine {
public:
    static SimulationEngine* Instance();
    static void DeleteInstance();

    bool loadConfig(const std::string& path);
    /// @brief Load configuration from in-memory YAML text (written to a temp file).
    bool loadConfigText(const std::string& text);
    bool init();

    /// @brief Run as configured: STAGES (hybrid chemistry) or root EVENTS.
    bool run();
    /// @brief Preview run: a single plain stage with exactly @a nEvents events.
    bool run(int nEvents);

    void reset();
    void setVisualization(bool on) { fVisualization = on; }
    void setCommandLine(int argc, char** argv);

    bool exportGdml();
    bool exportVtk(const std::string& file = "");
    /// @brief Export the Geant4 scene (geometry) to a .vtk via the G4Vtk driver.
    bool exportSceneVtk(const std::string& file);

    bool isInitialized() const { return fInitialized; }
    bool isVisualization() const { return fVisualization; }
    const std::string& lastError() const { return fLastError; }
    const std::string& configPath() const { return fConfigPath; }

    G4RunManager* runManager() const { return fRunManager; }
    GeometryManager* geometryManager() const { return fGeomManager; }

    // ── Progress (lock-free) ───────────────────────────────────────────────
    static void RecordEventCompleted();
    static long EventsCompleted();
    static long EventsTotal();
    static void ResetProgress(long total);

private:
    SimulationEngine();
    ~SimulationEngine();
    SimulationEngine(const SimulationEngine&) = delete;
    SimulationEngine& operator=(const SimulationEngine&) = delete;

    static SimulationEngine* fInstance;
    static std::atomic<long> sEventsCompleted;
    static std::atomic<long> sEventsTotal;

    std::string fConfigPath;
    bool fConfigLoaded = false;
    bool fVisualization = false;
    bool fInitialized = false;
    std::string fLastError;
    int fArgc = 0;
    char** fArgv = nullptr;

    struct MacroEntry { std::string file, stage, condition; };
    struct SeedEntry  { std::vector<long> values; std::string condition; };
    std::vector<MacroEntry> fMacros;
    std::vector<SeedEntry>  fSeeds;

    G4RunManager*   fRunManager = nullptr;
    GeometryManager* fGeomManager = nullptr;
    PhysicsManager*  fPhysMgr = nullptr;
    StageManager*    fStageMgr = nullptr;
    G4VisManager*    fVis = nullptr;
    G4UIExecutive*   fUI = nullptr;

    void setError(const std::string& msg);
    void extractMacrosAndSeed();
    void configureChemistry();
    void configureCrossSections();
    void execMacros(const std::string& stage);
    void teardown();
    void cleanupChemStateFiles();
    bool runStages(int nEvents, bool forceSingleStage);
};

#endif // SIMULATION_ENGINE_HH
