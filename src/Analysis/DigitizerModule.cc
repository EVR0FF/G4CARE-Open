//==============================================================================
// G4CARE
// @file    DigitizerModule.cc
// @brief   Implements a Geant4 digitizer module that converts G4 hits into
//          detector digits with dead-time, quantum efficiency, gain, pile-up,
//          noise, and time/energy smearing effects.
// @details DigitizerModule processes all HitsCollections each event, applying
//   per-detector dead-time models (nonparalyzable / paralyzable), quantum
//   efficiency, configurable signal-time generation (exponential, double
//   exponential, gamma, or expression), time and energy smearing (gaussian,
//   Breit-Wigner, Landau, or expression), optional afterpulsing, threshold
//   cuts, and pile-up grouping.  Supports calorimeter mode (sum all hits
//   into one digit per event) and per-detector digit type tagging.
//
//   The resulting DigitCollection is stored in the G4DigiManager and
//   forwarded to BeamAnalysis for ntuple filling.
//
//   Configuration keys read: per-detector properties from DetectorRegistry.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "DigitizerModule.hh"
#include "G4DigiManager.hh"
#include "G4Event.hh"
#include "G4HCofThisEvent.hh"
#include "G4RunManager.hh"
#include "Randomize.hh"
#include "Hit.hh"
#include "BeamAnalysis.hh"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

static thread_local std::map<std::string, double> s_evalVarsCache;
static thread_local std::vector<std::map<std::string, double>> s_emptyVertexResults;

/// @brief Constructs the digitizer module and pre-allocates dead-time
///        tracking vectors based on the highest registered detector ID.
/// @param name   Module name.
/// @param detReg Detector registry with per-detector properties.
/// @param eval   Expression evaluator for expression-based smearing/signal models.
DigitizerModule::DigitizerModule(G4String name, DetectorRegistry* detReg, ExpressionEvaluator* eval)
    : G4VDigitizerModule(name), fDetReg(detReg), fEval(eval) {
    collectionName.push_back("DigitsCollection");
    if (fDetReg) {
        const auto& detectors = fDetReg->GetAll();
        fMaxDetectorID = -1;
        for (const auto& [sd, props] : detectors) {
            G4int id = fDetReg->GetDetectorID(sd);
            if (id > fMaxDetectorID) fMaxDetectorID = id;
        }
        if (fMaxDetectorID >= 0) {
            fLastHitTime.assign(fMaxDetectorID + 1, kNeverHit);
            fDeadUntil.assign(fMaxDetectorID + 1, kNeverHit);
        }
    }
}

DigitizerModule::~DigitizerModule() = default;

