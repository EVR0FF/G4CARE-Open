//==============================================================================
// G4CARE
// @file    RunAction.cc
// @brief   Implementation of Geant4 run-level user action
// @details Implements BeginOfRunAction and EndOfRunAction for G4CARE.
//   Handles ROOT file creation, ntuple booking, target positioning,
//   chemistry species registration, chemistry state save/load,
//   G-factor scoring setup, G4MoleculeCounter reset, EXPRS script
//   execution with chemistry vectors (Nc, Nt, Gc, Gt, charge), VTK
//   export of adaptive scoring grids, and weight-window statistics
//   saving in collect mode.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "RunAction.hh"
#include "PrimaryGeneratorAction.hh"
#include "GeometryManager.hh"
#include "SensitiveDetector.hh"
#include "G4SDManager.hh"
#include "G4HCtable.hh"
#include "G4Run.hh"
#include "G4Threading.hh"
#include "G4UImanager.hh"
#include "BeamAnalysis.hh"
#include <map>
#include <chrono>
#include <iomanip>
#include "PositionSampler.hh"
#include "G4ios.hh"
#include "G4Timer.hh"
#include "TrackingAction.hh"
#include "MoleculeIO.hh"
#include "G4Molecule.hh"
#include "G4MoleculeTable.hh"
#include "G4DNAChemistryManager.hh"
#include "Randomize.hh"
#include "Run.hh"
#include "AdaptiveScoringManager.hh"
#include "ExprsManager.hh"
#include "G4TransportationManager.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4NistManager.hh"
#include "G4Material.hh"
#include "SteppingAction.hh"
#include "ITSteppingAction.hh"
#include "ITTrackingInteractivity.hh"
#include "G4Scheduler.hh"
#include "TFile.h"
#include "TTree.h"

// G4MoleculeCounter
#include "G4MoleculeCounter.hh"
#include "G4MoleculeCounterManager.hh"
#include "G4MolecularConfiguration.hh"
#include "G4H2O.hh"

namespace {

    /// @brief Get the number of molecules of a species at a given global time
    ///
    /// Looks up the species in the GfactorCounter molecule counter.
    /// Maps short species names (OH, H, e_aq, etc.) to exact Geant4-DNA
    /// molecule names and queries the counter for the molecule count
    /// at the specified time.
    /// @param speciesName Short species name (e.g., "OH", "e_aq")
    /// @param globalTime  Global time at which to query
    /// @return Number of molecules, or 0 if not found
    G4int GetNbMoleculesFromCounter(const std::string& speciesName, G4double globalTime)
    {
        auto counters = G4MoleculeCounterManager::Instance()->GetMoleculeCounters("GfactorCounter");
        const G4MoleculeCounter* counter = nullptr;
        if (!counters.empty()) {
            counter = dynamic_cast<const G4MoleculeCounter*>(counters.front());
        }
        if (!counter) return 0;

        static const std::map<std::string, std::string> kExactName = {
            {"OH",    "\u00b0OH^0"},
            {"H",     "H^0"},
            {"e_aq",  "e_aq^-1"},
            {"H3O",   "H3O^1"},
            {"H2O2",  "H2O2^0"},
            {"H2",    "H_2^0"},
            {"HO2",   "HO2^0"},
            {"O2",    "O2^0"},
            {"OHm",   "OH^-1"},
            {"O",     "O^0"},
            {"O-",    "O^-1"},
            {"H2Op",  "H2O^+"}
        };

        auto it = kExactName.find(speciesName);
        if (it == kExactName.end()) return 0;
        const std::string& exactName = it->second;

        auto& cmap = counter->GetCounterMap();
        for (auto const& entry : cmap) {
            if (entry.first.GetMolecule()->GetName() == exactName) {
                return counter->GetNbMoleculesAtTime(entry.first, globalTime);
            }
        }
        return 0;
    }
}

