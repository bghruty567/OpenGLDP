#include "CAEProcessingFacade.h"
#include <vtkDataSetReader.h>
#include <algorithm>
#include <chrono>

CAEProcessingFacade::CAEProcessingFacade() {}
CAEProcessingFacade::~CAEProcessingFacade() {}

bool CAEProcessingFacade::initialize(const std::string& shaderDir)
{
    //初始化计算引擎和OpenGL上下文
    if (!m_gl.initialize(false)) return false;
    m_engine.setShaderDir(shaderDir);
    m_initialized = m_engine.init();
    if (m_initialized) {
        m_engine.setEnableGpuTiming(true);
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
