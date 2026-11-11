//==============================================================================
// G4CARE - JsonRpcServer.cc — JSON-RPC 2.0 server over stdio (NDJSON).
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-10-07  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================
#include "JsonRpcServer.hh"
#include "SimulationEngine.hh"
#include "DoseScorer.hh"
#include "G4UImanager.hh"
#include "G4RunManager.hh"
#include "PrimaryGeneratorAction.hh"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using nlohmann::json;

namespace {

json resultObj(const json& id, const json& result) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json errorObj(const json& id, int code, const std::string& message) {
    return json{{"jsonrpc", "2.0"},
                {"id", id},
                {"error", json{{"code", code}, {"message", message}}}};
}

/// Encode raw bytes as standard base64 (RFC 4648) with padding.
std::string base64Encode(const unsigned char* data, std::size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        const unsigned b0 = data[i];
        const unsigned b1 = (i + 1 < n) ? data[i + 1] : 0;
        const unsigned b2 = (i + 2 < n) ? data[i + 2] : 0;
        const unsigned v = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(T[(v >> 18) & 0x3F]);
        out.push_back(T[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < n) ? T[(v >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < n) ? T[v & 0x3F] : '=');
    }
    return out;
}

/// Encode the raw little-endian int16 bytes of a voxel buffer as base64.
std::string base64EncodeInt16(const std::vector<short>& data) {
    return base64Encode(reinterpret_cast<const unsigned char*>(data.data()),
                        data.size() * sizeof(short));
}

/// Encode the raw little-endian float32 bytes of a voxel buffer as base64.
std::string base64EncodeFloat(const std::vector<float>& data) {
    return base64Encode(reinterpret_cast<const unsigned char*>(data.data()),
                        data.size() * sizeof(float));
}

}  // namespace

void JsonRpcServer::writeLine(const json& msg) const {
    fprintf(fOut, "%s\n", msg.dump().c_str());
    fflush(fOut);
}

int JsonRpcServer::run() {
    std::string line;
    while (!fShutdown && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        json msg;
        try {
            msg = json::parse(line);
        } catch (const std::exception& e) {
            writeLine(errorObj(json(nullptr), -32700,
                               std::string("Parse error: ") + e.what()));
            continue;
        }
        dispatch(msg);
    }
    return 0;
}

void JsonRpcServer::dispatch(const json& msg) {
    if (!msg.is_object()) {
        writeLine(errorObj(json(nullptr), -32600, "Invalid Request"));
        return;
    }

    std::string method = msg.value("method", "");
    json id = msg.contains("id") ? msg["id"] : json(nullptr);
    json params = msg.value("params", json::object());
    if (!params.is_object()) params = json::object();

    json result;
    int errCode = 0;
    std::string errMsg;
    bool hasError = false;

    try {
        if (method == "ping") result = handlePing(params);
        else if (method == "getStatus") result = handleGetStatus(params);
        else if (method == "loadConfig") result = handleLoadConfig(params);
        else if (method == "loadConfigText") result = handleLoadConfigText(params);
        else if (method == "init") result = handleInit(params);
        else if (method == "run") result = handleRun(params);
        else if (method == "reset") result = handleReset(params);
        else if (method == "setVisualization") result = handleSetVisualization(params);
        else if (method == "exportGdml") result = handleExportGdml(params);
        else if (method == "exportVtk") result = handleExportVtk(params);
        else if (method == "exportSceneVtk") result = handleExportSceneVtk(params);
        else if (method == "executeCommand") result = handleExecuteCommand(params);
        else if (method == "sampleSource") result = handleSampleSource(params);
        else if (method == "dicom.load") result = handleDicomLoad(params);
        else if (method == "dicom.slice") result = handleDicomSlice(params);
        else if (method == "dicom.volume") result = handleDicomVolume(params);
        else if (method == "volume.load") result = handleVolumeLoad(params);
        else if (method == "dicom.rtstruct.load") result = handleDicomRtStructLoad(params);
        else if (method == "dicom.rtstruct.save") result = handleDicomRtStructSave(params);
        else if (method == "dicom.transfer") result = handleDicomTransfer(params);
        else if (method == "dicom.transfer.meshes") result = handleDicomTransferMeshes(params);
        else if (method == "dicom.rtdose.load") result = handleDicomRtDoseLoad(params);
        else if (method == "dicom.rtplan.load") result = handleDicomRtPlanLoad(params);
        else if (method == "dicom.rtplan.save") result = handleDicomRtPlanSave(params);
        else if (method == "dicom.rtdose.save") result = handleDicomRtDoseSave(params);
        else if (method == "shutdown") result = handleShutdown(params);
        else {
            errCode = -32601;
            errMsg = "Method not found: " + method;
            hasError = true;
        }
    } catch (const std::exception& e) {
        errCode = -32603;
        errMsg = e.what();
        hasError = true;
    }

    // Notifications (no id) receive no response.
    if (!id.is_null()) {
        if (hasError) writeLine(errorObj(id, errCode, errMsg));
        else writeLine(resultObj(id, result));
    }
}