/// @brief Constructor
///
/// Reads RUNACTION_ENABLE and RUNACTION_VERBOSE from config,
/// enables ROOT ntuple merging in MT mode, and initializes BeamAnalysis.
/// @param primaryGen     Pointer to PrimaryGeneratorAction (may be nullptr)
/// @param geomManager    Pointer to GeometryManager
/// @param objMgr         Pointer to ObjectManager
/// @param trackingAction Pointer to TrackingAction (may be nullptr)
RunAction::RunAction(PrimaryGeneratorAction* primaryGen, GeometryManager* geomManager,
                     ObjectManager* objMgr, TrackingAction* trackingAction)
    : G4UserRunAction(), fEnabled(true), fVerbose(0), fPrimaryGenerator(primaryGen),
      fGeometryManager(geomManager), fObjMgr(objMgr), fTrackingAction(trackingAction)
{
    fEnabled = ConfigManager::Instance()->GetBool("RUNACTION_ENABLE", true);
    fVerbose = ConfigManager::Instance()->GetInt("RUNACTION_VERBOSE", 0);

    auto* man = G4AnalysisManager::Instance();
    if (G4Threading::IsMultithreadedApplication()) {
        man->SetNtupleMerging(true);
    }
    man->SetDefaultFileType("root");
    BeamAnalysis::Instance()->Initialize();
}

/// @brief Destructor: cleans up PositionSampler caches
RunAction::~RunAction() {
    PositionSampler::Cleanup();
}

/// @brief Create a custom G4Run object (Run with ChemVoxel support)
/// @return New Run instance
G4Run* RunAction::GenerateRun() {
    return new Run();
}

