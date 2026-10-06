//==============================================================================
//
// G4CARE
//
// @file    MetaReader.cc
// @brief   Implementation of the G4CARE cloud metadata (TOML) reader.
//
// @details
//   See MetaReader.hh for the canonical TOML structure. Parsing is done with
//   the header-only toml++ library. This module has no Geant4 dependency and
//   logs to std::cerr / std::cout.
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

#include "MetaReader.hh"

#include <toml++/toml.hpp>

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

std::string MetaReader::DeriveMetaPath(const std::string& artifact) {
    return artifact + ".meta.toml";
}

bool MetaReader::Load(const std::string& metaFile) {
    fLoaded = false;
    fMeta = MetaInfo{};

    try {
        toml::table tbl = toml::parse_file(metaFile);

        fMeta.doi         = tbl["doi"].value_or(std::string{});
        fMeta.infoUrl     = tbl["info_url"].value_or(std::string{});
        fMeta.description = tbl["description"].value_or(std::string{});
        fMeta.version     = tbl["version"].value_or(std::string{});
        fMeta.created     = tbl["created"].value_or(std::string{});

        fMeta.author.name  = tbl["author"]["name"].value_or(std::string{});
        fMeta.author.alias = tbl["author"]["alias"].value_or(std::string{});

        fMeta.affiliation.group      = tbl["affiliation"]["group"].value_or(std::string{});
        fMeta.affiliation.institute  = tbl["affiliation"]["institute"].value_or(std::string{});
        fMeta.affiliation.department = tbl["affiliation"]["department"].value_or(std::string{});

        if (auto* tags = tbl["tags"].as_array()) {
            for (std::size_t i = 0; i < tags->size(); ++i) {
                if (auto* tag = tags->get(i)) {
                    std::string value = tag->value_or(std::string{});
                    if (!value.empty()) {
                        fMeta.tags.push_back(std::move(value));
                    }
                }
            }
        }

        fLoaded = true;
        return true;
    }
    catch (const toml::parse_error& err) {
        std::cerr << "MetaReader: TOML parse error in '" << metaFile
                  << "': " << err.description() << std::endl;
        return false;
    }
    catch (const std::exception& err) {
        std::cerr << "MetaReader: error loading '" << metaFile
                  << "': " << err.what() << std::endl;
        return false;
    }
}

bool MetaReader::LoadFor(MetaInfo& out, const std::string& artifact) {
    MetaReader reader;
    if (!reader.Load(DeriveMetaPath(artifact))) {
        return false;
    }
    out = reader.Get();
    return true;
}
