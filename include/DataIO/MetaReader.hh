//==============================================================================
//
// G4CARE
//
// @file    MetaReader.hh
// @brief   Reader for G4CARE cloud metadata (TOML).
//
// @details
//   Loads metadata from a TOML file following the "<artifact>.meta.toml"
//   sidecar convention, e.g. "scene.gdml" -> "scene.gdml.meta.toml". The
//   canonical TOML structure is:
//
//     doi         = "10.5281/zenodo.1234567"
//     info_url    = "https://example.org/articles/detector"
//     description = "Silicon detector ..."
//     version     = "0.9.0"
//     created     = "2026-09-30"
//     tags        = ["detector", "nano", "niel"]
//
//     [author]
//     name  = "G4CARE Developers"
//     alias = "g4care-dev"
//
//     [affiliation]
//     group      = "G4CARE"
//     institute  = "TPU"
//     department = "..."   # optional
//
//   Backed by the header-only toml++ library (third_party/toml++, MIT).
//
//   Configuration keys read: none.
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-09-30
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#ifndef META_READER_HH
#define META_READER_HH

#include "MetaInfo.hh"

#include <string>

/// @brief Reads G4CARE cloud metadata from a TOML file.
class MetaReader {
public:
    /// @brief Load metadata from an explicit .meta.toml file.
    /// @param metaFile Path to the TOML metadata file.
    /// @return true on success.
    bool Load(const std::string& metaFile);

    /// @brief Load metadata for an artifact using the <artifact>.meta.toml convention.
    /// @param out      Receives the metadata on success.
    /// @param artifact Path to the artifact (e.g. "scene.gdml", "config.yaml").
    /// @return true on success.
    static bool LoadFor(MetaInfo& out, const std::string& artifact);

    /// @brief Derive the metadata file path from an artifact path.
    /// @param artifact Path to the artifact.
    /// @return "<artifact>.meta.toml".
    static std::string DeriveMetaPath(const std::string& artifact);

    /// @brief Get the loaded metadata (valid after a successful Load()).
    const MetaInfo& Get() const { return fMeta; }

    /// @brief Whether metadata has been successfully loaded.
    bool IsLoaded() const { return fLoaded; }

private:
    MetaInfo fMeta;
    bool fLoaded = false;
};

#endif // META_READER_HH
