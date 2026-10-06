//==============================================================================
//
// G4CARE
//
// @file    MetaInfo.hh
// @brief   Cloud metadata structure (DOI, description, tags, author, affiliation).
//
// @details
//   Plain data structures describing a cloud artifact (YAML config, GDML,
//   shared library, macro, ...). The values are filled by MetaReader from a
//   TOML sidecar file "<artifact>.meta.toml". The TOML format is deliberately
//   distinct from the YAML configuration so the two can never be confused.
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

#ifndef META_INFO_HH
#define META_INFO_HH

#include <string>
#include <vector>

/// @brief Author of a cloud artifact (full name + alias / pseudonym).
struct MetaAuthor {
    std::string name;    ///< Full name (ФИО).
    std::string alias;   ///< Alias / pseudonym (псевдоним).
};

/// @brief Affiliation of a cloud artifact (group, institute, department).
struct MetaAffiliation {
    std::string group;      ///< Group (группа).
    std::string institute;  ///< Institute (институт).
    std::string department; ///< Optional department / laboratory (кафедра / лаборатория).
};

/// @brief Metadata describing a cloud artifact for preview purposes.
struct MetaInfo {
    std::string doi;          ///< DOI of the related article.
    std::string infoUrl;      ///< Link to a file with full information.
    std::string description;  ///< Human-readable description.
    std::string version;      ///< Artifact version.
    std::string created;      ///< Creation date (ISO-8601).
    MetaAuthor author;        ///< Author of the artifact.
    MetaAffiliation affiliation; ///< Affiliation of the artifact.
    std::vector<std::string> tags; ///< Search tags.
};

#endif // META_INFO_HH
