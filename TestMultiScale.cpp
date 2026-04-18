#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "CAEProcessingFacade.h"
#include "TestHarnessUtils.h"

namespace
{
using namespace testharness;

enum class RunMode
{
    Synthetic,
    Fields
};

struct Options
{
    std::string path = "Data\\ShipHull_0.vtk";
    CAEFieldAssociation assoc = CAEFieldAssociation::Point;
    RunMode runMode = RunMode::Synthetic;
    std::string arrayName;
    std::string nameFilter;
    std::string noiseMode = "all";
    int reps = 3;
    int levels = 3;
    int iterations = 1;
    bool storeIntermediate = true;
    bool listFields = false;
    bool listSynthetic = false;
    bool showConfig = false;
    std::string csvPath = "results\\multiscale_report.csv";
    std::string exportPath="results\\multiscale_report.vtk";

    float spatialSigmaFactor = 1.5f;
    float rangeSigmaFactor = 0.5f;
    float levelScale = 1.8f;
    float edgeSigmaFactor = 0.35f;
    float detailGain0 = 1.0f;
    float detailGain1 = 0.75f;
    float detailGain2 = 0.5f;

    float sigmaFactor = 0.35f;
    float impulseRatio = 0.04f;
    float impulseScale = 2.5f;
    int seed = 1337;
};

struct ErrorMetrics
{
    bool available = false;
    size_t sampleCount = 0;
    size_t finiteCount = 0;
    size_t nonFiniteCount = 0;
    double mae = 0.0;
    double rmse = 0.0;
    double maxAbs = 0.0;
    double signalRms = 0.0;
    double nmae = 0.0;
    double nrmse = 0.0;
};

struct CaseRecord
{
    std::string dataset;
    std::string association;
    std::string mode;
    std::string cleanArrayName;
    std::string inputArrayName;
    std::string fusedArrayName;
    std::string baseArrayName;
    std::vector<std::string> smoothArrayNames;
    std::vector<std::string> detailArrayNames;
    std::string noiseTag;
    std::string caseLabel;
    std::string exportedVtkPath;
    bool exportSuccess = false;
    int components = 0;
    bool success = false;
    std::string failureReason;
    double wallAvgMs = 0.0;
    double wallMinMs = 0.0;
    double gpuAvgMs = 0.0;
    double gpuMinMs = 0.0;

