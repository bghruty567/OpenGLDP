#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkGradientFilter.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "CAEProcessingFacade.h"
#include "TestHarnessUtils.h"

namespace
{
using namespace testharness;

enum class ReferenceMode
{
    Auto,
    Analytic,
    Vtk,
    None
};

enum class RunMode
{
    Single,
    Benchmarks,
    Fields
};

struct Options
{
    std::string path = "Data\\ShipHull_0.vtk";
    CAEFieldAssociation assoc = CAEFieldAssociation::Point;
    std::string arrayName;
    int reps = 5;
    bool enableAnalyticBenchmarks = true;
    ReferenceMode referenceMode = ReferenceMode::Auto;
    int maxSamplesToPrint = 12;
    RunMode runMode = RunMode::Fields;
    bool listFields = false;
    bool listBenchmarks = false;
    bool showConfig = false;
    std::string nameFilter;
    std::string csvPath = "results\\gradient_report.csv";

    CAEGradientMethod method = CAEGradientMethod::Auto;
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

struct ReferenceData
{
    bool available = false;
    bool analytic = false;
    std::string label;
    std::vector<float> values;
    int comps = 0;
    double timeAvgMs = 0.0;
    double timeMinMs = 0.0;
};

struct CompareMetrics
{
    bool haveReference = false;
    std::string referenceLabel;
    size_t tupleCount = 0;
    int compareComponents = 0;
    int vecsPerTuple = 0;
    size_t rawVecCount = 0;
    size_t finiteVecCount = 0;
    size_t nonFiniteVecCount = 0;
    size_t lowRefCount = 0;
    double lowRefRatio = 0.0;

    double vecMaeAbs = 0.0;
    double vecRmseAbs = 0.0;
    double vecMaxAbs = 0.0;
    double refScaleRms = 0.0;
    double nmaeVec = 0.0;
    double nrmseVec = 0.0;
    double softRelTau = 0.0;
    double softRelMean = 0.0;
    double softRelMedian = 0.0;
    double softRelP90 = 0.0;

    size_t stableVecCount = 0;
    double stableMaeAbs = 0.0;
    double stableRmseAbs = 0.0;
    double stableSoftRelMean = 0.0;
    double stableSoftRelMedian = 0.0;
    double stableSoftRelP90 = 0.0;

    double angleMeanDeg = 0.0;
    double angleP90Deg = 0.0;
    size_t angleCount = 0;
    double scaleBias = 0.0;

    size_t worstTuple = std::numeric_limits<size_t>::max();
    int worstVector = -1;
    double worstErrAbs = 0.0;
    double worstRefNorm = 0.0;

    double refTimeAvgMs = 0.0;
    double refTimeMinMs = 0.0;
};

struct GeometrySummary
{
    bool available = false;
    int topoDim = 3;
    int globalGeomDim = 3;
    bool surfaceLike = false;
    std::array<double, 3> extents{ 0.0, 0.0, 0.0 };
    std::array<double, 3> eigenRatios{ 1.0, 1.0, 1.0 };
    size_t dim1Count = 0;
    size_t dim2Count = 0;
    size_t dim3Count = 0;
};

struct CaseRecord
{
    std::string dataset;
    std::string association;
    std::string arrayName;
    int inputComponents = 0;
    bool success = false;
    std::string failureReason;
    double glWallAvgMs = 0.0;
    double glWallMinMs = 0.0;
    double glGpuAvgMs = 0.0;
    double glGpuMinMs = 0.0;
    size_t resultTuples = 0;
    int resultComponents = 0;
    CompareMetrics ambientMetrics;
    CompareMetrics intrinsicMetrics;
    bool hasIntrinsicMetrics = false;
};

const char* assocName(CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point ? "POINT" : "CELL";
}

const char* referenceModeName(ReferenceMode mode)
{
    switch (mode) {
    case ReferenceMode::Analytic: return "analytic";
    case ReferenceMode::Vtk: return "vtk";
    case ReferenceMode::None: return "none";
    default: return "auto";
    }
}

const char* runModeName(RunMode mode)
{
    switch (mode) {
    case RunMode::Benchmarks: return "benchmarks";
    case RunMode::Fields: return "fields";
    default: return "single";
    }
}

const char* methodName(CAEGradientMethod method)
{
    switch (method) {
    case CAEGradientMethod::FiniteDifference: return "fd";
    case CAEGradientMethod::AdaptiveWeightedLeastSquares: return "awls";
    default: return "auto";
    }
}

bool parseAssociation(std::string value, CAEFieldAssociation& out)
{
    value = toLower(std::move(value));
    if (value == "point" || value == "p") {
        out = CAEFieldAssociation::Point;
        return true;
    }
    if (value == "cell" || value == "c") {
        out = CAEFieldAssociation::Cell;
        return true;
    }
    return false;
}

bool parseReferenceMode(std::string value, ReferenceMode& out)
{
    value = toLower(std::move(value));
    if (value == "auto") {
        out = ReferenceMode::Auto;
        return true;
    }
    if (value == "analytic" || value == "exact") {
        out = ReferenceMode::Analytic;
        return true;
    }
    if (value == "vtk") {
        out = ReferenceMode::Vtk;
        return true;
    }
    if (value == "none" || value == "off" || value == "skip") {
        out = ReferenceMode::None;
        return true;
    }
    return false;
}

bool parseRunMode(std::string value, RunMode& out)
{
    value = toLower(std::move(value));
    if (value == "single" || value == "case") {
        out = RunMode::Single;
        return true;
    }
    if (value == "benchmarks" || value == "benchmark" || value == "suite") {
        out = RunMode::Benchmarks;
        return true;
    }
    if (value == "fields" || value == "all-fields") {
        out = RunMode::Fields;
        return true;
    }
    return false;
}

bool parseMethod(std::string value, CAEGradientMethod& out)
{
    value = toLower(std::move(value));
    if (value == "auto") {
        out = CAEGradientMethod::Auto;
        return true;
    }
    if (value == "fd" || value == "finite-difference" || value == "finitedifference") {
        out = CAEGradientMethod::FiniteDifference;
        return true;
    }
    if (value == "awls" || value == "wls" || value == "adaptive-wls" || value == "adaptiveweightedleastsquares") {
        out = CAEGradientMethod::AdaptiveWeightedLeastSquares;
        return true;
    }
    return false;
}

bool isOptionToken(const std::string& token)
{
    return startsWith(token, "--") || token.find('=') != std::string::npos;
}

bool parseFloatOption(const std::string& value, float& out)
{
    char* endPtr = nullptr;
    const double parsed = std::strtod(value.c_str(), &endPtr);
    if (!endPtr || *endPtr != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    out = static_cast<float>(parsed);
    return true;
}

void printHelp()
{
    std::cout
        << "Usage:\n"
        << "  opengldp_benchmark [dataset] [point|cell] [array] [reps] [options]\n\n"
        << "Common options:\n"
        << "  --dataset=<path>\n"
        << "  --assoc=point|cell\n"
        << "  --array=<name>\n"
        << "  --reps=<n>\n"
        << "  --run=single|benchmarks|fields\n"
        << "  --reference=auto|analytic|vtk|none\n"
        << "  --analytic-bench=on|off\n"
        << "  --method=auto|fd|awls\n"
        << "  --adaptive-neighborhood=on|off\n"
        << "  --adaptive-dimension=on|off\n"
        << "  --adaptive-regularization=on|off\n"
        << "  --min-neighbors=<n>\n"
        << "  --target-neighbors=<n>\n"
        << "  --max-neighbors=<n>\n"
        << "  --radius-scale=<x>\n"
        << "  --plane-eigen-ratio=<x>\n"
        << "  --line-eigen-ratio=<x>\n"
        << "  --lambda-amplify=<x>\n"
        << "  --dump=<n>\n"
        << "  --filter=<text>\n"
        << "  --csv=<path>\n"
        << "  --list-fields\n"
        << "  --list-benchmarks\n"
        << "  --show-config\n"
        << "  --help\n";
}

bool parseCommandLine(int argc, char** argv, Options& opt)
{
    std::vector<std::string> positional;
    positional.reserve(static_cast<size_t>(std::max(argc - 1, 0)));

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return false;
        }
        if (!isOptionToken(arg)) {
            positional.push_back(arg);
        }
    }

