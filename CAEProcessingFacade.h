#pragma once

#include "CAEInterfaceTypes.h"
#include "DataObject.h"
#include "GLGradientEngine.h"
#include "VTKDataConverter.h"
#include "OpenGLManager.h"
#include "GLFilterEngine.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

class CAEProcessingFacade {
public:
    CAEProcessingFacade();
    ~CAEProcessingFacade();

    bool initialize(const std::string& shaderDir);

    void setAnalyticBenchmarkEnabled(bool enabled);
    bool isAnalyticBenchmarkEnabled() const;

    std::string loadDatasetFromVTKFile(const std::string& filePath);

    std::vector<CAEDatasetSummary> listDatasets() const;
    bool getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const;

    bool listFields(const std::string& datasetId,
                    CAEFieldAssociation assoc,
                    std::vector<CAEFieldInfo>& outFields) const;

    bool computeGradient(const CAEGradientRequest& req, CAEGradientResultMeta& outMeta);

    bool computeMultiScaleDecompositionAndFusion(const CAEMultiScaleRequest& req,
                                                 CAEMultiScaleResultMeta& outMeta);

    bool exportDatasetToVTK(const std::string& datasetId,
                            vtkSmartPointer<vtkDataSet>& outVtk) const;

    bool saveDatasetToVTKFile(const std::string& datasetId,
                              const std::string& filePath,
                              bool binary = true) const;

    bool getArrayData(const std::string& datasetId,
                      const std::string& arrayName,
                      CAEFieldAssociation assoc,
                      std::vector<float>& outData,
                      int& outComps) const;

    bool upsertArrayData(const std::string& datasetId,
                         const std::string& arrayName,
                         CAEFieldAssociation assoc,
                         const std::vector<float>& data,
                         int numComponents);

    double getLastComputeWallMs() const;
    double getLastComputeGpuMs() const;

private:
    struct AdaptiveGradientSupport {
        bool ready = false;
        int minNeighbors = 0;
        int targetNeighbors = 0;
        int maxNeighbors = 0;
        float radiusScale = 0.0f;
        float planeEigenRatio = 0.0f;
        float lineEigenRatio = 0.0f;
        bool useAdaptiveNeighborhood = true;
        std::vector<int> offsets;
        std::vector<int> neighbors;
        std::vector<float> frames;
        std::vector<std::uint32_t> dimTags;
        std::vector<float> quality;
        std::vector<float> meanNeighborDistance;
    };

    struct DatasetRecord {
        std::string id;
        std::string displayName;
        DataObject data;
        vtkSmartPointer<vtkDataSet> sourceVtk;
        std::vector<CAEGradientResultMeta> results;
        AdaptiveGradientSupport pointSupport;
        AdaptiveGradientSupport cellSupport;
    };

    OpenGLManager m_gl;
    GLGradientEngine m_engine;
    GLFilterEngine m_filter;

    bool m_initialized = false;
    bool m_appendAnalyticBenchmarkArrays = false;

    std::unordered_map<std::string, std::unique_ptr<DatasetRecord>> m_records;
    std::uint64_t m_nextId = 1;

    double m_lastComputeWallMs = 0.0;
    double m_lastComputeGpuMs = 0.0;

    static CAEGridClass toGridClass(GridType t);
    static DataArrayType toDataArrayType(CAEFieldAssociation a);
    static CAEFieldAssociation toAssociation(DataArrayType t);
    static std::string fileNameFromPath(const std::string& path);
    static std::string makeResultName(const std::string& src,
                                      CAEFieldAssociation assoc,
                                      CAEGradientMethod method);

    bool computeByFD(DatasetRecord& rec,
                     const DataArray& src,
                     std::vector<float>& outGrad);

    bool computeByShapeFunction(DatasetRecord& rec,
                                const DataArray& src,
                                const CAEGradientRequest& req,
                                std::vector<float>& outGrad);

    bool computeByAdaptiveWLS(DatasetRecord& rec,
                              const DataArray& src,
                              const CAEGradientRequest& req,
                              std::vector<float>& outGrad);

    bool ensureAdaptiveSupport(DatasetRecord& rec,
                               CAEFieldAssociation assoc,
                               const CAEGradientRequest& req);
};
