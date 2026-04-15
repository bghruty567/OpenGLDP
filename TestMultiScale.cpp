#include <vtkDataSetReader.h>
#include <vtkDataSet.h>
#include <vtkNew.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "CAEProcessingFacade.h"
#include "VTKDataConverter.h"

namespace
{
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

bool buildStatGraph(const DataObject& data,
                    CAEFieldAssociation assoc,
                    std::vector<int>& offsets,
                    std::vector<int>& neighbors)
{
    offsets.clear();
    neighbors.clear();

    if (data.gridType == DATA_OBJECT_TYPE_RegularGrid) {
        if (assoc == CAEFieldAssociation::Point) {
            return buildRegularNeighbors(data.dimensions[0], data.dimensions[1], data.dimensions[2], offsets, neighbors);
        }

        const int nx = data.dimensions[0] - 1;
        const int ny = data.dimensions[1] - 1;
        const int nz = data.dimensions[2] - 1;
        return buildRegularNeighbors(nx, ny, nz, offsets, neighbors);
    }

    if (assoc == CAEFieldAssociation::Point) {
        offsets = data.pointNeighborOffsets;
        neighbors = data.pointNeighbors;
    } else {
        offsets = data.cellNeighborsOffsets;
        neighbors = data.cellNeighbors;
    }

    return !offsets.empty();
}

double computeStdDev(const std::vector<float>& values)
{
    if (values.empty()) return 0.0;

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

double computeMeanAbsDelta(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty()) return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    }
    return sum / static_cast<double>(a.size());
}

double computeGraphRoughness(const std::vector<float>& values,
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
            if (j < 0 || static_cast<size_t>(j) >= tupleCount) continue;
            if (static_cast<size_t>(j) <= i) continue;

            for (int c = 0; c < comps; ++c) {
                const double vi = values[i * static_cast<size_t>(comps) + c];
                const double vj = values[static_cast<size_t>(j) * static_cast<size_t>(comps) + c];
                sum += std::abs(vi - vj);
                ++cnt;
            }
        }
    }

    return cnt > 0 ? (sum / static_cast<double>(cnt)) : 0.0;
}
}

