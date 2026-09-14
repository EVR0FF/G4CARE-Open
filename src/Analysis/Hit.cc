//==============================================================================
// G4CARE
// @file    Hit.cc
// @brief   Implementation of the G4VHit-derived Hit class that stores
//          energy deposition, time, position, detector ID, and track ID.
// @details Hit is a lightweight data object produced by the sensitive detector
//   and collected into a HitsCollection for later digitization.  A thread-local
//   allocator (HitAllocator) is provided for efficient memory management in
//   multi-threaded Geant4 runs.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Hit.hh"

G4ThreadLocal G4Allocator<Hit>* HitAllocator = nullptr;

/// @brief Default constructor — initializes all fields to zero or invalid values.
Hit::Hit() 
    : G4VHit(), 
      fEdep(0.), 
      fTime(0.), 
      fPosition(0,0,0),
      fDetectorID(-1), 
      fTrackID(-1) 
{}

Hit::~Hit() {}
