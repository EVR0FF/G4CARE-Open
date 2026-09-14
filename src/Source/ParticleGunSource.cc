//==============================================================================
//
// G4CARE
//
// @file    ParticleGunSource.cc
// @brief   Implementation of ParticleGunSource — ExprTK-based particle gun.
//
// @details
//   Parses SOURCE.<name> configuration and generates primary particles
//   using compiled ExprTK expressions for energy, direction, position,
//   and time.  Supports multiple vertices, per-particle overrides,
//   vertex generators, and data-driven IAEA sampling.
//
//   Configuration keys read from SOURCE.<name>.*:
//     particle, energy, direction, pos, time, vertex_mode,
//     VERTEX_GENERATOR, VERTICES, reject_if, variables,
//     macro, iaea, data_alias, cone_angle, sigma_theta, sigma_phi.
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

#include "ParticleGunSource.hh"
#include "ConfigManager.hh"
#include "ParticleUtils.hh"
#include "GenericVertexGenerator.hh"
#include "SamplingUtils.hh"
#include "ExpressionEvaluator.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "G4ParticleTable.hh"
#include "G4IonTable.hh"
#include "G4SystemOfUnits.hh"
#include "G4RandomDirection.hh"
#include "Randomize.hh"
#include "G4ios.hh"
#include "BeamAnalysis.hh"
#include "G4RunManager.hh"
#include "UnifiedSource.hh"

#include <sstream>
#include <regex>

namespace {
    
    /// @brief Convert a string to boolean (case-insensitive).
    /// @details Recognises "true", "yes", "1", "on" (any case) as @c true;
    ///          all other strings return @c false.
    /// @param s Input string.
    /// @return @c true if @p s represents a truthy value.
    bool ParseBool(const std::string& s) {
        std::string lower;
        lower.resize(s.size());
        std::transform(s.begin(), s.end(), lower.begin(), ::tolower);
        return (lower == "true" || lower == "yes" || lower == "1" || lower == "on");
    }
}

ParticleGunSource::ParticleGunSource(const std::string& sourceName)
    : fSourceName(sourceName)
{
    fEval = std::make_unique<ExpressionEvaluator>();
    ParseConfig();
}

ParticleGunSource::~ParticleGunSource() = default;

