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
#include "G4Run.hh"
#include "Randomize.hh"
#include "Hit.hh"
#include "BeamAnalysis.hh"
#include <algorithm>
#include <functional>
#include <tuple>
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
    BuildChain();
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
            fBufferCount.assign(fMaxDetectorID + 1, 0);
        }
    }
}

DigitizerModule::~DigitizerModule() = default;

/// @brief Main digitization entry point called each event.  Builds a fresh
///        DigitizerContext and runs the per-detector chains, then the global
///        cross-detector stages, and attaches the DigitCollection to the event.
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
    if (hce) {
        G4int nColl = hce->GetNumberOfCollections();
        for (G4int ic = 0; ic < nColl; ++ic) {
            G4VHitsCollection* hc = hce->GetHC(ic);
            if (!hc) continue;
            G4String hcName = hc->GetName();
            if (hcName.find("HitsCollection") != std::string::npos) {
                auto* chc = static_cast<const HitsCollection*>(hc);
                if (chc) nHitsColl++;
            }
        }
    }

    if (fDigiColID < 0) return;
    if (!hce || nHitsColl == 0) return;

    DigitCollection* digitCollection = new DigitCollection(collectionName[0], collectionName[0]);

    DigitizerContext ctx;
    ctx.eventId = eventId;
    ctx.hce = hce;
    ctx.digits = digitCollection;
    ctx.owner = this;

    // Reset dead-time state only at the start of a new run (dead time is
    // persistent across events within a run, like GATE).
    G4int runId = G4RunManager::GetRunManager()->GetCurrentRun()->GetRunID();
    if (runId != fLastRunID) {
        fLastRunID = runId;
        for (auto& t : fLastHitTime) t = kNeverHit;
        for (auto& t : fDeadUntil) t = kNeverHit;
        for (auto& c : fBufferCount) c = 0;
    }

    // 1. Collect and group hits by detector.
    if (fCollectionStage) fCollectionStage->Process(ctx);

    // 2. Run each detector's own chain (per-detector order/selection).
    for (const auto& [sd, props] : fDetReg->GetAll()) {
        G4int detId = fDetReg->GetDetectorID(sd);
        if (detId < 0) continue;
        auto it = fDetectorChains.find(detId);
        if (it == fDetectorChains.end()) continue;
        for (const auto& stage : it->second) stage->Process(ctx);
    }

    // 3. Global cross-detector stages.
    for (const auto& stage : fGlobalChain) stage->Process(ctx);

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

// ── Chain stage implementations (nested classes access private members) ──

/// Collects all hits and groups them by detector id (once per event).
struct DigitizerModule::HitCollectionStage : public IDigitizerStage {
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        G4HCofThisEvent* hce = ctx.hce;
        G4int nColl = hce->GetNumberOfCollections();
        for (G4int ic = 0; ic < nColl; ++ic) {
            G4VHitsCollection* hc = hce->GetHC(ic);
            if (!hc) continue;
            G4String hcName = hc->GetName();
            if (hcName.find("HitsCollection") == std::string::npos) continue;
            auto* hitsCollection = static_cast<const HitsCollection*>(hc);
            if (!hitsCollection) continue;
            for (size_t i = 0; i < hitsCollection->GetSize(); ++i) {
                const Hit* hit = (*hitsCollection)[i];
                G4int detId = hit->GetDetectorID();
                if (detId < 0) continue;
                ctx.hitsByDetector[detId].push_back(hit);
                BeamAnalysis::Instance()->Fill("hits", hit);
            }
        }
        for (const auto& [sd, props] : self->fDetReg->GetAll()) {
            G4int id = self->fDetReg->GetDetectorID(sd);
            if (id >= 0 && props.calorimeterMode) ctx.calorimeterDetIDs.insert(id);
        }
    }
};

/// Per-detector per-hit processing (QE, signal time, smearing, dead time,
/// energy, gain, recovery, noise, thresholds, energy framing, pile-up/digit).
struct DigitizerModule::HitProcessingStage : public IDigitizerStage {
    G4int fDetId;
    explicit HitProcessingStage(G4int detId) : fDetId(detId) {}
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        auto hitIt = ctx.hitsByDetector.find(fDetId);
        if (hitIt == ctx.hitsByDetector.end()) return;
        const DetectorProperties& props = self->fDetReg->GetProperties(fDetId);
        const std::vector<DigitizerModule::PipelineStep>& steps = self->GetPipeline(fDetId);

