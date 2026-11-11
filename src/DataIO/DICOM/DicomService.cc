//==============================================================================
// G4CARE - DicomService.cc — axis-aligned CT slicing for the viewer.
//==============================================================================

#include "DicomService.hh"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "DICOMReader.hh"
#include "NrrdReader.hh"
#include "ConfigManager.hh"

#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>
#include <unordered_map>

#ifdef G4CARE_HAS_DICOM
#include <gdcmAttribute.h>
#include <gdcmDicts.h>
#include <gdcmGlobal.h>
#include <gdcmItem.h>
#include <gdcmPixelFormat.h>
#include <gdcmSequenceOfItems.h>
#include <gdcmSmartPointer.h>
#include <gdcmUIDGenerator.h>
#include <gdcmImageWriter.h>
#include <gdcmWriter.h>
#endif

DicomService* DicomService::fInstance = nullptr;

DicomService* DicomService::Instance() {
    if (!fInstance) fInstance = new DicomService();
    return fInstance;
}

void DicomService::DeleteInstance() {
    delete fInstance;
    fInstance = nullptr;
}

bool DicomService::GetCroppedCT(CTSeries& out) const {
    if (!fCT.valid || !fHasTransfer) return false;
    const int nz = fCT.nz;
    const int from = std::max(0, std::min(fTransferRequest.sliceFrom, nz - 1));
    const int to = std::max(0, std::min(fTransferRequest.sliceTo, nz - 1));
    if (from > to) return false;
    out = fCT;
    out.nz = to - from + 1;
    out.originMm[2] += from * fCT.dzMm;
    const size_t plane = static_cast<size_t>(fCT.nx) * fCT.ny;
    const size_t base = static_cast<size_t>(from) * plane;
    const size_t n = plane * static_cast<size_t>(out.nz);
    if (fCT.floatData) out.floatValues.assign(fCT.floatValues.begin() + base, fCT.floatValues.begin() + base + n);
    else out.hu.assign(fCT.hu.begin() + base, fCT.hu.begin() + base + n);
    return true;
}

bool DicomService::LoadCT(const std::string& directory, std::string& err) {
    DICOMReader reader;
    if (!reader.ReadCTSeries(directory, fCT)) {
        err = "Failed to read CT series from: " + directory;
        return false;
    }
    if (!fCT.valid || fCT.TotalVoxels() == 0) {
        err = "CT series is empty or invalid";
        return false;
    }
    fCtDir = directory;
    return true;
}

void DicomService::SetWorldOffset(const CTSeries& ct, const double pos[3]) {
    const double col[3] = {ct.direction[3], ct.direction[4], ct.direction[5]};
    const double row[3] = {ct.direction[0], ct.direction[1], ct.direction[2]};
    const double slc[3] = {ct.direction[6], ct.direction[7], ct.direction[8]};
    for (int i = 0; i < 3; ++i) {
        const double center = ct.originMm[i]
            + 0.5 * (ct.nx * ct.dxMm * col[i] + ct.ny * ct.dyMm * row[i]
                     + ct.nz * ct.dzMm * slc[i]);
        fWorldOffset[i] = center - pos[i];
    }
}

bool DicomService::UpdateWorldOffset(const CTSeries& ct) {
    for (int i = 0; i < 3; ++i) fWorldOffset[i] = 0.0;
    auto* cfg = ConfigManager::Instance();
    if (!cfg->HasKey("GEOMETRY.DICOM.POSITION.x")) return false;
    const double pos[3] = {
        cfg->GetValueWithUnits("GEOMETRY.DICOM.POSITION.x", 0.0),
        cfg->GetValueWithUnits("GEOMETRY.DICOM.POSITION.y", 0.0),
        cfg->GetValueWithUnits("GEOMETRY.DICOM.POSITION.z", 0.0),
    };
    SetWorldOffset(ct, pos);
    return true;
}

bool DicomService::GetSlice(int axis, int index, std::vector<int16_t>& out,
                            int& w, int& h) const {
    if (!fCT.valid) return false;
    const int nx = fCT.nx, ny = fCT.ny, nz = fCT.nz;
    if (axis == 2) {  // axial: iz fixed
        if (index < 0 || index >= nz) return false;
        w = nx; h = ny;
        out.assign(static_cast<size_t>(nx) * ny, 0);
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix)
                out[static_cast<size_t>(iy) * nx + ix] = fCT.HU(ix, iy, index);
        return true;
    }
    if (axis == 1) {  // coronal: iy fixed
        if (index < 0 || index >= ny) return false;
        w = nx; h = nz;
        out.assign(static_cast<size_t>(nx) * nz, 0);
        for (int iz = 0; iz < nz; ++iz)
            for (int ix = 0; ix < nx; ++ix)
                out[static_cast<size_t>(iz) * nx + ix] = fCT.HU(ix, index, iz);
        return true;
    }
    // sagittal: ix fixed
    if (index < 0 || index >= nx) return false;
    w = ny; h = nz;
    out.assign(static_cast<size_t>(ny) * nz, 0);
    for (int iz = 0; iz < nz; ++iz)
        for (int iy = 0; iy < ny; ++iy)
            out[static_cast<size_t>(iz) * ny + iy] = fCT.HU(index, iy, iz);
    return true;
}

