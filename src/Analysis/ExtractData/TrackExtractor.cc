//==============================================================================
// G4CARE
// @file    TrackExtractor.cc
// @brief   Extracts track-level quantities (length, status, vertex info,
//          mean free path, at-rest rates) from Geant4 G4Track data.
// @details TrackExtractor operates on Secondary-type UnifiedSources and
//   provides track length, status, vertex position/energy/PDG code, mean
//   free path (from PostStep process interaction lengths), and at-rest
//   process rates and lifetime.
//
//   Configuration keys read: none.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "TrackExtractor.hh"
#include "G4SystemOfUnits.hh"
#include "G4ProcessManager.hh"
#include "G4VProcess.hh"

/// @brief Extracts a track-level numeric column value.
/// @param type Column type (TrackLength, TrackStatus, VertexX/Y/Z,
///        VertexKineticEnergy, VertexPDGCode, MeanFreePath, AtRestRate,
///        AtRestLifeTime).
/// @param src  UnifiedSource providing G4Track data (must be Secondary type).
/// @return Extracted value, or 0.0 if source is not a Secondary.
double TrackExtractor::Get(ColType type, const UnifiedSource& src) const {
    if (src.GetType() != UnifiedSource::Type::Secondary) return 0.0;

    const G4Track* track = src.GetTrack();
    if (!track) return 0.0;

    switch (type) {
        case ColType::TrackLength:
            return track->GetTrackLength() / mm;

        case ColType::TrackStatus:
            return static_cast<double>(track->GetTrackStatus());

        case ColType::VertexX:
            return track->GetVertexPosition().x() / mm;
        case ColType::VertexY:
            return track->GetVertexPosition().y() / mm;
        case ColType::VertexZ:
            return track->GetVertexPosition().z() / mm;

        case ColType::VertexKineticEnergy:
            return track->GetVertexKineticEnergy() / MeV;

        case ColType::VertexPDGCode: {
            // No direct method; return PDG of the current particle (not vertex)
            auto* def = track->GetParticleDefinition();
            return def ? static_cast<double>(def->GetPDGEncoding()) : 0.0;
        }

        case ColType::MeanFreePath: {
            // Sum inverse interaction lengths for all PostStep processes
            G4ProcessManager* pManager = track->GetDefinition()->GetProcessManager();
            if (!pManager) return 0.0;
            G4ProcessVector* pVector = pManager->GetPostStepProcessVector(typeDoIt);
            if (!pVector) return 0.0;
            double totalMacroXS = 0.0;
            for (G4int i = 0; i < pVector->size(); ++i) {
                G4VProcess* process = (*pVector)[i];
                G4ForceCondition condition;
                double lambda = process->PostStepGetPhysicalInteractionLength(*track, 0.0, &condition);
                if (lambda > 0.0 && lambda < DBL_MAX) {
                    totalMacroXS += 1.0 / lambda;
                }
            }
            return (totalMacroXS > 0.0) ? (1.0 / totalMacroXS) / mm : 0.0;
        }

        /// @brief Calculates total at-rest process rate at zero kinetic energy.
        /// @param track G4Track pointer.
        /// @return Total rate in 1/ns, or 0.0 if not applicable.
        case ColType::AtRestRate: {
            if (track->GetKineticEnergy() > 0.0) return 0.0;
            G4ProcessManager* pManager = track->GetDefinition()->GetProcessManager();
            if (!pManager) return 0.0;
            G4ProcessVector* pVector = pManager->GetAtRestProcessVector();
            if (!pVector) return 0.0;
            double totalRate = 0.0;
            for (G4int i = 0; i < pVector->size(); ++i) {
                G4VProcess* process = (*pVector)[i];
                G4ForceCondition condition;
                double tau = process->AtRestGetPhysicalInteractionLength(*track, &condition);
                if (tau > 0.0 && tau < DBL_MAX) {
                    totalRate += 1.0 / tau;
                }
            }
            return totalRate * ns; // 1/ns
        }

        case ColType::AtRestLifeTime:
            if (track->GetKineticEnergy() <= 0.0) {
                return track->GetProperTime() / ns;
            }
            return 0.0;

        default:
            return 0.0;
    }
}