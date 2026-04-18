#pragma once

#include <vtkDataSet.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "CAEInterfaceTypes.h"
#include "DataObject.h"
#include "VTKDataConverter.h"

namespace testharness
{
inline std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
        value.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

inline bool parseBoolSwitch(std::string value, bool& out)
{
    value = toLower(std::move(value));
    if (value == "1" || value == "on" || value == "true" ||
        value == "yes" || value == "enable" || value == "enabled") {
        out = true;
        return true;
    }
    if (value == "0" || value == "off" || value == "false" ||
        value == "no" || value == "disable" || value == "disabled") {
        out = false;
        return true;
    }
    return false;
}

inline bool parsePositiveInt(const std::string& value, int& out)
{
    char* endPtr = nullptr;
    const long parsed = std::strtol(value.c_str(), &endPtr, 10);
    if (!endPtr || *endPtr != '\0' || parsed < 0 ||
        parsed > static_cast<long>(std::numeric_limits<int>::max())) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

inline double percentile(std::vector<double> values, double p)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    const double t = pos - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}

inline std::string csvEscape(const std::string& value)
{
    const bool needsQuotes = value.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }

    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

inline bool buildRegularNeighbors(int nx, int ny, int nz, std::vector<int>& offsets, std::vector<int>& neighbors)
{
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        return false;
    }

    auto idx = [nx, ny](int i, int j, int k) {
        return (k * ny + j) * nx + i;
    };

    offsets.clear();
    neighbors.clear();
    offsets.reserve(static_cast<size_t>(nx) * ny * nz + 1);
    offsets.push_back(0);

    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                if (i > 0)       neighbors.push_back(idx(i - 1, j, k));
                if (i + 1 < nx)  neighbors.push_back(idx(i + 1, j, k));
                if (j > 0)       neighbors.push_back(idx(i, j - 1, k));
                if (j + 1 < ny)  neighbors.push_back(idx(i, j + 1, k));
                if (k > 0)       neighbors.push_back(idx(i, j, k - 1));
                if (k + 1 < nz)  neighbors.push_back(idx(i, j, k + 1));
                offsets.push_back(static_cast<int>(neighbors.size()));
            }
        }
    }

    return true;
}

inline bool buildAssociationGraph(const DataObject& data,
                                  CAEFieldAssociation assoc,
                                  std::vector<float>& positions,
                                  std::vector<int>& offsets,
                                  std::vector<int>& neighbors)
{
    positions.clear();
    offsets.clear();
    neighbors.clear();

    if (data.gridType == DATA_OBJECT_TYPE_RegularGrid) {
        if (assoc == CAEFieldAssociation::Point) {
            positions = data.points;
            return buildRegularNeighbors(data.dimensions[0], data.dimensions[1], data.dimensions[2], offsets, neighbors);
        }

        const int nx = data.dimensions[0] - 1;
        const int ny = data.dimensions[1] - 1;
        const int nz = data.dimensions[2] - 1;
        if (nx <= 0 || ny <= 0 || nz <= 0) {
            return false;
        }
        positions = data.cellCenters;
        if (positions.size() != static_cast<size_t>(nx) * ny * nz * 3u) {
            return false;
        }
        return buildRegularNeighbors(nx, ny, nz, offsets, neighbors);
    }

    if (assoc == CAEFieldAssociation::Point) {
        positions = data.points;
        offsets = data.pointNeighborOffsets;
        neighbors = data.pointNeighbors;
    } else {
        positions = data.cellCenters;
        offsets = data.cellNeighborsOffsets;
        neighbors = data.cellNeighbors;
    }

    return !positions.empty() && offsets.size() == positions.size() / 3u + 1u;
}

inline bool buildDataObjectFromVtk(vtkDataSet* dataset, DataObject& outData)
{
    if (!dataset) {
        return false;
    }
    VTKDataConverter conv;
    conv.bindVTKDataAndInternalData(dataset, &outData);
    return conv.convertVTKToInternal() != 0;
}

inline double computeStdDev(const std::vector<float>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    double mean = 0.0;
    for (float v : values) {
        mean += static_cast<double>(v);
    }
    mean /= static_cast<double>(values.size());

    double var = 0.0;
    for (float v : values) {
        const double d = static_cast<double>(v) - mean;
        var += d * d;
    }
    var /= static_cast<double>(values.size());
    return std::sqrt(var);
}