bool DicomService::LoadRTStruct(const std::string& file, std::string& err) {
    DICOMReader reader;
    if (!reader.ReadRTStruct(file, fRTStruct)) {
        err = "Failed to read RTSTRUCT: " + file;
        return false;
    }
    return true;
}

bool DicomService::LoadRTDose(const std::string& file, std::string& err) {
    DICOMReader reader;
    if (!reader.ReadRTDose(file, fRTDose)) {
        err = "Failed to read RTDOSE: " + file;
        return false;
    }
    return true;
}

bool DicomService::LoadRTPlan(const std::string& file, std::string& err) {
    DICOMReader reader;
    if (!reader.ReadRTPlan(file, fRTPlan)) {
        err = "Failed to read RTPLAN: " + file;
        return false;
    }
    return true;
}

bool DicomService::LoadVolumeFile(const std::string& file, std::string& err) {
    // Detect NRRD by magic, NIfTI by extension (.nii/.nii.gz).
    std::ifstream probe(file, std::ios::binary);
    if (!probe) {
        err = "Cannot open file: " + file;
        return false;
    }
    char magic[4] = {0, 0, 0, 0};
    probe.read(magic, 4);
    probe.close();

    std::string lower = file;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const bool nifti = (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".nii") ||
                       (lower.size() >= 7 && lower.substr(lower.size() - 7) == ".nii.gz");

    if (std::string(magic, 4) == "NRRD") {
        if (!ReadNRRD(file, fCT, err)) return false;
    } else if (nifti) {
        if (!ReadNIfTI(file, fCT, err)) return false;
    } else {
        err = "Unsupported volume format (NRRD / NIfTI): " + file;
        return false;
    }
    return true;
}

