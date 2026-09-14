#ifndef SENSITIVE_DETECTOR_HH
#define SENSITIVE_DETECTOR_HH

//==============================================================================
// G4CARE
// @file    SensitiveDetector.hh
// @brief   Sensitive detector for hit collection in Geant4 volumes
// @details Implements G4VSensitiveDetector to record energy deposits
//   in sensitive volumes. Creates a HitsCollection per event and
//   stores Hit objects (edep, time, position, detector ID, track ID)
//   for each step with positive energy deposit. The detector ID
//   links hits to the DetectorRegistry for analysis.
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
#include "G4VSensitiveDetector.hh"

class SensitiveDetector : public G4VSensitiveDetector {
public:
    /// @brief Constructor
    /// @param name Name of the sensitive detector / hits collection
    SensitiveDetector(const G4String& name);

    /// @brief Destructor
    virtual ~SensitiveDetector();

    /// @brief Initialize hits collection for the current event
    /// @param hce Handle for the hit collection of this event
    virtual void Initialize(G4HCofThisEvent*) override;

    /// @brief Process a step in the sensitive volume
    ///
    /// Creates a Hit with edep, time, position, detector ID, and
    /// track ID for steps with positive energy deposit.
    /// @param step  Current step
    /// @param touch Touchable history
    /// @return true (always)
    virtual G4bool ProcessHits(G4Step*, G4TouchableHistory*) override;

    /// @brief End-of-event hook (currently no-op)
    /// @param hce Handle for the hit collection of this event
    virtual void EndOfEvent(G4HCofThisEvent*) override;

    /// @brief Clone for multi-threaded operation
    /// @return New SensitiveDetector instance
    virtual G4VSensitiveDetector* Clone() const override;

    /// @brief Set the detector ID linking hits to DetectorRegistry
    /// @param id Detector identifier
    void SetDetectorID(G4int id) { fDetectorID = id; }

    /// @brief Get the detector ID
    /// @return Detector identifier
    G4int GetDetectorID() const { return fDetectorID; }

private:
    /// @brief Detector name
    G4String fDetectorName;

    /// @brief Pointer to the current event's hits collection
    HitsCollection* fHitsCollection;

    /// @brief Detector ID for linking to DetectorRegistry
    G4int fDetectorID;
};

#endif