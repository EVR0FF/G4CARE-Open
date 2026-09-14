//==============================================================================
//
// G4CARE
//
// @file    RadioactiveSource.cc
// @brief   Radioactive decay ion source with isotope selection and vertex sampling.
//
// @details
//   Generates primary ions (G4IonTable) representing radioactive isotopes
//   with per-isotope abundance selection.  Supports three vertex placement
//   modes: expression-based, uniform random within a logical volume, and
//   uniform random on a surface.  Optional decay configuration includes
//   photo-evaporation, atomic relaxation, custom decay macro, and isomer table.
//
//   Processing flow:
//   1. ParseConfig() — reads SOURCE.<name>.* and dispatches to sub-parsers.
//   2. ParseVariables() — loads numeric variables for ExprTK expressions.
//   3. ParseIsotopes() — builds cumulative abundance distribution.
//   4. ParseVertices() — reads multi-vertex definitions.
//   5. GeneratePrimaries() — per-event: lazy ion init, vertex selection,
//      isotope roulette, position sampling, vertex creation.
//
//   Configuration keys read from SOURCE.<name>.sub-keys:
//     ISOTOPES.<idx>.(name|Z|A|excitation|abundance|lifetime)
//     VERTICES.<idx>.position.(type|x|y|z|volume), .time, .weight
//     object, pos.(x|y|z), time, weight, decay.*, isomer_table,
//     variables.*, vertex_mode
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

#include "RadioactiveSource.hh"
#include "ConfigManager.hh"
#include "G4IonTable.hh"
#include "G4NistManager.hh"
#include "G4RandomDirection.hh"
#include "Randomize.hh"
#include "PositionSampler.hh"
#include "SurfaceSampler.hh"
#include "BeamAnalysis.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4SystemOfUnits.hh"
#include "G4PrimaryVertex.hh"
#include "G4PrimaryParticle.hh"

RadioactiveSource::RadioactiveSource(const std::string& sourceName, ObjectManager* objMgr)
    : fSourceName(sourceName), fObjMgr(objMgr)
{
    fEval = std::make_unique<ExpressionEvaluator>();
    ParseConfig();
}

RadioactiveSource::~RadioactiveSource() = default;

/// @brief Read the entire SOURCE.<name> configuration and populate
///        variables, isotopes, vertices, and decay options.
///
/// @details
/// Three vertex placement strategies are supported, tried in order:
/// 1. **VERTICES block** — multiple vertex subsections exist under
///    SOURCE.<name>.VERTICES.  Each defines its own position (expression,
///    volume, or surface), time, and weight.  fVertexMode (sequential
///    or random, from vertex_mode) controls how many vertices are emitted
///    per event.
/// 2. **Object reference** — the key `object` names a logical volume;
///    vertices are uniformly sampled within that volume.
/// 3. **Single-vertex expressions** — top-level pos.x/y/z, time, and
///    weight keys are compiled as ExprTK expressions.
///
/// Decay settings (fEnablePhotoEvaporation, fEnableARM, fDecayMacro,
/// fIsomerTable) are applied later via the G4RadioactiveDecay process.
void RadioactiveSource::ParseConfig() {
    auto* cfg = ConfigManager::Instance();
    
    // Step 1: load user-defined numeric variables for expressions
    ParseVariables(fSourceName + ".variables");
    
    // Step 2: load isotope definitions and build abundance CDF
    ParseIsotopes(fSourceName + ".ISOTOPES");
    
    // Step 3: check for multi-vertex (VERTICES) or single-vertex mode
    std::vector<std::string> vertexIndices = cfg->GetSubsections(fSourceName + ".VERTICES");
    if (!vertexIndices.empty()) {
        // Multi-vertex mode — parse VERTICES subsections
        ParseVertices(fSourceName + ".VERTICES");
        
        std::string modeStr = cfg->GetString(fSourceName + ".vertex_mode", "sequential");
        fVertexMode = (modeStr == "random") ? VertexMode::kRandom : VertexMode::kSequential;
    } else {
        // Single-vertex mode — check for object-based or expression-based placement
        fObjectName = cfg->GetString(fSourceName + ".object", "");
        fUseObject = !fObjectName.empty();
        
        if (!fUseObject) {
            // Expression-based single vertex: compile pos/time/weight as ExprTK
            fSingleVertex.posType = VertexDesc::kExpression;
            
            auto allVars = GetAllVariableNames();
            
            auto preX = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".pos.x", "0"), allVars, fEval.get());
            fSingleVertex.posXExpr = preX.compiled;
            fSingleVertex.fastPosX.Init(preX);
        
            auto preY = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".pos.y", "0"), allVars, fEval.get());
            fSingleVertex.posYExpr = preY.compiled;
            fSingleVertex.fastPosY.Init(preY);
            
            auto preZ = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".pos.z", "0"), allVars, fEval.get());
            fSingleVertex.posZExpr = preZ.compiled;
            fSingleVertex.fastPosZ.Init(preZ);
            
            auto preTime = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".time", "0"), allVars, fEval.get());
            fSingleVertex.timeExpr = preTime.compiled;
            fSingleVertex.fastTime.Init(preTime);
            
            auto preWeight = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".weight", "1"), allVars, fEval.get());
            fSingleVertex.weightExpr = preWeight.compiled;
            fSingleVertex.fastWeight.Init(preWeight);
        } else {
           // Object-based single vertex: sample uniformly within the named volume
            fSingleVertex.posType = VertexDesc::kVolume;
            fSingleVertex.volumeName = fObjectName;
            fSingleVertex.cachedLogicalVolume = FindLogicalVolume(fObjectName);
        }
    }
    
    // Step 4: decay configuration
    std::string decayPath = fSourceName + ".decay";
    fEnablePhotoEvaporation = cfg->GetBool(decayPath + ".enable_photo_evaporation", true);
    fEnableARM = cfg->GetBool(decayPath + ".enable_arm", true);
    fDecayMacro = cfg->GetString(decayPath + ".macro", "");
    fIsomerTable = cfg->GetString(fSourceName + ".isomer_table", "");
}

