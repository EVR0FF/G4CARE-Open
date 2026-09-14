//==============================================================================
//
// G4CARE
//
// @file    PhysicsManager.cc
// @brief   Physics list construction and module management.
//
// @details
//   Creates Geant4 physics lists from YAML configuration. Reads
//   PHYSICS.BASIC.LIST for the base physics list and iterates over
//   PHYSICS.ADVANCED subsections to register/replace physics modules
//   (STEPLIMITER, OPTICS, EM variants, DNA, ION, DECAY, etc.).
//   Supports loading custom constructors from shared libraries.
//
//   Configuration keys read:
//     PHYSICS.BASIC.LIST
//     PHYSICS.BASIC.MACRO
//     PHYSICS.ADVANCED.<name>.ENABLE
//     PHYSICS.ADVANCED.<name>.MACRO
//     PHYSICS.ADVANCED.<name>.LIBRARY
//     PHYSICS.ADVANCED.<name>.CONSTRUCTOR
//     PHYSICS.EM_REGIONS.<name>.REGION
//     PHYSICS.EM_REGIONS.<name>.PHYSICS
//     PHYSICS.EM_REGIONS.<name>.VOLUMES
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

#include "PhysicsManager.hh"
#include "ConfigManager.hh"
#include "G4PhysListFactory.hh"
#include "G4VModularPhysicsList.hh"
#include "G4VPhysicsConstructor.hh"
#include "G4UImanager.hh"
#include "G4ios.hh"
#include "G4Exception.hh"

#include "G4StepLimiterPhysics.hh"
#include "G4RadioactiveDecayPhysics.hh"
#include "G4OpticalPhysics.hh"
#include "G4EmStandardPhysics.hh"
#include "G4EmStandardPhysics_option1.hh"
#include "G4EmStandardPhysics_option2.hh"
#include "G4EmStandardPhysics_option3.hh"
#include "G4EmStandardPhysics_option4.hh"
#include "G4EmLivermorePhysics.hh"
#include "G4EmPenelopePhysics.hh"
#include "G4EmDNAPhysics.hh"
#include "G4EmDNAChemistry.hh"
#include "G4HadronPhysicsFTFP_BERT.hh"
#include "G4EmExtraPhysics.hh"
#include "G4NeutronTrackingCut.hh"
#include "G4IonPhysics.hh"
#include "G4IonElasticPhysics.hh"
#include "G4DecayPhysics.hh"
#include "G4HadronElasticPhysics.hh"
#include "G4EmDNAPhysics_option8.hh"
#include "WeightWindowManager.hh"
#include "G4ParallelWorldPhysics.hh"
#include "G4EmModelManager.hh"
#include "G4ProcessManager.hh"
#include "G4VEmProcess.hh"
#include "G4Electron.hh"
#include "G4EmParameters.hh"
#include "G4Region.hh"
#include "G4RegionStore.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4EmDNAPhysicsActivator.hh"

#ifdef __linux__
#include <dlfcn.h>
#elif _WIN32
#include <windows.h>
#endif

PhysicsManager::PhysicsManager() {
    ReadConfiguration();
}

PhysicsManager::~PhysicsManager() {
    for (void* handle : fLibraryHandles) {
#ifdef __linux__
        
#elif _WIN32
        FreeLibrary((HMODULE)handle);
#endif
    }
}