/// @brief Called at the beginning of each run
///
/// Invalidates thread caches, repositions target if needed, opens
/// the ROOT output file (unless externally managed), registers
/// chemistry species from configuration, creates BeamAnalysis trees,
/// resets molecule counters, optionally loads chemistry state from
/// disk, applies weight-window weights, sets detector registry,
/// resets ExprsManager blocks, resets chemistry accumulation, and
/// computes the cached target mass.
/// @param run Pointer to the current G4Run
void RunAction::BeginOfRunAction(const G4Run* run) {
    auto* cfg = ConfigManager::Instance();
    cfg->InvalidateThreadCache();

    auto* geomMgr = fGeometryManager;
    auto* scoringMgr = AdaptiveScoringManager::Instance();

    if (geomMgr) {
        G4double x = cfg->GetValueWithUnits("TARGET_POS_X", 0.0);
        G4double y = cfg->GetValueWithUnits("TARGET_POS_Y", 0.0);
        G4double z = cfg->GetValueWithUnits("TARGET_POS_Z", 0.0);
        G4ThreeVector targetPos(x, y, z);

        if (G4Threading::IsMasterThread()) {
            G4ThreeVector currentPos = geomMgr->GetTargetPosition();
            G4double dist = (targetPos - currentPos).mag();
            if (dist > 1.0*mm) {
                geomMgr->MoveTarget(targetPos);
                if (scoringMgr) scoringMgr->Reinitialize();
            } else {
                if (scoringMgr) scoringMgr->Reset();
            }
        } else {
            if (scoringMgr) {
                scoringMgr->TryAttachOnWorker();
                scoringMgr->Reset();
            }
        }
    }

    SteppingAction::InitializeDoseControl(geomMgr);

    if (!fEnabled) return;

    auto* beam = BeamAnalysis::Instance();
    auto* man = G4AnalysisManager::Instance();

    if (!fExternalFileMgmt) {
        std::string outFile = cfg->GetString("OUTPUT_FILE", "output.root");
        man->SetFileName(outFile);
        G4bool ok = man->OpenFile(outFile);
    }

    bool chemEnabled = cfg->GetBool("CHEMISTRY.ENABLE", false);
    {
        auto* bm = BeamAnalysis::Instance();
        auto* reg = bm ? bm->GetChemSpeciesRegistry() : nullptr;

        if (chemEnabled && reg && reg->Size() == 0) {
            auto speciesList = cfg->GetStringVector("CHEMISTRY.SPECIES");
            if (speciesList.empty()) speciesList = cfg->GetStringVector("CHEMISTRY.CHEM_LIST");
            static const std::map<std::string, std::string> kCanonical = {
                {"eaq","e_aq"}, {"aq","e_aq"}, {"e_aq","e_aq"},
                {"OH","OH"}, {"H","H"}, {"H3O","H3O"}, {"H2O2","H2O2"},
                {"H2","H2"}, {"HO2","HO2"}, {"O2","O2"}, {"O2-","O2-"},
                {"O","O"}, {"O-","O-"}, {"OHm","OHm"}, {"H2Op","H2O^+"}
            };
            bool useAll = (!speciesList.empty() && speciesList[0] == "All");
            if (useAll) {
                speciesList.clear();
                for (const auto& pair : kCanonical)
                    if (pair.first == pair.second) speciesList.push_back(pair.second);
            }
            for (const auto& mol : speciesList) {
                auto it = kCanonical.find(mol);
                std::string cname = (it != kCanonical.end()) ? it->second : mol;
                reg->Register(cname);
            }
        }

        if (chemEnabled) {
            size_t nSpecies = reg ? reg->Size() : 0;
            if (nSpecies > 0) {
                size_t nThreads = G4Threading::GetNumberOfRunningWorkerThreads();
                if (nThreads == 0) nThreads = 1;
                ChemistryExtractor::InitGlobalAccumulator(nSpecies, nThreads);
                ChemistryExtractor::GlobalReset();
            }
        }
    }

    beam->CreateTrees();
    beam->BeginRun();

    G4MoleculeCounterManager::Instance()->ResetCounters();

    if (chemEnabled && G4DNAChemistryManager::GetInstanceIfExists() != nullptr) {
        G4DNAChemistryManager::GetInstanceIfExists()->BeginOfRunAction(run);
    }

    if (fLoadChemistryState) {
        CLHEP::HepRandom::restoreEngineStatus("rng_sbs.dat");
        auto records = MoleculeIO::ReadBinary("sbs_molecules.bin");
        if (!records.empty()) {
            auto* chemMgr = G4DNAChemistryManager::Instance();
            auto* molTable = G4MoleculeTable::Instance();
            auto* bm = BeamAnalysis::Instance();
            auto* speciesReg = bm ? bm->GetChemSpeciesRegistry() : nullptr;
            for (const auto& rec : records) {
                G4String molName;
                if (speciesReg) molName = G4String(speciesReg->GetValue(rec.speciesIndex));
                if (molName.empty()) continue;
                auto* def = molTable->GetMoleculeDefinition(molName, false);
                if (!def) continue;
                auto* mol = new G4Molecule(def);
                G4ThreeVector pos(rec.x * CLHEP::mm, rec.y * CLHEP::mm, rec.z * CLHEP::mm);
                chemMgr->PushMolecule(std::unique_ptr<G4Molecule>(mol), rec.time * CLHEP::ps, pos, 0);
            }
        }
    }

    auto* wwm = WeightWindowManager::Instance();
    if (wwm && wwm->IsEnabled() && wwm->GetAutoMode() != "collect") {
        wwm->LoadWeights();
        wwm->ApplyWeights();
    }

    if (fGeometryManager) {
        BeamAnalysis::Instance()->SetDetectorRegistry(fGeometryManager->GetDetectorRegistry());
    }

    {
        auto* sm = ExprsManager::Instance();
        if (!sm->GetBlocks().empty()) sm->Reset();
    }

    {
        auto* bm = BeamAnalysis::Instance();
        if (bm) {
            auto* extractor = bm->GetDataExtractor();
            if (extractor) extractor->GetChemistryExtractor().ResetAccumulation();
        }
    }

    static double sTargetMassKg = -1.0;
    if (sTargetMassKg <= 0.0) {
        double rmax_mm = cfg->GetValueWithUnits("GEOMETRY.OBJECTS.target.DIMENSIONS[1]", 0.3e-3*mm) / mm;
        double volume_cm3 = (4.0/3.0) * CLHEP::pi * std::pow(rmax_mm * 0.1, 3.0);
        sTargetMassKg = 1.0 * volume_cm3 / 1000.0;
    }
    fCachedTargetMassKg = sTargetMassKg;
}