    bool hasCleanReference = false;
    double cleanStd = 0.0;
    double inputStd = 0.0;
    double fusedStd = 0.0;
    double cleanRoughness = 0.0;
    double inputRoughness = 0.0;
    double fusedRoughness = 0.0;
    double inputToFusedMeanAbsDelta = 0.0;
    ErrorMetrics inputError;
    ErrorMetrics fusedError;
    double maeImprovementRatio = 0.0;
    double rmseImprovementRatio = 0.0;
    double roughnessRatio = 0.0;
};

const char* assocName(CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point ? "POINT" : "CELL";
}

const char* runModeName(RunMode mode)
{
    return mode == RunMode::Synthetic ? "synthetic" : "fields";
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

bool parseRunMode(std::string value, RunMode& out)
{
    value = toLower(std::move(value));
    if (value == "synthetic" || value == "bench" || value == "benchmarks") {
        out = RunMode::Synthetic;
        return true;
    }
    if (value == "fields" || value == "real") {
        out = RunMode::Fields;
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
        << "  opengldp_multiscale_test [dataset] [point|cell] [options]\n\n"
        << "Modes:\n"
        << "  --run=synthetic|fields\n\n"
        << "Common options:\n"
        << "  --dataset=<path>\n"
        << "  --assoc=point|cell\n"
        << "  --array=<name>\n"
        << "  --filter=<text>\n"
        << "  --reps=<n>\n"
        << "  --levels=<n>\n"
        << "  --iterations=<n>\n"
        << "  --store-intermediate=on|off\n"
        << "  --csv=<path>\n"
        << "  --export=<path-or-dir>\n"
        << "  --list-fields\n"
        << "  --list-synthetic\n"
        << "  --show-config\n"
        << "Synthetic options:\n"
        << "  --noise=gaussian|impulse|mixed|all\n"
        << "  --sigma-factor=<x>\n"
        << "  --impulse-ratio=<x>\n"
        << "  --impulse-scale=<x>\n"
        << "  --seed=<n>\n"
        << "Fusion options:\n"
        << "  --spatial-sigma-factor=<x>\n"
        << "  --range-sigma-factor=<x>\n"
        << "  --level-scale=<x>\n"
        << "  --edge-sigma-factor=<x>\n"
        << "  --detail-gain0=<x>\n"
        << "  --detail-gain1=<x>\n"
        << "  --detail-gain2=<x>\n\n"
        << "Export notes:\n"
        << "  If one case is selected and --export ends with .vtk, that exact file is written.\n"
        << "  If multiple cases are selected, --export is treated as a directory or filename prefix,\n"
        << "  and one VTK file is written per case for ParaView inspection.\n";
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
        if (arg == "--list-synthetic") {
            opt.listSynthetic = true;
            continue;
        }
        if (arg == "--show-config") {
            opt.showConfig = true;
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
        if (parseValueOption("--run=", value) || parseValueOption("run=", value)) {
            if (!parseRunMode(value, opt.runMode)) {
                std::cerr << "invalid run mode: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--array=", value) || parseValueOption("array=", value)) {
            opt.arrayName = value;
            continue;
        }
        if (parseValueOption("--filter=", value) || parseValueOption("filter=", value)) {
            opt.nameFilter = value;
            continue;
        }
        if (parseValueOption("--noise=", value) || parseValueOption("noise=", value)) {
            opt.noiseMode = toLower(value);
            continue;
        }
        if (parseValueOption("--csv=", value) || parseValueOption("csv=", value)) {
            opt.csvPath = value;
            continue;
        }
        if (parseValueOption("--export=", value) || parseValueOption("export=", value)) {
            opt.exportPath = value;
            continue;
        }
        if (parseValueOption("--reps=", value) || parseValueOption("reps=", value)) {
            if (!parsePositiveInt(value, opt.reps)) {
                std::cerr << "invalid reps: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--levels=", value) || parseValueOption("levels=", value)) {
            if (!parsePositiveInt(value, opt.levels)) {
                std::cerr << "invalid levels: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--iterations=", value) || parseValueOption("iterations=", value)) {
            if (!parsePositiveInt(value, opt.iterations)) {
                std::cerr << "invalid iterations: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--seed=", value) || parseValueOption("seed=", value)) {
            if (!parsePositiveInt(value, opt.seed)) {
                std::cerr << "invalid seed: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--store-intermediate=", value)) {
            if (!parseBoolSwitch(value, opt.storeIntermediate)) {
                std::cerr << "invalid store-intermediate: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--spatial-sigma-factor=", value)) {
            if (!parseFloatOption(value, opt.spatialSigmaFactor)) {
                std::cerr << "invalid spatial sigma factor: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--range-sigma-factor=", value)) {
            if (!parseFloatOption(value, opt.rangeSigmaFactor)) {
                std::cerr << "invalid range sigma factor: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--level-scale=", value)) {
            if (!parseFloatOption(value, opt.levelScale)) {
                std::cerr << "invalid level scale: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--edge-sigma-factor=", value)) {
            if (!parseFloatOption(value, opt.edgeSigmaFactor)) {
                std::cerr << "invalid edge sigma factor: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--detail-gain0=", value)) {
            if (!parseFloatOption(value, opt.detailGain0)) {
                std::cerr << "invalid detail-gain0: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--detail-gain1=", value)) {
            if (!parseFloatOption(value, opt.detailGain1)) {
                std::cerr << "invalid detail-gain1: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--detail-gain2=", value)) {
            if (!parseFloatOption(value, opt.detailGain2)) {
                std::cerr << "invalid detail-gain2: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--sigma-factor=", value)) {
            if (!parseFloatOption(value, opt.sigmaFactor)) {
                std::cerr << "invalid sigma-factor: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--impulse-ratio=", value)) {
            if (!parseFloatOption(value, opt.impulseRatio)) {
                std::cerr << "invalid impulse-ratio: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--impulse-scale=", value)) {
            if (!parseFloatOption(value, opt.impulseScale)) {
                std::cerr << "invalid impulse-scale: " << arg << std::endl;
                return false;
            }
            continue;
        }

        std::cerr << "unknown option: " << arg << std::endl;
        return false;
    }

    opt.reps = std::max(opt.reps, 1);
    opt.levels = std::clamp(opt.levels, 1, 3);
    opt.iterations = std::max(opt.iterations, 1);
    return true;
}

bool shouldKeepField(const CAEFieldInfo& field, const Options& opt)
{
    if (!opt.arrayName.empty() && field.name != opt.arrayName) {
        return false;
    }
    if (!opt.nameFilter.empty() && !containsCaseInsensitive(field.name, opt.nameFilter)) {
        return false;
    }
    if (containsCaseInsensitive(field.name, "_ms_")) {
        return false;
    }
    if (endsWith(field.name, "_exact_grad")) {
        return false;
    }
    return true;
}

std::vector<NoiseKind> resolveNoiseKinds(const Options& opt)
{
    const std::string mode = toLower(opt.noiseMode);
    if (mode == "gaussian") {
        return { NoiseKind::Gaussian };
    }
    if (mode == "impulse") {
        return { NoiseKind::Impulse };
    }
    if (mode == "mixed") {
        return { NoiseKind::Mixed };
    }
    return { NoiseKind::Gaussian, NoiseKind::Impulse, NoiseKind::Mixed };
}

ErrorMetrics computeErrorMetrics(const std::vector<float>& values,
                                 const std::vector<float>& reference)
{
    ErrorMetrics out;
    out.available = values.size() == reference.size() && !values.empty();
    out.sampleCount = values.size();
    if (!out.available) {
        return out;
    }

    double maeSum = 0.0;
    double rmseAccum = 0.0;
    double signalAbsSum = 0.0;
    double signalSqSum = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        const double v = static_cast<double>(values[i]);
        const double r = static_cast<double>(reference[i]);
        if (!std::isfinite(v) || !std::isfinite(r)) {
            ++out.nonFiniteCount;
            continue;
        }
        const double e = std::abs(v - r);
        ++out.finiteCount;
        maeSum += e;
        rmseAccum += e * e;
        signalAbsSum += std::abs(r);
        signalSqSum += r * r;
        out.maxAbs = std::max(out.maxAbs, e);
    }

    if (out.finiteCount == 0) {
        out.available = false;
        return out;
    }

    const double denom = static_cast<double>(out.finiteCount);
    out.mae = maeSum / denom;
    out.rmse = std::sqrt(rmseAccum / denom);
    out.signalRms = std::sqrt(signalSqSum / denom);
    out.nmae = signalAbsSum > 1e-12 ? (maeSum / signalAbsSum) : 0.0;
    out.nrmse = signalSqSum > 1e-12 ? std::sqrt(rmseAccum / signalSqSum) : 0.0;
    return out;
}

void printConfig(const Options& opt)
{
    std::cout << "Config"
              << " dataset=" << opt.path
              << " assoc=" << assocName(opt.assoc)
              << " run=" << runModeName(opt.runMode)
              << " reps=" << opt.reps
              << " levels=" << opt.levels
              << " iterations=" << opt.iterations
              << " storeIntermediate=" << (opt.storeIntermediate ? "ON" : "OFF")
              << " noise=" << opt.noiseMode
              << " sigmaFactor=" << opt.sigmaFactor
              << " impulseRatio=" << opt.impulseRatio
              << " impulseScale=" << opt.impulseScale
              << " csv=" << opt.csvPath;
    if (!opt.arrayName.empty()) {
        std::cout << " array=" << opt.arrayName;
    }
    if (!opt.nameFilter.empty()) {
        std::cout << " filter=" << opt.nameFilter;
    }
    if (!opt.exportPath.empty()) {
        std::cout << " export=" << opt.exportPath;
    }
    std::cout << std::endl;
}

void printFieldList(const std::vector<CAEFieldInfo>& fields, CAEFieldAssociation assoc)
{
    std::cout << "Available arrays (" << assocName(assoc) << "):" << std::endl;
    for (const auto& field : fields) {
        std::cout << "  - " << field.name
                  << " comps=" << field.numComponents
                  << " tuples=" << field.tupleCount
                  << std::endl;
    }
}

void printSyntheticList(const std::vector<NamedArray>& arrays)
{
    std::cout << "Synthetic clean fields:" << std::endl;
    for (const auto& array : arrays) {
        std::cout << "  - " << array.name
                  << " comps=" << array.numComponents
                  << " tuples=" << (array.numComponents > 0 ? array.data.size() / static_cast<size_t>(array.numComponents) : 0)
                  << std::endl;
    }
}

std::string joinStrings(const std::vector<std::string>& values, const char* sep)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        out += values[i];
    }
    return out;
}