/// @brief Main digitization entry point called each event.  Reads hits,
///        applies dead-time, QE, signal generation, smearing, threshold,
///        and pile-up, and fills the DigitCollection.
void DigitizerModule::Digitize() {
    G4DigiManager* dm = G4DigiManager::GetDMpointer();

    const G4Event* constEvent = G4RunManager::GetRunManager()->GetCurrentEvent();
    G4Event* event = const_cast<G4Event*>(constEvent);
    G4int eventId = event->GetEventID();

    if (fDigiColID < 0) {
        fDigiColID = dm->GetDigiCollectionID(collectionName[0]);
    }

    G4HCofThisEvent* hce = event->GetHCofThisEvent();
    G4int nHitsColl = 0;
    G4int totalHits = 0;
    if (hce) {
        G4int nColl = hce->GetNumberOfCollections();
        for (G4int ic = 0; ic < nColl; ++ic) {
            G4VHitsCollection* hc = hce->GetHC(ic);
            if (!hc) continue;
            // Accept any collection whose name starts with "HitsCollection"
            G4String hcName = hc->GetName();
            if (hcName.find("HitsCollection") != std::string::npos) {
                auto* chc = static_cast<const HitsCollection*>(hc);
                if (chc) {
                    nHitsColl++;
                    totalHits += (G4int)chc->GetSize();
                }
            }
        }
    }

    if (fDigiColID < 0) return;
    if (!hce || nHitsColl == 0) return;

    DigitCollection* digitCollection = new DigitCollection(collectionName[0], collectionName[0]);

    fPendingHits.clear();

    // Identify calorimeter-mode detector IDs
    std::unordered_set<G4int> calorimeterDetIDs;
    std::unordered_map<G4int, double> calSumEnergy;
    std::unordered_map<G4int, G4double> calFirstTime;
    std::unordered_map<G4int, G4int> calHitCount;

    for (const auto& [sd, props] : fDetReg->GetAll()) {
        G4int id = fDetReg->GetDetectorID(sd);
        if (id >= 0 && props.calorimeterMode) {
            calorimeterDetIDs.insert(id);
        }
    }

    // ========== Iterate over ALL HitsCollections from HCofThisEvent ==========
    G4int nColl = hce->GetNumberOfCollections();
    for (G4int ic = 0; ic < nColl; ++ic) {
        G4VHitsCollection* hc = hce->GetHC(ic);
        if (!hc) continue;
        G4String hcName2 = hc->GetName();
        if (hcName2.find("HitsCollection") == std::string::npos) continue;

        auto* hitsCollection = static_cast<const HitsCollection*>(hc);
        if (!hitsCollection) continue;

        for (size_t i = 0; i < hitsCollection->GetSize(); ++i) {
            const Hit* hit = (*hitsCollection)[i];
            G4int detId = hit->GetDetectorID();
            if (detId < 0) continue;

            if (detId > fMaxDetectorID) {
                fMaxDetectorID = detId;
                fLastHitTime.resize(fMaxDetectorID + 1, kNeverHit);
                fDeadUntil.resize(fMaxDetectorID + 1, kNeverHit);
            }

            const DetectorProperties& props = fDetReg->GetProperties(detId);

            // Calorimeter-mode: accumulate
            if (props.calorimeterMode) {
                double edep = hit->GetEdep();
                calSumEnergy[detId] += edep;
                if (calHitCount[detId] == 0) {
                    calFirstTime[detId] = hit->GetTime();
                }
                calHitCount[detId]++;
                continue;
            }

            if (!ApplyQuantumEfficiency(props)) continue;

            G4double rawTime = hit->GetTime();
            G4double rawEnergy = hit->GetEdep();

            G4double signalTime = GenerateSignalTime(rawTime, hit, props, eventId);
            signalTime = ApplyTimeSmearing(signalTime, hit, eventId, props);

            bool dead = IsDead(detId, signalTime, props);
            if (props.deadTimeModel == "paralyzable") {
                UpdateDeadTime(detId, signalTime, props);
            }
            if (dead) continue;

            G4double signalEnergy = ApplyEnergySmearing(rawEnergy, hit, props);
            signalEnergy = ApplyGain(signalEnergy, props);
            G4double noise = 0.0;
            if (props.noiseLevel_MeV > 0.0) {
                noise = CLHEP::RandGauss::shoot(0.0, props.noiseLevel_MeV);
                signalEnergy += noise;
                if (signalEnergy < 0.0) signalEnergy = 0.0;
            }
            if (signalEnergy < props.threshold_MeV) continue;

            if (props.deadTimeModel == "nonparalyzable") {
                UpdateDeadTime(detId, signalTime, props);
            }

            bool afterpulse = (props.afterpulseProbability > 0.0 &&
                               G4UniformRand() < props.afterpulseProbability);

            if (props.pileupModel != "none" && props.pileupWindow_ns > 0.0) {
                fPendingHits.push_back({signalTime, signalEnergy, hit->GetPosition(), detId, hit->GetTrackID()});
            } else {
                Digit* digit = new Digit();
                FillDigit(digit, detId, signalTime, signalEnergy, hit->GetPosition(), hit->GetTrackID(),
                          rawEnergy, rawTime, 1, false, props.quantumEfficiency, noise, afterpulse);
                digitCollection->insert(digit);
            }
        }
    }

    // Calorimeter summed digits
    if (!calorimeterDetIDs.empty()) {
        for (auto& [detId, rawEnergy] : calSumEnergy) {
            if (rawEnergy <= 0.0) continue;
            const DetectorProperties& props = fDetReg->GetProperties(detId);
            if (!ApplyQuantumEfficiency(props)) continue;

            Hit dummyHit;
            dummyHit.SetDetectorID(detId);
            dummyHit.SetEdep(rawEnergy);
            G4double sigEnergy = ApplyEnergySmearing(rawEnergy, &dummyHit, props);
            sigEnergy = ApplyGain(sigEnergy, props);
            G4double noise = 0.0;
            if (props.noiseLevel_MeV > 0.0) {
                noise = CLHEP::RandGauss::shoot(0.0, props.noiseLevel_MeV);
                sigEnergy += noise;
                if (sigEnergy < 0.0) sigEnergy = 0.0;
            }
            if (sigEnergy < props.threshold_MeV) continue;

            Digit* digit = new Digit();
            FillDigit(digit, detId, calFirstTime[detId], sigEnergy, G4ThreeVector(), 0,
                      rawEnergy, calFirstTime[detId], calHitCount[detId], false,
                      props.quantumEfficiency, noise, false);
            digitCollection->insert(digit);
        }
    }

    // Pile-up
    if (!fPendingHits.empty()) {
        std::unordered_map<G4int, std::vector<PendingHit>> perDetector;
        for (const auto& h : fPendingHits) perDetector[h.detectorID].push_back(h);
        for (auto& [detId, hits] : perDetector) {
            const DetectorProperties& props = fDetReg->GetProperties(detId);
            ProcessPileup(hits, props, digitCollection, eventId);
        }
    }

    G4DCofThisEvent* dc = event->GetDCofThisEvent();
    if (!dc) {
        dc = new G4DCofThisEvent();
        event->SetDCofThisEvent(dc);
    }
    dc->AddDigiCollection(fDigiColID, digitCollection);

    for (size_t i = 0; i < digitCollection->GetSize(); ++i) {
        Digit* digit = (*digitCollection)[i];
        BeamAnalysis::Instance()->Fill("digits", digit);
    }
}

