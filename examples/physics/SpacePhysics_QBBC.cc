//==============================================================================
// G4CARE — Custom physics module: SpacePhysics_QBBC
// Extracted from Geant4 xray_TESdetector example
// (G4EmStandardPhysics_SpacePhysics + QBBC hadron + decay)
//
// Build as shared library:
//   g++ -shared -fPIC -o libSpacePhysics_QBBC.so SpacePhysics_QBBC.cc \
//     $(geant4-config --cflags --libs)
//
// Usage in YAML:
//   PHYSICS:
//     BASIC:
//       LIST: ""
//     ADVANCED:
//       SpacePhysics_QBBC:
//         ENABLE: true
//         LIBRARY: "physics/libSpacePhysics_QBBC.so"
//         CONSTRUCTOR: "SpacePhysics_QBBC"
//==============================================================================

#include "G4VModularPhysicsList.hh"
#include "G4VPhysicsConstructor.hh"
#include "G4EmStandardPhysics_SpacePhysics.hh"
#include "G4HadronInelasticQBBC.hh"
#include "G4EmExtraPhysics.hh"
#include "G4HadronElasticPhysics.hh"
#include "G4StoppingPhysics.hh"
#include "G4IonPhysics.hh"
#include "G4NeutronTrackingCut.hh"
#include "G4RadioactiveDecayPhysics.hh"
#include "G4DecayPhysics.hh"
#include "G4EmParameters.hh"
#include "G4SystemOfUnits.hh"
#include "CLHEP/Units/SystemOfUnits.h"

/// @brief Custom physics list: G4EmStandardPhysics_SpacePhysics + QBBC hadron
///
/// Implements the "SpacePhysics_QBBC" configuration from the Geant4
/// xray_TESdetector example: SpacePhysics EM + QBBC hadronic + decays.
class SpacePhysics_QBBC : public G4VModularPhysicsList {
public:
    SpacePhysics_QBBC(int verbose = 1) : G4VModularPhysicsList() {
        SetVerboseLevel(verbose);
        // Standard EM with SpacePhysics
        ReplacePhysics(new G4EmStandardPhysics_SpacePhysics());
        // Hadronic
        RegisterPhysics(new G4EmExtraPhysics(verbose));
        RegisterPhysics(new G4HadronElasticPhysics(verbose));
        RegisterPhysics(new G4StoppingPhysics(verbose));
        RegisterPhysics(new G4IonPhysics(verbose));
        RegisterPhysics(new G4NeutronTrackingCut(verbose));
        RegisterPhysics(new G4HadronInelasticQBBC(verbose));
        // Decays
        RegisterPhysics(new G4DecayPhysics("decays"));
        RegisterPhysics(new G4RadioactiveDecayPhysics());

        // ── EM parameters — matching XrayTESdetPhysicsList::ConstructProcess() ──
        G4EmParameters* param = G4EmParameters::Instance();
        param->SetDeexActiveRegion("InnerRegion", true, true, true);
        param->SetAuger(true);
        param->SetAugerCascade(false);
        param->SetFluo(true);
        param->SetPixe(true);
        param->SetDeexcitationIgnoreCut(false);
        param->SetMuHadLateralDisplacement(false);
        param->SetBremsstrahlungTh(10 * CLHEP::TeV);
    }
    ~SpacePhysics_QBBC() override = default;
};

extern "C" {
    /// @brief Factory function for PhysicsManager::CreatePhysicsList.
    /// @return New SpacePhysics_QBBC physics list.
    G4VModularPhysicsList* createSpacePhysics_QBBC() {
        return new SpacePhysics_QBBC();
    }
}
