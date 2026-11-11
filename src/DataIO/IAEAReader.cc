#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "IAEAReader.hh"
#include "iaea_config.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <G4ios.hh>
#include <sys/types.h>

static inline bool isLittleEndian() {
    static const uint16_t test = 0x1;
    return *reinterpret_cast<const uint8_t*>(&test) == 0x1;
}

template<typename T>
static T maybeSwapBytes(T value, bool needSwap) {
    if (!needSwap) return value;
    T result;
    uint8_t* src = reinterpret_cast<uint8_t*>(&value);
    uint8_t* dst = reinterpret_cast<uint8_t*>(&result);
    for (size_t i = 0; i < sizeof(T); ++i)
        dst[i] = src[sizeof(T)-1-i];
    return result;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos)
        return "";
    return s.substr(start, end-start+1);
}

static std::string stripComment(const std::string& line) {
    size_t pos = line.find("//");
    if (pos != std::string::npos)
        return line.substr(0, pos);
    return line;
}

IAEAReader::IAEAReader() {

}

IAEAReader::~IAEAReader() {

    ThreadLocalState* tls = fStateCache.Get();
    if (tls) {
        delete tls;
        fStateCache.Put(nullptr);
    }

}

bool IAEAReader::Load(const std::string& filename) {
    std::string headerFile = filename;
    if (headerFile.size() <= 11 || headerFile.substr(headerFile.size()-11) != ".IAEAheader") {
        if (headerFile.find('.') == std::string::npos)
            headerFile += ".IAEAheader";
        else {
            size_t dot = headerFile.rfind('.');
            headerFile = headerFile.substr(0, dot) + ".IAEAheader";
        }
    }
    if (!ParseHeader(headerFile))
        return false;

    fDataFilename = headerFile;
    size_t pos = fDataFilename.rfind(".IAEAheader");
    if (pos != std::string::npos)
        fDataFilename.replace(pos, 11, ".IAEAphsp");
    else
        fDataFilename = filename + ".IAEAphsp";

    return true;
}

bool IAEAReader::ParseHeader(const std::string& headerFilename) {
    FILE* fh = fopen(headerFilename.c_str(), "r");
    if (!fh) {
        std::cerr << "IAEAReader: cannot open header file " << headerFilename << std::endl;
        return false;
    }

    char line[512];
    bool ok = true;

    auto findBlock = [&](const std::string& blockName) -> bool {
        rewind(fh);
        std::string target = "$" + blockName + ":";
        while (fgets(line, sizeof(line), fh)) {
            std::string s = stripComment(line);
            s = trim(s);

            if (s.size() >= target.size() &&
                strncasecmp(s.c_str(), target.c_str(), target.size()) == 0)
                return true;
        }
        return false;
    };

    auto readNumber = [&](double& val) -> bool {
        while (fgets(line, sizeof(line), fh)) {
            std::string s = stripComment(line);
            s = trim(s);
            if (s.empty()) continue;
            char* end;
            val = strtod(s.c_str(), &end);
            if (end != s.c_str()) return true;
        }
        return false;
    };

    double tmp_len;
    if (!findBlock("RECORD_LENGTH") || !readNumber(tmp_len)) {
        std::cerr << "IAEAReader: missing or invalid RECORD_LENGTH" << std::endl;
        ok = false; goto cleanup;
    }
    fRecordLength = static_cast<size_t>(tmp_len);

    if (!findBlock("RECORD_CONTENTS")) {
        std::cerr << "IAEAReader: missing RECORD_CONTENTS block" << std::endl;
        ok = false; goto cleanup;
    }
    for (int i = 0; i < 9; ++i) {
        double tmp;
        if (!readNumber(tmp)) {
            std::cerr << "IAEAReader: not enough numbers in RECORD_CONTENTS" << std::endl;
            ok = false; goto cleanup;
        }
        fRecordContents[i] = static_cast<int>(tmp);
    }
    if (fVerbose > 0) {
        G4cout << "IAEAReader: RECORD_CONTENTS =";
        for (int i=0;i<9;i++) G4cout << " " << fRecordContents[i];
        G4cout << G4endl;
    }

    fNumExtraFloats = fRecordContents[7];
    fNumExtraLongs  = fRecordContents[8];

    if (findBlock("BYTE_ORDER")) {
        double tmp;
        if (readNumber(tmp)) {
            fByteOrder = static_cast<int>(tmp);
            if (fVerbose > 0)
                G4cout << "IAEAReader: BYTE_ORDER = " << fByteOrder << G4endl;
        }
    }

    if (findBlock("RECORD_CONSTANT")) {
        for (int i = 0; i < 7; ++i) {
            if (fRecordContents[i] == 0) {
                double tmp;
                if (!readNumber(tmp)) {
                    std::cerr << "IAEAReader: missing constant for index " << i << std::endl;
                    ok = false; goto cleanup;
                }
                fRecordConstant[i] = static_cast<float>(tmp);
            }
        }
    }


    {
        int storedFields = 0;
        for (int i = 0; i < 7; ++i) if (fRecordContents[i] == 1) storedFields++;
        size_t minSize = 5 + storedFields * 4;
        if (minSize + fNumExtraFloats*4 + fNumExtraLongs*4 > fRecordLength) {
            G4cout << "IAEAReader: WARNING: extra fields declared but not enough space. Ignoring extra fields." << G4endl;
            fNumExtraFloats = 0;
            fNumExtraLongs = 0;
        }
    }

    double tmp_particles;
    if (!findBlock("PARTICLES") || !readNumber(tmp_particles)) {
        std::cerr << "IAEAReader: missing or invalid PARTICLES" << std::endl;
        ok = false; goto cleanup;
    }
    fNumParticles = static_cast<size_t>(tmp_particles);

cleanup:
    fclose(fh);
    return ok;
}

