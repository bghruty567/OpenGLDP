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

struct CAEDatasetSummary {
    std::string datasetId;
    std::string displayName;
    CAEGridClass gridClass = CAEGridClass::Unstructured;
    std::size_t pointCount = 0;
    std::size_t cellCount = 0;
    std::vector<CAEFieldInfo> fields;
    std::vector<CAEGradientResultMeta> results;
}; 