/// @brief Applies quantum efficiency as a probabilistic acceptance test.
/// @param props Detector properties holding quantumEfficiency.
/// @return true if the hit passes QE; false otherwise.
bool DigitizerModule::ApplyQuantumEfficiency(const DetectorProperties& props) const {
    return G4UniformRand() <= props.quantumEfficiency;
}

/// @brief Applies the detector gain factor to the signal energy.
/// @param energy Input energy.
/// @param props Detector properties with gain value.
/// @return energy * gain.
G4double DigitizerModule::ApplyGain(G4double energy, const DetectorProperties& props) const {
    return energy * props.gain;
}

/// @brief Checks whether a detector is currently in dead time.
/// @param detId Detector ID.
/// @param time  Current hit time in ns.
/// @param props Detector properties with deadTime_ns and deadTimeModel.
/// @return true if the detector is dead for this hit.
bool DigitizerModule::IsDead(G4int detId, G4double time, const DetectorProperties& props) {
    if (props.deadTime_ns <= 0.0) return false;
    if (props.deadTimeModel == "nonparalyzable") {
        return (fLastHitTime[detId] > kNeverHit && time - fLastHitTime[detId] < props.deadTime_ns);
    } else if (props.deadTimeModel == "paralyzable") {
        return (fDeadUntil[detId] > kNeverHit && time < fDeadUntil[detId]);
    }
    return false;
}

/// @brief Updates the dead-time tracking arrays after a hit is accepted.
/// @param detId Detector ID.
/// @param time  Hit time in ns.
/// @param props Detector properties with deadTime_ns and deadTimeModel.
void DigitizerModule::UpdateDeadTime(G4int detId, G4double time, const DetectorProperties& props) {
    if (props.deadTime_ns <= 0.0) return;
    if (props.deadTimeModel == "nonparalyzable") {
        fLastHitTime[detId] = time;
    } else if (props.deadTimeModel == "paralyzable") {
        fDeadUntil[detId] = time + props.deadTime_ns;
    }
}

/// @brief Fills a Digit object with all relevant parameters.
/// @param digit       Output digit to fill.
/// @param detId       Detector ID.
/// @param time        Signal time in ns.
/// @param energy      Signal energy in MeV.
/// @param pos         Hit position.
/// @param trackId     Geant4 track ID.
/// @param rawEnergy   Raw (un-processed) energy in MeV.
/// @param rawTime     Raw hit time in ns.
/// @param pileupSize  Number of hits contributing to this digit.
/// @param isPileup    Whether this digit results from pile-up.
/// @param qe          Applied quantum efficiency.
/// @param noise       Noise contribution in MeV.
/// @param afterpulse  Whether this digit is an afterpulse.
void DigitizerModule::FillDigit(Digit* digit, G4int detId, G4double time, G4double energy,
                                const G4ThreeVector& pos, G4int trackId,
                                G4double rawEnergy, G4double rawTime, G4int pileupSize,
                                G4bool isPileup, G4double qe, G4double noise, G4bool afterpulse) {
    digit->SetDetectorID(detId);
    digit->SetTime(time);
    digit->SetEnergy(energy);
    digit->SetPosition(pos);
    digit->SetTrackID(trackId);
    digit->SetRawEnergy(rawEnergy);
    digit->SetRawTime(rawTime);
    digit->SetPileupSize(pileupSize);
    digit->SetIsPileup(isPileup);
    digit->SetAfterpulse(afterpulse);
    digit->SetQuantumEfficiency(qe);
    digit->SetNoiseEnergy(noise);
    digit->SetDigitType(fDetReg->GetProperties(detId).digitType);
}

