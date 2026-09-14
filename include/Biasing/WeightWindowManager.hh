//==============================================================================
// G4CARE
// @file    WeightWindowManager.hh
// @brief   Singleton manager for weight-window biasing: configuration
//          loading, process registration, weight computation, and parallel
//          world configuration.
// @details Reads weight-window configuration (manual, GDML, or auto mode),
//   registers the G4WeightWindowProcess for requested particles, supports
//   per-particle algorithm parameters (upper limit, survival, max splits),
//   and can compute weights from accumulated statistics.
//
//   Configuration keys read: WEIGHT_WINDOW.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef WEIGHT_WINDOW_MANAGER_HH
#define WEIGHT_WINDOW_MANAGER_HH

#include "globals.hh"
#include "G4String.hh"
#include "RegularGrid.hh"
#include <vector>
#include <map>

class G4VModularPhysicsList;
class WeightWindowWorld;

/// @brief Singleton controlling weight-window biasing.
class WeightWindowManager {
public:
    /// @brief Returns the thread-local singleton instance.
    static WeightWindowManager* Instance();
    /// @brief Initializes the manager from a configuration file.
    static void Initialize(const std::string& configPath);

    /// Registers G4WeightWindowProcess for all requested particles.
    void RegisterProcesses(G4VModularPhysicsList* physList);
    /// Loads pre-computed weights into the grid.
    void LoadWeights();
    /// Loads statistics and computes weights via the configured formula.
    void LoadStatisticsAndComputeWeights();
    /// Saves accumulated statistics for post-processing.
    void SaveStatistics();
    /// Configures the parallel world geometry from the grid or GDML file.
    void ConfigureParallelWorld(WeightWindowWorld* world) const;
    /// Applies the computed weight-window parameters to the process.
    void ApplyWeights();
    /// Sets up scoring infrastructure for weight-window statistics.
    void SetupScoring();

    const RegularGrid<double>& GetGrid() const { return fGrid; }
    bool IsEnabled() const { return fEnabled; }
    const std::string& GetAutoMode() const { return fAuto.mode; }

private:
    WeightWindowManager();
    explicit WeightWindowManager(const WeightWindowManager& other);
    ~WeightWindowManager() = default;

    /// Parses the weight-window configuration YAML section.
    void ReadConfiguration(const std::string& configPath);

    /// Meshing strategy.
    enum class MeshType { kNone, kManual, kGdml, kAuto };

    /// Manual mesh parameters (loaded from YAML or file).
    struct ManualMesh {
        G4int nx, ny, nz;
        G4double minX, minY, minZ, maxX, maxY, maxZ;
        std::vector<double> weights;
        std::string weightsFile;
    };
    /// Auto-computed mesh parameters (from statistics).
    struct AutoMesh {
        G4int nx, ny, nz;
        G4double minX, minY, minZ, maxX, maxY, maxZ;
        std::string mode;
        std::string statFile;
        std::string weightFormula;
    };
    /// Per-particle weight-window algorithm parameters.
    struct ParticleAlgorithmParams {
        double upperLimitFactor;
        double survivalFactor;
        int maxNumberOfSplits;
    };

    bool fEnabled;
    MeshType fMeshType;
    ManualMesh fManual;
    AutoMesh fAuto;
    std::string fGdmlFile;
    std::vector<G4String> fParticles;
    double fUpperLimitFactor, fSurvivalFactor;
    int fMaxNumberOfSplits;
    std::string fWeightFormula;
    std::map<G4String, ParticleAlgorithmParams> fParticleOverrides;

    RegularGrid<double> fGrid;
    bool fWeightsApplied;

    /// Returns parameters for a given particle, with per-particle overrides.
    ParticleAlgorithmParams GetParticleParams(const G4String& particleName) const;
    /// Checks whether a particle has specific override parameters.
    bool HasParticleOverride(const G4String& particleName) const;

    static G4ThreadLocal WeightWindowManager* fgInstance;
    static WeightWindowManager* fgMasterInstance;
};

#endif
