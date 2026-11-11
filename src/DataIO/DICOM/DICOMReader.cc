//==============================================================================
//
// G4CARE
//
// @file    DICOMReader.cc
// @brief   Implementation of DICOMReader — DICOM import facade.
//
// @details
//   Placeholder implementation for Phase 0.  The GDCM-based bodies are added
//   in subsequent phases behind the G4CARE_HAS_DICOM guard; until then every
//   method reports failure so callers can detect missing GDCM support.
//
// @author  G4CARE Developers
// @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//
//==============================================================================

#include "DICOMReader.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <utility>
#include <vector>

#ifdef G4CARE_HAS_DICOM
#include "gdcmAttribute.h"
#include "gdcmDirectory.h"
#include "gdcmImage.h"
#include "gdcmImageReader.h"
#include "gdcmIPPSorter.h"
#include "gdcmItem.h"
#include "gdcmReader.h"
#include "gdcmSequenceOfItems.h"
#include "gdcmStringFilter.h"
#include "gdcmTag.h"
#endif

bool DICOMReader::ReadCTSeries(const std::string& directory, CTSeries& out) {
#ifdef G4CARE_HAS_DICOM
    using namespace gdcm;

    // Verify the directory exists before handing it to GDCM: gdcm::Directory
    // asserts in GetFilenames() if Load() never populated Toplevel (e.g. the
    // path does not exist), which would abort the whole process.
    struct stat st;
    if (stat(directory.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::cerr << "[DICOMReader] CT directory not found: " << directory << std::endl;
        return false;
    }

    gdcm::Directory dir;
    unsigned int nfiles = dir.Load(directory.c_str(), true);  // recursive scan
    if (nfiles == 0) {
        std::cerr << "[DICOMReader] No DICOM files in: " << directory << std::endl;
        return false;
    }
    std::vector<std::string> files = dir.GetFilenames();
    // Filter out non-DICOM files (e.g. CITATION.txt bundled with IDC/TCIA data).
    std::vector<std::string> dicomFiles;
    for (const auto& f : files) {
        if (f.size() >= 4) {
            std::string ext = f.substr(f.size() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext == ".dcm") dicomFiles.push_back(f);
        }
    }
    files = dicomFiles;
    if (files.empty()) {
        std::cerr << "[DICOMReader] No DICOM files in: " << directory << std::endl;
        return false;
    }

    gdcm::IPPSorter sorter;
    sorter.SetComputeZSpacing(true);
    sorter.SetZSpacingTolerance(1e-3);
    sorter.SetDropDuplicatePositions(true);
    // A single file may be a multi-frame DICOM (or a lone slice); sort by
    // ImagePositionPatient only when there are multiple files.
    if (files.size() > 1) {
        if (!sorter.Sort(files)) {
            std::cerr << "[DICOMReader] IPPSorter failed." << std::endl;
            return false;
        }
        files = sorter.GetFilenames();
    }

    gdcm::ImageReader first;
    first.SetFileName(files.front().c_str());
    if (!first.Read()) {
        std::cerr << "[DICOMReader] Cannot read first slice." << std::endl;
        return false;
    }
    const gdcm::Image& img0 = first.GetImage();

    const unsigned int* dims = img0.GetDimensions();
    out.nx = static_cast<int>(dims[0]);
    out.ny = static_cast<int>(dims[1]);
    const int nFramesInFile = (dims[2] > 0) ? static_cast<int>(dims[2]) : 1;
    const bool multiFrame = (nFramesInFile > 1 && files.size() == 1);
    out.nz = multiFrame ? nFramesInFile : static_cast<int>(files.size());

    const double* spacing = img0.GetSpacing();
    out.dxMm = spacing[0];
    out.dyMm = spacing[1];
    out.dzMm = spacing[2];
    if (sorter.GetZSpacing() > 0.0) out.dzMm = sorter.GetZSpacing();

    const double* origin = img0.GetOrigin();
    const double* dc = img0.GetDirectionCosines();
    // gdcm returns DICOM IOP order: dc[0..2] = first row = direction of the
    // column index (ix), dc[3..5] = first column = direction of the row index (iy).
    // CTSeries stores the layout used by the rest of the codebase:
    // direction[0..2] = row (iy), direction[3..5] = column (ix).
    out.direction[0] = dc[3]; out.direction[1] = dc[4]; out.direction[2] = dc[5];
    out.direction[3] = dc[0]; out.direction[4] = dc[1]; out.direction[5] = dc[2];

    // DICOM ImagePositionPatient (GetOrigin) is the centre of the first voxel;
    // CTSeries.originMm stores the corner of voxel (0,0,0), so subtract half a
    // voxel along each lattice axis (column/row/slice).
    {
        const double col[3] = {dc[0], dc[1], dc[2]};
        const double row[3] = {dc[3], dc[4], dc[5]};
        const double slc[3] = {
            col[1] * row[2] - col[2] * row[1],
            col[2] * row[0] - col[0] * row[2],
            col[0] * row[1] - col[1] * row[0],
        };
        for (int i = 0; i < 3; ++i) {
            out.originMm[i] = origin[i]
                - 0.5 * (out.dxMm * col[i] + out.dyMm * row[i] + out.dzMm * slc[i]);
        }
    }

    // Slice normal = column x row (the image z axis), stored as direction[6..8].
    {
        const double* r = out.direction;      // row (iy)
        const double* c = out.direction + 3;  // column (ix)
        out.direction[6] = c[1] * r[2] - c[2] * r[1];
        out.direction[7] = c[2] * r[0] - c[0] * r[2];
        out.direction[8] = c[0] * r[1] - c[1] * r[0];
    }

    // Gantry tilt / obliquity detection: the slice normal must be axis-aligned
    // for the regular voxel grid built later.
    {
        const double n[3] = {out.direction[6], out.direction[7], out.direction[8]};
        const double nn = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nn > 0.0) {
            const double m = std::max(std::max(std::fabs(n[0]), std::fabs(n[1])),
                                      std::fabs(n[2]));
            out.tiltAngleDeg = std::acos(std::min(1.0, m / nn)) * 180.0 / std::acos(-1.0);
            out.tilted = out.tiltAngleDeg > 0.5;
            if (out.tilted) {
                std::cout << "[DICOMReader] Gantry tilt / oblique acquisition detected: "
                          << out.tiltAngleDeg << " deg." << std::endl;
            }
        }
    }

    out.rescaleSlope = img0.GetSlope();
    out.rescaleIntercept = img0.GetIntercept();

    gdcm::StringFilter sf;
    sf.SetFile(first.GetFile());
    out.modality = sf.ToString(gdcm::Tag(0x0008, 0x0060));
    out.patientName = sf.ToString(gdcm::Tag(0x0010, 0x0010));
    out.patientId = sf.ToString(gdcm::Tag(0x0010, 0x0020));
    out.studyId = sf.ToString(gdcm::Tag(0x0020, 0x0010));
    out.seriesNumber = sf.ToString(gdcm::Tag(0x0020, 0x0011));
    out.institutionName = sf.ToString(gdcm::Tag(0x0008, 0x0080));
    out.acquisitionDate = sf.ToString(gdcm::Tag(0x0008, 0x0022));
    out.acquisitionTime = sf.ToString(gdcm::Tag(0x0008, 0x0032));
    out.studyInstanceUID = sf.ToString(gdcm::Tag(0x0020, 0x000D));
    out.seriesInstanceUID = sf.ToString(gdcm::Tag(0x0020, 0x000E));
    out.frameOfReferenceUID = sf.ToString(gdcm::Tag(0x0020, 0x0052));
    out.sopClassUID = sf.ToString(gdcm::Tag(0x0008, 0x0016));

    const gdcm::PixelFormat& pf = img0.GetPixelFormat();
    const unsigned short bitsAllocated = pf.GetBitsAllocated();
    const unsigned short bitsStored = pf.GetBitsStored();
    const unsigned short pixelRep = pf.GetPixelRepresentation();
    const gdcm::PixelFormat::ScalarType scalarType = pf.GetScalarType();
    if (pf.GetSamplesPerPixel() != 1) {
        std::cerr << "[DICOMReader] Unsupported pixel format (samples != 1)." << std::endl;
        return false;
    }
    const bool isFloatPixel = (scalarType == gdcm::PixelFormat::FLOAT32 ||
                               scalarType == gdcm::PixelFormat::FLOAT64);
    if (!isFloatPixel && bitsAllocated != 16 && bitsAllocated != 8) {
        std::cerr << "[DICOMReader] Unsupported pixel format." << std::endl;
        return false;
    }

    // Fractional rescale slope (e.g. PET: SUV = 0.11771 * raw) must be kept as
    // float so the fractional part is not truncated to int16.
    const double slope = out.rescaleSlope;
    out.floatData = isFloatPixel || (std::floor(slope) != slope);

    const std::size_t plane = static_cast<std::size_t>(out.nx) * out.ny;
    if (out.floatData) out.floatValues.resize(plane * out.nz);
    else out.hu.resize(plane * out.nz);

    // Fill one slice `z` from `frame` raw bytes.
    auto fillSlice = [&](int z, const char* frame) {
        const std::size_t base = static_cast<std::size_t>(z) * plane;
        if (isFloatPixel) {
            if (scalarType == gdcm::PixelFormat::FLOAT32) {
                const float* p = reinterpret_cast<const float*>(frame);
                for (std::size_t i = 0; i < plane; ++i)
                    out.floatValues[base + i] = static_cast<float>(slope * p[i] + out.rescaleIntercept);
            } else {
                const double* p = reinterpret_cast<const double*>(frame);
                for (std::size_t i = 0; i < plane; ++i)
                    out.floatValues[base + i] = static_cast<float>(slope * p[i] + out.rescaleIntercept);
            }
            return;
        }
        const std::uint16_t mask = (bitsStored >= 16)
            ? static_cast<std::uint16_t>(0xFFFFu)
            : static_cast<std::uint16_t>((1u << bitsStored) - 1u);
        if (bitsAllocated == 16) {
            const std::uint16_t* raw = reinterpret_cast<const std::uint16_t*>(frame);
            for (std::size_t i = 0; i < plane; ++i) {
                std::uint16_t v = raw[i] & mask;
                if (pixelRep == 1 && bitsStored >= 1 && (v & (1u << (bitsStored - 1)))) {
                    v |= static_cast<std::uint16_t>(~mask);
                }
                const double val = (pixelRep == 1) ? static_cast<std::int16_t>(v)
                                                   : static_cast<int>(v);
                const double rescaled = slope * val + out.rescaleIntercept;
                if (out.floatData) out.floatValues[base + i] = static_cast<float>(rescaled);
                else out.hu[base + i] = static_cast<short>(rescaled);
            }
        } else {
            const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(frame);
            for (std::size_t i = 0; i < plane; ++i) {
                const int val = (pixelRep == 1) ? static_cast<std::int8_t>(raw[i])
                                                : static_cast<int>(raw[i]);
                const double rescaled = slope * val + out.rescaleIntercept;
                if (out.floatData) out.floatValues[base + i] = static_cast<float>(rescaled);
                else out.hu[base + i] = static_cast<short>(rescaled);
            }
        }
    };

    if (multiFrame) {
        gdcm::ImageReader r;
        r.SetFileName(files[0].c_str());
        if (!r.Read()) return false;
        out.sliceSopInstanceUIDs.assign(1, sf.ToString(gdcm::Tag(0x0008, 0x0018)));
        const gdcm::Image& img = r.GetImage();
        std::vector<char> buf(img.GetBufferLength());
        if (!img.GetBuffer(buf.data())) return false;
        const std::size_t bytesPerFrame =
            plane * (isFloatPixel ? (scalarType == gdcm::PixelFormat::FLOAT32 ? 4u : 8u)
                                  : (bitsAllocated / 8u));
        for (int z = 0; z < out.nz; ++z) fillSlice(z, buf.data() + z * bytesPerFrame);
    } else {
        out.sliceSopInstanceUIDs.clear();
        for (int z = 0; z < out.nz; ++z) {
            gdcm::ImageReader r;
            r.SetFileName(files[z].c_str());
            if (!r.Read()) return false;
            gdcm::StringFilter sfz;
            sfz.SetFile(r.GetFile());
            out.sliceSopInstanceUIDs.push_back(sfz.ToString(gdcm::Tag(0x0008, 0x0018)));
            const gdcm::Image& img = r.GetImage();
            std::vector<char> buf(img.GetBufferLength());
            if (!img.GetBuffer(buf.data())) return false;
            fillSlice(z, buf.data());
        }
    }

    out.valid = true;
    std::cout << "[DICOMReader] CT series: " << out.nx << "x" << out.ny << "x" << out.nz
              << " voxels, spacing " << out.dxMm << "/" << out.dyMm << "/" << out.dzMm
              << " mm, modality " << out.modality << std::endl;
    return true;
#else
    (void)directory;
    (void)out;
    std::cerr << "[DICOMReader] DICOM disabled (no GDCM)." << std::endl;
    return false;
#endif
}