/// @brief Populates an expression variable map with time-related quantities.
/// @param[out] vars    Map to fill.
/// @param rawTime      Raw hit time in internal units.
/// @param hit          Source hit.
/// @param eventId      Current event ID.
/// @param props        Detector properties with timing parameters.
void DigitizerModule::PrepareTimeVars(std::map<std::string, double>& vars, G4double rawTime, const Hit* hit,
                                      G4int eventId, const DetectorProperties& props) const {
    vars["time_ns"] = rawTime / ns;
    vars["energy_MeV"] = hit->GetEdep() / MeV;
    vars["detector_id"] = static_cast<double>(hit->GetDetectorID());
    vars["track_id"] = static_cast<double>(hit->GetTrackID());
    vars["event_id"] = static_cast<double>(eventId);
    vars["pos_x_mm"] = hit->GetPosition().x() / mm;
    vars["pos_y_mm"] = hit->GetPosition().y() / mm;
    vars["pos_z_mm"] = hit->GetPosition().z() / mm;
    vars["dead_time_ns"] = props.deadTime_ns;
    vars["time_res_fwhm_ns"] = props.timeResFWHM_ns;
    vars["signal_rise_ns"] = props.signalRiseTime_ns;
    vars["signal_fall_ns"] = props.signalFallTime_ns;
    vars["quantum_efficiency"] = props.quantumEfficiency;
    vars["gain"] = props.gain;
    vars["threshold_MeV"] = props.threshold_MeV;
    vars["noise_MeV"] = props.noiseLevel_MeV;
}

/// @brief Populates an expression variable map with energy-related quantities.
/// @param[out] vars    Map to fill.
/// @param rawEnergy    Raw hit energy in internal units.
/// @param hit          Source hit.
/// @param props        Detector properties with energy resolution parameters.
void DigitizerModule::PrepareEnergyVars(std::map<std::string, double>& vars, G4double rawEnergy, const Hit* hit,
                                        const DetectorProperties& props) const {
    vars["energy_MeV"] = rawEnergy / MeV;
    vars["detector_id"] = static_cast<double>(hit->GetDetectorID());
    vars["energy_res_fwhm"] = props.energyResFWHM;
    vars["energy_ref_MeV"] = props.energyResRefEnergy_MeV;
}

/// @brief Populates an expression variable map with pile-up cluster quantities.
/// @param[out] vars    Map to fill.
/// @param hits         Vector of pending hits in the cluster.
/// @param start        Start index of the cluster.
/// @param end          One-past-end index of the cluster.
/// @param eventId      Current event ID.
/// @param props        Detector properties.
void DigitizerModule::PreparePileupVars(std::map<std::string, double>& vars, const std::vector<PendingHit>& hits,
                                        size_t start, size_t end, G4int eventId, const DetectorProperties& props) const {
    double sumEnergy = 0.0, weightedTime = 0.0, maxEnergy = 0.0;
    for (size_t i = start; i < end; ++i) {
        sumEnergy += hits[i].energy;
        weightedTime += hits[i].time * hits[i].energy;
        if (hits[i].energy > maxEnergy) maxEnergy = hits[i].energy;
    }
    vars["sum_energy_MeV"] = sumEnergy / MeV;
    vars["max_energy_MeV"] = maxEnergy / MeV;
    if (sumEnergy > 1e-12) {
        vars["weighted_time_ns"] = (weightedTime / sumEnergy) / ns;
    } else {
        vars["weighted_time_ns"] = 0.0;
    }
    vars["count"] = static_cast<double>(end - start);
    vars["detector_id"] = static_cast<double>(hits[start].detectorID);
    vars["event_id"] = static_cast<double>(eventId);
    vars["dead_time_ns"] = props.deadTime_ns;
    vars["time_res_fwhm_ns"] = props.timeResFWHM_ns;
    vars["signal_rise_ns"] = props.signalRiseTime_ns;
    vars["signal_fall_ns"] = props.signalFallTime_ns;
    vars["quantum_efficiency"] = props.quantumEfficiency;
    vars["gain"] = props.gain;
    vars["threshold_MeV"] = props.threshold_MeV;
    vars["noise_MeV"] = props.noiseLevel_MeV;
}

// ----------------------------------------------------------------------------
// Signal time generation and smearing
// ----------------------------------------------------------------------------

