#ifndef SOURCE_HH
#define SOURCE_HH

//==============================================================================
// G4CARE
// @file    Source.hh
// @brief   Abstract base class for particle sources
// @details Defines the interface for all particle sources in G4CARE.
//   Each source must implement GeneratePrimaries() to fill a G4Event
//   with primary vertices and particles. Sources have a weight for
//   mixed-source event generation (managed by SourceManager).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4Event.hh"
#include "G4String.hh"

class Source {
public:
    /// @brief Virtual destructor
    virtual ~Source() = default;

    /// @brief Generate primary particles for the given event
    /// @param event Pointer to the G4Event to fill
    virtual void GeneratePrimaries(G4Event* event) = 0;

    /// @brief Get the source name
    /// @return Source name string
    virtual G4String GetName() const { return fName; }

    /// @brief Get the source weight for mixed-source generation
    /// @return Weight value
    double GetWeight() const { return fWeight; }

    /// @brief Set the source weight
    /// @param w Weight value
    void SetWeight(double w) { fWeight = w; }

protected:
    /// @brief Source name identifier
    G4String fName;

    /// @brief Source weight (default 1.0)
    double fWeight = 1.0;
};

#endif