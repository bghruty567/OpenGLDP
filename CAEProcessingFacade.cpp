#include "CAEProcessingFacade.h"
#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkDataSetWriter.h>
#include <vtkDataSetReader.h>
#include <vtkKdTreePointLocator.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

namespace
{
bool solveDenseSystem(double a[4][4], double b[4], int n, double x[4]);
std::array<double, 3> loadPosition(const std::vector<float>& positions, int idx);
std::array<double, 3> sub3(const std::array<double, 3>& a, const std::array<double, 3>& b);
double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b);
double norm3(const std::array<double, 3>& a);

std::string assocTag(CAEFieldAssociation a)
{
    return a == CAEFieldAssociation::Point ? "P" : "C";
}

std::string makeSmoothName(const std::string& src, CAEFieldAssociation assoc, int level)
{
    return src + "_ms_s" + std::to_string(level) + "_" + assocTag(assoc);
}

std::string makeDetailName(const std::string& src, CAEFieldAssociation assoc, int level)
{
    return src + "_ms_d" + std::to_string(level) + "_" + assocTag(assoc);
}

std::string makeBaseName(const std::string& src, CAEFieldAssociation assoc)
{
    return src + "_ms_base_" + assocTag(assoc);
}

std::string makeFusedName(const std::string& src, CAEFieldAssociation assoc)
{
    return src + "_ms_fused_" + assocTag(assoc);
}

int cellTopologicalDimensionFromType(int cellType)
{
    switch (cellType) {
    case VTK_EMPTY_CELL:
        return 0;
    case VTK_VERTEX:
    case VTK_POLY_VERTEX:
        return 0;
    case VTK_LINE:
    case VTK_POLY_LINE:
    case VTK_QUADRATIC_EDGE:
    case VTK_CUBIC_LINE:
        return 1;
    case VTK_TRIANGLE:
    case VTK_TRIANGLE_STRIP:
    case VTK_POLYGON:
    case VTK_PIXEL:
    case VTK_QUAD:
    case VTK_QUADRATIC_TRIANGLE:
    case VTK_QUADRATIC_QUAD:
    case VTK_BIQUADRATIC_QUAD:
    case VTK_QUADRATIC_LINEAR_QUAD:
    case VTK_BIQUADRATIC_TRIANGLE:
        return 2;
    case VTK_TETRA:
    case VTK_VOXEL:
    case VTK_HEXAHEDRON:
    case VTK_WEDGE:
    case VTK_PYRAMID:
    case VTK_PENTAGONAL_PRISM:
    case VTK_HEXAGONAL_PRISM:
    case VTK_QUADRATIC_TETRA:
    case VTK_QUADRATIC_HEXAHEDRON:
    case VTK_QUADRATIC_WEDGE:
    case VTK_QUADRATIC_PYRAMID:
    case VTK_TRIQUADRATIC_HEXAHEDRON:
    case VTK_QUADRATIC_LINEAR_WEDGE:
    case VTK_BIQUADRATIC_QUADRATIC_WEDGE:
    case VTK_BIQUADRATIC_QUADRATIC_HEXAHEDRON:
    case VTK_POLYHEDRON:
        return 3;
    default:
        return 0;
    }
}

int datasetMaxCellDimension(const DataObject& data)
{
    int maxDim = 0;
    for (int cellType : data.cellTypes) {
        maxDim = std::max(maxDim, cellTopologicalDimensionFromType(cellType));
    }
    return maxDim;
}

std::vector<int> buildPointPreferredCellDimensions(const DataObject& data)
{
    const size_t numPoints = data.pointCount();
    std::vector<int> preferred(numPoints, 0);
    if (data.pointInCellNeighborOffsets.size() != numPoints + 1) {
        return preferred;
    }

    for (size_t pid = 0; pid < numPoints; ++pid) {
        int dim = 0;
        const int begin = data.pointInCellNeighborOffsets[pid];
        const int end = data.pointInCellNeighborOffsets[pid + 1];
        for (int k = begin; k < end; ++k) {
            const int cellId = data.pointInCellNeighbors[static_cast<size_t>(k)];
            if (cellId < 0 || static_cast<size_t>(cellId) >= data.cellTypes.size()) {
                continue;
            }
            dim = std::max(dim, cellTopologicalDimensionFromType(data.cellTypes[static_cast<size_t>(cellId)]));
        }
        preferred[pid] = dim;
    }

    return preferred;
}

bool isFiniteGradientBlock(const std::vector<double>& derivs, int comps)
{
    const size_t need = static_cast<size_t>(comps) * 3u;
    if (derivs.size() < need) {
        return false;
    }
    for (size_t i = 0; i < need; ++i) {
        if (!std::isfinite(derivs[i])) {
            return false;
        }
    }
    return true;
}

bool evaluateCellDerivativesAtParametricPoint(vtkCell* cell,
                                              int subId,
                                              const double pcoords[3],
                                              const std::vector<double>& localValues,
                                              int comps,
                                              std::vector<double>& derivs)
{
    if (!cell || comps <= 0) {
        return false;
    }

    derivs.assign(static_cast<size_t>(comps) * 3u, 0.0);
    cell->Derivatives(subId, pcoords, localValues.data(), comps, derivs.data());
    return isFiniteGradientBlock(derivs, comps);
}

void accumulateWeightedSampleDouble(double normal[4][4],
                                    std::vector<double>& rhs,
                                    const double row[4],
                                    int unknownCount,
                                    const double* values,
                                    int channels,
                                    double weight)
{
    if (weight <= 0.0) {
        return;
    }

    for (int r = 0; r < unknownCount; ++r) {
        for (int c = 0; c < unknownCount; ++c) {
            normal[r][c] += weight * row[r] * row[c];
        }
        for (int ch = 0; ch < channels; ++ch) {
            rhs[static_cast<size_t>(ch) * 4u + static_cast<size_t>(r)] +=
                weight * row[r] * values[static_cast<size_t>(ch)];
        }
    }
}

bool computeVolumePointGradientByCellPatches(const DataObject& data,
                                             vtkDataSet* source,
                                             const DataArray& src,
                                             std::vector<float>& outGrad)
{
    const size_t numPoints = data.pointCount();
    const size_t numCells = data.cellCount();
    const int comps = src.numComponents;
    const int channels = comps * 3;
    if (!source ||
        data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED ||
        src.dataType != POINT_DATA ||
        comps <= 0 ||
        src.data.size() != numPoints * static_cast<size_t>(comps) ||
        data.pointInCellNeighborOffsets.size() != numPoints + 1 ||
        data.pointInCellNeighborOffsets.empty() ||
        data.pointInCellNeighborOffsets.back() < 0 ||
        static_cast<size_t>(data.pointInCellNeighborOffsets.back()) > data.pointInCellNeighbors.size() ||
        data.cellCenters.size() != numCells * 3u ||
        source->GetNumberOfPoints() != static_cast<vtkIdType>(numPoints) ||
        source->GetNumberOfCells() != static_cast<vtkIdType>(numCells)) {
        return false;
    }

    const std::vector<int> preferredDims = buildPointPreferredCellDimensions(data);
    // Recover nodal gradients from cell-center gradient samples using an
    // inverse-distance-weighted patch LSQ fit. This follows the standard
    // "element derivatives + patch recovery" pattern from FE recovery
    // literature and avoids the instability of raw point-neighborhood LSQ.
    outGrad.assign(numPoints * static_cast<size_t>(channels), 0.0f);
    std::vector<double> cellGradientSamples(numCells * static_cast<size_t>(channels), 0.0);
    std::vector<unsigned char> cellGradientValid(numCells, 0u);

    std::vector<double> localValues;
    std::vector<double> centerDerivs;
    std::vector<int> sampleCellIds;
    std::vector<double> sampleDistances;
    std::vector<double> rhs;

    for (size_t cellId = 0; cellId < numCells; ++cellId) {
        vtkCell* cell = source->GetCell(static_cast<vtkIdType>(cellId));
        if (!cell) {
            continue;
        }

        const int cellDim = cell->GetCellDimension();
        if (cellDim <= 0) {
            continue;
        }

        vtkIdList* pointIds = cell->GetPointIds();
        const vtkIdType pointCount = pointIds ? pointIds->GetNumberOfIds() : 0;
        if (pointCount <= 0) {
            continue;
        }

        localValues.assign(static_cast<size_t>(pointCount) * static_cast<size_t>(comps), 0.0);
        for (vtkIdType localId = 0; localId < pointCount; ++localId) {
            const vtkIdType pointId = pointIds->GetId(localId);
            if (pointId < 0 || pointId >= static_cast<vtkIdType>(numPoints)) {
                return false;
            }
            for (int comp = 0; comp < comps; ++comp) {
                localValues[static_cast<size_t>(localId) * static_cast<size_t>(comps) + static_cast<size_t>(comp)] =
                    static_cast<double>(src.data[static_cast<size_t>(pointId) * static_cast<size_t>(comps) + static_cast<size_t>(comp)]);
            }
        }

        double centerPcoords[3] = { 0.0, 0.0, 0.0 };
        const int centerSubId = cell->GetParametricCenter(centerPcoords);
        if (!evaluateCellDerivativesAtParametricPoint(
                cell,
                centerSubId,
                centerPcoords,
                localValues,
                comps,
                centerDerivs)) {
            continue;
        }

        const size_t cellBase = cellId * static_cast<size_t>(channels);
        for (int comp = 0; comp < comps; ++comp) {
            const size_t derivBase = static_cast<size_t>(comp) * 3u;
            const size_t outBase = cellBase + static_cast<size_t>(comp) * 3u;
            cellGradientSamples[outBase + 0] = centerDerivs[derivBase + 0];
            cellGradientSamples[outBase + 1] = centerDerivs[derivBase + 1];
            cellGradientSamples[outBase + 2] = centerDerivs[derivBase + 2];
        }
        cellGradientValid[cellId] = 1u;
    }

    bool hasValidPoint = false;
    for (size_t pointId = 0; pointId < numPoints; ++pointId) {
        const int preferredDim = preferredDims[pointId];
        if (preferredDim <= 0) {
            continue;
        }

        sampleCellIds.clear();
        sampleDistances.clear();
        const std::array<double, 3> pointPos = loadPosition(data.points, static_cast<int>(pointId));
        const int begin = data.pointInCellNeighborOffsets[pointId];
        const int end = data.pointInCellNeighborOffsets[pointId + 1];
        double meanDistance = 0.0;

        for (int k = begin; k < end; ++k) {
            const int cellId = data.pointInCellNeighbors[static_cast<size_t>(k)];
            if (cellId < 0 || static_cast<size_t>(cellId) >= numCells || !cellGradientValid[static_cast<size_t>(cellId)]) {
                continue;
            }

            int cellDim = 0;
            if (static_cast<size_t>(cellId) < data.cellTypes.size()) {
                cellDim = cellTopologicalDimensionFromType(data.cellTypes[static_cast<size_t>(cellId)]);
            }
            if (cellDim <= 0) {
                vtkCell* cell = source->GetCell(static_cast<vtkIdType>(cellId));
                cellDim = cell ? cell->GetCellDimension() : 0;
            }
            if (cellDim != preferredDim) {
                continue;
            }

            const std::array<double, 3> d = sub3(loadPosition(data.cellCenters, cellId), pointPos);
            const double dist = norm3(d);
            if (dist <= 1e-12) {
                continue;
            }

            sampleCellIds.push_back(cellId);
            sampleDistances.push_back(dist);
            meanDistance += dist;
        }

        if (sampleCellIds.empty()) {
            continue;
        }

        hasValidPoint = true;
        const size_t outBase = pointId * static_cast<size_t>(channels);

        if (sampleCellIds.size() == 1u) {
            const size_t cellBase = static_cast<size_t>(sampleCellIds.front()) * static_cast<size_t>(channels);
            for (int ch = 0; ch < channels; ++ch) {
                outGrad[outBase + static_cast<size_t>(ch)] =
                    static_cast<float>(cellGradientSamples[cellBase + static_cast<size_t>(ch)]);
            }
            continue;
        }

        meanDistance /= static_cast<double>(sampleCellIds.size());
        const double scale = std::max(meanDistance, 1e-6);
        double normal[4][4] = {
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 }
        };
        rhs.assign(static_cast<size_t>(channels) * 4u, 0.0);

        std::vector<double> weightedAverage(static_cast<size_t>(channels), 0.0);
        double weightSum = 0.0;
        for (size_t sampleIdx = 0; sampleIdx < sampleCellIds.size(); ++sampleIdx) {
            const int cellId = sampleCellIds[sampleIdx];
            const std::array<double, 3> d = sub3(loadPosition(data.cellCenters, cellId), pointPos);
            const double row[4] = {
                1.0,
                d[0] / scale,
                d[1] / scale,
                d[2] / scale
            };
            const double dist = sampleDistances[sampleIdx];
            const double weight = 1.0 / std::max(dist, 1e-6);
            const double* sample =
                cellGradientSamples.data() + static_cast<size_t>(cellId) * static_cast<size_t>(channels);
            accumulateWeightedSampleDouble(normal, rhs, row, 4, sample, channels, weight);
            weightSum += weight;
            for (int ch = 0; ch < channels; ++ch) {
                weightedAverage[ch] += weight * sample[static_cast<size_t>(ch)];
            }
        }

        bool usedLsq = sampleCellIds.size() >= 4u;
        if (usedLsq) {
            double trace = normal[1][1] + normal[2][2] + normal[3][3];
            const double reg = std::max(trace * 1e-10, 1e-10);
            normal[1][1] += reg;
            normal[2][2] += reg;
            normal[3][3] += reg;

            for (int ch = 0; ch < channels; ++ch) {
                double a[4][4] = {
                    { 0.0, 0.0, 0.0, 0.0 },
                    { 0.0, 0.0, 0.0, 0.0 },
                    { 0.0, 0.0, 0.0, 0.0 },
                    { 0.0, 0.0, 0.0, 0.0 }
                };
                double bVec[4] = { 0.0, 0.0, 0.0, 0.0 };
                double coeff[4] = { 0.0, 0.0, 0.0, 0.0 };
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        a[r][c] = normal[r][c];
                    }
                    bVec[r] = rhs[static_cast<size_t>(ch) * 4u + static_cast<size_t>(r)];
                }
                if (!solveDenseSystem(a, bVec, 4, coeff)) {
                    usedLsq = false;
                    break;
                }
                outGrad[outBase + static_cast<size_t>(ch)] = static_cast<float>(coeff[0]);
            }
        }

        if (!usedLsq) {
            if (weightSum <= 0.0) {
                continue;
            }
            const double invWeightSum = 1.0 / weightSum;
            for (int ch = 0; ch < channels; ++ch) {
                outGrad[outBase + static_cast<size_t>(ch)] =
                    static_cast<float>(weightedAverage[ch] * invWeightSum);
            }
        }
    }

    return hasValidPoint;
}