    if (!positional.empty()) {
        opt.path = positional[0];
    }
    if (positional.size() >= 2) {
        if (!parseAssociation(positional[1], opt.assoc)) {
            std::cerr << "invalid association: " << positional[1] << std::endl;
            return false;
        }
    }
    if (positional.size() >= 3) {
        opt.arrayName = positional[2];
    }
    if (positional.size() >= 4) {
        if (!parsePositiveInt(positional[3], opt.reps)) {
            std::cerr << "invalid repetitions: " << positional[3] << std::endl;
            return false;
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!isOptionToken(arg)) {
            continue;
        }

        auto parseValueOption = [&](const std::string& key, std::string& out) -> bool {
            if (startsWith(arg, key)) {
                out = arg.substr(key.size());
                return true;
            }
            return false;
        };

        if (arg == "--list-fields") {
            opt.listFields = true;
            continue;
        }
        if (arg == "--list-benchmarks") {
            opt.listBenchmarks = true;
            continue;
        }
        if (arg == "--show-config") {
            opt.showConfig = true;
            continue;
        }
        if (arg == "--no-reference") {
            opt.referenceMode = ReferenceMode::None;
            continue;
        }

        std::string value;
        if (parseValueOption("--dataset=", value) || parseValueOption("dataset=", value)) {
            opt.path = value;
            continue;
        }
        if (parseValueOption("--assoc=", value) || parseValueOption("assoc=", value)) {
            if (!parseAssociation(value, opt.assoc)) {
                std::cerr << "invalid association: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--array=", value) || parseValueOption("array=", value)) {
            opt.arrayName = value;
            continue;
        }
        if (parseValueOption("--reps=", value) || parseValueOption("reps=", value)) {
            if (!parsePositiveInt(value, opt.reps)) {
                std::cerr << "invalid repetitions: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--dump=", value) || parseValueOption("dump=", value)) {
            if (!parsePositiveInt(value, opt.maxSamplesToPrint)) {
                std::cerr << "invalid dump count: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--reference=", value) || parseValueOption("--ref=", value) ||
            parseValueOption("reference=", value) || parseValueOption("ref=", value)) {
            if (!parseReferenceMode(value, opt.referenceMode)) {
                std::cerr << "invalid reference mode: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--run=", value) || parseValueOption("run=", value)) {
            if (!parseRunMode(value, opt.runMode)) {
                std::cerr << "invalid run mode: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--filter=", value) || parseValueOption("filter=", value)) {
            opt.nameFilter = value;
            continue;
        }
        if (parseValueOption("--csv=", value) || parseValueOption("csv=", value)) {
            opt.csvPath = value;
            continue;
        }
        if (parseValueOption("--method=", value) || parseValueOption("method=", value)) {
            if (!parseMethod(value, opt.method)) {
                std::cerr << "invalid method: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--analytic-bench=", value) || parseValueOption("analytic=", value)) {
            if (!parseBoolSwitch(value, opt.enableAnalyticBenchmarks)) {
                std::cerr << "invalid analytic benchmark switch: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--adaptive-neighborhood=", value)) {
            if (!parseBoolSwitch(value, opt.useAdaptiveNeighborhood)) {
                std::cerr << "invalid adaptive-neighborhood switch: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--adaptive-dimension=", value)) {
            if (!parseBoolSwitch(value, opt.useAdaptiveDimension)) {
                std::cerr << "invalid adaptive-dimension switch: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--adaptive-regularization=", value)) {
            if (!parseBoolSwitch(value, opt.useAdaptiveRegularization)) {
                std::cerr << "invalid adaptive-regularization switch: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--min-neighbors=", value)) {
            if (!parsePositiveInt(value, opt.minNeighbors)) {
                std::cerr << "invalid min-neighbors: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--target-neighbors=", value)) {
            if (!parsePositiveInt(value, opt.targetNeighbors)) {
                std::cerr << "invalid target-neighbors: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--max-neighbors=", value)) {
            if (!parsePositiveInt(value, opt.maxNeighbors)) {
                std::cerr << "invalid max-neighbors: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--radius-scale=", value)) {
            if (!parseFloatOption(value, opt.radiusScale)) {
                std::cerr << "invalid radius-scale: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--plane-eigen-ratio=", value)) {
            if (!parseFloatOption(value, opt.planeEigenRatio)) {
                std::cerr << "invalid plane-eigen-ratio: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--line-eigen-ratio=", value)) {
            if (!parseFloatOption(value, opt.lineEigenRatio)) {
                std::cerr << "invalid line-eigen-ratio: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--lambda-amplify=", value)) {
            if (!parseFloatOption(value, opt.lambdaAmplify)) {
                std::cerr << "invalid lambda-amplify: " << arg << std::endl;
                return false;
            }
            continue;
        }

        std::cerr << "unknown option: " << arg << std::endl;
        return false;
    }

