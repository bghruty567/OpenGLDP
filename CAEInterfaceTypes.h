#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// 字段归属类型。
///
/// 与 `DataArrayType` 不同，这个枚举主要服务于门面层和测试程序接口，
/// 用来描述“请求的是点字段还是单元字段”。
enum class CAEFieldAssociation {
    Point = 0,
    Cell = 1
};

/// 网格的大类。
///
/// 当前系统对外只区分两类：
/// - `Regular`：规则网格，梯度主线走 FD；
/// - `Unstructured`：非结构网格，梯度主线走 AWLS。
enum class CAEGridClass {
    Regular = 0,
    Unstructured = 1
};

/// 梯度计算方法。
enum class CAEGradientMethod {
    Auto = 0,                         ///< 自动按网格类型选择方法
    FiniteDifference = 1,             ///< 规则网格有限差分
    AdaptiveWeightedLeastSquares = 2  ///< 非结构网格自适应加权最小二乘
};

/// 字段摘要信息。
///
/// 主要用于 GUI、测试程序列出字段列表时展示。
struct CAEFieldInfo {
    std::string name;                                   ///< 字段名称
    CAEFieldAssociation association = CAEFieldAssociation::Point; ///< 点字段或单元字段
    int numComponents = 1;                              ///< 每个 tuple 的分量数
    std::size_t tupleCount = 0;                         ///< tuple 总数
};

/// 梯度计算请求。
///
/// 这个结构把一次梯度实验或 GUI 操作需要的输入全部打包在一起。
struct CAEGradientRequest {
    std::string datasetId;              ///< 目标数据集 id
    std::string inputArrayName;         ///< 待计算梯度的输入字段名
    CAEFieldAssociation association = CAEFieldAssociation::Point; ///< 字段归属类型
    CAEGradientMethod method = CAEGradientMethod::Auto;           ///< 指定方法或自动选择

    float wlsExponent = 1.0f;           ///< WLS 距离权重指数
    float wlsLambda = 1e-3f;            ///< 基础正则项
    bool useAdaptiveNeighborhood = true;///< 是否启用自适应邻域
    bool useAdaptiveDimension = true;   ///< 是否启用局部维度自适应
    bool useAdaptiveRegularization = true; ///< 是否启用自适应正则
    int minNeighbors = 12;              ///< 邻域最小样本数
    int targetNeighbors = 20;           ///< 期望邻域样本数
    int maxNeighbors = 32;              ///< 邻域最大样本数
    float radiusScale = 2.5f;           ///< 自适应半径缩放系数
    float planeEigenRatio = 0.06f;      ///< 近二维判定阈值
    float lineEigenRatio = 0.02f;       ///< 近一维判定阈值
    float lambdaAmplify = 4.0f;         ///< 低质量邻域时的正则放大量
};

/// 梯度结果元数据。
///
/// 算法结果本身保存在数据集字段数组里，这个结构只记录“这次计算产生了什么”。
struct CAEGradientResultMeta {
    std::string resultArrayName;        ///< 结果字段名
    std::string sourceArrayName;        ///< 输入字段名
    CAEFieldAssociation association = CAEFieldAssociation::Point; ///< 点/单元归属
    CAEGradientMethod method = CAEGradientMethod::Auto;           ///< 实际使用的方法
    int inputComponents = 1;            ///< 输入字段分量数
    int outputComponents = 3;           ///< 输出字段分量数，通常是 `3 * inputComponents`
    double computeWallMs = 0.0;         ///< 墙钟时间，包含 CPU 侧调度开销
    double computeGpuMs = 0.0;          ///< GPU 核心计算时间
};

/// 多尺度优化请求。
struct CAEMultiScaleRequest {
    std::string datasetId;              ///< 目标数据集 id
    std::string inputArrayName;         ///< 待优化字段名
    CAEFieldAssociation association = CAEFieldAssociation::Point; ///< 点/单元归属

    int levels = 3;                     ///< 分解层数，当前建议 1~3
    int iterationsPerLevel = 1;         ///< 每层双边滤波迭代次数

    float spatialSigmaFactor = 1.5f;    ///< 相对平均邻边长度的空间尺度因子
    float rangeSigmaFactor = 0.5f;      ///< 相对场标准差的数值尺度因子
    float levelScale = 1.8f;            ///< 每层空间 sigma 的放大倍数

    float edgeSigmaFactor = 0.35f;      ///< 融合时细节抑制阈值因子
    float detailGain0 = 1.0f;           ///< 第 0 层细节增益
    float detailGain1 = 0.75f;          ///< 第 1 层细节增益
    float detailGain2 = 0.5f;           ///< 第 2 层细节增益

    bool storeIntermediate = true;      ///< 是否将 smooth/detail/base 等中间层写回数据集
};

/// 多尺度优化结果元数据。
struct CAEMultiScaleResultMeta {
    std::string sourceArrayName;        ///< 输入字段名
    CAEFieldAssociation association = CAEFieldAssociation::Point; ///< 点/单元归属
    int numLevels = 0;                  ///< 实际使用的层数
    int inputComponents = 1;            ///< 输入字段分量数

    std::vector<std::string> smoothArrayNames; ///< 每层平滑结果数组名
    std::vector<std::string> detailArrayNames; ///< 每层细节结果数组名
    std::string baseArrayName;          ///< 最深平滑层或 base 层数组名
    std::string fusedArrayName;         ///< 融合重建结果数组名

    double computeWallMs = 0.0;         ///< 墙钟时间
    double computeGpuMs = 0.0;          ///< GPU 计算时间
};

/// 数据集摘要信息。
///
/// 主要用于列出当前 facade 中可用数据集，以及供测试程序输出数据集基本信息。
struct CAEDatasetSummary {
    std::string datasetId;              ///< 数据集 id
    std::string displayName;            ///< 展示名称，通常来自文件名
    CAEGridClass gridClass = CAEGridClass::Unstructured; ///< 规则/非结构网格
    std::size_t pointCount = 0;         ///< 点数量
    std::size_t cellCount = 0;          ///< 单元数量
    std::vector<CAEFieldInfo> fields;   ///< 当前可用字段列表
    std::vector<CAEGradientResultMeta> results; ///< 已产生的梯度结果记录
};