bool buildRegularNeighbors(int nx, int ny, int nz, std::vector<int>& offsets, std::vector<int>& neighbors)
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

bool buildFilterGraph(const DataObject& data,
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
        if (positions.size() != static_cast<size_t>(nx) * ny * nz * 3) {
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

    if (positions.empty() || (positions.size() % 3) != 0) {
        return false;
    }

    return offsets.size() == positions.size() / 3 + 1;
}

double estimateMeanNeighborDistance(const std::vector<float>& positions,
                                    const std::vector<int>& offsets,
                                    const std::vector<int>& neighbors)
{
    const size_t n = positions.size() / 3;
    if (n == 0 || offsets.size() != n + 1) {
        return 1.0;
    }

    double sum = 0.0;
    size_t cnt = 0;

    for (size_t i = 0; i < n; ++i) {
        const double px = positions[i * 3 + 0];
        const double py = positions[i * 3 + 1];
        const double pz = positions[i * 3 + 2];

        for (int k = offsets[i]; k < offsets[i + 1]; ++k) {
            const int j = neighbors[k];
            if (j < 0 || static_cast<size_t>(j) >= n) {
                continue;
            }

            const double dx = positions[static_cast<size_t>(j) * 3 + 0] - px;
            const double dy = positions[static_cast<size_t>(j) * 3 + 1] - py;
            const double dz = positions[static_cast<size_t>(j) * 3 + 2] - pz;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (d > 1e-12) {
                sum += d;
                ++cnt;
            }
        }
    }

    return cnt > 0 ? (sum / static_cast<double>(cnt)) : 1.0;
}

double estimateStdDev(const std::vector<float>& values)
{
    if (values.empty()) {
        return 1.0;
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

    const double stddev = std::sqrt(var);
    return stddev > 1e-12 ? stddev : 1.0;
}

void subtractField(const std::vector<float>& a,
                   const std::vector<float>& b,
                   std::vector<float>& out)
{
    out.resize(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] - b[i];
    }
}

constexpr double kGeomEps = 1e-12;

struct SupportBuildConfig
{
    int minNeighbors = 12;
    int targetNeighbors = 20;
    int maxNeighbors = 32;
    double radiusScale = 2.5;
    double planeEigenRatio = 0.06;
    double lineEigenRatio = 0.02;
    bool useAdaptiveNeighborhood = true;
};

bool approxEqual(double a, double b)
{
    return std::abs(a - b) <= 1e-6;
}

bool usesDefaultSupportTuning(const CAEGradientRequest& req)
{
    CAEGradientRequest defaults;
    return req.minNeighbors == defaults.minNeighbors &&
           req.targetNeighbors == defaults.targetNeighbors &&
           req.maxNeighbors == defaults.maxNeighbors &&
           approxEqual(req.radiusScale, defaults.radiusScale) &&
           approxEqual(req.planeEigenRatio, defaults.planeEigenRatio) &&
           approxEqual(req.lineEigenRatio, defaults.lineEigenRatio) &&
           req.useAdaptiveNeighborhood == defaults.useAdaptiveNeighborhood;
}

SupportBuildConfig makeSupportBuildConfig(CAEFieldAssociation assoc,
                                          const CAEGradientRequest& req)
{
    SupportBuildConfig cfg;
    cfg.minNeighbors = std::max(2, req.minNeighbors);
    cfg.targetNeighbors = std::max(cfg.minNeighbors, req.targetNeighbors);
    cfg.maxNeighbors = std::max(cfg.targetNeighbors, req.maxNeighbors);
    cfg.radiusScale = std::max(1.0f, req.radiusScale);
    cfg.planeEigenRatio = std::max(1e-4f, req.planeEigenRatio);
    cfg.lineEigenRatio = std::max(1e-4f, req.lineEigenRatio);
    cfg.useAdaptiveNeighborhood = req.useAdaptiveNeighborhood;

    // Cell-centered scalar fields on shell-like meshes are much more sensitive
    // to oversized supports than point fields, so keep the default cell stencil tighter.
    if (assoc == CAEFieldAssociation::Cell && usesDefaultSupportTuning(req)) {
        cfg.minNeighbors = 4;
        cfg.targetNeighbors = 8;
        cfg.maxNeighbors = 12;
        cfg.radiusScale = 1.75;
    }

    return cfg;
}

struct AdaptiveSupportData
{
    std::vector<int> offsets;
    std::vector<int> neighbors;
    std::vector<float> frames;
    std::vector<std::uint32_t> dimTags;
    std::vector<float> quality;
    std::vector<float> meanNeighborDistance;
};

struct LocalSpectralInfo
{
    std::array<double, 9> frame{
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    std::array<double, 3> eigenValues{ 1.0, 1.0, 1.0 };
    std::uint32_t dimTag = 3;
    double quality = 0.0;
    double meanDistance = 1.0;
};

double clamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

std::array<double, 3> loadPosition(const std::vector<float>& positions, int idx)
{
    return {
        static_cast<double>(positions[static_cast<size_t>(idx) * 3 + 0]),
        static_cast<double>(positions[static_cast<size_t>(idx) * 3 + 1]),
        static_cast<double>(positions[static_cast<size_t>(idx) * 3 + 2])
    };
}

std::array<double, 3> sub3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<double, 3> cross3(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

double norm3(const std::array<double, 3>& a)
{
    return std::sqrt(std::max(dot3(a, a), 0.0));
}

std::array<double, 3> normalize3(const std::array<double, 3>& a, const std::array<double, 3>& fallback)
{
    const double n = norm3(a);
    if (n <= kGeomEps) {
        return fallback;
    }
    return { a[0] / n, a[1] / n, a[2] / n };
}

std::array<double, 3> arbitraryPerpendicular(const std::array<double, 3>& a)
{
    const std::array<double, 3> axisX{ 1.0, 0.0, 0.0 };
    const std::array<double, 3> axisY{ 0.0, 1.0, 0.0 };
    std::array<double, 3> c = cross3(a, axisX);
    if (norm3(c) <= kGeomEps) {
        c = cross3(a, axisY);
    }
    return normalize3(c, axisY);
}

void orthonormalizeFrame(std::array<double, 3>& e1,
                         std::array<double, 3>& e2,
                         std::array<double, 3>& e3)
{
    e1 = normalize3(e1, { 1.0, 0.0, 0.0 });
    const double p12 = dot3(e2, e1);
    e2 = { e2[0] - p12 * e1[0], e2[1] - p12 * e1[1], e2[2] - p12 * e1[2] };
    e2 = normalize3(e2, arbitraryPerpendicular(e1));
    e3 = cross3(e1, e2);
    e3 = normalize3(e3, arbitraryPerpendicular(e1));
    e2 = normalize3(cross3(e3, e1), e2);
}

void rotateJacobi(double a[3][3], double v[3][3], int p, int q)
{
    if (std::abs(a[p][q]) <= kGeomEps) {
        return;
    }

    const double tau = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
    const double t = (tau >= 0.0)
        ? 1.0 / (tau + std::sqrt(1.0 + tau * tau))
        : -1.0 / (-tau + std::sqrt(1.0 + tau * tau));
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
        a[r][p] = c * arp - s * arq;
        a[p][r] = a[r][p];
        a[r][q] = c * arq + s * arp;
        a[q][r] = a[r][q];
    }

    for (int r = 0; r < 3; ++r) {
        const double vrp = v[r][p];
        const double vrq = v[r][q];
        v[r][p] = c * vrp - s * vrq;
        v[r][q] = c * vrq + s * vrp;
    }
}

bool eigenSymmetric3(const double cov[3][3], LocalSpectralInfo& out)
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

    for (int it = 0; it < 16; ++it) {
        int p = 0;
        int q = 1;
        double maxOff = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > maxOff) { p = 0; q = 2; maxOff = std::abs(a[0][2]); }
        if (std::abs(a[1][2]) > maxOff) { p = 1; q = 2; maxOff = std::abs(a[1][2]); }
        if (maxOff <= 1e-10) {
            break;
        }
        rotateJacobi(a, v, p, q);
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
    out.frame = {
        e1[0], e1[1], e1[2],
        e2[0], e2[1], e2[2],
        e3[0], e3[1], e3[2]
    };
    return true;
}

double distanceBetween(const std::vector<float>& positions, int i, int j)
{
    const std::array<double, 3> pi = loadPosition(positions, i);
    const std::array<double, 3> pj = loadPosition(positions, j);
    return norm3(sub3(pj, pi));
}

double meanDistanceToNeighbors(const std::vector<float>& positions, int i, const std::set<int>& nbrs)
{
    if (nbrs.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    int cnt = 0;
    for (int j : nbrs) {
        if (j == i) {
            continue;
        }
        const double d = distanceBetween(positions, i, j);
        if (d > kGeomEps) {
            sum += d;
            ++cnt;
        }
    }
    return cnt > 0 ? (sum / static_cast<double>(cnt)) : 0.0;
}

vtkSmartPointer<vtkKdTreePointLocator> buildLocatorFromPositions(const std::vector<float>& positions)
{
    const size_t n = positions.size() / 3;
    vtkNew<vtkPoints> pts;
    pts->SetNumberOfPoints(static_cast<vtkIdType>(n));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(n); ++i) {
        pts->SetPoint(i,
            positions[static_cast<size_t>(i) * 3 + 0],
            positions[static_cast<size_t>(i) * 3 + 1],
            positions[static_cast<size_t>(i) * 3 + 2]);
    }

    vtkNew<vtkPolyData> pd;
    pd->SetPoints(pts);

    vtkSmartPointer<vtkKdTreePointLocator> locator = vtkSmartPointer<vtkKdTreePointLocator>::New();
    locator->SetDataSet(pd);
    locator->BuildLocator();
    return locator;
}

void collectBaseCandidates(const std::vector<int>& offsets,
                           const std::vector<int>& neighbors,
                           int centerId,
                           std::set<int>& out)
{
    out.clear();
    if (centerId < 0 || static_cast<size_t>(centerId + 1) >= offsets.size()) {
        return;
    }
    for (int k = offsets[centerId]; k < offsets[centerId + 1]; ++k) {
        const int j = neighbors[static_cast<size_t>(k)];
        if (j >= 0 && j != centerId) {
            out.insert(j);
        }
    }
}

void expandSecondRing(const std::vector<int>& offsets,
                      const std::vector<int>& neighbors,
                      int centerId,
                      int desiredCount,
                      std::set<int>& inOut)
{
    if (desiredCount <= 0 || inOut.empty()) {
        return;
    }
    std::vector<int> firstRing(inOut.begin(), inOut.end());
    for (int nb : firstRing) {
        if (nb < 0 || static_cast<size_t>(nb + 1) >= offsets.size()) {
            continue;
        }
        for (int k = offsets[nb]; k < offsets[nb + 1]; ++k) {
            const int j = neighbors[static_cast<size_t>(k)];
            if (j >= 0 && j != centerId) {
                inOut.insert(j);
            }
        }
        if (static_cast<int>(inOut.size()) >= desiredCount) {
            break;
        }
    }
}

void supplementByKnn(vtkKdTreePointLocator* locator,
                     const std::vector<float>& positions,
                     int centerId,
                     int desiredCount,
                     int queryCount,
                     double maxAcceptDistance,
                     std::set<int>& inOut)
{
    if (!locator || desiredCount <= 0) {
        return;
    }
    double q[3] = {
        positions[static_cast<size_t>(centerId) * 3 + 0],
        positions[static_cast<size_t>(centerId) * 3 + 1],
        positions[static_cast<size_t>(centerId) * 3 + 2]
    };

    vtkNew<vtkIdList> ids;
    locator->FindClosestNPoints(queryCount + 1, q, ids);
    for (vtkIdType t = 0; t < ids->GetNumberOfIds(); ++t) {
        const int j = static_cast<int>(ids->GetId(t));
        if (j == centerId || j < 0) {
            continue;
        }
        const double d = distanceBetween(positions, centerId, j);
        if (maxAcceptDistance < std::numeric_limits<double>::max() && d > maxAcceptDistance) {
            continue;
        }
        inOut.insert(j);
        if (static_cast<int>(inOut.size()) >= desiredCount) {
            break;
        }
    }
}

double computeDirectionalBalance(const std::vector<float>& positions,
                                 int centerId,
                                 const std::vector<int>& neighbors,
                                 const std::array<double, 9>& frame,
                                 int activeDim)
{
    if (neighbors.empty() || activeDim <= 0) {
        return 0.0;
    }
    const std::array<double, 3> center = loadPosition(positions, centerId);
    int posCount[3] = { 0, 0, 0 };
    int negCount[3] = { 0, 0, 0 };

    for (int j : neighbors) {
        const std::array<double, 3> d = sub3(loadPosition(positions, j), center);
        for (int axis = 0; axis < activeDim; ++axis) {
            const std::array<double, 3> e{
                frame[static_cast<size_t>(axis) * 3 + 0],
                frame[static_cast<size_t>(axis) * 3 + 1],
                frame[static_cast<size_t>(axis) * 3 + 2]
            };
            const double proj = dot3(d, e);
            if (proj > 1e-8) {
                ++posCount[axis];
            } else if (proj < -1e-8) {
                ++negCount[axis];
            }
        }
    }

    double sum = 0.0;
    int used = 0;
    for (int axis = 0; axis < activeDim; ++axis) {
        const int mx = std::max(posCount[axis], negCount[axis]);
        if (mx <= 0) {
            continue;
        }
        sum += static_cast<double>(std::min(posCount[axis], negCount[axis])) / static_cast<double>(mx);
        ++used;
    }
    return used > 0 ? (sum / static_cast<double>(used)) : 0.0;
}

LocalSpectralInfo analyzeNeighborhood(const std::vector<float>& positions,
                                      int centerId,
                                      const std::vector<int>& neighbors,
                                      double planeEigenRatio,
                                      double lineEigenRatio,
                                      int targetNeighbors)
{
    LocalSpectralInfo info;
    if (neighbors.empty()) {
        info.quality = 0.0;
        return info;
    }

    const std::array<double, 3> center = loadPosition(positions, centerId);
    double cov[3][3] = {
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 }
    };
    double sumDist = 0.0;
    int distCount = 0;

    for (int j : neighbors) {
        const std::array<double, 3> d = sub3(loadPosition(positions, j), center);
        const double dist = norm3(d);
        if (dist <= kGeomEps) {
            continue;
        }
        cov[0][0] += d[0] * d[0];
        cov[0][1] += d[0] * d[1];
        cov[0][2] += d[0] * d[2];
        cov[1][1] += d[1] * d[1];
        cov[1][2] += d[1] * d[2];
        cov[2][2] += d[2] * d[2];
        sumDist += dist;
        ++distCount;
    }
    cov[1][0] = cov[0][1];
    cov[2][0] = cov[0][2];
    cov[2][1] = cov[1][2];

    if (distCount <= 0) {
        info.quality = 0.0;
        return info;
    }

    const double invCount = 1.0 / static_cast<double>(distCount);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            cov[r][c] *= invCount;
        }
    }

    eigenSymmetric3(cov, info);
    info.meanDistance = sumDist * invCount;

    const double l1 = std::max(info.eigenValues[0], kGeomEps);
    const double l2 = std::max(info.eigenValues[1], 0.0);
    const double l3 = std::max(info.eigenValues[2], 0.0);
    const double r21 = l2 / l1;
    const double r31 = l3 / l1;

    std::uint32_t dim = 3;
    if (r21 < std::max(lineEigenRatio, 1e-6)) {
        dim = 1;
    } else if (r31 < std::max(planeEigenRatio, 1e-6)) {
        dim = 2;
    }

    const size_t n = neighbors.size();
    const std::uint32_t maxDimByCount = (n <= 2) ? 1u : ((n <= 4) ? 2u : 3u);
    info.dimTag = std::min(dim, maxDimByCount);

    double spectralQuality = 1.0;
    if (info.dimTag == 3) {
        spectralQuality = clamp01((r31 / std::max(planeEigenRatio, 1e-6)));
    } else if (info.dimTag == 2) {
        spectralQuality = clamp01(r21);
    }

    const double directionalQuality = computeDirectionalBalance(
        positions,
        centerId,
        neighbors,
        info.frame,
        static_cast<int>(info.dimTag));
    const double countQuality = clamp01(
        static_cast<double>(neighbors.size()) /
        static_cast<double>(std::max(targetNeighbors, 1)));

    info.quality = clamp01(0.55 * spectralQuality + 0.30 * directionalQuality + 0.15 * countQuality);
    return info;
}