    if ((opt.runMode == RunMode::Benchmarks || opt.listBenchmarks) && !opt.enableAnalyticBenchmarks) {
        opt.enableAnalyticBenchmarks = true;
    }
    opt.reps = std::max(opt.reps, 1);
    opt.minNeighbors = std::max(opt.minNeighbors, 1);
    opt.targetNeighbors = std::max(opt.targetNeighbors, opt.minNeighbors);
    opt.maxNeighbors = std::max(opt.maxNeighbors, opt.targetNeighbors);
    return true;
}

bool isExactGradientArray(const std::string& name)
{
    return endsWith(name, "_exact_grad");
}

bool isBenchmarkInputArray(const std::string& name)
{
    return startsWith(name, "benchmark_") && !isExactGradientArray(name);
}

bool shouldKeepField(const CAEFieldInfo& field, RunMode mode, const std::string& filter)
{
    if (mode == RunMode::Benchmarks) {
        return isBenchmarkInputArray(field.name) && containsCaseInsensitive(field.name, filter);
    }
    if (mode == RunMode::Fields) {
        return !isExactGradientArray(field.name) && containsCaseInsensitive(field.name, filter);
    }
    return containsCaseInsensitive(field.name, filter);
}

ReferenceData buildAnalyticReference(CAEProcessingFacade& facade,
                                     const std::string& datasetId,
                                     const std::string& arrayName,
                                     CAEFieldAssociation assoc)
{
    ReferenceData out;
    out.label = "ANALYTIC";
    const std::string refArray = arrayName + "_exact_grad";
    if (facade.getArrayData(datasetId, refArray, assoc, out.values, out.comps)) {
        out.available = true;
        out.analytic = true;
        out.label = "ANALYTIC:" + refArray;
    }
    return out;
}

ReferenceData buildVtkReference(vtkDataSet* dataset,
                                const std::string& arrayName,
                                CAEFieldAssociation assoc,
                                int reps)
{
    ReferenceData out;
    out.label = "VTK";
    if (!dataset) {
        return out;
    }

    vtkNew<vtkGradientFilter> gf;
    gf->SetResultArrayName("__vtk_grad_ref");
    const int vtkAssoc = assoc == CAEFieldAssociation::Point
        ? vtkDataObject::FIELD_ASSOCIATION_POINTS
        : vtkDataObject::FIELD_ASSOCIATION_CELLS;
    gf->SetInputArrayToProcess(0, 0, 0, vtkAssoc, arrayName.c_str());
    gf->SetInputData(dataset);

    double sumMs = 0.0;
    double minMs = std::numeric_limits<double>::max();
    for (int i = 0; i < std::max(reps, 1); ++i) {
        gf->Modified();
        const auto t0 = std::chrono::high_resolution_clock::now();
        gf->Update();
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        sumMs += ms;
        minMs = std::min(minMs, ms);
    }

    vtkDataSet* outDs = vtkDataSet::SafeDownCast(gf->GetOutput());
    if (!outDs) {
        return out;
    }
    vtkDataArray* gradArray = assoc == CAEFieldAssociation::Point
        ? outDs->GetPointData()->GetArray("__vtk_grad_ref")
        : outDs->GetCellData()->GetArray("__vtk_grad_ref");
    if (!gradArray) {
        return out;
    }

    out.comps = gradArray->GetNumberOfComponents();
    const vtkIdType tuples = gradArray->GetNumberOfTuples();
    out.values.resize(static_cast<size_t>(tuples) * static_cast<size_t>(out.comps));
    for (vtkIdType i = 0; i < tuples; ++i) {
        for (int c = 0; c < out.comps; ++c) {
            out.values[static_cast<size_t>(i) * static_cast<size_t>(out.comps) + static_cast<size_t>(c)] =
                static_cast<float>(gradArray->GetComponent(i, c));
        }
    }

    out.available = true;
    out.timeAvgMs = sumMs / static_cast<double>(std::max(reps, 1));
    out.timeMinMs = minMs;
    return out;
}

ReferenceData resolveReference(CAEProcessingFacade& facade,
                               const std::string& datasetId,
                               vtkDataSet* vtkDataset,
                               const std::string& arrayName,
                               CAEFieldAssociation assoc,
                               ReferenceMode mode,
                               int reps)
{
    if (mode == ReferenceMode::None) {
        return ReferenceData{};
    }
    if (mode == ReferenceMode::Analytic) {
        return buildAnalyticReference(facade, datasetId, arrayName, assoc);
    }
    if (mode == ReferenceMode::Vtk) {
        return buildVtkReference(vtkDataset, arrayName, assoc, reps);
    }

    ReferenceData analytic = buildAnalyticReference(facade, datasetId, arrayName, assoc);
    if (analytic.available) {
        return analytic;
    }
    return buildVtkReference(vtkDataset, arrayName, assoc, reps);
}

CompareMetrics compareGradients(const std::vector<float>& result,
                                int resultComps,
                                const ReferenceData& ref)
{
    CompareMetrics metrics;
    metrics.haveReference = ref.available;
    metrics.referenceLabel = ref.label;
    metrics.refTimeAvgMs = ref.timeAvgMs;
    metrics.refTimeMinMs = ref.timeMinMs;

    if (!ref.available || resultComps <= 0 || ref.comps <= 0) {
        return metrics;
    }

    const size_t resultTuples = result.size() / static_cast<size_t>(resultComps);
    const size_t refTuples = ref.values.size() / static_cast<size_t>(ref.comps);
    metrics.tupleCount = std::min(resultTuples, refTuples);
    const int compCap = std::min(resultComps, ref.comps);
    metrics.compareComponents = (compCap / 3) * 3;
    metrics.vecsPerTuple = metrics.compareComponents / 3;
    metrics.rawVecCount = metrics.tupleCount * static_cast<size_t>(metrics.vecsPerTuple);

    if (metrics.compareComponents <= 0 || metrics.tupleCount == 0) {
        return metrics;
    }

    double maeSum = 0.0;
    double rmseAccum = 0.0;
    double refNormSum = 0.0;
    double refNormSqSum = 0.0;
    double refDotResult = 0.0;
    std::vector<double> angleValues;
    std::vector<double> softRelValues;
    std::vector<double> stableSoftRelValues;

    for (size_t i = 0; i < metrics.tupleCount; ++i) {
        for (int v = 0; v < metrics.vecsPerTuple; ++v) {
            const size_t resultBase = i * static_cast<size_t>(resultComps) + static_cast<size_t>(v) * 3u;
            const size_t refBase = i * static_cast<size_t>(ref.comps) + static_cast<size_t>(v) * 3u;

            const double gx = static_cast<double>(result[resultBase + 0]);
            const double gy = static_cast<double>(result[resultBase + 1]);
            const double gz = static_cast<double>(result[resultBase + 2]);
            const double rx = static_cast<double>(ref.values[refBase + 0]);
            const double ry = static_cast<double>(ref.values[refBase + 1]);
            const double rz = static_cast<double>(ref.values[refBase + 2]);

            if (!std::isfinite(gx) || !std::isfinite(gy) || !std::isfinite(gz) ||
                !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz)) {
                ++metrics.nonFiniteVecCount;
                continue;
            }

            const double dx = gx - rx;
            const double dy = gy - ry;
            const double dz = gz - rz;
            const double errNorm = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double refNorm = std::sqrt(rx * rx + ry * ry + rz * rz);
            const double glNorm = std::sqrt(gx * gx + gy * gy + gz * gz);

            ++metrics.finiteVecCount;
            maeSum += errNorm;
            rmseAccum += errNorm * errNorm;
            refNormSum += refNorm;
            refNormSqSum += refNorm * refNorm;
            refDotResult += gx * rx + gy * ry + gz * rz;
            metrics.vecMaxAbs = std::max(metrics.vecMaxAbs, errNorm);

            if (errNorm > metrics.worstErrAbs) {
                metrics.worstErrAbs = errNorm;
                metrics.worstTuple = i;
                metrics.worstVector = v;
                metrics.worstRefNorm = refNorm;
            }

            if (glNorm > 1e-12 && refNorm > 1e-12) {
                double cosTheta = (gx * rx + gy * ry + gz * rz) / (glNorm * refNorm);
                cosTheta = std::clamp(cosTheta, -1.0, 1.0);
                angleValues.push_back(std::acos(cosTheta) * 180.0 / 3.14159265358979323846);
            }
        }
    }

