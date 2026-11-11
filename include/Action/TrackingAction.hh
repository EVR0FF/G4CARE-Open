//==============================================================================
//
// G4CARE
//
// @file    TrackingAction.hh
// @brief   Per-track action: ion production recording, secondary NTuple
//          fill, and track-level ExprTK script execution.
//
// @details
//   Implements G4UserTrackingAction.
//
//   PreUserTrackingAction() fills "secondary" and (for ions)
//   "ion_production" NTuple trees and collects isotope data for the
//   "activation" table.
//
//   PostUserTrackingAction() executes track-level ExprTK scripts via
//   ScriptsManager when the track ends in a recognised logical volume.
//
//   Configuration keys read: none (uses GeometryManager-stored volumes
//   and active SCRIPT.scripts from ConfigManager).
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

#ifndef TRACKINGACTION_HH
#define TRACKINGACTION_HH

#include "G4UserTrackingAction.hh"
#include "globals.hh"
#include "G4LogicalVolume.hh"  

#include <atomic>
#include <unordered_map>

class BeamAnalysis;

/// @brief Per-track action for ion recording, secondary NTuple, and ExprTK.
///
/// At track start, records data in "secondary" and "ion_production" NTuple
/// trees and accumulates isotope information.  At track end, executes
/// track-level ExprTK scripts for volumes with active SCRIPT.scripts.
class TrackingAction : public G4UserTrackingAction {
public:
    TrackingAction();
    virtual ~TrackingAction() = default;

    /// @brief Called before a new track is processed.
    ///        Fills "secondary" and "ion_production" NTuples, collects
    ///        isotope data for the "activation" NTuple.
    virtual void PreUserTrackingAction(const G4Track* track) override;

    /// @brief Called after a track has finished.
    ///        Executes track-level ExprTK scripts if SCRIPT.scripts exist.
    virtual void PostUserTrackingAction(const G4Track* track) override;

    /// @brief Total number of tracks across all threads (lock-free counter).
    static unsigned long long GetTrackCount() { return sTrackCount.load(std::memory_order_relaxed); }

    /// @brief Reset the track counter (called from RunAction::BeginOfRunAction).
    static void ResetTrackCount() { sTrackCount.store(0, std::memory_order_relaxed); }

private:
    // Lock-free track counter (perf benchmarking: tracks/s).
    static std::atomic<unsigned long long> sTrackCount;

    /// @brief Cached per-volume information (legacy, used by ObjectManager).
    struct VolumeCache {
        bool recordIon = false;
        G4String volumeName;
    };
    
    BeamAnalysis* fBeamAnalysis = nullptr;
    bool fIonProductionEnabled = false;
    bool fSaveSecondaries = false;
    double fSecondaryMinEnergy = 0.0;

    /// @brief Per-volume metadata for ion production recording.
    struct VolumeInfo {
        bool recordIon;
        G4String volumeName;
    };
    std::unordered_map<const G4LogicalVolume*, VolumeInfo> fVolumeCache;
};

#endif