        for (const Hit* hit : hitIt->second) {
            if (fDetId > self->fMaxDetectorID) {
                self->fMaxDetectorID = fDetId;
                self->fLastHitTime.resize(self->fMaxDetectorID + 1, DigitizerModule::kNeverHit);
                self->fDeadUntil.resize(self->fMaxDetectorID + 1, DigitizerModule::kNeverHit);
                self->fBufferCount.resize(self->fMaxDetectorID + 1, 0);
            }

            // Calorimeter-mode: accumulate (digit created in CalorimeterStage).
            if (props.calorimeterMode) {
                double edep = hit->GetEdep();
                G4int key = (props.readoutGroup >= 0) ? props.readoutGroup : fDetId;
                if (props.readoutGroup >= 0) ctx.readoutRep[key] = fDetId;
                ctx.calSumEnergy[key] += edep;
                if (ctx.calHitCount[key] == 0) ctx.calFirstTime[key] = hit->GetTime();
                ctx.calHitCount[key]++;
                ctx.calWeightedPos[key] += hit->GetPosition() * edep;
                ctx.calWeightedTime[key] += hit->GetTime() * edep;
                if (edep > ctx.calMaxEnergy[key]) {
                    ctx.calMaxEnergy[key] = edep;
                    ctx.calWinnerPos[key] = hit->GetPosition();
                    ctx.calWinnerTime[key] = hit->GetTime();
                }
                continue;
            }

            G4double rawTime = hit->GetTime();
            G4double rawEnergy = hit->GetEdep();
            G4double time = rawTime;
            G4double energy = rawEnergy;
            G4double noise = 0.0;
            G4bool afterpulse = false;
            bool pendingNonParalyzable = false;
            bool dropped = false;

            for (DigitizerModule::PipelineStep step : steps) {
                switch (step) {
                    case DigitizerModule::kEfficiency:
                        if (G4UniformRand() > self->EfficiencyProbability(rawEnergy, hit, ctx.eventId, props)) dropped = true;
                        break;
                    case DigitizerModule::kSignalTime:
                        time = self->GenerateSignalTime(rawTime, hit, props, ctx.eventId);
                        break;
                    case DigitizerModule::kTimeSmear:
                        time = self->ApplyTimeSmearing(time, hit, ctx.eventId, props);
                        break;
                    case DigitizerModule::kDeadTime: {
                        bool dead = self->IsDead(fDetId, time, props);
                        if (props.deadTimeModel == "paralyzable") {
                            self->UpdateDeadTime(fDetId, time, props);
                            if (dead) dropped = true;
                        } else {
                            if (dead) {
                                if (props.deadTimeBufferSize > 0 &&
                                    self->fBufferCount[fDetId] < props.deadTimeBufferSize) {
                                    // Nonparalyzable buffer: defer the hit to the end of the
                                    // current dead period instead of dropping it.
                                    time = self->fLastHitTime[fDetId] + props.deadTime_ns;
                                    self->fBufferCount[fDetId]++;
                                } else {
                                    dropped = true;
                                }
                            } else {
                                pendingNonParalyzable = true;
                                self->fBufferCount[fDetId] = 0;  // drain buffer on accepted hit
                            }
                        }
                        break;
                    }
                    case DigitizerModule::kEnergySmear:
                        energy = self->ApplyEnergySmearing(energy, hit, props, ctx.eventId);
                        break;
                    case DigitizerModule::kGain:
                        energy = self->ApplyGain(energy, hit, ctx.eventId, props);
                        break;
                    case DigitizerModule::kRecovery:
                        energy = self->ApplyRecovery(fDetId, time, energy, props);
                        break;
                    case DigitizerModule::kNoise:
                        energy = self->ApplyNoise(energy, hit, ctx.eventId, props, noise);
                        break;
                    case DigitizerModule::kEnergyFrame:
                        if (props.energyFrame_MeV > 0.0) {
                            double f = props.energyFrame_MeV;
                            energy = std::floor(energy / f) * f + 0.5 * f;
                        }
                        break;
                    case DigitizerModule::kThreshold:
                        if (!self->PassThreshold(energy, hit, ctx.eventId, props)) dropped = true;
                        break;
                    case DigitizerModule::kAfterpulse:
                        afterpulse = (props.afterpulseProbability > 0.0 &&
                                      G4UniformRand() < props.afterpulseProbability);
                        break;
                    case DigitizerModule::kInvalid:
                        break;
                }
                if (dropped) break;
            }
            if (dropped) continue;

            if (pendingNonParalyzable) self->UpdateDeadTime(fDetId, time, props);

            G4ThreeVector pos = self->ApplySpatialResolution(hit->GetPosition(), props);
            if (props.pileupModel != "none" && props.pileupWindow_ns > 0.0) {
                ctx.pending.push_back({time, energy, pos, fDetId, hit->GetTrackID(), rawEnergy, rawTime});
            } else {
                Digit* digit = new Digit();
                self->FillDigit(digit, fDetId, time, energy, pos, hit->GetTrackID(),
                                rawEnergy, rawTime, 1, false, props.quantumEfficiency, noise, afterpulse);
                ctx.digits->insert(digit);
            }
        }
    }
};

