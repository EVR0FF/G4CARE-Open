//==============================================================================
// G4CARE
// @file    DicomService.hh
// @brief   DICOM slice service for the viewer (axis-aligned MPR).
//==============================================================================

#ifndef DICOM_SERVICE_HH
#define DICOM_SERVICE_HH

#include <cstdint>
#include <string>
#include <vector>

#include "CTSeries.hh"
#include "RTStruct.hh"
#include "RTDose.hh"
#include "RTPlan.hh"

/// @brief Serves axis-aligned slices (axial/coronal/sagittal) of a loaded CT
/// series to the viewer. Heavy MPR/segmentation (oblique reslice, MarchingCubes)
/// will be added on top of this once VTK is linked (-DG4CARE_WITH_VTK).
class DicomService {
public:
    static DicomService* Instance();
    static void DeleteInstance();

    bool LoadCT(const std::string& directory, std::string& err);
    bool IsLoaded() const { return fCT.valid; }
    const CTSeries& GetCT() const { return fCT; }
    /// @return directory of the last successfully loaded CT series ("" if none).
    const std::string& GetCtDir() const { return fCtDir; }

    /// @brief Compute and store the DICOM->world placement offset from the
    ///        phantom's DICOM centre and GEOMETRY.DICOM.POSITION. Returns false
    ///        when POSITION is not configured (offset stays 0).
    bool UpdateWorldOffset(const CTSeries& ct);
    /// @brief Compute and store the offset from an explicit world position of
    ///        the phantom centre (mm), used by the GUI transfer RPC.
    void SetWorldOffset(const CTSeries& ct, const double pos[3]);
    /// @brief DICOM coordinate of the world origin (mm, LPS); 0 when unset.
    const double* GetWorldOffset() const { return fWorldOffset; }

    /// Extract an axis-aligned slice (0 = sagittal/X, 1 = coronal/Y, 2 = axial/Z).
    /// `out` is a flat row-major array of int16 HU values; w/h are its dims.
    bool GetSlice(int axis, int index, std::vector<int16_t>& out, int& w, int& h) const;

    /// Load a generic volume file (NRRD / NIfTI) into the same series storage.
    bool LoadVolumeFile(const std::string& file, std::string& err);

    /// Load RT objects (planner data).
    bool LoadRTStruct(const std::string& file, std::string& err);

    /// Write the given structures to a DICOM RTSTRUCT file (GDCM).
    bool SaveRTStruct(const RTStruct& rs, const std::string& file, std::string& err);

    /// Write the given treatment plan to a DICOM RTPLAN file (GDCM).
    bool SaveRTPlan(const RTPlan& plan, const std::string& file, std::string& err);

    /// Write the given dose grid to a DICOM RTDOSE file (GDCM).
    bool SaveRTDose(const RTDose& dose, const std::string& file, std::string& err);

    /// ── GUI → core transfer ────────────────────────────────────────────────
    /// A transfer request: a slice range plus named volume masks (cropped to the
    /// same range). Each volume may override the HU-based material.
    struct TransferVolume {
        std::string name;
        std::string material;          ///< override (empty = HU-based)
        std::vector<uint8_t> mask;      ///< cropped mask (nx*ny*nz2), 1 = inside
    };
    struct TransferRequest {
        int mode = 0;                  ///< 0 = whole, 1 = volumes only, 2 = background
        int sliceFrom = 0;
        int sliceTo = 0;
        std::vector<TransferVolume> volumes;
    };
    struct TransferResult {
        bool ok = false;
        std::string error;
        int nx = 0, ny = 0, nz = 0;
        double dx = 0, dy = 0, dz = 0;
        std::vector<double> origin;    ///< LPS origin of the cropped volume
        struct VolumeInfo {
            std::string name;
            std::string material;      ///< override (empty = HU-based)
            struct Group { std::string material; size_t voxelCount = 0; };
            std::vector<Group> groups; ///< material -> voxel count
            size_t totalVoxels = 0;
        };
        std::vector<VolumeInfo> volumes;
    };

    /// Resolve per-voxel materials (HU + volume override) and build the
    /// volume -> material -> voxel hierarchy for the requested slice range.
    bool TransferSelection(const TransferRequest& req, TransferResult& out);

    /// @return true if a GUI transfer selection has been stored.
    bool HasTransfer() const { return fHasTransfer; }
    /// @return the stored transfer selection (valid only when HasTransfer()).
    const TransferRequest& GetTransferRequest() const { return fTransferRequest; }

    /// Crop the loaded CT to the transfer slice range (copies only that range).
    bool GetCroppedCT(CTSeries& out) const;

    /// A triangle mesh of one transferred volume (voxel boundary faces).
    struct TransferMesh {
        std::string name;
        std::string material;
        std::vector<double> color;     ///< 3
        std::vector<double> vertices;  ///< flat xyz (mm, LPS)
        std::vector<int> triangles;    ///< flat indices (3 per triangle)
    };
    /// Tessellate the transferred volume masks into triangle meshes.
    bool BuildTransferMeshes(std::vector<TransferMesh>& out) const;
    bool LoadRTDose(const std::string& file, std::string& err);
    bool LoadRTPlan(const std::string& file, std::string& err);
    const RTStruct& GetRTStruct() const { return fRTStruct; }
    const RTDose& GetRTDose() const { return fRTDose; }
    const RTPlan& GetRTPlan() const { return fRTPlan; }

private:
    DicomService() = default;
    DicomService(const DicomService&) = delete;
    DicomService& operator=(const DicomService&) = delete;
    static DicomService* fInstance;

    CTSeries fCT;
    std::string fCtDir;                 ///< directory of last loaded CT series
    double fWorldOffset[3] = {0.0, 0.0, 0.0};  ///< DICOM->world offset (mm)
    RTStruct fRTStruct;
    RTDose fRTDose;
    RTPlan fRTPlan;
    bool fHasTransfer = false;
    TransferRequest fTransferRequest;
};

#endif  // DICOM_SERVICE_HH
