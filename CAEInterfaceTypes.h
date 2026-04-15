#pragma once
#include <string>
#include <vector>
#include <cstdint>


enum class CAEFieldAssociation {
    Point = 0,
    Cell = 1
};

enum class CAEGridClass {
    Regular = 0,
    Unstructured = 1
};

enum class CAEGradientMethod {
    Auto = 0,
    FiniteDifference = 1,
    WeightedLeastSquares = 2
};


struct CAEFieldInfo {
    std::string name;
    CAEFieldAssociation association = CAEFieldAssociation::Point;
    int numComponents = 1;
    std::size_t tupleCount = 0;
};

struct CAEGradientRequest {
	std::string datasetId;///数据集ID
	std::string inputArrayName;//输入数组名称
    CAEFieldAssociation association = CAEFieldAssociation::Point;
    CAEGradientMethod method = CAEGradientMethod::Auto;
    float wlsExponent = 1.0f;
    float wlsLambda = 1e-3f;
};

struct CAEGradientResultMeta {
    std::string resultArrayName;
    std::string sourceArrayName;
    CAEFieldAssociation association = CAEFieldAssociation::Point;
    CAEGradientMethod method = CAEGradientMethod::Auto;
    int inputComponents = 1;
    int outputComponents = 3;
    double computeWallMs = 0.0;
    double computeGpuMs = 0.0;
};


struct CAEMultiScaleRequest {
    std::string datasetId;
    std::string inputArrayName;
    CAEFieldAssociation association = CAEFieldAssociation::Point;

    int levels = 3;                 // 首版建议 1~3
    int iterationsPerLevel = 1;

    float spatialSigmaFactor = 1.5f; // 相对平均邻边长度
    float rangeSigmaFactor = 0.5f;   // 相对场标准差
    float levelScale = 1.8f;         // 每层空间sigma放大倍数

    float edgeSigmaFactor = 0.35f;   // 融合时的细节抑制阈值
    float detailGain0 = 1.0f;
    float detailGain1 = 0.75f;
    float detailGain2 = 0.5f;

    bool storeIntermediate = true;   // 是否把 s1/s2/s3 和 d0/d1/d2 存回 DataObject
};

struct CAEMultiScaleResultMeta {
    std::string sourceArrayName;
    CAEFieldAssociation association = CAEFieldAssociation::Point;
    int numLevels = 0;
    int inputComponents = 1;

    std::vector<std::string> smoothArrayNames;
    std::vector<std::string> detailArrayNames;
    std::string baseArrayName;
    std::string fusedArrayName;

    double computeWallMs = 0.0;
    double computeGpuMs = 0.0;
};

struct CAEDatasetSummary {
    std::string datasetId;
    std::string displayName;
    CAEGridClass gridClass = CAEGridClass::Unstructured;
    std::size_t pointCount = 0;
    std::size_t cellCount = 0;
    std::vector<CAEFieldInfo> fields;
    std::vector<CAEGradientResultMeta> results;
}; 