void PhysicsManager::ReadConfiguration() {
    auto* cfg = ConfigManager::Instance();

    fBaseListName = cfg->GetString("PHYSICS.BASIC.LIST", "QGSP_BERT_EMV");
    fBaseMacro = cfg->GetString("PHYSICS.BASIC.MACRO", "");

    std::vector<std::string> moduleNames = cfg->GetSubsections("PHYSICS.ADVANCED");
    for (const auto& modName : moduleNames) {
        ModuleInfo info;
        info.name = modName;
        G4String base = "PHYSICS.ADVANCED." + modName + ".";

        info.enabled = cfg->GetBool(base + "ENABLE", false);
        if (!info.enabled) continue;
        info.macro = cfg->GetString(base + "MACRO", "");
        info.library = cfg->GetString(base + "LIBRARY", "");
        info.constructorName = cfg->GetString(base + "CONSTRUCTOR", "");

        fModules.push_back(info);
    }
    G4cout << "PhysicsManager: found " << fModules.size() << " modules in PHYSICS.ADVANCED" << G4endl;

    // Read EM_REGIONS: list of { REGION: name, PHYSICS: listName, VOLUMES: [...] } entries
    std::vector<std::string> regionNames = cfg->GetSubsections("PHYSICS.EM_REGIONS");
    for (const auto& rname : regionNames) {
        G4String base = "PHYSICS.EM_REGIONS." + rname + ".";
        EmRegionInfo info;
        info.regionName = cfg->GetString(base + "REGION", rname);
        info.emListName = cfg->GetString(base + "PHYSICS", "");
        if (cfg->HasKey(base + "VOLUMES")) {
            auto volList = cfg->GetStringVector(base + "VOLUMES");
            for (const auto& v : volList) info.volumeNames.push_back(v);
        }
        if (!info.emListName.empty()) {
            fEmRegions.push_back(info);
            G4cout << "PhysicsManager: EM region '" << info.regionName
                   << "' -> '" << info.emListName << "'"
                   << " volumes=" << info.volumeNames.size() << G4endl;
        }
    }
    G4cout << "PhysicsManager: found " << fEmRegions.size() << " EM region(s)" << G4endl;
}

void PhysicsManager::AddCrossSectionSource(ICrossSectionSource* src) {
    fCrossSectionSources.push_back(src);
}

void PhysicsManager::RegisterUserCrossSection(
    G4VEmModel* model,
    const G4ParticleDefinition* particle,
    const G4String& processName)
{
    G4ProcessManager* pm = particle->GetProcessManager();
    if (!pm) {
        G4cerr << "PhysicsManager: No process manager for "
               << particle->GetParticleName() << G4endl;
        return;
    }

    G4ProcessVector* pv = pm->GetProcessList();
    G4VEmProcess* emProc = nullptr;
    for (G4int i = 0; i < pv->size(); ++i) {
        G4VProcess* proc = (*pv)[i];
        if (proc->GetProcessName() == processName) {
            emProc = dynamic_cast<G4VEmProcess*>(proc);
            break;
        }
    }

    if (!emProc) {
        G4cerr << "PhysicsManager: EM process " << processName
               << " not found for particle "
               << particle->GetParticleName() << G4endl;
        return;
    }

    emProc->AddEmModel(0, model, nullptr);
    
    G4cout << "PhysicsManager: User XS model registered for "
           << particle->GetParticleName() << " / " << processName << G4endl;
}

G4VModularPhysicsList* PhysicsManager::CreateBaseList(const G4String& baseName) {
    G4PhysListFactory factory;
    if (!factory.IsReferencePhysList(baseName)) {
        G4Exception("PhysicsManager::CreateBaseList", "InvalidBaseList", FatalException,
                    ("Requested physics list '" + baseName + "' not found.").c_str());
        return nullptr;
    }
    return factory.GetReferencePhysList(baseName);
}

G4VPhysicsConstructor* PhysicsManager::LoadCustomConstructor(const G4String& libPath, const G4String& ctorName) {
    if (libPath.empty() || ctorName.empty()) {
        G4cerr << "PhysicsManager: LIBRARY and CONSTRUCTOR must be specified together." << G4endl;
        return nullptr;
    }

#ifdef __linux__
    void* handle = dlopen(libPath.c_str(), RTLD_LAZY);
    if (!handle) {
        G4cerr << "PhysicsManager: Failed to load library: " << libPath << " : " << dlerror() << G4endl;
        return nullptr;
    }

    std::string funcName = "create" + ctorName;
    typedef G4VPhysicsConstructor* (*CreateConstructor_t)();
    CreateConstructor_t create = (CreateConstructor_t)dlsym(handle, funcName.c_str());
    if (!create) {
        G4cerr << "PhysicsManager: Symbol " << funcName << " not found in library " << libPath << G4endl;
        dlclose(handle);
        return nullptr;
    }

    G4VPhysicsConstructor* ctor = create();
    if (!ctor) {
        G4cerr << "PhysicsManager: Constructor creation failed for " << ctorName << G4endl;
        dlclose(handle);
        return nullptr;
    }

    fLibraryHandles.push_back(handle);
    return ctor;
#elif _WIN32
    G4cerr << "PhysicsManager: Dynamic library loading on Windows not yet implemented." << G4endl;
    return nullptr;
#else
    G4cerr << "PhysicsManager: Unsupported platform for dynamic libraries." << G4endl;
    return nullptr;
#endif
}

