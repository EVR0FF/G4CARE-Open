//==============================================================================
// G4CARE
// @file    Digit.cc
// @brief   Implementation of the G4VDigi-derived Digit class holding the
//          final digitized detector signal produced by DigitizerModule.
// @details Digit stores the processed signal time, energy, position, detector
//   and track IDs, along with metadata such as raw (pre-digitization) energy
//   and time, pile-up size, afterpulse flag, quantum efficiency, and noise
//   contribution.  A static G4Allocator provides thread-local memory pooling
//   in multi-threaded Geant4 jobs.
//
//   Configuration keys read: none (data object).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Digit.hh"

G4Allocator<Digit>* Digit::fAllocator = nullptr;

/// @brief Default constructor — zero-initializes all fields.
Digit::Digit() : fDetectorID(-1), fTime(0.), fEnergy(0.), fPosition(0.), fTrackID(-1),
                 fRawEnergy(0.), fRawTime(0.), fPileupSize(0), fIsPileup(false),
                 fAfterpulse(false), fQuantumEfficiency(1.0), fNoiseEnergy(0.) {}

Digit::~Digit() {}

/// @brief Custom thread-local allocator operator new for memory pooling.
void* Digit::operator new(size_t) {
    if (!fAllocator) fAllocator = new G4Allocator<Digit>;
    return fAllocator->MallocSingle();
}

/// @brief Custom thread-local allocator operator delete for memory pooling.
void Digit::operator delete(void* aDigit) {
    fAllocator->FreeSingle((Digit*)aDigit);
}