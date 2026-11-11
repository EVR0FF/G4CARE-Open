//==============================================================================
//
// G4CARE
//
// @file    RTPlan.hh
// @brief   RTPLAN data: treatment beams (energy, gantry/couch, isocenter).
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#ifndef RT_PLAN_HH
#define RT_PLAN_HH

#include <cmath>
#include <string>
#include <vector>

/// @brief RTPLAN data (treatment beams).
struct RTPlan {
    /// @brief A single control point.
    struct ControlPoint {
        double energyMeV = 0.0;       ///< Nominal beam energy (MeV).
        double gantryAngleDeg = 0.0;  ///< Gantry angle (IEC 61217).
        double couchAngleDeg = 0.0;   ///< Patient-support (couch) angle.
        double isocenter[3] = {0.0, 0.0, 0.0};  ///< Isocenter (mm, DICOM LPS).
    };

    /// @brief A treatment beam.
    struct Beam {
        int number = 0;
        std::string name;
        std::string radiationType;          ///< "PHOTON", "ELECTRON", ...
        double sadMm = 1000.0;              ///< Source-Axis Distance (300A,00B4), mm.
        std::vector<ControlPoint> controlPoints;
    };

    std::vector<Beam> beams;
    std::string label;                 ///< RT Plan Label (300A,0002).
    double prescriptionDoseCGy = 0.0;  ///< Target prescription dose (cGy) from DoseReferenceSequence.
    bool valid = false;

    /// @brief Beam direction (unit vector) from gantry/couch angles.
    ///        Simplified IEC 61217: gantry rotates in the x-z plane; couch
    ///        rotation about z. dir[3] is filled with a unit vector.
    static void BeamDirection(double gantryDeg, double couchDeg, double dir[3]) {
        const double g = gantryDeg * 3.14159265358979323846 / 180.0;
        const double c = couchDeg * 3.14159265358979323846 / 180.0;
        // Source-to-isocentre direction for gantry angle (IEC): (sin g, 0, -cos g).
        const double bx = std::sin(g);
        const double by = 0.0;
        const double bz = -std::cos(g);
        // Couch rotation about z.
        dir[0] = bx * std::cos(c) - by * std::sin(c);
        dir[1] = bx * std::sin(c) + by * std::cos(c);
        dir[2] = bz;
    }

    /// @brief Map a Geant4 particle name to a DICOM RT Radiation Type (300A,00C6).
    static std::string RadiationTypeFromParticle(const std::string& particle) {
        if (particle == "gamma") return "PHOTON";
        if (particle == "e-" || particle == "e+") return "ELECTRON";
        if (particle == "proton") return "PROTON";
        if (particle == "neutron") return "NEUTRON";
        return "ION";
    }
};

#endif // RT_PLAN_HH
