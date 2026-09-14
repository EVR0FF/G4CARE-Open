//==============================================================================
//
// G4CARE
//
// @file    ParticleGunSource.hh
// @brief   Configurable particle gun source with ExprTK expressions.
//
// @details
//   Reads SOURCE.<name> configuration to set up a particle gun with
//   user-defined expressions for particle type, energy, direction,
//   emission time, and multiple-vertex support. Compiles ExprTK
//   expressions for high-performance primary generation.
//
//   Configuration keys read from SOURCE.<name>.*:
//     particle, energy, direction.x/y/z, pos.x/y/z, time, macro,
//     VERTEX_GENERATOR, VERTICES, vertex_mode, reject_if, variables.
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

#ifndef PARTICLE_GUN_SOURCE_HH
#define PARTICLE_GUN_SOURCE_HH

#include "Source.hh"
#include "ExpressionEvaluator.hh"
#include "Config/ConfigManager.hh"
#include <memory>
#include <vector>
#include <string>
#include <map>

class GenericVertexGenerator;
class ExpressionEvaluator;

/// @brief Particle gun source with ExprTK-based parameter evaluation.
class ParticleGunSource : public Source {
public:
    ParticleGunSource(const std::string& sourceName);
    virtual ~ParticleGunSource();

    /// @brief Generate primary particles for the given event.
    virtual void GeneratePrimaries(G4Event* event) override;
    
    /// @brief Fast compiled expression evaluator with cached variable pointers.
    struct FastExpr {
        ExpressionEvaluator::CompiledExpression* comp = nullptr;
        double* ptr_event_id = nullptr;
        double* ptr_v_i      = nullptr;
        double* ptr_p_i      = nullptr;
        double unitMultiplier = 1.0;

        void Init(const ExpressionEvaluator::PrecompiledExpr& precomp) {
            comp = precomp.compiled.get();
            unitMultiplier = precomp.unitMultiplier;
            if (comp) {
                ptr_event_id = ExpressionEvaluator::GetVariablePtr(comp, "event_id");
                ptr_v_i      = ExpressionEvaluator::GetVariablePtr(comp, "v_i");
                ptr_p_i      = ExpressionEvaluator::GetVariablePtr(comp, "p_i");
            }
        }

        inline double Evaluate(double event_id, double v_i = 0.0, double p_i = 0.0) const {
            if (!comp) return 0.0;
            if (ptr_event_id) *ptr_event_id = event_id;
            if (ptr_v_i)      *ptr_v_i      = v_i;
            if (ptr_p_i)      *ptr_p_i      = p_i;
            // Apply EXPRS runtime variables (EXPRS feedback → SOURCE)
            for (const auto& [vname, vptr] : comp->varPtrs) {
                double rtVal = ConfigManager::GetRuntime(vname);
                if (rtVal != 0.0 || ConfigManager::GetAllRuntime().count(vname))
                    if (vptr) *vptr = rtVal;
            }
            return comp->expr.value() * unitMultiplier;
        }
    };

    /// @brief Per-particle descriptor with compiled expressions.
    struct ParticleDesc {

        std::string type;
        int Z = 0, A = 0;
        double excitation = 0.0;
        double magnetic_moment = 0.0;
        double lifetime = 0.0;
        std::string dirMode;
        double coneAngle = 0.0;
        double sigmaTheta = 0.0;
        double sigmaPhi = 0.0;

        ExpressionEvaluator::PrecompiledExpr iaeaExpr;
        ExpressionEvaluator::PrecompiledExpr energyExpr;
        ExpressionEvaluator::PrecompiledExpr pxExpr, pyExpr, pzExpr;
        ExpressionEvaluator::PrecompiledExpr dirXExpr, dirYExpr, dirZExpr;
        ExpressionEvaluator::PrecompiledExpr dirThetaExpr, dirPhiExpr;
        ExpressionEvaluator::PrecompiledExpr polXExpr, polYExpr, polZExpr;
        ExpressionEvaluator::PrecompiledExpr properTimeExpr;
        ExpressionEvaluator::PrecompiledExpr weightExpr;

        FastExpr fastIaea;
        FastExpr fastEnergy;
        FastExpr fastPx, fastPy, fastPz;
        FastExpr fastDirX, fastDirY, fastDirZ;
        FastExpr fastDirTheta, fastDirPhi;
        FastExpr fastPolX, fastPolY, fastPolZ;
        FastExpr fastProperTime;
        FastExpr fastWeight;
    };

private:
    
    /// @brief Vertex descriptor with position, time, weight, reject expressions.
    struct VertexDesc {
        bool enabled = true;
        ExpressionEvaluator::PrecompiledExpr posXExpr;
        ExpressionEvaluator::PrecompiledExpr posYExpr;
        ExpressionEvaluator::PrecompiledExpr posZExpr;
        ExpressionEvaluator::PrecompiledExpr timeExpr;
        ExpressionEvaluator::PrecompiledExpr weightExpr;
        ExpressionEvaluator::PrecompiledExpr rejectIfExpr;
        std::vector<ParticleDesc> particles;

        FastExpr fastPosX, fastPosY, fastPosZ;
        FastExpr fastTime;
        FastExpr fastWeight;
        FastExpr fastRejectIf;
    };

    struct VarExpr {
        std::string name;
        ExpressionEvaluator::PrecompiledExpr expr;
        FastExpr fast;
    };

    enum class VertexMode { kSequential, kRandom };

    /// @brief Read SOURCE config section and compile all expressions.
    void ParseConfig();
    bool CreateVertexGenerator(const std::string& type);

    std::string fSourceName;
    std::unique_ptr<ExpressionEvaluator> fEval;
    std::vector<VarExpr> fVars;
    std::vector<VertexDesc> fVertices;
    std::unique_ptr<GenericVertexGenerator> fVertexGenerator;
    VertexMode fVertexMode = VertexMode::kSequential;

    size_t fDataRows = 0;
    bool fCheckDataRows = false;
};

#endif