int directionBucket(const std::array<double, 3>& localCoord, std::uint32_t dimTag)
{
    const double ax = std::abs(localCoord[0]);
    const double ay = std::abs(localCoord[1]);
    const double az = std::abs(localCoord[2]);

    if (dimTag <= 1) {
        return localCoord[0] >= 0.0 ? 0 : 1;
    }
    if (dimTag == 2) {
        if (ax >= ay) {
            return localCoord[0] >= 0.0 ? 0 : 1;
        }
        return localCoord[1] >= 0.0 ? 2 : 3;
    }

    if (ax >= ay && ax >= az) {
        return localCoord[0] >= 0.0 ? 0 : 1;
    }
    if (ay >= az) {
        return localCoord[1] >= 0.0 ? 2 : 3;
    }
    return localCoord[2] >= 0.0 ? 4 : 5;
}

std::vector<int> selectBalancedNeighbors(const std::vector<float>& positions,
                                         int centerId,
                                         const std::vector<int>& candidates,
                                         const LocalSpectralInfo& info,
                                         int maxNeighbors)
{
    struct CandidateView
    {
        int id = -1;
        int bucket = 0;
        double dist = 0.0;
    };

    if (maxNeighbors <= 0 || static_cast<int>(candidates.size()) <= maxNeighbors) {
        return candidates;
    }

    const std::array<double, 3> center = loadPosition(positions, centerId);
    std::array<double, 3> e1{ info.frame[0], info.frame[1], info.frame[2] };
    std::array<double, 3> e2{ info.frame[3], info.frame[4], info.frame[5] };
    std::array<double, 3> e3{ info.frame[6], info.frame[7], info.frame[8] };

    std::vector<CandidateView> views;
    views.reserve(candidates.size());
    for (int j : candidates) {
        const std::array<double, 3> d = sub3(loadPosition(positions, j), center);
        const double dist = norm3(d);
        if (dist <= kGeomEps) {
            continue;
        }
        const std::array<double, 3> local{
            dot3(d, e1),
            dot3(d, e2),
            dot3(d, e3)
        };
        CandidateView view;
        view.id = j;
        view.bucket = directionBucket(local, info.dimTag);
        view.dist = dist;
        views.push_back(view);
    }

    std::sort(views.begin(), views.end(), [](const CandidateView& lhs, const CandidateView& rhs) {
        return lhs.dist < rhs.dist;
    });

    const int bucketCount = info.dimTag <= 1 ? 2 : (info.dimTag == 2 ? 4 : 6);
    bool bucketUsed[6] = { false, false, false, false, false, false };
    std::set<int> usedIds;
    std::vector<int> out;
    out.reserve(static_cast<size_t>(maxNeighbors));

    for (const CandidateView& view : views) {
        if (view.bucket < 0 || view.bucket >= bucketCount) {
            continue;
        }
        if (!bucketUsed[view.bucket]) {
            out.push_back(view.id);
            usedIds.insert(view.id);
            bucketUsed[view.bucket] = true;
            if (static_cast<int>(out.size()) >= maxNeighbors) {
                return out;
            }
        }
    }

    for (const CandidateView& view : views) {
        if (usedIds.find(view.id) != usedIds.end()) {
            continue;
        }
        out.push_back(view.id);
        if (static_cast<int>(out.size()) >= maxNeighbors) {
            break;
        }
    }
    return out;
}

bool buildAdaptiveGradientSupport(const std::vector<float>& positions,
                                  const std::vector<int>& baseOffsets,
                                  const std::vector<int>& baseNeighbors,
                                  const SupportBuildConfig& cfg,
                                  AdaptiveSupportData& out)
{
    const size_t n = positions.size() / 3;
    if (n == 0 || positions.size() % 3 != 0 || baseOffsets.size() != n + 1) {
        return false;
    }

    auto locator = buildLocatorFromPositions(positions);
    const double globalMean = estimateMeanNeighborDistance(positions, baseOffsets, baseNeighbors);
    const int targetNeighbors = std::max(cfg.targetNeighbors, cfg.minNeighbors);
    const int cappedMaxNeighbors = std::max(targetNeighbors, cfg.maxNeighbors);
    const int queryCount = std::max(cappedMaxNeighbors * 2, targetNeighbors + 8);

    out.offsets.clear();
    out.neighbors.clear();
    out.frames.resize(n * 9);
    out.dimTags.resize(n);
    out.quality.resize(n);
    out.meanNeighborDistance.resize(n);
    out.offsets.reserve(n + 1);
    out.offsets.push_back(0);

    for (size_t i = 0; i < n; ++i) {
        std::set<int> candidateSet;
        collectBaseCandidates(baseOffsets, baseNeighbors, static_cast<int>(i), candidateSet);

        if (cfg.useAdaptiveNeighborhood && static_cast<int>(candidateSet.size()) < targetNeighbors) {
            expandSecondRing(baseOffsets, baseNeighbors, static_cast<int>(i), targetNeighbors * 2, candidateSet);
        }

        double localMean = meanDistanceToNeighbors(positions, static_cast<int>(i), candidateSet);
        if (localMean <= kGeomEps) {
            localMean = globalMean > kGeomEps ? globalMean : 1.0;
        }

        if (cfg.useAdaptiveNeighborhood && static_cast<int>(candidateSet.size()) < targetNeighbors) {
            supplementByKnn(locator, positions, static_cast<int>(i), targetNeighbors, queryCount,
                std::max(localMean * cfg.radiusScale, localMean), candidateSet);
        }

        if (static_cast<int>(candidateSet.size()) < cfg.minNeighbors) {
            supplementByKnn(locator, positions, static_cast<int>(i), cfg.minNeighbors, queryCount,
                std::numeric_limits<double>::max(), candidateSet);
        }

        std::vector<int> candidates(candidateSet.begin(), candidateSet.end());
        if (candidates.empty()) {
            supplementByKnn(locator, positions, static_cast<int>(i), 2, queryCount,
                std::numeric_limits<double>::max(), candidateSet);
            candidates.assign(candidateSet.begin(), candidateSet.end());
        }

        LocalSpectralInfo prelim = analyzeNeighborhood(
            positions,
            static_cast<int>(i),
            candidates,
            cfg.planeEigenRatio,
            cfg.lineEigenRatio,
            targetNeighbors);

        std::vector<int> finalNeighbors = selectBalancedNeighbors(
            positions,
            static_cast<int>(i),
            candidates,
            prelim,
            cappedMaxNeighbors);

        LocalSpectralInfo finalInfo = analyzeNeighborhood(
            positions,
            static_cast<int>(i),
            finalNeighbors,
            cfg.planeEigenRatio,
            cfg.lineEigenRatio,
            targetNeighbors);

        out.neighbors.insert(out.neighbors.end(), finalNeighbors.begin(), finalNeighbors.end());
        out.offsets.push_back(static_cast<int>(out.neighbors.size()));
        for (int c = 0; c < 9; ++c) {
            out.frames[i * 9 + static_cast<size_t>(c)] = static_cast<float>(finalInfo.frame[static_cast<size_t>(c)]);
        }
        out.dimTags[i] = finalInfo.dimTag;
        out.quality[i] = static_cast<float>(finalInfo.quality);
        out.meanNeighborDistance[i] = static_cast<float>(std::max(finalInfo.meanDistance, 1e-6));
    }

    return true;
}

bool solveDenseSystem(double a[4][4], double b[4], int n, double x[4])
{
    if (n <= 0 || n > 4) {
        return false;
    }

    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double pivotAbs = std::abs(a[col][col]);
        for (int row = col + 1; row < n; ++row) {
            const double cand = std::abs(a[row][col]);
            if (cand > pivotAbs) {
                pivot = row;
                pivotAbs = cand;
            }
        }

        if (pivotAbs <= 1e-14) {
            return false;
        }

        if (pivot != col) {
            for (int k = 0; k < n; ++k) {
                std::swap(a[col][k], a[pivot][k]);
            }
            std::swap(b[col], b[pivot]);
        }

        const double diag = a[col][col];
        for (int k = col; k < n; ++k) {
            a[col][k] /= diag;
        }
        b[col] /= diag;

        for (int row = 0; row < n; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = a[row][col];
            if (std::abs(factor) <= 1e-16) {
                continue;
            }
            for (int k = col; k < n; ++k) {
                a[row][k] -= factor * a[col][k];
            }
            b[row] -= factor * b[col];
        }
    }

    for (int i = 0; i < n; ++i) {
        x[i] = b[i];
    }
    return true;
}