inline double computeSignalRms(const std::vector<float>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    double sumSq = 0.0;
    for (float v : values) {
        const double dv = static_cast<double>(v);
        sumSq += dv * dv;
    }
    return std::sqrt(sumSq / static_cast<double>(values.size()));
}

inline double computeMeanAbsDelta(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    }
    return sum / static_cast<double>(a.size());
}

inline double computeGraphRoughness(const std::vector<float>& values,
                                    int comps,
                                    const std::vector<int>& offsets,
                                    const std::vector<int>& neighbors)
{
    if (values.empty() || comps <= 0 || offsets.size() < 2) {
        return 0.0;
    }

    const size_t tupleCount = values.size() / static_cast<size_t>(comps);
    if (offsets.size() != tupleCount + 1) {
        return 0.0;
    }

    double sum = 0.0;
    size_t cnt = 0;

    for (size_t i = 0; i < tupleCount; ++i) {
        for (int k = offsets[i]; k < offsets[i + 1]; ++k) {
            const int j = neighbors[k];
            if (j < 0 || static_cast<size_t>(j) >= tupleCount) {
                continue;
            }
            if (static_cast<size_t>(j) <= i) {
                continue;
            }
            for (int c = 0; c < comps; ++c) {
                const double vi = values[i * static_cast<size_t>(comps) + static_cast<size_t>(c)];
                const double vj = values[static_cast<size_t>(j) * static_cast<size_t>(comps) + static_cast<size_t>(c)];
                sum += std::abs(vi - vj);
                ++cnt;
            }
        }
    }

    return cnt > 0 ? (sum / static_cast<double>(cnt)) : 0.0;
}

struct BoundsInfo
{
    bool valid = false;
    std::array<double, 3> min{ 0.0, 0.0, 0.0 };
    std::array<double, 3> max{ 0.0, 0.0, 0.0 };
    std::array<double, 3> extent{ 0.0, 0.0, 0.0 };
    std::array<double, 3> center{ 0.0, 0.0, 0.0 };
    double maxExtent = 1.0;
};

inline BoundsInfo computeBounds(const std::vector<float>& positions)
{
    BoundsInfo out;
    const size_t tupleCount = positions.size() / 3u;
    if (tupleCount == 0 || positions.size() != tupleCount * 3u) {
        return out;
    }

    out.valid = true;
    for (int axis = 0; axis < 3; ++axis) {
        out.min[axis] = static_cast<double>(positions[static_cast<size_t>(axis)]);
        out.max[axis] = out.min[axis];
    }

    for (size_t i = 1; i < tupleCount; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            const double v = static_cast<double>(positions[i * 3u + static_cast<size_t>(axis)]);
            out.min[axis] = std::min(out.min[axis], v);
            out.max[axis] = std::max(out.max[axis], v);
        }
    }

    out.maxExtent = 1.0;
    for (int axis = 0; axis < 3; ++axis) {
        out.extent[axis] = out.max[axis] - out.min[axis];
        out.center[axis] = 0.5 * (out.min[axis] + out.max[axis]);
        out.maxExtent = std::max(out.maxExtent, out.extent[axis]);
    }
    return out;
}

struct AnalyticBenchmarkFrame
{
    std::array<double, 3> center{ 0.0, 0.0, 0.0 };
    double refLength = 1.0;
};

inline bool buildAnalyticBenchmarkFrame(const std::vector<float>& positions, AnalyticBenchmarkFrame& frame)
{
    const BoundsInfo bounds = computeBounds(positions);
    if (!bounds.valid) {
        return false;
    }
    frame.center = bounds.center;
    frame.refLength = std::max(bounds.maxExtent, 1.0);
    return true;
}

constexpr double kGeomEps = 1e-12;

inline std::array<double, 3> loadPosition(const std::vector<float>& positions, size_t index)
{
    return {
        static_cast<double>(positions[index * 3u + 0]),
        static_cast<double>(positions[index * 3u + 1]),
        static_cast<double>(positions[index * 3u + 2])
    };
}

inline std::array<double, 3> sub3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

inline double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline std::array<double, 3> cross3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

inline double norm3(const std::array<double, 3>& a)
{
    return std::sqrt(dot3(a, a));
}

inline std::array<double, 3> normalize3(const std::array<double, 3>& a, const std::array<double, 3>& fallback)
{
    const double n = norm3(a);
    if (n <= kGeomEps) {
        return fallback;
    }
    return { a[0] / n, a[1] / n, a[2] / n };
}