int main(int argc, char** argv)
{
    std::string path = "Data\\SampleStructGrid.vtk";
    std::string assocArg = "point";
    std::string arrayName;
    int reps = 3;
    int levels = 3;
    int iterations = 1;
    std::string exportPath = "results\\multiscale_out.vtk";

    if (argc >= 2) path = argv[1];
    if (argc >= 3) assocArg = argv[2];
    if (argc >= 4) arrayName = argv[3];
    if (argc >= 5) reps = std::max(1, atoi(argv[4]));
    if (argc >= 6) levels = std::max(1, std::min(3, atoi(argv[5])));
    if (argc >= 7) iterations = std::max(1, atoi(argv[6]));
    if (argc >= 8) exportPath = argv[7];

    const CAEFieldAssociation assoc =
        (assocArg == "cell" || assocArg == "CELL")
            ? CAEFieldAssociation::Cell
            : CAEFieldAssociation::Point;

    CAEProcessingFacade facade;
    if (!facade.initialize("Shaders")) {
        std::cerr << "facade init failed\n";
        return 1;
    }

    const std::string dsId = facade.loadDatasetFromVTKFile(path);
    if (dsId.empty()) {
        std::cerr << "load dataset failed\n";
        return 2;
    }

    CAEDatasetSummary summary;
    if (!facade.getDatasetSummary(dsId, summary)) {
        std::cerr << "dataset summary failed\n";
        return 3;
    }

    std::cout << "Dataset=" << summary.displayName
              << " points=" << summary.pointCount
              << " cells=" << summary.cellCount << "\n";

    std::vector<CAEFieldInfo> fields;
    if (!facade.listFields(dsId, assoc, fields) || fields.empty()) {
        std::cerr << "no arrays for association\n";
        return 4;
    }

    std::cout << "Available arrays (" << (assoc == CAEFieldAssociation::Point ? "POINT" : "CELL") << "):\n";
    for (const auto& f : fields) {
        std::cout << "  - " << f.name << " comps=" << f.numComponents << " tuples=" << f.tupleCount << "\n";
    }

    if (arrayName.empty()) {
        arrayName = fields.front().name;
        std::cout << "Use default array: " << arrayName << "\n";
    }

    CAEMultiScaleRequest req;
    req.datasetId = dsId;
    req.inputArrayName = arrayName;
    req.association = assoc;
    req.levels = levels;
    req.iterationsPerLevel = iterations;
    req.spatialSigmaFactor = 1.5f;
    req.rangeSigmaFactor = 0.5f;
    req.levelScale = 1.8f;
    req.edgeSigmaFactor = 0.35f;
    req.detailGain0 = 1.0f;
    req.detailGain1 = 0.75f;
    req.detailGain2 = 0.5f;
    req.storeIntermediate = true;

    CAEMultiScaleResultMeta meta;
    double wallSum = 0.0;
    double wallMin = std::numeric_limits<double>::max();
    double gpuSum = 0.0;
    double gpuMin = std::numeric_limits<double>::max();

    for (int i = 0; i < reps; ++i) {
        if (!facade.computeMultiScaleDecompositionAndFusion(req, meta)) {
            std::cerr << "computeMultiScaleDecompositionAndFusion failed\n";
            return 5;
        }

        const double wall = meta.computeWallMs;
        const double gpu = meta.computeGpuMs;
        wallSum += wall;
        gpuSum += gpu;
        wallMin = std::min(wallMin, wall);
        gpuMin = std::min(gpuMin, gpu);
    }

    std::vector<float> srcData;
    std::vector<float> fusedData;
    int srcComps = 0;
    int fusedComps = 0;

    if (!facade.getArrayData(dsId, arrayName, assoc, srcData, srcComps)) {
        std::cerr << "get source array failed\n";
        return 6;
    }
    if (!facade.getArrayData(dsId, meta.fusedArrayName, assoc, fusedData, fusedComps)) {
        std::cerr << "get fused array failed\n";
        return 7;
    }

    vtkNew<vtkDataSetReader> reader;
    reader->SetFileName(path.c_str());
    reader->Update();
    vtkDataSet* vtkDs = vtkDataSet::SafeDownCast(reader->GetOutput());
    if (!vtkDs) {
        std::cerr << "vtk read failed\n";
        return 8;
    }

    DataObject obj;
    VTKDataConverter conv;
    conv.bindVTKDataAndInternalData(vtkDs, &obj);
    if (!conv.convertVTKToInternal()) {
        std::cerr << "convert to internal failed\n";
        return 9;
    }

    std::vector<int> offsets;
    std::vector<int> neighbors;
    if (!buildStatGraph(obj, assoc, offsets, neighbors)) {
        std::cerr << "build stat graph failed\n";
        return 10;
    }

    const double srcStd = computeStdDev(srcData);
    const double fusedStd = computeStdDev(fusedData);
    const double srcRough = computeGraphRoughness(srcData, srcComps, offsets, neighbors);
    const double fusedRough = computeGraphRoughness(fusedData, fusedComps, offsets, neighbors);
    const double meanDelta = computeMeanAbsDelta(srcData, fusedData);

    std::cout << "Array=" << arrayName
              << " Assoc=" << (assoc == CAEFieldAssociation::Point ? "POINT" : "CELL")
              << " Levels=" << levels
              << " IterationsPerLevel=" << iterations << "\n";

    std::cout << "MS_wall_ms_avg=" << (wallSum / reps)
              << " MS_wall_ms_min=" << wallMin << "\n";

    std::cout << "MS_gpu_ms_avg=" << (gpuSum / reps)
              << " MS_gpu_ms_min=" << gpuMin << "\n";

    std::cout << "InputStd=" << srcStd
              << " FusedStd=" << fusedStd
              << " MeanAbsDelta=" << meanDelta << "\n";

    std::cout << "InputRoughness=" << srcRough
              << " FusedRoughness=" << fusedRough << "\n";

    std::cout << "BaseArray=" << meta.baseArrayName << "\n";
    std::cout << "FusedArray=" << meta.fusedArrayName << "\n";

    for (size_t i = 0; i < meta.smoothArrayNames.size(); ++i) {
        std::cout << "Smooth[" << i + 1 << "]=" << meta.smoothArrayNames[i] << "\n";
    }
    for (size_t i = 0; i < meta.detailArrayNames.size(); ++i) {
        std::cout << "Detail[" << i << "]=" << meta.detailArrayNames[i] << "\n";
    }

    if (!exportPath.empty()) {
        if (facade.saveDatasetToVTKFile(dsId, exportPath, false)) {
            std::cout << "Exported=" << exportPath << "\n";
        } else {
            std::cout << "Export failed: " << exportPath << "\n";
        }
    }

    return 0;
}