    if (metrics.finiteVecCount == 0) {
        return metrics;
    }

    const double denom = static_cast<double>(metrics.finiteVecCount);
    metrics.vecMaeAbs = maeSum / denom;
    metrics.vecRmseAbs = std::sqrt(rmseAccum / denom);
    metrics.refScaleRms = std::sqrt(refNormSqSum / denom);
    metrics.nmaeVec = refNormSum > 1e-12 ? (maeSum / refNormSum) : 0.0;
    metrics.nrmseVec = refNormSqSum > 1e-12 ? std::sqrt(rmseAccum / refNormSqSum) : 0.0;
    metrics.softRelTau = std::max(1e-12, 0.05 * metrics.refScaleRms);
    metrics.scaleBias = refNormSqSum > 1e-12 ? (refDotResult / refNormSqSum) : 0.0;

    double stableMaeSum = 0.0;
    double stableRmseAccum = 0.0;
    for (size_t i = 0; i < metrics.tupleCount; ++i) {
        for (int v = 0; v < metrics.vecsPerTuple; ++v) {
            const size_t resultBase = i * static_cast<size_t>(resultComps) + static_cast<size_t>(v) * 3u;
            const size_t refBase = i * static_cast<size_t>(ref.comps) + static_cast<size_t>(v) * 3u;

            const double gx = static_cast<double>(result[resultBase + 0]);
            const double gy = static_cast<double>(result[resultBase + 1]);
            const double gz = static_cast<double>(result[resultBase + 2]);
            const double rx = static_cast<double>(ref.values[refBase + 0]);
            const double ry = static_cast<double>(ref.values[refBase + 1]);
            const double rz = static_cast<double>(ref.values[refBase + 2]);
            if (!std::isfinite(gx) || !std::isfinite(gy) || !std::isfinite(gz) ||
                !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz)) {
                continue;
            }

            const double dx = gx - rx;
            const double dy = gy - ry;
            const double dz = gz - rz;
            const double errNorm = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double refNorm = std::sqrt(rx * rx + ry * ry + rz * rz);
            const double softRel = errNorm / std::max(refNorm, metrics.softRelTau);
            softRelValues.push_back(softRel);

            if (refNorm < metrics.softRelTau) {
                ++metrics.lowRefCount;
                continue;
            }

            stableSoftRelValues.push_back(softRel);
            stableMaeSum += errNorm;
            stableRmseAccum += errNorm * errNorm;
            ++metrics.stableVecCount;
        }
    }

    metrics.lowRefRatio = metrics.finiteVecCount > 0
        ? static_cast<double>(metrics.lowRefCount) / static_cast<double>(metrics.finiteVecCount)
        : 0.0;
    metrics.softRelMean = softRelValues.empty()
        ? 0.0
        : std::accumulate(softRelValues.begin(), softRelValues.end(), 0.0) / static_cast<double>(softRelValues.size());
    metrics.softRelMedian = percentile(softRelValues, 0.5);
    metrics.softRelP90 = percentile(softRelValues, 0.9);
    if (metrics.stableVecCount > 0) {
        const double stableDenom = static_cast<double>(metrics.stableVecCount);
        metrics.stableMaeAbs = stableMaeSum / stableDenom;
        metrics.stableRmseAbs = std::sqrt(stableRmseAccum / stableDenom);
        metrics.stableSoftRelMean = std::accumulate(stableSoftRelValues.begin(), stableSoftRelValues.end(), 0.0) / stableDenom;
        metrics.stableSoftRelMedian = percentile(stableSoftRelValues, 0.5);
        metrics.stableSoftRelP90 = percentile(stableSoftRelValues, 0.9);
    }
    metrics.angleCount = angleValues.size();
    metrics.angleMeanDeg = angleValues.empty()
        ? 0.0
        : std::accumulate(angleValues.begin(), angleValues.end(), 0.0) / static_cast<double>(angleValues.size());
    metrics.angleP90Deg = percentile(angleValues, 0.9);
    return metrics;
}

GeometrySummary summarizeGeometry(const GeometryAnalysis& geometry)
{
    GeometrySummary out;
    out.available = geometry.available;
    out.topoDim = geometry.topologicalDim;
    out.globalGeomDim = geometry.globalGeometricDim;
    out.surfaceLike = geometry.surfaceLike;
    out.extents = geometry.bounds.extent;
    out.eigenRatios = geometry.globalEigenRatios;
    for (std::uint32_t dim : geometry.dimTags) {
        if (dim <= 1u) {
            ++out.dim1Count;
        } else if (dim == 2u) {
            ++out.dim2Count;
        } else {
            ++out.dim3Count;
        }
    }
    return out;
}

void printFieldList(const std::vector<CAEFieldInfo>& fields, CAEFieldAssociation assoc)
{
    std::cout << "Available arrays (" << assocName(assoc) << "):" << std::endl;
    for (const auto& field : fields) {
        std::cout << "  - " << field.name
                  << " comps=" << field.numComponents
                  << " tuples=" << field.tupleCount;
        if (isBenchmarkInputArray(field.name)) {
            std::cout << " [benchmark]";
        }
        if (isExactGradientArray(field.name)) {
            std::cout << " [exact-grad]";
        }
        std::cout << std::endl;
    }
}