inline std::array<double, 3> arbitraryPerpendicular(const std::array<double, 3>& a)
{
    const std::array<double, 3> axisX{ 1.0, 0.0, 0.0 };
    const std::array<double, 3> axisY{ 0.0, 1.0, 0.0 };
    std::array<double, 3> c = cross3(a, axisX);
    if (norm3(c) <= kGeomEps) {
        c = cross3(a, axisY);
    }
    return normalize3(c, axisY);
}

inline void orthonormalizeFrame(std::array<double, 3>& e1,
                                std::array<double, 3>& e2,
                                std::array<double, 3>& e3)
{
    e1 = normalize3(e1, { 1.0, 0.0, 0.0 });
    const double p12 = dot3(e2, e1);
    e2 = { e2[0] - p12 * e1[0], e2[1] - p12 * e1[1], e2[2] - p12 * e1[2] };
    e2 = normalize3(e2, arbitraryPerpendicular(e1));
    e3 = normalize3(cross3(e1, e2), arbitraryPerpendicular(e1));
    e2 = normalize3(cross3(e3, e1), e2);
}

inline void rotateJacobi(double a[3][3], double v[3][3], int p, int q)
{
    if (std::abs(a[p][q]) <= kGeomEps) {
        return;
    }

    const double tau = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
    const double t = (tau >= 0.0 ? 1.0 : -1.0) /
        (std::abs(tau) + std::sqrt(1.0 + tau * tau));
    const double c = 1.0 / std::sqrt(1.0 + t * t);
    const double s = t * c;

    const double app = a[p][p];
    const double aqq = a[q][q];
    const double apq = a[p][q];

    a[p][p] = app - t * apq;
    a[q][q] = aqq + t * apq;
    a[p][q] = 0.0;
    a[q][p] = 0.0;

    for (int r = 0; r < 3; ++r) {
        if (r == p || r == q) {
            continue;
        }
        const double arp = a[r][p];
        const double arq = a[r][q];
        a[r][p] = arp - s * (arq + t * arp);
        a[p][r] = a[r][p];
        a[r][q] = arq + s * (arp - t * arq);
        a[q][r] = a[r][q];
    }

    for (int r = 0; r < 3; ++r) {
        const double vrp = v[r][p];
        const double vrq = v[r][q];
        v[r][p] = c * vrp - s * vrq;
        v[r][q] = s * vrp + c * vrq;
    }
}

struct EigenFrameInfo
{
    std::array<float, 9> frame{
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    std::array<double, 3> eigenValues{ 1.0, 1.0, 1.0 };
};

inline void eigenSymmetric3(const double cov[3][3], EigenFrameInfo& out)
{
    double a[3][3] = {
        { cov[0][0], cov[0][1], cov[0][2] },
        { cov[1][0], cov[1][1], cov[1][2] },
        { cov[2][0], cov[2][1], cov[2][2] }
    };
    double v[3][3] = {
        { 1.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 1.0 }
    };

    for (int iter = 0; iter < 12; ++iter) {
        rotateJacobi(a, v, 0, 1);
        rotateJacobi(a, v, 0, 2);
        rotateJacobi(a, v, 1, 2);
    }

    std::array<int, 3> order{ 0, 1, 2 };
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        return a[lhs][lhs] > a[rhs][rhs];
    });

    std::array<double, 3> e1{ v[0][order[0]], v[1][order[0]], v[2][order[0]] };
    std::array<double, 3> e2{ v[0][order[1]], v[1][order[1]], v[2][order[1]] };
    std::array<double, 3> e3{ v[0][order[2]], v[1][order[2]], v[2][order[2]] };
    orthonormalizeFrame(e1, e2, e3);

    out.eigenValues = {
        std::max(a[order[0]][order[0]], 0.0),
        std::max(a[order[1]][order[1]], 0.0),
        std::max(a[order[2]][order[2]], 0.0)
    };

    const std::array<double, 9> frameD{
        e1[0], e1[1], e1[2],
        e2[0], e2[1], e2[2],
        e3[0], e3[1], e3[2]
    };
    for (size_t i = 0; i < frameD.size(); ++i) {
        out.frame[i] = static_cast<float>(frameD[i]);
    }
}

inline int vtkCellDimension(int cellType)
{
    switch (cellType) {
    case 1:
    case 2:
        return 0;
    case 3:
    case 4:
    case 21:
        return 1;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 22:
    case 23:
        return 2;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
        return 3;
    default:
        return 3;
    }
}