/// @brief Load a full physics list (G4VModularPhysicsList*) from a shared library.
/// @param libPath  Path to the .so file.
/// @param ctorName Name of the constructor (used to build symbol "create<ctorName>").
/// @return Pointer to G4VModularPhysicsList, or nullptr on failure.
G4VModularPhysicsList* PhysicsManager::LoadCustomPhysicsList(const G4String& libPath, const G4String& ctorName) {
    if (libPath.empty() || ctorName.empty()) {
        G4cerr << "PhysicsManager::LoadCustomPhysicsList: LIBRARY and CONSTRUCTOR required." << G4endl;
        return nullptr;
    }
#ifdef __linux__
    void* handle = dlopen(libPath.c_str(), RTLD_LAZY);
    if (!handle) {
        G4cerr << "PhysicsManager::LoadCustomPhysicsList: dlopen failed: " << dlerror() << G4endl;
        return nullptr;
    }
    std::string funcName = "create" + ctorName;
    typedef G4VModularPhysicsList* (*CreateList_t)();
    CreateList_t create = (CreateList_t)dlsym(handle, funcName.c_str());
    if (!create) {
        G4cerr << "PhysicsManager::LoadCustomPhysicsList: symbol " << funcName << " not found." << G4endl;
        dlclose(handle);
        return nullptr;
    }
    G4VModularPhysicsList* pl = create();
    if (!pl) {
        G4cerr << "PhysicsManager::LoadCustomPhysicsList: constructor returned null." << G4endl;
        dlclose(handle);
        return nullptr;
    }
    fLibraryHandles.push_back(handle);
    return pl;
#else
    G4cerr << "PhysicsManager::LoadCustomPhysicsList: Only Linux supported for custom .so." << G4endl;
    return nullptr;
#endif
}