/// @brief Called at the end of each run
///
/// Performs end-of-run finalization: chemistry state save, ROOT file
/// write/close (unless externally managed), chemistry vector collection
/// (Nc/Nt/Gc/Gt/charge per species from global ChemVoxel merge),
/// VTK export of adaptive scoring grid results, EXPRS script execution
/// with species vectors and SCRIPT constants, and weight-window
/// statistics saving in collect mode.
/// Only the master thread performs collection and export.
/// @param run Pointer to the current G4Run
void RunAction::EndOfRunAction(const G4Run* run) {
    if (!fEnabled) return;

    if (G4DNAChemistryManager::GetInstanceIfExists() != nullptr) {
        G4DNAChemistryManager::GetInstanceIfExists()->EndOfRunAction(run);
    }

    auto* cfg = ConfigManager::Instance();
    auto* man = G4AnalysisManager::Instance();

    if (!fExternalFileMgmt) {
        man->Write();
        man->CloseFile();
    }

    if (!G4Threading::IsMasterThread()) return;

    // ── Chemistry: collect per-species vectors ──
    {
        auto* bm = BeamAnalysis::Instance();
        auto* reg = bm ? bm->GetChemSpeciesRegistry() : nullptr;
        size_t nSpecies = reg ? reg->Size() : 0;
        bool chemEnabled = cfg->GetBool("CHEMISTRY.ENABLE", false);
        double endTime = cfg->GetValueWithUnits("CHEMISTRY.END_TIME", 10.0 * CLHEP::ns);

        ChemVoxel agg;
        ChemistryExtractor::GlobalMerge(agg);
        double edep_MeV = agg.energyDeposit;

        static const std::map<std::string, double> kCharge = {
            {"e_aq",-1}, {"H3O",+1}, {"OHm",-1}, {"H2Op",+1},
            {"O2-",-1}, {"O-",-1}
        };

        if (nSpecies > 0) {
            double totalCharge = 0.0;
            G4cout << "[RunAction] Chemistry @ t=" << endTime/CLHEP::ns
                   << " ns, edep=" << edep_MeV << " MeV:" << G4endl;
            G4cout << "  " << std::left << std::setw(8) << "Species"
                   << std::right << std::setw(12) << "N_created"
                   << std::setw(12) << "N_at_end"
                   << std::setw(8) << "charge" << G4endl;
            for (size_t i = 0; i < nSpecies; ++i) {
                std::string sp = std::string(reg->GetValue(i));
                G4int nCreated = static_cast<G4int>(agg[i]);
                G4int nAtT = GetNbMoleculesFromCounter(sp, endTime);
                auto qit = kCharge.find(sp);
                double q = (qit != kCharge.end()) ? qit->second : 0.0;
                G4cout << "  " << std::left << std::setw(8) << sp
                       << std::right << std::setw(12) << nCreated
                       << std::setw(12) << nAtT
                       << std::setw(8) << std::fixed << std::setprecision(1) << (nAtT * q) << G4endl;
                totalCharge += nAtT * q;
            }
            G4cout << "[RunAction] TOTAL charge (survival) = " << totalCharge
                   << (std::abs(totalCharge) < 0.001 ? " (NEUTRAL)" : " (VIOLATION!)")
                   << G4endl;
        }
        if (chemEnabled && nSpecies > 0) {
            double edep_eV = agg.energyDeposit / CLHEP::eV;
            fChemNc.resize(nSpecies);
            fChemNt.resize(nSpecies);
            fChemGc.resize(nSpecies);
            fChemGt.resize(nSpecies);
            fChemCharge.resize(nSpecies);
            fChemEdepEV = edep_eV;
            fChemEndNS = endTime / CLHEP::ns;
            fChemNSpecies = nSpecies;
            for (size_t i = 0; i < nSpecies; ++i) {
                std::string sp = std::string(reg->GetValue(i));
                fChemNc[i] = static_cast<double>(agg[i]);
                fChemNt[i] = static_cast<double>(GetNbMoleculesFromCounter(sp, endTime));
                fChemGc[i] = (edep_eV > 0.0) ? (fChemNc[i] * 100.0 / edep_eV) : 0.0;
                fChemGt[i] = (edep_eV > 0.0) ? (fChemNt[i] * 100.0 / edep_eV) : 0.0;
                auto qit = kCharge.find(sp);
                fChemCharge[i] = (qit != kCharge.end()) ? qit->second : 0.0;
            }
        }
    }

    // ── VTK export of adaptive scoring grid ──
    if (fGeometryManager) {
        G4VPhysicalVolume* world = fGeometryManager->GetWorldVolume();
        if (!world) {
            auto* transMgr = G4TransportationManager::GetTransportationManager();
            if (transMgr) world = transMgr->GetNavigatorForTracking()->GetWorldVolume();
        }
        if (world) {
            auto* scoringMgr = AdaptiveScoringManager::Instance();
            if (scoringMgr && scoringMgr->IsInitialized() && scoringMgr->GetScorer()) {
                scoringMgr->MergeAllWorkerData();
                std::string vtkFile = "adaptive_grid_run_" + std::to_string(run->GetRunID()) + ".vtk";
                scoringMgr->ExportResults(vtkFile);
            }
        }
    }

    // ── EXPRS: execute SCRIPT blocks ──
    {
        auto* sm = ExprsManager::Instance();
        const auto& blocks = sm->GetBlocks();
        if (!blocks.empty()) {
            sm->Merge();
            std::map<std::string, double> lastValues;
            G4cout << std::scientific << std::setprecision(15);
            G4cout << "=== EXPRS: Executing " << blocks.size() << " block(s) ===" << G4endl;
            ExpressionEvaluator eval;
            for (const auto& block : blocks) {
                auto values = sm->GetBlockValues(block);
                for (const auto& [cname, cval] : block.scriptConstants) {
                    values[cname] = cval;
                }

                // ── Chemistry species vectors ──
                auto* bm = BeamAnalysis::Instance();
                auto* reg = bm ? bm->GetChemSpeciesRegistry() : nullptr;
                size_t chem_n = reg ? reg->Size() : 0;
                if (chem_n > 0) {
                    ExpressionEvaluator::SetGlobalSpeciesRegistry(reg);
                    for (const auto& compiledLine : block.compiledScripts) {
                        if (compiledLine) {
                            eval.RegisterChemVectors(compiledLine.get(), chem_n, reg);
                            compiledLine->chemNc.assign(fChemNc.begin(), fChemNc.end());
                            compiledLine->chemNt.assign(fChemNt.begin(), fChemNt.end());
                            compiledLine->chemGc.assign(fChemGc.begin(), fChemGc.end());
                            compiledLine->chemGt.assign(fChemGt.begin(), fChemGt.end());
                            compiledLine->chemCharge.assign(fChemCharge.begin(), fChemCharge.end());
                        }
                    }
                    G4cout << "[RunAction] ChemVec: n=" << chem_n
                           << " edep_eV=" << fChemEdepEV
                           << " Nc[0]=" << (fChemNc.empty() ? -1 : fChemNc[0])
                           << " Nt[0]=" << (fChemNt.empty() ? -1 : fChemNt[0])
                           << G4endl;
                    values["chem_n_species"] = static_cast<double>(chem_n);
                    values["chem_edep_eV"]   = fChemEdepEV;
                    values["chem_end_ns"]    = fChemEndNS;
                }

                for (const auto& compiledLine : block.compiledScripts) {
                    eval.Execute(compiledLine.get(), values);
                    if (compiledLine) {
                        for (const auto& [vname, vptr] : compiledLine->varPtrs) {
                            if (vptr) {
                                values[vname] = *vptr;
                                ConfigManager::SetRuntime(vname, *vptr);
                            }
                        }
                    }
                }
            }
            // ── Write user variables to ROOT ──
            if (!lastValues.empty()) {
                auto* bm = BeamAnalysis::Instance();
                if (bm) bm->Fill("exprs_user", lastValues);
            }
            G4cout << "=== EXPRS: Done ===" << G4endl;
            sm->ExecuteRunScripts();
            sm->Reset();
        }
    }

    auto* wwm = WeightWindowManager::Instance();
    if (wwm && wwm->IsEnabled() && wwm->GetAutoMode() == "collect") {
        wwm->SaveStatistics();
    }
}