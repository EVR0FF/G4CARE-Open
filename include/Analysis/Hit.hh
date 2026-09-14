//==============================================================================
// G4CARE
// @file    Hit.hh
// @brief   G4VHit-derived class storing energy deposition, time, position,
//          detector ID, and track ID for each sensitive-detector step.
// @details Hit is produced by the sensitive detector and collected into a
//   HitsCollection for later digitization.  A thread-local G4Allocator
//   provides efficient memory management in multi-threaded Geant4 runs.
//
//   Configuration keys read: none (data object).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef HIT_HH
#define HIT_HH

#include "G4VHit.hh"
#include "G4THitsCollection.hh"
#include "G4Allocator.hh"
#include "G4ThreeVector.hh"

class Hit;
extern G4ThreadLocal G4Allocator<Hit>* HitAllocator; 

/// @brief Sensitive-detector hit with energy, time, position, and IDs.
class Hit : public G4VHit {
public:
    Hit();
    virtual ~Hit();

    inline void* operator new(size_t) {
        if (!HitAllocator) HitAllocator = new G4Allocator<Hit>;
        return (void*)HitAllocator->MallocSingle();
    }
    inline void operator delete(void* aHit) {
        HitAllocator->FreeSingle((Hit*)aHit);
    }
    virtual void Draw() override {}
    virtual void Print() override {}

    void SetEdep(G4double e) { fEdep = e; }
    void SetTime(G4double t) { fTime = t; }
    void SetPosition(const G4ThreeVector& pos) { fPosition = pos; }
    void SetDetectorID(G4int id) { fDetectorID = id; }
    void SetTrackID(G4int id) { fTrackID = id; }
    void SetStripNumber(G4int n) { fStripNumber = n; }
    void SetPlaneNumber(G4int n) { fPlaneNumber = n; }
    void SetIsXPlane(G4int v) { fIsXPlane = v; }

    G4double GetEdep() const { return fEdep; }
    G4double GetTime() const { return fTime; }
    G4ThreeVector GetPosition() const { return fPosition; }
    G4int GetDetectorID() const { return fDetectorID; }
    G4int GetTrackID() const { return fTrackID; }
    G4int GetStripNumber() const { return fStripNumber; }
    G4int GetPlaneNumber() const { return fPlaneNumber; }
    G4int GetIsXPlane() const { return fIsXPlane; }

private:
    G4double fEdep;
    G4double fTime;
    G4ThreeVector fPosition;
    G4int fDetectorID;
    G4int fTrackID;
    G4int fStripNumber = -1;
    G4int fPlaneNumber = -1;
    G4int fIsXPlane = -1;
};

using HitsCollection = G4THitsCollection<Hit>;

#endif