bool DicomService::SaveRTStruct(const RTStruct& rs, const std::string& file, std::string& err) {
#ifndef G4CARE_HAS_DICOM
    (void)rs; (void)file;
    err = "DICOM support disabled (rebuild with -DG4CARE_WITH_DICOM=ON)";
    return false;
#else
    using namespace gdcm;
    if (!fCT.valid) { err = "CT not loaded"; return false; }
    if (rs.rois.empty()) { err = "No structures to export"; return false; }

    UIDGenerator uid;
    const std::string sopClass = "1.2.840.10008.5.1.4.1.1.481.3";
    const std::string sopInst = uid.Generate();
    const std::string studyUid = uid.Generate();
    const std::string seriesUid = uid.Generate();
    const std::string frameUid = uid.Generate();

    DataSet ds;
    auto put = [&](const Tag& t, const std::string& v) {
        DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
        de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
        ds.Insert(de);
    };
    put(Tag(0x0008, 0x0016), sopClass);
    put(Tag(0x0008, 0x0018), sopInst);
    put(Tag(0x0008, 0x0020), fCT.acquisitionDate.empty() ? "20260101" : fCT.acquisitionDate);
    put(Tag(0x0008, 0x0030), fCT.acquisitionTime.empty() ? "000000" : fCT.acquisitionTime);
    put(Tag(0x0008, 0x0060), "RTSTRUCT");
    put(Tag(0x0008, 0x0070), "G4CARE");
    put(Tag(0x0008, 0x103E), "G4CARE Structures");
    put(Tag(0x0010, 0x0010), fCT.patientName);
    put(Tag(0x0010, 0x0020), fCT.patientId);
    const std::string ctStudyUid = fCT.studyInstanceUID.empty() ? studyUid : fCT.studyInstanceUID;
    const std::string ctFrameUid = fCT.frameOfReferenceUID.empty() ? frameUid : fCT.frameOfReferenceUID;
    const std::string ctSeriesUid = fCT.seriesInstanceUID.empty() ? seriesUid : fCT.seriesInstanceUID;
    put(Tag(0x0020, 0x000D), ctStudyUid);
    put(Tag(0x0020, 0x000E), seriesUid);
    put(Tag(0x0020, 0x0010), fCT.studyId);
    put(Tag(0x0020, 0x0011), fCT.seriesNumber);

    // Referenced Frame of Reference Sequence (3006,0010) — links back to the CT
    // study/series/slices so the structures overlay the same CT in viewers.
    {
        SmartPointer<SequenceOfItems> sq = new SequenceOfItems();
        sq->SetLengthToUndefined();
        Item item;
        item.SetVLToUndefined();
        DataSet& sub = item.GetNestedDataSet();
        auto putR = [&](const Tag& t, const std::string& v) {
            DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
            de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
            sub.Insert(de);
        };
        putR(Tag(0x0020, 0x0052), ctFrameUid);  // Frame of Reference UID
        SmartPointer<SequenceOfItems> studySq = new SequenceOfItems();
        studySq->SetLengthToUndefined();
        {
            Item sitem;
            sitem.SetVLToUndefined();
            DataSet& ssub = sitem.GetNestedDataSet();
            auto putS = [&](const Tag& t, const std::string& v) {
                DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                ssub.Insert(de);
            };
            putS(Tag(0x0008, 0x1150), "1.2.840.10008.3.1.2.3.2");  // Study Component Management
            putS(Tag(0x0008, 0x1155), ctStudyUid);
            SmartPointer<SequenceOfItems> seriesSq = new SequenceOfItems();
            seriesSq->SetLengthToUndefined();
            {
                Item seritem;
                seritem.SetVLToUndefined();
                DataSet& sersub = seritem.GetNestedDataSet();
                auto putSr = [&](const Tag& t, const std::string& v) {
                    DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                    de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                    sersub.Insert(de);
                };
                putSr(Tag(0x0020, 0x000E), ctSeriesUid);
                SmartPointer<SequenceOfItems> imgSq = new SequenceOfItems();
                imgSq->SetLengthToUndefined();
                const std::string ctSopClass = fCT.sopClassUID.empty() ? "1.2.840.10008.5.1.4.1.1.2" : fCT.sopClassUID;
                for (const auto& sop : fCT.sliceSopInstanceUIDs) {
                    Item imgitem;
                    imgitem.SetVLToUndefined();
                    DataSet& imgsub = imgitem.GetNestedDataSet();
                    auto putI = [&](const Tag& t, const std::string& v) {
                        DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                        de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                        imgsub.Insert(de);
                    };
                    putI(Tag(0x0008, 0x1150), ctSopClass);
                    putI(Tag(0x0008, 0x1155), sop);
                    imgSq->AddItem(imgitem);
                }
                DataElement imgDe(Tag(0x3006, 0x0016));
                imgDe.SetVR(VR::SQ);
                imgDe.SetValue(*imgSq);
                sersub.Insert(imgDe);
                seriesSq->AddItem(seritem);
            }
            DataElement seriesDe(Tag(0x3006, 0x0014));
            seriesDe.SetVR(VR::SQ);
            seriesDe.SetValue(*seriesSq);
            ssub.Insert(seriesDe);
            studySq->AddItem(sitem);
        }
        DataElement studyDe(Tag(0x3006, 0x0012));
        studyDe.SetVR(VR::SQ);
        studyDe.SetValue(*studySq);
        sub.Insert(studyDe);
        sq->AddItem(item);
        DataElement de(Tag(0x3006, 0x0010));
        de.SetVR(VR::SQ);
        de.SetValue(*sq);
        ds.Insert(de);
    }

    // Structure Set ROI Sequence (3006,0020)
    {
        SmartPointer<SequenceOfItems> sq = new SequenceOfItems();
        sq->SetLengthToUndefined();
        for (const auto& roi : rs.rois) {
            Item item;
            item.SetVLToUndefined();
            DataSet& sub = item.GetNestedDataSet();
            auto put2 = [&](const Tag& t, const std::string& v) {
                DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                sub.Insert(de);
            };
            put2(Tag(0x3006, 0x0022), std::to_string(roi.number));
            put2(Tag(0x3006, 0x0026), roi.name);
            put2(Tag(0x3006, 0x0024), ctFrameUid);
            put2(Tag(0x3006, 0x0036), "MANUAL");
            put2(Tag(0x3006, 0x00A4), roi.type.empty() ? "ORGAN" : roi.type);
            sq->AddItem(item);
        }
        DataElement de(Tag(0x3006, 0x0020));
        de.SetVR(VR::SQ);
        de.SetValue(*sq);
        ds.Insert(de);
    }

    // ROI Contour Sequence (3006,0039)
    {
        SmartPointer<SequenceOfItems> sq = new SequenceOfItems();
        sq->SetLengthToUndefined();
        for (const auto& roi : rs.rois) {
            Item item;
            item.SetVLToUndefined();
            DataSet& sub = item.GetNestedDataSet();
            auto put2 = [&](const Tag& t, const std::string& v) {
                DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                sub.Insert(de);
            };
            put2(Tag(0x3006, 0x0084), std::to_string(roi.number));
            put2(Tag(0x3006, 0x002A), "255 0 0");  // ROIDisplayColor (R G B)
            SmartPointer<SequenceOfItems> csq = new SequenceOfItems();
            csq->SetLengthToUndefined();
            for (const auto& c : roi.contours) {
                if (c.x.size() < 3 || c.x.size() != c.y.size() || c.x.size() != c.z.size()) continue;
                Item citem;
                citem.SetVLToUndefined();
                DataSet& csub = citem.GetNestedDataSet();
                auto put3 = [&](const Tag& t, const std::string& v) {
                    DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                    de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                    csub.Insert(de);
                };
                put3(Tag(0x3006, 0x0042), "CLOSED_PLANAR");
                put3(Tag(0x3006, 0x0046), std::to_string(c.x.size()));
                std::ostringstream cd;
                for (size_t i = 0; i < c.x.size(); ++i) {
                    if (i) cd << "\\\\";
                    cd << c.x[i] << "\\\\" << c.y[i] << "\\\\" << c.z[i];
                }
                put3(Tag(0x3006, 0x0050), cd.str());
                csq->AddItem(citem);
            }
            DataElement cde(Tag(0x3006, 0x0040));
            cde.SetVR(VR::SQ);
            cde.SetValue(*csq);
            sub.Insert(cde);
            sq->AddItem(item);
        }
        DataElement de(Tag(0x3006, 0x0039));
        de.SetVR(VR::SQ);
        de.SetValue(*sq);
        ds.Insert(de);
    }

    // RT ROI Observations Sequence (3006,0080)
    {
        SmartPointer<SequenceOfItems> sq = new SequenceOfItems();
        sq->SetLengthToUndefined();
        int obs = 1;
        for (const auto& roi : rs.rois) {
            Item item;
            item.SetVLToUndefined();
            DataSet& sub = item.GetNestedDataSet();
            auto put2 = [&](const Tag& t, const std::string& v) {
                DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                sub.Insert(de);
            };
            put2(Tag(0x3006, 0x0082), std::to_string(obs));
            put2(Tag(0x3006, 0x0084), std::to_string(roi.number));
            put2(Tag(0x3006, 0x00A4), roi.type.empty() ? "ORGAN" : roi.type);
            put2(Tag(0x3006, 0x00A6), roi.name);
            sq->AddItem(item);
            obs++;
        }
        DataElement de(Tag(0x3006, 0x0080));
        de.SetVR(VR::SQ);
        de.SetValue(*sq);
        ds.Insert(de);
    }

    Writer writer;
    writer.SetFileName(file.c_str());
    writer.GetFile().GetDataSet() = ds;
    writer.GetFile().GetHeader().SetDataSetTransferSyntax(TransferSyntax::ImplicitVRLittleEndian);
    if (!writer.Write()) { err = "GDCM writer failed: " + file; return false; }
    return true;
#endif
}

