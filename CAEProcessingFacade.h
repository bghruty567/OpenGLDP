#pragma once
#include "CAEInterfaceTypes.h"
#include "DataObject.h"
#include "GLGradientEngine.h"
#include "VTKDataConverter.h"
#include "OpenGLManager.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <vtkSmartPointer.h>
#include <vtkDataSet.h>

class CAEProcessingFacade {
public:
    CAEProcessingFacade();
    ~CAEProcessingFacade();

    bool initialize(const std::string& shaderDir);
    std::string loadDatasetFromVTKFile(const std::string& filePath);
    std::vector<CAEDatasetSummary> listDatasets() const;
    bool getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const;
    bool listFields(const std::string& datasetId, CAEFieldAssociation assoc, std::vector<CAEFieldInfo>& outFields) const;
    bool computeGradient(const CAEGradientRequest& req, CAEGradientResultMeta& outMeta);
    bool exportDatasetToVTK(const std::string& datasetId, vtkSmartPointer<vtkDataSet>& outVtk) const;
    bool getArrayData(const std::string& datasetId, const std::string& arrayName, CAEFieldAssociation assoc, std::vector<float>& outData, int& outComps) const;
    double getLastComputeWallMs() const;
    double getLastComputeGpuMs() const;

private:
    struct DatasetRecord {
        std::string id;
        std::string displayName;
        DataObject data;
        vtkSmartPointer<vtkDataSet> sourceVtk;
        std::vector<CAEGradientResultMeta> results;
    };

    OpenGLManager m_gl;
    GLGradientEngine m_engine;
    bool m_initialized = false;
    std::unordered_map<std::string, std::unique_ptr<DatasetRecord>> m_records;
    std::uint64_t m_nextId = 1;
    double m_lastComputeWallMs = 0.0;
    double m_lastComputeGpuMs = 0.0;
    //double m_lastComputeWallMs = 0.0;
    //double m_lastComputeGpuMs = 0.0;


    static CAEGridClass toGridClass(GridType t);
    static DataArrayType toDataArrayType(CAEFieldAssociation a);
    static CAEFieldAssociation toAssociation(DataArrayType t);

    static std::string fileNameFromPath(const std::string& path);
    static std::string makeResultName(const std::string& src, CAEFieldAssociation assoc, CAEGradientMethod method);

    bool computeByFD(DatasetRecord& rec, const DataArray& src, std::vector<float>& outGrad) ;
    bool computeByWLS(DatasetRecord& rec, const DataArray& src, CAEFieldAssociation assoc, float exp, float lambda, std::vector<float>& outGrad) ;
};
