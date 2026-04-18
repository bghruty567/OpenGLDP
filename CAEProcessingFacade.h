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

/// CAEProcessingFacade 是系统对外暴露的统一入口。
///
/// 从上层视角看，这个类承担了 4 件事：
/// 1. 读取 VTK 数据并转换为统一内部数据结构；
/// 2. 管理数据集、字段和结果数组；
/// 3. 调用 GPU 端梯度计算与多尺度优化模块；
/// 4. 将内部数据重新导出为 VTK，便于外部验证与可视化。
///
/// 这样做的好处是：GUI、测试程序、后续论文实验都复用同一条主流程，
/// 能避免“界面一套逻辑、测试一套逻辑”导致结果不一致。
class CAEProcessingFacade {
public:
    CAEProcessingFacade();
    ~CAEProcessingFacade();

    /// 初始化 OpenGL 计算上下文，并加载梯度计算/滤波所需的着色器。
    /// shaderDir 一般指向 Shaders 目录。
    bool initialize(const std::string& shaderDir);

    /// 控制加载数据集时是否额外注入解析 benchmark 字段。
    /// 这个开关主要给测试程序用，GUI 日常使用通常关闭。
    void setAnalyticBenchmarkEnabled(bool enabled);
    bool isAnalyticBenchmarkEnabled() const;

    /// 读取一个 VTK 数据集，转换为内部 DataObject，并返回系统内部的 datasetId。
    /// 后续所有查询、计算、导出都通过这个 id 进行。
    std::string loadDatasetFromVTKFile(const std::string& filePath);

    /// 枚举当前 facade 中已经加载的所有数据集摘要信息。
    std::vector<CAEDatasetSummary> listDatasets() const;

    /// 查询指定数据集的摘要信息，例如点数、单元数、字段列表等。
    bool getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const;

    /// 枚举指定点/单元关联下的字段列表。
    bool listFields(const std::string& datasetId,
                    CAEFieldAssociation assoc,
                    std::vector<CAEFieldInfo>& outFields) const;

    /// 计算某个字段的梯度。
    ///
    /// 当前主线策略是：
    /// - 规则网格：有限差分 FD；
    /// - 非结构网格：自适应加权最小二乘 AWLS。
    ///
    /// computeGradient 负责做方法分派、结果数组命名、计时和结果写回。
    bool computeGradient(const CAEGradientRequest& req, CAEGradientResultMeta& outMeta);

    /// 对某个字段执行多尺度分解与融合。
    ///
    /// 典型流程为：
    /// 原始场 -> 多层双边滤波平滑 -> 相邻层做差得到细节 -> 按权重融合重建。
    bool computeMultiScaleDecompositionAndFusion(const CAEMultiScaleRequest& req,
                                                 CAEMultiScaleResultMeta& outMeta);

    /// 将内部数据重新转换为 vtkDataSet。
    bool exportDatasetToVTK(const std::string& datasetId,
                            vtkSmartPointer<vtkDataSet>& outVtk) const;

    /// 将内部数据直接保存为 VTK 文件。
    /// binary=true 时输出二进制 legacy VTK，便于 ParaView 和旧版 reader 兼容。
    bool saveDatasetToVTKFile(const std::string& datasetId,
                              const std::string& filePath,
                              bool binary = true) const;

    /// 读取某个字段的原始数据，供测试程序做误差分析或 CSV 统计。
    bool getArrayData(const std::string& datasetId,
                      const std::string& arrayName,
                      CAEFieldAssociation assoc,
                      std::vector<float>& outData,
                      int& outComps) const;

    /// 更新或插入一个字段数组。
    ///
    /// 该接口主要给测试程序使用，例如：
    /// - 注入解析 benchmark；
    /// - 写入加噪数据；
    /// - 构造干净参考场。
    bool upsertArrayData(const std::string& datasetId,
                         const std::string& arrayName,
                         CAEFieldAssociation assoc,
                         const std::vector<float>& data,
                         int numComponents);

    /// 返回最近一次 GPU/CPU 计算的墙钟时间。
    double getLastComputeWallMs() const;

    /// 返回最近一次 GPU 计算时间查询结果。
    double getLastComputeGpuMs() const;

private:
    /// 非结构网格梯度计算所需的“自适应支撑信息”。
    ///
    /// 这些量不是输入数据本身，而是为了让 AWLS 更稳定而预计算出来的辅助信息：
    /// - offsets/neighbors：最终选定的邻域图；
    /// - frames：局部主方向框架；
    /// - dimTags：局部维度标签，区分近 1D/2D/3D；
    /// - quality：局部邻域质量估计；
    /// - meanNeighborDistance：局部平均邻距。
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

    /// facade 内部维护的数据集记录。
    /// 除了原始数据和显示名外，还会缓存点/单元两套 AWLS 支撑数据以及历史结果。
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

    /// 内部枚举转换辅助函数。
    static CAEGridClass toGridClass(GridType t);
    static DataArrayType toDataArrayType(CAEFieldAssociation a);
    static CAEFieldAssociation toAssociation(DataArrayType t);

    /// 从文件路径中提取显示名。
    static std::string fileNameFromPath(const std::string& path);

    /// 统一生成结果数组名，便于 GUI、测试程序和导出保持一致。
    /// 例如：velocity_grad_P_FD 或 stress_grad_C_AWLS
    static std::string makeResultName(const std::string& src,
                                      CAEFieldAssociation assoc,
                                      CAEGradientMethod method);

    /// 规则网格梯度计算实现。
    bool computeByFD(DatasetRecord& rec,
                     const DataArray& src,
                     std::vector<float>& outGrad);

    /// 非结构网格自适应加权最小二乘梯度计算实现。
    bool computeByAdaptiveWLS(DatasetRecord& rec,
                              const DataArray& src,
                              const CAEGradientRequest& req,
                              std::vector<float>& outGrad);

    /// 为指定点/单元关联构建或复用 AWLS 支撑信息。
    bool ensureAdaptiveSupport(DatasetRecord& rec,
                               CAEFieldAssociation assoc,
                               const CAEGradientRequest& req);
};