bool DicomService::SaveRTPlan(const RTPlan& plan, const std::string& file, std::string& err) {
#ifndef G4CARE_HAS_DICOM
    (void)plan; (void)file;
    err = "DICOM support disabled (rebuild with -DG4CARE_WITH_DICOM=ON)";
    return false;
#else
    using namespace gdcm;
    if (plan.beams.empty()) { err = "No beams to export"; return false; }

    UIDGenerator uid;
    const std::string sopClass = "1.2.840.10008.5.1.4.1.1.481.5";  // RT Plan Storage
    const std::string sopInst = uid.Generate();
    const std::string seriesUid = uid.Generate();

    DataSet ds;
    auto put = [&](const Tag& t, const std::string& v) {
        DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
        de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
        ds.Insert(de);
    };

    put(Tag(0x0008, 0x0016), sopClass);
    put(Tag(0x0008, 0x0018), sopInst);
    put(Tag(0x0008, 0x0020), fCT.acquisitionDate.empty() ? "20260101" : fCT.acquisitionDate);
    put(Tag(0x0008, 0x0030), fCT.acquisitionTime.empty() ? "000000" : fCT.acquisitionTime);
    put(Tag(0x0008, 0x0060), "RTPLAN");
    put(Tag(0x0008, 0x0070), "G4CARE");
    put(Tag(0x0010, 0x0010), fCT.patientName);
    put(Tag(0x0010, 0x0020), fCT.patientId);
    const std::string ctStudyUid = fCT.studyInstanceUID.empty() ? uid.Generate() : fCT.studyInstanceUID;
    const std::string ctFrameUid = fCT.frameOfReferenceUID.empty() ? uid.Generate() : fCT.frameOfReferenceUID;
    put(Tag(0x0020, 0x000D), ctStudyUid);
    put(Tag(0x0020, 0x000E), seriesUid);
    put(Tag(0x0020, 0x0010), fCT.studyId);
    put(Tag(0x0020, 0x0011), fCT.seriesNumber);
    put(Tag(0x0020, 0x0052), ctFrameUid);

    // RT Plan Label.
    put(Tag(0x300A, 0x0002), plan.label.empty() ? "G4CARE Plan" : plan.label);

    // DoseReferenceSequence (optional prescription dose).
    if (plan.prescriptionDoseCGy > 0.0) {
        SmartPointer<SequenceOfItems> sq = new SequenceOfItems();
        sq->SetLengthToUndefined();
        Item item; item.SetVLToUndefined();
        DataSet& sub = item.GetNestedDataSet();
        auto putR = [&](const Tag& t, const std::string& v) {
            DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
            de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
            sub.Insert(de);
        };
        putR(Tag(0x300A, 0x0012), uid.Generate());  // Dose Reference UID
        putR(Tag(0x300A, 0x0014), "TARGET");       // Dose Reference Structure Type
        putR(Tag(0x300A, 0x0026), std::to_string(plan.prescriptionDoseCGy));
        sq->AddItem(item);
        DataElement de(Tag(0x300A, 0x0010));
        de.SetVR(VR::SQ); de.SetValue(*sq);
        ds.Insert(de);
    }

    // BeamSequence (300A,00B0).
    {
        SmartPointer<SequenceOfItems> beamSq = new SequenceOfItems();
        beamSq->SetLengthToUndefined();
        for (const auto& beam : plan.beams) {
            Item item; item.SetVLToUndefined();
            DataSet& sub = item.GetNestedDataSet();
            auto putB = [&](const Tag& t, const std::string& v) {
                DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                sub.Insert(de);
            };
            putB(Tag(0x300A, 0x00C0), std::to_string(beam.number));
            putB(Tag(0x300A, 0x00C2), beam.name.empty() ? ("Beam " + std::to_string(beam.number)) : beam.name);
            putB(Tag(0x300A, 0x00B2), "G4CARE");  // Treatment Machine Name
            putB(Tag(0x300A, 0x00BB), "STATIC");  // Beam Type
            putB(Tag(0x300A, 0x00C6), beam.radiationType.empty() ? "PHOTON" : beam.radiationType);

            // ControlPointSequence (300A,0111).
            SmartPointer<SequenceOfItems> cpSq = new SequenceOfItems();
            cpSq->SetLengthToUndefined();
            int cpIndex = 0;
            for (const auto& cp : beam.controlPoints) {
                Item citem; citem.SetVLToUndefined();
                DataSet& csub = citem.GetNestedDataSet();
                auto putC = [&](const Tag& t, const std::string& v) {
                    DataElement de(t); de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
                    de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
                    csub.Insert(de);
                };
                putC(Tag(0x300A, 0x0112), std::to_string(cpIndex));        // Control Point Index
                putC(Tag(0x300A, 0x0134), std::to_string(cpIndex));        // Cumulative Meterset Weight
                putC(Tag(0x300A, 0x0114), std::to_string(cp.energyMeV));   // Nominal Beam Energy
                putC(Tag(0x300A, 0x011E), std::to_string(cp.gantryAngleDeg));
                putC(Tag(0x300A, 0x0122), std::to_string(cp.couchAngleDeg));  // Patient Support Angle
                std::ostringstream iso;
                iso << cp.isocenter[0] << "\\" << cp.isocenter[1] << "\\" << cp.isocenter[2];
                putC(Tag(0x300A, 0x012C), iso.str());  // Isocenter Position (DS3)
                cpSq->AddItem(citem);
                ++cpIndex;
            }
            DataElement cpDe(Tag(0x300A, 0x0111));
            cpDe.SetVR(VR::SQ); cpDe.SetValue(*cpSq);
            sub.Insert(cpDe);
            beamSq->AddItem(item);
        }
        DataElement beamDe(Tag(0x300A, 0x00B0));
        beamDe.SetVR(VR::SQ); beamDe.SetValue(*beamSq);
        ds.Insert(beamDe);
    }

    Writer writer;
    writer.SetFileName(file.c_str());
    writer.GetFile().GetDataSet() = ds;
    writer.GetFile().GetHeader().SetDataSetTransferSyntax(TransferSyntax::ImplicitVRLittleEndian);
    if (!writer.Write()) { err = "GDCM writer failed: " + file; return false; }
    return true;
#endif
}

