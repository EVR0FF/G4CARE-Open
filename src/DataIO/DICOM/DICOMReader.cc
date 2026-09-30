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
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
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

    gdcm::Directory dir;
    dir.Load(directory.c_str());
    std::vector<std::string> files = dir.GetFilenames();
    if (files.empty()) {
        std::cerr << "[DICOMReader] No DICOM files in: " << directory << std::endl;
        return false;
    }

    gdcm::IPPSorter sorter;
    sorter.SetComputeZSpacing(true);
    sorter.SetZSpacingTolerance(1e-3);
    sorter.SetDropDuplicatePositions(true);
    if (!sorter.Sort(files)) {
        std::cerr << "[DICOMReader] IPPSorter failed." << std::endl;
        return false;
    }
    files = sorter.GetFilenames();

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
    out.nz = static_cast<int>(files.size());

    const double* spacing = img0.GetSpacing();
    out.dxMm = spacing[0];
    out.dyMm = spacing[1];
    out.dzMm = spacing[2];
    if (sorter.GetZSpacing() > 0.0) out.dzMm = sorter.GetZSpacing();

    const double* origin = img0.GetOrigin();
    for (int i = 0; i < 3; ++i) out.originMm[i] = origin[i];

    const double* dc = img0.GetDirectionCosines();
    for (int i = 0; i < 6; ++i) out.direction[i] = dc[i];

    // Gantry tilt / obliquity detection: the slice normal (row x col) must be
    // axis-aligned for the regular voxel grid built later.
    {
        const double row[3] = {dc[0], dc[1], dc[2]};
        const double col[3] = {dc[3], dc[4], dc[5]};
        const double n[3] = {
            row[1] * col[2] - row[2] * col[1],
            row[2] * col[0] - row[0] * col[2],
            row[0] * col[1] - row[1] * col[0]
        };
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

    const gdcm::PixelFormat& pf = img0.GetPixelFormat();
    const unsigned short bitsAllocated = pf.GetBitsAllocated();
    const unsigned short bitsStored = pf.GetBitsStored();
    const unsigned short pixelRep = pf.GetPixelRepresentation();
    if (pf.GetSamplesPerPixel() != 1 || (bitsAllocated != 16 && bitsAllocated != 8)) {
        std::cerr << "[DICOMReader] Unsupported pixel format." << std::endl;
        return false;
    }

    const std::size_t plane = static_cast<std::size_t>(out.nx) * out.ny;
    out.hu.resize(plane * out.nz);

    for (int z = 0; z < out.nz; ++z) {
        gdcm::ImageReader r;
        r.SetFileName(files[z].c_str());
        if (!r.Read()) return false;
        const gdcm::Image& img = r.GetImage();
        std::vector<char> buf(img.GetBufferLength());
        if (!img.GetBuffer(buf.data())) return false;

        const std::size_t base = static_cast<std::size_t>(z) * plane;
        if (bitsAllocated == 16) {
            const std::uint16_t* raw = reinterpret_cast<const std::uint16_t*>(buf.data());
            const std::uint16_t mask = (bitsStored >= 16)
                ? static_cast<std::uint16_t>(0xFFFFu)
                : static_cast<std::uint16_t>((1u << bitsStored) - 1u);
            for (std::size_t i = 0; i < plane; ++i) {
                std::uint16_t v = raw[i] & mask;
                if (pixelRep == 1 && bitsStored >= 1 && (v & (1u << (bitsStored - 1)))) {
                    v |= static_cast<std::uint16_t>(~mask);
                }
                const int val = (pixelRep == 1) ? static_cast<std::int16_t>(v)
                                                : static_cast<int>(v);
                out.hu[base + i] = static_cast<short>(out.rescaleSlope * val + out.rescaleIntercept);
            }
        } else {
            const std::uint8_t* raw = reinterpret_cast<const std::uint8_t*>(buf.data());
            for (std::size_t i = 0; i < plane; ++i) {
                const int val = (pixelRep == 1) ? static_cast<std::int8_t>(raw[i])
                                                : static_cast<int>(raw[i]);
                out.hu[base + i] = static_cast<short>(out.rescaleSlope * val + out.rescaleIntercept);
            }
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
                RTStruct::ROI roi;
                roi.number = num.GetValue();
                roi.name = name.GetValue();
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
    std::cout << "[DICOMReader] RTPLAN: " << out.beams.size() << " beams" << std::endl;
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

    gdcm::ImageReader reader;
    reader.SetFileName(file.c_str());
    if (!reader.Read()) {
        std::cerr << "[DICOMReader] Cannot read RTDOSE: " << file << std::endl;
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
    for (int i = 0; i < 6; ++i) out.direction[i] = dc[i];

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