/// Per-detector calorimeter summation into a single digit (readout-aware).
struct DigitizerModule::CalorimeterStage : public IDigitizerStage {
    G4int fDetId;
    explicit CalorimeterStage(G4int detId) : fDetId(detId) {}
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        const DetectorProperties& props = self->fDetReg->GetProperties(fDetId);
        G4int key = (props.readoutGroup >= 0) ? props.readoutGroup : fDetId;
        auto eIt = ctx.calSumEnergy.find(key);
        if (eIt == ctx.calSumEnergy.end() || eIt->second <= 0.0) return;
        double rawEnergy = eIt->second;

        Hit dummyHit;
        dummyHit.SetDetectorID(fDetId);
        dummyHit.SetEdep(rawEnergy);
        if (G4UniformRand() > self->EfficiencyProbability(rawEnergy, &dummyHit, ctx.eventId, props)) return;

        G4double sigEnergy = self->ApplyEnergySmearing(rawEnergy, &dummyHit, props, ctx.eventId);
        sigEnergy = self->ApplyGain(sigEnergy, &dummyHit, ctx.eventId, props);
        G4double noise = 0.0;
        sigEnergy = self->ApplyNoise(sigEnergy, &dummyHit, ctx.eventId, props, noise);
        if (props.energyFrame_MeV > 0.0) {
            double f = props.energyFrame_MeV;
            sigEnergy = std::floor(sigEnergy / f) * f + 0.5 * f;
        }
        if (!self->PassThreshold(sigEnergy, &dummyHit, ctx.eventId, props)) return;

        // Readout position/time: energy-winner (default) or energy-weighted centroid.
        G4ThreeVector rPos;
        G4double rTime = ctx.calFirstTime[key];
        if (props.readoutPolicy == "energy_centroid" && rawEnergy > 1e-12) {
            double inv = 1.0 / rawEnergy;
            rPos = ctx.calWeightedPos[key] * inv;
            rTime = ctx.calWeightedTime[key] * inv;
        } else if (ctx.calMaxEnergy[key] > 0.0) {
            rPos = ctx.calWinnerPos[key];
            rTime = ctx.calWinnerTime[key];
        }

        Digit* digit = new Digit();
        self->FillDigit(digit, fDetId, rTime, sigEnergy,
                        self->ApplySpatialResolution(rPos, props), 0,
                        rawEnergy, ctx.calFirstTime[key], ctx.calHitCount[key], false,
                        props.quantumEfficiency, noise, false);
        ctx.digits->insert(digit);
    }
};

/// Per-detector pile-up processing.
struct DigitizerModule::PileupStage : public IDigitizerStage {
    G4int fDetId;
    explicit PileupStage(G4int detId) : fDetId(detId) {}
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        std::vector<PendingHit> hits;
        for (const auto& h : ctx.pending) if (h.detectorID == fDetId) hits.push_back(h);
        if (hits.empty()) return;
        const DetectorProperties& props = self->fDetReg->GetProperties(fDetId);
        self->ProcessPileup(hits, props, ctx.digits, ctx.eventId);
    }
};

/// Per-detector dark-count pulses.
struct DigitizerModule::DarkCountStage : public IDigitizerStage {
    G4int fDetId;
    explicit DarkCountStage(G4int detId) : fDetId(detId) {}
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        const DetectorProperties& props = self->fDetReg->GetProperties(fDetId);
        if (props.darkCountRate_kHz <= 0.0) return;

        // Time window spanned by the already-digitized signals (for dark pulses).
        G4double tMin = 1e18, tMax = -1e18;
        for (size_t i = 0; i < ctx.digits->GetSize(); ++i) {
            G4double t = (*ctx.digits)[i]->GetTime();
            if (t < tMin) tMin = t;
            if (t > tMax) tMax = t;
        }
        G4double window_ns = (tMax > tMin) ? (tMax - tMin) : 0.0;
        G4double expected = props.darkCountRate_kHz * window_ns * 1e-6;  // kHz * ns -> counts
        if (expected <= 0.0) return;
        G4int nDark = CLHEP::RandPoisson::shoot(expected);
        for (G4int k = 0; k < nDark; ++k) {
            G4double t = tMin;
            if (window_ns > 0.0) t = tMin + G4UniformRand() * window_ns;
            Digit* digit = new Digit();
            self->FillDigit(digit, fDetId, t, props.darkCountEnergy_MeV, G4ThreeVector(), -1,
                            props.darkCountEnergy_MeV, t, 0, false, props.quantumEfficiency, 0.0, false);
            ctx.digits->insert(digit);
        }
    }
};

