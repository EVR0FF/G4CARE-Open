#ifndef GENERIC_VERTEX_GENERATOR_HH
#define GENERIC_VERTEX_GENERATOR_HH

//==============================================================================
// G4CARE
// @file    GenericVertexGenerator.hh
// @brief   Expression-driven vertex generator with surface/volume sampling
// @details Generates primary vertices using ExprTk expressions for
//   vertex count, position, time, and weight. Supports surface
//   sampling via SurfaceSampler and volume sampling via
//   PositionSampler. Expressions can reference built-in variables:
//   event_id, v_i (vertex index), surf_x/y/z (surface sample),
//   vol_x/y/z (volume sample). Local variable bindings allow
//   precomputed values to be forwarded to dependent expressions.
//   Uses FastExprGen (compiled expression + direct variable pointers)
//   for high-performance evaluation in the event loop.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "G4PrimaryVertex.hh"
#include "SurfaceSampler.hh"
#include "PositionSampler.hh"
#include "ExpressionEvaluator.hh"
#include "ParticleGunSource.hh"

#include <memory>
#include <vector>
#include <string>
#include <map>

class GenericVertexGenerator {
public:
    /// @brief Constructor
    /// @param configPath YAML config path for this vertex generator
    /// @param eval       Pointer to the ExpressionEvaluator
    /// @param sourceName Name of the parent source
    GenericVertexGenerator(const std::string& configPath, ExpressionEvaluator* eval, const std::string& sourceName);

    /// @brief Destructor
    ~GenericVertexGenerator() = default;

    /// @brief Generate all primary vertices for one event
    /// @param event_id Current event index
    /// @return Vector of G4PrimaryVertex pointers
    std::vector<G4PrimaryVertex*> GenerateVertices(double event_id);

private:
    /// @brief Fast compiled expression wrapper with direct variable pointers
    ///
    /// Binds the compiled expression to built-in variable pointers
    /// (event_id, v_i, surf_x/y/z, vol_x/y/z) for zero-overhead
    /// variable updates during evaluation.
    struct FastExprGen {
        ExpressionEvaluator::CompiledExpression* comp = nullptr;
        double* ptr_event_id = nullptr;
        double* ptr_v_i      = nullptr;
        double* ptr_surf_x   = nullptr;
        double* ptr_surf_y   = nullptr;
        double* ptr_surf_z   = nullptr;
        double* ptr_vol_x    = nullptr;
        double* ptr_vol_y    = nullptr;
        double* ptr_vol_z    = nullptr;

        /// @brief Initialize variable pointers from a precompiled expression
        /// @param precomp The precompiled expression to bind
        void Init(const ExpressionEvaluator::PrecompiledExpr& precomp) {
            comp = precomp.compiled.get();
            if (comp) {
                ptr_event_id = ExpressionEvaluator::GetVariablePtr(comp, "event_id");
                ptr_v_i      = ExpressionEvaluator::GetVariablePtr(comp, "v_i");
                ptr_surf_x   = ExpressionEvaluator::GetVariablePtr(comp, "surf_x");
                ptr_surf_y   = ExpressionEvaluator::GetVariablePtr(comp, "surf_y");
                ptr_surf_z   = ExpressionEvaluator::GetVariablePtr(comp, "surf_z");
                ptr_vol_x    = ExpressionEvaluator::GetVariablePtr(comp, "vol_x");
                ptr_vol_y    = ExpressionEvaluator::GetVariablePtr(comp, "vol_y");
                ptr_vol_z    = ExpressionEvaluator::GetVariablePtr(comp, "vol_z");
            }
        }

        /// @brief Evaluate the expression with given variable values
        /// @param event_id Current event ID
        /// @param v_i      Vertex index within event
        /// @param surf_x   Surface sample X coordinate
        /// @param surf_y   Surface sample Y coordinate
        /// @param surf_z   Surface sample Z coordinate
        /// @param vol_x    Volume sample X coordinate
        /// @param vol_y    Volume sample Y coordinate
        /// @param vol_z    Volume sample Z coordinate
        /// @return Evaluated expression value
        inline double Evaluate(double event_id, double v_i,
                               double surf_x = 0.0, double surf_y = 0.0, double surf_z = 0.0,
                               double vol_x = 0.0, double vol_y = 0.0, double vol_z = 0.0) const {
            if (!comp) return 0.0;
            if (ptr_event_id) *ptr_event_id = event_id;
            if (ptr_v_i)      *ptr_v_i      = v_i;
            if (ptr_surf_x)   *ptr_surf_x   = surf_x;
            if (ptr_surf_y)   *ptr_surf_y   = surf_y;
            if (ptr_surf_z)   *ptr_surf_z   = surf_z;
            if (ptr_vol_x)    *ptr_vol_x    = vol_x;
            if (ptr_vol_y)    *ptr_vol_y    = vol_y;
            if (ptr_vol_z)    *ptr_vol_z    = vol_z;
            return comp->expr.value();
        }
    };

    /// @brief Local variable binding for forwarding computed values
    ///
    /// Evaluates a source expression and writes the result into
    /// one or more target variable pointers, allowing precomputed
    /// values to be shared across multiple expressions.
    struct LocalVarBinding {
        std::string name;                     ///< Variable name
        FastExprGen fast;                     ///< Source expression
        std::vector<double*> target_ptrs;     ///< Target variable pointers to update

        /// @brief Evaluate the expression and update all target pointers
        /// @param val The computed value to write
        inline void UpdateTargets(double val) const {
            for (double* ptr : target_ptrs) if (ptr) *ptr = val;
        }
    };

    /// @brief Pointer to the shared expression evaluator
    ExpressionEvaluator* fEval;

    /// @name Expression generators
    /// @{
    FastExprGen fCountExpr;      ///< Number of vertices per event
    FastExprGen fPosXExpr;       ///< Vertex X position
    FastExprGen fPosYExpr;       ///< Vertex Y position
    FastExprGen fPosZExpr;       ///< Vertex Z position
    FastExprGen fTimeExpr;       ///< Vertex time
    FastExprGen fWeightExpr;     ///< Vertex weight
    FastExprGen fRejectIfExpr;   ///< Rejection condition (if > 0, skip vertex)
    /// @}

    std::vector<LocalVarBinding> fLocalVarBindings;

    /// @brief Target volume name for PositionSampler
    std::string fVolumeName;

    /// @brief Target surface name for SurfaceSampler
    std::string fSurfaceName;

    /// @brief Parent source name
    std::string fSourceName;

    /// @brief Optional surface sampler
    std::unique_ptr<SurfaceSampler> fSurfaceSampler;

    /// @brief Mathematical sphere radius (used when no G4 volume)
    double fSphereRadius = -1.0;

    /// @brief Mathematical sphere center (used when no G4 volume)
    G4ThreeVector fSphereCenter;

    /// @brief Use mathematical sphere instead of G4 volume/surface
    bool fUseMathSphere = false;

    /// @brief Target physical volume pointer
    G4VPhysicalVolume* fVolumePhys = nullptr;

    /// @brief Particle definitions for this source
    std::vector<ParticleGunSource::ParticleDesc> fParticles;
};

#endif