bool reconstructPointValuesFromCells(const DataObject& data,
                                     const DataArray& src,
                                     std::vector<float>& pointValues)
{
    const size_t numPoints = data.pointCount();
    const size_t numCells = data.cellCount();
    const int comps = src.numComponents;

    if (comps <= 0 ||
        src.dataType != CELL_DATA ||
        src.data.size() != numCells * static_cast<size_t>(comps) ||
        data.pointInCellNeighborOffsets.size() != numPoints + 1 ||
        data.pointInCellNeighborOffsets.empty() ||
        data.pointInCellNeighborOffsets.back() < 0 ||
        static_cast<size_t>(data.pointInCellNeighborOffsets.back()) > data.pointInCellNeighbors.size()) {
        return false;
    }

    pointValues.assign(numPoints * static_cast<size_t>(comps), 0.0f);

    for (size_t pid = 0; pid < numPoints; ++pid) {
        const int begin = data.pointInCellNeighborOffsets[pid];
        const int end = data.pointInCellNeighborOffsets[pid + 1];
        if (end <= begin) {
            continue;
        }

        std::vector<double> accum(static_cast<size_t>(comps), 0.0);
        int validCount = 0;
        for (int k = begin; k < end; ++k) {
            const int cellId = data.pointInCellNeighbors[static_cast<size_t>(k)];
            if (cellId < 0 || static_cast<size_t>(cellId) >= numCells) {
                continue;
            }
            const size_t base = static_cast<size_t>(cellId) * static_cast<size_t>(comps);
            for (int comp = 0; comp < comps; ++comp) {
                accum[static_cast<size_t>(comp)] += static_cast<double>(src.data[base + static_cast<size_t>(comp)]);
            }
            ++validCount;
        }

        if (validCount <= 0) {
            continue;
        }

        const double invCount = 1.0 / static_cast<double>(validCount);
        const size_t outBase = pid * static_cast<size_t>(comps);
        for (int comp = 0; comp < comps; ++comp) {
            pointValues[outBase + static_cast<size_t>(comp)] =
                static_cast<float>(accum[static_cast<size_t>(comp)] * invCount);
        }
    }

    return true;
}

bool computeCellLocalGeometry(const DataObject& data,
                              int cellId,
                              LocalSpectralInfo& out)
{
    const size_t numCells = data.cellCount();
    const size_t numPoints = data.pointCount();
    if (cellId < 0 ||
        static_cast<size_t>(cellId) >= numCells ||
        data.cellOffsets.size() != numCells + 1 ||
        data.cellCenters.size() != numCells * 3 ||
        data.points.size() != numPoints * 3) {
        return false;
    }

    const int begin = data.cellOffsets[static_cast<size_t>(cellId)];
    const int end = data.cellOffsets[static_cast<size_t>(cellId) + 1];
    if (end - begin < 2) {
        return false;
    }

    const std::array<double, 3> center = loadPosition(data.cellCenters, cellId);
    double cov[3][3] = {
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0 }
    };
    double sumDist = 0.0;
    int validCount = 0;

    for (int k = begin; k < end; ++k) {
        const int pointId = data.cells[static_cast<size_t>(k)];
        if (pointId < 0 || static_cast<size_t>(pointId) >= numPoints) {
            continue;
        }
        const std::array<double, 3> d = sub3(loadPosition(data.points, pointId), center);
        const double dist = norm3(d);
        if (dist <= kGeomEps) {
            continue;
        }
        cov[0][0] += d[0] * d[0];
        cov[0][1] += d[0] * d[1];
        cov[0][2] += d[0] * d[2];
        cov[1][1] += d[1] * d[1];
        cov[1][2] += d[1] * d[2];
        cov[2][2] += d[2] * d[2];
        sumDist += dist;
        ++validCount;
    }

    if (validCount < 2) {
        return false;
    }

    cov[1][0] = cov[0][1];
    cov[2][0] = cov[0][2];
    cov[2][1] = cov[1][2];

    const double invCount = 1.0 / static_cast<double>(validCount);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            cov[r][c] *= invCount;
        }
    }

    if (!eigenSymmetric3(cov, out)) {
        return false;
    }

    const int cellType = data.cellTypes[static_cast<size_t>(cellId)];
    const int topoDim = cellTopologicalDimensionFromType(cellType);
    const double l1 = std::max(out.eigenValues[0], kGeomEps);
    const double l2 = std::max(out.eigenValues[1], 0.0);
    const double l3 = std::max(out.eigenValues[2], 0.0);

    if (topoDim >= 1 && topoDim <= 3) {
        out.dimTag = static_cast<std::uint32_t>(topoDim);
    } else if (l2 <= 1e-8 * l1 || validCount <= 2) {
        out.dimTag = 1;
    } else if (l3 <= 1e-4 * l1) {
        out.dimTag = 2;
    } else {
        out.dimTag = 3;
    }

    out.meanDistance = sumDist * invCount;
    out.quality = 1.0;
    return true;
}

constexpr int kVtkTriangleCellType = 5;
constexpr int kVtkPolygonCellType = 7;
constexpr int kVtkQuadCellType = 9;

std::array<double, 2> quadNodeParamCoord(int localId)
{
    switch (localId) {
    case 0: return { 0.0, 0.0 };
    case 1: return { 1.0, 0.0 };
    case 2: return { 1.0, 1.0 };
    case 3: return { 0.0, 1.0 };
    default: return { 0.0, 0.0 };
    }
}

void quadLeastSquaresParametricDerivatives(double u,
                                           double v,
                                           double (&ddu)[4],
                                           double (&ddv)[4])
{
    ddu[0] = -(1.0 - v);
    ddu[1] = +(1.0 - v);
    ddu[2] = +v;
    ddu[3] = -v;

    ddv[0] = -(1.0 - u);
    ddv[1] = -u;
    ddv[2] = +u;
    ddv[3] = +(1.0 - u);
}

bool invertSymmetric2(double a00, double a01, double a11, double inv[2][2])
{
    const double det = a00 * a11 - a01 * a01;
    if (std::abs(det) <= 1e-18) {
        return false;
    }

    const double invDet = 1.0 / det;
    inv[0][0] = +a11 * invDet;
    inv[0][1] = -a01 * invDet;
    inv[1][0] = -a01 * invDet;
    inv[1][1] = +a00 * invDet;
    return true;
}

bool supportsLeastSquaresOperatorPrerequisites(const DataObject& data)
{
    const size_t numCells = data.cellCount();
    const size_t numPoints = data.pointCount();

    if (data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED ||
        data.points.size() != numPoints * 3 ||
        data.cellOffsets.size() != numCells + 1 ||
        data.cellTypes.size() != numCells ||
        data.pointInCellNeighborOffsets.size() != numPoints + 1 ||
        data.cellOffsets.empty() ||
        data.pointInCellNeighborOffsets.empty() ||
        data.cellOffsets.back() < 0 ||
        data.pointInCellNeighborOffsets.back() < 0 ||
        static_cast<size_t>(data.cellOffsets.back()) > data.cells.size() ||
        static_cast<size_t>(data.pointInCellNeighborOffsets.back()) > data.pointInCellNeighbors.size()) {
        return false;
    }

    return true;
}

bool buildQuadLeastSquaresGradientCoefficients(const DataObject& data,
                                               int cellId,
                                               double u,
                                               double v,
                                               int (&pointIds)[4],
                                               std::array<double, 3> (&coeffs)[4])
{
    const size_t numCells = data.cellCount();
    const size_t numPoints = data.pointCount();
    if (cellId < 0 ||
        static_cast<size_t>(cellId) >= numCells ||
        data.cellOffsets.size() != numCells + 1 ||
        data.cellTypes.size() != numCells ||
        data.cellTypes[static_cast<size_t>(cellId)] != kVtkQuadCellType) {
        return false;
    }

    const int begin = data.cellOffsets[static_cast<size_t>(cellId)];
    const int end = data.cellOffsets[static_cast<size_t>(cellId) + 1];
    if (end - begin != 4) {
        return false;
    }

    std::array<std::array<double, 3>, 4> x{};
    for (int localId = 0; localId < 4; ++localId) {
        const int pointId = data.cells[static_cast<size_t>(begin + localId)];
        if (pointId < 0 || static_cast<size_t>(pointId) >= numPoints) {
            return false;
        }
        pointIds[localId] = pointId;
        x[localId] = loadPosition(data.points, pointId);
    }

    double ddu[4] = { 0.0, 0.0, 0.0, 0.0 };
    double ddv[4] = { 0.0, 0.0, 0.0, 0.0 };
    quadLeastSquaresParametricDerivatives(u, v, ddu, ddv);

    std::array<double, 3> xu{ 0.0, 0.0, 0.0 };
    std::array<double, 3> xv{ 0.0, 0.0, 0.0 };
    for (int localId = 0; localId < 4; ++localId) {
        for (int axis = 0; axis < 3; ++axis) {
            xu[axis] += ddu[localId] * x[localId][axis];
            xv[axis] += ddv[localId] * x[localId][axis];
        }
    }

    const double g00 = dot3(xu, xu);
    const double g01 = dot3(xu, xv);
    const double g11 = dot3(xv, xv);
    double invMetric[2][2] = {
        { 0.0, 0.0 },
        { 0.0, 0.0 }
    };
    if (!invertSymmetric2(g00, g01, g11, invMetric)) {
        return false;
    }

    for (int localId = 0; localId < 4; ++localId) {
        const double alpha =
            invMetric[0][0] * ddu[localId] +
            invMetric[0][1] * ddv[localId];
        const double beta =
            invMetric[1][0] * ddu[localId] +
            invMetric[1][1] * ddv[localId];
        coeffs[localId] = {
            alpha * xu[0] + beta * xv[0],
            alpha * xu[1] + beta * xv[1],
            alpha * xu[2] + beta * xv[2]
        };
    }

    return true;
}

bool buildPlanarLeastSquaresGradientCoefficients(
    const DataObject& data,
    int cellId,
    std::vector<int>& pointIds,
    std::vector<std::array<double, 3>>& coeffs)
{
    const size_t numCells = data.cellCount();
    const size_t numPoints = data.pointCount();
    if (cellId < 0 ||
        static_cast<size_t>(cellId) >= numCells ||
        data.cellOffsets.size() != numCells + 1 ||
        data.cellTypes.size() != numCells ||
        data.points.size() != numPoints * 3 ||
        data.cells.empty()) {
        return false;
    }

    const int begin = data.cellOffsets[static_cast<size_t>(cellId)];
    const int end = data.cellOffsets[static_cast<size_t>(cellId) + 1];
    const int pointCount = end - begin;
    if (pointCount < 3) {
        return false;
    }

    LocalSpectralInfo geom;
    if (!computeCellLocalGeometry(data, cellId, geom) || geom.dimTag != 2) {
        return false;
    }

    const std::array<double, 3> center = loadPosition(data.cellCenters, cellId);
    const std::array<double, 3> e1{ geom.frame[0], geom.frame[1], geom.frame[2] };
    const std::array<double, 3> e2{ geom.frame[3], geom.frame[4], geom.frame[5] };

    double normal[4][4] = {
        { 0.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 }
    };
    std::vector<std::array<double, 3>> rows(static_cast<size_t>(pointCount));
    pointIds.resize(static_cast<size_t>(pointCount));
    coeffs.assign(static_cast<size_t>(pointCount), { 0.0, 0.0, 0.0 });

    for (int localId = 0; localId < pointCount; ++localId) {
        const int pointId = data.cells[static_cast<size_t>(begin + localId)];
        if (pointId < 0 || static_cast<size_t>(pointId) >= numPoints) {
            return false;
        }

        const std::array<double, 3> d = sub3(loadPosition(data.points, pointId), center);
        const std::array<double, 3> row{
            1.0,
            dot3(d, e1),
            dot3(d, e2)
        };
        rows[static_cast<size_t>(localId)] = row;
        pointIds[static_cast<size_t>(localId)] = pointId;

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                normal[r][c] += row[static_cast<size_t>(r)] * row[static_cast<size_t>(c)];
            }
        }
    }

    const double trace = normal[0][0] + normal[1][1] + normal[2][2];
    const double reg = std::max(1e-12 * trace, 1e-12);
    normal[1][1] += reg;
    normal[2][2] += reg;

    for (int localId = 0; localId < pointCount; ++localId) {
        double a[4][4] = {
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 }
        };
        double rhs[4] = {
            rows[static_cast<size_t>(localId)][0],
            rows[static_cast<size_t>(localId)][1],
            rows[static_cast<size_t>(localId)][2],
            0.0
        };
        double sol[4] = { 0.0, 0.0, 0.0, 0.0 };

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                a[r][c] = normal[r][c];
            }
        }
        if (!solveDenseSystem(a, rhs, 3, sol)) {
            return false;
        }

        coeffs[static_cast<size_t>(localId)] = {
            sol[1] * e1[0] + sol[2] * e2[0],
            sol[1] * e1[1] + sol[2] * e2[1],
            sol[1] * e1[2] + sol[2] * e2[2]
        };
    }

    return true;
}

