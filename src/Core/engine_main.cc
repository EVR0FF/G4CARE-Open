//==============================================================================
// G4CARE - engine_main.cc — entry point of the g4care-engine sidecar.
//
// Modes:
//   g4care-engine --rpc          JSON-RPC 2.0 over stdio (default for a GUI).
//   g4care-engine [config.yaml]  Batch run (same as the G4CARE CLI wrapper).
//
// In --rpc mode Geant4 output is redirected to stderr so stdout stays a clean
// JSON channel.
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-10-07  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================
#include "SimulationEngine.hh"
#include "JsonRpcServer.hh"

#include <TROOT.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {
    ROOT::EnableThreadSafety();

    bool rpc = false;
    std::string configPath = "config.yaml";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rpc") == 0) {
            rpc = true;
        } else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            configPath = argv[++i];
        } else if (argv[i][0] != '-') {
            configPath = argv[i];
        }
    }

    auto* engine = SimulationEngine::Instance();
    engine->setCommandLine(argc, argv);

    if (rpc) {
        // Keep the process stdout clean for JSON: redirect fd 1 (stdout) to
        // fd 2 (stderr) so ALL Geant4/ROOT output (G4cout, G4Exception,
        // G4GDML, ...) goes to stderr, and write JSON to the saved fd.
        int jsonFd = dup(1);              // save original stdout
        dup2(2, 1);                       // stdout -> stderr
        FILE* jsonOut = fdopen(jsonFd, "w");
        setvbuf(jsonOut, nullptr, _IONBF, 0);  // unbuffered, line-based JSON

        JsonRpcServer server;
        server.setOutput(jsonOut);
        int code = server.run();
        fclose(jsonOut);
        SimulationEngine::DeleteInstance();
        return code;
    }

    // Batch mode.
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