/// @brief Load numeric variables from SOURCE.<name>.variables.*.
///
/// @param basePath ConfigManager key prefix (e.g. "SOURCE.e_gun.variables").
///
/// Each key under the prefix is evaluated via GetValueWithUnits so that
/// expressions like "300*kelvin" are converted to Geant4 internal units.
/// Values are stored in fVariables for use in ExprTK expressions.
void RadioactiveSource::ParseVariables(const std::string& basePath) {
    auto* cfg = ConfigManager::Instance();
    auto varKeys = cfg->GetSectionKeys(basePath);
    for (const auto& key : varKeys) {
        double val = cfg->GetValueWithUnits(basePath + "." + key, 0.0);
        fVariables[key] = val;
    }
}

/// @brief Build the list of all variable names available in expressions.
///
/// @details
/// Returns the built-in variables ("event_id", "v_i", "p_i") followed by
/// all user-defined keys from fVariables.
///
/// @return Ordered vector of variable name strings.
std::vector<std::string> RadioactiveSource::GetAllVariableNames() const {
    std::vector<std::string> names = {"event_id", "v_i", "p_i"};
    for (const auto& var : fVariables) {
        names.push_back(var.first);
    }
    return names;
}

/// @brief Parse isotope definitions from SOURCE.<name>.ISOTOPES.*
///        and build the cumulative abundance distribution.
///
/// @param basePath Prefix for isotope subsections (e.g. "SOURCE.rad.ISOTOPES").
///
/// For each isotope subsection, reads:
/// - `name` — human-readable isotope name (e.g. "Cs137").
/// - `Z` — atomic number.  If 0, parsed from the element prefix of `name`
///   via G4NistManager::GetZ().
/// - `A` — mass number.
/// - `excitation` — excitation energy with units (converted to MeV).
/// - `abundance` — relative weight for roulette-wheel selection.
/// - `lifetime` — isotope lifetime with units (converted to seconds,
///   passed to G4IonTable).
///
/// Valid isotopes (Z > 0 && A > 0) are appended to fIsotopes.
/// Abundances are normalised to sum=1 and stored as a cumulative
/// distribution in fCumulativeAbundances.
///
/// @note G4IonTable::GetIon() is deferred until the first call to
///       GeneratePrimaries() (lazy initialisation in fIonsEnsured).
void RadioactiveSource::ParseIsotopes(const std::string& basePath) {
    auto* cfg = ConfigManager::Instance();
    auto isotopeIndices = cfg->GetSubsections(basePath);
    
    for (const auto& idx : isotopeIndices) {
        IsotopeDesc iso;
        std::string p = basePath + "." + idx + ".";
        
        iso.name = cfg->GetString(p + "name", "");
        
        iso.Z = cfg->GetInt(p + "Z", 0);
        iso.A = cfg->GetInt(p + "A", 0);
        
        // If Z is not specified, try to infer it from the element prefix of the name
        if (iso.Z == 0 && !iso.name.empty()) {
            size_t num_start = iso.name.find_first_of("0123456789");
            if (num_start != std::string::npos) {
                std::string element = iso.name.substr(0, num_start);
                iso.Z = G4NistManager::Instance()->GetZ(element);
                try {
                    iso.A = std::stoi(iso.name.substr(num_start));
                } catch (const std::exception& e) {
                    G4cerr << "RadioactiveSource: Failed to parse mass number from '" 
                        << iso.name << "': " << e.what() << G4endl;
                    continue;
                }
            }
        }
        
        iso.excitation = cfg->GetValueWithUnits(p + "excitation", 0.0) / CLHEP::MeV;
        iso.abundance = cfg->GetDouble(p + "abundance", 1.0);
        iso.lifetime = cfg->GetValueWithUnits(p + "lifetime", 0.0) / CLHEP::second;
        
        if (iso.Z > 0 && iso.A > 0) {
            fIsotopes.push_back(std::move(iso));
        } else {
            G4cerr << "RadioactiveSource: invalid isotope at " << p << G4endl;
        }
    }
    
    // Build cumulative abundance distribution for roulette-wheel selection
    double total = 0.0;
    for (const auto& iso : fIsotopes) total += iso.abundance;
    double cum = 0.0;
    for (const auto& iso : fIsotopes) {
        cum += iso.abundance / total;
        fCumulativeAbundances.push_back(cum);
    }

    // EnsureIon() is deferred until first GeneratePrimaryVertex()
    // when G4IonTable is already initialised.
}