void appendGradientEntry(std::vector<int>& ids,
                         std::vector<std::array<double, 3>>& coeffs,
                         int sourceId,
                         const std::array<double, 3>& coeff)
{
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == sourceId) {
            coeffs[i][0] += coeff[0];
            coeffs[i][1] += coeff[1];
            coeffs[i][2] += coeff[2];
            return;
        }
    }

    ids.push_back(sourceId);
    coeffs.push_back(coeff);
}

bool buildCellLocalGradientEntries(const DataObject& data,
                                   int cellId,
                                   bool evaluateAtCenter,
                                   int pointId,
                                   std::vector<int>& pointIds,
                                   std::vector<std::array<double, 3>>& coeffs)
{
    const size_t numCells = data.cellCount();
    if (cellId < 0 ||
        static_cast<size_t>(cellId) >= numCells ||
        data.cellOffsets.size() != numCells + 1 ||
        data.cellTypes.size() != numCells) {
        return false;
    }

    const int begin = data.cellOffsets[static_cast<size_t>(cellId)];
    const int end = data.cellOffsets[static_cast<size_t>(cellId) + 1];
    const int cellType = data.cellTypes[static_cast<size_t>(cellId)];
    const int pointCount = end - begin;
    if (pointCount < 3) {
        return false;
    }

    if (cellType == kVtkQuadCellType && pointCount == 4) {
        int quadPointIds[4] = { -1, -1, -1, -1 };
        std::array<double, 3> quadCoeffs[4];
        double u = 0.5;
        double v = 0.5;

        if (!evaluateAtCenter) {
            int localVertex = -1;
            for (int localId = 0; localId < 4; ++localId) {
                if (data.cells[static_cast<size_t>(begin + localId)] == pointId) {
                    localVertex = localId;
                    break;
                }
            }
            if (localVertex < 0) {
                return false;
            }
            const std::array<double, 2> uv = quadNodeParamCoord(localVertex);
            u = uv[0];
            v = uv[1];
        }

        if (buildQuadLeastSquaresGradientCoefficients(data, cellId, u, v, quadPointIds, quadCoeffs)) {
            pointIds.assign(quadPointIds, quadPointIds + 4);
            coeffs.assign(quadCoeffs, quadCoeffs + 4);
            return true;
        }
    }

    if (cellTopologicalDimensionFromType(cellType) == 2 &&
        (cellType == kVtkTriangleCellType ||
         cellType == kVtkPolygonCellType ||
         cellType == VTK_PIXEL ||
         cellType == kVtkQuadCellType ||
         pointCount >= 3)) {
        return buildPlanarLeastSquaresGradientCoefficients(data, cellId, pointIds, coeffs);
    }

    return false;
}

bool buildPointGradientOperator(const DataObject& data,
                                std::vector<int>& offsets,
                                std::vector<int>& sourceIndices,
                                std::vector<float>& coeffs4)
{
    const size_t numPoints = data.pointCount();
    if (!supportsLeastSquaresOperatorPrerequisites(data) ||
        data.pointInCellNeighborOffsets.size() != numPoints + 1) {
        return false;
    }

    offsets.clear();
    sourceIndices.clear();
    coeffs4.clear();
    offsets.reserve(numPoints + 1);
    offsets.push_back(0);

    for (size_t pointId = 0; pointId < numPoints; ++pointId) {
        const int begin = data.pointInCellNeighborOffsets[pointId];
        const int end = data.pointInCellNeighborOffsets[pointId + 1];
        if (end <= begin) {
            offsets.push_back(static_cast<int>(sourceIndices.size()));
            continue;
        }

        std::vector<int> localSources;
        std::vector<std::array<double, 3>> localCoeffs;
        int validCellCount = 0;

        for (int k = begin; k < end; ++k) {
            const int cellId = data.pointInCellNeighbors[static_cast<size_t>(k)];
            if (cellId < 0 || static_cast<size_t>(cellId) >= data.cellCount()) {
                continue;
            }

            std::vector<int> cellPointIds;
            std::vector<std::array<double, 3>> cellCoeffs;
            if (!buildCellLocalGradientEntries(
                    data,
                    cellId,
                    false,
                    static_cast<int>(pointId),
                    cellPointIds,
                    cellCoeffs)) {
                continue;
            }

            ++validCellCount;
            for (size_t localId = 0; localId < cellPointIds.size(); ++localId) {
                appendGradientEntry(localSources, localCoeffs, cellPointIds[localId], cellCoeffs[localId]);
            }
        }

        if (validCellCount <= 0) {
            return false;
        }

        const double invCellCount = 1.0 / static_cast<double>(validCellCount);
        for (size_t i = 0; i < localSources.size(); ++i) {
            sourceIndices.push_back(localSources[i]);
            coeffs4.push_back(static_cast<float>(localCoeffs[i][0] * invCellCount));
            coeffs4.push_back(static_cast<float>(localCoeffs[i][1] * invCellCount));
            coeffs4.push_back(static_cast<float>(localCoeffs[i][2] * invCellCount));
            coeffs4.push_back(0.0f);
        }
        offsets.push_back(static_cast<int>(sourceIndices.size()));
    }

    return true;
}

bool buildPointValueReconstructionFromCells(const DataObject& data,
                                            std::vector<int>& offsets,
                                            std::vector<int>& sourceIndices,
                                            std::vector<float>& weights)
{
    const size_t numPoints = data.pointCount();
    const size_t numCells = data.cellCount();
    if (data.pointInCellNeighborOffsets.size() != numPoints + 1) {
        return false;
    }

    offsets.clear();
    sourceIndices.clear();
    weights.clear();
    offsets.reserve(numPoints + 1);
    offsets.push_back(0);

    for (size_t pointId = 0; pointId < numPoints; ++pointId) {
        const int begin = data.pointInCellNeighborOffsets[pointId];
        const int end = data.pointInCellNeighborOffsets[pointId + 1];
        if (end <= begin) {
            offsets.push_back(static_cast<int>(sourceIndices.size()));
            continue;
        }

        int validCount = 0;
        for (int k = begin; k < end; ++k) {
            const int cellId = data.pointInCellNeighbors[static_cast<size_t>(k)];
            if (cellId >= 0 && static_cast<size_t>(cellId) < numCells) {
                ++validCount;
            }
        }
        if (validCount <= 0) {
            return false;
        }

        const float weight = 1.0f / static_cast<float>(validCount);
        for (int k = begin; k < end; ++k) {
            const int cellId = data.pointInCellNeighbors[static_cast<size_t>(k)];
            if (cellId < 0 || static_cast<size_t>(cellId) >= numCells) {
                continue;
            }
            sourceIndices.push_back(cellId);
            weights.push_back(weight);
        }
        offsets.push_back(static_cast<int>(sourceIndices.size()));
    }

    return true;
}

bool buildCellGradientOperator(const DataObject& data,
                               std::vector<int>& offsets,
                               std::vector<int>& sourceIndices,
                               std::vector<float>& coeffs4)
{
    const size_t numCells = data.cellCount();
    if (!supportsLeastSquaresOperatorPrerequisites(data)) {
        return false;
    }

    offsets.clear();
    sourceIndices.clear();
    coeffs4.clear();
    offsets.reserve(numCells + 1);
    offsets.push_back(0);

    for (size_t cellId = 0; cellId < numCells; ++cellId) {
        std::vector<int> cellPointIds;
        std::vector<std::array<double, 3>> cellCoeffs;
        if (!buildCellLocalGradientEntries(
                data,
                static_cast<int>(cellId),
                true,
                -1,
                cellPointIds,
                cellCoeffs)) {
            return false;
        }

        for (size_t localId = 0; localId < cellPointIds.size(); ++localId) {
            sourceIndices.push_back(cellPointIds[localId]);
            coeffs4.push_back(static_cast<float>(cellCoeffs[localId][0]));
            coeffs4.push_back(static_cast<float>(cellCoeffs[localId][1]));
            coeffs4.push_back(static_cast<float>(cellCoeffs[localId][2]));
            coeffs4.push_back(0.0f);
        }
        offsets.push_back(static_cast<int>(sourceIndices.size()));
    }

    return true;
}

void accumulateWeightedSample(double normal[4][4],
                              std::vector<double>& rhs,
                              const double row[4],
                              int unknownCount,
                              const float* values,
                              int comps,
                              double weight)
{
    if (weight <= 0.0) {
        return;
    }

    for (int r = 0; r < unknownCount; ++r) {
        for (int c = 0; c < unknownCount; ++c) {
            normal[r][c] += weight * row[r] * row[c];
        }
        for (int comp = 0; comp < comps; ++comp) {
            rhs[static_cast<size_t>(comp) * 4 + static_cast<size_t>(r)] +=
                weight * row[r] * static_cast<double>(values[static_cast<size_t>(comp)]);
        }
    }
}

bool fitCellGradientFromNeighborCenters3D(const DataObject& data,
                                          const DataArray& src,
                                          const std::vector<int>& sampleOffsets,
                                          const std::vector<int>& sampleNeighbors,
                                          int cellId,
                                          int comps,
                                          double exponent,
                                          float lambda,
                                          std::vector<float>& outGrad)
{
    const size_t numCells = data.cellCount();
    if (cellId < 0 ||
        static_cast<size_t>(cellId) >= numCells ||
        sampleOffsets.size() != numCells + 1 ||
        sampleOffsets.empty() ||
        sampleOffsets.back() < 0 ||
        static_cast<size_t>(sampleOffsets.back()) > sampleNeighbors.size() ||
        data.cellCenters.size() != numCells * 3u) {
        return false;
    }

    const std::array<double, 3> center = loadPosition(data.cellCenters, cellId);
    const float* cellTuple = src.data.data() + static_cast<size_t>(cellId) * static_cast<size_t>(comps);
    const int nbBegin = sampleOffsets[static_cast<size_t>(cellId)];
    const int nbEnd = sampleOffsets[static_cast<size_t>(cellId) + 1];

    std::vector<int> validNeighbors;
    std::vector<double> distances;
    validNeighbors.reserve(static_cast<size_t>(std::max(nbEnd - nbBegin, 0)));
    distances.reserve(validNeighbors.capacity());

    double meanDistance = 0.0;
    for (int k = nbBegin; k < nbEnd; ++k) {
        const int nb = sampleNeighbors[static_cast<size_t>(k)];
        if (nb < 0 || static_cast<size_t>(nb) >= numCells || nb == cellId) {
            continue;
        }

        const std::array<double, 3> d = sub3(loadPosition(data.cellCenters, nb), center);
        const double dist = norm3(d);
        if (dist <= 1e-12) {
            continue;
        }

        validNeighbors.push_back(nb);
        distances.push_back(dist);
        meanDistance += dist;
    }

    if (validNeighbors.size() < 3u) {
        return false;
    }

    const double scale = std::max(meanDistance / static_cast<double>(validNeighbors.size()), 1e-6);
    double normal[4][4] = {
        { 0.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 }
    };
    std::vector<double> rhs(static_cast<size_t>(comps) * 4u, 0.0);

    const double centerRow[4] = { 1.0, 0.0, 0.0, 0.0 };
    accumulateWeightedSample(normal, rhs, centerRow, 4, cellTuple, comps, 1.0);

    for (size_t i = 0; i < validNeighbors.size(); ++i) {
        const int nb = validNeighbors[i];
        const std::array<double, 3> d = sub3(loadPosition(data.cellCenters, nb), center);
        const double row[4] = {
            1.0,
            d[0] / scale,
            d[1] / scale,
            d[2] / scale
        };
        const double dist = distances[i];
        const double weight = 1.0 / std::pow(std::max(dist, 1e-6), exponent);
        const float* nbTuple = src.data.data() + static_cast<size_t>(nb) * static_cast<size_t>(comps);
        accumulateWeightedSample(normal, rhs, row, 4, nbTuple, comps, weight);
    }

    const double trace = normal[1][1] + normal[2][2] + normal[3][3];
    const double reg = std::max(
        static_cast<double>(lambda) * std::max(trace / 3.0, 1.0),
        1e-10);
    normal[1][1] += reg;
    normal[2][2] += reg;
    normal[3][3] += reg;

    for (int comp = 0; comp < comps; ++comp) {
        double a[4][4] = {
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 }
        };
        double bVec[4] = { 0.0, 0.0, 0.0, 0.0 };
        double coeff[4] = { 0.0, 0.0, 0.0, 0.0 };

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                a[r][c] = normal[r][c];
            }
            bVec[r] = rhs[static_cast<size_t>(comp) * 4u + static_cast<size_t>(r)];
        }

        if (!solveDenseSystem(a, bVec, 4, coeff)) {
            return false;
        }

        const size_t outBase =
            static_cast<size_t>(cellId) * static_cast<size_t>(comps * 3) + static_cast<size_t>(comp * 3);
        outGrad[outBase + 0] = static_cast<float>(coeff[1] / scale);
        outGrad[outBase + 1] = static_cast<float>(coeff[2] / scale);
        outGrad[outBase + 2] = static_cast<float>(coeff[3] / scale);
    }

    return true;
}

