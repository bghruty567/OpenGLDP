#include "CAEProcessingFacade.h"
#include <vtkDataSetWriter.h>
#include <vtkDataSetReader.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkGradientFilter.h>
#include <vtkKdTreePointLocator.h>
#include <vtkCellData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
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

const std::vector<float>& unstructuredSamplePositions(const DataObject& data,
                                                      CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point ? data.points : data.cellCenters;
}

const std::vector<int>& unstructuredSampleOffsets(const DataObject& data,
                                                  CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point
        ? data.pointNeighborOffsets
        : data.cellNeighborsOffsets;
}

const std::vector<int>& unstructuredSampleNeighbors(const DataObject& data,
                                                    CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point
        ? data.pointNeighbors
        : data.cellNeighbors;
}

bool gradientAssociationMatchesArray(const DataArray& src, CAEFieldAssociation assoc)
{
    if (assoc == CAEFieldAssociation::Point) {
        return src.dataType == POINT_DATA;
    }
    return src.dataType == CELL_DATA;
}

bool computeUnstructuredGradientByVTKFilter(const DataObject& data,
                                            const DataArray& src,
                                            CAEFieldAssociation assoc,
                                            std::vector<float>& outGrad)
{
    if (data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED ||
        !gradientAssociationMatchesArray(src, assoc) ||
        src.numComponents <= 0) {
        return false;
    }

    VTKDataConverter conv;
    conv.bindVTKDataAndInternalData(nullptr, const_cast<DataObject*>(&data));
    if (!conv.convertInternalToVTK() || !conv.vtkData) {
        return false;
    }

    vtkDataSet* ds = conv.vtkData;
    vtkNew<vtkGradientFilter> gf;
    constexpr const char* kResultName = "__cae_tmp_grad";
    const int vtkAssoc = (assoc == CAEFieldAssociation::Point)
        ? vtkDataObject::FIELD_ASSOCIATION_POINTS
        : vtkDataObject::FIELD_ASSOCIATION_CELLS;
    gf->SetResultArrayName(kResultName);
    gf->SetInputArrayToProcess(0, 0, 0, vtkAssoc, src.name.c_str());
    gf->SetInputData(ds);
    gf->Update();

    vtkDataSet* out = vtkDataSet::SafeDownCast(gf->GetOutput());
    if (!out) {
        return false;
    }

    vtkDataArray* ga = (assoc == CAEFieldAssociation::Point)
        ? out->GetPointData()->GetArray(kResultName)
        : out->GetCellData()->GetArray(kResultName);
    if (!ga) {
        return false;
    }

    const int comps = ga->GetNumberOfComponents();
    const vtkIdType tuples = ga->GetNumberOfTuples();
    outGrad.resize(static_cast<size_t>(tuples) * static_cast<size_t>(comps));
    for (vtkIdType i = 0; i < tuples; ++i) {
        for (int c = 0; c < comps; ++c) {
            outGrad[static_cast<size_t>(i) * static_cast<size_t>(comps) + static_cast<size_t>(c)] =
                static_cast<float>(ga->GetComponent(i, c));
        }
    }
    return true;
}