/// @brief Parse multi-vertex definitions from SOURCE.<name>.VERTICES.*.
///
/// @param basePath Prefix for vertex subsections (e.g. "SOURCE.rad.VERTICES").
///
/// Each vertex subsection may specify one of three position types:
/// - `position.type = "volume"`  → uniform random in a logical volume
///   (uses PositionSampler::SampleInLogicalVolume).
/// - `position.type = "surface"` → uniform random on a surface
///   (uses SurfaceSampler::SamplePoint).
/// - otherwise → ExprTK expressions for x, y, z via position.x/.y/.z.
///
/// Time and weight fields are also compiled as ExprTK expressions.
/// Subsections are sorted numerically by key before processing.
void RadioactiveSource::ParseVertices(const std::string& basePath) {
    auto* cfg = ConfigManager::Instance();
    auto vertexIndices = cfg->GetSubsections(basePath);
    
    // Sort vertex indices numerically for deterministic ordering
    std::sort(vertexIndices.begin(), vertexIndices.end(),
              [](const std::string& a, const std::string& b) {
                  return std::stoi(a) < std::stoi(b);
              });
    
    auto allVars = GetAllVariableNames();
    
    for (const auto& idx : vertexIndices) {
        std::string vPath = basePath + "." + idx;
        VertexDesc vtx;
        
        // --- Position ---
        std::string posType = cfg->GetString(vPath + ".position.type", "");
        if (posType == "volume") {
            vtx.posType = VertexDesc::kVolume;
            vtx.volumeName = cfg->GetString(vPath + ".position.volume", "");
            vtx.cachedLogicalVolume = FindLogicalVolume(vtx.volumeName);
            if (!vtx.cachedLogicalVolume) {
                G4cerr << "RadioactiveSource: volume '" << vtx.volumeName << "' not found." << G4endl;
            }
        } else if (posType == "surface") {
            vtx.posType = VertexDesc::kSurface;
            vtx.volumeName = cfg->GetString(vPath + ".position.volume", "");
            vtx.cachedSurfaceSampler = std::make_shared<SurfaceSampler>(vtx.volumeName);
        } else {
            // Default: expression-based placement
            vtx.posType = VertexDesc::kExpression;

            auto preX = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vPath + ".position.x", "0"), allVars, fEval.get());
            vtx.posXExpr = preX.compiled;
            vtx.fastPosX.Init(preX);
            
            auto preY = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vPath + ".position.y", "0"), allVars, fEval.get());
            vtx.posYExpr = preY.compiled;
            vtx.fastPosY.Init(preY);
            
            auto preZ = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vPath + ".position.z", "0"), allVars, fEval.get());
            vtx.posZExpr = preZ.compiled;
            vtx.fastPosZ.Init(preZ);
        }
        
        // --- Time ---
        auto preTime = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(vPath + ".time", "0"), allVars, fEval.get());
        vtx.timeExpr = preTime.compiled;
        vtx.fastTime.Init(preTime);
        
        // --- Weight ---
        auto preWeight = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(vPath + ".weight", "1"), allVars, fEval.get());
        vtx.weightExpr = preWeight.compiled;
        vtx.fastWeight.Init(preWeight);
        
        fVertices.push_back(std::move(vtx));
    }
}

