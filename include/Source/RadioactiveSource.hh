//==============================================================================
//
// G4CARE
//
// @file    RadioactiveSource.hh
// @brief   Radioactive decay ion source with isotope selection and vertex sampling.
//
// @details
//   Generates primary ions (G4IonTable) representing radioactive isotopes
//   with per-isotope abundance selection.  Supports three vertex placement
//   modes: expression-based, volume-sampled, and surface-sampled.
//
//   Configuration keys read from SOURCE.<name>:
//     ISOTOPES.<idx>.(name|Z|A|excitation|abundance|lifetime)
//     VERTICES.<idx>.position.(type|x|y|z|volume), .time, .weight
//     object, pos.(x|y|z), time, weight
//     decay.(enable_photo_evaporation|enable_arm|macro), isomer_table,
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

#ifndef RADIOACTIVE_SOURCE_HH
#define RADIOACTIVE_SOURCE_HH

#include "Source.hh"
#include "ObjectManager.hh"
#include "ExpressionEvaluator.hh"
#include "SurfaceSampler.hh"
#include "G4IonTable.hh"
#include "G4ParticleDefinition.hh"
#include "G4Event.hh"
#include <memory>
#include <vector>
#include <map>

class RadioactiveSource : public Source {
public:
    RadioactiveSource(const std::string& sourceName, ObjectManager* objMgr);
    virtual ~RadioactiveSource();
    /// @brief Generate primary ion vertices for one Geant4 event.
    ///        Selects isotopes by abundance and vertices by mode.
    virtual void GeneratePrimaries(G4Event* event) override;

private:
    
    struct FastExpr {
        ExpressionEvaluator::CompiledExpression* comp = nullptr;
        double* ptr_event_id = nullptr;
        double* ptr_v_i      = nullptr;
        double* ptr_p_i      = nullptr;

        void Init(const ExpressionEvaluator::PrecompiledExpr& precomp) {
            comp = precomp.compiled.get();
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
            return comp->expr.value();
        }
    };

    struct IsotopeDesc {
        G4String name;
        G4int Z = 0;
        G4int A = 0;
        G4double excitation = 0.0;
        G4double abundance = 1.0;
        G4double lifetime = 0.0;
        G4ParticleDefinition* cachedIon = nullptr;
        
        void EnsureIon() {
            if (!cachedIon && Z > 0 && A > 0) {
                cachedIon = G4IonTable::GetIonTable()->GetIon(Z, A, excitation);
                if (lifetime > 0) {
                    cachedIon->SetPDGLifeTime(lifetime);
                }
            }
        }
    };

    struct VertexDesc {
        enum PosType { kNone, kExpression, kVolume, kSurface } posType = kNone;
        
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> posXExpr;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> posYExpr;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> posZExpr;

        G4String volumeName;

        G4LogicalVolume* cachedLogicalVolume = nullptr;
        std::shared_ptr<SurfaceSampler> cachedSurfaceSampler;
        
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> timeExpr;
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> weightExpr;
        
        FastExpr fastPosX;
        FastExpr fastPosY;
        FastExpr fastPosZ;
        FastExpr fastTime;
        FastExpr fastWeight;
        
        VertexDesc() = default;
        VertexDesc(const VertexDesc&) = delete;
        VertexDesc& operator=(const VertexDesc&) = delete;
        VertexDesc(VertexDesc&&) = default;
        VertexDesc& operator=(VertexDesc&&) = default;
    };

    enum class VertexMode { kSequential, kRandom };

    void ParseConfig();
    void ParseIsotopes(const std::string& basePath);
    void ParseVertices(const std::string& basePath);
    void ParseVariables(const std::string& basePath);
    std::vector<std::string> GetAllVariableNames() const;
    G4ThreeVector SamplePosition(const VertexDesc& vtx, double event_id, double v_i);
    G4LogicalVolume* FindLogicalVolume(const G4String& name) const;
    
    std::string fSourceName;
    ObjectManager* fObjMgr;
    std::unique_ptr<ExpressionEvaluator> fEval;
    
    std::map<std::string, double> fVariables;
    std::vector<IsotopeDesc> fIsotopes;
    std::vector<double> fCumulativeAbundances;
    
    std::vector<VertexDesc> fVertices;
    VertexMode fVertexMode = VertexMode::kSequential;
    
    bool fUseObject = false;
    G4String fObjectName;
    VertexDesc fSingleVertex;

    bool fEnablePhotoEvaporation;
    bool fEnableARM;
    G4String fDecayMacro;
    G4String fIsomerTable;
    bool fIonsEnsured = false;
};

#endif