bool validateNeighborGraph(const std::vector<int>& offsets,
                           const std::vector<int>& neighbors,
                           size_t sampleCount)
{
    if (offsets.size() != sampleCount + 1u) {
        return false;
    }
    if (offsets.empty() || offsets.front() != 0) {
        return false;
    }

    int prev = 0;
    for (size_t i = 0; i < offsets.size(); ++i) {
        const int off = offsets[i];
        if (off < prev || off < 0 || static_cast<size_t>(off) > neighbors.size()) {
            return false;
        }
        prev = off;
    }
    if (static_cast<size_t>(offsets.back()) != neighbors.size()) {
        return false;
    }

    for (int nb : neighbors) {
        if (nb < 0 || static_cast<size_t>(nb) >= sampleCount) {
            return false;
        }
    }
    return true;
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

bool validateAdaptiveSupportData(const AdaptiveSupportData& support, size_t sampleCount)
{
    if (!validateNeighborGraph(support.offsets, support.neighbors, sampleCount)) {
        return false;
    }
    if (support.frames.size() != sampleCount * 9u ||
        support.dimTags.size() != sampleCount ||
        support.quality.size() != sampleCount ||
        support.meanNeighborDistance.size() != sampleCount) {
        return false;
    }

    for (float v : support.frames) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    for (size_t i = 0; i < sampleCount; ++i) {
        if (support.dimTags[i] < 1u || support.dimTags[i] > 3u) {
            return false;
        }
        if (!std::isfinite(support.quality[i]) ||
            !std::isfinite(support.meanNeighborDistance[i]) ||
            support.meanNeighborDistance[i] <= 0.0f) {
            return false;
        }
    }
    return true;
}

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
    const int pointCount = static_cast<int>(positions.size() / 3u);
    if (!locator || desiredCount <= 0 || pointCount <= 1) {
        return;
    }

    double q[3] = {
        positions[static_cast<size_t>(centerId) * 3 + 0],
        positions[static_cast<size_t>(centerId) * 3 + 1],
        positions[static_cast<size_t>(centerId) * 3 + 2]
    };

    const int clampedQueryCount = std::max(1, std::min(queryCount + 1, pointCount));
    vtkNew<vtkIdList> ids;
    locator->FindClosestNPoints(clampedQueryCount, q, ids);
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
    if (n == 0 ||
        positions.size() % 3 != 0 ||
        !validateNeighborGraph(baseOffsets, baseNeighbors, n)) {
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
    std::vector<float> cubic(tupleCount, 0.0f);
    std::vector<float> cubicGrad(tupleCount * 3u, 0.0f);
    std::vector<float> gaussian(tupleCount, 0.0f);
    std::vector<float> gaussianGrad(tupleCount * 3u, 0.0f);

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

        cubic[i] = static_cast<float>(
            (0.5 * xr * xr * xr - 0.35 * yr * yr * yr + 0.2 * zr * zr * zr +
             0.15 * xr * yr * zr) / (L * L));
        cubicGrad[i * 3u + 0] = static_cast<float>((1.5 * xr * xr + 0.15 * yr * zr) / (L * L));
        cubicGrad[i * 3u + 1] = static_cast<float>((-1.05 * yr * yr + 0.15 * xr * zr) / (L * L));
        cubicGrad[i * 3u + 2] = static_cast<float>((0.6 * zr * zr + 0.15 * xr * yr) / (L * L));

        const double sigma = 0.35 * L;
        const double sigma2 = sigma * sigma;
        const double expo = -(
            xr * xr +
            0.7 * yr * yr +
            1.3 * zr * zr) / sigma2;
        const double g = std::exp(expo);
        gaussian[i] = static_cast<float>(g);
        gaussianGrad[i * 3u + 0] = static_cast<float>(g * (-2.0 * xr / sigma2));
        gaussianGrad[i * 3u + 1] = static_cast<float>(g * (-1.4 * yr / sigma2));
        gaussianGrad[i * 3u + 2] = static_cast<float>(g * (-2.6 * zr / sigma2));
    }

    data.upsertDataArray("benchmark_linear", linear, 1, type);
    data.upsertDataArray("benchmark_linear_exact_grad", linearGrad, 3, type);
    data.upsertDataArray("benchmark_quadratic", quadratic, 1, type);
    data.upsertDataArray("benchmark_quadratic_exact_grad", quadraticGrad, 3, type);
    data.upsertDataArray("benchmark_trig", trig, 1, type);
    data.upsertDataArray("benchmark_trig_exact_grad", trigGrad, 3, type);
    data.upsertDataArray("benchmark_cubic", cubic, 1, type);
    data.upsertDataArray("benchmark_cubic_exact_grad", cubicGrad, 3, type);
    data.upsertDataArray("benchmark_gaussian", gaussian, 1, type);
    data.upsertDataArray("benchmark_gaussian_exact_grad", gaussianGrad, 3, type);
}

