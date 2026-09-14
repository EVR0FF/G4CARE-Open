//==============================================================================
// G4CARE
// @file    DigitizerModule.hh
// @brief   Geant4 digitizer module that converts G4 hits into detector digits
//          with dead-time, quantum efficiency, gain, pile-up, noise, and
//          time/energy smearing effects.
// @details DigitizerModule processes the HitsCollection each event, applying
//   per-detector dead-time models (nonparalyzable / paralyzable), quantum
//   efficiency, configurable signal-time generation, time/energy smearing
//   (gaussian, Breit-Wigner, Landau, or expression-based), threshold cuts,
//   optional afterpulsing, and pile-up grouping into a DigitCollection.
//
//   Configuration keys read: per-detector properties from DetectorRegistry.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DIGITIZER_MODULE_HH
#define DIGITIZER_MODULE_HH

#include "G4VDigitizerModule.hh"
#include "DetectorRegistry.hh"
#include "Digit.hh"
#include "ExpressionEvaluator.hh"
#include <vector>
#include <unordered_map>
#include <array>
#include <string>

class Hit; // forward

/// @brief Converts G4Hits into Digits with realistic detector effects.
class DigitizerModule : public G4VDigitizerModule {
public:
    DigitizerModule(G4String name, DetectorRegistry* detReg, ExpressionEvaluator* eval = nullptr);
    virtual ~DigitizerModule();

    virtual void Digitize() override;

private:
    struct PendingHit {
        G4double time;
        G4double energy;
        G4ThreeVector position;
        G4int detectorID;
        G4int trackID;
    };

    // Helper methods
    void ProcessPileup(std::vector<PendingHit>& hits, const DetectorProperties& props,
                       DigitCollection* digitCol, G4int eventId);
    void UpdateDeadTime(G4int detId, G4double time, const DetectorProperties& props);
    bool IsDead(G4int detId, G4double time, const DetectorProperties& props);
    void FillDigit(Digit* digit, G4int detId, G4double time, G4double energy,
               const G4ThreeVector& pos, G4int trackId,
               G4double rawEnergy, G4double rawTime, G4int pileupSize,
               G4bool isPileup, G4double qe, G4double noise, G4bool afterpulse);

    // Expression evaluation methods
    G4double GenerateSignalTime(G4double rawTime, const Hit* hit,
                                const DetectorProperties& props, G4int eventId) const;
    G4double ApplyTimeSmearing(G4double rawTime, const Hit* hit,
                               G4int eventId, const DetectorProperties& props) const;
    G4double ApplyEnergySmearing(G4double rawEnergy, const Hit* hit,
                                 const DetectorProperties& props) const;
    bool ApplyQuantumEfficiency(const DetectorProperties& props) const;
    G4double ApplyGain(G4double energy, const DetectorProperties& props) const;

    // Prepare variable maps for expressions (uses thread_local cache)
    void PrepareTimeVars(std::map<std::string, double>& vars, G4double rawTime, const Hit* hit,
                         G4int eventId, const DetectorProperties& props) const;
    void PrepareEnergyVars(std::map<std::string, double>& vars, G4double rawEnergy, const Hit* hit,
                           const DetectorProperties& props) const;
    void PreparePileupVars(std::map<std::string, double>& vars, const std::vector<PendingHit>& hits,
                           size_t start, size_t end, G4int eventId, const DetectorProperties& props) const;

    DetectorRegistry* fDetReg;
    ExpressionEvaluator* fEval;

    // Cached collection IDs
    G4int fHcID = -1;
    G4int fDigiColID = -1;

    // Dead-time storage (vectors for O(1) access)
    static constexpr G4double kNeverHit = -1e12;
    std::vector<G4double> fLastHitTime;   // for nonparalyzable
    std::vector<G4double> fDeadUntil;     // for paralyzable
    G4int fMaxDetectorID = -1;

    // Pile-up buffer
    std::vector<PendingHit> fPendingHits;
};

#endif