bool DicomService::SaveRTDose(const RTDose& dose, const std::string& file, std::string& err) {
#ifndef G4CARE_HAS_DICOM
    (void)dose; (void)file;
    err = "DICOM support disabled (rebuild with -DG4CARE_WITH_DICOM=ON)";
    return false;
#else
    using namespace gdcm;
    if (!dose.valid || dose.doseGy.empty()) { err = "No dose data to export"; return false; }
    const std::size_t n = static_cast<std::size_t>(dose.nx) * dose.ny * dose.nz;
    if (dose.doseGy.size() != n) { err = "Dose grid size mismatch"; return false; }

    UIDGenerator uid;
    const std::string sopClass = "1.2.840.10008.5.1.4.1.1.481.2";  // RT Dose Storage

    // RTDOSE stores integer pixels scaled by DoseGridScaling. gdcm's ImageWriter
    // rejects FLOAT32 (PixelFormat::IsValid forbids the float PixelRepresentation),
    // so encode dose as UINT32 with a computed scaling factor.
    double maxDose = 0.0;
    for (double v : dose.doseGy) if (v > maxDose) maxDose = v;
    const double scaling = (maxDose > 0.0) ? (maxDose / 4294967295.0) : 1.0;
    std::vector<std::uint32_t> buf(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double r = dose.doseGy[i] / scaling;
        buf[i] = static_cast<std::uint32_t>(r + 0.5);
    }

    ImageWriter writer;
    Image& image = writer.GetImage();
    image.SetNumberOfDimensions(3);
    unsigned int dims[3] = { static_cast<unsigned int>(dose.nx),
                             static_cast<unsigned int>(dose.ny),
                             static_cast<unsigned int>(dose.nz) };
    image.SetDimensions(dims);
    double sp[3] = { dose.dxMm, dose.dyMm, dose.dzMm };
    image.SetSpacing(sp);
    double o[3] = { dose.originMm[0], dose.originMm[1], dose.originMm[2] };
    image.SetOrigin(o);
    // Internal convention [0..2]=row(iy), [3..5]=column(ix) -> gdcm IOP order
    // [first row (ix), first column (iy)].
    double dc[6] = { dose.direction[3], dose.direction[4], dose.direction[5],
                     dose.direction[0], dose.direction[1], dose.direction[2] };
    image.SetDirectionCosines(dc);
    image.SetPixelFormat(PixelFormat::UINT32);
    image.SetPhotometricInterpretation(PhotometricInterpretation::MONOCHROME2);
    image.SetSlope(scaling);                // DoseGridScaling (3004,000E)
    image.SetIntercept(0.0);                // RTDOSE forbids an intercept

    DataElement pixelData(Tag(0x7fe0, 0x0010));
    pixelData.SetVR(VR::OW);
    pixelData.SetByteValue(reinterpret_cast<const char*>(buf.data()),
                           static_cast<uint32_t>(n * sizeof(std::uint32_t)));
    image.SetDataElement(pixelData);

    writer.SetFileName(file.c_str());

    // Metadata must live on the file dataset so ImageWriter::ComputeTargetMediaStorage
    // can read the SOP Class UID (=> RTDoseStorage) before it prepares the write.
    DataSet& ds = writer.GetFile().GetDataSet();
    auto put = [&](const Tag& t, const std::string& v) {
        DataElement de(t);
        de.SetVR(Global::GetInstance().GetDicts().GetDictEntry(t).GetVR());
        de.SetByteValue(v.c_str(), VL(static_cast<uint32_t>(v.size())));
        ds.Insert(de);
    };
    put(Tag(0x0008, 0x0016), sopClass);
    put(Tag(0x0008, 0x0018), uid.Generate());
    put(Tag(0x0008, 0x0020), fCT.acquisitionDate.empty() ? "20260101" : fCT.acquisitionDate);
    put(Tag(0x0008, 0x0030), fCT.acquisitionTime.empty() ? "000000" : fCT.acquisitionTime);
    put(Tag(0x0008, 0x0060), "RTDOSE");
    put(Tag(0x0008, 0x0070), "G4CARE");
    put(Tag(0x0010, 0x0010), fCT.patientName);
    put(Tag(0x0010, 0x0020), fCT.patientId);
    put(Tag(0x0020, 0x000D), fCT.studyInstanceUID.empty() ? uid.Generate() : fCT.studyInstanceUID);
    put(Tag(0x0020, 0x000E), uid.Generate());
    put(Tag(0x0020, 0x0010), fCT.studyId);
    put(Tag(0x0020, 0x0011), fCT.seriesNumber);
    put(Tag(0x0020, 0x0052), fCT.frameOfReferenceUID.empty() ? uid.Generate() : fCT.frameOfReferenceUID);
    // RT Dose module (DoseGridScaling is written by gdcm from image.SetSlope).
    put(Tag(0x3004, 0x0002), dose.doseUnits.empty() ? "GY" : dose.doseUnits);  // Dose Units
    put(Tag(0x3004, 0x0004), "PHYSICAL");                                       // Dose Type
    put(Tag(0x3004, 0x000A), "PLAN");                                           // Dose Summation Type

    writer.GetFile().GetHeader().SetDataSetTransferSyntax(TransferSyntax::ExplicitVRLittleEndian);
    if (!writer.Write()) { err = "GDCM writer failed: " + file; return false; }
    return true;
#endif
}