/// @brief Read and compile the entire SOURCE.<name> configuration block.
///
/// @details
/// Three mutually exclusive modes are supported:
/// 1. **Simple mode** — a top-level `particle` key is present.  One vertex
///    is created with pos/energy/direction/weight/reject_if expressions
///    compiled from the source section.  Per-particle overrides are read
///    from SOURCE.<name>.PARTICLES via ParticleUtils::ParseParticles.
/// 2. **VERTICES mode** — no top-level `particle` and no VERTEX_GENERATOR.
///    SOURCE.<name>.VERTICES subsections are enumerated (sorted
///    numerically), each defining its own position, time, weight,
///    reject_if, and per-vertex particle list.
/// 3. **VERTEX_GENERATOR mode** — SOURCE.<name>.VERTEX_GENERATOR.type
///    is set (currently only `"line"` is implemented).  Vertex placement
///    is delegated to GenericVertexGenerator.
///
/// All numeric fields are compiled as ExprTK expressions stored in
/// FastExpr structs for low-latency evaluation during event generation.
/// User variables from SOURCE.<name>.variables are exported as
/// runtime variables accessible in EXPRS scripts.
///
/// @see ParticleGunSource::GeneratePrimaries
void ParticleGunSource::ParseConfig() {
    auto* cfg = ConfigManager::Instance();

    G4cout << "ParticleGunSource::ParseConfig - Source: '" << fSourceName << "'" << G4endl;

    std::string varPath = fSourceName + ".variables";
    auto varKeys = cfg->GetSectionKeys(varPath);

    std::vector<std::string> allVarNames = {"event_id", "v_i", "p_i"};
    for (const auto& key : varKeys) {
        
        allVarNames.push_back(key);
    }

    for (const auto& key : varKeys) {
        std::string raw = cfg->GetString(varPath + "." + key, "");
        VarExpr ve;
        ve.name = key;
        ve.expr = ExpressionEvaluator::PrecompiledExpr(raw, allVarNames, fEval.get());
        ve.fast.Init(ve.expr);
        fVars.push_back(std::move(ve));
        
        G4cout << "ParticleGunSource::ParseConfig - Variable '" << key 
               << "' = '" << raw << "'" << G4endl;
    }

    std::string modeStr = cfg->GetString(fSourceName + ".vertex_mode", "sequential");
    if (modeStr == "random") {
        fVertexMode = VertexMode::kRandom;
    } else {
        fVertexMode = VertexMode::kSequential;
    }

    std::string genType = cfg->GetString(fSourceName + ".VERTEX_GENERATOR.type", "");
    if (!genType.empty()) {
        if (!CreateVertexGenerator(genType)) {
            G4cerr << "ParticleGunSource: Failed to create vertex generator of type " << genType << G4endl;
        }
        return;
    } else {
        if (cfg->HasKey(fSourceName + ".VERTEX_GENERATOR.count") ||
            cfg->HasKey(fSourceName + ".VERTEX_GENERATOR.position.x")) {
            G4cout << "ParticleGunSource: No type specified, assuming generic vertex generator." << G4endl;
            fVertexGenerator = std::make_unique<GenericVertexGenerator>(fSourceName + ".VERTEX_GENERATOR", fEval.get(), fSourceName);
            return;
        }
    }

    std::string simplestParticle = cfg->GetString(fSourceName + ".particle", "");
    if (!simplestParticle.empty()) {
        G4cout << "ParticleGunSource::ParseConfig - Simple mode: particle=" << simplestParticle << G4endl;
        VertexDesc vtx;
        vtx.enabled = true;
        vtx.posXExpr = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(fSourceName + ".pos.x", "0"), allVarNames, fEval.get());
        vtx.posYExpr = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(fSourceName + ".pos.y", "0"), allVarNames, fEval.get());
        vtx.posZExpr = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(fSourceName + ".pos.z", "0"), allVarNames, fEval.get());
        vtx.timeExpr = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(fSourceName + ".time", "0"), allVarNames, fEval.get());
        vtx.weightExpr = ExpressionEvaluator::PrecompiledExpr(
            cfg->GetString(fSourceName + ".weight", "1.0"), allVarNames, fEval.get());
        std::string rawReject = cfg->GetString(fSourceName + ".reject_if", "");
        // Detect multi-line script (contains "var " — compile as script, pass energy as variable)
        if (rawReject.find("var ") != std::string::npos) {
            // Build extended var list with "energy" available in reject_if context
            std::vector<std::string> extVars = allVarNames;
            extVars.push_back("energy");
            vtx.rejectIfExpr.compiled = fEval->Precompile(rawReject, extVars);
            vtx.rejectIfExpr.expr = rawReject;
            vtx.rejectIfExpr.unitMultiplier = 1.0;
        } else {
            vtx.rejectIfExpr = ExpressionEvaluator::PrecompiledExpr(
                rawReject, allVarNames, fEval.get());
        }
        vtx.particles = ParticleUtils::ParseParticles(cfg, fSourceName + ".PARTICLES", allVarNames, fEval.get());
        if (vtx.particles.empty()) {
            ParticleDesc pd;
            pd.type = simplestParticle;
            pd.energyExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".energy", "0"), allVarNames, fEval.get());
            pd.dirXExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".direction.x", "0"), allVarNames, fEval.get());
            pd.dirYExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".direction.y", "0"), allVarNames, fEval.get());
            pd.dirZExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".direction.z", "1"), allVarNames, fEval.get());
            pd.weightExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(fSourceName + ".weight", "1.0"), allVarNames, fEval.get());
            vtx.particles.push_back(std::move(pd));
        }
        for (auto& partDesc : vtx.particles) {
            partDesc.fastIaea.Init(partDesc.iaeaExpr);
            partDesc.fastEnergy.Init(partDesc.energyExpr);
            partDesc.fastPx.Init(partDesc.pxExpr);
            partDesc.fastPy.Init(partDesc.pyExpr);
            partDesc.fastPz.Init(partDesc.pzExpr);
            partDesc.fastDirX.Init(partDesc.dirXExpr);
            partDesc.fastDirY.Init(partDesc.dirYExpr);
            partDesc.fastDirZ.Init(partDesc.dirZExpr);
            partDesc.fastDirTheta.Init(partDesc.dirThetaExpr);
            partDesc.fastDirPhi.Init(partDesc.dirPhiExpr);
            partDesc.fastPolX.Init(partDesc.polXExpr);
            partDesc.fastPolY.Init(partDesc.polYExpr);
            partDesc.fastPolZ.Init(partDesc.polZExpr);
            partDesc.fastProperTime.Init(partDesc.properTimeExpr);
            partDesc.fastWeight.Init(partDesc.weightExpr);
        }
        vtx.fastPosX.Init(vtx.posXExpr);
        vtx.fastPosY.Init(vtx.posYExpr);
        vtx.fastPosZ.Init(vtx.posZExpr);
        vtx.fastTime.Init(vtx.timeExpr);
        vtx.fastWeight.Init(vtx.weightExpr);
        vtx.fastRejectIf.Init(vtx.rejectIfExpr);
        fVertices.push_back(std::move(vtx));
    } else {
        std::string verticesPath = fSourceName + ".VERTICES";
        std::vector<std::string> vertexIndices = cfg->GetSubsections(verticesPath);
        std::sort(vertexIndices.begin(), vertexIndices.end(),
                  [](const std::string& a, const std::string& b) {
                      return std::stoi(a) < std::stoi(b);
                  });
        for (const auto& idxStr : vertexIndices) {
            std::string vtxPath = verticesPath + "." + idxStr;
            G4cout << "ParticleGunSource::ParseConfig - Vertex " << idxStr << G4endl;
            VertexDesc vtx;
            vtx.enabled = cfg->GetBool(vtxPath + ".enabled", true);
            vtx.posXExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vtxPath + ".pos.x", ""), allVarNames, fEval.get());
            vtx.posYExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vtxPath + ".pos.y", ""), allVarNames, fEval.get());
            vtx.posZExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vtxPath + ".pos.z", ""), allVarNames, fEval.get());
            vtx.timeExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vtxPath + ".time", "0"), allVarNames, fEval.get());
            vtx.weightExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vtxPath + ".weight", "1.0"), allVarNames, fEval.get());
            vtx.rejectIfExpr = ExpressionEvaluator::PrecompiledExpr(
                cfg->GetString(vtxPath + ".reject_if", ""), allVarNames, fEval.get());
            vtx.particles = ParticleUtils::ParseParticles(cfg, vtxPath + ".PARTICLES", allVarNames, fEval.get());
            for (auto& partDesc : vtx.particles) {
                partDesc.fastIaea.Init(partDesc.iaeaExpr);
                partDesc.fastEnergy.Init(partDesc.energyExpr);
                partDesc.fastPx.Init(partDesc.pxExpr);
                partDesc.fastPy.Init(partDesc.pyExpr);
                partDesc.fastPz.Init(partDesc.pzExpr);
                partDesc.fastDirX.Init(partDesc.dirXExpr);
                partDesc.fastDirY.Init(partDesc.dirYExpr);
                partDesc.fastDirZ.Init(partDesc.dirZExpr);
                partDesc.fastDirTheta.Init(partDesc.dirThetaExpr);
                partDesc.fastDirPhi.Init(partDesc.dirPhiExpr);
                partDesc.fastPolX.Init(partDesc.polXExpr);
                partDesc.fastPolY.Init(partDesc.polYExpr);
                partDesc.fastPolZ.Init(partDesc.polZExpr);
                partDesc.fastProperTime.Init(partDesc.properTimeExpr);
                partDesc.fastWeight.Init(partDesc.weightExpr);
            }
            vtx.fastPosX.Init(vtx.posXExpr);
            vtx.fastPosY.Init(vtx.posYExpr);
            vtx.fastPosZ.Init(vtx.posZExpr);
            vtx.fastTime.Init(vtx.timeExpr);
            vtx.fastWeight.Init(vtx.weightExpr);
            vtx.fastRejectIf.Init(vtx.rejectIfExpr);
            fVertices.push_back(std::move(vtx));
        }
    }
    std::string dataAlias = cfg->GetString(fSourceName + ".data_alias", "");
    if (!dataAlias.empty()) {
        int idx = ExpressionEvaluator::GetDataAlias(dataAlias);
        if (idx >= 0) {
            auto reader = ExpressionEvaluator::GetReader(idx);
            if (reader) {
                fDataRows = reader->GetNumberOfRows();
                fCheckDataRows = true;
                G4cout << "ParticleGunSource: will stop after " << fDataRows << " events." << G4endl;
            } else {
                G4cerr << "ParticleGunSource: WARNING - data alias '" << dataAlias << "' has no reader." << G4endl;
            }
        } else {
            G4cerr << "ParticleGunSource: WARNING - data alias '" << dataAlias << "' not found." << G4endl;
        }
    }
}

