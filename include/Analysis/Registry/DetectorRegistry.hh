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
    double intrinsicResFWHM_MeV = 0.0;
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
    bool useEfficiencyExpression = false;
    std::string efficiencyExpression;
    bool useGainExpression = false;
    std::string gainExpression;
    bool useNoiseExpression = false;
    std::string noiseExpression;
    bool useThresholdExpression = false;
    std::string thresholdExpression;
    bool useUpperThresholdExpression = false;
    std::string upperThresholdExpression;

    double darkCountRate_kHz = 0.0;
    double crossTalkProbability = 0.0;
    double recoveryTime_ns = 0.0;
    double coincidenceWindow_ns = 0.0;

    double timeLandauMPV_ns = 0.0;
    double timeLandauSigma_ns = 0.0;
    double energyLandauMPV_MeV = 0.0;

    // ==== Spatial resolution / efficiency / readout (GATE-parity) ====
    double spatialResFWHM_mm = 0.0;
    std::string efficiencyMode = "constant";
    double efficiencyCenter_MeV = 0.511;
    double efficiencyFWHM_MeV = 0.0;
    int readoutGroup = -1;
    double upperThreshold_MeV = 0.0;     ///< Upholder: reject energy above this (0 = off)
    double energyFrame_MeV = 0.0;        ///< Energy framing: discretize energy into bins (0 = off)
    double doiResolution_mm = 0.0;       ///< Depth-of-interaction smearing (0 = off)
    int coincidenceMultiplicity = 2;     ///< Max digits per coincidence (multiples killer; 0 = unlimited)
    std::string efficiencyFile;          ///< Efficiency curve file (energy efficiency per line)
    std::vector<std::pair<double,double>> efficiencyCurve;  ///< loaded curve (energy MeV, efficiency)
    double energyLandauSigma_MeV = 0.0;

    // ==== Detector type ====
    bool calorimeterMode = false;   ///< Sum all hits into one digit per event (like CAL)
    int digitType = -1;             ///< Subsystem type: 0=TKR, 1=CAL, 2=ACD

    // ==== Configurable singles pipeline (ordered step names; empty = default order) ====
    std::vector<std::string> pipeline;

    // ==== GATE-parity enhancements ====
    double crossTalkFraction = 0.0;             ///< fraction of energy to each crosstalk neighbor (energy-conserving)
    std::vector<int> crossTalkNeighbors = {1};  ///< neighbor ID offsets for crosstalk
    double darkCountEnergy_MeV = 1.0;           ///< energy of dark-count pulses
    int coincidenceMinSectorDiff = 0;           ///< min detector-id difference for a valid coincidence (0 = off)
    std::string coincidenceMultiplesPolicy = "reject";  ///< "reject" | "keep_all"
    std::string readoutPolicy = "energy_winner";        ///< "energy_winner" | "energy_centroid"

    double lightYield = 0.0;                  ///< scintillation light yield (photons/MeV); 0 = off (use energy_res_fwhm)
    double transferEfficiency = 1.0;          ///< light transfer efficiency (0..1)
    double intrinsicResFWHM_frac = 0.0;       ///< fractional intrinsic FWHM at reference energy (GATE light-yield model)
    double intrinsicResRefEnergy_MeV = 1.0;   ///< reference energy for fractional intrinsic resolution

    int deadTimeBufferSize = 0;           ///< nonparalyzable dead-time buffer depth (0 = off)
    bool spatialConfine = false;          ///< confine blurred position (truncated Gaussian)
    double spatialHalfX_mm = 0.0;         ///< half-width clamp for x (0 = no clamp)
    double spatialHalfY_mm = 0.0;         ///< half-width clamp for y (0 = no clamp)
    double spatialHalfZ_mm = 0.0;         ///< half-width clamp for z (0 = no clamp)
};

/// @brief Maps sensitive detectors to integer IDs with per-detector properties.
class DetectorRegistry {
public:
    int RegisterDetector(G4VSensitiveDetector* sd, const std::string& name, const DetectorProperties& props);
    int GetDetectorID(G4VSensitiveDetector* sd) const;
    int GetDetectorID(const std::string& name) const;
    const DetectorProperties& GetProperties(int id) const;
    std::string GetName(int id) const;
    const std::vector<std::pair<G4VSensitiveDetector*, DetectorProperties>>& GetAll() const { return fDetectors; }
    void Clear();

private:
    std::map<std::string, int> fNameToID;
    std::map<int, std::string> fIDToName;
    std::map<G4VSensitiveDetector*, int> fDetectorToID;
    std::vector<std::pair<G4VSensitiveDetector*, DetectorProperties>> fDetectors;
};

#endif