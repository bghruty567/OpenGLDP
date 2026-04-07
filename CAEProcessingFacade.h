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

/*
* @brief CAEProcessingFacade类提供了一个统一的接口，用于加载VTK数据集、查询数据集信息、计算梯度以及导出结果等功能
*/
class CAEProcessingFacade {
public:
    CAEProcessingFacade();
    ~CAEProcessingFacade();

    /*
	* @brief 初始化CAEProcessingFacade，设置着色器目录并初始化GLGradientEngine
    */
    bool initialize(const std::string& shaderDir);
    /*
	* @brief 从VTK文件加载数据集，返回一个唯一的字符串ID用于后续操作
    */
    std::string loadDatasetFromVTKFile(const std::string& filePath);
    /*
	* @brief 列出所有加载的数据集的摘要信息，包括数据集ID、显示名称、网格类型、点数、单元数和字段信息等
    */
    std::vector<CAEDatasetSummary> listDatasets() const;
    /*
	* @brief 获取指定数据集的摘要信息
    */
    bool getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const;
    /*
	* @brief 列出指定数据集和字段关联类型（点数据或单元数据）的所有字段信息，包括字段名称、组件数和元组数等
    */
    bool listFields(const std::string& datasetId, CAEFieldAssociation assoc, std::vector<CAEFieldInfo>& outFields) const;

    /*
	* @brief 计算指定数据集和字段的梯度，支持有限差分和加权最小二乘两种方法，并返回计算结果的元信息，包括结果数组名称、计算时间等
    */
    bool computeGradient(const CAEGradientRequest& req, CAEGradientResultMeta& outMeta);
    /*
	* @brief 将内部数据集转换为VTK数据集，供外部使用或保存到文件等操作
    */
    bool exportDatasetToVTK(const std::string& datasetId, vtkSmartPointer<vtkDataSet>& outVtk) const;
    /*
	* @brief 获取指定数据集和字段的原始数据数组，用于外部分析或验证等目的
    */
    bool getArrayData(const std::string& datasetId, const std::string& arrayName, CAEFieldAssociation assoc, std::vector<float>& outData, int& outComps) const;
    /*
	* @brief 获取上一次梯度计算的CPU时间和GPU时间，单位为毫秒，用于性能分析和比较不同方法的效率等目的
    */
    double getLastComputeWallMs() const;
    /*
	* @brief 获取上一次梯度计算的GPU时间，单位为毫秒，用于性能分析和比较不同方法的效率等目的
    */
    double getLastComputeGpuMs() const;

private:
    
	struct DatasetRecord {//内部数据集记录结构，包含数据集ID、显示名称、数据对象、原始VTK数据集和计算结果等信息
        std::string id;
        std::string displayName;
        DataObject data;
        vtkSmartPointer<vtkDataSet> sourceVtk;
        std::vector<CAEGradientResultMeta> results;
    };

	OpenGLManager m_gl;
    GLGradientEngine m_engine;
	bool m_initialized = false;//是否成功初始化
    std::unordered_map<std::string, std::unique_ptr<DatasetRecord>> m_records;
    std::uint64_t m_nextId = 1;
    double m_lastComputeWallMs = 0.0;
    double m_lastComputeGpuMs = 0.0;
    //double m_lastComputeWallMs = 0.0;
    //double m_lastComputeGpuMs = 0.0;

    /*
	* @brief 将内部定义的GridType转换为CAEGridClass枚举类型，用于描述数据集的网格类型
    */
    static CAEGridClass toGridClass(GridType t);
    /*
	* @brief 将CAEFieldAssociation枚举类型转换为内部定义的DataArrayType枚举类型，用于描述数据数组的关联类型（点数据或单元数据）
    */
    static DataArrayType toDataArrayType(CAEFieldAssociation a);
    /*
	* @brief 将内部定义的DataArrayType枚举类型转换为CAEFieldAssociation枚举类型，用于描述数据数组的关联类型（点数据或单元数据）
    */
    static CAEFieldAssociation toAssociation(DataArrayType t);
    /*
	* @brief 从文件路径中提取文件名，用于显示名称等用途
    */
    static std::string fileNameFromPath(const std::string& path);
    /*
	* @brief 根据源字段名称、关联类型和梯度计算方法生成结果数组的名称，格式为"源字段_grad_关联类型_方法"，例如"velocity_grad_P_FD"表示点数据的速度场使用有限差分方法计算的梯度结果
    */
    static std::string makeResultName(const std::string& src, CAEFieldAssociation assoc, CAEGradientMethod method);

    bool computeByFD(DatasetRecord& rec, const DataArray& src, std::vector<float>& outGrad) ;
    bool computeByWLS(DatasetRecord& rec, const DataArray& src, CAEFieldAssociation assoc, float exp, float lambda, std::vector<float>& outGrad) ;
};