/// Global crosstalk. Supports an energy-conserving fraction model
/// (GATE-like: a fraction of the source energy is shared with neighbours and
/// the source is reduced accordingly) and a legacy stochastic copy model.
struct DigitizerModule::CrosstalkStage : public IDigitizerStage {
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        std::vector<Digit*> xtalk;
        for (size_t i = 0; i < ctx.digits->GetSize(); ++i) {
            Digit* d = (*ctx.digits)[i];
            G4int detId = d->GetDetectorID();
            if (detId < 0) continue;
            const DetectorProperties& props = self->fDetReg->GetProperties(detId);

            if (props.crossTalkFraction > 0.0 && !props.crossTalkNeighbors.empty()) {
                G4double totalOut = 0.0;
                for (int off : props.crossTalkNeighbors) {
                    G4int nb = detId + off;
                    if (nb < 0 || nb > self->fMaxDetectorID) continue;
                    G4double eOut = d->GetEnergy() * props.crossTalkFraction;
                    if (eOut <= 0.0) continue;
                    Digit* xt = new Digit();
                    self->FillDigit(xt, nb, d->GetTime(), eOut, d->GetPosition(), d->GetTrackID(),
                                    d->GetRawEnergy(), d->GetRawTime(), 0, false,
                                    self->fDetReg->GetProperties(nb).quantumEfficiency, 0.0, false);
                    xtalk.push_back(xt);
                    totalOut += props.crossTalkFraction;
                }
                if (totalOut > 0.0) {
                    G4double remain = d->GetEnergy() * (1.0 - totalOut);
                    d->SetEnergy(remain < 0.0 ? 0.0 : remain);
                }
                continue;
            }

            if (props.crossTalkProbability > 0.0) {
                if (detId + 1 > self->fMaxDetectorID) continue;
                if (G4UniformRand() >= props.crossTalkProbability) continue;
                Digit* xt = new Digit();
                self->FillDigit(xt, detId + 1, d->GetTime(), d->GetEnergy(), d->GetPosition(), d->GetTrackID(),
                                d->GetRawEnergy(), d->GetRawTime(), 0, false,
                                self->fDetReg->GetProperties(detId + 1).quantumEfficiency, 0.0, false);
                xtalk.push_back(xt);
            }
        }
        for (Digit* d : xtalk) ctx.digits->insert(d);
    }
};

/// Global coincidence sorter with multiples rejection / policy.
struct DigitizerModule::CoincidenceStage : public IDigitizerStage {
    void Process(DigitizerContext& ctx) override {
        DigitizerModule* self = ctx.owner;
        double window = 0.0;
        G4int multiplicity = 0;
        G4int minSectorDiff = 0;
        std::string policy = "reject";
        for (const auto& [sd, props] : self->fDetReg->GetAll()) {
            if (props.coincidenceWindow_ns > window) window = props.coincidenceWindow_ns;
            if (props.coincidenceMultiplicity > multiplicity) multiplicity = props.coincidenceMultiplicity;
            if (props.coincidenceMinSectorDiff > minSectorDiff) minSectorDiff = props.coincidenceMinSectorDiff;
            if (!props.coincidenceMultiplesPolicy.empty()) policy = props.coincidenceMultiplesPolicy;
        }
        if (window <= 0.0 || ctx.digits->GetSize() < 2) return;
        size_t n = ctx.digits->GetSize();
        std::vector<std::tuple<G4double, G4int, size_t>> singles;
        singles.reserve(n);
        for (size_t i = 0; i < n; ++i) singles.emplace_back((*ctx.digits)[i]->GetTime(), (*ctx.digits)[i]->GetDetectorID(), i);
        std::sort(singles.begin(), singles.end());

        std::vector<size_t> parent(n); for (size_t i = 0; i < n; ++i) parent[i] = i;
        std::function<size_t(size_t)> find = [&](size_t x){ return parent[x] == x ? x : (parent[x] = find(parent[x])); };
        auto unite = [&](size_t a, size_t b){ size_t ra = find(a), rb = find(b); if (ra != rb) parent[ra] = rb; };
        for (size_t i = 0; i < singles.size(); ++i) {
            for (size_t j = i + 1; j < singles.size(); ++j) {
                if (std::get<0>(singles[j]) - std::get<0>(singles[i]) > window) break;
                G4int di = std::get<1>(singles[i]), dj = std::get<1>(singles[j]);
                if (dj == di) continue;
                G4int ddiff = di - dj;
                if (ddiff < 0) ddiff = -ddiff;
                if (minSectorDiff > 0 && ddiff < minSectorDiff) continue;  // neighbour sectors forbidden
                unite(std::get<2>(singles[i]), std::get<2>(singles[j]));
            }
        }
        std::unordered_map<size_t, std::vector<size_t>> groups;
        for (size_t i = 0; i < n; ++i) groups[find(i)].push_back(i);
        G4int nextId = 0;
        for (auto& [root, members] : groups) {
            if (members.size() < 2) continue;
            if (policy == "keep_all") {
                for (size_t idx : members) (*ctx.digits)[idx]->SetCoincidenceID(nextId);
                ++nextId;
            } else if (multiplicity > 0 && static_cast<G4int>(members.size()) > multiplicity) {
                for (size_t idx : members) (*ctx.digits)[idx]->SetCoincidenceID(-2);  // rejected (multiple)
            } else {
                for (size_t idx : members) (*ctx.digits)[idx]->SetCoincidenceID(nextId);
                ++nextId;
            }
        }
    }
};

