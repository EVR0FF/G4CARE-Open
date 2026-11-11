//==============================================================================
// G4CARE
// @file    LogVerbose.hh
// @brief   Thin global output filter: routes G4CARE informational messages
//          through a VERBOSITY gate (0 = quiet, 1 = info, 2 = verbose).
// @details G4CARE_LOG(level, ...) prints only when VERBOSITY >= level.
//   Use level 1 for normal informational messages and level 2 for debug.
//   G4cerr (errors/warnings) is intentionally NOT filtered.
//==============================================================================

#ifndef G4CARE_LOGVERBOSE_HH
#define G4CARE_LOGVERBOSE_HH

#include "G4ios.hh"
#include "ConfigManager.hh"

/// Current global verbosity (cached read of the VERBOSITY config key).
inline int G4CAREVerbosity() {
    return ConfigManager::Instance()->GetInt("VERBOSITY", 0);
}

/// G4CARE_LOG(level, ...) — prints stream expression ... only if VERBOSITY >= level.
#define G4CARE_LOG(level, ...) \
    do { if (G4CAREVerbosity() >= (level)) { G4cout << __VA_ARGS__ << G4endl; } } while (0)

#endif
