//==============================================================================
//
// G4CARE
//
// @file    GenericVertexGenerator.cc
// @brief   Procedural vertex generator — parametric placement of primary vertices.
//
// @details
//   Used by ParticleGunSource and RadioactiveSource when
//   VERTEX_GENERATOR.type is set.  Compiles ExprTK expressions for
//   vertex count, position, time, weight, and reject_if.  Supports
//   surface and volume sampling, per-particle definitions, and
//   local variable bindings that connect user variables to
//   compiled expression pointers across all expressions.
//
//   Processing flow:
//   1. Constructor — compiles top-level expressions (count/pos/time/…),
//      parses particle list, initialises surface/volume samplers,
//      and builds local variable bindings.
//   2. GenerateVertices() — evaluates the count expression, then for
//      each vertex: samples surface/volume points, updates local
//      variable targets, evaluates pos/time/weight/reject, creates
//      G4PrimaryVertex and G4PrimaryParticle objects.
//
//   Configuration keys read from <configPath>.*:
//     count, pos.x/.y/.z, time, weight, reject_if,
//     volume, surface, PARTICLE, variables.*
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

#include "GenericVertexGenerator.hh"
#include "ParticleUtils.hh"
#include "G4SystemOfUnits.hh"
#include "G4PhysicalConstants.hh"
#include "G4PhysicalVolumeStore.hh"
#include "BeamAnalysis.hh"
#include "ConfigManager.hh"
#include "G4ParticleDefinition.hh"
#include "ParticleGunSource.hh"

#include <sstream>
#include <cmath>