bool computeUnstructuredCellDataWLS(const DataObject& data,
                                    const DataArray& src,
                                    const std::vector<int>& sampleOffsets,
                                    const std::vector<int>& sampleNeighbors,
                                    float wExponent,
                                    float lambda,
                                    std::vector<float>& outGrad)
{
    const size_t numCells = data.cellCount();
    const size_t numPoints = data.pointCount();
    const int comps = src.numComponents;
    if (data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED ||
        src.dataType != CELL_DATA ||
        comps <= 0 ||
        src.data.size() != numCells * static_cast<size_t>(comps) ||
        data.cellOffsets.size() != numCells + 1 ||
        data.cellOffsets.empty() ||
        data.cellOffsets.back() < 0 ||
        static_cast<size_t>(data.cellOffsets.back()) > data.cells.size() ||
        data.cellCenters.size() != numCells * 3 ||
        data.points.size() != numPoints * 3) {
        return false;
    }

    outGrad.assign(numCells * static_cast<size_t>(comps) * 3u, 0.0f);

    const bool hasNeighborSamples =
        sampleOffsets.size() == numCells + 1 &&
        !sampleOffsets.empty() &&
        sampleOffsets.back() >= 0 &&
        static_cast<size_t>(sampleOffsets.back()) <= sampleNeighbors.size();
    const double exponent = std::max(static_cast<double>(wExponent), 0.0);
    std::vector<float> pointValues;
    bool pointValuesReady = false;

    for (size_t cellId = 0; cellId < numCells; ++cellId) {
        LocalSpectralInfo geom;
        if (!computeCellLocalGeometry(data, static_cast<int>(cellId), geom)) {
            continue;
        }

        const int begin = data.cellOffsets[cellId];
        const int end = data.cellOffsets[cellId + 1];
        if (end <= begin) {
            continue;
        }

        const int fitDim = geom.dimTag >= 3 ? 3 : (geom.dimTag <= 1 ? 1 : 2);
        if (fitDim == 3 && hasNeighborSamples) {
            if (fitCellGradientFromNeighborCenters3D(
                    data,
                    src,
                    sampleOffsets,
                    sampleNeighbors,
                    static_cast<int>(cellId),
                    comps,
                    exponent,
                    lambda,
                    outGrad)) {
                continue;
            }
        }

        if (!pointValuesReady) {
            if (!reconstructPointValuesFromCells(data, src, pointValues)) {
                return false;
            }
            pointValuesReady = true;
        }

        const int unknownCount = fitDim + 1;
        const float* cellTuple = src.data.data() + cellId * static_cast<size_t>(comps);
        const std::array<double, 3> center = loadPosition(data.cellCenters, static_cast<int>(cellId));
        const std::array<double, 3> e1{ geom.frame[0], geom.frame[1], geom.frame[2] };
        const std::array<double, 3> e2{ geom.frame[3], geom.frame[4], geom.frame[5] };
        const std::array<double, 3> e3{ geom.frame[6], geom.frame[7], geom.frame[8] };

        double normal[4][4] = {
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0, 0.0 }
        };
        std::vector<double> rhs(static_cast<size_t>(comps) * 4u, 0.0);

        const double centerRow[4] = { 1.0, 0.0, 0.0, 0.0 };
        accumulateWeightedSample(normal, rhs, centerRow, unknownCount, cellTuple, comps, 1.0);

        int vertexSamples = 0;
        for (int k = begin; k < end; ++k) {
            const int pointId = data.cells[static_cast<size_t>(k)];
            if (pointId < 0 || static_cast<size_t>(pointId) >= numPoints) {
                continue;
            }

            const std::array<double, 3> d = sub3(loadPosition(data.points, pointId), center);
            const double local[3] = {
                dot3(d, e1),
                dot3(d, e2),
                dot3(d, e3)
            };
            double dist2 = local[0] * local[0];
            if (fitDim >= 2) {
                dist2 += local[1] * local[1];
            }
            if (fitDim >= 3) {
                dist2 += local[2] * local[2];
            }
            if (dist2 <= 1e-18) {
                continue;
            }

            const double row[4] = {
                1.0,
                local[0],
                fitDim >= 2 ? local[1] : 0.0,
                fitDim >= 3 ? local[2] : 0.0
            };
            const double dist = std::sqrt(dist2);
            const double weight = 1.0 / std::pow(std::max(dist, 1e-6), exponent);
            const float* pointTuple = pointValues.data() + static_cast<size_t>(pointId) * static_cast<size_t>(comps);
            accumulateWeightedSample(normal, rhs, row, unknownCount, pointTuple, comps, weight);
            ++vertexSamples;
        }

        const bool useNeighborCenters =
            hasNeighborSamples && vertexSamples < unknownCount;
        if (useNeighborCenters) {
            const int nbBegin = sampleOffsets[cellId];
            const int nbEnd = sampleOffsets[cellId + 1];
            for (int k = nbBegin; k < nbEnd; ++k) {
                const int nb = sampleNeighbors[static_cast<size_t>(k)];
                if (nb < 0 || static_cast<size_t>(nb) >= numCells || nb == static_cast<int>(cellId)) {
                    continue;
                }

                const std::array<double, 3> d = sub3(loadPosition(data.cellCenters, nb), center);
                const double local[3] = {
                    dot3(d, e1),
                    dot3(d, e2),
                    dot3(d, e3)
                };
                double dist2 = local[0] * local[0];
                if (fitDim >= 2) {
                    dist2 += local[1] * local[1];
                }
                if (fitDim >= 3) {
                    dist2 += local[2] * local[2];
                }
                if (dist2 <= 1e-18) {
                    continue;
                }

                const double row[4] = {
                    1.0,
                    local[0],
                    fitDim >= 2 ? local[1] : 0.0,
                    fitDim >= 3 ? local[2] : 0.0
                };
                const double dist = std::sqrt(dist2);
                const double weight = 0.35 / std::pow(std::max(dist, 1e-6), exponent);
                const float* nbTuple = src.data.data() + static_cast<size_t>(nb) * static_cast<size_t>(comps);
                accumulateWeightedSample(normal, rhs, row, unknownCount, nbTuple, comps, weight);
            }
        }

        double trace = 0.0;
        for (int axis = 1; axis < unknownCount; ++axis) {
            trace += normal[axis][axis];
        }
        const double reg = std::max(
            static_cast<double>(lambda) * std::max(trace / static_cast<double>(std::max(1, fitDim)), 1.0),
            1e-10);
        for (int axis = 1; axis < unknownCount; ++axis) {
            normal[axis][axis] += reg;
        }

        for (int comp = 0; comp < comps; ++comp) {
            double a[4][4] = {
                { 0.0, 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0, 0.0 }
            };
            double bVec[4] = { 0.0, 0.0, 0.0, 0.0 };
            double coeff[4] = { 0.0, 0.0, 0.0, 0.0 };

            for (int r = 0; r < unknownCount; ++r) {
                for (int c = 0; c < unknownCount; ++c) {
                    a[r][c] = normal[r][c];
                }
                bVec[r] = rhs[static_cast<size_t>(comp) * 4u + static_cast<size_t>(r)];
            }

            if (!solveDenseSystem(a, bVec, unknownCount, coeff)) {
                continue;
            }

            const std::array<double, 3> grad = {
                coeff[1] * e1[0] +
                    (fitDim >= 2 ? coeff[2] * e2[0] : 0.0) +
                    (fitDim >= 3 ? coeff[3] * e3[0] : 0.0),
                coeff[1] * e1[1] +
                    (fitDim >= 2 ? coeff[2] * e2[1] : 0.0) +
                    (fitDim >= 3 ? coeff[3] * e3[1] : 0.0),
                coeff[1] * e1[2] +
                    (fitDim >= 2 ? coeff[2] * e2[2] : 0.0) +
                    (fitDim >= 3 ? coeff[3] * e3[2] : 0.0)
            };

            const size_t outBase =
                cellId * static_cast<size_t>(comps * 3) + static_cast<size_t>(comp * 3);
            outGrad[outBase + 0] = static_cast<float>(grad[0]);
            outGrad[outBase + 1] = static_cast<float>(grad[1]);
            outGrad[outBase + 2] = static_cast<float>(grad[2]);
        }
    }

    return true;
}

struct AnalyticBenchmarkFrame
{
    std::array<double, 3> center{ 0.0, 0.0, 0.0 };
    double refLength = 1.0;
};

bool buildAnalyticBenchmarkFrame(const std::vector<float>& positions,
                                 AnalyticBenchmarkFrame& frame)
{
    if (positions.empty() || (positions.size() % 3u) != 0u) {
        return false;
    }

    std::array<double, 3> mn{
        static_cast<double>(positions[0]),
        static_cast<double>(positions[1]),
        static_cast<double>(positions[2])
    };
    std::array<double, 3> mx = mn;

    const size_t tupleCount = positions.size() / 3u;
    for (size_t i = 1; i < tupleCount; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            const double v = static_cast<double>(positions[i * 3u + static_cast<size_t>(axis)]);
            mn[axis] = std::min(mn[axis], v);
            mx[axis] = std::max(mx[axis], v);
        }
    }

    double maxExtent = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        frame.center[axis] = 0.5 * (mn[axis] + mx[axis]);
        maxExtent = std::max(maxExtent, mx[axis] - mn[axis]);
    }
    frame.refLength = std::max(maxExtent, 1.0);
    return true;
}

void appendAnalyticScalarBenchmarks(DataObject& data,
                                    const std::vector<float>& positions,
                                    DataArrayType type)
{
    const size_t tupleCount = positions.size() / 3u;
    if (tupleCount == 0 || positions.size() != tupleCount * 3u) {
        return;
    }

    AnalyticBenchmarkFrame frame;
    if (!buildAnalyticBenchmarkFrame(positions, frame)) {
        return;
    }

    const double cx = frame.center[0];
    const double cy = frame.center[1];
    const double cz = frame.center[2];
    const double L = frame.refLength;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<float> linear(tupleCount, 0.0f);
    std::vector<float> linearGrad(tupleCount * 3u, 0.0f);
    std::vector<float> quadratic(tupleCount, 0.0f);
    std::vector<float> quadraticGrad(tupleCount * 3u, 0.0f);
    std::vector<float> trig(tupleCount, 0.0f);
    std::vector<float> trigGrad(tupleCount * 3u, 0.0f);

    for (size_t i = 0; i < tupleCount; ++i) {
        const double x = static_cast<double>(positions[i * 3u + 0]);
        const double y = static_cast<double>(positions[i * 3u + 1]);
        const double z = static_cast<double>(positions[i * 3u + 2]);
        const double xr = x - cx;
        const double yr = y - cy;
        const double zr = z - cz;

        linear[i] = static_cast<float>(0.75 * xr - 1.25 * yr + 2.0 * zr);
        linearGrad[i * 3u + 0] = 0.75f;
        linearGrad[i * 3u + 1] = -1.25f;
        linearGrad[i * 3u + 2] = 2.0f;

        quadratic[i] = static_cast<float>(
            (xr * xr + 0.5 * yr * yr - 0.25 * zr * zr +
             0.1 * xr * yr - 0.15 * yr * zr + 0.2 * zr * xr) / L);
        quadraticGrad[i * 3u + 0] = static_cast<float>((2.0 * xr + 0.1 * yr + 0.2 * zr) / L);
        quadraticGrad[i * 3u + 1] = static_cast<float>((yr + 0.1 * xr - 0.15 * zr) / L);
        quadraticGrad[i * 3u + 2] = static_cast<float>((-0.5 * zr - 0.15 * yr + 0.2 * xr) / L);

        const double sx = kPi * xr / L;
        const double sy = kPi * yr / L;
        const double sz = kPi * zr / L;
        trig[i] = static_cast<float>(L * (std::sin(sx) + 0.5 * std::cos(sy) + 0.25 * std::sin(sz)));
        trigGrad[i * 3u + 0] = static_cast<float>(kPi * std::cos(sx));
        trigGrad[i * 3u + 1] = static_cast<float>(-0.5 * kPi * std::sin(sy));
        trigGrad[i * 3u + 2] = static_cast<float>(0.25 * kPi * std::cos(sz));
    }

    data.upsertDataArray("benchmark_linear", linear, 1, type);
    data.upsertDataArray("benchmark_linear_exact_grad", linearGrad, 3, type);
    data.upsertDataArray("benchmark_quadratic", quadratic, 1, type);
    data.upsertDataArray("benchmark_quadratic_exact_grad", quadraticGrad, 3, type);
    data.upsertDataArray("benchmark_trig", trig, 1, type);
    data.upsertDataArray("benchmark_trig_exact_grad", trigGrad, 3, type);
}

void appendAnalyticBenchmarkArrays(DataObject& data)
{
    appendAnalyticScalarBenchmarks(data, data.points, POINT_DATA);
    appendAnalyticScalarBenchmarks(data, data.cellCenters, CELL_DATA);
}
}


