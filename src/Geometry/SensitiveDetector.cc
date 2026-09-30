//==============================================================================
// G4CARE
// @file    SensitiveDetector.cc
// @brief   Implementation of sensitive detector for hit collection
// @details Implements the G4VSensitiveDetector interface to record
//   energy deposits in sensitive volumes as Hit objects. Each Hit
//   stores edep, global time, global position, detector ID, and
//   track ID. Hits are only recorded for steps with positive edep
//   and a valid detector ID. The hits collection is created per
//   event in Initialize() and cleared automatically by Geant4.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifdef Hit
#undef Hit
#endif
#include "Hit.hh"
#include "SensitiveDetector.hh"
#include "BeamAnalysis.hh"
#include "G4RunManager.hh"
#include "G4Event.hh"
#include "DetectorRegistry.hh"

/// @brief Constructor
///
/// Registers the hits collection name with Geant4 and initializes
/// the detector ID to -1 (invalid).
/// @param name Name of the sensitive detector / hits collection
SensitiveDetector::SensitiveDetector(const G4String& name)
    : G4VSensitiveDetector(name), fDetectorName(name), fHitsCollection(nullptr), fDetectorID(-1) {
    collectionName.insert("HitsCollection_" + name);
}

/// @brief Destructor
SensitiveDetector::~SensitiveDetector() {}

/// @brief Clone for multi-threaded operation
///
/// Creates a new SensitiveDetector with the same name and collection
/// name, but with a new per-thread hits collection.
/// @return New SensitiveDetector instance
G4VSensitiveDetector* SensitiveDetector::Clone() const {
    auto* clone = new SensitiveDetector(SensitiveDetectorName);
    clone->collectionName = collectionName;
    return clone;
}

/// @brief Initialize hits collection for the current event
///
/// Creates a new HitsCollection and registers it with Geant4's
/// hit-collection-of-this-event mechanism.
/// @param hce Handle for the hit collection of this event
void SensitiveDetector::Initialize(G4HCofThisEvent* hce) {
    fHitsCollection = new HitsCollection(fDetectorName, collectionName[0]);
    static G4int hcID = -1;
    if (hcID < 0) hcID = GetCollectionID(0);
    hce->AddHitsCollection(hcID, fHitsCollection);
}

/// @brief Process a step in the sensitive volume
///
/// Only creates a Hit when edep > 0 and fDetectorID >= 0.
/// The Hit stores the energy deposit, global time, position,
/// detector ID, and track ID.
/// @param step  Current step
/// @param touch Touchable history (unused)
/// @return true (always)
G4bool SensitiveDetector::ProcessHits(G4Step* step, G4TouchableHistory*) {
    G4double edep = step->GetTotalEnergyDeposit();
    if (edep <= 0.) return true;
    if (fDetectorID < 0) return true;

    auto* touchable = step->GetPreStepPoint()->GetTouchable();

    ::Hit* hit = new ::Hit();
    hit->SetEdep(edep);
    hit->SetDetectorID(fDetectorID);
    hit->SetTime(step->GetPostStepPoint()->GetGlobalTime());
    hit->SetPosition(step->GetPostStepPoint()->GetPosition());
    hit->SetTrackID(step->GetTrack()->GetTrackID());

    // Read replica hierarchy from geometry touchable
    // Level 0 = innermost replica (e.g. strip), Level 1 = parent (e.g. tile), Level 2 = grandparent (e.g. plane)
    G4int depth = touchable->GetHistoryDepth();
    if (depth >= 1) hit->SetStripNumber(touchable->GetReplicaNumber(0));
    if (depth >= 2) hit->SetPlaneNumber(touchable->GetReplicaNumber(1));
    // For a voxel phantom (G4PVParameterised) the copy number at level 0 is
    // the voxel index (iz*(nx*ny) + iy*nx + ix).
    if (depth >= 1) hit->SetVoxelIndex(touchable->GetReplicaNumber(0));
    // isXPlane is inferred from volume name convention (X-plane has capital X in name)
    G4String volName = touchable->GetVolume(0)->GetName();
    hit->SetIsXPlane((volName.find("X") != std::string::npos && volName.find("Y") == std::string::npos) ? 1 : 0);

    fHitsCollection->insert(hit);
    return true;
}

/// @brief End-of-event hook (currently a no-op)
/// @param hce Handle for the hit collection of this event (unused)
void SensitiveDetector::EndOfEvent(G4HCofThisEvent*) {}