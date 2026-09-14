//==============================================================================
// G4CARE
// @file    RadioactivePhysicsList.cc
// @brief   Implementation of the radioactive decay physics list
// @details Registers standard physics modules (EM option4, FTFP_BERT,
//   decay, stopping, ion, radioactive decay) and configures atomic
//   de-excitation (fluorescence and Auger enabled), radioactive decay
//   commands (ARM enabled, all channels selected), and a 1 ns mean-life
//   threshold for G4NuclideTable.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "RadioactivePhysicsList.hh"
#include "G4RadioactiveDecayPhysics.hh"
#include "G4UnitsTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4NuclideTable.hh"
#include "G4IonTable.hh"
#include "G4ParticleTable.hh"
#include "G4PhysicsListHelper.hh"
#include "G4LossTableManager.hh"
#include "G4UAtomicDeexcitation.hh"
#include "G4DecayPhysics.hh"
#include "G4EmStandardPhysics.hh"
#include "G4EmStandardPhysics_option4.hh"
#include "G4EmExtraPhysics.hh"
#include "G4HadronPhysicsFTFP_BERT.hh"
#include "G4StoppingPhysics.hh"
#include "G4IonPhysics.hh"
#include "G4GenericIon.hh"
#include "G4UImanager.hh"
#include "G4ProcessVector.hh"
#include "G4ProcessManager.hh"
#include "G4VProcess.hh"

/// @brief Constructor: register all physics modules
///
/// Sets the G4NuclideTable mean-life threshold to 1 ns and registers
/// decay, EM option4, extra EM, FTFP_BERT hadronic, stopping, ion,
/// and radioactive-decay physics modules.
RadioactivePhysicsList::RadioactivePhysicsList()
{
    const G4double meanLife = 1.0 * nanosecond;
    G4NuclideTable::GetInstance()->SetMeanLifeThreshold(meanLife);

    RegisterPhysics(new G4DecayPhysics());
    RegisterPhysics(new G4EmStandardPhysics_option4());
    RegisterPhysics(new G4EmExtraPhysics());
    RegisterPhysics(new G4HadronPhysicsFTFP_BERT());
    RegisterPhysics(new G4StoppingPhysics());
    RegisterPhysics(new G4IonPhysics());
    RegisterPhysics(new G4RadioactiveDecayPhysics());
}

/// @brief Construct particles via base class
void RadioactivePhysicsList::ConstructParticle()
{
    G4VModularPhysicsList::ConstructParticle();
}

/// @brief Construct processes: enable ARM, fluorescence, Auger
///
/// Configures radioactive decay (all channels, photo-evaporation,
/// ARM enabled), atomic de-excitation (fluorescence and Auger on),
/// and suppresses PIXE.
void RadioactivePhysicsList::ConstructProcess()
{
    G4VModularPhysicsList::ConstructProcess();

    G4UImanager* UI = G4UImanager::GetUIpointer();
    UI->ApplyCommand("/process/had/rdm/verbose 0");
    UI->ApplyCommand("/process/had/rdm/selectAll");
    UI->ApplyCommand("/process/had/rdm/setPhotoEvaporation true");
    UI->ApplyCommand("/process/had/rdm/applyARM true");

    G4UAtomicDeexcitation* atomDeex = new G4UAtomicDeexcitation();
    atomDeex->SetFluo(true);
    atomDeex->SetAuger(true);
    atomDeex->SetPIXE(false);
    G4LossTableManager::Instance()->SetAtomDeexcitation(atomDeex);

    G4PhysicsListHelper* ph = G4PhysicsListHelper::GetPhysicsListHelper();
    G4ParticleDefinition* genericIon = G4GenericIon::GenericIon();

    G4ProcessManager* pmanager = genericIon->GetProcessManager();
    if (pmanager) {
        G4ProcessVector* processes = pmanager->GetProcessList();
        for (int i = 0; i < processes->size(); i++) {
            G4VProcess* process = (*processes)[i];
        }
    }
}

/// @brief Set production cuts via base class
void RadioactivePhysicsList::SetCuts()
{
    G4VModularPhysicsList::SetCuts();
}