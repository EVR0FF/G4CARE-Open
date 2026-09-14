//==============================================================================
// G4CARE
// @file    DetectorRegistry.hh
// @brief   Registry that maps G4VSensitiveDetector pointers and detector names
//          to unique integer IDs and stores per-detector properties.
// @details Defines DetectorProperties struct (dead time, resolution, signal
//   shaping, smearing models, user expressions, pile-up, noise, afterpulsing)
//   and the DetectorRegistry class for mapping SDs to IDs.
//
//   Configuration keys read: none (registry data structure).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DETECTOR_REGISTRY_HH
#define DETECTOR_REGISTRY_HH

#include "G4VSensitiveDetector.hh"
#include <map>
#include <vector>
#include <string>

/// @brief Per-detector configuration properties used by DigitizerModule.
struct DetectorProperties {
    // ==== Basic parameters ====
    double deadTime_ns = 0.0;
    std::string deadTimeModel = "nonparalyzable";
    double timeResFWHM_ns = 0.0;
    double energyResFWHM = 0.0;
    double energyResRefEnergy_MeV = 1.0;
    double quantumEfficiency = 1.0;
    double gain = 1.0;
    std::string pileupModel = "none";
    double afterpulseProbability = 0.0;

    // ==== Signal shaping ====
    double signalRiseTime_ns = 0.0;
    double signalFallTime_ns = 0.0;
    std::string signalModel = "exponential";
    double pileupWindow_ns = 0.0;
    int pileupBufferSize = 100;
    double threshold_MeV = 0.0;
    double noiseLevel_MeV = 0.0;

    // ==== Smearing models ====
    std::string timeSmearModel = "gaussian";
    std::string energySmearModel = "gaussian";

    // ==== User expressions ====
    bool useTimeSmearExpression = false;
    std::string timeSmearExpression;
    bool useEnergySmearExpression = false;
    std::string energySmearExpression;
    bool useSignalTimeExpression = false;
    std::string signalTimeExpression;
    bool usePileupEnergyExpression = false;
    std::string pileupEnergyExpression;
    bool usePileupTimeExpression = false;
    std::string pileupTimeExpression;

    double darkCountRate_kHz = 0.0;
    double crossTalkProbability = 0.0;
    double recoveryTime_ns = 0.0;

    double timeLandauMPV_ns = 0.0;
    double timeLandauSigma_ns = 0.0;
    double energyLandauMPV_MeV = 0.0;
    double energyLandauSigma_MeV = 0.0;

    // ==== Detector type ====
    bool calorimeterMode = false;   ///< Sum all hits into one digit per event (like CAL)
    int digitType = -1;             ///< Subsystem type: 0=TKR, 1=CAL, 2=ACD
};

/// @brief Maps sensitive detectors to integer IDs with per-detector properties.
class DetectorRegistry {
public:
    int RegisterDetector(G4VSensitiveDetector* sd, const std::string& name, const DetectorProperties& props);
    int GetDetectorID(G4VSensitiveDetector* sd) const;
    int GetDetectorID(const std::string& name) const;
    const DetectorProperties& GetProperties(int id) const;
    const std::vector<std::pair<G4VSensitiveDetector*, DetectorProperties>>& GetAll() const { return fDetectors; }
    void Clear();

private:
    std::map<std::string, int> fNameToID;
    std::map<G4VSensitiveDetector*, int> fDetectorToID;
    std::vector<std::pair<G4VSensitiveDetector*, DetectorProperties>> fDetectors;
};

#endif