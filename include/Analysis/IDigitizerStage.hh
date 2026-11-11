//==============================================================================
// G4CARE
// @file    IDigitizerStage.hh
// @brief   Base interface and per-event context for the modular digitizer
//          chain (GATE-style).
// @details A stage transforms the DigitizerContext (input hits, intermediate
//   pending hits, and output digits) in order.  The DigitizerModule owns the
//   chain and is referenced by the context so stages can reach shared
//   per-thread state and helper methods without mutexes.
//==============================================================================

#ifndef G4CARE_IDIGITIZERSTAGE_HH
#define G4CARE_IDIGITIZERSTAGE_HH

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "G4Types.hh"
#include "G4ThreeVector.hh"
#include "Digit.hh"

class Hit;
class G4HCofThisEvent;
class DigitizerModule;

/// A hit buffered for pile-up processing.
struct PendingHit {
    G4double time = 0.0;
    G4double energy = 0.0;
    G4ThreeVector position;
    G4int detectorID = -1;
    G4int trackID = -1;
    G4double rawEnergy = 0.0;   ///< raw (pre-digitization) hit energy
    G4double rawTime = 0.0;     ///< raw (pre-digitization) hit time
};

/// Per-event state flowing through the digitizer chain.
struct DigitizerContext {
    G4int eventId = 0;
    G4HCofThisEvent* hce = nullptr;
    std::unordered_map<G4int, std::vector<const Hit*>> hitsByDetector;  ///< hits grouped by detector id
    DigitCollection* digits = nullptr;
    std::vector<PendingHit> pending;
    std::unordered_map<G4int, double> calSumEnergy;
    std::unordered_map<G4int, G4double> calFirstTime;
    std::unordered_map<G4int, G4int> calHitCount;
    std::unordered_map<G4int, G4ThreeVector> calWeightedPos;   ///< energy-weighted position sum (readout centroid)
    std::unordered_map<G4int, G4double> calWeightedTime;       ///< energy-weighted time sum (readout centroid)
    std::unordered_map<G4int, G4double> calMaxEnergy;          ///< highest hit energy (readout winner)
    std::unordered_map<G4int, G4ThreeVector> calWinnerPos;     ///< position of the energy winner
    std::unordered_map<G4int, G4double> calWinnerTime;         ///< time of the energy winner
    std::unordered_set<G4int> calorimeterDetIDs;
    std::unordered_map<G4int, G4int> readoutRep;   ///< readout group -> representative detId
    DigitizerModule* owner = nullptr;  ///< back-reference for helpers/state
};

/// Base class for a single digitizer chain stage.
class IDigitizerStage {
public:
    virtual ~IDigitizerStage() = default;
    virtual void Process(DigitizerContext& ctx) = 0;
};

#endif