inline int inferTopologicalDimension(const DataObject& data)
{
    if (data.gridType == DATA_OBJECT_TYPE_RegularGrid) {
        return 3;
    }
    if (data.cellTypes.empty()) {
        return 3;
    }
    int maxDim = 0;
    for (int cellType : data.cellTypes) {
        maxDim = std::max(maxDim, vtkCellDimension(cellType));
    }
    return maxDim;
}

struct GeometryAnalysis
{
    bool available = false;
    BoundsInfo bounds;
    int topologicalDim = 3;
    int globalGeometricDim = 3;
    bool surfaceLike = false;
    std::vector<float> positions;
    std::vector<int> offsets;
    std::vector<int> neighbors;
    std::vector<float> frames;
    std::vector<std::uint32_t> dimTags;
    std::vector<float> meanNeighborDistance;
    std::array<double, 3> globalEigenRatios{ 1.0, 1.0, 1.0 };
};

inline int inferGeometricDimensionFromEigenvalues(const std::array<double, 3>& values,
                                                  double planeEigenRatio = 0.06,
                                                  double lineEigenRatio = 0.02)
{
    const double l1 = std::max(values[0], kGeomEps);
    const double l2 = std::max(values[1], 0.0);
    const double l3 = std::max(values[2], 0.0);
    const double r21 = l2 / l1;
    const double r31 = l3 / l1;
    if (r21 < std::max(lineEigenRatio, 1e-6)) {
        return 1;
    }
    if (r31 < std::max(planeEigenRatio, 1e-6)) {
        return 2;
    }
    return 3;
}

inline GeometryAnalysis analyzeGeometry(const DataObject& data,
                                        CAEFieldAssociation assoc,
                                        double planeEigenRatio = 0.06,
                                        double lineEigenRatio = 0.02)
{
    GeometryAnalysis out;
    out.topologicalDim = inferTopologicalDimension(data);
    if (!buildAssociationGraph(data, assoc, out.positions, out.offsets, out.neighbors)) {
        return out;
    }

    out.available = true;
    out.bounds = computeBounds(out.positions);
    out.frames.resize((out.positions.size() / 3u) * 9u, 0.0f);
    out.dimTags.resize(out.positions.size() / 3u, 3u);
    out.meanNeighborDistance.resize(out.positions.size() / 3u, 1.0f);

    double cov[3][3] = {
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 }
    };
    for (size_t i = 0; i < out.positions.size() / 3u; ++i) {
        const std::array<double, 3> p = loadPosition(out.positions, i);
        const std::array<double, 3> d = sub3(p, out.bounds.center);
        cov[0][0] += d[0] * d[0];
        cov[0][1] += d[0] * d[1];
        cov[0][2] += d[0] * d[2];
        cov[1][1] += d[1] * d[1];
        cov[1][2] += d[1] * d[2];
        cov[2][2] += d[2] * d[2];
    }
    cov[1][0] = cov[0][1];
    cov[2][0] = cov[0][2];
    cov[2][1] = cov[1][2];

    EigenFrameInfo globalFrame;
    eigenSymmetric3(cov, globalFrame);
    out.globalGeometricDim = inferGeometricDimensionFromEigenvalues(
        globalFrame.eigenValues, planeEigenRatio, lineEigenRatio);
    out.globalEigenRatios = {
        globalFrame.eigenValues[0] > kGeomEps ? globalFrame.eigenValues[1] / globalFrame.eigenValues[0] : 0.0,
        globalFrame.eigenValues[0] > kGeomEps ? globalFrame.eigenValues[2] / globalFrame.eigenValues[0] : 0.0,
        globalFrame.eigenValues[1] > kGeomEps ? globalFrame.eigenValues[2] / globalFrame.eigenValues[1] : 0.0
    };

    const size_t sampleCount = out.positions.size() / 3u;
    for (size_t i = 0; i < sampleCount; ++i) {
        const std::array<double, 3> center = loadPosition(out.positions, i);
        double localCov[3][3] = {
            { 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0 }
        };
        double sumDist = 0.0;
        int cnt = 0;
        for (int k = out.offsets[i]; k < out.offsets[i + 1]; ++k) {
            const int j = out.neighbors[k];
            if (j < 0 || static_cast<size_t>(j) >= sampleCount) {
                continue;
            }
            const std::array<double, 3> d = sub3(loadPosition(out.positions, static_cast<size_t>(j)), center);
            const double dist = norm3(d);
            if (dist <= kGeomEps) {
                continue;
            }
            localCov[0][0] += d[0] * d[0];
            localCov[0][1] += d[0] * d[1];
            localCov[0][2] += d[0] * d[2];
            localCov[1][1] += d[1] * d[1];
            localCov[1][2] += d[1] * d[2];
            localCov[2][2] += d[2] * d[2];
            sumDist += dist;
            ++cnt;
        }
        localCov[1][0] = localCov[0][1];
        localCov[2][0] = localCov[0][2];
        localCov[2][1] = localCov[1][2];
        if (cnt > 0) {
            const double inv = 1.0 / static_cast<double>(cnt);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    localCov[r][c] *= inv;
                }
            }
        }

        EigenFrameInfo frame;
        eigenSymmetric3(localCov, frame);
        const int localDim = inferGeometricDimensionFromEigenvalues(
            frame.eigenValues, planeEigenRatio, lineEigenRatio);
        const std::uint32_t maxDimByCount = (cnt <= 2) ? 1u : ((cnt <= 4) ? 2u : 3u);
        const std::uint32_t topoMaxDim = static_cast<std::uint32_t>(std::clamp(out.topologicalDim, 1, 3));
        out.dimTags[i] = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(localDim),
            std::min(maxDimByCount, topoMaxDim));
        out.meanNeighborDistance[i] = static_cast<float>(cnt > 0 ? (sumDist / static_cast<double>(cnt)) : 1.0);
        for (int c = 0; c < 9; ++c) {
            out.frames[i * 9u + static_cast<size_t>(c)] = frame.frame[static_cast<size_t>(c)];
        }
    }

    out.surfaceLike = out.topologicalDim <= 2 || out.globalGeometricDim <= 2;
    return out;
}