void printConfig(const Options& opt)
{
    std::cout << "Config"
              << " dataset=" << opt.path
              << " assoc=" << assocName(opt.assoc)
              << " run=" << runModeName(opt.runMode)
              << " reps=" << opt.reps
              << " reference=" << referenceModeName(opt.referenceMode)
              << " analyticBench=" << (opt.enableAnalyticBenchmarks ? "ON" : "OFF")
              << " method=" << methodName(opt.method)
              << " minN=" << opt.minNeighbors
              << " targetN=" << opt.targetNeighbors
              << " maxN=" << opt.maxNeighbors
              << " radiusScale=" << opt.radiusScale
              << " planeRatio=" << opt.planeEigenRatio
              << " lineRatio=" << opt.lineEigenRatio
              << " lambdaAmplify=" << opt.lambdaAmplify
              << " adaptiveNeighborhood=" << (opt.useAdaptiveNeighborhood ? "ON" : "OFF")
              << " adaptiveDimension=" << (opt.useAdaptiveDimension ? "ON" : "OFF")
              << " adaptiveRegularization=" << (opt.useAdaptiveRegularization ? "ON" : "OFF")
              << " dump=" << opt.maxSamplesToPrint;
    if (!opt.arrayName.empty()) {
        std::cout << " array=" << opt.arrayName;
    }
    if (!opt.nameFilter.empty()) {
        std::cout << " filter=" << opt.nameFilter;
    }
    if (!opt.csvPath.empty()) {
        std::cout << " csv=" << opt.csvPath;
    }
    std::cout << std::endl;
}

void printGeometrySummary(const GeometrySummary& geometry)
{
    if (!geometry.available) {
        std::cout << "Geometry=UNAVAILABLE" << std::endl;
        return;
    }

    std::cout << "Geometry"
              << " topo_dim=" << geometry.topoDim
              << " global_geom_dim=" << geometry.globalGeomDim
              << " surface_like=" << (geometry.surfaceLike ? "YES" : "NO")
              << " extents=[" << geometry.extents[0] << "," << geometry.extents[1] << "," << geometry.extents[2] << "]"
              << " eigen_ratios=[" << geometry.eigenRatios[0] << "," << geometry.eigenRatios[1] << "," << geometry.eigenRatios[2] << "]"
              << std::endl;
    std::cout << "GeometryLocalDims"
              << " dim1=" << geometry.dim1Count
              << " dim2=" << geometry.dim2Count
              << " dim3=" << geometry.dim3Count
              << std::endl;
    if (geometry.surfaceLike) {
        std::cout << "GeometryHint=surface-like geometry detected; ambient analytic gradients can over-penalize normal components, so intrinsic tangent metrics are reported separately when exact gradients exist."
                  << std::endl;
    }
}

void printCompareBlock(const std::string& title, const CompareMetrics& m)
{
    if (!m.haveReference) {
        std::cout << "  " << title << ": reference unavailable" << std::endl;
        return;
    }

    std::cout << "  " << title
              << " ref=" << m.referenceLabel
              << " compare_tuples=" << m.tupleCount
              << " compare_components=" << m.compareComponents
              << " raw_vecs=" << m.rawVecCount
              << " finite_vecs=" << m.finiteVecCount
              << " nonfinite_vecs=" << m.nonFiniteVecCount
              << std::endl;
    std::cout << "    abs_mae=" << m.vecMaeAbs
              << " abs_rmse=" << m.vecRmseAbs
              << " abs_max=" << m.vecMaxAbs
              << " ref_rms=" << m.refScaleRms
              << " nmae=" << m.nmaeVec
              << " nrmse=" << m.nrmseVec
              << std::endl;
    std::cout << "    softrel_tau=" << m.softRelTau
              << " softrel_mean=" << m.softRelMean
              << " softrel_median=" << m.softRelMedian
              << " softrel_p90=" << m.softRelP90
              << " low_ref_count=" << m.lowRefCount
              << " low_ref_ratio=" << m.lowRefRatio
              << std::endl;
    std::cout << "    stable_vecs=" << m.stableVecCount
              << " stable_abs_mae=" << m.stableMaeAbs
              << " stable_abs_rmse=" << m.stableRmseAbs
              << " stable_softrel_mean=" << m.stableSoftRelMean
              << " stable_softrel_median=" << m.stableSoftRelMedian
              << " stable_softrel_p90=" << m.stableSoftRelP90
              << std::endl;
    std::cout << "    angle_mean_deg=" << m.angleMeanDeg
              << " angle_p90_deg=" << m.angleP90Deg
              << " angle_count=" << m.angleCount
              << " scale_bias=" << m.scaleBias
              << std::endl;
    if (m.worstVector >= 0) {
        std::cout << "    worst_tuple=" << m.worstTuple
                  << " worst_vector=" << m.worstVector
                  << " worst_abs_err=" << m.worstErrAbs
                  << " worst_ref_norm=" << m.worstRefNorm
                  << std::endl;
    }
}

