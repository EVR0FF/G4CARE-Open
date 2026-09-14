#ifndef RUNACTION_HH
#define RUNACTION_HH

//==============================================================================
// G4CARE
// @file    RunAction.hh
// @brief   Geant4 run-level user action (Begin/End of run)
// @details Inherits G4UserRunAction and manages per-run initialization
//   and finalization. Handles ROOT file creation, ntuple booking,
//   chemistry state save/load, G-factor scoring column setup,
//   and merges thread-local ChemVoxel data via the Run::Merge mechanism.
//   Supports external file management for multi-stage simulations
//   driven by StageManager.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4UserRunAction.hh"
#include "ConfigManager.hh"
#include "ObjectManager.hh"
#include "TrackingAction.hh"
#include "GeometryManager.hh"
#include "ChemistryExtractor.hh"
#include <memory>

class PrimaryGeneratorAction;

class RunAction : public G4UserRunAction {
public:
    /// @brief Constructor
    /// @param primaryGen     Pointer to PrimaryGeneratorAction (may be nullptr)
    /// @param geomManager    Pointer to GeometryManager
    /// @param objMgr         Pointer to ObjectManager
    /// @param trackingAction Pointer to TrackingAction (may be nullptr)
    RunAction(PrimaryGeneratorAction* primaryGen, GeometryManager* geomManager,
              ObjectManager* objMgr, TrackingAction* trackingAction = nullptr);

    /// @brief Destructor
    virtual ~RunAction();

    /// @brief Create a custom G4Run object (Run with ChemVoxel support)
    virtual G4Run* GenerateRun() override;

    /// @brief Called at the beginning of each run
    virtual void BeginOfRunAction(const G4Run*) override;

    /// @brief Called at the end of each run
    virtual void EndOfRunAction(const G4Run*) override;

    /// @brief Enable/disable external file management for multi-stage simulations
    /// @param ext If true, the ROOT file is managed externally by StageManager
    void SetExternalFileManagement(bool ext) { fExternalFileMgmt = ext; }

    /// @brief Set chemistry state save/load flags for multi-stage simulations
    /// @param save If true, save chemistry state at end of run
    /// @param load If true, load chemistry state at beginning of run
    void SetChemFlags(bool save, bool load) {
        fSaveChemistryState = save;
        fLoadChemistryState = load;
    }

private:
    bool fEnabled;
    int fVerbose;
    PrimaryGeneratorAction* fPrimaryGenerator;
    ObjectManager* fObjMgr;
    TrackingAction* fTrackingAction;
    GeometryManager* fGeometryManager;
    bool fSaveChemistryState = false;
    bool fLoadChemistryState = false;
    bool fExternalFileMgmt = false;
    G4int fGfactorNtupleId = -1;

    /// @brief Cached target mass from BeginOfRunAction [kg]
    double fCachedTargetMassKg = -1.0;

    /// @name Chemistry scoring vectors
    /// @{
    std::vector<double> fChemNc, fChemNt, fChemGc, fChemGt, fChemCharge;
    double fChemEdepEV = 0.0;
    double fChemEndNS = 0.0;
    size_t fChemNSpecies = 0;
    /// @}
};

#endif