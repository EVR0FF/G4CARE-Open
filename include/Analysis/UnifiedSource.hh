//==============================================================================
// G4CARE
// @file    UnifiedSource.hh
// @brief   Type-erased wrapper that presents a uniform interface over
//          primary vertices, secondary tracks, G4Steps (hits), and Digits
//          for data extraction and filter evaluation.
// @details UnifiedSource stores one of four possible source types and
//   provides accessor methods for all common quantities (PDG, mass, charge,
//   kinetic energy, momentum, position, time, volume, material, process,
//   ion properties, isotope info, chemical species, etc.).  Accessors return
//   type-appropriate values or sensible defaults when the requested quantity
//   is not available for the stored source type.
//
//   Configuration keys read: none (utility class).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef UNIFIED_SOURCE_HH
#define UNIFIED_SOURCE_HH

#include "G4PrimaryVertex.hh"
#include "G4PrimaryParticle.hh"
#include "G4Track.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4VProcess.hh"
#include "G4ParticleDefinition.hh"
#include "G4ParticleTable.hh"
#include "G4Ions.hh"
#include "G4ThreeVector.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4Region.hh"
#include "G4ProductionCuts.hh"
#include "G4VUserTrackInformation.hh"
#include "G4VUserPrimaryVertexInformation.hh"
#include "G4VUserPrimaryParticleInformation.hh"
#include "G4VUserRegionInformation.hh"
#include "G4SystemOfUnits.hh"
#include "Digit.hh"

class UnifiedSource {
public:
    enum class Type { Primary, Secondary, Hit, Digit };

    const Digit* GetDigit() const { return (fType == Type::Digit) ? fDigit : nullptr; }

    // Constructors
    UnifiedSource(const G4PrimaryVertex* v, const G4PrimaryParticle* p)
        : fType(Type::Primary), fVertex(v), fParticle(p), fTrack(nullptr), fStep(nullptr) {}

    explicit UnifiedSource(const G4Track* t)
        : fType(Type::Secondary), fVertex(nullptr), fParticle(nullptr), fTrack(t), fStep(nullptr) {}

    explicit UnifiedSource(const G4Step* s)
        : fType(Type::Hit), fVertex(nullptr), fParticle(nullptr), fTrack(nullptr), fStep(s) {}

    explicit UnifiedSource(const Digit* d)
        : fType(Type::Digit), fDigit(d), fVertex(nullptr), fParticle(nullptr), fTrack(nullptr), fStep(nullptr) {}

    [[nodiscard]] Type GetType() const noexcept { return fType; }

    // ========== Basic identifiers ==========
    [[nodiscard]] int GetTrackID() const noexcept {
        if (fType == Type::Primary) return 0;
        if (fType == Type::Secondary) return fTrack->GetTrackID();
        if (fType == Type::Digit) return fDigit->GetTrackID();
        return fStep->GetTrack()->GetTrackID();
    }

    [[nodiscard]] int GetParentID() const noexcept {
        if (fType == Type::Primary) return 0;
        if (fType == Type::Secondary) return fTrack->GetParentID();
        return fStep->GetTrack()->GetParentID();
    }

    [[nodiscard]] const G4Track* GetTrack() const noexcept {
        if (fType == Type::Secondary) return fTrack;
        if (fType == Type::Hit) return fStep->GetTrack();
        return nullptr;
    }

    // ========== Particle parameters ==========
    [[nodiscard]] const G4ParticleDefinition* GetParticleDefinition() const noexcept {
        if (fType == Type::Primary) {
            return G4ParticleTable::GetParticleTable()->FindParticle(fParticle->GetPDGcode());
        }
        if (fType == Type::Secondary) return fTrack->GetParticleDefinition();
        return fStep->GetTrack()->GetParticleDefinition();
    }

