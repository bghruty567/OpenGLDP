#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    AdaptiveWeightedLeastSquares = 2,
    ShapeFunctionDerivatives = 3
};

struct CAEFieldInfo {
    std::string name;
    CAEFieldAssociation association = CAEFieldAssociation::Point;
    int numComponents = 1;
    std::size_t tupleCount = 0;
};

struct CAEGradientRequest {
    std::string datasetId;
    std::string inputArrayName;
    CAEFieldAssociation association = CAEFieldAssociation::Point;
    CAEGradientMethod method = CAEGradientMethod::Auto;

    float wlsExponent = 1.0f;
    float wlsLambda = 1e-3f;
    bool useAdaptiveNeighborhood = true;
    bool useAdaptiveDimension = true;
    bool useAdaptiveRegularization = true;
    int minNeighbors = 12;
    int targetNeighbors = 20;
    int maxNeighbors = 32;
    float radiusScale = 2.5f;
    float planeEigenRatio = 0.06f;
    float lineEigenRatio = 0.02f;
    float lambdaAmplify = 4.0f;
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

    int levels = 3;
    int iterationsPerLevel = 1;

    float spatialSigmaFactor = 1.5f;
    float rangeSigmaFactor = 0.5f;
    float levelScale = 1.8f;

    float edgeSigmaFactor = 0.35f;
    float detailGain0 = 1.0f;
    float detailGain1 = 0.75f;
    float detailGain2 = 0.5f;
    bool storeIntermediate = true;
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