//==============================================================================
// ReadRTStruct — RTSTRUCT -> organ contours (Phase 3).
//==============================================================================
bool DICOMReader::ReadRTStruct(const std::string& file, RTStruct& out) {
#ifdef G4CARE_HAS_DICOM
    using namespace gdcm;

    gdcm::Reader reader;
    reader.SetFileName(file.c_str());
    if (!reader.Read()) {
        std::cerr << "[DICOMReader] Cannot read RTSTRUCT: " << file << std::endl;
        return false;
    }
    const gdcm::DataSet& ds = reader.GetFile().GetDataSet();

    out = RTStruct{};
    std::map<int, int> numberToIdx;

    // StructureSetROISequence (3006,0020): ROINumber -> ROIName.
    const gdcm::Tag tROISeq(0x3006, 0x0020);
    if (ds.FindDataElement(tROISeq)) {
        gdcm::SmartPointer<gdcm::SequenceOfItems> seq =
            ds.GetDataElement(tROISeq).GetValueAsSQ();
        if (seq) {
            for (size_t i = 1; i <= seq->GetNumberOfItems(); ++i) {
                const gdcm::DataSet& ids = seq->GetItem(i).GetNestedDataSet();
                gdcm::Attribute<0x3006, 0x0022> num; num.SetFromDataSet(ids);
                gdcm::Attribute<0x3006, 0x0026> name; name.SetFromDataSet(ids);
                gdcm::Attribute<0x3006, 0x00A4> type; type.SetFromDataSet(ids);
                RTStruct::ROI roi;
                roi.number = num.GetValue();
                roi.name = name.GetValue();
                roi.type = type.GetValue();
                numberToIdx[roi.number] = static_cast<int>(out.rois.size());
                out.rois.push_back(std::move(roi));
            }
        }
    }

    // ROIContourSequence (3006,0039): contours per ROI.
    const gdcm::Tag tROIContourSeq(0x3006, 0x0039);
    if (ds.FindDataElement(tROIContourSeq)) {
        gdcm::SmartPointer<gdcm::SequenceOfItems> seq =
            ds.GetDataElement(tROIContourSeq).GetValueAsSQ();
        if (seq) {
            for (size_t i = 1; i <= seq->GetNumberOfItems(); ++i) {
                const gdcm::DataSet& ids = seq->GetItem(i).GetNestedDataSet();
                gdcm::Attribute<0x3006, 0x0084> refROI; refROI.SetFromDataSet(ids);
                const int roiNumber = refROI.GetValue();
                auto it = numberToIdx.find(roiNumber);
                if (it == numberToIdx.end()) continue;
                RTStruct::ROI& roi = out.rois[it->second];

                const gdcm::Tag tContourSeq(0x3006, 0x0040);
                if (!ids.FindDataElement(tContourSeq)) continue;
                gdcm::SmartPointer<gdcm::SequenceOfItems> cseq =
                    ids.GetDataElement(tContourSeq).GetValueAsSQ();
                if (!cseq) continue;
                for (size_t k = 1; k <= cseq->GetNumberOfItems(); ++k) {
                    const gdcm::DataSet& cds = cseq->GetItem(k).GetNestedDataSet();
                    gdcm::Attribute<0x3006, 0x0050> data; data.SetFromDataSet(cds);
                    const unsigned int n = data.GetNumberOfValues();
                    if (n == 0 || n % 3 != 0) continue;
                    const double* v = data.GetValues();
                    RTStruct::Contour c;
                    for (unsigned int p = 0; p < n; p += 3) {
                        c.x.push_back(v[p]);
                        c.y.push_back(v[p + 1]);
                        c.z.push_back(v[p + 2]);
                    }
                    roi.contours.push_back(std::move(c));
                }
            }
        }
    }

    out.valid = true;
    std::cout << "[DICOMReader] RTSTRUCT: " << out.rois.size() << " ROIs" << std::endl;
    return true;
#else
    (void)file;
    (void)out;
    std::cerr << "[DICOMReader] DICOM disabled (no GDCM)." << std::endl;
    return false;
#endif
}