size_t IAEAReader::GetNumberOfColumns() const noexcept {
    return 9 + fNumExtraFloats + fNumExtraLongs;
}

std::vector<std::string> IAEAReader::GetColumnNames() const {
    std::vector<std::string> names = {
        "particle_type", "energy",
        "x", "y", "z", "u", "v", "w", "weight"
    };
    for (int i = 0; i < fNumExtraFloats; ++i)
        names.push_back("extra_float_" + std::to_string(i));
    for (int i = 0; i < fNumExtraLongs; ++i)
        names.push_back("extra_long_" + std::to_string(i));
    return names;
}

bool IAEAReader::ReadRecord(size_t row) const {

    ThreadLocalState* tls = fStateCache.Get();
    if (!tls) {
        tls = new ThreadLocalState();

        tls->currentBuffer.resize(fRecordLength);
        fStateCache.Put(tls);
    }

    if (tls->currentRow == row && !tls->currentValues.empty())
        return true;

    if (!tls->dataFile) {
        tls->dataFile = fopen(fDataFilename.c_str(), "rb");
        if (!tls->dataFile) {
            std::cerr << "IAEAReader: cannot open data file " << fDataFilename << " in thread" << std::endl;
            return false;
        }
    }

    off_t offset = static_cast<off_t>(row) * static_cast<off_t>(fRecordLength);
    if (fseeko(tls->dataFile, offset, SEEK_SET) != 0) {
        std::cerr << "IAEAReader: failed to seek to row " << row << " (offset " << offset << ")" << std::endl;
        return false;
    }

    if (fread(tls->currentBuffer.data(), 1, fRecordLength, tls->dataFile) != fRecordLength) {
        std::cerr << "IAEAReader: failed to read " << fRecordLength << " bytes at row " << row << std::endl;
        return false;
    }

    tls->currentRow = row;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(tls->currentBuffer.data());

    static bool hostIsLittle = isLittleEndian();
    bool needSwap = (hostIsLittle && fByteOrder == 4321) || (!hostIsLittle && fByteOrder == 1234);

    int8_t particle_type_signed = *reinterpret_cast<const int8_t*>(ptr);
    ptr += 1;
    bool w_negative = (particle_type_signed < 0);
    int particle_type = std::abs(particle_type_signed);

    uint32_t energy_bytes;
    memcpy(&energy_bytes, ptr, 4); ptr += 4;
    energy_bytes = maybeSwapBytes(energy_bytes, needSwap);
    float energy;
    memcpy(&energy, &energy_bytes, 4);
    energy = std::fabs(energy);

    float x = fRecordConstant[0];
    float y = fRecordConstant[1];
    float z = fRecordConstant[2];
    float u = fRecordConstant[3];
    float v = fRecordConstant[4];
    float w = 0.0f;
    float weight = fRecordConstant[6];

    if (fRecordContents[0] == 1) { // X
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&x, &tmp, 4);
    }
    if (fRecordContents[1] == 1) { // Y
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&y, &tmp, 4);
    }
    if (fRecordContents[2] == 1) { // Z
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&z, &tmp, 4);
    }
    if (fRecordContents[3] == 1) { // U
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&u, &tmp, 4);
    }
    if (fRecordContents[4] == 1) { // V
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&v, &tmp, 4);
    }

    if (fRecordContents[6] == 1) {
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&weight, &tmp, 4);
    }

    if (fRecordContents[5] == 1) {
        ptr += 4;
    }

    double uv2 = static_cast<double>(u)*u + static_cast<double>(v)*v;
    
    const double epsilon = 1e-12;
    if (uv2 > 1.0 + epsilon) {

        double norm = std::sqrt(uv2);
        u = static_cast<float>(u / norm);
        v = static_cast<float>(v / norm);
        w = 0.0f;
    } else if (uv2 > 1.0) {

        w = 0.0f;
    } else {
        double one_minus_uv2 = 1.0 - uv2;
        double w_abs = std::sqrt(one_minus_uv2);
        w = (w_negative ? -w_abs : w_abs);
    }

    std::vector<float> extraFloats(fNumExtraFloats);
    for (int i = 0; i < fNumExtraFloats; ++i) {
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&extraFloats[i], &tmp, 4);
    }

    std::vector<int32_t> extraLongs(fNumExtraLongs);
    for (int i = 0; i < fNumExtraLongs; ++i) {
        uint32_t tmp; memcpy(&tmp, ptr, 4); ptr += 4;
        tmp = maybeSwapBytes(tmp, needSwap);
        memcpy(&extraLongs[i], &tmp, 4);
    }

    size_t bytesRead = ptr - reinterpret_cast<const uint8_t*>(tls->currentBuffer.data());
    if (bytesRead != fRecordLength) {
        std::cerr << "ERROR: bytes read (" << bytesRead << ") != record length (" << fRecordLength << ")" << std::endl;
        return false;
    }

    tls->currentValues.clear();
    tls->currentValues.reserve(9 + fNumExtraFloats + fNumExtraLongs);
    tls->currentValues.push_back(static_cast<float>(particle_type));
    tls->currentValues.push_back(energy);
    tls->currentValues.push_back(x);
    tls->currentValues.push_back(y);
    tls->currentValues.push_back(z);
    tls->currentValues.push_back(u);
    tls->currentValues.push_back(v);
    tls->currentValues.push_back(w);
    tls->currentValues.push_back(weight);
    for (float ef : extraFloats) tls->currentValues.push_back(ef);
    for (int32_t el : extraLongs) tls->currentValues.push_back(static_cast<float>(el));

    if (fVerbose > 1) {
        G4cout << "IAEAReader row " << row
               << ": type=" << particle_type
               << " E=" << energy
               << " x=" << x << " y=" << y << " z=" << z
               << " u=" << u << " v=" << v << " w=" << w
               << " wt=" << weight
               << " extra=" << (extraLongs.empty() ? -1 : extraLongs[0])
               << G4endl;
    }

    return true;
}

std::optional<double> IAEAReader::GetCell(size_t row, size_t col) const {
    if (!ReadRecord(row))
        return std::nullopt;
    ThreadLocalState* tls = fStateCache.Get();
    if (!tls || col >= tls->currentValues.size())
        return std::nullopt;
    return tls->currentValues[col];
}

std::optional<std::vector<double>> IAEAReader::GetRow(size_t row) const {
    if (!ReadRecord(row))
        return std::nullopt;
    ThreadLocalState* tls = fStateCache.Get();
    if (!tls) return std::nullopt;
    return tls->currentValues;
}