void PhysicsManager::ProcessModule(const ModuleInfo& module, G4VModularPhysicsList* physList) {
    if (!module.enabled) return;

    bool builtinHandled = false;

     if (module.name == "STEPLIMITER") {
        physList->RegisterPhysics(new G4StepLimiterPhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: registered G4StepLimiterPhysics" << G4endl;
    }
    else if (module.name == "RDECAY" || module.name == "RADIOACTIVE_DECAY") {
        physList->ReplacePhysics(new G4RadioactiveDecayPhysics());
        G4UImanager* ui = G4UImanager::GetUIpointer();
        ui->ApplyCommand("/process/had/rdm/thresholdForVeryLongDecay 1.0e+20 year");
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced G4RadioactiveDecayPhysics (thresholdForVeryLongDecay=1e20 years)" << G4endl;
    }
    else if (module.name == "OPTICS") {
        physList->RegisterPhysics(new G4OpticalPhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: registered G4OpticalPhysics" << G4endl;
    }
    else if (module.name == "EM_OPTION0" || module.name == "EM") {
        physList->ReplacePhysics(new G4EmStandardPhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmStandardPhysics" << G4endl;
    }
    else if (module.name == "EM_OPTION1") {
        physList->ReplacePhysics(new G4EmStandardPhysics_option1());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmStandardPhysics_option1" << G4endl;
    }
    else if (module.name == "EM_OPTION2") {
        physList->ReplacePhysics(new G4EmStandardPhysics_option2());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmStandardPhysics_option2" << G4endl;
    }
    else if (module.name == "EM_OPTION3") {
        physList->ReplacePhysics(new G4EmStandardPhysics_option3());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmStandardPhysics_option3" << G4endl;
    }
    else if (module.name == "EM_OPTION4") {
        physList->ReplacePhysics(new G4EmStandardPhysics_option4());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmStandardPhysics_option4" << G4endl;
    }
    else if (module.name == "EM_LIVERMORE") {
        physList->ReplacePhysics(new G4EmLivermorePhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmLivermorePhysics" << G4endl;
    }
    else if (module.name == "EM_PENELOPE") {
        physList->ReplacePhysics(new G4EmPenelopePhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: replaced EM with G4EmPenelopePhysics" << G4endl;
    }
    else if (module.name == "EM_DNA") {
        if (fBaseListName.empty() || fBaseListName == "none" || fBaseListName == "DNA") {
            physList->RegisterPhysics(new G4EmDNAPhysics());
            G4cout << "PhysicsManager: registered G4EmDNAPhysics (DNA-only mode)" << G4endl;
        } else {
            physList->ReplacePhysics(new G4EmDNAPhysics());
            G4cout << "PhysicsManager: replaced EM with G4EmDNAPhysics" << G4endl;
        }
        builtinHandled = true;
        fChemistryEnabled = true;
    }
        else if (module.name == "EM_DNA_CHEM") {
        physList->RegisterPhysics(new G4EmDNAChemistry());
        builtinHandled = true;
        fChemistryEnabled = true;
        G4cout << "PhysicsManager: enabling G4EmDNAChemistry_option1 module" << G4endl;
    }
    else if (module.name == "EM_DNA_CHEM8") {
        physList->RegisterPhysics(new G4EmDNAPhysicsActivator(8));
        physList->RegisterPhysics(new G4EmDNAChemistry());
        builtinHandled = true;
        fChemistryEnabled = true;
        fUseRegionalDNA = true;
        G4cout << "PhysicsManager: DNA physics (option8) per-region + G4EmDNAChemistry" << G4endl;
    }
    else if (module.name == "ION") {
        physList->RegisterPhysics(new G4IonPhysics());
        physList->RegisterPhysics(new G4IonElasticPhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: registered G4IonPhysics and G4IonElasticPhysics" << G4endl;
    }
    else if (module.name == "DECAY") {
        physList->RegisterPhysics(new G4DecayPhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: registered G4DecayPhysics" << G4endl;
    }
    else if (module.name == "HADRON_ELASTIC") {
        physList->RegisterPhysics(new G4HadronElasticPhysics());
        builtinHandled = true;
        G4cout << "PhysicsManager: registered G4HadronElasticPhysics" << G4endl;
    }
    else if (module.name == "WEIGHT_WINDOW") {
    WeightWindowManager::Initialize("PHYSICS.ADVANCED.WEIGHT_WINDOW");
    auto* wwm = WeightWindowManager::Instance();
        if (wwm && wwm->IsEnabled()) {

            physList->RegisterPhysics(new G4ParallelWorldPhysics("WeightWindowWorld"));

            if (wwm->GetAutoMode() != "collect") {
                wwm->RegisterProcesses(physList);
            } else {
                G4cout << "WeightWindowManager: Collect mode – skipping biasing registration." << G4endl;
                wwm->SetupScoring();
            }
        }
        builtinHandled = true;
        G4cout << "PhysicsManager: WEIGHT_WINDOW module processed." << G4endl;
    }

    if (builtinHandled) {
        if (!module.macro.empty()) {
            G4UImanager* ui = G4UImanager::GetUIpointer();
            G4cout << "PhysicsManager: Executing macro " << module.macro << " for module " << module.name << G4endl;
            ui->ApplyCommand("/control/execute " + module.macro);
        }
        return;
    }

    if (!module.library.empty() && !module.constructorName.empty()) {
        G4VPhysicsConstructor* ctor = LoadCustomConstructor(module.library, module.constructorName);
        if (ctor) {
            physList->RegisterPhysics(ctor);
            G4cout << "PhysicsManager: Registered constructor " << module.constructorName
                   << " from library " << module.library << G4endl;
        } else {
            G4cerr << "PhysicsManager: Failed to load custom constructor for module " << module.name << G4endl;
        }
    } else {

        G4cerr << "PhysicsManager: Module '" << module.name << "' is not a known builtin and has no library defined." << G4endl;
    }

    if (!module.macro.empty()) {
        G4UImanager* ui = G4UImanager::GetUIpointer();
        G4cout << "PhysicsManager: Executing macro " << module.macro << " for module " << module.name << G4endl;
        ui->ApplyCommand("/control/execute " + module.macro);
    }
}

/// @brief Apply per-region EM physics lists via G4EmParameters::AddPhysics.
/// Creates G4Region if it doesn't exist and assigns logical volumes to it.
/// Config keys: PHYSICS.EM_REGIONS.<name>.REGION, .PHYSICS, .VOLUMES.
/// VOLUMES: [list of LV names, or "*" to add ALL logical volumes]
void PhysicsManager::ApplyEmRegions() {
    if (fEmRegions.empty()) return;
    G4cout << "PhysicsManager::ApplyEmRegions: applying "
           << fEmRegions.size() << " EM region(s)" << G4endl;
    auto* regionStore = G4RegionStore::GetInstance();
    auto* lvStore = G4LogicalVolumeStore::GetInstance();
    auto* emParams = G4EmParameters::Instance();

    for (const auto& er : fEmRegions) {
        // Find or create G4Region
        G4Region* region = regionStore->FindOrCreateRegion(er.regionName);
        G4int nAssigned = 0;
        bool useAll = false;

        // Check for wildcard
        for (const auto& lvName : er.volumeNames) {
            if (lvName == "*") { useAll = true; break; }
        }

        if (useAll) {
            for (auto* lv : *lvStore) {
                if (!lv) continue;
                G4String lvn = lv->GetName();
                if (lvn.find("World") != std::string::npos || lvn.find("world") != std::string::npos ||
                    lvn.find("Hall") != std::string::npos  || lvn.find("hall") != std::string::npos) continue;
                region->AddRootLogicalVolume(lv);
                ++nAssigned;
            }
            G4cout << "  + WILDCARD: added " << nAssigned << " LV(s)" << G4endl;
        } else {
            // Assign specified logical volumes to region
            for (const auto& lvName : er.volumeNames) {
                G4LogicalVolume* lv = lvStore->GetVolume(lvName, false);
                if (lv) {
                    region->AddRootLogicalVolume(lv);
                    G4cout << "  + LV '" << lvName << "'" << G4endl;
                    ++nAssigned;
                } else {
                    G4cerr << "  - LV '" << lvName << "' NOT FOUND" << G4endl;
                }
            }
        }

        G4cout << "PhysicsManager: region '" << er.regionName
               << "' -> '" << er.emListName
               << "' assigned=" << nAssigned
               << " total=" << region->GetNumberOfRootVolumes() << G4endl;
        emParams->AddPhysics(er.regionName, er.emListName);
    }
}

G4VModularPhysicsList* PhysicsManager::CreatePhysicsList() {
    
    G4VModularPhysicsList* physList = nullptr;
    bool customListLoaded = false;
    if (fBaseListName.empty() || fBaseListName == "none" || fBaseListName == "DNA") {
        // ── Check if any ADVANCED module has LIBRARY+CONSTRUCTOR as full list ──
        for (const auto& module : fModules) {
            if (!module.library.empty() && !module.constructorName.empty() && module.enabled) {
                physList = LoadCustomPhysicsList(module.library, module.constructorName);
                if (physList) {
                    G4cout << "PhysicsManager: Loaded full physics list '" << module.constructorName
                           << "' from " << module.library << G4endl;
                    customListLoaded = true;
                    break;
                }
            }
        }
        if (!physList) {
            physList = new G4VModularPhysicsList();
            G4cout << "PhysicsManager: Empty physics list (no base, no custom .so)" << G4endl;
        }
    } else {
        physList = CreateBaseList(fBaseListName);
        if (!physList) return nullptr;
        G4cout << "PhysicsManager: Base physics list created: " << fBaseListName << G4endl;
    }

    if (!fBaseMacro.empty()) {
        G4UImanager* ui = G4UImanager::GetUIpointer();
        G4cout << "PhysicsManager: Executing base macro " << fBaseMacro << G4endl;
        ui->ApplyCommand("/control/execute " + fBaseMacro);
    }

    // Only process modules if a custom full list was NOT loaded
    if (!customListLoaded) {
        for (const auto& module : fModules) {
            ProcessModule(module, physList);
        }
    }

    G4UImanager* ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/process/em/nuclearStopping true");
    G4cout << "PhysicsManager: NuclearStopping enabled (NIEL for ions)" << G4endl;

    return physList;
}

void PhysicsManager::ConfigureDNARegions() {
    auto* regionStore = G4RegionStore::GetInstance();
    G4cout << "PhysicsManager::ConfigureDNARegions: scanning "
           << regionStore->size() << " regions" << G4endl;

    G4int nDNA = 0;
    for (auto* region : *regionStore) {
        if (!region) continue;
        G4String rname = region->GetName();
        if (rname.find("water") != std::string::npos) {
            G4EmParameters::Instance()->AddDNA(rname, "option8");
            G4cout << "PhysicsManager: DNA physics (option8) registered for region '"
                   << rname << "'" << G4endl;
            ++nDNA;
        }
    }

    G4cout << "PhysicsManager::ConfigureDNARegions: DNA physics applied to "
           << nDNA << " region(s)" << G4endl;
}