/// @brief Construct a vertex generator and compile all configuration expressions.
///
/// @param configPath ConfigManager key prefix (e.g. "SOURCE.e_gun.VERTEX_GENERATOR").
/// @param eval       ExpressionEvaluator for compilation (non-owning pointer).
/// @param sourceName Human-readable name of the owning source.
///
/// Reads count, position, time, weight, reject_if, volume, surface, particle
/// list and local variables from the config.  Initialises surface/volume
/// samplers if requested, compiles all expressions, and builds local variable
/// bindings that propagate values to all compiled expressions.
GenericVertexGenerator::GenericVertexGenerator(const std::string& configPath, 
                                               ExpressionEvaluator* eval,
                                               const std::string& sourceName)
    : fEval(eval), fSourceName(sourceName)
{
    auto* cfg = ConfigManager::Instance();

    // --- Read all configuration expressions from YAML ---
    // Vertex count expression (how many vertices per event)
    std::string countStr = cfg->GetString(configPath + ".count", "1");
    // Position expressions (may reference surf_*/vol_* variables)
    std::string posXStr = cfg->GetString(configPath + ".pos.x", "");
    std::string posYStr = cfg->GetString(configPath + ".pos.y", "");
    std::string posZStr = cfg->GetString(configPath + ".pos.z", "");
    // Global time and weight for all vertices
    std::string timeStr = cfg->GetString(configPath + ".time", "0");
    std::string weightStr = cfg->GetString(configPath + ".weight", "1.0");
    // Optional rejection expression (vertex skipped if non-zero)
    std::string rejectStr = cfg->GetString(configPath + ".reject_if", "");

    // Volume/surface names for constrained placement
    fVolumeName = cfg->GetString(configPath + ".volume", "");
    fSurfaceName = cfg->GetString(configPath + ".surface", "");

    // --- Mathematical sphere (no G4 volume needed) ---
    std::string radiusStr = cfg->GetString(configPath + ".radius", "");
    if (!radiusStr.empty()) {
        std::map<std::string, double> emptyVars;
        std::vector<std::map<std::string, double>> emptyResults;
        fSphereRadius = fEval->EvaluateWithUnit(radiusStr, emptyVars, emptyResults);
        fUseMathSphere = true;
        // Read center (default: origin)
        std::string cx = cfg->GetString(configPath + ".center.x", "0");
        std::string cy = cfg->GetString(configPath + ".center.y", "0");
        std::string cz = cfg->GetString(configPath + ".center.z", "0");
        fSphereCenter = G4ThreeVector(
            fEval->EvaluateWithUnit(cx, emptyVars, emptyResults),
            fEval->EvaluateWithUnit(cy, emptyVars, emptyResults),
            fEval->EvaluateWithUnit(cz, emptyVars, emptyResults)
        );
    }

    // --- Load user-defined local variables ---
    // These become ExprTK variables accessible in all expressions.
    // Values are evaluated per-vertex and propagated to all compiled
    // expressions via LocalVarBinding::UpdateTargets.
    std::string varPath = configPath + ".variables";
    auto varKeys = cfg->GetSectionKeys(varPath);
    std::vector<std::pair<std::string, std::string>> localVarStrs;
    for (const auto& key : varKeys) {
        localVarStrs.emplace_back(key, cfg->GetString(varPath + "." + key, ""));
    }

    // Build the full set of variable names: built-in + user-defined
    std::vector<std::string> allVarNames = {"event_id", "v_i", "surf_x", "surf_y", "surf_z", "surf_dx", "surf_dy", "surf_dz", "vol_x", "vol_y", "vol_z"};
    for (const auto& p : localVarStrs) allVarNames.push_back(p.first);

    // Lambda: precompile a raw expression string into a PrecompiledExpr
    auto precompile = [&](const std::string& raw) -> ExpressionEvaluator::PrecompiledExpr {
        return ExpressionEvaluator::PrecompiledExpr(raw, allVarNames, fEval);
    };

    // --- Compile top-level vertex expressions ---
    auto countPrecomp   = precompile(countStr);
    auto posXPrecomp    = precompile(posXStr);
    auto posYPrecomp    = precompile(posYStr);
    auto posZPrecomp    = precompile(posZStr);
    auto timePrecomp    = precompile(timeStr);
    auto weightPrecomp  = precompile(weightStr);
    auto rejectPrecomp  = precompile(rejectStr);

    // Initialise FastExprGen wrappers (cache variable pointers for fast eval)
    fCountExpr.Init(countPrecomp);
    fPosXExpr.Init(posXPrecomp);
    fPosYExpr.Init(posYPrecomp);
    fPosZExpr.Init(posZPrecomp);
    fTimeExpr.Init(timePrecomp);
    fWeightExpr.Init(weightPrecomp);
    fRejectIfExpr.Init(rejectPrecomp);

    // --- Parse per-particle definitions (energy, direction, type, etc.) ---
    fParticles = ParticleUtils::ParseParticles(cfg, configPath + ".PARTICLES", allVarNames, fEval);
    for (auto& part : fParticles) {
        part.fastIaea.Init(part.iaeaExpr);
        part.fastEnergy.Init(part.energyExpr);
        part.fastPx.Init(part.pxExpr);
        part.fastPy.Init(part.pyExpr);
        part.fastPz.Init(part.pzExpr);
        part.fastDirX.Init(part.dirXExpr);
        part.fastDirY.Init(part.dirYExpr);
        part.fastDirZ.Init(part.dirZExpr);
        part.fastDirTheta.Init(part.dirThetaExpr);
        part.fastDirPhi.Init(part.dirPhiExpr);
        part.fastPolX.Init(part.polXExpr);
        part.fastPolY.Init(part.polYExpr);
        part.fastPolZ.Init(part.polZExpr);
        part.fastProperTime.Init(part.properTimeExpr);
        part.fastWeight.Init(part.weightExpr);
    }

    // --- Initialise surface / volume samplers ---
    // Surface takes priority over volume if both are specified.
    if (!fSurfaceName.empty() && !fVolumeName.empty()) {
        G4cerr << "GenericVertexGenerator: both volume and surface specified, using surface only." << G4endl;
        fVolumeName.clear();
    }
    if (!fSurfaceName.empty()) {
        fSurfaceSampler = std::make_unique<SurfaceSampler>(fSurfaceName);
        if (!fSurfaceSampler->IsValid()) {
            G4cerr << "GenericVertexGenerator: failed to create surface sampler for '" << fSurfaceName << "'" << G4endl;
            fSurfaceSampler.reset();
            fSurfaceName.clear();
        }
    }
    if (!fVolumeName.empty()) {
        G4PhysicalVolumeStore* store = G4PhysicalVolumeStore::GetInstance();
        if (store) {
            fVolumePhys = store->GetVolume(fVolumeName, false);
            if (!fVolumePhys) {
                G4cerr << "GenericVertexGenerator: volume '" << fVolumeName << "' not found in store." << G4endl;
            }
        } else {
            G4cerr << "GenericVertexGenerator: PhysicalVolumeStore not available." << G4endl;
        }
    }

    // --- Build local variable bindings ---
    // Collect pointers to all FastExprGen/FastExpr objects so that
    // when a local variable is updated, we can write the value to
    // every expression that references it by name.

    // Gather all main (vertex-level) expression wrappers
    std::vector<FastExprGen*> mainExprs;
    mainExprs.push_back(&fCountExpr);
    mainExprs.push_back(&fPosXExpr);
    mainExprs.push_back(&fPosYExpr);
    mainExprs.push_back(&fPosZExpr);
    mainExprs.push_back(&fTimeExpr);
    mainExprs.push_back(&fWeightExpr);
    mainExprs.push_back(&fRejectIfExpr);

    // Gather all per-particle expression wrappers
    std::vector<ParticleGunSource::FastExpr*> particleExprs;
    for (auto& part : fParticles) {
        particleExprs.push_back(&part.fastIaea);
        particleExprs.push_back(&part.fastEnergy);
        particleExprs.push_back(&part.fastPx);
        particleExprs.push_back(&part.fastPy);
        particleExprs.push_back(&part.fastPz);
        particleExprs.push_back(&part.fastDirX);
        particleExprs.push_back(&part.fastDirY);
        particleExprs.push_back(&part.fastDirZ);
        particleExprs.push_back(&part.fastDirTheta);
        particleExprs.push_back(&part.fastDirPhi);
        particleExprs.push_back(&part.fastPolX);
        particleExprs.push_back(&part.fastPolY);
        particleExprs.push_back(&part.fastPolZ);
        particleExprs.push_back(&part.fastProperTime);
        particleExprs.push_back(&part.fastWeight);
    }

    // For each local variable, find all expressions that reference it
    // and store pointers for fast bulk update during event generation.
    for (const auto& p : localVarStrs) {
        LocalVarBinding bnd;
        bnd.name = p.first;
        auto precomp = precompile(p.second);
        bnd.fast.Init(precomp);

        // Search main (vertex-level) expressions for this variable
        for (FastExprGen* fe : mainExprs) {
            if (!fe->comp) continue;
            double* ptr = ExpressionEvaluator::GetVariablePtr(fe->comp, bnd.name);
            if (ptr) bnd.target_ptrs.push_back(ptr);
        }

        // Search per-particle expressions for this variable
        for (ParticleGunSource::FastExpr* fe : particleExprs) {
            if (!fe->comp) continue;
            double* ptr = ExpressionEvaluator::GetVariablePtr(fe->comp, bnd.name);
            if (ptr) bnd.target_ptrs.push_back(ptr);
        }

        // Deduplicate pointers (same variable may appear in multiple expressions)
        std::sort(bnd.target_ptrs.begin(), bnd.target_ptrs.end());
        bnd.target_ptrs.erase(std::unique(bnd.target_ptrs.begin(), bnd.target_ptrs.end()), bnd.target_ptrs.end());

        if (bnd.target_ptrs.empty()) {
            G4cerr << "GenericVertexGenerator: warning – local variable '" << bnd.name
                << "' is defined but not used in any expression." << G4endl;
        }

        fLocalVarBindings.push_back(std::move(bnd));
    }
}

