//==============================================================================
// G4CARE - main.cc — thin CLI wrapper around SimulationEngine.
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-10-07  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================
#include "SimulationEngine.hh"

#include <TROOT.h>
#include <string>
#include <iostream>

int main(int argc, char** argv) {
    ROOT::EnableThreadSafety();

    std::string configPath = "config.yaml";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            ++i;  // consume macro path (unused in batch mode)
        } else if (arg[0] != '-') {
            configPath = arg;
        }
    }

    auto* engine = SimulationEngine::Instance();
    engine->setCommandLine(argc, argv);

    if (!engine->loadConfig(configPath)) {
        SimulationEngine::DeleteInstance();
        return 1;
    }
    if (!engine->init()) {
        SimulationEngine::DeleteInstance();
        return 1;
    }
    bool ok = engine->run();

    SimulationEngine::DeleteInstance();
    return ok ? 0 : 1;
}