inline std::vector<float> projectGradientToIntrinsic(const std::vector<float>& values,
                                                     int comps,
                                                     const GeometryAnalysis& geometry)
{
    std::vector<float> projected(values.size(), 0.0f);
    if (!geometry.available || comps < 3 || comps % 3 != 0) {
        return projected;
    }

    const size_t tupleCount = values.size() / static_cast<size_t>(comps);
    if (tupleCount != geometry.dimTags.size() || geometry.frames.size() != tupleCount * 9u) {
        return projected;
    }

    for (size_t i = 0; i < tupleCount; ++i) {
        const std::array<double, 3> e1{
            geometry.frames[i * 9u + 0],
            geometry.frames[i * 9u + 1],
            geometry.frames[i * 9u + 2]
        };
        const std::array<double, 3> e2{
            geometry.frames[i * 9u + 3],
            geometry.frames[i * 9u + 4],
            geometry.frames[i * 9u + 5]
        };
        const std::array<double, 3> e3{
            geometry.frames[i * 9u + 6],
            geometry.frames[i * 9u + 7],
            geometry.frames[i * 9u + 8]
        };
        const std::uint32_t dim = std::clamp<std::uint32_t>(geometry.dimTags[i], 1u, 3u);
        for (int c = 0; c < comps; c += 3) {
            const std::array<double, 3> g{
                static_cast<double>(values[i * static_cast<size_t>(comps) + static_cast<size_t>(c + 0)]),
                static_cast<double>(values[i * static_cast<size_t>(comps) + static_cast<size_t>(c + 1)]),
                static_cast<double>(values[i * static_cast<size_t>(comps) + static_cast<size_t>(c + 2)])
            };
            std::array<double, 3> proj{
                dot3(g, e1) * e1[0],
                dot3(g, e1) * e1[1],
                dot3(g, e1) * e1[2]
            };
            if (dim >= 2u) {
                const double d2 = dot3(g, e2);
                proj[0] += d2 * e2[0];
                proj[1] += d2 * e2[1];
                proj[2] += d2 * e2[2];
            }
            if (dim >= 3u) {
                const double d3 = dot3(g, e3);
                proj[0] += d3 * e3[0];
                proj[1] += d3 * e3[1];
                proj[2] += d3 * e3[2];
            }
            projected[i * static_cast<size_t>(comps) + static_cast<size_t>(c + 0)] = static_cast<float>(proj[0]);
            projected[i * static_cast<size_t>(comps) + static_cast<size_t>(c + 1)] = static_cast<float>(proj[1]);
            projected[i * static_cast<size_t>(comps) + static_cast<size_t>(c + 2)] = static_cast<float>(proj[2]);
        }
    }

    return projected;
}