/// @brief Parses a pipeline step name into its enum value.
DigitizerModule::PipelineStep DigitizerModule::ParsePipelineStep(const std::string& name) {
    if (name == "efficiency") return kEfficiency;
    if (name == "signal_time") return kSignalTime;
    if (name == "time_smear" || name == "time_resolution") return kTimeSmear;
    if (name == "dead_time") return kDeadTime;
    if (name == "energy_smear" || name == "energy_resolution") return kEnergySmear;
    if (name == "gain") return kGain;
    if (name == "recovery") return kRecovery;
    if (name == "noise") return kNoise;
    if (name == "energy_framing" || name == "framing") return kEnergyFrame;
    if (name == "threshold") return kThreshold;
    if (name == "afterpulse") return kAfterpulse;
    return kInvalid;
}

/// @brief Returns the default (GATE-style) per-hit pipeline order.
const std::vector<DigitizerModule::PipelineStep>& DigitizerModule::DefaultPipeline() {
    static const std::vector<PipelineStep> steps = {
        kEfficiency, kSignalTime, kTimeSmear, kDeadTime, kEnergySmear, kGain,
        kRecovery, kNoise, kEnergyFrame, kThreshold, kAfterpulse
    };
    return steps;
}

/// @brief Returns the configured per-detector pipeline (or the default order).
const std::vector<DigitizerModule::PipelineStep>& DigitizerModule::GetPipeline(G4int detId) const {
    auto it = fPipeline.find(detId);
    if (it != fPipeline.end()) return it->second;
    return DefaultPipeline();
}

/// @brief Builds the ordered digitizer stage chains (per-detector + global).
void DigitizerModule::BuildChain() {
    fCollectionStage.reset(new HitCollectionStage());

    fGlobalChain.clear();
    fGlobalChain.emplace_back(new CrosstalkStage());
    fGlobalChain.emplace_back(new CoincidenceStage());

    fDetectorChains.clear();
    fPipeline.clear();
    std::unordered_set<G4int> calorimeterKeys;
    for (const auto& [sd, props] : fDetReg->GetAll()) {
        G4int detId = fDetReg->GetDetectorID(sd);
        if (detId < 0) continue;

        if (!props.pipeline.empty()) {
            std::vector<PipelineStep> steps;
            steps.reserve(props.pipeline.size());
            for (const auto& name : props.pipeline) {
                PipelineStep s = ParsePipelineStep(name);
                if (s == kInvalid) {
                    G4cerr << "[DigitizerModule] Unknown pipeline step '" << name
                           << "' for detector '" << sd << "' -> skipped." << G4endl;
                    continue;
                }
                steps.push_back(s);
            }
            fPipeline[detId] = std::move(steps);
        }

        auto& chain = fDetectorChains[detId];
        chain.emplace_back(new HitProcessingStage(detId));
        if (props.calorimeterMode) {
            G4int key = (props.readoutGroup >= 0) ? props.readoutGroup : detId;
            if (calorimeterKeys.insert(key).second) chain.emplace_back(new CalorimeterStage(detId));
        }
        if (props.pileupModel != "none" && props.pileupWindow_ns > 0.0) chain.emplace_back(new PileupStage(detId));
        if (props.darkCountRate_kHz > 0.0) chain.emplace_back(new DarkCountStage(detId));
    }
}

/// @brief Applies the detector gain factor to the signal energy.
/// @param energy Input energy.
/// @param props Detector properties with gain value.
/// @return energy * gain.
G4double DigitizerModule::ApplyGain(G4double energy, const Hit* hit, G4int eventId,
                                    const DetectorProperties& props) const {
    G4double g = props.gain;
    if (props.useGainExpression && !props.gainExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, energy, hit, eventId, props);
        g = fEval->EvaluateWithUnit(props.gainExpression, s_evalVarsCache, s_emptyVertexResults);
    }
    return energy * g;
}