std::string sanitizeFileToken(std::string value)
{
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if ((uch >= '0' && uch <= '9') ||
            (uch >= 'a' && uch <= 'z') ||
            (uch >= 'A' && uch <= 'Z')) {
            continue;
        }
        ch = '_';
    }

    std::string compact;
    compact.reserve(value.size());
    bool prevUnderscore = false;
    for (char ch : value) {
        if (ch == '_') {
            if (prevUnderscore) {
                continue;
            }
            prevUnderscore = true;
        } else {
            prevUnderscore = false;
        }
        compact.push_back(ch);
    }

    while (!compact.empty() && compact.front() == '_') {
        compact.erase(compact.begin());
    }
    while (!compact.empty() && compact.back() == '_') {
        compact.pop_back();
    }
    if (compact.empty()) {
        compact = "case";
    }
    return compact;
}

bool pathLooksLikeVtkFile(const std::filesystem::path& path)
{
    return toLower(path.extension().string()) == ".vtk";
}

std::filesystem::path resolveCaseExportPath(const Options& opt,
                                            const CaseRecord& rec,
                                            size_t caseIndex,
                                            size_t totalCases)
{
    if (opt.exportPath.empty()) {
        return {};
    }

    const std::filesystem::path raw(opt.exportPath);
    const bool singleCase = totalCases == 1;
    if (singleCase && pathLooksLikeVtkFile(raw)) {
        return raw;
    }

    std::filesystem::path directory;
    std::string prefix;
    if (pathLooksLikeVtkFile(raw)) {
        directory = raw.parent_path();
        prefix = raw.stem().string();
    } else {
        directory = raw;
        prefix = std::filesystem::path(opt.path).stem().string();
    }

    if (directory.empty()) {
        directory = ".";
    }

    const std::string indexTag = std::to_string(caseIndex + 1);
    const std::string label = sanitizeFileToken(rec.caseLabel.empty() ? rec.inputArrayName : rec.caseLabel);
    const std::string fileName =
        sanitizeFileToken(prefix) + "__" + indexTag + "__" + label + ".vtk";
    return directory / fileName;
}