    [[nodiscard]] double GetMass() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGMass() : 0.0;
    }

    [[nodiscard]] double GetCharge() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGCharge() : 0.0;
    }

    [[nodiscard]] double GetPDGCode() const noexcept {
        if (fType == Type::Primary) return static_cast<double>(fParticle->GetPDGcode());
        auto* def = GetParticleDefinition();
        return def ? static_cast<double>(def->GetPDGEncoding()) : 0.0;
    }

    // ========== Energy and kinematics ==========
    [[nodiscard]] double GetKineticEnergy() const noexcept {
        if (fType == Type::Primary) return fParticle->GetKineticEnergy();
        if (fType == Type::Secondary) return fTrack->GetKineticEnergy();
        if (fType == Type::Digit) return fDigit->GetEnergy();
        return fStep->GetPostStepPoint()->GetKineticEnergy();
    }

    [[nodiscard]] double GetPreKineticEnergy() const noexcept {
        if (fType == Type::Hit && fStep->GetPreStepPoint())
            return fStep->GetPreStepPoint()->GetKineticEnergy();
        return GetKineticEnergy();
    }

    // ========== Momentum and direction ==========
    [[nodiscard]] G4ThreeVector GetMomentum() const noexcept {
        if (fType == Type::Primary) return G4ThreeVector(fParticle->GetPx(), fParticle->GetPy(), fParticle->GetPz());
        if (fType == Type::Secondary) return fTrack->GetMomentum();
        return fStep->GetPostStepPoint()->GetMomentum();
    }

    [[nodiscard]] G4ThreeVector GetMomentumDirection() const noexcept {
        G4ThreeVector p = GetMomentum();
        double mag = p.mag();
        return mag > 0 ? p / mag : G4ThreeVector(0,0,1);
    }

    [[nodiscard]] G4ThreeVector GetPreMomentum() const noexcept {
        if (fType == Type::Hit && fStep->GetPreStepPoint())
            return fStep->GetPreStepPoint()->GetMomentum();
        return GetMomentum();
    }

    // ========== Polarization ==========
    [[nodiscard]] G4ThreeVector GetPolarization() const noexcept {
        if (fType == Type::Primary) return G4ThreeVector(fParticle->GetPolX(), fParticle->GetPolY(), fParticle->GetPolZ());
        if (fType == Type::Secondary) return fTrack->GetPolarization();
        return G4ThreeVector();
    }

    // ========== Space and time ==========
    [[nodiscard]] G4ThreeVector GetPosition() const noexcept {
        if (fType == Type::Primary) return fVertex->GetPosition();
        if (fType == Type::Secondary) return fTrack->GetPosition();
        if (fType == Type::Digit) return fDigit->GetPosition();
        return fStep->GetPostStepPoint()->GetPosition();
    }

    [[nodiscard]] double GetGlobalTime() const noexcept {
        if (fType == Type::Primary) return fVertex->GetT0();
        if (fType == Type::Secondary) return fTrack->GetGlobalTime();
        if (fType == Type::Digit) return fDigit->GetTime();
        return fStep->GetPostStepPoint()->GetGlobalTime();
    }

    [[nodiscard]] double GetLocalTime() const noexcept {
        if (fType == Type::Primary) return 0.0;
        if (fType == Type::Secondary) return fTrack->GetLocalTime();
        return fStep->GetTrack()->GetLocalTime();
    }

    [[nodiscard]] double GetProperTime() const noexcept {
        if (fType == Type::Primary) return fParticle->GetProperTime();
        if (fType == Type::Secondary) return fTrack->GetProperTime();
        return fStep->GetTrack()->GetProperTime();
    }

    [[nodiscard]] double GetWeight() const noexcept {
        if (fType == Type::Primary) return fParticle->GetWeight();
        if (fType == Type::Secondary) return fTrack->GetWeight();
        return fStep->GetTrack()->GetWeight();
    }

    // ========== Quantum numbers ==========
    [[nodiscard]] int GetSpin() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGiSpin() : 0;
    }

    [[nodiscard]] int GetParity() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGiParity() : 0;
    }

    [[nodiscard]] int GetConjugation() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGiConjugation() : 0;
    }

    [[nodiscard]] int GetIsospin() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGiIsospin() : 0;
    }

    [[nodiscard]] int GetIsospin3() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGiIsospin3() : 0;
    }

    [[nodiscard]] int GetIsomerLevel() const noexcept {
        auto* def = GetParticleDefinition();
        if (!def || !def->IsGeneralIon()) return 0;
        auto* ion = dynamic_cast<const G4Ions*>(def);
        return ion ? ion->GetIsomerLevel() : 0;
    }

    [[nodiscard]] int GetGParity() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGiGParity() : 0;
    }

    [[nodiscard]] double GetLifeTime() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGLifeTime() : 0.0;
    }

    [[nodiscard]] double GetDecayWidth() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetPDGWidth() : 0.0;
    }

    [[nodiscard]] int GetLeptonNumber() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetLeptonNumber() : 0;
    }

    [[nodiscard]] int GetBaryonNumber() const noexcept {
        auto* def = GetParticleDefinition();
        return def ? def->GetBaryonNumber() : 0;
    }

    // ========== Ions ==========
    [[nodiscard]] int GetAtomicNumber() const noexcept {
        auto* def = GetParticleDefinition();
        return (def && def->IsGeneralIon()) ? def->GetAtomicNumber() : 0;
    }

    [[nodiscard]] int GetAtomicMass() const noexcept {
        auto* def = GetParticleDefinition();
        return (def && def->IsGeneralIon()) ? def->GetAtomicMass() : 0;
    }

    [[nodiscard]] double GetExcitationEnergy() const noexcept {
        auto* def = GetParticleDefinition();
        if (def && def->IsGeneralIon()) {
            if (auto* ion = dynamic_cast<const G4Ions*>(def)) {
                return ion->GetExcitationEnergy();
            }
        }
        return 0.0;
    }

    // ========== Processes ==========
    [[nodiscard]] const G4VProcess* GetCreatorProcess() const noexcept {
        if (fType == Type::Primary) return nullptr;
        if (fType == Type::Secondary) return fTrack->GetCreatorProcess();
        if (fType == Type::Hit && fStep->GetTrack()->GetCreatorProcess())
            return fStep->GetTrack()->GetCreatorProcess();
        return nullptr;
    }

    [[nodiscard]] G4int GetCreatorProcessSubType() const noexcept {
        auto* creator = GetCreatorProcess();
        return creator ? creator->GetProcessSubType() : 0;
    }

    [[nodiscard]] G4int GetProcessSubType() const noexcept {
        auto* proc = GetCreatorProcess();
        return proc ? proc->GetProcessSubType() : -1;
    }

    [[nodiscard]] G4ProcessType GetProcessType() const noexcept {
        auto* proc = GetCreatorProcess();
        return proc ? proc->GetProcessType() : fNotDefined;
    }

    [[nodiscard]] const G4String& GetProcessName() const noexcept {
        if (fType == Type::Hit) {
            auto* proc = fStep->GetPostStepPoint()->GetProcessDefinedStep();
            if (proc) return proc->GetProcessName();
        }
        static G4String empty;
        return empty;
    }

    [[nodiscard]] const G4String& GetCreatorProcessName() const noexcept {
        auto* creator = GetCreatorProcess();
        static G4String empty;
        return creator ? creator->GetProcessName() : empty;
    }

    [[nodiscard]] double GetCurrentInteractionLength() const noexcept {
        auto* proc = GetCreatorProcess();
        return proc ? proc->GetCurrentInteractionLength() : -1.0;
    }

    // ========== Track parameters ==========
    [[nodiscard]] double GetTrackLength() const noexcept {
        if (fType == Type::Secondary) return fTrack->GetTrackLength();
        if (fType == Type::Hit) return fStep->GetTrack()->GetTrackLength();
        return 0.0;
    }

    [[nodiscard]] G4TrackStatus GetTrackStatus() const noexcept {
        if (fType == Type::Secondary) return fTrack->GetTrackStatus();
        if (fType == Type::Hit) return fStep->GetTrack()->GetTrackStatus();
        return fAlive;
    }

    // ========== Vertex ==========
    [[nodiscard]] G4ThreeVector GetVertexPosition() const noexcept {
        if (fType == Type::Secondary) return fTrack->GetVertexPosition();
        if (fType == Type::Hit) return fStep->GetTrack()->GetVertexPosition();
        return G4ThreeVector();
    }

    [[nodiscard]] double GetVertexKineticEnergy() const noexcept {
        if (fType == Type::Secondary) return fTrack->GetVertexKineticEnergy();
        if (fType == Type::Hit) return fStep->GetTrack()->GetVertexKineticEnergy();
        return 0.0;
    }

    [[nodiscard]] int GetVertexPDGCode() const noexcept { return 0; }

    // ========== Step parameters (Hit only) ==========
    [[nodiscard]] const G4Step* GetStep() const noexcept { return (fType == Type::Hit) ? fStep : nullptr; }
    [[nodiscard]] const G4StepPoint* GetPreStepPoint() const noexcept { return (fType == Type::Hit) ? fStep->GetPreStepPoint() : nullptr; }
    [[nodiscard]] const G4StepPoint* GetPostStepPoint() const noexcept { return (fType == Type::Hit) ? fStep->GetPostStepPoint() : nullptr; }

    [[nodiscard]] double GetTotalEnergyDeposit() const noexcept { return (fType == Type::Hit) ? fStep->GetTotalEnergyDeposit() : 0.0; }
    [[nodiscard]] double GetNonIonizingEnergyDeposit() const noexcept { return (fType == Type::Hit) ? fStep->GetNonIonizingEnergyDeposit() : 0.0; }
    [[nodiscard]] double GetDeltaEnergy() const noexcept { return (fType == Type::Hit) ? fStep->GetDeltaEnergy() : 0.0; }
    [[nodiscard]] double GetStepLength() const noexcept { return (fType == Type::Hit) ? fStep->GetStepLength() : 0.0; }
    [[nodiscard]] size_t GetNumberOfSecondaries() const noexcept { return (fType == Type::Hit) ? fStep->GetNumberOfSecondariesInCurrentStep() : 0; }
    [[nodiscard]] double GetDeltaTime() const noexcept { return (fType == Type::Hit) ? fStep->GetDeltaTime() : 0.0; }

    [[nodiscard]] G4ThreeVector GetDeltaPosition() const noexcept {
        if (fType != Type::Hit) return G4ThreeVector();
        auto* pre = fStep->GetPreStepPoint();
        auto* post = fStep->GetPostStepPoint();
        return post->GetPosition() - pre->GetPosition();
    }

    [[nodiscard]] G4ThreeVector GetDeltaMomentum() const noexcept {
        if (fType != Type::Hit) return G4ThreeVector();
        auto* pre = fStep->GetPreStepPoint();
        auto* post = fStep->GetPostStepPoint();
        return post->GetMomentum() - pre->GetMomentum();
    }

    [[nodiscard]] double GetSafety() const noexcept {
        auto* point = (fType == Type::Hit) ? fStep->GetPostStepPoint() : nullptr;
        return point ? point->GetSafety() : -1.0;
    }

    [[nodiscard]] G4StepStatus GetStepStatus() const noexcept {
        auto* point = (fType == Type::Hit) ? fStep->GetPostStepPoint() : nullptr;
        return point ? point->GetStepStatus() : fWorldBoundary;
    }

    [[nodiscard]] bool IsFirstStepInVolume() const noexcept {
        if (fType != Type::Hit) return false;
        return fStep->IsFirstStepInVolume();
    }

    [[nodiscard]] bool IsLastStepInVolume() const noexcept {
        if (fType != Type::Hit) return false;
        return fStep->IsLastStepInVolume();
    }

    [[nodiscard]] int GetStepNumber() const noexcept {
    if (fType == Type::Hit && fStep && fStep->GetTrack()) {
        return fStep->GetTrack()->GetCurrentStepNumber();
    }
        if (fType == Type::Secondary && fTrack) {
            return fTrack->GetCurrentStepNumber();
        }
        return 0;
    }

    // ========== Geometry, material, region ==========
    [[nodiscard]] const G4VPhysicalVolume* GetPhysicalVolume() const noexcept {
        if (fType == Type::Primary) return nullptr;
        if (fType == Type::Secondary) return fTrack->GetVolume();
        return fStep->GetPreStepPoint()->GetPhysicalVolume();
    }

    [[nodiscard]] const G4LogicalVolume* GetLogicalVolume() const noexcept {
        auto* pv = GetPhysicalVolume();
        return pv ? pv->GetLogicalVolume() : nullptr;
    }

    [[nodiscard]] const G4Material* GetMaterial() const noexcept {
        auto* lv = GetLogicalVolume();
        return lv ? lv->GetMaterial() : nullptr;
    }

    [[nodiscard]] const G4Region* GetRegion() const noexcept {
        auto* lv = GetLogicalVolume();
        return lv ? lv->GetRegion() : nullptr;
    }

    [[nodiscard]] size_t GetRegionID() const noexcept {
        auto* region = GetRegion();
        return region ? region->GetInstanceID() : 0;
    }

    [[nodiscard]] double GetProductionCut(const G4String& particleName) const noexcept {
        auto* lv = GetLogicalVolume();
        if (!lv) return 0.0;
        auto* region = lv->GetRegion();
        if (!region) return 0.0;
        auto* cuts = region->GetProductionCuts();
        if (!cuts) return 0.0;
        return cuts->GetProductionCut(particleName);
    }

    // ========== User data ==========
    [[nodiscard]] const G4VUserTrackInformation* GetUserTrackInformation() const noexcept {
        if (fType == Type::Secondary) return fTrack->GetUserInformation();
        if (fType == Type::Hit) return fStep->GetTrack()->GetUserInformation();
        return nullptr;
    }

    [[nodiscard]] const G4VUserPrimaryVertexInformation* GetUserVertexInformation() const noexcept {
        return (fType == Type::Primary && fVertex) ? fVertex->GetUserInformation() : nullptr;
    }

    [[nodiscard]] const G4VUserPrimaryParticleInformation* GetUserParticleInformation() const noexcept {
        return (fType == Type::Primary && fParticle) ? fParticle->GetUserInformation() : nullptr;
    }

    [[nodiscard]] const G4VUserRegionInformation* GetUserRegionInformation() const noexcept {
        auto* region = GetRegion();
        return region ? region->GetUserInformation() : nullptr;
    }
    [[nodiscard]] std::string GetChemicalSpeciesName() const noexcept {
        if (fType == Type::Hit && fStep) {
            auto* track = fStep->GetTrack();
            if (track) {
                return track->GetParticleDefinition()->GetParticleName();
            }
        }
        return "";
    }

private:
    Type fType;
    const Digit* fDigit;
    const G4PrimaryVertex* fVertex;
    const G4PrimaryParticle* fParticle;
    const G4Track* fTrack;
    const G4Step* fStep;
};

#endif // UNIFIED_SOURCE_HH