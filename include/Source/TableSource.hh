//==============================================================================
//
// G4CARE
//
// @file    TableSource.hh
// @brief   Source that replays a particle table (CSV / ROOT / IAEA phase space).
//
// @details
//   Reads a tabular data file and emits one particle per event by mapping
//   columns by name to: particle type, energy, position, direction and
//   statistical weight.  Position is in cm and energy in MeV (IAEA convention).
//   Missing columns fall back to fixed config values (particle/energy) or to
//   isotropic direction / unit weight.
//
//   The file provides the beam kinematics (type / energy / direction / weight)
//   while the vertex position can be re-placed like the other sources:
//     pos     — fixed point {x,y,z} in cm (overrides file x/y/z);
//     object  — uniform sampling inside a logical/physical volume;
//     surface — uniform sampling on a volume surface (SurfaceSampler);
//     offset  — translation {x,y,z} in cm added to file x/y/z.
//   Precedence: surface > object > pos > file (x/y/z + offset).
//
//   pos/offset/energy/weight/reject_if also accept ExprTK expressions wrapped
//   in $$ ... $$ (evaluated per event, like ParticleGunSource).  Units are the
//   file convention: pos/offset in cm, energy in MeV, weight dimensionless.
//   A trailing unit suffix after the closing $$ (e.g. "$$ ... $$ keV") is
//   converted to the field's unit.  Available variables inside expressions:
//     event_id, x, y, z, energy, u, v, w, wt
//   (the file row columns), plus rand_gauss/rand_uniform/... functions.
//
//   Configuration keys read from SOURCE.<name>.*:
//     file, particle, energy, mode (random|sequential), loop,
//     pos.x/y/z, offset.x/y/z, object, surface, weight, reject_if,
//     delimiter/header/skip_first_n (CSV), tree (ROOT).
//
//==============================================================================

#ifndef TABLE_SOURCE_HH
#define TABLE_SOURCE_HH

#include "Source.hh"
#include "DataReader.hh"
#include "SurfaceSampler.hh"
#include "ExpressionEvaluator.hh"
#include "Config/ConfigManager.hh"
#include "G4ThreeVector.hh"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class G4ParticleGun;
class G4VPhysicalVolume;

/// @brief Particle-table replay source (CSV / ROOT / IAEA).
class TableSource : public Source {
public:
    explicit TableSource(const std::string& sourceName);
    ~TableSource() override;

    void GeneratePrimaries(G4Event* event) override;

    /// @brief Map a numeric particle-type code (IAEA convention) to a particle name.
    static std::string ParticleFromTypeCode(int type);

private:
    std::unique_ptr<G4ParticleGun> fGun;
    std::unique_ptr<DataReader> fReader;

    int fColType = -1, fColE = -1;
    int fColX = -1, fColY = -1, fColZ = -1;
    int fColU = -1, fColV = -1, fColW = -1, fColWt = -1;

    std::string fParticle;   // fixed particle when no type column
    bool fSequential = false;
    bool fLoop = true;
    std::size_t fNextRow = 0;

    /// @brief A field that is either a static number or a compiled ExprTK expression.
    struct Expr {
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> compiled; // null => static
        double staticVal = 0.0;   // static value in target units (cm/MeV)
        double unitScale = 1.0;   // multiply expr.value() by this -> target units
        bool active = false;      // config key present

        double* ptrEventId = nullptr;
        double* ptrX = nullptr, *ptrY = nullptr, *ptrZ = nullptr;
        double* ptrEnergy = nullptr;
        double* ptrU = nullptr, *ptrV = nullptr, *ptrW = nullptr, *ptrWt = nullptr;

        void CachePointers() {
            ptrEventId = ExpressionEvaluator::GetVariablePtr(compiled.get(), "event_id");
            ptrX  = ExpressionEvaluator::GetVariablePtr(compiled.get(), "x");
            ptrY  = ExpressionEvaluator::GetVariablePtr(compiled.get(), "y");
            ptrZ  = ExpressionEvaluator::GetVariablePtr(compiled.get(), "z");
            ptrEnergy = ExpressionEvaluator::GetVariablePtr(compiled.get(), "energy");
            ptrU  = ExpressionEvaluator::GetVariablePtr(compiled.get(), "u");
            ptrV  = ExpressionEvaluator::GetVariablePtr(compiled.get(), "v");
            ptrW  = ExpressionEvaluator::GetVariablePtr(compiled.get(), "w");
            ptrWt = ExpressionEvaluator::GetVariablePtr(compiled.get(), "wt");
        }

        double Evaluate(double eventId, double x, double y, double z,
                        double energy, double u, double v, double w, double wt) const {
            if (!compiled) return staticVal;
            if (ptrEventId) *ptrEventId = eventId;
            if (ptrX) *ptrX = x;
            if (ptrY) *ptrY = y;
            if (ptrZ) *ptrZ = z;
            if (ptrEnergy) *ptrEnergy = energy;
            if (ptrU) *ptrU = u;
            if (ptrV) *ptrV = v;
            if (ptrW) *ptrW = w;
            if (ptrWt) *ptrWt = wt;
            // SCRIPT.scripts runtime feedback -> SOURCE (same as ParticleGunSource::FastExpr).
            for (const auto& [vname, vptr] : compiled->varPtrs) {
                double rtVal = ConfigManager::GetRuntime(vname);
                if (rtVal != 0.0 || ConfigManager::GetAllRuntime().count(vname))
                    if (vptr) *vptr = rtVal;
            }
            return compiled->expr.value() * unitScale;
        }
    };

    /// @brief Compile one config field into an Expr (or a static value).
    /// @param targetUnit Geant4 unit value the result should be expressed in
    ///                   (e.g. cm, MeV); 0.0 means dimensionless.
    /// @param bareExpr   Treat the whole raw string as an ExprTK expression
    ///                   (no $$ wrapper), used for reject_if.
    Expr CompileField(const std::string& key, double defVal, double targetUnit,
                      const std::vector<std::string>& varNames, bool bareExpr = false);

    std::unique_ptr<ExpressionEvaluator> fEval;

    // Placement / energy / weight / reject_if — ExprTK-capable fields.
    Expr fPosX, fPosY, fPosZ;    // pos.* (cm)
    Expr fOffX, fOffY, fOffZ;    // offset.* (cm)
    Expr fEnergyExpr;            // fixed energy fallback (MeV)
    Expr fWeightExpr;            // weight override (dimensionless)
    Expr fRejectIf;              // reject_if (non-zero rejects the particle)
    bool fHasPos = false;
    bool fHasOffset = false;


    std::string fObjectName;               // volume name for uniform volume sampling
    std::string fSurfaceName;              // surface name for uniform surface sampling
    std::unique_ptr<SurfaceSampler> fSurfaceSampler;  // surface sampler (when surface set)
    G4VPhysicalVolume* fVolumePhys = nullptr;         // physical volume for volume sampling
};

#endif // TABLE_SOURCE_HH
