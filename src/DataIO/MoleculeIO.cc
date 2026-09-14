//==============================================================================
//
// G4CARE
//
// @file    MoleculeIO.cc
// @brief   Binary I/O for chemistry molecule state (SBS checkpoint).
//
// @details
//   Provides WriteBinary() and ReadBinary() for serializing/deserializing
//   molecule state vectors.  Supports two file format versions:
//     v1: legacy format with char[16] species names (52 bytes/record).
//     v2: compact format with int32 speciesIndex (40 bytes/record + header).
//
//   The binary file layout:
//     [uint32 magic] [uint32 version] [MoleculeFileHeader (v2+)]
//     [uint64 count] [MoleculeRecord × count]
//
//   Used by RunAction to save/restore chemistry state between runs.
//
//   Configuration keys read: none (pure I/O utility).
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "MoleculeIO.hh"
#include <fstream>
#include <iostream>
#include <cstring>
#include <G4Exception.hh>

/// @brief Safely copy a C-string into a fixed-size 8-byte buffer.
/// @param dest Destination buffer (8 bytes).
/// @param src  Null-terminated source string.
static void CopyString8(char dest[8], const char* src) {
    std::strncpy(dest, src, 7);
    dest[7] = '\0';
}

bool MoleculeIO::WriteBinary(const std::string& filename,
                             const std::vector<MoleculeRecord>& molecules,
                             uint32_t numSpecies,
                             const char* timeUnit,
                             const char* lengthUnit,
                             const char* energyUnit) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        G4Exception("MoleculeIO::WriteBinary", "IO001", JustWarning,
                    ("Cannot open file: " + filename).c_str());
        return false;
    }

    // --- Заголовок ---
    // 1. Магическое число + версия
    out.write(reinterpret_cast<const char*>(&MOLECULE_FILE_MAGIC),   sizeof(MOLECULE_FILE_MAGIC));
    out.write(reinterpret_cast<const char*>(&MOLECULE_FILE_VERSION), sizeof(MOLECULE_FILE_VERSION));

    // 2. Расширенный заголовок с метаданными единиц
    MoleculeFileHeader header{};
    CopyString8(header.timeUnit,   timeUnit);
    CopyString8(header.lengthUnit, lengthUnit);
    CopyString8(header.energyUnit, energyUnit);
    header.numSpecies = numSpecies;
    header.reserved   = 0;
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // 3. Количество записей
    uint64_t n = molecules.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));

    // 4. Данные
    if (n > 0) {
        out.write(reinterpret_cast<const char*>(molecules.data()),
                  n * sizeof(MoleculeRecord));
    }

    if (!out) {
        G4Exception("MoleculeIO::WriteBinary", "IO002", JustWarning, "Write failed");
        return false;
    }
    return true;
}

std::vector<MoleculeRecord> MoleculeIO::ReadBinary(const std::string& filename,
                                                    bool* versionMismatch,
                                                    MoleculeFileHeader* headerOut) {
    std::vector<MoleculeRecord> result;
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        G4Exception("MoleculeIO::ReadBinary", "IO003", JustWarning,
                    ("Cannot open file: " + filename).c_str());
        return result;
    }

    // 1. Магическое число + версия
    uint32_t magic = 0, version = 0;
    in.read(reinterpret_cast<char*>(&magic),   sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (magic != MOLECULE_FILE_MAGIC) {
        G4Exception("MoleculeIO::ReadBinary", "IO004", JustWarning,
                    "Invalid magic number – not a molecule file");
        return result;
    }
    if (versionMismatch)
        *versionMismatch = (version != MOLECULE_FILE_VERSION);

    // 2. Расширенный заголовок (v2+) или пропуск для v1
    MoleculeFileHeader header{};
    if (version >= 2) {
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
    }
    // v1: заголовка нет, оставляем header нулевым
    if (headerOut)
        *headerOut = header;

    // 3. Количество записей
    uint64_t n = 0;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (!in || n > 1e8) {
        G4Exception("MoleculeIO::ReadBinary", "IO005", JustWarning, "Invalid record count");
        return result;
    }

    // 4. Данные
    result.resize(n);
    if (n > 0) {
        // Для v1 запись была 16+24+8+4=52 байта (char[16]+3*double+double+int),
        // для v2 — 40 байт (int+3*double+double+int).
        if (version <= 1) {
            // Читаем v1-совместимые записи и конвертируем на лету
#pragma pack(push, 1)
            struct MoleculeRecordV1 {
                char    type[16];
                double  x, y, z;
                double  time;
                int32_t trackID;
            };
#pragma pack(pop)
            static_assert(sizeof(MoleculeRecordV1) == 52, "V1 size mismatch");
            std::vector<MoleculeRecordV1> tmp(n);
            in.read(reinterpret_cast<char*>(tmp.data()), n * sizeof(MoleculeRecordV1));
            result.clear();
            result.reserve(n);
            for (const auto& r : tmp) {
                MoleculeRecord rec;
                rec.speciesIndex = 0; // v1: индекс неизвестен
                rec.x = r.x;
                rec.y = r.y;
                rec.z = r.z;
                rec.time = r.time;
                rec.trackID = r.trackID;
                result.push_back(rec);
            }
        } else {
            in.read(reinterpret_cast<char*>(result.data()), n * sizeof(MoleculeRecord));
        }
    }

    if (!in) {
        G4Exception("MoleculeIO::ReadBinary", "IO006", JustWarning, "Read failed");
        result.clear();
    }
    return result;
}