bool ensureParentDirectory(const std::filesystem::path& path)
{
    if (path.empty() || path.parent_path().empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec;
}

bool exportCaseDataset(const Options& opt,
                       const CaseRecord& rec,
                       size_t caseIndex,
                       size_t totalCases,
                       CAEProcessingFacade& sourceFacade,
                       const std::string& sourceDatasetId)
{
    if (opt.exportPath.empty()) {
        return true;
    }

    const std::filesystem::path outPath = resolveCaseExportPath(opt, rec, caseIndex, totalCases);
    if (outPath.empty() || !ensureParentDirectory(outPath)) {
        return false;
    }

    CAEProcessingFacade exportFacade;
    const std::string exportDatasetId = exportFacade.loadDatasetFromVTKFile(opt.path);
    if (exportDatasetId.empty()) {
        return false;
    }

    std::vector<std::string> arraysToCopy;
    auto pushUnique = [&](const std::string& name) {
        if (name.empty()) {
            return;
        }
        if (std::find(arraysToCopy.begin(), arraysToCopy.end(), name) == arraysToCopy.end()) {
            arraysToCopy.push_back(name);
        }
    };

    pushUnique(rec.cleanArrayName);
    pushUnique(rec.inputArrayName);
    pushUnique(rec.baseArrayName);
    pushUnique(rec.fusedArrayName);
    for (const auto& name : rec.smoothArrayNames) {
        pushUnique(name);
    }
    for (const auto& name : rec.detailArrayNames) {
        pushUnique(name);
    }

    for (const auto& arrayName : arraysToCopy) {
        std::vector<float> data;
        int comps = 0;
        if (!sourceFacade.getArrayData(sourceDatasetId, arrayName, opt.assoc, data, comps)) {
            return false;
        }
        if (!exportFacade.upsertArrayData(exportDatasetId, arrayName, opt.assoc, data, comps)) {
            return false;
        }
    }

    return exportFacade.saveDatasetToVTKFile(exportDatasetId, outPath.string(), true);
}

CAEMultiScaleRequest buildRequest(const Options& opt,
                                  const std::string& datasetId,
                                  const std::string& inputArray)
{
    CAEMultiScaleRequest req;
    req.datasetId = datasetId;
    req.inputArrayName = inputArray;
    req.association = opt.assoc;
    req.levels = opt.levels;
    req.iterationsPerLevel = opt.iterations;
    req.spatialSigmaFactor = opt.spatialSigmaFactor;
    req.rangeSigmaFactor = opt.rangeSigmaFactor;
    req.levelScale = opt.levelScale;
    req.edgeSigmaFactor = opt.edgeSigmaFactor;
    req.detailGain0 = opt.detailGain0;
    req.detailGain1 = opt.detailGain1;
    req.detailGain2 = opt.detailGain2;
    req.storeIntermediate = opt.storeIntermediate;
    return req;
}

bool runMultiScale(CAEProcessingFacade& facade,
                   const Options& opt,
                   const std::string& datasetId,
                   const std::string& inputArray,
                   CAEMultiScaleResultMeta& meta,
                   double& wallAvgMs,
                   double& wallMinMs,
                   double& gpuAvgMs,
                   double& gpuMinMs)
{
    wallAvgMs = 0.0;
    wallMinMs = std::numeric_limits<double>::max();
    gpuAvgMs = 0.0;
    gpuMinMs = std::numeric_limits<double>::max();

    for (int i = 0; i < std::max(opt.reps, 1); ++i) {
        if (!facade.computeMultiScaleDecompositionAndFusion(buildRequest(opt, datasetId, inputArray), meta)) {
            return false;
        }
        wallAvgMs += meta.computeWallMs;
        gpuAvgMs += meta.computeGpuMs;
        wallMinMs = std::min(wallMinMs, meta.computeWallMs);
        gpuMinMs = std::min(gpuMinMs, meta.computeGpuMs);
    }

    wallAvgMs /= static_cast<double>(std::max(opt.reps, 1));
    gpuAvgMs /= static_cast<double>(std::max(opt.reps, 1));
    return true;
}

CaseRecord runSyntheticCase(CAEProcessingFacade& facade,
                            const std::string& datasetId,
                            const CAEDatasetSummary& summary,
                            const Options& opt,
                            const std::vector<int>& offsets,
                            const std::vector<int>& neighbors,
                            const NamedArray& clean,
                            NoiseKind noiseKind,
                            std::uint32_t seed)
{
    CaseRecord rec;
    rec.dataset = summary.displayName;
    rec.association = assocName(opt.assoc);
    rec.mode = "synthetic";
    rec.cleanArrayName = clean.name;
    rec.noiseTag = noiseKindTag(noiseKind);
    rec.caseLabel = clean.name + "_" + rec.noiseTag;
    rec.components = clean.numComponents;
    rec.hasCleanReference = true;

    const std::string noisyName = clean.name + "_noisy_" + rec.noiseTag;
    const std::vector<float> noisy = addNoise(
        clean.data,
        clean.numComponents,
        noiseKind,
        opt.sigmaFactor,
        opt.impulseRatio,
        opt.impulseScale,
        seed);

    if (!facade.upsertArrayData(datasetId, clean.name, opt.assoc, clean.data, clean.numComponents)) {
        rec.failureReason = "failed to insert clean array";
        return rec;
    }
    if (!facade.upsertArrayData(datasetId, noisyName, opt.assoc, noisy, clean.numComponents)) {
        rec.failureReason = "failed to insert noisy array";
        return rec;
    }

    CAEMultiScaleResultMeta meta;
    if (!runMultiScale(facade, opt, datasetId, noisyName, meta,
                       rec.wallAvgMs, rec.wallMinMs, rec.gpuAvgMs, rec.gpuMinMs)) {
        rec.failureReason = "computeMultiScaleDecompositionAndFusion failed";
        return rec;
    }

    std::vector<float> fused;
    int fusedComps = 0;
    if (!facade.getArrayData(datasetId, meta.fusedArrayName, opt.assoc, fused, fusedComps)) {
        rec.failureReason = "failed to fetch fused array";
        return rec;
    }

    rec.success = true;
    rec.inputArrayName = noisyName;
    rec.baseArrayName = meta.baseArrayName;
    rec.fusedArrayName = meta.fusedArrayName;
    rec.smoothArrayNames = meta.smoothArrayNames;
    rec.detailArrayNames = meta.detailArrayNames;
    rec.cleanStd = computeStdDev(clean.data);
    rec.inputStd = computeStdDev(noisy);
    rec.fusedStd = computeStdDev(fused);
    rec.cleanRoughness = computeGraphRoughness(clean.data, clean.numComponents, offsets, neighbors);
    rec.inputRoughness = computeGraphRoughness(noisy, clean.numComponents, offsets, neighbors);
    rec.fusedRoughness = computeGraphRoughness(fused, fusedComps, offsets, neighbors);
    rec.inputToFusedMeanAbsDelta = computeMeanAbsDelta(noisy, fused);
    rec.inputError = computeErrorMetrics(noisy, clean.data);
    rec.fusedError = computeErrorMetrics(fused, clean.data);
    if (rec.inputError.available && rec.inputError.mae > 1e-12) {
        rec.maeImprovementRatio = rec.fusedError.mae / rec.inputError.mae;
    }
    if (rec.inputError.available && rec.inputError.rmse > 1e-12) {
        rec.rmseImprovementRatio = rec.fusedError.rmse / rec.inputError.rmse;
    }
    if (rec.inputRoughness > 1e-12) {
        rec.roughnessRatio = rec.fusedRoughness / rec.inputRoughness;
    }
    return rec;
}

CaseRecord runFieldCase(CAEProcessingFacade& facade,
                        const std::string& datasetId,
                        const CAEDatasetSummary& summary,
                        const Options& opt,
                        const std::vector<int>& offsets,
                        const std::vector<int>& neighbors,
                        const CAEFieldInfo& field)
{
    CaseRecord rec;
    rec.dataset = summary.displayName;
    rec.association = assocName(opt.assoc);
    rec.mode = "fields";
    rec.inputArrayName = field.name;
    rec.caseLabel = field.name;
    rec.components = field.numComponents;

    std::vector<float> input;
    int inputComps = 0;
    if (!facade.getArrayData(datasetId, field.name, opt.assoc, input, inputComps)) {
        rec.failureReason = "failed to fetch input array";
        return rec;
    }

    CAEMultiScaleResultMeta meta;
    if (!runMultiScale(facade, opt, datasetId, field.name, meta,
                       rec.wallAvgMs, rec.wallMinMs, rec.gpuAvgMs, rec.gpuMinMs)) {
        rec.failureReason = "computeMultiScaleDecompositionAndFusion failed";
        return rec;
    }

    std::vector<float> fused;
    int fusedComps = 0;
    if (!facade.getArrayData(datasetId, meta.fusedArrayName, opt.assoc, fused, fusedComps)) {
        rec.failureReason = "failed to fetch fused array";
        return rec;
    }

    rec.success = true;
    rec.baseArrayName = meta.baseArrayName;
    rec.fusedArrayName = meta.fusedArrayName;
    rec.smoothArrayNames = meta.smoothArrayNames;
    rec.detailArrayNames = meta.detailArrayNames;
    rec.inputStd = computeStdDev(input);
    rec.fusedStd = computeStdDev(fused);
    rec.inputRoughness = computeGraphRoughness(input, inputComps, offsets, neighbors);
    rec.fusedRoughness = computeGraphRoughness(fused, fusedComps, offsets, neighbors);
    rec.inputToFusedMeanAbsDelta = computeMeanAbsDelta(input, fused);
    if (rec.inputRoughness > 1e-12) {
        rec.roughnessRatio = rec.fusedRoughness / rec.inputRoughness;
    }
    return rec;
}

bool writeCsvReport(const std::string& path,
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

    out << "dataset,association,mode,clean_array,input_array,base_array,fused_array,smooth_arrays,detail_arrays,noise,export_success,exported_vtk,components,success,failure_reason,"
           "run_mode,reps,levels,iterations,store_intermediate,spatial_sigma_factor,range_sigma_factor,level_scale,edge_sigma_factor,detail_gain0,detail_gain1,detail_gain2,"
           "sigma_factor,impulse_ratio,impulse_scale,seed,"
           "wall_avg_ms,wall_min_ms,gpu_avg_ms,gpu_min_ms,"
           "has_clean_reference,clean_std,input_std,fused_std,clean_roughness,input_roughness,fused_roughness,input_to_fused_mean_abs_delta,"
           "input_error_samples,input_error_finite,input_error_nonfinite,input_mae,input_rmse,input_max_abs,input_signal_rms,input_nmae,input_nrmse,"
           "fused_error_samples,fused_error_finite,fused_error_nonfinite,fused_mae,fused_rmse,fused_max_abs,fused_signal_rms,fused_nmae,fused_nrmse,"
           "mae_improvement_ratio,rmse_improvement_ratio,roughness_ratio\n";

    for (const auto& rec : records) {
        out << csvEscape(rec.dataset) << ','
            << csvEscape(rec.association) << ','
            << csvEscape(rec.mode) << ','
            << csvEscape(rec.cleanArrayName) << ','
            << csvEscape(rec.inputArrayName) << ','
            << csvEscape(rec.baseArrayName) << ','
            << csvEscape(rec.fusedArrayName) << ','
            << csvEscape(joinStrings(rec.smoothArrayNames, "|")) << ','
            << csvEscape(joinStrings(rec.detailArrayNames, "|")) << ','
            << csvEscape(rec.noiseTag) << ','
            << (rec.exportSuccess ? "1" : "0") << ','
            << csvEscape(rec.exportedVtkPath) << ','
            << rec.components << ','
            << (rec.success ? "1" : "0") << ','
            << csvEscape(rec.failureReason) << ','
            << csvEscape(runModeName(opt.runMode)) << ','
            << opt.reps << ','
            << opt.levels << ','
            << opt.iterations << ','
            << (opt.storeIntermediate ? "1" : "0") << ','
            << opt.spatialSigmaFactor << ','
            << opt.rangeSigmaFactor << ','
            << opt.levelScale << ','
            << opt.edgeSigmaFactor << ','
            << opt.detailGain0 << ','
            << opt.detailGain1 << ','
            << opt.detailGain2 << ','
            << opt.sigmaFactor << ','
            << opt.impulseRatio << ','
            << opt.impulseScale << ','
            << opt.seed << ','
            << rec.wallAvgMs << ','
            << rec.wallMinMs << ','
            << rec.gpuAvgMs << ','
            << rec.gpuMinMs << ','
            << (rec.hasCleanReference ? "1" : "0") << ','
            << rec.cleanStd << ','
            << rec.inputStd << ','
            << rec.fusedStd << ','
            << rec.cleanRoughness << ','
            << rec.inputRoughness << ','
            << rec.fusedRoughness << ','
            << rec.inputToFusedMeanAbsDelta << ','
            << rec.inputError.sampleCount << ','
            << rec.inputError.finiteCount << ','
            << rec.inputError.nonFiniteCount << ','
            << rec.inputError.mae << ','
            << rec.inputError.rmse << ','
            << rec.inputError.maxAbs << ','
            << rec.inputError.signalRms << ','
            << rec.inputError.nmae << ','
            << rec.inputError.nrmse << ','
            << rec.fusedError.sampleCount << ','
            << rec.fusedError.finiteCount << ','
            << rec.fusedError.nonFiniteCount << ','
            << rec.fusedError.mae << ','
            << rec.fusedError.rmse << ','
            << rec.fusedError.maxAbs << ','
            << rec.fusedError.signalRms << ','
            << rec.fusedError.nmae << ','
            << rec.fusedError.nrmse << ','
            << rec.maeImprovementRatio << ','
            << rec.rmseImprovementRatio << ','
            << rec.roughnessRatio
            << '\n';
    }

    return true;
}

void printCaseSummary(const CaseRecord& rec)
{
    if (!rec.success) {
        std::cout << "CaseFailed input=" << rec.inputArrayName
                  << " reason=" << rec.failureReason
                  << std::endl;
        return;
    }

    std::cout << "Case=" << rec.inputArrayName
              << " fused=" << rec.fusedArrayName
              << " wall_avg_ms=" << rec.wallAvgMs
              << " gpu_avg_ms=" << rec.gpuAvgMs
              << std::endl;
    if (!rec.baseArrayName.empty()) {
        std::cout << "  Base=" << rec.baseArrayName << std::endl;
    }
    if (!rec.smoothArrayNames.empty()) {
        std::cout << "  Smooth=" << joinStrings(rec.smoothArrayNames, ",") << std::endl;
    }
    if (!rec.detailArrayNames.empty()) {
        std::cout << "  Detail=" << joinStrings(rec.detailArrayNames, ",") << std::endl;
    }
    std::cout << "  Std clean=" << rec.cleanStd
              << " input=" << rec.inputStd
              << " fused=" << rec.fusedStd
              << std::endl;
    std::cout << "  Roughness clean=" << rec.cleanRoughness
              << " input=" << rec.inputRoughness
              << " fused=" << rec.fusedRoughness
              << " fused/input=" << rec.roughnessRatio
              << std::endl;
    if (rec.hasCleanReference) {
        std::cout << "  InputErr mae=" << rec.inputError.mae
                  << " rmse=" << rec.inputError.rmse
                  << " nrmse=" << rec.inputError.nrmse
                  << std::endl;
        std::cout << "  FusedErr mae=" << rec.fusedError.mae
                  << " rmse=" << rec.fusedError.rmse
                  << " nrmse=" << rec.fusedError.nrmse
                  << std::endl;
        std::cout << "  Improvement mae_ratio=" << rec.maeImprovementRatio
                  << " rmse_ratio=" << rec.rmseImprovementRatio
                  << std::endl;
    } else {
        std::cout << "  InputToFusedMeanAbsDelta=" << rec.inputToFusedMeanAbsDelta << std::endl;
    }
    if (!rec.exportedVtkPath.empty()) {
        std::cout << "  ExportVtk=" << rec.exportedVtkPath
                  << " success=" << (rec.exportSuccess ? "YES" : "NO")
                  << std::endl;
    }
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

    const std::string datasetId = facade.loadDatasetFromVTKFile(opt.path);
    if (datasetId.empty()) {
        std::cerr << "load dataset failed: " << opt.path << std::endl;
        return 2;
    }

    CAEDatasetSummary summary;
    if (!facade.getDatasetSummary(datasetId, summary)) {
        std::cerr << "dataset summary failed" << std::endl;
        return 3;
    }

    vtkSmartPointer<vtkDataSet> vtkDataset;
    if (!facade.exportDatasetToVTK(datasetId, vtkDataset)) {
        std::cerr << "exportDatasetToVTK failed" << std::endl;
        return 4;
    }

    DataObject data;
    if (!buildDataObjectFromVtk(vtkDataset, data)) {
        std::cerr << "convert VTK to internal failed" << std::endl;
        return 5;
    }

    std::vector<float> positions;
    std::vector<int> offsets;
    std::vector<int> neighbors;
    if (!buildAssociationGraph(data, opt.assoc, positions, offsets, neighbors)) {
        std::cerr << "build association graph failed" << std::endl;
        return 6;
    }

    std::vector<NamedArray> syntheticFields = buildSyntheticScalarFields(positions);
    std::vector<CAEFieldInfo> fields;
    if (!facade.listFields(datasetId, opt.assoc, fields)) {
        std::cerr << "listFields failed" << std::endl;
        return 7;
    }
    std::sort(fields.begin(), fields.end(), [](const CAEFieldInfo& a, const CAEFieldInfo& b) {
        return a.name < b.name;
    });

    std::cout << "Dataset=" << summary.displayName
              << " points=" << summary.pointCount
              << " cells=" << summary.cellCount
              << " association=" << assocName(opt.assoc)
              << " run=" << runModeName(opt.runMode)
              << std::endl;

    if (opt.listFields) {
        printFieldList(fields, opt.assoc);
        return 0;
    }
    if (opt.listSynthetic) {
        printSyntheticList(syntheticFields);
        return 0;
    }

    std::vector<CaseRecord> records;
    bool anyFailure = false;

    if (opt.runMode == RunMode::Synthetic) {
        std::vector<NamedArray> selected;
        for (const auto& field : syntheticFields) {
            if (!opt.arrayName.empty() && field.name != opt.arrayName) {
                continue;
            }
            if (!opt.nameFilter.empty() && !containsCaseInsensitive(field.name, opt.nameFilter)) {
                continue;
            }
            selected.push_back(field);
        }
        if (selected.empty()) {
            std::cerr << "no synthetic fields selected" << std::endl;
            return 8;
        }

        const std::vector<NoiseKind> noiseKinds = resolveNoiseKinds(opt);
        const size_t totalCases = selected.size() * noiseKinds.size();
        std::uint32_t seedBase = static_cast<std::uint32_t>(opt.seed);
        for (size_t i = 0; i < selected.size(); ++i) {
            for (size_t j = 0; j < noiseKinds.size(); ++j) {
                CaseRecord rec = runSyntheticCase(
                    facade,
                    datasetId,
                    summary,
                    opt,
                    offsets,
                    neighbors,
                    selected[i],
                    noiseKinds[j],
                    seedBase + static_cast<std::uint32_t>(i * 37 + j * 101));
                const size_t caseIndex = i * noiseKinds.size() + j;
                if (rec.success && !opt.exportPath.empty()) {
                    rec.exportedVtkPath = resolveCaseExportPath(opt, rec, caseIndex, totalCases).string();
                    rec.exportSuccess = exportCaseDataset(opt, rec, caseIndex, totalCases, facade, datasetId);
                    if (!rec.exportSuccess) {
                        anyFailure = true;
                        if (!rec.failureReason.empty()) {
                            rec.failureReason += "; ";
                        }
                        rec.failureReason += "VTK export failed";
                    }
                }
                if (!rec.success) {
                    anyFailure = true;
                }
                printCaseSummary(rec);
                records.push_back(rec);
            }
        }
    } else {
        std::vector<CAEFieldInfo> selected;
        for (const auto& field : fields) {
            if (shouldKeepField(field, opt)) {
                selected.push_back(field);
            }
        }
        if (selected.empty()) {
            std::cerr << "no real fields selected" << std::endl;
            return 9;
        }

        const size_t totalCases = selected.size();
        for (size_t i = 0; i < selected.size(); ++i) {
            CaseRecord rec = runFieldCase(
                facade,
                datasetId,
                summary,
                opt,
                offsets,
                neighbors,
                selected[i]);
            if (rec.success && !opt.exportPath.empty()) {
                rec.exportedVtkPath = resolveCaseExportPath(opt, rec, i, totalCases).string();
                rec.exportSuccess = exportCaseDataset(opt, rec, i, totalCases, facade, datasetId);
                if (!rec.exportSuccess) {
                    anyFailure = true;
                    if (!rec.failureReason.empty()) {
                        rec.failureReason += "; ";
                    }
                    rec.failureReason += "VTK export failed";
                }
            }
            if (!rec.success) {
                anyFailure = true;
            }
            printCaseSummary(rec);
            records.push_back(rec);
        }
    }

    if (!opt.csvPath.empty()) {
        if (writeCsvReport(opt.csvPath, opt, records)) {
            std::cout << "CSVReport=" << opt.csvPath << std::endl;
        } else {
            std::cout << "CSVReportFailed=" << opt.csvPath << std::endl;
            anyFailure = true;
        }
    }

    size_t successCount = 0;
    std::vector<double> rmseRatios;
    for (const auto& rec : records) {
        successCount += rec.success ? 1u : 0u;
        if (rec.hasCleanReference && rec.rmseImprovementRatio > 0.0) {
            rmseRatios.push_back(rec.rmseImprovementRatio);
        }
    }

    std::cout << "Summary success=" << successCount
              << " failed=" << (records.size() - successCount)
              << " total=" << records.size()
              << std::endl;
    if (!rmseRatios.empty()) {
        const double meanRatio = std::accumulate(rmseRatios.begin(), rmseRatios.end(), 0.0) /
            static_cast<double>(rmseRatios.size());
        std::cout << "SummarySynthetic mean_rmse_ratio=" << meanRatio << std::endl;
    }

    return anyFailure ? 10 : 0;
}