CAEProcessingFacade::CAEProcessingFacade() {}
CAEProcessingFacade::~CAEProcessingFacade() {}

bool CAEProcessingFacade::initialize(const std::string& shaderDir)
{
    if (!m_gl.initialize(false)) return false;

    m_engine.setShaderDir(shaderDir);
    m_filter.setShaderDir(shaderDir);

    const bool okGrad = m_engine.init();
    const bool okFilter = m_filter.init();

    m_initialized = okGrad && okFilter;
    if (m_initialized) {
        m_engine.setEnableGpuTiming(true);
        m_filter.setEnableGpuTiming(true);
    }
    return m_initialized;
}

void CAEProcessingFacade::setAnalyticBenchmarkEnabled(bool enabled)
{
    m_appendAnalyticBenchmarkArrays = enabled;
}

bool CAEProcessingFacade::isAnalyticBenchmarkEnabled() const
{
    return m_appendAnalyticBenchmarkArrays;
}

std::string CAEProcessingFacade::loadDatasetFromVTKFile(const std::string& filePath)
{
    vtkNew<vtkDataSetReader> r;
    r->SetFileName(filePath.c_str());
    r->Update();
    vtkDataSet* ds = vtkDataSet::SafeDownCast(r->GetOutput());
    if (!ds) return std::string();

    auto rec = std::make_unique<DatasetRecord>();
    rec->id = "ds_" + std::to_string(m_nextId++);
    rec->displayName = fileNameFromPath(filePath);
    rec->sourceVtk = ds;

    VTKDataConverter conv;
    conv.bindVTKDataAndInternalData(ds, &rec->data);
    if (!conv.convertVTKToInternal()) return std::string();
    if (m_appendAnalyticBenchmarkArrays) {
        appendAnalyticBenchmarkArrays(rec->data);
    }

    std::string id = rec->id;
    m_records[id] = std::move(rec);
    return id;
}

std::vector<CAEDatasetSummary> CAEProcessingFacade::listDatasets() const
{
    //
    std::vector<CAEDatasetSummary> out;
    out.reserve(m_records.size());
    for (const auto& kv : m_records) {
        const auto& rec = *kv.second;
        CAEDatasetSummary s;
        s.datasetId = rec.id;
        s.displayName = rec.displayName;
        s.gridClass = toGridClass(rec.data.gridType);
        s.pointCount = rec.data.pointCount();
        s.cellCount = rec.data.cellCount();
        for (const auto& a : rec.data.dataArrays) {
            CAEFieldInfo fi;
            fi.name = a.name;
            fi.association = toAssociation(a.dataType);
            fi.numComponents = a.numComponents;
            fi.tupleCount = a.numComponents > 0 ? a.data.size() / static_cast<std::size_t>(a.numComponents) : 0;
            s.fields.push_back(std::move(fi));
        }
        s.results = rec.results;
        out.push_back(std::move(s));
    }
    return out;
}