bool writeCsvReport(const std::string& path,
                    const GeometrySummary& geometry,
                    const Options& opt,
                    const std::vector<CaseRecord>& records)
{
    std::filesystem::path outPath(path);
    if (!outPath.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    out << "dataset,association,array,input_components,success,failure_reason,"
           "run_mode,reference_mode,method,reps,"
           "adaptive_neighborhood,adaptive_dimension,adaptive_regularization,"
           "min_neighbors,target_neighbors,max_neighbors,radius_scale,plane_eigen_ratio,line_eigen_ratio,lambda_amplify,"
           "geom_available,geom_topo_dim,geom_global_dim,geom_surface_like,geom_extent_x,geom_extent_y,geom_extent_z,"
           "geom_eigen_ratio21,geom_eigen_ratio31,geom_eigen_ratio32,geom_dim1_count,geom_dim2_count,geom_dim3_count,"
           "gl_wall_avg_ms,gl_wall_min_ms,gl_gpu_avg_ms,gl_gpu_min_ms,result_tuples,result_components,"
           "ambient_reference,ambient_compare_tuples,ambient_compare_components,ambient_raw_vecs,ambient_finite_vecs,ambient_nonfinite_vecs,ambient_low_ref_count,ambient_low_ref_ratio,"
           "ambient_abs_mae,ambient_abs_rmse,ambient_abs_max,ambient_ref_rms,ambient_nmae,ambient_nrmse,"
           "ambient_softrel_tau,ambient_softrel_mean,ambient_softrel_median,ambient_softrel_p90,"
           "ambient_stable_vecs,ambient_stable_abs_mae,ambient_stable_abs_rmse,ambient_stable_softrel_mean,ambient_stable_softrel_median,ambient_stable_softrel_p90,"
           "ambient_angle_mean_deg,ambient_angle_p90_deg,ambient_angle_count,ambient_scale_bias,ambient_worst_tuple,ambient_worst_vector,ambient_worst_abs_err,ambient_worst_ref_norm,ambient_ref_time_avg_ms,ambient_ref_time_min_ms,"
           "has_intrinsic,intrinsic_reference,intrinsic_compare_tuples,intrinsic_compare_components,intrinsic_raw_vecs,intrinsic_finite_vecs,intrinsic_nonfinite_vecs,intrinsic_low_ref_count,intrinsic_low_ref_ratio,"
           "intrinsic_abs_mae,intrinsic_abs_rmse,intrinsic_abs_max,intrinsic_ref_rms,intrinsic_nmae,intrinsic_nrmse,"
           "intrinsic_softrel_tau,intrinsic_softrel_mean,intrinsic_softrel_median,intrinsic_softrel_p90,"
           "intrinsic_stable_vecs,intrinsic_stable_abs_mae,intrinsic_stable_abs_rmse,intrinsic_stable_softrel_mean,intrinsic_stable_softrel_median,intrinsic_stable_softrel_p90,"
           "intrinsic_angle_mean_deg,intrinsic_angle_p90_deg,intrinsic_angle_count,intrinsic_scale_bias,intrinsic_worst_tuple,intrinsic_worst_vector,intrinsic_worst_abs_err,intrinsic_worst_ref_norm\n";

    auto worstTupleCsv = [](size_t value) -> std::string {
        return value == std::numeric_limits<size_t>::max() ? std::string() : std::to_string(value);
    };

    for (const auto& rec : records) {
        out << csvEscape(rec.dataset) << ','
            << csvEscape(rec.association) << ','
            << csvEscape(rec.arrayName) << ','
            << rec.inputComponents << ','
            << (rec.success ? "1" : "0") << ','
            << csvEscape(rec.failureReason) << ','
            << csvEscape(runModeName(opt.runMode)) << ','
            << csvEscape(referenceModeName(opt.referenceMode)) << ','
            << csvEscape(methodName(opt.method)) << ','
            << opt.reps << ','
            << (opt.useAdaptiveNeighborhood ? "1" : "0") << ','
            << (opt.useAdaptiveDimension ? "1" : "0") << ','
            << (opt.useAdaptiveRegularization ? "1" : "0") << ','
            << opt.minNeighbors << ','
            << opt.targetNeighbors << ','
            << opt.maxNeighbors << ','
            << opt.radiusScale << ','
            << opt.planeEigenRatio << ','
            << opt.lineEigenRatio << ','
            << opt.lambdaAmplify << ','
            << (geometry.available ? "1" : "0") << ','
            << geometry.topoDim << ','
            << geometry.globalGeomDim << ','
            << (geometry.surfaceLike ? "1" : "0") << ','
            << geometry.extents[0] << ','
            << geometry.extents[1] << ','
            << geometry.extents[2] << ','
            << geometry.eigenRatios[0] << ','
            << geometry.eigenRatios[1] << ','
            << geometry.eigenRatios[2] << ','
            << geometry.dim1Count << ','
            << geometry.dim2Count << ','
            << geometry.dim3Count << ','
            << rec.glWallAvgMs << ','
            << rec.glWallMinMs << ','
            << rec.glGpuAvgMs << ','
            << rec.glGpuMinMs << ','
            << rec.resultTuples << ','
            << rec.resultComponents << ','
            << csvEscape(rec.ambientMetrics.referenceLabel) << ','
            << rec.ambientMetrics.tupleCount << ','
            << rec.ambientMetrics.compareComponents << ','
            << rec.ambientMetrics.rawVecCount << ','
            << rec.ambientMetrics.finiteVecCount << ','
            << rec.ambientMetrics.nonFiniteVecCount << ','
            << rec.ambientMetrics.lowRefCount << ','
            << rec.ambientMetrics.lowRefRatio << ','
            << rec.ambientMetrics.vecMaeAbs << ','
            << rec.ambientMetrics.vecRmseAbs << ','
            << rec.ambientMetrics.vecMaxAbs << ','
            << rec.ambientMetrics.refScaleRms << ','
            << rec.ambientMetrics.nmaeVec << ','
            << rec.ambientMetrics.nrmseVec << ','
            << rec.ambientMetrics.softRelTau << ','
            << rec.ambientMetrics.softRelMean << ','
            << rec.ambientMetrics.softRelMedian << ','
            << rec.ambientMetrics.softRelP90 << ','
            << rec.ambientMetrics.stableVecCount << ','
            << rec.ambientMetrics.stableMaeAbs << ','
            << rec.ambientMetrics.stableRmseAbs << ','
            << rec.ambientMetrics.stableSoftRelMean << ','
            << rec.ambientMetrics.stableSoftRelMedian << ','
            << rec.ambientMetrics.stableSoftRelP90 << ','
            << rec.ambientMetrics.angleMeanDeg << ','
            << rec.ambientMetrics.angleP90Deg << ','
            << rec.ambientMetrics.angleCount << ','
            << rec.ambientMetrics.scaleBias << ','
            << worstTupleCsv(rec.ambientMetrics.worstTuple) << ','
            << rec.ambientMetrics.worstVector << ','
            << rec.ambientMetrics.worstErrAbs << ','
            << rec.ambientMetrics.worstRefNorm << ','
            << rec.ambientMetrics.refTimeAvgMs << ','
            << rec.ambientMetrics.refTimeMinMs << ','
            << (rec.hasIntrinsicMetrics ? "1" : "0") << ','
            << csvEscape(rec.intrinsicMetrics.referenceLabel) << ','
            << rec.intrinsicMetrics.tupleCount << ','
            << rec.intrinsicMetrics.compareComponents << ','
            << rec.intrinsicMetrics.rawVecCount << ','
            << rec.intrinsicMetrics.finiteVecCount << ','
            << rec.intrinsicMetrics.nonFiniteVecCount << ','
            << rec.intrinsicMetrics.lowRefCount << ','
            << rec.intrinsicMetrics.lowRefRatio << ','
            << rec.intrinsicMetrics.vecMaeAbs << ','
            << rec.intrinsicMetrics.vecRmseAbs << ','
            << rec.intrinsicMetrics.vecMaxAbs << ','
            << rec.intrinsicMetrics.refScaleRms << ','
            << rec.intrinsicMetrics.nmaeVec << ','
            << rec.intrinsicMetrics.nrmseVec << ','
            << rec.intrinsicMetrics.softRelTau << ','
            << rec.intrinsicMetrics.softRelMean << ','
            << rec.intrinsicMetrics.softRelMedian << ','
            << rec.intrinsicMetrics.softRelP90 << ','
            << rec.intrinsicMetrics.stableVecCount << ','
            << rec.intrinsicMetrics.stableMaeAbs << ','
            << rec.intrinsicMetrics.stableRmseAbs << ','
            << rec.intrinsicMetrics.stableSoftRelMean << ','
            << rec.intrinsicMetrics.stableSoftRelMedian << ','
            << rec.intrinsicMetrics.stableSoftRelP90 << ','
            << rec.intrinsicMetrics.angleMeanDeg << ','
            << rec.intrinsicMetrics.angleP90Deg << ','
            << rec.intrinsicMetrics.angleCount << ','
            << rec.intrinsicMetrics.scaleBias << ','
            << worstTupleCsv(rec.intrinsicMetrics.worstTuple) << ','
            << rec.intrinsicMetrics.worstVector << ','
            << rec.intrinsicMetrics.worstErrAbs << ','
            << rec.intrinsicMetrics.worstRefNorm
            << '\n';
    }

    return true;
}

void printSampleVectors(const std::vector<float>& result,
                        int resultComps,
                        const ReferenceData& ref,
                        const CompareMetrics& ambient,
                        int maxSamples)
{
    if (maxSamples <= 0 || resultComps <= 0) {
        return;
    }

    const size_t resultTuples = result.size() / static_cast<size_t>(resultComps);
    size_t show = std::min(resultTuples, static_cast<size_t>(maxSamples));
    std::set<size_t> sampleIndices;
    if (ambient.worstTuple != std::numeric_limits<size_t>::max()) {
        sampleIndices.insert(ambient.worstTuple);
    }
    for (size_t i = 0; i < show; ++i) {
        sampleIndices.insert(i);
    }

    for (size_t tuple : sampleIndices) {
        if (tuple >= resultTuples) {
            continue;
        }
        std::cout << "  tuple=" << tuple << " GL=[";
        for (int c = 0; c < resultComps; ++c) {
            std::cout << result[tuple * static_cast<size_t>(resultComps) + static_cast<size_t>(c)]
                      << (c + 1 < resultComps ? "," : "");
        }
        std::cout << "]";
        if (ref.available && ref.comps > 0 && tuple < ref.values.size() / static_cast<size_t>(ref.comps)) {
            std::cout << " REF=[";
            for (int c = 0; c < ref.comps; ++c) {
                std::cout << ref.values[tuple * static_cast<size_t>(ref.comps) + static_cast<size_t>(c)]
                          << (c + 1 < ref.comps ? "," : "");
            }
            std::cout << "]";
        }
        std::cout << std::endl;
    }
}

CaseRecord runSingleCase(CAEProcessingFacade& facade,
                         const std::string& datasetId,
                         const CAEDatasetSummary& summary,
                         vtkDataSet* vtkDataset,
                         const GeometryAnalysis& geometry,
                         const CAEFieldInfo& field,
                         const Options& opt,
                         bool verboseSamples)
{
    CaseRecord rec;
    rec.dataset = summary.displayName;
    rec.association = assocName(opt.assoc);
    rec.arrayName = field.name;
    rec.inputComponents = field.numComponents;

    CAEGradientRequest req;
    req.datasetId = datasetId;
    req.inputArrayName = field.name;
    req.association = opt.assoc;
    req.method = opt.method;
    req.useAdaptiveNeighborhood = opt.useAdaptiveNeighborhood;
    req.useAdaptiveDimension = opt.useAdaptiveDimension;
    req.useAdaptiveRegularization = opt.useAdaptiveRegularization;
    req.minNeighbors = opt.minNeighbors;
    req.targetNeighbors = opt.targetNeighbors;
    req.maxNeighbors = opt.maxNeighbors;
    req.radiusScale = opt.radiusScale;
    req.planeEigenRatio = opt.planeEigenRatio;
    req.lineEigenRatio = opt.lineEigenRatio;
    req.lambdaAmplify = opt.lambdaAmplify;

    double wallSum = 0.0;
    double wallMin = std::numeric_limits<double>::max();
    double gpuSum = 0.0;
    double gpuMin = std::numeric_limits<double>::max();
    CAEGradientResultMeta meta;

    for (int i = 0; i < std::max(opt.reps, 1); ++i) {
        if (!facade.computeGradient(req, meta)) {
            rec.failureReason = "computeGradient failed";
            return rec;
        }
        const double wall = facade.getLastComputeWallMs();
        const double gpu = facade.getLastComputeGpuMs();
        wallSum += wall;
        gpuSum += gpu;
        wallMin = std::min(wallMin, wall);
        gpuMin = std::min(gpuMin, gpu);
    }

    std::vector<float> result;
    int resultComps = 0;
    if (!facade.getArrayData(datasetId, meta.resultArrayName, opt.assoc, result, resultComps)) {
        rec.failureReason = "failed to fetch result array";
        return rec;
    }

    ReferenceData ref = resolveReference(
        facade, datasetId, vtkDataset, field.name, opt.assoc, opt.referenceMode, opt.reps);
    if (opt.referenceMode != ReferenceMode::Auto &&
        opt.referenceMode != ReferenceMode::None &&
        !ref.available) {
        rec.failureReason = "requested reference unavailable";
        return rec;
    }

    rec.success = true;
    rec.glWallAvgMs = wallSum / static_cast<double>(std::max(opt.reps, 1));
    rec.glWallMinMs = wallMin;
    rec.glGpuAvgMs = gpuSum / static_cast<double>(std::max(opt.reps, 1));
    rec.glGpuMinMs = gpuMin;
    rec.resultComponents = resultComps;
    rec.resultTuples = resultComps > 0 ? result.size() / static_cast<size_t>(resultComps) : 0;
    rec.ambientMetrics = compareGradients(result, resultComps, ref);

    if (ref.available && ref.analytic && geometry.available && geometry.surfaceLike) {
        ReferenceData intrinsicRef = ref;
        intrinsicRef.label = ref.label + " [intrinsic]";
        intrinsicRef.values = projectGradientToIntrinsic(ref.values, ref.comps, geometry);
        std::vector<float> intrinsicResult = projectGradientToIntrinsic(result, resultComps, geometry);
        rec.intrinsicMetrics = compareGradients(intrinsicResult, resultComps, intrinsicRef);
        rec.hasIntrinsicMetrics = true;
    }

    std::cout << "Case=" << field.name
              << " input_components=" << field.numComponents
              << " result_tuples=" << rec.resultTuples
              << " result_components=" << rec.resultComponents
              << " gl_wall_avg_ms=" << rec.glWallAvgMs
              << " gl_gpu_avg_ms=" << rec.glGpuAvgMs
              << std::endl;
    printCompareBlock("Ambient", rec.ambientMetrics);
    if (rec.hasIntrinsicMetrics) {
        printCompareBlock("Intrinsic", rec.intrinsicMetrics);
    }

    if (verboseSamples) {
        printSampleVectors(result, resultComps, ref, rec.ambientMetrics, opt.maxSamplesToPrint);
    }

    return rec;
}
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        }
    }

    Options opt;
    if (!parseCommandLine(argc, argv, opt)) {
        return argc > 1 ? 1 : 0;
    }

    if (opt.showConfig) {
        printConfig(opt);
    }

    CAEProcessingFacade facade;
    if (!facade.initialize("Shaders")) {
        std::cerr << "facade init failed" << std::endl;
        return 1;
    }
    facade.setAnalyticBenchmarkEnabled(opt.enableAnalyticBenchmarks);

    const std::string datasetId = facade.loadDatasetFromVTKFile(opt.path);
    if (datasetId.empty()) {
        std::cerr << "load dataset failed: " << opt.path << std::endl;
        return 2;
    }

    CAEDatasetSummary summary;
    if (!facade.getDatasetSummary(datasetId, summary)) {
        std::cerr << "failed to query dataset summary" << std::endl;
        return 3;
    }

    std::vector<CAEFieldInfo> fields;
    if (!facade.listFields(datasetId, opt.assoc, fields) || fields.empty()) {
        std::cerr << "no arrays for association " << assocName(opt.assoc) << std::endl;
        return 4;
    }
    std::sort(fields.begin(), fields.end(), [](const CAEFieldInfo& a, const CAEFieldInfo& b) {
        return a.name < b.name;
    });

    vtkSmartPointer<vtkDataSet> vtkDataset;
    if (!facade.exportDatasetToVTK(datasetId, vtkDataset)) {
        std::cout << "GeometryHint=failed to export dataset to VTK, geometry analysis and VTK reference may be unavailable" << std::endl;
    }

    DataObject data;
    GeometryAnalysis geometry;
    GeometrySummary geometrySummary;
    if (vtkDataset && buildDataObjectFromVtk(vtkDataset, data)) {
        geometry = analyzeGeometry(data, opt.assoc, opt.planeEigenRatio, opt.lineEigenRatio);
        geometrySummary = summarizeGeometry(geometry);
    }

    std::cout << "Dataset=" << summary.displayName
              << " points=" << summary.pointCount
              << " cells=" << summary.cellCount
              << " association=" << assocName(opt.assoc)
              << " run=" << runModeName(opt.runMode)
              << " reference=" << referenceModeName(opt.referenceMode)
              << " method=" << methodName(opt.method)
              << std::endl;
    printGeometrySummary(geometrySummary);

    if (opt.listFields) {
        printFieldList(fields, opt.assoc);
        return 0;
    }

    if (opt.listBenchmarks) {
        std::cout << "Analytic benchmark inputs (" << assocName(opt.assoc) << "):" << std::endl;
        for (const auto& field : fields) {
            if (!isBenchmarkInputArray(field.name)) {
                continue;
            }
            std::cout << "  - " << field.name
                      << " comps=" << field.numComponents
                      << " exact=" << field.name << "_exact_grad"
                      << std::endl;
        }
        return 0;
    }

    std::vector<CAEFieldInfo> selected;
    if (opt.runMode == RunMode::Single) {
        if (!opt.arrayName.empty()) {
            auto it = std::find_if(fields.begin(), fields.end(),
                [&](const CAEFieldInfo& field) { return field.name == opt.arrayName; });
            if (it == fields.end()) {
                std::cerr << "selected array missing: " << opt.arrayName << std::endl;
                return 5;
            }
            selected.push_back(*it);
        } else {
            auto it = std::find_if(fields.begin(), fields.end(),
                [&](const CAEFieldInfo& field) {
                    return !isExactGradientArray(field.name) && containsCaseInsensitive(field.name, opt.nameFilter);
                });
            if (it == fields.end()) {
                std::cerr << "no usable field found" << std::endl;
                return 5;
            }
            selected.push_back(*it);
        }
    } else {
        for (const auto& field : fields) {
            if (shouldKeepField(field, opt.runMode, opt.nameFilter)) {
                selected.push_back(field);
            }
        }
    }

    if (selected.empty()) {
        std::cerr << "no fields selected" << std::endl;
        return 6;
    }

    std::cout << "SelectedCases=" << selected.size() << std::endl;
    for (const auto& field : selected) {
        std::cout << "  - " << field.name
                  << " comps=" << field.numComponents
                  << " tuples=" << field.tupleCount
                  << std::endl;
    }

    std::vector<CaseRecord> records;
    records.reserve(selected.size());
    bool anyFailure = false;

    for (size_t i = 0; i < selected.size(); ++i) {
        std::cout << "CaseStart index=" << (i + 1) << "/" << selected.size()
                  << " name=" << selected[i].name
                  << std::endl;
        CaseRecord rec = runSingleCase(
            facade,
            datasetId,
            summary,
            vtkDataset,
            geometry,
            selected[i],
            opt,
            opt.runMode == RunMode::Single);
        if (!rec.success) {
            anyFailure = true;
            std::cout << "CaseFailed name=" << rec.arrayName
                      << " reason=" << rec.failureReason
                      << std::endl;
        }
        records.push_back(rec);
        std::cout << "CaseEnd index=" << (i + 1) << "/" << selected.size()
                  << " success=" << (rec.success ? "YES" : "NO")
                  << std::endl;
    }

    if (!opt.csvPath.empty()) {
        if (writeCsvReport(opt.csvPath, geometrySummary, opt, records)) {
            std::cout << "CSVReport=" << opt.csvPath << std::endl;
        } else {
            std::cout << "CSVReportFailed=" << opt.csvPath << std::endl;
            anyFailure = true;
        }
    }

    size_t successCount = 0;
    std::vector<double> ambientStableSoftRel;
    std::vector<double> intrinsicStableSoftRel;
    for (const auto& rec : records) {
        successCount += rec.success ? 1u : 0u;
        if (rec.success && rec.ambientMetrics.haveReference && rec.ambientMetrics.stableVecCount > 0) {
            ambientStableSoftRel.push_back(rec.ambientMetrics.stableSoftRelMean);
        }
        if (rec.success && rec.hasIntrinsicMetrics && rec.intrinsicMetrics.stableVecCount > 0) {
            intrinsicStableSoftRel.push_back(rec.intrinsicMetrics.stableSoftRelMean);
        }
    }

    std::cout << "Summary success=" << successCount
              << " failed=" << (records.size() - successCount)
              << " total=" << records.size()
              << std::endl;

    if (!ambientStableSoftRel.empty()) {
        const double meanAmbientStable = std::accumulate(
            ambientStableSoftRel.begin(), ambientStableSoftRel.end(), 0.0) /
            static_cast<double>(ambientStableSoftRel.size());
        std::cout << "SummaryAmbient stable_softrel_mean=" << meanAmbientStable << std::endl;
    }
    if (!intrinsicStableSoftRel.empty()) {
        const double meanIntrinsicStable = std::accumulate(
            intrinsicStableSoftRel.begin(), intrinsicStableSoftRel.end(), 0.0) /
            static_cast<double>(intrinsicStableSoftRel.size());
        std::cout << "SummaryIntrinsic stable_softrel_mean=" << meanIntrinsicStable << std::endl;
    }

    return anyFailure ? 7 : 0;
}