/// @brief Applies noise (Gaussian or ExprTK expression) to the signal energy.
/// @param[in]  energy  Input energy in MeV.
/// @param[in]  hit     Source hit (for expression variables).
/// @param[in]  eventId Current event ID.
/// @param[in]  props   Detector properties.
/// @param[out] noise   The noise contribution actually added (MeV).
/// @return energy + noise (clamped to >= 0).
G4double DigitizerModule::ApplyNoise(G4double energy, const Hit* hit, G4int eventId,
                                     const DetectorProperties& props, G4double& noise) const {
    noise = 0.0;
    if (props.useNoiseExpression && !props.noiseExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, energy, hit, eventId, props);
        noise = fEval->EvaluateWithUnit(props.noiseExpression, s_evalVarsCache, s_emptyVertexResults) * MeV;
    } else if (props.noiseLevel_MeV > 0.0) {
        noise = CLHEP::RandGauss::shoot(0.0, props.noiseLevel_MeV);
    }
    G4double out = energy + noise;
    return (out < 0.0) ? 0.0 : out;
}

/// @brief Applies lower/upper energy thresholds (optionally expression-driven).
/// @return true if the energy survives both cuts.
bool DigitizerModule::PassThreshold(G4double energy, const Hit* hit, G4int eventId,
                                    const DetectorProperties& props) const {
    G4double lower = props.threshold_MeV;
    G4double upper = props.upperThreshold_MeV;
    if (props.useThresholdExpression && !props.thresholdExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, energy, hit, eventId, props);
        lower = fEval->EvaluateWithUnit(props.thresholdExpression, s_evalVarsCache, s_emptyVertexResults) * MeV;
    }
    if (props.useUpperThresholdExpression && !props.upperThresholdExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, energy, hit, eventId, props);
        upper = fEval->EvaluateWithUnit(props.upperThresholdExpression, s_evalVarsCache, s_emptyVertexResults) * MeV;
    }
    if (energy < lower) return false;
    if (upper > 0.0 && energy > upper) return false;
    return true;
}

/// @brief Applies a linear recovery attenuation for hits arriving during the
///        finite recovery window (after dead time, before full recovery).
/// @param detId Detector ID.
/// @param time  Signal time in ns.
/// @param energy Signal energy in MeV.
/// @param props Detector properties with deadTime_ns and recoveryTime_ns.
/// @return Attenuated energy (recovery factor in [0,1]).
G4double DigitizerModule::ApplyRecovery(G4int detId, G4double time, G4double energy, const DetectorProperties& props) {
    if (props.recoveryTime_ns <= 0.0 || props.deadTime_ns <= 0.0) return energy;
    if (props.deadTimeModel != "nonparalyzable") return energy;
    if (fLastHitTime[detId] <= kNeverHit) return energy;
    G4double dt = time - fLastHitTime[detId];
    if (dt < props.deadTime_ns) return energy;
    if (dt >= props.deadTime_ns + props.recoveryTime_ns) return energy;
    G4double frac = (dt - props.deadTime_ns) / props.recoveryTime_ns;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return energy * frac;

}
/// @brief Returns the detection efficiency probability for a given energy.
/// @param energy Signal energy in internal units (MeV).
/// @param props Detector properties (efficiencyMode, quantumEfficiency, ...).
/// @return Probability in [0,1].
G4double DigitizerModule::EfficiencyProbability(G4double energy, const Hit* hit, G4int eventId,
                                                const DetectorProperties& props) const {
    if (props.useEfficiencyExpression && !props.efficiencyExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, energy, hit, eventId, props);
        double p = fEval->EvaluateWithUnit(props.efficiencyExpression, s_evalVarsCache, s_emptyVertexResults);
        if (p < 0.0) p = 0.0;
        if (p > 1.0) p = 1.0;
        return p;
    }
    // File-based efficiency curve (linear interpolation between points).
    if (!props.efficiencyCurve.empty()) {
        double e = energy / MeV;
        if (e <= props.efficiencyCurve.front().first) return props.quantumEfficiency * props.efficiencyCurve.front().second;
        if (e >= props.efficiencyCurve.back().first) return props.quantumEfficiency * props.efficiencyCurve.back().second;
        for (size_t i = 1; i < props.efficiencyCurve.size(); ++i) {
            if (e <= props.efficiencyCurve[i].first) {
                double e0 = props.efficiencyCurve[i-1].first, e1 = props.efficiencyCurve[i].first;
                double p0 = props.efficiencyCurve[i-1].second, p1 = props.efficiencyCurve[i].second;
                double t = (e - e0) / (e1 - e0);
                return props.quantumEfficiency * (p0 + t * (p1 - p0));
            }
        }
    }
    if (props.efficiencyMode == "energy" && props.efficiencyFWHM_MeV > 0.0) {
        double sigma = props.efficiencyFWHM_MeV / 2.355;
        double d = (energy / MeV - props.efficiencyCenter_MeV) / sigma;
        return props.quantumEfficiency * std::exp(-0.5 * d * d);
    }
    return props.quantumEfficiency;  // "constant" (scalar QE)
}