namespace {

// HU -> NIST material name, consistent with the GUI auto-seg classes
// (air / lung / soft tissue / fat / muscle / bone).
const char* HuToMaterialName(short hu) {
    if (hu < -900) return "G4_AIR";
    if (hu < -400) return "G4_LUNG_ICRP";
    if (hu < -100) return "G4_TISSUE_SOFT_ICRP";
    if (hu < -50)  return "G4_ADIPOSE_TISSUE_ICRP";
    if (hu < 300)  return "G4_MUSCLE_SKELETAL_ICRP";
    return "G4_BONE_COMPACT_ICRU";
}

}  // namespace

bool DicomService::TransferSelection(const TransferRequest& req, TransferResult& out) {
    out = TransferResult{};
    if (!fCT.valid) { out.error = "CT not loaded"; return false; }
    const int nz = fCT.nz;
    const int from = std::max(0, std::min(req.sliceFrom, nz - 1));
    const int to = std::max(0, std::min(req.sliceTo, nz - 1));
    if (from > to) { out.error = "Empty slice range"; return false; }

    const size_t plane = static_cast<size_t>(fCT.nx) * fCT.ny;
    const size_t base = static_cast<size_t>(from) * plane;
    const size_t nz2 = static_cast<size_t>(to - from + 1);
    const size_t total = plane * nz2;

    out.ok = true;
    out.nx = fCT.nx; out.ny = fCT.ny; out.nz = static_cast<int>(nz2);
    out.dx = fCT.dxMm; out.dy = fCT.dyMm; out.dz = fCT.dzMm;
    out.origin = { fCT.originMm[0], fCT.originMm[1], fCT.originMm[2] + from * fCT.dzMm };

    for (const auto& v : req.volumes) {
        TransferResult::VolumeInfo info;
        info.name = v.name;
        info.material = v.material;
        if (v.mask.size() < total) continue;  // mask must match the cropped grid
        std::map<std::string, size_t> groups;
        for (size_t i = 0; i < total; ++i) {
            if (!v.mask[i]) continue;
            const short hu = fCT.floatData
                ? static_cast<short>(fCT.floatValues[base + i])
                : fCT.hu[base + i];
            const std::string mat = v.material.empty() ? HuToMaterialName(hu) : v.material;
            groups[mat]++;
            info.totalVoxels++;
        }
        for (const auto& kv : groups) info.groups.push_back({ kv.first, kv.second });
        out.volumes.push_back(std::move(info));
    }
    fTransferRequest = req;
    fHasTransfer = true;
    return true;
}