/// @brief Generate primary ion vertices for one Geant4 event.
///
/// @param event Current Geant4 event to receive primary ions.
///
/// @details
/// **Processing order:**
/// 1. **Lazy ion initialisation** — on the first call, G4IonTable::GetIon()
///    is invoked for each isotope definition and the result is cached
///    (fIonsEnsured flag).
/// 2. **Vertex selection** — depends on the configured mode:
///    - Single-vertex: always uses fSingleVertex.
///    - Multi-vertex sequential: emits all vertices with positive weight.
///    - Multi-vertex random: selects one vertex by roulette-wheel on weights.
/// 3. **Isotope selection** — roulette-wheel on fCumulativeAbundances to
///    pick one isotope for each selected vertex.
/// 4. **Position sampling** — delegated to SamplePosition() for the chosen
///    vertex descriptor's placement type (volume/surface/expression).
/// 5. **Vertex creation** — a G4PrimaryVertex at the sampled position with
///    a G4PrimaryParticle (the ion) at zero kinetic energy (radioactive
///    decay drives subsequent emissions).  The vertex is registered in
///    BeamAnalysis for the "primary" NTuple.
void RadioactiveSource::GeneratePrimaries(G4Event* event) {
    if (fIsotopes.empty()) return;
    
    // Lazy ion initialisation on first call — G4IonTable must be ready
    if (!fIonsEnsured) {
        fIonsEnsured = true;
        for (auto& iso : fIsotopes) {
            iso.EnsureIon();
            if (!iso.cachedIon) {
                G4cerr << "RadioactiveSource: Failed to create ion for " << iso.name << G4endl;
            }
        }
    }
    
    double event_id = event->GetEventID();
    
    // --- Step 1: collect active vertices and their weights ---
    std::vector<size_t> activeIndices;
    std::vector<double> weights;
    
    if (fVertices.empty()) {
        // Single-vertex mode
        activeIndices.push_back(0);
        if (fUseObject) weights.push_back(1.0);
        else weights.push_back(fSingleVertex.fastWeight.Evaluate(event_id, 0.0, 0.0));
    } else if (fVertexMode == VertexMode::kSequential) {
        // Multi-vertex sequential — all vertices with positive weight are emitted
        for (size_t i = 0; i < fVertices.size(); ++i) {
            double w = fVertices[i].fastWeight.Evaluate(event_id, static_cast<double>(i), 0.0);
            if (w > 0) {
                activeIndices.push_back(i);
                weights.push_back(w);
            }
        }
    } else {
        // Multi-vertex random — roulette-wheel selection of one vertex
        std::vector<size_t> validIndices;
        std::vector<double> validWeights;
        double totalWeight = 0.0;
        for (size_t i = 0; i < fVertices.size(); ++i) {
            double w = fVertices[i].fastWeight.Evaluate(event_id, static_cast<double>(i), 0.0);
            if (w > 0) {
                validIndices.push_back(i);
                validWeights.push_back(w);
                totalWeight += w;
            }
        }
        if (totalWeight > 0) {
            double r = G4UniformRand() * totalWeight;
            double cum = 0.0;
            for (size_t j = 0; j < validWeights.size(); ++j) {
                cum += validWeights[j];
                if (r <= cum) {
                    activeIndices.push_back(validIndices[j]);
                    weights.push_back(validWeights[j]);
                    break;
                }
            }
        }
    }
    
    // --- Step 2: for each active vertex, pick an isotope and create the particle ---
    for (size_t idx = 0; idx < activeIndices.size(); ++idx) {
        size_t vtxIdx = activeIndices[idx];
        double v_i = static_cast<double>(vtxIdx);
        double weight = weights[idx];
        
        // Resolve vertex descriptor (single or multi)
        const VertexDesc& vtx = fVertices.empty() ? fSingleVertex : fVertices[vtxIdx];
        
        // Roulette-wheel isotope selection using cumulative abundances
        double rIso = G4UniformRand();
        size_t isoIdx = 0;
        while (isoIdx < fCumulativeAbundances.size() - 1 && rIso > fCumulativeAbundances[isoIdx]) {
            ++isoIdx;
        }
        
        IsotopeDesc& iso = fIsotopes[isoIdx];
        
        // Sample position based on vertex placement type
        G4ThreeVector pos = SamplePosition(vtx, event_id, v_i);
        
        // Evaluate emission time (ns) from the compiled expression
        double time = 0.0;
        if (vtx.fastTime.comp) {
            time = vtx.fastTime.Evaluate(event_id, v_i, 0.0) * CLHEP::ns;
        }
        
        // Create vertex and ion primary particle (zero kinetic energy —
        // radioactive decay process handles subsequent emissions)
        G4PrimaryVertex* vertex = new G4PrimaryVertex(pos, time);
        G4PrimaryParticle* particle = new G4PrimaryParticle(iso.cachedIon);
        particle->SetKineticEnergy(0.0);
        vertex->SetPrimary(particle);
        vertex->SetWeight(weight);
        event->AddPrimaryVertex(vertex);

        G4cout << "[RadioactiveSource] Event " << event->GetEventID()
               << ": created ion " << iso.name
               << " (Z=" << iso.Z << ", A=" << iso.A
               << ") at pos (" << pos.x()/CLHEP::mm << ", " << pos.y()/CLHEP::mm << ", " << pos.z()/CLHEP::mm << ") mm"
               << " time=" << time/CLHEP::ns << " ns"
               << " PDG=" << iso.cachedIon->GetPDGEncoding()
               << " lifetime=" << iso.cachedIon->GetPDGLifeTime()/CLHEP::ns << " ns"
               << G4endl;

        BeamAnalysis::Instance()->Fill("primary", UnifiedSource(vertex, particle));
            }
}