/// @brief Applies gaussian spatial-resolution + depth-of-interaction smearing.
/// @param pos   Position before smearing (internal units, mm).
/// @param props Detector properties with spatialResFWHM_mm / doiResolution_mm.
/// @return Smeared position.
G4ThreeVector DigitizerModule::ApplySpatialResolution(const G4ThreeVector& pos, const DetectorProperties& props) const {
    double sigmaS = props.spatialResFWHM_mm / 2.355;
    double sigmaDoI = props.doiResolution_mm / 2.355;
    if (sigmaS <= 0.0 && sigmaDoI <= 0.0 && !props.spatialConfine) return pos;
    double x = (sigmaS > 0.0) ? CLHEP::RandGauss::shoot(pos.x(), sigmaS) : pos.x();
    double y = (sigmaS > 0.0) ? CLHEP::RandGauss::shoot(pos.y(), sigmaS) : pos.y();
    // DoI smears along z (crystal depth); spatial resolution covers x/y
    // (and z as well when no DoI is configured).
    double z = (sigmaDoI > 0.0) ? CLHEP::RandGauss::shoot(pos.z(), sigmaDoI)
                               : ((sigmaS > 0.0) ? CLHEP::RandGauss::shoot(pos.z(), sigmaS) : pos.z());
    if (props.spatialConfine) {
        // Confined (truncated) Gaussian: clamp the displacement to +/-half-width.
        double dx = x - pos.x();
        double dy = y - pos.y();
        double dz = z - pos.z();
        if (props.spatialHalfX_mm > 0.0) dx = std::max(-props.spatialHalfX_mm, std::min(dx, props.spatialHalfX_mm));
        if (props.spatialHalfY_mm > 0.0) dy = std::max(-props.spatialHalfY_mm, std::min(dy, props.spatialHalfY_mm));
        if (props.spatialHalfZ_mm > 0.0) dz = std::max(-props.spatialHalfZ_mm, std::min(dz, props.spatialHalfZ_mm));
        x = pos.x() + dx;
        y = pos.y() + dy;
        z = pos.z() + dz;
    }
    return G4ThreeVector(x, y, z);
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

/// @brief Populates an expression variable map with per-hit quantities shared
///        by signal-time, time-smearing and energy-smearing expressions.
/// @param[out] vars    Map to fill.
/// @param hit          Source hit (per-hit attributes).
/// @param energy       Hit energy in internal units.
/// @param eventId      Current event ID.
void DigitizerModule::PrepareHitVars(std::map<std::string, double>& vars, const Hit* hit,
                                     G4double energy, G4int eventId) const {
    vars["energy_MeV"] = energy / MeV;
    vars["detector_id"] = static_cast<double>(hit->GetDetectorID());
    vars["track_id"] = static_cast<double>(hit->GetTrackID());
    vars["event_id"] = static_cast<double>(eventId);
    vars["pos_x_mm"] = hit->GetPosition().x() / mm;
    vars["pos_y_mm"] = hit->GetPosition().y() / mm;
    vars["pos_z_mm"] = hit->GetPosition().z() / mm;
}

/// @brief Populates an expression variable map with detector-property quantities
///        shared by all signal/time/energy/pile-up expressions.
/// @param[out] vars    Map to fill.
/// @param props        Detector properties.
void DigitizerModule::PrepareCommonVars(std::map<std::string, double>& vars,
                                        const DetectorProperties& props) const {
    vars["dead_time_ns"] = props.deadTime_ns;
    vars["time_res_fwhm_ns"] = props.timeResFWHM_ns;
    vars["energy_res_fwhm"] = props.energyResFWHM;
    vars["energy_ref_MeV"] = props.energyResRefEnergy_MeV;
    vars["intrinsic_res_fwhm_MeV"] = props.intrinsicResFWHM_MeV;
    vars["signal_rise_ns"] = props.signalRiseTime_ns;
    vars["signal_fall_ns"] = props.signalFallTime_ns;
    vars["quantum_efficiency"] = props.quantumEfficiency;
    vars["gain"] = props.gain;
    vars["threshold_MeV"] = props.threshold_MeV;
    vars["upper_threshold_MeV"] = props.upperThreshold_MeV;
    vars["noise_MeV"] = props.noiseLevel_MeV;
    vars["energy_frame_MeV"] = props.energyFrame_MeV;
    vars["pileup_window_ns"] = props.pileupWindow_ns;
    vars["spatial_res_fwhm_mm"] = props.spatialResFWHM_mm;
    vars["doi_resolution_mm"] = props.doiResolution_mm;
    vars["light_yield"] = props.lightYield;
    vars["transfer_efficiency"] = props.transferEfficiency;
    vars["intrinsic_res_fwhm_frac"] = props.intrinsicResFWHM_frac;
    vars["intrinsic_res_ref_energy_MeV"] = props.intrinsicResRefEnergy_MeV;
}

/// @brief Populates an expression variable map with time-related quantities.
/// @param[out] vars    Map to fill.
/// @param rawTime      Raw hit time in internal units.
/// @param hit          Source hit.
/// @param eventId      Current event ID.
/// @param props        Detector properties with timing parameters.
void DigitizerModule::PrepareTimeVars(std::map<std::string, double>& vars, G4double rawTime, const Hit* hit,
                                      G4int eventId, const DetectorProperties& props) const {
    PrepareHitVars(vars, hit, hit->GetEdep(), eventId);
    PrepareCommonVars(vars, props);
    vars["time_ns"] = rawTime / ns;
}

/// @brief Populates an expression variable map with energy-related quantities.
/// @param[out] vars    Map to fill.
/// @param rawEnergy    Raw hit energy in internal units.
/// @param hit          Source hit.
/// @param eventId      Current event ID.
/// @param props        Detector properties with energy resolution parameters.
void DigitizerModule::PrepareEnergyVars(std::map<std::string, double>& vars, G4double rawEnergy, const Hit* hit,
                                        G4int eventId, const DetectorProperties& props) const {
    PrepareHitVars(vars, hit, rawEnergy, eventId);
    PrepareCommonVars(vars, props);
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
    PrepareCommonVars(vars, props);
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
        double result = fEval->EvaluateWithUnit(props.timeSmearExpression, s_evalVarsCache, s_emptyVertexResults);
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
                                              const DetectorProperties& props, G4int eventId) const {
    if (props.useEnergySmearExpression && !props.energySmearExpression.empty() && fEval) {
        s_evalVarsCache.clear();
        PrepareEnergyVars(s_evalVarsCache, rawEnergy, hit, eventId, props);
        double result = fEval->EvaluateWithUnit(props.energySmearExpression, s_evalVarsCache, s_emptyVertexResults);
        return result * MeV;
    }

    if (props.energyResFWHM <= 0.0 && props.intrinsicResFWHM_MeV <= 0.0 &&
        props.lightYield <= 0.0 && props.energySmearModel != "landau") return rawEnergy;

    if (props.energySmearModel == "gaussian") {
        double sigma;
        if (props.lightYield > 0.0) {
            // GATE light-yield model: R = sqrt(1.1/N_pe * 2.355^2 + Ri^2), sigma = R*E/2.355.
            double Npe = rawEnergy * props.lightYield * props.transferEfficiency * props.quantumEfficiency;
            double statSq = (Npe > 1e-12) ? (1.1 / Npe) * 2.355 * 2.355 : 0.0;
            double Ri = (props.intrinsicResFWHM_frac > 0.0)
                        ? props.intrinsicResFWHM_frac * std::sqrt(props.intrinsicResRefEnergy_MeV / rawEnergy) : 0.0;
            double R = std::sqrt(statSq + Ri * Ri);
            sigma = R * rawEnergy / 2.355;
        } else {
            double sigmaStat = (props.energyResFWHM / 2.355) * std::sqrt(rawEnergy / props.energyResRefEnergy_MeV);
            double sigmaIntr = props.intrinsicResFWHM_MeV / 2.355;
            sigma = std::sqrt(sigmaStat * sigmaStat + sigmaIntr * sigmaIntr);
        }
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
        double sumRawEnergy = hits[start].rawEnergy;
        double weightedTime = hits[start].time * hits[start].energy;
        double maxEnergy = hits[start].energy;
        G4int detId = hits[start].detectorID;
        G4ThreeVector pos = hits[start].position;
        G4int trackId = hits[start].trackID;
        G4double rawTime = hits[start].rawTime;

        G4int maxGroup = (props.pileupBufferSize > 0) ? props.pileupBufferSize : 1000000000;
        while (end < hits.size() && (hits[end].time - hits[start].time) < props.pileupWindow_ns && static_cast<G4int>(end - start) < maxGroup) {
            sumEnergy += hits[end].energy;
            sumRawEnergy += hits[end].rawEnergy;
            weightedTime += hits[end].time * hits[end].energy;
            if (hits[end].energy > maxEnergy) maxEnergy = hits[end].energy;
            ++end;
        }

        int pileupSize = static_cast<int>(end - start);
        double avgTime = (sumEnergy > 1e-12) ? (weightedTime / sumEnergy) : hits[start].time;
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
                  sumRawEnergy, rawTime, pileupSize, true,
                  props.quantumEfficiency, 0.0, afterpulse);
        digitCol->insert(digit);
        UpdateDeadTime(detId, avgTime, props);
        start = end;
    }
}