/// @brief Instantiate a vertex generator of the requested type.
///
/// @details
/// Reads SOURCE.<name>.VERTEX_GENERATOR configuration and creates
/// a GenericVertexGenerator that handles procedural vertex placement.
/// Supported generator types:
/// - `"line"` — vertices placed along a parametric line
///   (fully implemented).
/// - `"grid"` — regular 2D/3D grid of vertices (planned).
/// - `"random_in_volume"` — uniform random sampling within a logical
///   volume (planned).
/// - `"custom"` — user-supplied vertex generator via shared library
///   (planned).
///
/// @param type Generator type identifier (currently only `"line"` is
///        implemented; others log a TODO message and return @c false).
/// @return @c true if a generator was successfully created and stored
///         in fVertexGenerator.
bool ParticleGunSource::CreateVertexGenerator(const std::string& type) {
    auto* cfg = ConfigManager::Instance();
    std::string genPath = fSourceName + ".VERTEX_GENERATOR";

    if (type == "line") {
        fVertexGenerator = std::make_unique<GenericVertexGenerator>(genPath, fEval.get(), fSourceName);
        G4cout << "ParticleGunSource: Line vertex generator created." << G4endl;
        return true;
    } else if (type == "grid") {
        // Future extension: regular 2D/3D grid of vertices
    } else if (type == "random_in_volume") {
        // Future extension: uniform random sampling within a logical volume
    } else if (type == "custom") {
        // Future extension: user-supplied vertex generator plugin
    } else {
        G4cerr << "ParticleGunSource: Unknown vertex generator type: " << type << G4endl;
        return false;
    }
    return false;
}