/// @brief Generate all primary vertices for one event.
///
/// @param event_id Current event ID (used in expression evaluation).
/// @return         Vector of G4PrimaryVertex pointers (caller owns).
///
/// Evaluates the count expression to determine N vertices.  For each
/// vertex i ∈ [0, N):
/// - Samples surface/volume coordinates if samplers are active.
/// - Evaluates local variable bindings and pushes values to target pointers.
/// - Checks reject_if; skips vertex if expression evaluates to non-zero.
/// - Evaluates pos/time/weight expressions.
/// - Creates G4PrimaryVertex and G4PrimaryParticle via
///   ParticleUtils::CreateParticleFast.
/// - Registers the vertex in BeamAnalysis for the "primary" NTuple.
std::vector<G4PrimaryVertex*> GenericVertexGenerator::GenerateVertices(double event_id)
{
    std::vector<G4PrimaryVertex*> vertices;

    // Evaluate the count expression to determine how many vertices to generate
    int nVertices = static_cast<int>(std::round(fCountExpr.Evaluate(event_id, 0.0)));
    if (event_id < 5) {
        G4cout << "[GVG] event_id=" << event_id << " nVertices=" << nVertices
               << " useMathSphere=" << (fUseMathSphere ? 1 : 0)
               << " r=" << fSphereRadius << " center=(" << fSphereCenter.x() << "," << fSphereCenter.y() << "," << fSphereCenter.z() << ")" << G4endl;
    }
    if (nVertices <= 0) return vertices;

    // Generate each vertex
    for (int i = 0; i < nVertices; ++i) {
        double v_i = static_cast<double>(i);

        // --- Step 1: sample surface / volume coordinates (if configured) ---
        double surf_x = 0.0, surf_y = 0.0, surf_z = 0.0;
        double surf_dx = 0.0, surf_dy = 0.0, surf_dz = 0.0;
        if (fUseMathSphere) {
            // Mathematical sphere (identical to official xray_TESdetector):
            // Two uniform random points on sphere → position + direction
            double theta0 = std::acos(1.0 - 2.0 * G4UniformRand());
            double theta1 = std::acos(1.0 - 2.0 * G4UniformRand());
            double phi0   = twopi * G4UniformRand();
            double phi1   = twopi * G4UniformRand();
            double r = fSphereRadius;
            surf_x  = r * std::sin(theta0) * std::cos(phi0) + fSphereCenter.x();
            surf_y  = r * std::sin(theta0) * std::sin(phi0) + fSphereCenter.y();
            surf_z  = r * std::cos(theta0) + fSphereCenter.z();
            surf_dx = r * std::sin(theta1) * std::cos(phi1) + fSphereCenter.x();
            surf_dy = r * std::sin(theta1) * std::sin(phi1) + fSphereCenter.y();
            surf_dz = r * std::cos(theta1) + fSphereCenter.z();
        } else if (fSurfaceSampler) {
            G4ThreeVector p1 = fSurfaceSampler->SamplePoint();
            G4ThreeVector p2 = fSurfaceSampler->SamplePoint();  // second sample for direction
            surf_x = p1.x(); surf_y = p1.y(); surf_z = p1.z();
            surf_dx = p2.x(); surf_dy = p2.y(); surf_dz = p2.z();
        }
        double vol_x = 0.0, vol_y = 0.0, vol_z = 0.0;
        if (fVolumePhys) {
            G4ThreeVector p = PositionSampler::SampleInPhysicalVolume(fVolumePhys);
            vol_x = p.x(); vol_y = p.y(); vol_z = p.z();
        }

        // --- Step 2: evaluate local variables and push to all target pointers ---
        // This propagates user-defined variable values (e.g. "radius")
        // into every compiled expression that references them, before
        // the expressions are evaluated.
        for (auto& bnd : fLocalVarBindings) {
            double val = bnd.fast.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);
            bnd.UpdateTargets(val);
        }

        // --- Step 3: check reject_if expression ---
        // If the expression evaluates to non-zero, skip this vertex entirely.
        if (fRejectIfExpr.comp) {
            double reject = fRejectIfExpr.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);
            if (reject != 0.0) continue;
        }

        // --- Step 4: evaluate position, time, and weight expressions ---
        // These may reference surf_*/vol_* variables for surface- or
        // volume-constrained placement.
        double x = fPosXExpr.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);
        double y = fPosYExpr.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);
        double z = fPosZExpr.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);
        double time   = fTimeExpr.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);
        double weight = fWeightExpr.Evaluate(event_id, v_i, surf_x, surf_y, surf_z, vol_x, vol_y, vol_z);

        // --- Step 5: create G4PrimaryVertex at (x, y, z, time) ---
        G4PrimaryVertex* vertex = new G4PrimaryVertex(x, y, z, time);
        vertex->SetWeight(weight);

        // --- Step 6: create primary particles for this vertex ---
        // If surface/volume sampler is active, override direction using two surface points
        G4PrimaryParticle* lastParticle = nullptr;
        if (event_id < 5) G4cout << "[GVG] event_id=" << event_id << " nParticles=" << fParticles.size() << G4endl;
        for (size_t pIdx = 0; pIdx < fParticles.size(); ++pIdx) {
            const auto& part = fParticles[pIdx];
            double p_i = static_cast<double>(pIdx);

            G4PrimaryParticle* particle = ParticleUtils::CreateParticleFast(part, event_id, v_i, p_i);
            if (event_id < 5) {
                G4cout << "[GVG]   pIdx=" << pIdx << " type='" << part.type << "'"
                       << " energyExpr.comp=" << (part.energyExpr.compiled ? 1 : 0)
                       << " particle=" << (particle ? particle->GetParticleDefinition()->GetParticleName() : "NULL")
                       << " E=" << (particle ? particle->GetKineticEnergy()/MeV : -1) << " MeV" << G4endl;
            }
            if (particle) {
                if (fUseMathSphere || fSurfaceSampler) {
                    G4ThreeVector dir(surf_dx - surf_x, surf_dy - surf_y, surf_dz - surf_z);
                    if (dir.mag() > 0) particle->SetMomentumDirection(dir.unit());
                }
                vertex->SetPrimary(particle);
                lastParticle = particle;
            }
        }

        // --- Step 7: register vertex or discard if no particles were created ---
        if (vertex->GetNumberOfParticle() > 0) {
            vertices.push_back(vertex);
        } else {
            delete vertex;
        }
    }

    return vertices;
}