/// @brief Sample a 3D position from a vertex descriptor based on its
///        placement type.
///
/// @param vtx       Vertex descriptor determining the sampling strategy.
/// @param event_id  Current event ID (used in expression evaluation).
/// @param v_i       Vertex index (used in expression evaluation).
/// @return          Sampled position (Geant4 internal units).
///
/// Sampling strategies:
/// - kVolume:    uniform random point in the cached logical volume
///               via PositionSampler::SampleInLogicalVolume().
/// - kSurface:   uniform random point on the named surface
///               via SurfaceSampler::SamplePoint().
/// - kExpression: evaluate fastPosX/Y/Z ExprTK expressions and
///                multiply by mm to get Geant4 internal units.
/// - default:     return (0, 0, 0) as a safe fallback.
G4ThreeVector RadioactiveSource::SamplePosition(const VertexDesc& vtx, double event_id, double v_i) {
    if (vtx.posType == VertexDesc::kVolume) {
        if (vtx.cachedLogicalVolume) {
            return PositionSampler::SampleInLogicalVolume(vtx.cachedLogicalVolume);
        }
    } else if (vtx.posType == VertexDesc::kSurface) {
        if (vtx.cachedSurfaceSampler) {
            return vtx.cachedSurfaceSampler->SamplePoint();
        }
    } else if (vtx.posType == VertexDesc::kExpression) {
        double x = vtx.fastPosX.Evaluate(event_id, v_i, 0.0) * CLHEP::mm;
        double y = vtx.fastPosY.Evaluate(event_id, v_i, 0.0) * CLHEP::mm;
        double z = vtx.fastPosZ.Evaluate(event_id, v_i, 0.0) * CLHEP::mm;
        return G4ThreeVector(x, y, z);
    }
    return G4ThreeVector(0,0,0);
}

/// @brief Look up a G4LogicalVolume by exact name in the global store.
///
/// @param name Volume name.
/// @return     Pointer to the logical volume, or nullptr if not found.
G4LogicalVolume* RadioactiveSource::FindLogicalVolume(const G4String& name) const {
    G4LogicalVolumeStore* store = G4LogicalVolumeStore::GetInstance();
    if (!store) return nullptr;
    for (auto* lv : *store) {
        if (lv->GetName() == name) return lv;
    }
    return nullptr;
}