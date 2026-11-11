//==============================================================================
// G4CARE
// @file    JsonRpcServer.hh
// @brief   JSON-RPC 2.0 server over stdio (NDJSON framing).
//
// @details
//   Reads one JSON-RPC request per line from stdin and writes one JSON-RPC
//   response/notification per line to stdout. Geant4 logging is redirected to
//   stderr by the caller so stdout stays a clean JSON channel.
//
//   Supported methods: ping, getStatus, loadConfig, init, run, reset,
//   setVisualization, exportGdml, exportVtk, shutdown. Long-running "run"
//   emits "progress" notifications from a lock-free poller thread.
//
// @author  I. I. Everstov  @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-10-07  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef JSON_RPC_SERVER_HH
#define JSON_RPC_SERVER_HH

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "DataIO/DICOM/DicomService.hh"

/// @brief JSON-RPC 2.0 server over stdio.
class JsonRpcServer {
public:
    JsonRpcServer() : fDicom(*DicomService::Instance()) {}

    /// @brief Set the output stream for JSON messages (default: stdout).
    void setOutput(FILE* f) { fOut = f; }

    /// @brief Run the request loop until shutdown. @return process exit code.
    int run();

private:
    void dispatch(const nlohmann::json& msg);
    void writeLine(const nlohmann::json& msg) const;

    FILE* fOut = stdout;

    nlohmann::json handlePing(const nlohmann::json& params);
    nlohmann::json handleGetStatus(const nlohmann::json& params);
    nlohmann::json handleLoadConfig(const nlohmann::json& params);
    nlohmann::json handleLoadConfigText(const nlohmann::json& params);
    nlohmann::json handleInit(const nlohmann::json& params);
    nlohmann::json handleRun(const nlohmann::json& params);
    nlohmann::json handleReset(const nlohmann::json& params);
    nlohmann::json handleSetVisualization(const nlohmann::json& params);
    nlohmann::json handleExportGdml(const nlohmann::json& params);
    nlohmann::json handleExportVtk(const nlohmann::json& params);
    nlohmann::json handleExportSceneVtk(const nlohmann::json& params);
    nlohmann::json handleExecuteCommand(const nlohmann::json& params);
    nlohmann::json handleSampleSource(const nlohmann::json& params);
    nlohmann::json handleDicomLoad(const nlohmann::json& params);
    nlohmann::json handleDicomSlice(const nlohmann::json& params);
    nlohmann::json handleDicomVolume(const nlohmann::json& params);
    nlohmann::json handleVolumeLoad(const nlohmann::json& params);
    nlohmann::json handleDicomRtStructLoad(const nlohmann::json& params);
    nlohmann::json handleDicomRtStructSave(const nlohmann::json& params);
    nlohmann::json handleDicomTransfer(const nlohmann::json& params);
    nlohmann::json handleDicomTransferMeshes(const nlohmann::json& params);
    nlohmann::json handleDicomRtDoseLoad(const nlohmann::json& params);
    nlohmann::json handleDicomRtPlanLoad(const nlohmann::json& params);
    nlohmann::json handleDicomRtPlanSave(const nlohmann::json& params);
    nlohmann::json handleDicomRtDoseSave(const nlohmann::json& params);
    nlohmann::json handleShutdown(const nlohmann::json& params);

    bool fShutdown = false;
    DicomService& fDicom;
};

#endif // JSON_RPC_SERVER_HH
