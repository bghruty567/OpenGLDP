#include "CAEProcessingFacade.h"
#include <vtkDataSetWriter.h>
#include <vtkDataSetReader.h>
#include <vtkNew.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
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
        }
        else {
            method = CAEGradientMethod::WeightedLeastSquares;
        }
    }

    const std::string sourceName = src->name;
    const int inputComponents = src->numComponents;

    std::vector<float> grad;
    bool ok = false;

    // The GUI owns a separate VTK/Qt render context. Make the facade's
    // compute context current before issuing any GL commands.
    m_gl.makeCurrent();

    auto t0 = std::chrono::high_resolution_clock::now();
    if (method == CAEGradientMethod::FiniteDifference) {
        ok = computeByFD(rec, *src, grad);
    } else {
        ok = computeByWLS(rec, *src, req.association,
                          req.wlsExponent, req.wlsLambda, grad);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    if (!ok) {
        return false;
    }

    m_lastComputeWallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    m_lastComputeGpuMs = m_engine.getLastGpuTimeMs();

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
    std::string m = method == CAEGradientMethod::FiniteDifference ? "FD" : "WLS";
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

    return m_engine.computeRegularFD(positions, src.data, p, outGrad);
}

bool CAEProcessingFacade::computeByWLS(DatasetRecord& rec, const DataArray& src, CAEFieldAssociation assoc, float exp, float lambda, std::vector<float>& outGrad) 
{
    GLGradientEngine::WLSParams wp{};
    wp.wExponent = exp;
    wp.lambda = lambda;

    if (assoc == CAEFieldAssociation::Point) {
        return m_engine.computeUnstructuredWLS(rec.data.points, rec.data.pointNeighborOffsets, rec.data.pointNeighbors, src.data, wp, outGrad);
    }

    return m_engine.computeUnstructuredWLS(rec.data.cellCenters, rec.data.cellNeighborsOffsets, rec.data.cellNeighbors, src.data, wp, outGrad);
}
