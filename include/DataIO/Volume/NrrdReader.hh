//==============================================================================
// G4CARE
// @file    NrrdReader.hh
// @brief   NRRD volume reader (nearly raw raster data).
//==============================================================================

#ifndef NRRD_READER_HH
#define NRRD_READER_HH

#include <string>

#include "DataIO/DICOM/CTSeries.hh"

/// @brief Read an NRRD volume (.nrrd) into a generic image series.
/// Supports raw and gzip encodings, little-endian, integer and float types.
bool ReadNRRD(const std::string& file, CTSeries& out, std::string& err);

/// @brief Read a NIfTI volume (.nii / .nii.gz) into a generic image series.
bool ReadNIfTI(const std::string& file, CTSeries& out, std::string& err);

#endif  // NRRD_READER_HH