void appendAnalyticVectorBenchmarks(DataObject& data,
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

    std::vector<float> linear(tupleCount * 3u, 0.0f);
    std::vector<float> linearGrad(tupleCount * 9u, 0.0f);
    std::vector<float> poly(tupleCount * 3u, 0.0f);
    std::vector<float> polyGrad(tupleCount * 9u, 0.0f);
    std::vector<float> trig(tupleCount * 3u, 0.0f);
    std::vector<float> trigGrad(tupleCount * 9u, 0.0f);

    for (size_t i = 0; i < tupleCount; ++i) {
        const double x = static_cast<double>(positions[i * 3u + 0]);
        const double y = static_cast<double>(positions[i * 3u + 1]);
        const double z = static_cast<double>(positions[i * 3u + 2]);
        const double xr = x - cx;
        const double yr = y - cy;
        const double zr = z - cz;
        const double sx = kPi * xr / L;
        const double sy = kPi * yr / L;
        const double sz = kPi * zr / L;

        const size_t base = i * 3u;
        const size_t gbase = i * 9u;

        linear[base + 0] = static_cast<float>(0.8 * xr - 0.3 * yr + 0.2 * zr);
        linear[base + 1] = static_cast<float>(-0.4 * xr + 1.1 * yr + 0.5 * zr);
        linear[base + 2] = static_cast<float>(0.3 * xr + 0.6 * yr - 0.9 * zr);
        linearGrad[gbase + 0] = 0.8f;  linearGrad[gbase + 1] = -0.3f; linearGrad[gbase + 2] = 0.2f;
        linearGrad[gbase + 3] = -0.4f; linearGrad[gbase + 4] = 1.1f;  linearGrad[gbase + 5] = 0.5f;
        linearGrad[gbase + 6] = 0.3f;  linearGrad[gbase + 7] = 0.6f;  linearGrad[gbase + 8] = -0.9f;

        poly[base + 0] = static_cast<float>((xr * xr + 0.25 * xr * yr - 0.15 * yr * zr) / L);
        poly[base + 1] = static_cast<float>((0.5 * yr * yr - 0.2 * xr * zr + 0.1 * xr * yr) / L);
        poly[base + 2] = static_cast<float>((0.75 * zr * zr + 0.3 * xr * zr - 0.25 * yr * zr) / L);
        polyGrad[gbase + 0] = static_cast<float>((2.0 * xr + 0.25 * yr) / L);
        polyGrad[gbase + 1] = static_cast<float>((0.25 * xr - 0.15 * zr) / L);
        polyGrad[gbase + 2] = static_cast<float>((-0.15 * yr) / L);
        polyGrad[gbase + 3] = static_cast<float>((-0.2 * zr + 0.1 * yr) / L);
        polyGrad[gbase + 4] = static_cast<float>((yr + 0.1 * xr) / L);
        polyGrad[gbase + 5] = static_cast<float>((-0.2 * xr) / L);
        polyGrad[gbase + 6] = static_cast<float>((0.3 * zr) / L);
        polyGrad[gbase + 7] = static_cast<float>((-0.25 * zr) / L);
        polyGrad[gbase + 8] = static_cast<float>((1.5 * zr + 0.3 * xr - 0.25 * yr) / L);

        trig[base + 0] = static_cast<float>(L * (std::sin(sx) + 0.2 * std::cos(sy)));
        trig[base + 1] = static_cast<float>(L * (0.5 * std::cos(sy) - 0.25 * std::sin(sz)));
        trig[base + 2] = static_cast<float>(L * (0.3 * std::sin(sz) + 0.15 * std::cos(sx)));
        trigGrad[gbase + 0] = static_cast<float>(kPi * std::cos(sx));
        trigGrad[gbase + 1] = static_cast<float>(-0.2 * kPi * std::sin(sy));
        trigGrad[gbase + 2] = 0.0f;
        trigGrad[gbase + 3] = 0.0f;
        trigGrad[gbase + 4] = static_cast<float>(-0.5 * kPi * std::sin(sy));
        trigGrad[gbase + 5] = static_cast<float>(-0.25 * kPi * std::cos(sz));
        trigGrad[gbase + 6] = static_cast<float>(-0.15 * kPi * std::sin(sx));
        trigGrad[gbase + 7] = 0.0f;
        trigGrad[gbase + 8] = static_cast<float>(0.3 * kPi * std::cos(sz));
    }

    data.upsertDataArray("benchmark_vec_linear", linear, 3, type);
    data.upsertDataArray("benchmark_vec_linear_exact_grad", linearGrad, 9, type);
    data.upsertDataArray("benchmark_vec_poly", poly, 3, type);
    data.upsertDataArray("benchmark_vec_poly_exact_grad", polyGrad, 9, type);
    data.upsertDataArray("benchmark_vec_trig", trig, 3, type);
    data.upsertDataArray("benchmark_vec_trig_exact_grad", trigGrad, 9, type);
}

