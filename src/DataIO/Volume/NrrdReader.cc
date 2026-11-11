//==============================================================================
// G4CARE - NrrdReader.cc — NRRD volume reader.
//==============================================================================

#include "NrrdReader.hh"

#include <zlib.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Decompress a gzip stream with zlib.
bool gunzip(const char* src, std::size_t len, std::vector<char>& out) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return false;  // 15+16 => gzip
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(src));
    zs.avail_in = static_cast<uInt>(len);
    out.clear();
    char buf[65536];
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        out.insert(out.end(), buf, buf + (sizeof(buf) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return true;
}

std::vector<std::string> splitWs(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> parts;
    std::string tok;
    while (is >> tok) parts.push_back(tok);
    return parts;
}

}  // namespace
bool ReadNRRD(const std::string& file, CTSeries& out, std::string& err) {
    std::string content;
    if (!readFile(file, content)) {
        err = "Cannot read file: " + file;
        return false;
    }
    if (content.size() < 5 || content.compare(0, 4, "NRRD") != 0) {
        err = "Not an NRRD file: " + file;
        return false;
    }

    // Parse the text header line by line until a blank line.
    std::map<std::string, std::string> fields;
    std::size_t pos = 0;
    bool headerDone = false;
    while (pos < content.size() && !headerDone) {
        std::size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        std::string line = content.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = nl + 1;

        if (line.empty()) { headerDone = true; break; }
        if (line[0] == '#') continue;
        if (line.compare(0, 4, "NRRD") == 0) continue;

        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::size_t v0 = val.find_first_not_of(" \t");
        if (v0 != std::string::npos) val = val.substr(v0);
        fields[key] = val;
    }
    if (!headerDone) {
        err = "NRRD header not terminated (no blank line)";
        return false;
    }

    std::vector<std::string> sizesTok = splitWs(fields["sizes"]);
    if (sizesTok.empty()) { err = "NRRD missing 'sizes'"; return false; }
    const int dim = std::atoi(fields["dimension"].c_str());
    const int nx = std::atoi(sizesTok[0].c_str());
    const int ny = sizesTok.size() > 1 ? std::atoi(sizesTok[1].c_str()) : 1;
    const int nz = sizesTok.size() > 2 ? std::atoi(sizesTok[2].c_str()) : 1;
    if (nx <= 0 || ny <= 0 || nz <= 0) { err = "NRRD invalid sizes"; return false; }

    const std::string type = fields["type"];
    const std::string encoding = fields["encoding"];

    out = CTSeries{};
    out.nx = nx; out.ny = ny; out.nz = nz;
    out.modality = "NRRD";
    out.rescaleSlope = 1.0;
    out.rescaleIntercept = 0.0;

    // Space directions -> spacing + direction cosines.
    std::vector<std::string> dirsTok = splitWs(fields["space directions"]);
    auto parseVec = [](const std::string& s, double& x, double& y, double& z) {
        std::string t = s;
        for (auto& c : t) if (c == '(' || c == ')' || c == ',') c = ' ';
        std::vector<std::string> p = splitWs(t);
        if (p.size() < 3) return false;
        x = std::atof(p[0].c_str());
        y = std::atof(p[1].c_str());
        z = std::atof(p[2].c_str());
        return true;
    };
    double dirs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const std::size_t needDirs = (dim >= 3) ? 3 : 2;
    if (dirsTok.size() >= needDirs) {
        for (std::size_t i = 0; i < needDirs; ++i)
            parseVec(dirsTok[i], dirs[i][0], dirs[i][1], dirs[i][2]);
    }
    out.dxMm = std::sqrt(dirs[0][0]*dirs[0][0] + dirs[0][1]*dirs[0][1] + dirs[0][2]*dirs[0][2]);
    out.dyMm = std::sqrt(dirs[1][0]*dirs[1][0] + dirs[1][1]*dirs[1][1] + dirs[1][2]*dirs[1][2]);
    out.dzMm = std::sqrt(dirs[2][0]*dirs[2][0] + dirs[2][1]*dirs[2][1] + dirs[2][2]*dirs[2][2]);
    for (int i = 0; i < 3; ++i) {
        const double m = std::sqrt(dirs[i][0]*dirs[i][0] + dirs[i][1]*dirs[i][1] + dirs[i][2]*dirs[i][2]);
        if (m > 1e-12) {
            out.direction[i*3+0] = dirs[i][0] / m;
            out.direction[i*3+1] = dirs[i][1] / m;
            out.direction[i*3+2] = dirs[i][2] / m;
        }
    }

    // Space origin.
    {
        std::string so = fields["space origin"];
        for (auto& c : so) if (c == '(' || c == ')' || c == ',') c = ' ';
        std::vector<std::string> p = splitWs(so);
        if (p.size() >= 3) {
            out.originMm[0] = std::atof(p[0].c_str());
            out.originMm[1] = std::atof(p[1].c_str());
            out.originMm[2] = std::atof(p[2].c_str());
        }
    }

    // Decode data (gzip or raw).
    const char* rawData = content.data() + pos;
    const std::size_t rawLen = content.size() - pos;
    std::vector<char> data;
    if (encoding == "gzip") {
        if (!gunzip(rawData, rawLen, data)) {
            err = "NRRD gzip decompression failed";
            return false;
        }
    } else if (encoding == "raw" || encoding.empty()) {
        data.assign(rawData, rawData + rawLen);
    } else {
        err = "NRRD unsupported encoding: " + encoding;
        return false;
    }

    const std::size_t n = static_cast<std::size_t>(nx) * ny * nz;

    // Interpret the voxel data by type.
    out.floatData = (type == "float" || type == "double");
    if (out.floatData) {
        out.floatValues.resize(n);
        if (type == "float") {
            if (data.size() < n * sizeof(float)) { err = "NRRD data too short"; return false; }
            const float* p = reinterpret_cast<const float*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.floatValues[i] = p[i];
        } else {
            if (data.size() < n * sizeof(double)) { err = "NRRD data too short"; return false; }
            const double* p = reinterpret_cast<const double*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.floatValues[i] = static_cast<float>(p[i]);
        }
    } else {
        out.hu.resize(n);
        if (type == "short") {
            if (data.size() < n * sizeof(short)) { err = "NRRD data too short"; return false; }
            const short* p = reinterpret_cast<const short*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = p[i];
        } else if (type == "ushort") {
            if (data.size() < n * sizeof(unsigned short)) { err = "NRRD data too short"; return false; }
            const unsigned short* p = reinterpret_cast<const unsigned short*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = static_cast<short>(p[i]);
        } else if (type == "char") {
            if (data.size() < n) { err = "NRRD data too short"; return false; }
            const signed char* p = reinterpret_cast<const signed char*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = p[i];
        } else if (type == "uchar" || type == "unsigned char") {
            if (data.size() < n) { err = "NRRD data too short"; return false; }
            const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = p[i];
        } else if (type == "int" || type == "uint") {
            out.floatData = true;
            out.floatValues.resize(n);
            const int* p = reinterpret_cast<const int*>(data.data());
            for (std::size_t i = 0; i < n; ++i) out.floatValues[i] = static_cast<float>(p[i]);
            out.hu.clear();
        } else {
            err = "NRRD unsupported type: " + type;
            return false;
        }
    }

    out.valid = true;
    return true;
}