bool CAEProcessingFacade::computeGradient(const CAEGradientRequest& req,
    CAEGradientResultMeta& outMeta)
{
    auto it = m_records.find(req.datasetId);
    if (it == m_records.end() || !m_initialized) {
        return false;
    }
    DatasetRecord& rec = *it->second;

    DataArray* src = rec.data.findDataArray(req.inputArrayName,
        toDataArrayType(req.association));
    if (!src || src->numComponents <= 0) {
        return false;
    }

    CAEGradientMethod method = req.method;
    if (method == CAEGradientMethod::Auto) {
        if (rec.data.gridType == DATA_OBJECT_TYPE_RegularGrid) {
            method = CAEGradientMethod::FiniteDifference;
        } else {
            method = CAEGradientMethod::AdaptiveWeightedLeastSquares;
        }
    }
    if (rec.data.gridType == DATA_OBJECT_TYPE_RegularGrid &&
        method != CAEGradientMethod::FiniteDifference) {
        method = CAEGradientMethod::FiniteDifference;
    }

    const std::string sourceName = src->name;
    const int inputComponents = src->numComponents;

    std::vector<float> grad;
    bool ok = false;

    // The GUI owns a separate VTK/Qt render context. Make the facade's
    // compute context current before issuing any GL commands.
    m_gl.makeCurrent();
    m_lastComputeGpuMs = 0.0;

    auto t0 = std::chrono::high_resolution_clock::now();
    if (method == CAEGradientMethod::FiniteDifference) {
        ok = computeByFD(rec, *src, grad);
    } else if (method == CAEGradientMethod::WeightedLeastSquares) {
        ok = computeByWLS(rec, *src, req.association,
                          req.wlsExponent, req.wlsLambda, grad);
    } else {
        ok = computeByAdaptiveWLS(rec, *src, req, grad);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    if (!ok) {
        return false;
    }

    m_lastComputeWallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::string resultName = makeResultName(sourceName,
                                            req.association,
                                            method);

    if (!rec.data.upsertDataArray(resultName,
                                  grad,
                                  3 * inputComponents,
                                  toDataArrayType(req.association))) {
        return false;
    }

    CAEGradientResultMeta meta;
    meta.resultArrayName   = resultName;
    meta.sourceArrayName   = sourceName;
    meta.association       = req.association;
    meta.method            = method;
    meta.inputComponents   = inputComponents;
    meta.outputComponents  = 3 * inputComponents;
    meta.computeWallMs = m_lastComputeWallMs;
    meta.computeGpuMs = m_lastComputeGpuMs;

    rec.results.push_back(meta);
    outMeta = meta;

    return true;
}

bool CAEProcessingFacade::computeMultiScaleDecompositionAndFusion(const CAEMultiScaleRequest& req,
                                                                  CAEMultiScaleResultMeta& outMeta)
{
    auto it = m_records.find(req.datasetId);
    if (it == m_records.end() || !m_initialized) {
        return false;
    }

    DatasetRecord& rec = *it->second;
    DataArray* src = rec.data.findDataArray(req.inputArrayName, toDataArrayType(req.association));
    if (!src || src->numComponents <= 0 || src->data.empty()) {
        return false;
    }

    // 注意：后面会调用 upsertDataArray，它可能导致 dataArrays 重新分配，
    // 所以不能继续依赖 src 指针，先把后面要用的信息全部缓存下来。
    const std::string sourceName = src->name;
    const int inputComponents = src->numComponents;
    const std::vector<float> sourceData = src->data;

    const int levelCount = std::clamp(req.levels, 1, 3);
    const int iterations = std::max(1, req.iterationsPerLevel);
    const DataArrayType dstType = toDataArrayType(req.association);

    std::vector<float> positions;
    std::vector<int> offsets;
    std::vector<int> neighbors;
    if (!buildFilterGraph(rec.data, req.association, positions, offsets, neighbors)) {
        return false;
    }

    const double meanSpacing = estimateMeanNeighborDistance(positions, offsets, neighbors);
    const double valueStd = estimateStdDev(sourceData);


    const float spatialSigmaBase =
        std::max(1e-6f, req.spatialSigmaFactor * static_cast<float>(meanSpacing > 0.0 ? meanSpacing : 1.0));
    const float rangeSigmaBase =
        std::max(1e-6f, req.rangeSigmaFactor * static_cast<float>(valueStd > 0.0 ? valueStd : 1.0));
    const float edgeSigma =
        std::max(1e-6f, req.edgeSigmaFactor * static_cast<float>(valueStd > 0.0 ? valueStd : 1.0));

    m_gl.makeCurrent();

    auto t0 = std::chrono::high_resolution_clock::now();

    double totalGpuMs = 0.0;
    std::vector<std::vector<float>> smooth(levelCount + 1);
    smooth[0] = sourceData;


    for (int level = 0; level < levelCount; ++level) {
        GLFilterEngine::BilateralParams bp{};
        bp.spatialSigma = spatialSigmaBase * std::pow(req.levelScale, static_cast<float>(level));
        bp.rangeSigma = rangeSigmaBase;

        std::vector<float> current = smooth[level];
        std::vector<float> filtered;

        for (int iter = 0; iter < iterations; ++iter) {
            if (!m_filter.bilateralGraph(positions, offsets, neighbors, current, bp, filtered)) {
                return false;
            }
            current.swap(filtered);
            totalGpuMs += m_filter.getLastGpuTimeMs();
        }

        smooth[level + 1] = std::move(current);
    }

    std::vector<std::vector<float>> detail(levelCount);
    for (int level = 0; level < levelCount; ++level) {
        subtractField(smooth[level], smooth[level + 1], detail[level]);
    }

    const std::vector<float> zero(sourceData.size(), 0.0f);
    const std::vector<float>& d0 = levelCount > 0 ? detail[0] : zero;
    const std::vector<float>& d1 = levelCount > 1 ? detail[1] : zero;
    const std::vector<float>& d2 = levelCount > 2 ? detail[2] : zero;

    GLFilterEngine::FusionParams fp{};
    fp.levelCount = levelCount;
    fp.edgeSigma = edgeSigma;
    fp.detailGains[0] = req.detailGain0;
    fp.detailGains[1] = req.detailGain1;
    fp.detailGains[2] = req.detailGain2;

    std::vector<float> fused;
    if (!m_filter.fuseMultiScale(smooth[levelCount], d0, d1, d2, fp, fused)) {
        return false;
    }
    totalGpuMs += m_filter.getLastGpuTimeMs();

    auto t1 = std::chrono::high_resolution_clock::now();
    m_lastComputeWallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    m_lastComputeGpuMs = totalGpuMs;

    CAEMultiScaleResultMeta meta;
    meta.sourceArrayName = sourceName;
    meta.association = req.association;
    meta.numLevels = levelCount;
    meta.inputComponents = inputComponents;
    meta.computeWallMs = m_lastComputeWallMs;
    meta.computeGpuMs = m_lastComputeGpuMs;

    if (req.storeIntermediate) {
        for (int level = 1; level <= levelCount; ++level) {
            const std::string name = makeSmoothName(sourceName, req.association, level);
            if (!rec.data.upsertDataArray(name, smooth[level], inputComponents, dstType)) {
                return false;
            }
            meta.smoothArrayNames.push_back(name);
        }

        for (int level = 0; level < levelCount; ++level) {
            const std::string name = makeDetailName(sourceName, req.association, level);
            if (!rec.data.upsertDataArray(name, detail[level], inputComponents, dstType)) {
                return false;
            }
            meta.detailArrayNames.push_back(name);
        }

        meta.baseArrayName = meta.smoothArrayNames.back();
    } else {
        meta.baseArrayName = makeBaseName(sourceName, req.association);
        if (!rec.data.upsertDataArray(meta.baseArrayName, smooth[levelCount], inputComponents, dstType)) {
            return false;
        }
    }

    meta.fusedArrayName = makeFusedName(sourceName, req.association);
    if (!rec.data.upsertDataArray(meta.fusedArrayName, fused, inputComponents, dstType)) {
        return false;
    }

    outMeta = meta;
    return true;
}

bool CAEProcessingFacade::exportDatasetToVTK(const std::string& datasetId, vtkSmartPointer<vtkDataSet>& outVtk) const
{
    auto it = m_records.find(datasetId);
    if (it == m_records.end()) return false;

    VTKDataConverter conv;
    conv.bindVTKDataAndInternalData(nullptr, const_cast<DataObject*>(&it->second->data));
    if (!conv.convertInternalToVTK()) return false;
    outVtk = conv.vtkData;
    return outVtk != nullptr;
}

bool CAEProcessingFacade::saveDatasetToVTKFile(const std::string& datasetId, const std::string& filePath, bool binary) const
{
    vtkSmartPointer<vtkDataSet> outVtk;
    if (!exportDatasetToVTK(datasetId, outVtk) || !outVtk) {
        return false;
    }

    vtkNew<vtkDataSetWriter> writer;
    writer->SetFileName(filePath.c_str());
    writer->SetInputData(outVtk);
    if (binary) {
        writer->SetFileTypeToBinary();
    }
    else {
        writer->SetFileTypeToASCII();
    }
    return writer->Write() == 1;
}

bool CAEProcessingFacade::getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const
{
    auto all = listDatasets();
    for (const auto& s : all) {
        if (s.datasetId == datasetId) {
            outSummary = s;
            return true;
        }
    }
    return false;
}

bool CAEProcessingFacade::listFields(const std::string& datasetId, CAEFieldAssociation assoc, std::vector<CAEFieldInfo>& outFields) const
{
    outFields.clear();
    CAEDatasetSummary s;
    if (!getDatasetSummary(datasetId, s)) return false;
    for (const auto& f : s.fields) {
        if (f.association == assoc) outFields.push_back(f);
    }
    return true;
}

bool CAEProcessingFacade::getArrayData(const std::string& datasetId, const std::string& arrayName, CAEFieldAssociation assoc, std::vector<float>& outData, int& outComps) const
{
    auto it = m_records.find(datasetId);
    if (it == m_records.end()) return false;
    const DataArray* arr = it->second->data.findDataArray(arrayName, toDataArrayType(assoc));
    if (!arr) return false;
    outData = arr->data;
    outComps = arr->numComponents;
    return true;
}

double CAEProcessingFacade::getLastComputeWallMs() const
{
    return m_lastComputeWallMs;
}

double CAEProcessingFacade::getLastComputeGpuMs() const
{
    return m_lastComputeGpuMs;
}

CAEGridClass CAEProcessingFacade::toGridClass(GridType t)
{
    return t == DATA_OBJECT_TYPE_RegularGrid ? CAEGridClass::Regular : CAEGridClass::Unstructured;
}

DataArrayType CAEProcessingFacade::toDataArrayType(CAEFieldAssociation a)
{
    return a == CAEFieldAssociation::Point ? POINT_DATA : CELL_DATA;
}

CAEFieldAssociation CAEProcessingFacade::toAssociation(DataArrayType t)
{
    return t == POINT_DATA ? CAEFieldAssociation::Point : CAEFieldAssociation::Cell;
}

std::string CAEProcessingFacade::fileNameFromPath(const std::string& path)
{
    auto p1 = path.find_last_of('\\');
    auto p2 = path.find_last_of('/');
    auto p = std::max(p1, p2);
    if (p == std::string::npos) return path;
    return path.substr(p + 1);
}

std::string CAEProcessingFacade::makeResultName(const std::string& src, CAEFieldAssociation assoc, CAEGradientMethod method)
{
    std::string a = assoc == CAEFieldAssociation::Point ? "P" : "C";
    std::string m = "WLS";
    if (method == CAEGradientMethod::FiniteDifference) {
        m = "FD";
    } else if (method == CAEGradientMethod::AdaptiveWeightedLeastSquares) {
        m = "AWLS";
    }
    return src + "_grad_" + a + "_" + m;
}

bool CAEProcessingFacade::computeByFD(DatasetRecord& rec, const DataArray& src, std::vector<float>& outGrad) 
{
    if (rec.data.gridType != DATA_OBJECT_TYPE_RegularGrid) return false;

    GLGradientEngine::RegularParams p{};
    std::vector<float> positions;

    if (src.dataType == POINT_DATA) {
        p.dims[0] = rec.data.dimensions[0];
        p.dims[1] = rec.data.dimensions[1];
        p.dims[2] = rec.data.dimensions[2];
        positions = rec.data.points;
    } else {
        int nx = rec.data.dimensions[0] - 1;
        int ny = rec.data.dimensions[1] - 1;
        int nz = rec.data.dimensions[2] - 1;
        if (nx <= 0 || ny <= 0 || nz <= 0) return false;
        size_t nc = static_cast<size_t>(nx) * ny * nz;
        if (rec.data.cellCenters.size() != nc * 3) return false;
        if (src.data.size() % nc != 0) return false;
        p.dims[0] = nx;
        p.dims[1] = ny;
        p.dims[2] = nz;
        positions = rec.data.cellCenters;
    }

    const bool ok = m_engine.computeRegularFD(positions, src.data, p, outGrad);
    if (ok) {
        m_lastComputeGpuMs = m_engine.getLastGpuTimeMs();
    }
    return ok;
}

bool CAEProcessingFacade::computeByWLS(DatasetRecord& rec, const DataArray& src, CAEFieldAssociation assoc, float exp, float lambda, std::vector<float>& outGrad) 
{
    // For point-associated fields on 3D unstructured volume meshes, use
    // cell-local derivatives plus point-patch recovery. This follows the
    // finite-element/VTK-style reconstruction path and is much more robust
    // than raw point-neighborhood LSQ on high-order or strongly irregular cells.
    if (assoc == CAEFieldAssociation::Point &&
        rec.data.gridType == DATA_OBJECT_TYPE_UNSTRUCTURED &&
        src.dataType == POINT_DATA &&
        datasetMaxCellDimension(rec.data) >= 3) {
        if (computeVolumePointGradientByCellPatches(rec.data, rec.sourceVtk, src, outGrad)) {
            m_lastComputeGpuMs = 0.0;
            return true;
        }
    }

    if (tryComputeByLeastSquaresOperators(rec, src, assoc, outGrad)) {
        return true;
    }

    GLGradientEngine::WLSParams wp{};
    wp.wExponent = exp;
    wp.lambda = lambda;

    if (assoc == CAEFieldAssociation::Point) {
        const bool ok = m_engine.computeUnstructuredWLS(
            rec.data.points,
            rec.data.pointNeighborOffsets,
            rec.data.pointNeighbors,
            src.data,
            wp,
            outGrad);
        if (ok) {
            m_lastComputeGpuMs = m_engine.getLastGpuTimeMs();
        }
        return ok;
    }

    const bool ok = computeUnstructuredCellDataWLS(
        rec.data,
        src,
        rec.data.cellNeighborsOffsets,
        rec.data.cellNeighbors,
        exp,
        lambda,
        outGrad);
    if (ok) {
        m_lastComputeGpuMs = 0.0;
    }
    return ok;
}

bool CAEProcessingFacade::ensureLeastSquaresOperatorCache(DatasetRecord& rec)
{
    if (rec.lsqOperatorCacheBuilt) {
        return rec.lsqOperatorCacheSupported;
    }

    rec.lsqOperatorCacheBuilt = true;
    rec.lsqOperatorCacheSupported = false;
    rec.lsqPointGradOffsets.clear();
    rec.lsqPointGradSources.clear();
    rec.lsqPointGradCoeffs4.clear();
    rec.lsqPointValueOffsets.clear();
    rec.lsqPointValueSources.clear();
    rec.lsqPointValueWeights.clear();
    rec.lsqCellGradOffsets.clear();
    rec.lsqCellGradSources.clear();
    rec.lsqCellGradCoeffs4.clear();

    if (!supportsLeastSquaresOperatorPrerequisites(rec.data)) {
        return false;
    }

    if (!buildPointGradientOperator(
            rec.data,
            rec.lsqPointGradOffsets,
            rec.lsqPointGradSources,
            rec.lsqPointGradCoeffs4)) {
        return false;
    }

    if (!buildPointValueReconstructionFromCells(
            rec.data,
            rec.lsqPointValueOffsets,
            rec.lsqPointValueSources,
            rec.lsqPointValueWeights)) {
        return false;
    }

    if (!buildCellGradientOperator(
            rec.data,
            rec.lsqCellGradOffsets,
            rec.lsqCellGradSources,
            rec.lsqCellGradCoeffs4)) {
        return false;
    }

    rec.lsqOperatorCacheSupported = true;
    return true;
}

bool CAEProcessingFacade::tryComputeByLeastSquaresOperators(DatasetRecord& rec,
                                                            const DataArray& src,
                                                            CAEFieldAssociation assoc,
                                                            std::vector<float>& outGrad)
{
    if (rec.data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED) {
        return false;
    }
    if (assoc == CAEFieldAssociation::Point && src.dataType != POINT_DATA) {
        return false;
    }
    if (assoc == CAEFieldAssociation::Cell && src.dataType != CELL_DATA) {
        return false;
    }
    if (!ensureLeastSquaresOperatorCache(rec)) {
        return false;
    }

    if (assoc == CAEFieldAssociation::Point) {
        const bool ok = m_engine.applySparseGradientOperator(
            rec.lsqPointGradOffsets,
            rec.lsqPointGradSources,
            rec.lsqPointGradCoeffs4,
            static_cast<int>(rec.data.pointCount()),
            src.data,
            outGrad);
        if (ok) {
            m_lastComputeGpuMs = m_engine.getLastGpuTimeMs();
        }
        return ok;
    }

    std::vector<float> pointValues;
    double gpuMs = 0.0;

    if (!m_engine.reconstructSparseValues(
            rec.lsqPointValueOffsets,
            rec.lsqPointValueSources,
            rec.lsqPointValueWeights,
            static_cast<int>(rec.data.cellCount()),
            src.data,
            pointValues)) {
        return false;
    }
    gpuMs += m_engine.getLastGpuTimeMs();

    if (!m_engine.applySparseGradientOperator(
            rec.lsqCellGradOffsets,
            rec.lsqCellGradSources,
            rec.lsqCellGradCoeffs4,
            static_cast<int>(rec.data.pointCount()),
            pointValues,
            outGrad)) {
        return false;
    }
    gpuMs += m_engine.getLastGpuTimeMs();
    m_lastComputeGpuMs = gpuMs;
    return true;
}

bool CAEProcessingFacade::ensureAdaptiveSupport(DatasetRecord& rec,
                                                CAEFieldAssociation assoc,
                                                const CAEGradientRequest& req)
{
    AdaptiveGradientSupport& support = (assoc == CAEFieldAssociation::Point)
        ? rec.pointSupport
        : rec.cellSupport;

    const SupportBuildConfig cfg = makeSupportBuildConfig(assoc, req);

    const bool sameConfig = support.ready &&
        support.minNeighbors == cfg.minNeighbors &&
        support.targetNeighbors == cfg.targetNeighbors &&
        support.maxNeighbors == cfg.maxNeighbors &&
        std::abs(support.radiusScale - static_cast<float>(cfg.radiusScale)) <= 1e-6f &&
        std::abs(support.planeEigenRatio - static_cast<float>(cfg.planeEigenRatio)) <= 1e-6f &&
        std::abs(support.lineEigenRatio - static_cast<float>(cfg.lineEigenRatio)) <= 1e-6f &&
        support.useAdaptiveNeighborhood == cfg.useAdaptiveNeighborhood;

    if (sameConfig) {
        return true;
    }

    const std::vector<float>& positions = (assoc == CAEFieldAssociation::Point)
        ? rec.data.points
        : rec.data.cellCenters;
    const std::vector<int>& offsets = (assoc == CAEFieldAssociation::Point)
        ? rec.data.pointNeighborOffsets
        : rec.data.cellNeighborsOffsets;
    const std::vector<int>& neighbors = (assoc == CAEFieldAssociation::Point)
        ? rec.data.pointNeighbors
        : rec.data.cellNeighbors;

    AdaptiveSupportData built;
    if (!buildAdaptiveGradientSupport(positions, offsets, neighbors, cfg, built)) {
        return false;
    }

    support.ready = true;
    support.minNeighbors = cfg.minNeighbors;
    support.targetNeighbors = cfg.targetNeighbors;
    support.maxNeighbors = cfg.maxNeighbors;
    support.radiusScale = static_cast<float>(cfg.radiusScale);
    support.planeEigenRatio = static_cast<float>(cfg.planeEigenRatio);
    support.lineEigenRatio = static_cast<float>(cfg.lineEigenRatio);
    support.useAdaptiveNeighborhood = cfg.useAdaptiveNeighborhood;
    support.offsets = std::move(built.offsets);
    support.neighbors = std::move(built.neighbors);
    support.frames = std::move(built.frames);
    support.dimTags = std::move(built.dimTags);
    support.quality = std::move(built.quality);
    support.meanNeighborDistance = std::move(built.meanNeighborDistance);
    return true;
}

bool CAEProcessingFacade::computeByAdaptiveWLS(DatasetRecord& rec,
                                               const DataArray& src,
                                               const CAEGradientRequest& req,
                                               std::vector<float>& outGrad)
{
    if (req.association == CAEFieldAssociation::Point &&
        rec.data.gridType == DATA_OBJECT_TYPE_UNSTRUCTURED &&
        src.dataType == POINT_DATA &&
        datasetMaxCellDimension(rec.data) >= 3) {
        if (computeVolumePointGradientByCellPatches(rec.data, rec.sourceVtk, src, outGrad)) {
            m_lastComputeGpuMs = 0.0;
            return true;
        }
    }

    if (tryComputeByLeastSquaresOperators(rec, src, req.association, outGrad)) {
        return true;
    }

    if (rec.data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED) {
        return false;
    }
    if (!ensureAdaptiveSupport(rec, req.association, req)) {
        return false;
    }

    const AdaptiveGradientSupport& support = (req.association == CAEFieldAssociation::Point)
        ? rec.pointSupport
        : rec.cellSupport;
    const std::vector<float>& positions = (req.association == CAEFieldAssociation::Point)
        ? rec.data.points
        : rec.data.cellCenters;

    GLGradientEngine::WLSParams wp{};
    wp.wExponent = req.wlsExponent;
    wp.lambda = req.wlsLambda;
    wp.planeEigenRatio = req.planeEigenRatio;
    wp.lineEigenRatio = req.lineEigenRatio;
    wp.lambdaAmplify = req.lambdaAmplify;
    wp.enableAdaptiveDimension = req.useAdaptiveDimension ? 1 : 0;
    wp.enableAdaptiveRegularization = req.useAdaptiveRegularization ? 1 : 0;

    if (req.association == CAEFieldAssociation::Cell) {
        const bool ok = computeUnstructuredCellDataWLS(
            rec.data,
            src,
            support.offsets,
            support.neighbors,
            req.wlsExponent,
            req.wlsLambda,
            outGrad);
        if (ok) {
            m_lastComputeGpuMs = 0.0;
        }
        return ok;
    }

    const bool ok = m_engine.computeUnstructuredAdaptiveWLS(
        positions,
        support.offsets,
        support.neighbors,
        src.data,
        support.frames,
        support.dimTags,
        support.quality,
        support.meanNeighborDistance,
        wp,
        outGrad);
    if (ok) {
        m_lastComputeGpuMs = m_engine.getLastGpuTimeMs();
    }
    return ok;
}