namespace {

// Downsample a binary mask by factor f (any sub-voxel set -> coarse voxel set).
void DownsampleMask(const std::vector<uint8_t>& mask, int nx, int ny, int nz, int f,
                    std::vector<uint8_t>& out, int& cnx, int& cny, int& cnz) {
    cnx = (nx + f - 1) / f;
    cny = (ny + f - 1) / f;
    cnz = (nz + f - 1) / f;
    out.assign(static_cast<size_t>(cnx) * cny * cnz, 0);
    for (int k = 0; k < nz; k++) {
        for (int j = 0; j < ny; j++) {
            for (int i = 0; i < nx; i++) {
                if (mask[static_cast<size_t>(k * ny + j) * nx + i]) {
                    out[static_cast<size_t>(k / f * cny + j / f) * cnx + i / f] = 1;
                }
            }
        }
    }
}

// Emit the exposed faces of boundary voxels (blocky but correct).
void BuildVoxelFaces(const std::vector<uint8_t>& mask, int nx, int ny, int nz,
                     double dx, double dy, double dz, double ox, double oy, double oz,
                     std::vector<double>& verts, std::vector<int>& tris) {
    verts.clear();
    tris.clear();
    auto at = [&](int i, int j, int k) -> bool {
        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return false;
        return mask[static_cast<size_t>(k * ny + j) * nx + i] != 0;
    };
    // Share corners between adjacent faces (vertex dedup) to keep meshes compact.
    std::unordered_map<size_t, int> cornerIdx;
    auto corner = [&](int i, int j, int k) -> int {
        const size_t key = (static_cast<size_t>(i) * (ny + 1) + j) * (nz + 1) + k;
        auto it = cornerIdx.find(key);
        if (it != cornerIdx.end()) return it->second;
        const int idx = static_cast<int>(verts.size() / 3);
        verts.push_back(i * dx + ox);
        verts.push_back(j * dy + oy);
        verts.push_back(k * dz + oz);
        cornerIdx[key] = idx;
        return idx;
    };
    auto quad = [&](int a, int b, int c, int d) {
        tris.push_back(a); tris.push_back(b); tris.push_back(c);
        tris.push_back(a); tris.push_back(c); tris.push_back(d);
    };
    for (int k = 0; k < nz; k++) {
        for (int j = 0; j < ny; j++) {
            for (int i = 0; i < nx; i++) {
                if (!mask[static_cast<size_t>(k * ny + j) * nx + i]) continue;
                if (!at(i - 1, j, k)) quad(corner(i, j, k), corner(i, j + 1, k), corner(i, j + 1, k + 1), corner(i, j, k + 1));
                if (!at(i + 1, j, k)) quad(corner(i + 1, j, k), corner(i + 1, j, k + 1), corner(i + 1, j + 1, k + 1), corner(i + 1, j + 1, k));
                if (!at(i, j - 1, k)) quad(corner(i, j, k), corner(i, j, k + 1), corner(i + 1, j, k + 1), corner(i + 1, j, k));
                if (!at(i, j + 1, k)) quad(corner(i, j + 1, k), corner(i + 1, j + 1, k), corner(i + 1, j + 1, k + 1), corner(i, j + 1, k + 1));
                if (!at(i, j, k - 1)) quad(corner(i, j, k), corner(i + 1, j, k), corner(i + 1, j + 1, k), corner(i, j + 1, k));
                if (!at(i, j, k + 1)) quad(corner(i, j, k + 1), corner(i, j + 1, k + 1), corner(i + 1, j + 1, k + 1), corner(i + 1, j, k + 1));
            }
        }
    }
}

}  // namespace