void appendAnalyticBenchmarkArrays(DataObject& data)
{
    appendAnalyticScalarBenchmarks(data, data.points, POINT_DATA);
    appendAnalyticScalarBenchmarks(data, data.cellCenters, CELL_DATA);
    appendAnalyticVectorBenchmarks(data, data.points, POINT_DATA);
    appendAnalyticVectorBenchmarks(data, data.cellCenters, CELL_DATA);
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
    // Analytic benchmark arrays are intended for benchmark/reference runs only.
    // The GUI keeps the default dataset view and does not enable this path.
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
    std::string m = "AWLS";
    if (method == CAEGradientMethod::FiniteDifference) {
        m = "FD";
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
    if (!validateAdaptiveSupportData(built, positions.size() / 3u)) {
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
    if (rec.data.gridType != DATA_OBJECT_TYPE_UNSTRUCTURED ||
        !gradientAssociationMatchesArray(src, req.association)) {
        return false;
    }

    if (!ensureAdaptiveSupport(rec, req.association, req)) {
        return false;
    }

    const AdaptiveGradientSupport& support = (req.association == CAEFieldAssociation::Point)
        ? rec.pointSupport
        : rec.cellSupport;
    const std::vector<float>& positions = unstructuredSamplePositions(rec.data, req.association);

    GLGradientEngine::WLSParams wp{};
    wp.wExponent = req.wlsExponent;
    wp.lambda = req.wlsLambda;
    wp.planeEigenRatio = req.planeEigenRatio;
    wp.lineEigenRatio = req.lineEigenRatio;
    wp.lambdaAmplify = req.lambdaAmplify;
    wp.enableAdaptiveDimension = req.useAdaptiveDimension ? 1 : 0;
    wp.enableAdaptiveRegularization = req.useAdaptiveRegularization ? 1 : 0;

    if (!validateNeighborGraph(support.offsets, support.neighbors, positions.size() / 3u)) {
        return false;
    }

    bool ok = m_engine.computeUnstructuredAdaptiveWLS(
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

    // Keep a pure OpenGL fallback path for drivers that reject the adaptive
    // shader or for supports that still end up numerically awkward. The
    // fallback reuses the same entity-centered stencil and solves the global
    // 3D weighted least-squares system without frame-based dimension reduction.
    if (!ok) {
        ok = m_engine.computeUnstructuredWLS(
            positions,
            support.offsets,
            support.neighbors,
            src.data,
            wp,
            outGrad);
    }

    if (ok) {
        m_lastComputeGpuMs = m_engine.getLastGpuTimeMs();
    }
    return ok;
}