//==============================================================================
// ReadRTPlan — RTPLAN -> treatment beams (Phase 4).
//==============================================================================
bool DICOMReader::ReadRTPlan(const std::string& file, RTPlan& out) {
#ifdef G4CARE_HAS_DICOM
    using namespace gdcm;

    gdcm::Reader reader;
    reader.SetFileName(file.c_str());
    if (!reader.Read()) {
        std::cerr << "[DICOMReader] Cannot read RTPLAN: " << file << std::endl;
        return false;
    }
    const gdcm::DataSet& ds = reader.GetFile().GetDataSet();

    out = RTPlan{};

    // Plan label + prescription dose (prefer the SITE DoseReference, fall back
    // to the first item).
    gdcm::Attribute<0x300A, 0x0002> planLabel; planLabel.SetFromDataSet(ds);
    out.label = planLabel.GetValue();
    const gdcm::Tag tDoseRefSeq(0x300A, 0x0010);
    if (ds.FindDataElement(tDoseRefSeq)) {
        gdcm::SmartPointer<gdcm::SequenceOfItems> seq =
            ds.GetDataElement(tDoseRefSeq).GetValueAsSQ();
        if (seq) {
            double firstRx = 0.0; bool haveFirst = false;
            double siteRx = 0.0; bool haveSite = false;
            for (size_t i = 1; i <= seq->GetNumberOfItems(); ++i) {
                const gdcm::DataSet& rds = seq->GetItem(i).GetNestedDataSet();
                gdcm::Attribute<0x300A, 0x0026> rx; rx.SetFromDataSet(rds);
                if (!haveFirst) { firstRx = rx.GetValue(); haveFirst = true; }
                gdcm::Attribute<0x300A, 0x0014> st; st.SetFromDataSet(rds);
                if (st.GetValue() == "SITE") { siteRx = rx.GetValue(); haveSite = true; break; }
            }
            out.prescriptionDoseCGy = haveSite ? siteRx : firstRx;
        }
    }

    // BeamSequence (300A,00B0).
    const gdcm::Tag tBeamSeq(0x300A, 0x00B0);
    if (!ds.FindDataElement(tBeamSeq)) {
        std::cerr << "[DICOMReader] No BeamSequence in RTPLAN." << std::endl;
        return false;
    }
    gdcm::SmartPointer<gdcm::SequenceOfItems> beamSeq =
        ds.GetDataElement(tBeamSeq).GetValueAsSQ();
    if (!beamSeq) return false;

    for (size_t i = 1; i <= beamSeq->GetNumberOfItems(); ++i) {
        const gdcm::DataSet& bds = beamSeq->GetItem(i).GetNestedDataSet();
        RTPlan::Beam beam;
        gdcm::Attribute<0x300A, 0x00C0> num; num.SetFromDataSet(bds);
        gdcm::Attribute<0x300A, 0x00C2> name; name.SetFromDataSet(bds);
        gdcm::Attribute<0x300A, 0x00C6> rtype; rtype.SetFromDataSet(bds);
        beam.number = num.GetValue();
        beam.name = name.GetValue();
        beam.radiationType = rtype.GetValue();
        gdcm::Attribute<0x300A, 0x00B4> sad; sad.SetFromDataSet(bds);
        if (sad.GetValue() > 0.0) beam.sadMm = sad.GetValue();

        // ControlPointSequence (300A,0111).
        const gdcm::Tag tCpSeq(0x300A, 0x0111);
        if (bds.FindDataElement(tCpSeq)) {
            gdcm::SmartPointer<gdcm::SequenceOfItems> cpSeq =
                bds.GetDataElement(tCpSeq).GetValueAsSQ();
            if (cpSeq) {
                for (size_t k = 1; k <= cpSeq->GetNumberOfItems(); ++k) {
                    const gdcm::DataSet& cds = cpSeq->GetItem(k).GetNestedDataSet();
                    RTPlan::ControlPoint cp;
                    gdcm::Attribute<0x300A, 0x0114> en; en.SetFromDataSet(cds);
                    gdcm::Attribute<0x300A, 0x011E> ga; ga.SetFromDataSet(cds);
                    gdcm::Attribute<0x300A, 0x0122> ca; ca.SetFromDataSet(cds);
                    gdcm::Attribute<0x300A, 0x012C> iso; iso.SetFromDataSet(cds);
                    cp.energyMeV = en.GetValue();
                    cp.gantryAngleDeg = ga.GetValue();
                    cp.couchAngleDeg = ca.GetValue();
                    if (iso.GetNumberOfValues() >= 3) {
                        for (int j = 0; j < 3; ++j) cp.isocenter[j] = iso.GetValue(j);
                    }
                    beam.controlPoints.push_back(std::move(cp));
                }
            }
        }
        out.beams.push_back(std::move(beam));
    }

    out.valid = !out.beams.empty();
    std::cout << "[DICOMReader] RTPLAN: " << out.beams.size() << " beams"
              << " label='" << out.label << "' Rx=" << out.prescriptionDoseCGy << " cGy"
              << std::endl;
    return true;
#else
    (void)file;
    (void)out;
    std::cerr << "[DICOMReader] DICOM disabled (no GDCM)." << std::endl;
    return false;
#endif
}

