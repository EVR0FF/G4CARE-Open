//==============================================================================
// G4CARE
// @file    ExprsManager.hh
// @brief   Manages per-volume accumulation of filter variables and executes
//          user-defined ExprTk scripts at step, event, track, and run boundaries.
// @details ExprsManager reads EXPRS_BLOCKS from ConfigManager, compiles the
//   embedded ExprTk scripts, resolves target logical volumes (with wildcard
//   support), and uses a lock-free thread-local accumulator pool to collect
//   per-volume filter-variable sums.  Scripts are triggered at configurable
//   intervals (N_STEP, N_EVENT, N_TRACK, N_RUN) with either "Start" or "End"
//   timing.  Supports user-inline functions and PCOMP precompiled expressions
//   for use in GEOMETRY/SOURCE configuration.
//
//   Configuration keys read: EXPRS_BLOCKS.*.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef EXPRS_MANAGER_HH
#define EXPRS_MANAGER_HH

#include "ExpressionEvaluator.hh"
#include "ColumnTypes.hh"
#include "G4Threading.hh"
#include <string>
#include <vector>
#include <map>
#include <memory>

class G4LogicalVolume;
class ConfigManager;

/// @brief Compiles and executes user ExprTk scripts on collected per-volume data.
class ExprsManager {
public:
    enum class When { End, Start };

    struct CompiledLine {
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> compiled;
        std::string sourceLine;
    };

    /// User-inline function wrapper (igeneric_function for ExprTk)
    struct UserInlineFunc : public exprtk::igeneric_function<double> {
        std::shared_ptr<ExpressionEvaluator::CompiledExpression> compiled;
        std::vector<std::string> argNames;

        UserInlineFunc()
            : exprtk::igeneric_function<double>("T*", exprtk::igeneric_function<double>::e_rtrn_scalar) {}

        void setArg(int idx, double val) {
            if (idx >= 0 && idx < static_cast<int>(argNames.size()) && compiled) {
                auto it = compiled->varPtrs.find(argNames[idx]);
                if (it != compiled->varPtrs.end()) *(it->second) = val;
            }
        }

        double operator()(parameter_list_t parameters) override {
            int n = static_cast<int>(parameters.size());
            for (int i = 0; i < n; ++i) {
                auto& p = parameters[i];
                double val = (p.type == exprtk::type_store<double>::e_scalar)
                             ? *static_cast<double*>(p.data) : 0.0;
                setArg(i, val);
            }
            return compiled ? compiled->expr.value() : 0.0;
        }
    };

    struct ExprsBlock {
        std::string name;
        std::string volumeSelector;
        bool isPattern = false;
        std::vector<std::string> excludeVolumes;
        std::string scriptSource;
        double volumeMass = 0.0;
        std::vector<std::shared_ptr<ExpressionEvaluator::CompiledExpression>> compiledScripts;
        std::vector<std::string> varNames;
        std::vector<G4LogicalVolume*> volumes;

        // ── New unified counters ──
        int nStep  = 0;   // N_STEP  — every N steps
        int nEvent = 0;   // N_EVENT — every N events
        int nTrack = 0;   // N_TRACK — every N tracks
        int nRun   = 0;   // N_RUN   — end-of-run (1 = execute once)
        When when = When::End;

        /// User functions compiled for this block
        std::vector<std::unique_ptr<UserInlineFunc>> userFuncs;
        /// SCRIPT constants (:= variables) visible in this block
        std::map<std::string, double> scriptConstants;

        /// PCOMP — precompiled expressions for GEOMETRY/SOURCE
        std::vector<std::string> pcomp;
        std::map<std::string, double> precompiledValues;
    };

    ExprsManager();
    ~ExprsManager();

    bool LoadFromConfig(ConfigManager* cfg);
    const std::vector<ExprsBlock>& GetBlocks() const { return fBlocks; }

    static ExprsManager* Instance();
    static void DeleteInstance();

    /// Per-volume accumulation (thread-safe, no mutex).
    void InitAccumulators();
    void Reset();
    void Accumulate(const G4LogicalVolume* lv, const FilterVars& vars);
    void EventEnd(const G4LogicalVolume* lv, bool hadInteraction);
    void Merge();

    /// Get merged values as a map for ExprTk execution.
    std::map<std::string, double> GetBlockValues(const ExprsBlock& block) const;

    /// Script execution hooks (end of step/event/track).
    void TryExecuteStepScripts(const G4LogicalVolume* lv, const FilterVars& vars);
    void TryExecuteEventScripts(const G4LogicalVolume* lv);
    void TryExecuteTrackScripts(const G4LogicalVolume* lv, const FilterVars& vars);

    /// Script execution hooks (start of step/event/track — WHEN: start).
    void TryExecuteStepScriptsStart(const G4LogicalVolume* lv, const FilterVars& vars);
    void TryExecuteEventScriptsStart(const G4LogicalVolume* lv);
    void TryExecuteTrackScriptsStart(const G4LogicalVolume* lv, const FilterVars& vars);

    /// Execute N_RUN scripts at end of run.
    void ExecuteRunScripts();

    /// PCOMP — get precompiled values for GEOMETRY/SOURCE.
    double GetPrecompiled(const std::string& name) const;

private:
    void ResolveVolumes(ExprsBlock& block);
    bool MatchWildcard(const std::string& pattern, const std::string& name);

    std::vector<ExprsBlock> fBlocks;
    static ExprsManager* fInstance;

    /// Global function registry (shared across all blocks).
    std::vector<std::unique_ptr<UserInlineFunc>> fGlobalFuncs;
    std::map<std::string, exprtk::igeneric_function<double>*> fGlobalFuncMap;

    /// Thread-local accumulator slot.
    struct ThreadSlot {
        std::map<const G4LogicalVolume*, FilterVars> data;
        std::map<const G4LogicalVolume*, double> nEvents;
        std::map<const G4LogicalVolume*, double> nInteractingEvents;
    };
    
    static std::vector<ThreadSlot>* sGlobalSlots;
    static size_t sGlobalNumSlots;
    static std::map<const G4LogicalVolume*, FilterVars> sMerged;
    static std::map<const G4LogicalVolume*, double> sMergedEvents;
    static std::map<const G4LogicalVolume*, double> sMergedInteracting;

    /// Thread-local per-block counters (no atomics, no locks).
    static thread_local std::vector<long> sBlockStepCounts;
    static thread_local std::vector<long> sBlockEventCounts;
    static thread_local std::vector<long> sBlockTrackCounts;
};

#endif // EXPRS_MANAGER_HH