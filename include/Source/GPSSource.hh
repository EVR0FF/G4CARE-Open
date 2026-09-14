//==============================================================================
//
// G4CARE
//
// @file    GPSSource.hh
// @brief   General Particle Source (GPS) wrapper — executes Geant4 GPS macros.
//
// @details
//   Wraps G4GeneralParticleSource.  On construction, executes a user-supplied
//   GPS macro file via /control/execute.  At event generation, delegates
//   entirely to GPS.
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

#ifndef GPSSOURCE_HH
#define GPSSOURCE_HH

#include "Source.hh"
#include "G4GeneralParticleSource.hh"
#include <memory>

/// @brief General Particle Source wrapper — executes a Geant4 GPS macro file.
///
/// Each instance runs /control/execute on its macro file at construction,
/// configuring the global G4GeneralParticleSource singleton.
class GPSSource : public Source {
public:
    GPSSource(const G4String& macroFile);
    virtual ~GPSSource() = default;

    /// @brief Delegate primary vertex generation to G4GeneralParticleSource.
    void GeneratePrimaries(G4Event* event) override;

private:
    std::unique_ptr<G4GeneralParticleSource> fGPS;
};

#endif