//==============================================================================
// ReadRTDose — RTDOSE -> reference dose grid (Phase 5).
//==============================================================================
bool DICOMReader::ReadRTDose(const std::string& file, RTDose& out) {
#ifdef G4CARE_HAS_DICOM
    using namespace gdcm;

    out = RTDose{};

    gdcm::ImageReader reader;
    reader.SetFileName(file.c_str());
    if (!reader.Read()) {
        std::cerr << "[DICOMReader] Cannot read RTDOSE: " << file << std::endl;
        return false;
    }

    // Reject any non-RTDOSE object (e.g. a plain CT slice): must be RT Dose
    // Storage (SOP Class UID 1.2.840.10008.5.1.4.1.1.481.2).
    gdcm::StringFilter sopFilter;
    sopFilter.SetFile(reader.GetFile());
    std::string sopClassUid = sopFilter.ToString(gdcm::Tag(0x0008, 0x0016));
    while (!sopClassUid.empty() &&
           (sopClassUid.back() == ' ' || sopClassUid.back() == '\0' ||
            sopClassUid.back() == '\r' || sopClassUid.back() == '\n')) {
        sopClassUid.pop_back();
    }
    if (sopClassUid != "1.2.840.10008.5.1.4.1.1.481.2") {
        std::cerr << "[DICOMReader] Not an RTDOSE (SOP class " << sopClassUid
                  << "): " << file << std::endl;
        return false;
    }

    const gdcm::Image& img = reader.GetImage();

    const unsigned int* dims = img.GetDimensions();   // [cols, rows, frames]
    out.nx = static_cast<int>(dims[0]);
    out.ny = static_cast<int>(dims[1]);
    out.nz = (dims[2] > 0) ? static_cast<int>(dims[2]) : 1;

    const double* sp = img.GetSpacing();
    out.dxMm = sp[0];
    out.dyMm = sp[1];
    out.dzMm = sp[2];

    const double* o = img.GetOrigin();
    for (int i = 0; i < 3; ++i) out.originMm[i] = o[i];

    const double* dc = img.GetDirectionCosines();
    // gdcm IOP order [first row (ix), first column (iy)] -> internal
    // [row (iy), column (ix)] convention.
    out.direction[0] = dc[3]; out.direction[1] = dc[4]; out.direction[2] = dc[5];
    out.direction[3] = dc[0]; out.direction[4] = dc[1]; out.direction[5] = dc[2];
    {
        const double* r = out.direction;      // row (iy)
        const double* c = out.direction + 3;  // column (ix)
        out.direction[6] = c[1] * r[2] - c[2] * r[1];
        out.direction[7] = c[2] * r[0] - c[0] * r[2];
        out.direction[8] = c[0] * r[1] - c[1] * r[0];
    }

    // gdcm maps DoseGridScaling (3004,000E) to the image slope; intercept = 0.
    out.doseGridScaling = img.GetSlope();

    gdcm::StringFilter sf;
    sf.SetFile(reader.GetFile());
    out.doseUnits = sf.ToString(gdcm::Tag(0x3004, 0x0002));

    const std::size_t n = static_cast<std::size_t>(out.nx) * out.ny * out.nz;
    out.doseGy.resize(n);

    std::vector<char> buf(img.GetBufferLength());
    if (!img.GetBuffer(buf.data())) return false;

    const double scale = out.doseGridScaling;
    const gdcm::PixelFormat::ScalarType st = img.GetPixelFormat().GetScalarType();

    switch (st) {
        case gdcm::PixelFormat::FLOAT32: {
            const float* p = reinterpret_cast<const float*>(buf.data());
            for (std::size_t i = 0; i < n; ++i) out.doseGy[i] = scale * p[i];
            break;
        }
        case gdcm::PixelFormat::UINT16: {
            const std::uint16_t* p = reinterpret_cast<const std::uint16_t*>(buf.data());
            for (std::size_t i = 0; i < n; ++i) out.doseGy[i] = scale * p[i];
            break;
        }
        case gdcm::PixelFormat::INT16: {
            const std::int16_t* p = reinterpret_cast<const std::int16_t*>(buf.data());
            for (std::size_t i = 0; i < n; ++i) out.doseGy[i] = scale * p[i];
            break;
        }
        case gdcm::PixelFormat::UINT32: {
            const std::uint32_t* p = reinterpret_cast<const std::uint32_t*>(buf.data());
            for (std::size_t i = 0; i < n; ++i) out.doseGy[i] = scale * p[i];
            break;
        }
        case gdcm::PixelFormat::INT32: {
            const std::int32_t* p = reinterpret_cast<const std::int32_t*>(buf.data());
            for (std::size_t i = 0; i < n; ++i) out.doseGy[i] = scale * p[i];
            break;
        }
        default:
            std::cerr << "[DICOMReader] Unsupported RTDOSE pixel type." << std::endl;
            return false;
    }

    out.valid = true;
    std::cout << "[DICOMReader] RTDOSE: " << out.nx << "x" << out.ny << "x" << out.nz
              << " scaling=" << out.doseGridScaling << " units=" << out.doseUnits
              << std::endl;
    return true;
#else
    (void)file;
    (void)out;
    std::cerr << "[DICOMReader] DICOM disabled (no GDCM)." << std::endl;
    return false;
#endif
}