bool DicomService::BuildTransferMeshes(std::vector<TransferMesh>& out) const {
    out.clear();
    if (!fCT.valid || !fHasTransfer) return false;
    CTSeries ct;
    if (!GetCroppedCT(ct)) return false;
    for (int i = 0; i < 3; ++i) ct.originMm[i] -= fWorldOffset[i];

    // 6 ICRP material groups (composition) + display colours.
    static const char* kGroups[] = {
        "G4_AIR", "G4_LUNG_ICRP", "G4_TISSUE_SOFT_ICRP",
        "G4_ADIPOSE_TISSUE_ICRP", "G4_MUSCLE_SKELETAL_ICRP", "G4_BONE_COMPACT_ICRU",
    };
    static const double kCol[][3] = {
        {0.55, 0.55, 0.6}, {0.30, 0.60, 1.0}, {0.30, 1.0, 0.4},
        {0.95, 0.75, 0.5}, {0.90, 0.50, 0.6}, {1.0, 0.85, 0.3},
    };
    const int nx = ct.nx, ny = ct.ny, nz = ct.nz;
    const size_t n = static_cast<size_t>(nx) * ny * nz;

    int f = 1;
    const int big = (nx > ny) ? nx : ny;
    while (big / f > 128) f++;

    for (const auto& vol : fTransferRequest.volumes) {
        if (vol.mask.size() != n) continue;
        if (!vol.material.empty()) {
            // Single override material: one mesh for the whole volume.
            TransferMesh m;
            m.name = vol.name;
            m.material = vol.material;
            m.color = {1.0, 0.45, 0.25};
            std::vector<uint8_t> coarse;
            int cnx, cny, cnz;
            DownsampleMask(vol.mask, nx, ny, nz, f, coarse, cnx, cny, cnz);
            BuildVoxelFaces(coarse, cnx, cny, cnz,
                            ct.dxMm * f, ct.dyMm * f, ct.dzMm * f,
                            ct.originMm[0], ct.originMm[1], ct.originMm[2],
                            m.vertices, m.triangles);
            out.push_back(std::move(m));
            continue;
        }
        // HU-based: split the volume's voxels into the 6 material groups.
        std::vector<uint8_t> masks[6];
        size_t counts[6] = {0, 0, 0, 0, 0, 0};
        for (int g = 0; g < 6; ++g) masks[g].assign(n, 0);
        for (size_t i = 0; i < n; ++i) {
            if (!vol.mask[i]) continue;
            const short hu = ct.floatData ? static_cast<short>(ct.floatValues[i]) : ct.hu[i];
            int gi = 0;
            const char* mat = HuToMaterialName(hu);
            for (int g = 0; g < 6; ++g) if (std::strcmp(mat, kGroups[g]) == 0) { gi = g; break; }
            masks[gi][i] = 1;
            counts[gi]++;
        }
        for (int g = 0; g < 6; ++g) {
            if (counts[g] == 0) continue;
            TransferMesh m;
            m.name = vol.name + " · " + kGroups[g];
            m.material = kGroups[g];
            m.color = {kCol[g][0], kCol[g][1], kCol[g][2]};
            std::vector<uint8_t> coarse;
            int cnx, cny, cnz;
            DownsampleMask(masks[g], nx, ny, nz, f, coarse, cnx, cny, cnz);
            BuildVoxelFaces(coarse, cnx, cny, cnz,
                            ct.dxMm * f, ct.dyMm * f, ct.dzMm * f,
                            ct.originMm[0], ct.originMm[1], ct.originMm[2],
                            m.vertices, m.triangles);
            out.push_back(std::move(m));
        }
    }
    return true;
}