// ── Handlers ─────────────────────────────────────────────────────────────────
json JsonRpcServer::handlePing(const json&) {
    return json{{"ok", true}, {"pong", true}};
}

json JsonRpcServer::handleGetStatus(const json&) {
    auto* e = SimulationEngine::Instance();
    return json{
        {"initialized", e->isInitialized()},
        {"visualization", e->isVisualization()},
        {"configPath", e->configPath()},
        {"eventsCompleted", SimulationEngine::EventsCompleted()},
        {"eventsTotal", SimulationEngine::EventsTotal()}
    };
}

json JsonRpcServer::handleLoadConfig(const json& params) {
    auto* e = SimulationEngine::Instance();
    std::string path = params.value("path", "");
    if (path.empty()) {
        return json{{"ok", false}, {"error", "Missing 'path' parameter"}};
    }
    bool ok = e->loadConfig(path);
    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleLoadConfigText(const json& params) {
    auto* e = SimulationEngine::Instance();
    std::string text = params.value("text", "");
    if (text.empty()) {
        return json{{"ok", false}, {"error", "Missing 'text' parameter"}};
    }
    bool ok = e->loadConfigText(text);
    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleInit(const json&) {
    auto* e = SimulationEngine::Instance();
    bool ok = e->init();
    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleRun(const json& params) {
    auto* e = SimulationEngine::Instance();
    if (!e->isInitialized()) {
        return json{{"ok", false}, {"error", "Engine is not initialized"}};
    }

    int nEvents = -1;
    if (params.contains("nEvents")) {
        nEvents = params["nEvents"].get<int>();
    }

    // Lock-free progress poller: emits "progress" notifications while the
    // synchronous run blocks this thread. No mutexes — only atomics.
    std::atomic<bool> stop{false};
    long lastDone = -1;
    std::thread poller([&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            long done = SimulationEngine::EventsCompleted();
            long total = SimulationEngine::EventsTotal();
            if (total > 0 && done != lastDone) {
                lastDone = done;
                double pct = (double)done * 100.0 / (double)total;
                writeLine(json{
                    {"jsonrpc", "2.0"},
                    {"method", "progress"},
                    {"params", json{{"done", done}, {"total", total}, {"percent", pct}}}
                });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    bool ok = (nEvents >= 0) ? e->run(nEvents) : e->run();

    stop.store(true, std::memory_order_relaxed);
    poller.join();

    // Final 100% progress.
    long done = SimulationEngine::EventsCompleted();
    long total = SimulationEngine::EventsTotal();
    writeLine(json{
        {"jsonrpc", "2.0"},
        {"method", "progress"},
        {"params", json{{"done", done}, {"total", total}, {"percent", 100.0}}}
    });

    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleReset(const json&) {
    auto* e = SimulationEngine::Instance();
    e->reset();
    return json{{"ok", true}};
}

json JsonRpcServer::handleSetVisualization(const json& params) {
    auto* e = SimulationEngine::Instance();
    bool on = params.value("on", false);
    e->setVisualization(on);
    return json{{"ok", true}, {"visualization", on}};
}

json JsonRpcServer::handleExportGdml(const json&) {
    auto* e = SimulationEngine::Instance();
    bool ok = e->exportGdml();
    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleExportVtk(const json& params) {
    auto* e = SimulationEngine::Instance();
    std::string file = params.value("file", "");
    bool ok = e->exportVtk(file);
    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleExportSceneVtk(const json& params) {
    auto* e = SimulationEngine::Instance();
    std::string file = params.value("file", "");
    if (file.empty()) {
        return json{{"ok", false}, {"error", "Missing 'file' parameter"}};
    }
    bool ok = e->exportSceneVtk(file);
    return json{{"ok", ok}, {"error", e->lastError()}};
}

json JsonRpcServer::handleExecuteCommand(const json& params) {
    std::string cmd = params.value("command", "");
    if (cmd.empty()) {
        return json{{"ok", false}, {"error", "Missing 'command' parameter"}};
    }
    G4UImanager* ui = G4UImanager::GetUIpointer();
    if (!ui) {
        return json{{"ok", false}, {"error", "UI manager not available"}};
    }
    G4int status = ui->ApplyCommand(cmd);
    return json{{"ok", true}, {"status", status}, {"command", cmd}};
}

json JsonRpcServer::handleSampleSource(const json& params) {
    auto* e = SimulationEngine::Instance();
    if (!e->isInitialized() || !e->runManager()) {
        return json{{"ok", false}, {"error", "Engine is not initialized"}};
    }
    auto* pga = PrimaryGeneratorAction::Instance();
    if (!pga) {
        return json{{"ok", false}, {"error", "PrimaryGeneratorAction not available"}};
    }
    std::string name = params.value("name", "");
    int n = params.value("n", 100);
    if (name.empty()) {
        return json{{"ok", false}, {"error", "Missing 'name' parameter"}};
    }
    bool found = false;
    auto samples = pga->SampleSource(name, n, &found);
    if (!found) {
        return json{{"ok", false}, {"error", "Source not found: " + name}};
    }
    if (samples.empty()) {
        return json{{"ok", false}, {"error", "Source produced no primaries — check particle name / energy / expressions"}};
    }
    json arr = json::array();
    for (const auto& s : samples) {
        json obj;
        obj["position"] = json::array({s.x, s.y, s.z});
        obj["direction"] = json::array({s.dx, s.dy, s.dz});
        obj["energy"] = s.energy;
        obj["particle"] = s.particle;
        obj["time"] = s.time;
        obj["weight"] = s.weight;
        arr.push_back(obj);
    }
    json r;
    r["ok"] = true;
    r["count"] = static_cast<long long>(samples.size());
    r["samples"] = arr;
    return r;
}

json JsonRpcServer::handleShutdown(const json&) {
    fShutdown = true;
    return json{{"ok", true}};
}

json JsonRpcServer::handleDicomLoad(const json& params) {
    std::string dir = params.value("directory", "");
    if (dir.empty()) return json{{"ok", false}, {"error", "Missing 'directory'"}};
    std::string err;
    if (!fDicom.LoadCT(dir, err)) return json{{"ok", false}, {"error", err}};
    const CTSeries& ct = fDicom.GetCT();
    return json{
        {"ok", true},
        {"nx", ct.nx}, {"ny", ct.ny}, {"nz", ct.nz},
        {"dx", ct.dxMm}, {"dy", ct.dyMm}, {"dz", ct.dzMm},
        {"origin", {ct.originMm[0], ct.originMm[1], ct.originMm[2]}},
        {"direction", {ct.direction[0], ct.direction[1], ct.direction[2],
                        ct.direction[3], ct.direction[4], ct.direction[5],
                        ct.direction[6], ct.direction[7], ct.direction[8]}},
        {"tilted", ct.tilted}, {"tiltAngleDeg", ct.tiltAngleDeg},
        {"rescaleSlope", ct.rescaleSlope},
        {"rescaleIntercept", ct.rescaleIntercept},
        {"modality", ct.modality},
        {"patientName", ct.patientName}, {"patientId", ct.patientId},
        {"studyId", ct.studyId}, {"seriesNumber", ct.seriesNumber},
        {"institutionName", ct.institutionName},
        {"acquisitionDate", ct.acquisitionDate}, {"acquisitionTime", ct.acquisitionTime},
        {"valueType", ct.floatData ? "float32" : "int16"},
    };
}

json JsonRpcServer::handleVolumeLoad(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    std::string err;
    if (!fDicom.LoadVolumeFile(file, err)) return json{{"ok", false}, {"error", err}};
    const CTSeries& ct = fDicom.GetCT();
    return json{
        {"ok", true},
        {"nx", ct.nx}, {"ny", ct.ny}, {"nz", ct.nz},
        {"dx", ct.dxMm}, {"dy", ct.dyMm}, {"dz", ct.dzMm},
        {"origin", {ct.originMm[0], ct.originMm[1], ct.originMm[2]}},
        {"direction", {ct.direction[0], ct.direction[1], ct.direction[2],
                        ct.direction[3], ct.direction[4], ct.direction[5],
                        ct.direction[6], ct.direction[7], ct.direction[8]}},
        {"tilted", ct.tilted}, {"tiltAngleDeg", ct.tiltAngleDeg},
        {"rescaleSlope", ct.rescaleSlope},
        {"rescaleIntercept", ct.rescaleIntercept},
        {"modality", ct.modality},
        {"valueType", ct.floatData ? "float32" : "int16"},
    };
}

json JsonRpcServer::handleDicomSlice(const json& params) {
    int axis = params.value("axis", 2);    // 0=sagittal, 1=coronal, 2=axial
    int index = params.value("index", 0);
    std::vector<int16_t> data;
    int w = 0, h = 0;
    if (!fDicom.GetSlice(axis, index, data, w, h))
        return json{{"ok", false}, {"error", "Slice out of range or CT not loaded"}};
    return json{
        {"ok", true},
        {"axis", axis}, {"index", index},
        {"w", w}, {"h", h},
        {"data", data},  // int16 HU values, row-major
    };
}

json JsonRpcServer::handleDicomVolume(const json&) {
    if (!fDicom.IsLoaded())
        return json{{"ok", false}, {"error", "CT not loaded"}};
    const CTSeries& ct = fDicom.GetCT();
    return json{
        {"ok", true},
        {"nx", ct.nx}, {"ny", ct.ny}, {"nz", ct.nz},
        {"dx", ct.dxMm}, {"dy", ct.dyMm}, {"dz", ct.dzMm},
        {"origin", {ct.originMm[0], ct.originMm[1], ct.originMm[2]}},
        {"direction", {ct.direction[0], ct.direction[1], ct.direction[2],
                        ct.direction[3], ct.direction[4], ct.direction[5],
                        ct.direction[6], ct.direction[7], ct.direction[8]}},
        {"tilted", ct.tilted}, {"tiltAngleDeg", ct.tiltAngleDeg},
        {"rescaleSlope", ct.rescaleSlope},
        {"rescaleIntercept", ct.rescaleIntercept},
        {"modality", ct.modality},
        {"valueType", ct.floatData ? "float32" : "int16"},
        // Raw little-endian voxels (iz*(nx*ny)+iy*nx+ix), base64 (int16 or float32).
        {"data", ct.floatData ? base64EncodeFloat(ct.floatValues) : base64EncodeInt16(ct.hu)},
    };
}

json JsonRpcServer::handleDicomRtStructLoad(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    std::string err;
    if (!fDicom.LoadRTStruct(file, err)) return json{{"ok", false}, {"error", err}};
    const RTStruct& rs = fDicom.GetRTStruct();
    json rois = json::array();
    for (const auto& roi : rs.rois) {
        json contours = json::array();
        for (const auto& c : roi.contours) {
            contours.push_back(json{
                {"x", c.x}, {"y", c.y}, {"z", c.z},
            });
        }
        rois.push_back(json{
            {"number", roi.number}, {"name", roi.name}, {"type", roi.type},
            {"contours", contours},
        });
    }
    return json{{"ok", true}, {"rois", rois}};
}

json JsonRpcServer::handleDicomRtStructSave(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    RTStruct rs;
    if (params.contains("rois") && params["rois"].is_array()) {
        for (const auto& r : params["rois"]) {
            RTStruct::ROI roi;
            roi.number = r.value("number", 0);
            roi.name = r.value("name", "");
            roi.type = r.value("type", "ORGAN");
            if (r.contains("contours") && r["contours"].is_array()) {
                for (const auto& c : r["contours"]) {
                    RTStruct::Contour ct;
                    if (c.contains("x") && c["x"].is_array()) for (const auto& v : c["x"]) ct.x.push_back(v.get<double>());
                    if (c.contains("y") && c["y"].is_array()) for (const auto& v : c["y"]) ct.y.push_back(v.get<double>());
                    if (c.contains("z") && c["z"].is_array()) for (const auto& v : c["z"]) ct.z.push_back(v.get<double>());
                    roi.contours.push_back(std::move(ct));
                }
            }
            rs.rois.push_back(std::move(roi));
        }
    }
    std::string err;
    if (!fDicom.SaveRTStruct(rs, file, err)) return json{{"ok", false}, {"error", err}};
    return json{{"ok", true}, {"file", file}};
}


json JsonRpcServer::handleDicomTransfer(const json& params) {
    DicomService::TransferRequest req;
    req.mode = params.value("mode", 0);
    req.sliceFrom = params.value("sliceFrom", 0);
    req.sliceTo = params.value("sliceTo", 0);
    if (params.contains("volumes") && params["volumes"].is_array()) {
        for (const auto& v : params["volumes"]) {
            DicomService::TransferVolume vol;
            vol.name = v.value("name", "");
            vol.material = v.value("material", "");
            const size_t len = v.value("len", 0);
            if (v.contains("rle") && v["rle"].is_array() && len > 0) {
                std::vector<uint8_t> mask(len, 0);
                size_t idx = 0; int cur = 0;
                for (const auto& r : v["rle"]) {
                    int run = r.get<int>();
                    if (cur) for (size_t k = 0; k < static_cast<size_t>(run) && idx + k < len; ++k) mask[idx + k] = 1;
                    idx += static_cast<size_t>(run);
                    cur = 1 - cur;
                }
                vol.mask = std::move(mask);
            }
            req.volumes.push_back(std::move(vol));
        }
    }
    DicomService::TransferResult res;
    if (!fDicom.TransferSelection(req, res)) return json{{"ok", false}, {"error", res.error}};
    {
        CTSeries cropped;
        if (fDicom.GetCroppedCT(cropped)) {
            if (params.contains("position") && params["position"].is_object()) {
                const double pos[3] = {
                    params["position"].value("x", 0.0),
                    params["position"].value("y", 0.0),
                    params["position"].value("z", 0.0),
                };
                fDicom.SetWorldOffset(cropped, pos);
            } else {
                fDicom.UpdateWorldOffset(cropped);
            }
        }
    }
    // Persist the masks (RLE) so the config-driven phantom build can read them
    // after the engine restarts for a run.
    {
        json mv = json::array();
        if (params.contains("volumes") && params["volumes"].is_array()) {
            for (const auto& v : params["volumes"]) {
                mv.push_back(json{{"name", v.value("name", "")}, {"material", v.value("material", "")}, {"len", v.value("len", 0)}, {"rle", v.value("rle", json::array())}});
            }
        }
        std::ofstream mofs("/tmp/g4care_transfer_masks.json");
        mofs << json{{"sliceFrom", req.sliceFrom}, {"sliceTo", req.sliceTo}, {"volumes", mv}}.dump();
        mofs.close();
    }
    json volumes = json::array();
    for (const auto& v : res.volumes) {
        json groups = json::array();
        for (const auto& g : v.groups) groups.push_back(json{{"material", g.material}, {"voxelCount", g.voxelCount}});
        volumes.push_back(json{
            {"name", v.name}, {"material", v.material}, {"totalVoxels", v.totalVoxels},
            {"groups", groups},
        });
    }
    return json{
        {"ok", true},
        {"ctDir", fDicom.GetCtDir()},
        {"nx", res.nx}, {"ny", res.ny}, {"nz", res.nz},
        {"dx", res.dx}, {"dy", res.dy}, {"dz", res.dz},
        {"origin", res.origin},
        {"volumes", volumes},
    };
}


json JsonRpcServer::handleDicomTransferMeshes(const json& params) {
    // The Geometry viewer can move the whole phantom: update the world offset
    // from an explicit position before (re)building the transfer meshes.
    if (params.contains("position") && params["position"].is_object()) {
        CTSeries ct;
        if (fDicom.GetCroppedCT(ct)) {
            const double pos[3] = {
                params["position"].value("x", 0.0),
                params["position"].value("y", 0.0),
                params["position"].value("z", 0.0),
            };
            fDicom.SetWorldOffset(ct, pos);
        }
    }
    std::vector<DicomService::TransferMesh> meshes;
    if (!fDicom.BuildTransferMeshes(meshes)) return json{{"ok", false}, {"error", "No transfer or CT"}};
    json arr = json::array();
    for (const auto& m : meshes) {
        arr.push_back(json{
            {"name", m.name}, {"material", m.material},
            {"color", m.color}, {"vertices", m.vertices}, {"triangles", m.triangles},
        });
    }
    std::string phantom = fDicom.GetCT().patientName;
    if (!fDicom.GetCT().seriesNumber.empty()) {
        if (!phantom.empty()) phantom += " · ";
        phantom += "Серия " + fDicom.GetCT().seriesNumber;
    }
    if (phantom.empty()) phantom = "Фантом";
    json doc = json{{"phantom", phantom}, {"meshes", arr}};
    std::ofstream ofs("/tmp/g4care_transfer_meshes.json");
    if (!ofs) return json{{"ok", false}, {"error", "Cannot write /tmp/g4care_transfer_meshes.json"}};
    ofs << doc.dump();
    ofs.close();
    return json{{"ok", true}, {"file", "/tmp/g4care_transfer_meshes.json"}, {"meshCount", meshes.size()}, {"phantom", phantom}};
}

json JsonRpcServer::handleDicomRtDoseLoad(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    std::string err;
    if (!fDicom.LoadRTDose(file, err)) return json{{"ok", false}, {"error", err}};
    const RTDose& d = fDicom.GetRTDose();
    // Encode the dose grid as base64 float32 bytes (like dicom.volume): a full
    // 512x512xN grid passed as a JSON array would overflow the JS heap.
    std::vector<float> f32(d.doseGy.begin(), d.doseGy.end());
    return json{
        {"ok", true},
        {"nx", d.nx}, {"ny", d.ny}, {"nz", d.nz},
        {"dx", d.dxMm}, {"dy", d.dyMm}, {"dz", d.dzMm},
        {"origin", {d.originMm[0], d.originMm[1], d.originMm[2]}},
        {"direction", {d.direction[0], d.direction[1], d.direction[2],
                        d.direction[3], d.direction[4], d.direction[5],
                        d.direction[6], d.direction[7], d.direction[8]}},
        {"doseGridScaling", d.doseGridScaling},
        {"doseUnits", d.doseUnits},
        {"valueType", "float32"},
        {"data", base64EncodeFloat(f32)},
    };
}

json JsonRpcServer::handleDicomRtPlanLoad(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    std::string err;
    if (!fDicom.LoadRTPlan(file, err)) return json{{"ok", false}, {"error", err}};
    const RTPlan& p = fDicom.GetRTPlan();
    json beams = json::array();
    for (const auto& b : p.beams) {
        json cps = json::array();
        for (const auto& cp : b.controlPoints) {
            cps.push_back(json{
                {"energyMeV", cp.energyMeV},
                {"gantryAngleDeg", cp.gantryAngleDeg},
                {"couchAngleDeg", cp.couchAngleDeg},
                {"isocenter", {cp.isocenter[0], cp.isocenter[1], cp.isocenter[2]}},
            });
        }
        beams.push_back(json{
            {"number", b.number}, {"name", b.name},
            {"radiationType", b.radiationType}, {"sadMm", b.sadMm},
            {"controlPoints", cps},
        });
    }
    return json{
        {"ok", true},
        {"label", p.label},
        {"prescriptionDoseCGy", p.prescriptionDoseCGy},
        {"beams", beams},
    };
}

json JsonRpcServer::handleDicomRtPlanSave(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    RTPlan plan;
    plan.label = params.value("label", "G4CARE Plan");
    plan.prescriptionDoseCGy = params.value("prescriptionDoseCGy", 0.0);
    if (params.contains("beams") && params["beams"].is_array()) {
        for (const auto& b : params["beams"]) {
            RTPlan::Beam beam;
            beam.number = b.value("number", 0);
            beam.name = b.value("name", "");
            beam.radiationType = b.value("radiationType", "");
            if (beam.radiationType.empty()) {
                beam.radiationType = RTPlan::RadiationTypeFromParticle(b.value("particle", ""));
            }
            if (b.contains("controlPoints") && b["controlPoints"].is_array()) {
                for (const auto& cp : b["controlPoints"]) {
                    RTPlan::ControlPoint c;
                    c.energyMeV = cp.value("energyMeV", 0.0);
                    c.gantryAngleDeg = cp.value("gantryAngleDeg", 0.0);
                    c.couchAngleDeg = cp.value("couchAngleDeg", 0.0);
                    if (cp.contains("isocenter") && cp["isocenter"].is_array()
                        && cp["isocenter"].size() >= 3) {
                        for (int j = 0; j < 3; ++j) c.isocenter[j] = cp["isocenter"][j].get<double>();
                        for (int j = 0; j < 3; ++j) c.isocenter[j] += fDicom.GetWorldOffset()[j];
                    }
                    beam.controlPoints.push_back(std::move(c));
                }
            }
            plan.beams.push_back(std::move(beam));
        }
    }
    std::string err;
    if (!fDicom.SaveRTPlan(plan, file, err)) return json{{"ok", false}, {"error", err}};
    return json{{"ok", true}, {"file", file}};
}

json JsonRpcServer::handleDicomRtDoseSave(const json& params) {
    std::string file = params.value("file", "");
    if (file.empty()) return json{{"ok", false}, {"error", "Missing 'file'"}};
    RTDose dose;
    const bool simulated = DoseScorer::Instance()->BuildRTDose(dose);
    if (!simulated) {
        // No simulated dose yet: fall back to the reference RTDOSE loaded in the
        // DICOM tab so the DICOM->DICOM round-trip works without a simulation.
        const RTDose& ref = fDicom.GetRTDose();
        if (ref.valid && !ref.doseGy.empty()) {
            dose = ref;
        } else {
            return json{{"ok", false}, {"error", "No dose grid available (load an RTDOSE reference or run a simulation first)"}};
        }
    }
    if (simulated) {
        // Convert the world-frame dose grid back to absolute DICOM LPS.
        const double* off = fDicom.GetWorldOffset();
        for (int i = 0; i < 3; ++i) dose.originMm[i] += off[i];
    }
    std::string err;
    if (!fDicom.SaveRTDose(dose, file, err)) return json{{"ok", false}, {"error", err}};
    return json{{"ok", true}, {"file", file}, {"nx", dose.nx}, {"ny", dose.ny}, {"nz", dose.nz}};
}

