//==============================================================================
// G4CARE
// @file    StepExtractor.cc
// @brief   Extracts step-level quantities (Edep, NIEL, DPA, step length,
//          time delta, momentum components, status, safety, etc.) from Geant4
//          step data.
// @details StepExtractor computes energy deposition (Edep, DeltaE), NIEL with
//   a heavy-ion fallback, DPA via the NRT (Norgett-Robinson-Torrens) formula
//   using element-specific threshold displacement energies, step length, time
//   delta, number of secondaries, step number, pre/post momentum components,
//   step status, safety, first/last-in-volume flags, and delta position/momentum.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "StepExtractor.hh"
#include "G4SystemOfUnits.hh"
#include "G4Material.hh"
#include "G4Step.hh"

/// @brief Extracts a step-level numeric column value.
/// @param type Column type (Edep, DeltaE, NIEL, DPA, StepLength, DeltaTime,
///        NSecondaries, StepNumber, PrePx/Py/Pz, PostPx/Py/Pz, StepStatus,
///        Safety, IsFirstStepInVolume, IsLastStepInVolume, DeltaPositionX/Y/Z,
///        DeltaMomentumX/Y/Z).
/// @param src  UnifiedSource providing G4Step data (must be Hit type).
/// @param eventId Event ID (unused).
/// @param volId   Volume ID (unused).
/// @param matId   Material ID (unused).
/// @return Extracted value, or 0.0 if source is not a Hit.
double StepExtractor::Get(ColType type, const UnifiedSource& src, int /*eventId*/, int /*volId*/, int /*matId*/) const {
    if (src.GetType() != UnifiedSource::Type::Hit) return 0.0;

    switch (type) {
        case ColType::Edep:
            return src.GetTotalEnergyDeposit() / MeV;

        case ColType::DeltaE:
            return src.GetDeltaEnergy() / MeV;

        case ColType::NIEL: {
            // Geant4 automatically computes NIEL in EM processes as
            // sum of energy transfer to atomic nuclei (ionisation + elastic scattering).
            // Requires /process/em/nuclearStopping true (enabled in PhysicsManager).
            double niel = src.GetNonIonizingEnergyDeposit();
            if (niel <= 0.0) {
                // Fallback: heavy recoil nuclei killed below production cut
                // For heavy ions (A>4, Z>2) at ~100 keV, electronic stopping is
                // negligible — nearly 100% of energy goes to nuclear stopping (NIEL)
                // per Lindhard theory.
                auto* trk = src.GetTrack();
                if (trk) {
                    auto* def = trk->GetParticleDefinition();
                    if (def) {
                        G4int Z = def->GetAtomicNumber();
                        G4int A = def->GetAtomicMass();
                        if (A > 4 && Z > 2) {
                            niel = src.GetTotalEnergyDeposit();
                        }
                    }
                }
            }
            return niel / MeV;
        }

        case ColType::DPA: {
            // NRT (Norgett-Robinson-Torrens) formula:
            //   N_disp = 0.8 * T_dam / (2 * E_d)
            // Returns NUMBER OF DISPLACED ATOMS (not DPA!).
            // DPA = sum(N_disp) / total_atoms_in_volume (computed in post-processing).
            // T_dam = NIEL (damage energy), E_d = threshold displacement energy.
            double niel_MeV = src.GetNonIonizingEnergyDeposit() / MeV;
            if (niel_MeV <= 0.0) {
                // Fallback: heavy recoil nuclei killed below production cut
                auto* trk = src.GetTrack();
                if (trk) {
                    auto* def = trk->GetParticleDefinition();
                    if (def) {
                        G4int Z = def->GetAtomicNumber();
                        G4int A = def->GetAtomicMass();
                        if (A > 4 && Z > 2) {
                            niel_MeV = src.GetTotalEnergyDeposit() / MeV;
                        }
                    }
                }
            }
            if (niel_MeV <= 0.0) return 0.0;

            double ed_eV = 25.0; // default for silicate minerals

            const G4Material* mat = src.GetMaterial();
            if (mat) {
                // Get effective E_d from weighted average of element thresholds
                const G4ElementVector* elements = mat->GetElementVector();
                const double* fractions = mat->GetFractionVector();
                size_t nEl = mat->GetNumberOfElements();

                double sumWeight = 0.0;
                double edSum = 0.0;

                for (size_t i = 0; i < nEl; ++i) {
                    double Z = (*elements)[i]->GetZ();
                    double frac = fractions ? fractions[i] : 1.0 / nEl;
                    double ed_i = 25.0; // default

                    // Threshold displacement energies from literature
                    // (ASTM E521-96, Lucasson 1975, Was "Fundamentals of Radiation Materials Science")
                    if (Z == 1)  ed_i = 10.0;   // H
                    else if (Z == 8)  ed_i = 28.0;   // O (in oxides)
                    else if (Z == 11) ed_i = 15.0;   // Na
                    else if (Z == 12) ed_i = 25.0;   // Mg (in MgO)
                    else if (Z == 13) ed_i = 25.0;   // Al (in Al₂O₃, average)
                    else if (Z == 14) ed_i = 35.0;   // Si (in SiO₂)

                    edSum += ed_i * frac;
                    sumWeight += frac;
                }

                if (sumWeight > 0.0) {
                    ed_eV = edSum / sumWeight;
                }
            }

            // NRT formula
            double T_dam_eV = niel_MeV * 1.0e6; // convert MeV → eV
            double num_displacements = 0.8 * T_dam_eV / (2.0 * ed_eV);

            return num_displacements;
        }

        case ColType::StepLength:
            return src.GetStepLength() / mm;

        case ColType::DeltaTime:
            return src.GetDeltaTime() / ns;

        case ColType::NSecondaries:
            return static_cast<double>(src.GetNumberOfSecondaries());

        case ColType::StepNumber:
            return static_cast<double>(src.GetStepNumber());

        case ColType::PrePx: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetMomentum().x() / MeV : 0.0;
        }
        case ColType::PrePy: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetMomentum().y() / MeV : 0.0;
        }
        case ColType::PrePz: {
            auto* pre = src.GetPreStepPoint();
            return pre ? pre->GetMomentum().z() / MeV : 0.0;
        }
        case ColType::PostPx:
            return src.GetMomentum().x() / MeV;
        case ColType::PostPy:
            return src.GetMomentum().y() / MeV;
        case ColType::PostPz:
            return src.GetMomentum().z() / MeV;

        case ColType::StepStatus:
            return static_cast<double>(src.GetStepStatus());

        case ColType::Safety:
            return src.GetSafety() / mm;

        case ColType::IsFirstStepInVolume:
            return src.IsFirstStepInVolume() ? 1.0 : 0.0;

        case ColType::IsLastStepInVolume:
            return src.IsLastStepInVolume() ? 1.0 : 0.0;

        case ColType::DeltaPositionX:
            return src.GetDeltaPosition().x() / mm;
        case ColType::DeltaPositionY:
            return src.GetDeltaPosition().y() / mm;
        case ColType::DeltaPositionZ:
            return src.GetDeltaPosition().z() / mm;

        case ColType::DeltaMomentumX:
            return src.GetDeltaMomentum().x() / MeV;
        case ColType::DeltaMomentumY:
            return src.GetDeltaMomentum().y() / MeV;
        case ColType::DeltaMomentumZ:
            return src.GetDeltaMomentum().z() / MeV;

        default:
            return 0.0;
    }
}