//==============================================================================
// G4CARE
// @file    Digit.hh
// @brief   G4VDigi-derived class holding the final digitized detector signal
//          produced by DigitizerModule.
// @details Digit stores processed signal time, energy, position, detector
//   and track IDs, plus metadata: raw (pre-digitization) energy/time, pile-up
//   size, afterpulse flag, quantum efficiency, and noise contribution.
//   A static G4Allocator provides thread-local memory pooling.
//
//   Configuration keys read: none (data object).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DIGIT_HH
#define DIGIT_HH

#include "G4VDigi.hh"
#include "G4TDigiCollection.hh"
#include "G4Allocator.hh"
#include "G4ThreeVector.hh"

/// @brief Digitized detector signal with full metadata.
class Digit : public G4VDigi {
public:
    Digit();
    virtual ~Digit();

    static G4Allocator<Digit>* fAllocator;
    void* operator new(size_t);
    void operator delete(void*);

    virtual void Draw() override {}
    virtual void Print() override {}

    // Setters
    void SetDetectorID(G4int id) { fDetectorID = id; }
    void SetTime(G4double t) { fTime = t; }
    void SetEnergy(G4double e) { fEnergy = e; }
    void SetPosition(const G4ThreeVector& pos) { fPosition = pos; }
    void SetTrackID(G4int id) { fTrackID = id; }
    void SetRawEnergy(G4double e) { fRawEnergy = e; }
    void SetRawTime(G4double t) { fRawTime = t; }
    void SetPileupSize(G4int s) { fPileupSize = s; }
    void SetIsPileup(G4bool b) { fIsPileup = b; }
    void SetAfterpulse(G4bool b) { fAfterpulse = b; }
    void SetQuantumEfficiency(G4double qe) { fQuantumEfficiency = qe; }
    void SetNoiseEnergy(G4double e) { fNoiseEnergy = e; }
    void SetDigitType(G4int t) { fDigitType = t; }

    // Getters
    G4int GetDetectorID() const { return fDetectorID; }
    G4double GetTime() const { return fTime; }
    G4double GetEnergy() const { return fEnergy; }
    G4ThreeVector GetPosition() const { return fPosition; }
    G4int GetTrackID() const { return fTrackID; }
    G4double GetRawEnergy() const { return fRawEnergy; }
    G4double GetRawTime() const { return fRawTime; }
    G4int GetPileupSize() const { return fPileupSize; }
    G4bool IsPileup() const { return fIsPileup; }
    G4bool HasAfterpulse() const { return fAfterpulse; }
    G4double GetQuantumEfficiency() const { return fQuantumEfficiency; }
    G4double GetNoiseEnergy() const { return fNoiseEnergy; }
    G4int GetDigitType() const { return fDigitType; }

private:
    G4int fDetectorID;
    G4double fTime;
    G4double fEnergy;
    G4ThreeVector fPosition;
    G4int fTrackID;

    G4double fRawEnergy;
    G4double fRawTime;
    G4int fPileupSize;
    G4bool fIsPileup;
    G4bool fAfterpulse;
    G4double fQuantumEfficiency;
    G4double fNoiseEnergy;
    G4int fDigitType = -1;
};

using DigitCollection = G4TDigiCollection<Digit>;

#endif