/// @brief Generates the signal time from the raw hit time, applying the
///        selected signal model (exponential, double exponential, gamma, or
///        expression).
/// @param rawTime Raw hit time in internal units.
/// @param hit     Source hit.
/// @param props   Detector properties with signal model settings.
/// @param eventId Current event ID (used for expression variables).
/// @return Signal time in internal units.
G4double DigitizerModule::GenerateSignalTime(G4double rawTime, const Hit* hit,
                                             const DetectorProperties& props,
                                             G4int eventId) const {
    if (props.useSignalTimeExpression && !props.signalTimeExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareTimeVars(s_evalVarsCache, rawTime, hit, eventId, props);
        double result = fEval->EvaluateWithUnit(props.signalTimeExpression, s_evalVarsCache, s_emptyVertexResults);
        return result * ns;
    }

    G4double t = rawTime;
    if (props.signalModel == "exponential" && props.signalFallTime_ns > 0.0) {
        t += CLHEP::RandExponential::shoot(props.signalFallTime_ns);
    } else if (props.signalModel == "double_exponential" &&
               props.signalRiseTime_ns > 0.0 && props.signalFallTime_ns > 0.0) {
        double mean = props.signalRiseTime_ns + props.signalFallTime_ns;
        double shape = 2.0;
        double scale = mean / shape;
        t += CLHEP::RandGamma::shoot(shape, scale);
    } else if (props.signalModel == "gamma" && props.signalRiseTime_ns > 0.0) {
        t += CLHEP::RandGamma::shoot(2.0, props.signalRiseTime_ns / 2.0);
    }
    return t;
}

/// @brief Applies time smearing (gaussian, Breit-Wigner, Landau, or expression)
///        to the signal time.
/// @param rawTime Time before smearing in internal units.
/// @param hit     Source hit.
/// @param eventId Current event ID (used for expression variables).
/// @param props   Detector properties with time resolution settings.
/// @return Smeared time in internal units.
G4double DigitizerModule::ApplyTimeSmearing(G4double rawTime, const Hit* hit,
                                            G4int eventId,
                                            const DetectorProperties& props) const {
    if (props.useTimeSmearExpression && !props.timeSmearExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareTimeVars(s_evalVarsCache, rawTime, hit, eventId, props);
        double result = fEval->EvaluateWithUnit(props.signalTimeExpression, s_evalVarsCache, s_emptyVertexResults);
        return result * ns;
    }

    if (props.timeResFWHM_ns <= 0.0 && props.timeSmearModel != "landau") return rawTime;

    if (props.timeSmearModel == "gaussian") {
        double sigma = props.timeResFWHM_ns / 2.355;
        return CLHEP::RandGauss::shoot(rawTime, sigma);
    } else if (props.timeSmearModel == "breit_wigner") {
        double gamma = props.timeResFWHM_ns;
        return CLHEP::RandBreitWigner::shoot(rawTime, gamma);
    } else if (props.timeSmearModel == "landau") {
        if (props.timeLandauSigma_ns > 0.0) {
            return rawTime + props.timeLandauMPV_ns + CLHEP::RandLandau::shoot() * props.timeLandauSigma_ns;
        } else {
            static G4ThreadLocal std::map<G4int, bool> warned;
            if (!warned[hit->GetDetectorID()]) {
                G4cerr << "DigitizerModule: Landau time smearing requested for detector " << hit->GetDetectorID()
                       << " but timeLandauSigma_ns not set. Falling back to raw time." << G4endl;
                warned[hit->GetDetectorID()] = true;
            }
            return rawTime;
        }
    }
    return rawTime;
}