bool ReadNIfTI(const std::string& file, CTSeries& out, std::string& err) {
    std::string raw;
    if (!readFile(file, raw)) { err = "Cannot read file: " + file; return false; }

    std::string content;
    const bool isGz = file.size() > 3 && file.substr(file.size() - 3) == ".gz";
    if (isGz) {
        std::vector<char> dec;
        if (!gunzip(raw.data(), raw.size(), dec)) { err = "NIfTI gzip decompression failed"; return false; }
        content.assign(dec.data(), dec.size());
    } else {
        content = raw;
    }
    if (content.size() < 348) { err = "NIfTI header too short"; return false; }
    const char* h = content.data();

    auto rdI16 = [&](std::size_t off) { std::int16_t v; std::memcpy(&v, h + off, 2); return v; };
    auto rdF32 = [&](std::size_t off) { float v; std::memcpy(&v, h + off, 4); return v; };

    const int ndim = rdI16(40);
    if (ndim < 2 || ndim > 3) { err = "NIfTI unsupported ndim"; return false; }
    const int nx = rdI16(42);
    const int ny = rdI16(44);
    const int nz = (ndim >= 3) ? rdI16(46) : 1;
    const int datatype = rdI16(70);
    const float dx = rdF32(80), dy = rdF32(84), dz = rdF32(88);  // pixdim[1..3]
    const float sclSlope = rdF32(112);
    const float sclInter = rdF32(116);
    const std::int16_t sformCode = rdI16(254);

    out = CTSeries{};
    out.nx = nx; out.ny = ny; out.nz = nz;
    out.dxMm = dx > 0 ? dx : 1;
    out.dyMm = dy > 0 ? dy : 1;
    out.dzMm = dz > 0 ? dz : 1;
    out.modality = "NIFTI";
    out.rescaleSlope = sclSlope != 0 ? sclSlope : 1;
    out.rescaleIntercept = sclInter;

    if (sformCode > 0) {
        // srow_* rows: 4 floats each; translation is the 4th element.
        out.originMm[0] = rdF32(280 + 12);
        out.originMm[1] = rdF32(296 + 12);
        out.originMm[2] = rdF32(312 + 12);
        for (int r = 0; r < 3; ++r) {
            const float r0 = rdF32(280 + r * 16 + 0);
            const float r1 = rdF32(280 + r * 16 + 4);
            const float r2 = rdF32(280 + r * 16 + 8);
            const float m = std::sqrt(r0 * r0 + r1 * r1 + r2 * r2);
            if (m > 1e-9f) {
                out.direction[r * 3 + 0] = r0 / m;
                out.direction[r * 3 + 1] = r1 / m;
                out.direction[r * 3 + 2] = r2 / m;
            }
        }
    } else {
        out.originMm[0] = rdF32(268);  // qoffset_x
        out.originMm[1] = rdF32(272);
        out.originMm[2] = rdF32(276);
    }

    const float voxOff = rdF32(108);
    const std::size_t dataOff = voxOff > 0 ? static_cast<std::size_t>(voxOff) : 352;
    if (dataOff > content.size()) { err = "NIfTI vox_offset out of range"; return false; }
    const char* data = content.data() + dataOff;
    const std::size_t n = static_cast<std::size_t>(nx) * ny * nz;
    const double slope = out.rescaleSlope, inter = out.rescaleIntercept;

    out.floatData = (datatype == 16 || datatype == 64);
    if (out.floatData) {
        out.floatValues.resize(n);
        if (datatype == 16) {
            const float* p = reinterpret_cast<const float*>(data);
            for (std::size_t i = 0; i < n; ++i) out.floatValues[i] = static_cast<float>(slope * p[i] + inter);
        } else {
            const double* p = reinterpret_cast<const double*>(data);
            for (std::size_t i = 0; i < n; ++i) out.floatValues[i] = static_cast<float>(slope * p[i] + inter);
        }
    } else {
        out.hu.resize(n);
        if (datatype == 4) {
            const std::int16_t* p = reinterpret_cast<const std::int16_t*>(data);
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = static_cast<short>(slope * p[i] + inter);
        } else if (datatype == 2) {
            const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = static_cast<short>(slope * p[i] + inter);
        } else if (datatype == 256) {
            const std::int8_t* p = reinterpret_cast<const std::int8_t*>(data);
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = static_cast<short>(slope * p[i] + inter);
        } else if (datatype == 512) {
            const std::uint16_t* p = reinterpret_cast<const std::uint16_t*>(data);
            for (std::size_t i = 0; i < n; ++i) out.hu[i] = static_cast<short>(slope * p[i] + inter);
        } else {
            out.floatData = true;
            out.floatValues.resize(n);
            out.hu.clear();
            const std::int32_t* p = reinterpret_cast<const std::int32_t*>(data);
            for (std::size_t i = 0; i < n; ++i) out.floatValues[i] = static_cast<float>(slope * p[i] + inter);
        }
    }
    out.valid = true;
    return true;
}