struct NamedArray
{
    std::string name;
    std::vector<float> data;
    int numComponents = 1;
};

inline std::vector<NamedArray> buildSyntheticScalarFields(const std::vector<float>& positions)
{
    std::vector<NamedArray> out;
    AnalyticBenchmarkFrame frame;
    if (!buildAnalyticBenchmarkFrame(positions, frame)) {
        return out;
    }

    const size_t tupleCount = positions.size() / 3u;
    constexpr double kPi = 3.14159265358979323846;
    const double cx = frame.center[0];
    const double cy = frame.center[1];
    const double cz = frame.center[2];
    const double L = frame.refLength;

    NamedArray trig{ "ms_clean_trig", std::vector<float>(tupleCount, 0.0f), 1 };
    NamedArray gaussian{ "ms_clean_gaussian", std::vector<float>(tupleCount, 0.0f), 1 };
    NamedArray mixedPoly{ "ms_clean_poly", std::vector<float>(tupleCount, 0.0f), 1 };
    NamedArray edge{ "ms_clean_edge", std::vector<float>(tupleCount, 0.0f), 1 };

    for (size_t i = 0; i < tupleCount; ++i) {
        const double x = static_cast<double>(positions[i * 3u + 0]) - cx;
        const double y = static_cast<double>(positions[i * 3u + 1]) - cy;
        const double z = static_cast<double>(positions[i * 3u + 2]) - cz;
        const double sx = kPi * x / L;
        const double sy = kPi * y / L;
        const double sz = kPi * z / L;

        trig.data[i] = static_cast<float>(
            0.60 * std::sin(sx) +
            0.35 * std::cos(1.7 * sy) +
            0.20 * std::sin(0.8 * sz));

        const double sigma = 0.28 * L;
        const double sigma2 = std::max(sigma * sigma, 1e-8);
        gaussian.data[i] = static_cast<float>(std::exp(-(x * x + 0.8 * y * y + 1.3 * z * z) / sigma2));

        mixedPoly.data[i] = static_cast<float>(
            (0.45 * x * x - 0.25 * y * y + 0.15 * z * z + 0.12 * x * y - 0.07 * y * z) / (L * L) +
            0.25 * std::sin(1.4 * sx));

        const double smoothBase =
            0.20 * std::sin(0.6 * sx) +
            0.12 * std::cos(0.8 * sy) +
            0.08 * std::sin(0.5 * sz);
        const double signedPlane = 0.65 * x - 0.35 * y + 0.15 * z;
        const double sharp = signedPlane >= 0.0 ? 0.95 : -0.95;
        edge.data[i] = static_cast<float>(smoothBase + sharp);
    }

    out.push_back(std::move(trig));
    out.push_back(std::move(gaussian));
    out.push_back(std::move(mixedPoly));
    out.push_back(std::move(edge));
    return out;
}

enum class NoiseKind
{
    Gaussian,
    Impulse,
    Mixed
};

inline const char* noiseKindTag(NoiseKind kind)
{
    switch (kind) {
    case NoiseKind::Impulse:
        return "impulse";
    case NoiseKind::Mixed:
        return "mixed";
    default:
        return "gaussian";
    }
}

inline std::vector<float> addNoise(const std::vector<float>& clean,
                                   int numComponents,
                                   NoiseKind kind,
                                   double sigmaFactor,
                                   double impulseRatio,
                                   double impulseScale,
                                   std::uint32_t seed)
{
    std::vector<float> noisy = clean;
    if (clean.empty() || numComponents <= 0) {
        return noisy;
    }

    const double signalStd = std::max(computeStdDev(clean), 1e-6);
    const double sigma = std::max(0.0, sigmaFactor) * signalStd;
    const double impulseAmp = std::max(impulseScale, 0.0) * signalStd;

    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, sigma);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> sign(-1.0, 1.0);

    for (size_t i = 0; i < noisy.size(); ++i) {
        double value = static_cast<double>(clean[i]);
        if (kind == NoiseKind::Gaussian || kind == NoiseKind::Mixed) {
            value += gauss(rng);
        }
        if (kind == NoiseKind::Impulse || kind == NoiseKind::Mixed) {
            if (unit(rng) < impulseRatio) {
                value += impulseAmp * (sign(rng) >= 0.0 ? 1.0 : -1.0);
            }
        }
        noisy[i] = static_cast<float>(value);
    }
    return noisy;
}
}