/// @brief Applies energy smearing (gaussian, Breit-Wigner, Landau, or
///        expression) to the signal energy.
/// @param rawEnergy Energy before smearing in internal units.
/// @param hit       Source hit (may be nullptr in calorimeter mode).
/// @param props     Detector properties with energy resolution settings.
/// @return Smeared energy in internal units (non-negative).
G4double DigitizerModule::ApplyEnergySmearing(G4double rawEnergy, const Hit* hit,
                                              const DetectorProperties& props) const {
    if (props.useEnergySmearExpression && !props.energySmearExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, rawEnergy, hit, props);
        double result = fEval->EvaluateWithUnit(props.signalTimeExpression, s_evalVarsCache, s_emptyVertexResults);
        return result * MeV;
    }

    if (props.energyResFWHM <= 0.0 && props.energySmearModel != "landau") return rawEnergy;

    if (props.energySmearModel == "gaussian") {
        double sigma = (props.energyResFWHM / 2.355) * std::sqrt(rawEnergy / props.energyResRefEnergy_MeV);
        return std::max(0.0, CLHEP::RandGauss::shoot(rawEnergy, sigma));
    } else if (props.energySmearModel == "breit_wigner") {
        double gamma = props.energyResFWHM * rawEnergy;
        return std::max(0.0, CLHEP::RandBreitWigner::shoot(rawEnergy, gamma));
    } else if (props.energySmearModel == "landau") {
        if (props.energyLandauSigma_MeV > 0.0) {
            return std::max(0.0, rawEnergy + props.energyLandauMPV_MeV + CLHEP::RandLandau::shoot() * props.energyLandauSigma_MeV);
        } else {
            static G4ThreadLocal std::map<G4int, bool> warned;
            G4int fallbackDetId = (hit ? hit->GetDetectorID() : -1);
            if (!warned[fallbackDetId]) {
                G4cerr << "DigitizerModule: Landau energy smearing requested for detector " << fallbackDetId
                       << " but energyLandauSigma_MeV not set. Falling back to raw energy." << G4endl;
                warned[fallbackDetId] = true;
            }
            return rawEnergy;
        }
    }
    return rawEnergy;
}

/// @brief Processes a group of pending hits belonging to one detector for
///        pile-up.  Hits within the pileup window are merged according to the
///        pileup model (sum, max, or expression) and a single digit is created.
/// @param[in,out] hits         Pending hits for a single detector; will be sorted by time.
/// @param props                Detector properties with pileup settings.
/// @param[out]   digitCol      Output digit collection.
/// @param eventId              Current event ID (used for expression variables).
void DigitizerModule::ProcessPileup(std::vector<PendingHit>& hits, const DetectorProperties& props,
                                    DigitCollection* digitCol, G4int eventId) {
    if (hits.empty()) return;
    std::sort(hits.begin(), hits.end(),
              [](const PendingHit& a, const PendingHit& b) { return a.time < b.time; });

    size_t start = 0;
    while (start < hits.size()) {
        size_t end = start + 1;
        double sumEnergy = hits[start].energy;
        double weightedTime = hits[start].time * hits[start].energy;
        double maxEnergy = hits[start].energy;
        G4int detId = hits[start].detectorID;
        G4ThreeVector pos = hits[start].position;
        G4int trackId = hits[start].trackID;

        while (end < hits.size() && (hits[end].time - hits[start].time) < props.pileupWindow_ns) {
            sumEnergy += hits[end].energy;
            weightedTime += hits[end].time * hits[end].energy;
            if (hits[end].energy > maxEnergy) maxEnergy = hits[end].energy;
            ++end;
        }

        int pileupSize = static_cast<int>(end - start);
        double avgTime = (sumEnergy > 1e-12) ? (weightedTime / sumEnergy) : hits[start].time;
        double safeAvgTime = (sumEnergy > 1e-12) ? (weightedTime / sumEnergy) : hits[start].time;
        double finalEnergy = sumEnergy;

        if ((props.usePileupEnergyExpression && !props.pileupEnergyExpression.empty()) ||
            (props.usePileupTimeExpression && !props.pileupTimeExpression.empty())) {
            s_evalVarsCache.clear();
            PreparePileupVars(s_evalVarsCache, hits, start, end, eventId, props);
            if (props.usePileupEnergyExpression && !props.pileupEnergyExpression.empty() && fEval) {
                finalEnergy = fEval->EvaluateWithUnit(props.pileupEnergyExpression, s_evalVarsCache, s_emptyVertexResults) * MeV;
            }
            if (props.usePileupTimeExpression && !props.pileupTimeExpression.empty() && fEval) {
                avgTime = fEval->EvaluateWithUnit(props.pileupTimeExpression, s_evalVarsCache, s_emptyVertexResults) * ns;
            }
        } else if (props.pileupModel == "max") {
            finalEnergy = maxEnergy;
        }

        bool afterpulse = (props.afterpulseProbability > 0.0 &&
                           G4UniformRand() < props.afterpulseProbability);

        Digit* digit = new Digit();
        FillDigit(digit, detId, avgTime, finalEnergy, pos, trackId,
                  sumEnergy, safeAvgTime, pileupSize, true,
                  props.quantumEfficiency, 0.0, afterpulse);
        digitCol->insert(digit);
        UpdateDeadTime(detId, avgTime, props);
        start = end;
    }
}