/// @brief Generate all primary vertices and particles for one Geant4 event.
///
/// @details
/// **Processing order:**
/// 1. Evaluate all user variables (SOURCE.<name>.variables.*) and
///    export them via ConfigManager::SetRuntime so they are visible in
///    EXPRS scripts (EXPRS ↔ SOURCE feedback loop).
/// 2. If a data_alias table is active and the current event index exceeds
///    the number of rows, abort the run via G4RunManager::AbortRun().
/// 3. **Vertex-generator path** — if fVertexGenerator is active,
///    call GenericVertexGenerator::GenerateVertices() to obtain a list
///    of G4PrimaryVertex objects, then:
///    - Sequential mode: add every generated vertex to the event.
///    - Random mode: select one vertex by weight (G4UniformRand) and
///      delete the rest.
/// 4. **Direct-vertices path** — iterate over fVertices (populated by
///    ParseConfig from simple or VERTICES mode):
///    - Sequential mode: add all enabled vertices, each with its full
///      particle list.  For each vertex: evaluate pos/time/weight/
///      reject_if, create a G4PrimaryVertex, then for each particle
///      evaluate energy/direction/weight via FastExpr and call
///      ParticleUtils::CreateParticleFast.
///    - Random mode: build a temporary list of all generated vertices
///      with their weights, then select one vertex by roulette-wheel
///      sampling.  Vertices that fail the reject_if check are skipped.
///      Unselected vertices are deleted.
///
/// Each selected vertex is also registered in BeamAnalysis via
/// UnifiedSource for the "primary" NTuple tree.
///
/// @param event Current Geant4 event to populate with primary tracks.
void ParticleGunSource::GeneratePrimaries(G4Event* event) {
    double current_event_id = event->GetEventID();

    // Export user variables to runtime vars (for EXPRS ↔ SOURCE)
    for (const auto& v : fVars) {
        double val = v.fast.Evaluate(current_event_id, 0.0, 0.0);
        ConfigManager::SetRuntime(v.name, val);
    }

     if (fCheckDataRows && current_event_id >= fDataRows) {
        G4RunManager::GetRunManager()->AbortRun();
        return;
    }

    if (fVertexGenerator) {

        std::vector<G4PrimaryVertex*> vertices = fVertexGenerator->GenerateVertices(current_event_id);
        if (fVertexMode == VertexMode::kSequential) {
            for (auto* v : vertices) {
                G4PrimaryParticle* lastParticle = (v->GetNumberOfParticle() > 0) 
                    ? v->GetPrimary(0) : nullptr;
                BeamAnalysis::Instance()->Fill("primary", UnifiedSource(v, lastParticle));
                event->AddPrimaryVertex(v);
            }
        } else {
            std::vector<double> weights;
            for (auto* v : vertices) weights.push_back(v->GetWeight());
            double totalWeight = 0.0;
            for (double w : weights) totalWeight += w;
            if (totalWeight > 0.0) {
                double r = G4UniformRand() * totalWeight;
                double cum = 0.0;
                for (size_t i = 0; i < vertices.size(); ++i) {
                    cum += weights[i];
                    if (r <= cum) {
                        G4PrimaryParticle* lastParticle = (vertices[i]->GetNumberOfParticle() > 0)
                            ? vertices[i]->GetPrimary(0) : nullptr;
                        BeamAnalysis::Instance()->Fill("primary", UnifiedSource(vertices[i], lastParticle));
                        event->AddPrimaryVertex(vertices[i]);
                        break;
                    }
                }
            }
            for (auto* v : vertices) {
                if (v != event->GetPrimaryVertex()) delete v;
            }
        }
        return;
    }

    if (fVertices.empty()) return;

    if (fVertexMode == VertexMode::kSequential) {
        for (size_t vIdx = 0; vIdx < fVertices.size(); ++vIdx) {
            const auto& vtx = fVertices[vIdx];
            if (!vtx.enabled) continue;
            double v_i = static_cast<double>(vIdx);

            // Evaluate energy first, pass to reject_if
            G4PrimaryParticle* lastParticle = nullptr;
            double energyVal = 0;
            for (size_t pIdx = 0; pIdx < vtx.particles.size(); ++pIdx) {
                energyVal = vtx.particles[pIdx].fastEnergy.Evaluate(current_event_id, v_i, static_cast<double>(pIdx));
                break;
            }

            if (vtx.fastRejectIf.comp) {
                auto* eptr = ExpressionEvaluator::GetVariablePtr(vtx.fastRejectIf.comp, "energy");
                if (eptr) *eptr = energyVal;
                double reject = vtx.fastRejectIf.Evaluate(current_event_id, v_i, 0.0);
                if (reject != 0.0) continue;
            }

            double x = vtx.fastPosX.Evaluate(current_event_id, v_i, 0.0);
            double y = vtx.fastPosY.Evaluate(current_event_id, v_i, 0.0);
            double z = vtx.fastPosZ.Evaluate(current_event_id, v_i, 0.0);
            double time = vtx.fastTime.Evaluate(current_event_id, v_i, 0.0);
            double vtxWeight = vtx.fastWeight.Evaluate(current_event_id, v_i, 0.0);
            if (vtxWeight <= 0.0) continue;

            G4PrimaryVertex* vertex = new G4PrimaryVertex(x, y, z, time);
            vertex->SetWeight(vtxWeight);
            
            lastParticle = nullptr;
            for (size_t pIdx = 0; pIdx < vtx.particles.size(); ++pIdx) {
                const auto& part = vtx.particles[pIdx];
                double p_i = static_cast<double>(pIdx);
                
                G4PrimaryParticle* particle = ParticleUtils::CreateParticleFast(part, current_event_id, v_i, p_i);
                if (particle) {
                    if (event->GetEventID() < 5) {
                        G4cout << "Event " << event->GetEventID()
                            << ": particle " << particle->GetParticleDefinition()->GetParticleName()
                            << " pos=(" << x/mm << ", " << y/mm << ", " << z/mm << ") mm"
                            << " E=" << particle->GetKineticEnergy()/MeV << " MeV" << G4endl;
                    }
                    vertex->SetPrimary(particle);
                    lastParticle = particle;
                }
            }

            if (vertex->GetNumberOfParticle() > 0) {
                BeamAnalysis::Instance()->Fill("primary", UnifiedSource(vertex, lastParticle));
                event->AddPrimaryVertex(vertex);
            } else {
                delete vertex;
            }
        }
    } else { // kRandom
    std::vector<G4PrimaryVertex*> generatedVertices;
    std::vector<double> weights;

    for (size_t vIdx = 0; vIdx < fVertices.size(); ++vIdx) {
        const auto& vtx = fVertices[vIdx];
        if (!vtx.enabled) continue;
        double v_i = static_cast<double>(vIdx);

        if (vtx.fastRejectIf.comp) {
            double reject = vtx.fastRejectIf.Evaluate(current_event_id, v_i, 0.0);
            if (reject != 0.0) continue;
        }

        double x = vtx.fastPosX.Evaluate(current_event_id, v_i, 0.0);
        double y = vtx.fastPosY.Evaluate(current_event_id, v_i, 0.0);
        double z = vtx.fastPosZ.Evaluate(current_event_id, v_i, 0.0);
        double time = vtx.fastTime.Evaluate(current_event_id, v_i, 0.0);
        double vtxWeight = vtx.fastWeight.Evaluate(current_event_id, v_i, 0.0);
        if (vtxWeight <= 0.0) continue;

        G4PrimaryVertex* vertex = new G4PrimaryVertex(x, y, z, time);
        vertex->SetWeight(vtxWeight);

        G4PrimaryParticle* lastParticle = nullptr;
        for (size_t pIdx = 0; pIdx < vtx.particles.size(); ++pIdx) {
            const auto& part = vtx.particles[pIdx];
            double p_i = static_cast<double>(pIdx);
            G4PrimaryParticle* particle = ParticleUtils::CreateParticleFast(part, current_event_id, v_i, p_i);
            if (particle) {
                vertex->SetPrimary(particle);
                lastParticle = particle;
            }
        }

        if (vertex->GetNumberOfParticle() > 0) {
            generatedVertices.push_back(vertex);
            weights.push_back(vtxWeight);
            BeamAnalysis::Instance()->Fill("primary", UnifiedSource(vertex, lastParticle));
        } else {
            delete vertex;
        }
    }
    
    G4PrimaryVertex* pickedVertex = nullptr;
        double totalWeight = 0.0;
        for (double w : weights) totalWeight += w;
        if (totalWeight > 0.0) {
            double r = G4UniformRand() * totalWeight;
            double cum = 0.0;
            for (size_t i = 0; i < weights.size(); ++i) {
                cum += weights[i];
                if (r <= cum && generatedVertices[i] != nullptr) {
                    pickedVertex = generatedVertices[i];
                    event->AddPrimaryVertex(pickedVertex);
                    break;
                }
            }
        }

        for (auto* v : generatedVertices) {
            if (v && v != pickedVertex) delete v;
        }

    }

}
