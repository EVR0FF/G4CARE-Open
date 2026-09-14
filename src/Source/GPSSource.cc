//==============================================================================
//
// G4CARE
//
// @file    GPSSource.cc
// @brief   General Particle Source (GPS) wrapper — executes Geant4 GPS macros.
//
// @details
//   Wraps G4GeneralParticleSource.  On construction, executes a user-supplied
//   GPS macro file via /control/execute which configures the GPS singleton
//   (particle type, energy spectrum, angular distribution, source shape, etc.).
//   At event generation, delegates entirely to GPS and records the generated
//   primary vertex in BeamAnalysis for the "primary" NTuple tree.
//
//   Configuration keys read from SOURCE.<name>.*:
//     macro
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "GPSSource.hh"
#include "G4UImanager.hh"
#include "G4ios.hh"
#include "BeamAnalysis.hh"
#include "ConfigManager.hh"
#include "UnifiedSource.hh"

/// @brief Construct a GPS source and execute its configuration macro.
/// @param macroFile Path to the Geant4 GPS macro file (e.g. `/gps/...` commands).
///
/// Executes the macro via /control/execute, which configures the global
/// G4GeneralParticleSource singleton. The source name is set to
/// "GPS:<macroFile>" for log identification.
GPSSource::GPSSource(const G4String& macroFile)
    : fGPS(std::make_unique<G4GeneralParticleSource>())
{
    fName = "GPS:" + macroFile;
    G4UImanager* ui = G4UImanager::GetUIpointer();
    G4cout << "GPSSource: applying macro " << macroFile << G4endl;
    ui->ApplyCommand("/control/execute " + macroFile);
}

/// @brief Generate primary particles by delegating to the GPS singleton.
///
/// Calls G4GeneralParticleSource::GeneratePrimaryVertex() to create the
/// primary vertex, then extracts the last vertex added to the event and
/// registers it via BeamAnalysis::Fill("primary", ...) using UnifiedSource
/// for NTuple recording.
///
/// @param event Current Geant4 event.
void GPSSource::GeneratePrimaries(G4Event* event) {
    fGPS->GeneratePrimaryVertex(event);

    // Fill BeamAnalysis from the vertex added by GPS
    G4PrimaryVertex* vertex = event->GetPrimaryVertex(
        event->GetNumberOfPrimaryVertex() - 1);
    if (vertex && vertex->GetNumberOfParticle() > 0) {
        G4PrimaryParticle* particle = vertex->GetPrimary(0);
        BeamAnalysis::Instance()->Fill("primary", UnifiedSource(vertex, particle));
    }
}
