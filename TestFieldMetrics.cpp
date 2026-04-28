#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CAEProcessingFacade.h"
#include "TestHarnessUtils.h"

namespace
{
using namespace testharness;

struct ArraySpec
{
    std::string datasetPath;
    std::string arrayName;
};

struct SeriesRequest
{
    std::string label;
    ArraySpec spec;
};

struct Options
{
    // QtCreator 直接运行时，优先修改这一组默认配置。
    //
    // 当前默认配置对应你最近在做的插值误差实验：
    // - 参考真值：Data\u_interp_ms_fused.vtk 中的 u_exact（POINT）
    // - 输入场：Data\u_interp.vtk 中的 u_exact（POINT）
    // - 优化结果：Data\u_interp_ms_fused.vtk 中的 u_exact_ms_fused_P（POINT）
    //
    // 如果后面切到别的实验，只需要改下面这些字符串，不必改 main 或命令行。
    std::string file = "dux_num_ms_fused_P";
    std::string defaultDatasetPath = "Data\\" + file + ".vtk";
    CAEFieldAssociation assoc = CAEFieldAssociation::Point;
    ArraySpec reference{ "Data\\dux_exact.vtk", "dux_exact" };
    std::vector<SeriesRequest> series{
        { "input", { "Data\\dux_num.vtk", "dux_num" } },
        { "fused", { defaultDatasetPath, "dux_num_ms_fused_P" } }
    };
    std::string baselineLabel = "input";
    std::string csvPath = "results\\interp_field_metrics.csv";
    bool listFields = false;
    bool showConfig = false;
    bool strictGeometry = true;
    double positionTolerance = 1e-6;
};

struct ValueStats
{
    bool available = false;
    size_t sampleCount = 0;
    size_t finiteCount = 0;
    size_t nonFiniteCount = 0;
    double mean = 0.0;
    double stddev = 0.0;
    double rms = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
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
    bool correlationAvailable = false;
    double correlation = 0.0;
};

struct GraphCache
{
    bool ready = false;
    std::vector<float> positions;
    std::vector<int> offsets;
    std::vector<int> neighbors;
};

struct LoadedDataset
{
    std::string path;
    std::string datasetId;
    CAEProcessingFacade facade;
    vtkSmartPointer<vtkDataSet> vtkData;
    DataObject dataObject;
    GraphCache pointGraph;
    GraphCache cellGraph;
};

struct ReferenceInfo
{
    std::string datasetPath;
    std::string arrayName;
    int components = 0;
    size_t tupleCount = 0;
    ValueStats valueStats;
    double roughness = 0.0;
    std::vector<float> data;
    std::vector<float> positions;
    std::vector<int> offsets;
    std::vector<int> neighbors;
};

struct SeriesEvaluation
{
    std::string label;
    std::string datasetPath;
    std::string arrayName;
    bool success = false;
    std::string failureReason;
    int components = 0;
    size_t tupleCount = 0;
    bool geometryMatch = true;
    double maxPositionDelta = 0.0;
    ValueStats valueStats;
    double roughness = 0.0;
    ErrorMetrics error;
    bool baselineRatiosAvailable = false;
    double maeRatioVsBaseline = 0.0;
    double rmseRatioVsBaseline = 0.0;
    double roughnessRatioVsBaseline = 0.0;
    double maeReductionPct = 0.0;
    double rmseReductionPct = 0.0;
    double roughnessReductionPct = 0.0;
};

const char* assocName(CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point ? "POINT" : "CELL";
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

bool isOptionToken(const std::string& token)
{
    return startsWith(token, "--") || token.find('=') != std::string::npos;
}

bool parseDoubleOption(const std::string& value, double& out)
{
    char* endPtr = nullptr;
    const double parsed = std::strtod(value.c_str(), &endPtr);
    if (!endPtr || *endPtr != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

void printHelp()
{
    std::cout
        << "Usage:\n"
        << "  opengldp_field_metrics [dataset] [point|cell] [options]\n\n"
        << "Options:\n"
        << "  --dataset=<path>                 default dataset for array specs without an explicit path\n"
        << "  --assoc=point|cell              field association\n"
        << "  --reference=<spec>              required reference field, spec = <array> or <dataset>::<array>\n"
        << "  --input=<spec>                  add a series named input\n"
        << "  --fused=<spec>                  add a series named fused\n"
        << "  --series=<label>=<spec>         add one comparison series; can be repeated\n"
        << "  --baseline-label=<label>        series label used to compute improvement ratios\n"
        << "  --csv=<path>                    output CSV path\n"
        << "  --list-fields                   list fields of the default dataset and exit\n"
        << "  --show-config                   print parsed configuration\n"
        << "  --strict-geometry=on|off        require sample coordinates to match the reference\n"
        << "  --position-tol=<x>              coordinate tolerance used by geometry check\n\n"
        << "Spec format:\n"
        << "  <array>\n"
        << "  <dataset-path>::<array>\n\n"
        << "Examples:\n"
        << "  opengldp_field_metrics Data\\interp_case.vtk point --reference=u_exact --input=u_interp --fused=u_interp_ms_fused --baseline-label=input --csv=results\\interp_eval.csv\n"
        << "  opengldp_field_metrics --assoc=point --reference=Data\\u_exact.vtk::u_exact --input=Data\\u_interp.vtk::u_interp --fused=Data\\u_interp_ms_fused.vtk::u_interp_ms_fused --baseline-label=input\n";
}

bool parseArraySpec(const std::string& text,
                    const std::string& defaultDatasetPath,
                    ArraySpec& out)
{
    const size_t sep = text.find("::");
    if (sep == std::string::npos) {
        if (defaultDatasetPath.empty() || text.empty()) {
            return false;
        }
        out.datasetPath = defaultDatasetPath;
        out.arrayName = text;
        return true;
    }

    out.datasetPath = text.substr(0, sep);
    out.arrayName = text.substr(sep + 2);
    return !out.datasetPath.empty() && !out.arrayName.empty();
}

bool parseSeriesRequest(const std::string& value,
                        const std::string& defaultDatasetPath,
                        SeriesRequest& out)
{
    const size_t eq = value.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= value.size()) {
        return false;
    }

    out.label = value.substr(0, eq);
    return parseArraySpec(value.substr(eq + 1), defaultDatasetPath, out.spec);
}

void upsertSeries(std::vector<SeriesRequest>& series, const SeriesRequest& request)
{
    for (auto& item : series) {
        if (item.label == request.label) {
            item = request;
            return;
        }
    }
    series.push_back(request);
}

void remapDefaultDatasetPath(Options& opt, const std::string& oldPath, const std::string& newPath)
{
    if (oldPath == newPath) {
        return;
    }

    if (opt.reference.datasetPath == oldPath) {
        opt.reference.datasetPath = newPath;
    }
    for (auto& series : opt.series) {
        if (series.spec.datasetPath == oldPath) {
            series.spec.datasetPath = newPath;
        }
    }
    opt.defaultDatasetPath = newPath;
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
        opt.defaultDatasetPath = positional[0];
    }
    if (positional.size() >= 2) {
        if (!parseAssociation(positional[1], opt.assoc)) {
            std::cerr << "invalid association: " << positional[1] << std::endl;
            return false;
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!startsWith(arg, "--")) {
            continue;
        }

        auto parseValueOption = [&](const std::string& key, std::string& out) -> bool {
            const std::string prefix = "--" + key + "=";
            if (!startsWith(arg, prefix)) {
                return false;
            }
            out = arg.substr(prefix.size());
            return true;
        };

        std::string value;
        if (parseValueOption("dataset", value)) {
            remapDefaultDatasetPath(opt, opt.defaultDatasetPath, value);
            continue;
        }
        if (parseValueOption("assoc", value)) {
            if (!parseAssociation(value, opt.assoc)) {
                std::cerr << "invalid --assoc value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("reference", value)) {
            if (!parseArraySpec(value, opt.defaultDatasetPath, opt.reference)) {
                std::cerr << "invalid --reference spec: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("input", value)) {
            SeriesRequest req;
            req.label = "input";
            if (!parseArraySpec(value, opt.defaultDatasetPath, req.spec)) {
                std::cerr << "invalid --input spec: " << value << std::endl;
                return false;
            }
            upsertSeries(opt.series, req);
            continue;
        }
        if (parseValueOption("fused", value)) {
            SeriesRequest req;
            req.label = "fused";
            if (!parseArraySpec(value, opt.defaultDatasetPath, req.spec)) {
                std::cerr << "invalid --fused spec: " << value << std::endl;
                return false;
            }
            upsertSeries(opt.series, req);
            continue;
        }
        if (parseValueOption("series", value)) {
            SeriesRequest req;
            if (!parseSeriesRequest(value, opt.defaultDatasetPath, req)) {
                std::cerr << "invalid --series spec: " << value << std::endl;
                return false;
            }
            upsertSeries(opt.series, req);
            continue;
        }
        if (parseValueOption("baseline-label", value)) {
            opt.baselineLabel = value;
            continue;
        }
        if (parseValueOption("csv", value)) {
            opt.csvPath = value;
            continue;
        }
        if (parseValueOption("position-tol", value)) {
            if (!parseDoubleOption(value, opt.positionTolerance) || opt.positionTolerance < 0.0) {
                std::cerr << "invalid --position-tol value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("strict-geometry", value)) {
            if (!parseBoolSwitch(value, opt.strictGeometry)) {
                std::cerr << "invalid --strict-geometry value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (arg == "--list-fields") {
            opt.listFields = true;
            continue;
        }
        if (arg == "--show-config") {
            opt.showConfig = true;
            continue;
        }

        std::cerr << "unknown option: " << arg << std::endl;
        return false;
    }

    if (opt.listFields) {
        return !opt.defaultDatasetPath.empty();
    }

    if (opt.reference.arrayName.empty() || opt.reference.datasetPath.empty()) {
        std::cerr << "--reference is required" << std::endl;
        return false;
    }
    if (opt.series.empty()) {
        std::cerr << "at least one series is required; use --input, --fused, or --series" << std::endl;
        return false;
    }

    std::set<std::string> labels;
    for (const auto& series : opt.series) {
        if (!labels.insert(series.label).second) {
            std::cerr << "duplicate series label: " << series.label << std::endl;
            return false;
        }
    }

    if (opt.baselineLabel.empty()) {
        opt.baselineLabel = opt.series.front().label;
    }

    return true;
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

bool loadDatasetCached(const std::string& path,
                       std::unordered_map<std::string, std::unique_ptr<LoadedDataset>>& cache,
                       LoadedDataset*& out,
                       std::string& error)
{
    auto it = cache.find(path);
    if (it != cache.end()) {
        out = it->second.get();
        return true;
    }

    auto dataset = std::make_unique<LoadedDataset>();
    dataset->path = path;
    dataset->datasetId = dataset->facade.loadDatasetFromVTKFile(path);
    if (dataset->datasetId.empty()) {
        error = "failed to load VTK dataset: " + path;
        return false;
    }

    if (!dataset->facade.exportDatasetToVTK(dataset->datasetId, dataset->vtkData) || !dataset->vtkData) {
        error = "failed to export internal VTK dataset: " + path;
        return false;
    }

    if (!buildDataObjectFromVtk(dataset->vtkData, dataset->dataObject)) {
        error = "failed to build internal DataObject: " + path;
        return false;
    }

    out = dataset.get();
    cache.emplace(path, std::move(dataset));
    return true;
}

bool ensureGraphCache(LoadedDataset& dataset,
                      CAEFieldAssociation assoc,
                      GraphCache*& out,
                      std::string& error)
{
    GraphCache* cache = (assoc == CAEFieldAssociation::Point)
        ? &dataset.pointGraph
        : &dataset.cellGraph;
    if (!cache->ready) {
        if (!buildAssociationGraph(dataset.dataObject, assoc, cache->positions, cache->offsets, cache->neighbors)) {
            error = "failed to build association graph for dataset: " + dataset.path;
            return false;
        }
        cache->ready = true;
    }
    out = cache;
    return true;
}

bool getSeriesData(LoadedDataset& dataset,
                   const ArraySpec& spec,
                   CAEFieldAssociation assoc,
                   std::vector<float>& outData,
                   int& outComps,
                   std::string& error)
{
    if (!dataset.facade.getArrayData(dataset.datasetId, spec.arrayName, assoc, outData, outComps)) {
        error = "failed to read array '" + spec.arrayName + "' from dataset: " + spec.datasetPath;
        return false;
    }
    if (outData.empty() || outComps <= 0) {
        error = "array is empty or invalid: " + spec.arrayName;
        return false;
    }
    return true;
}

ValueStats computeValueStats(const std::vector<float>& values)
{
    ValueStats out;
    out.available = !values.empty();
    out.sampleCount = values.size();
    if (!out.available) {
        return out;
    }

    double sum = 0.0;
    double sumSq = 0.0;
    out.minValue = std::numeric_limits<double>::infinity();
    out.maxValue = -std::numeric_limits<double>::infinity();

    for (float value : values) {
        const double v = static_cast<double>(value);
        if (!std::isfinite(v)) {
            ++out.nonFiniteCount;
            continue;
        }
        ++out.finiteCount;
        sum += v;
        sumSq += v * v;
        out.minValue = std::min(out.minValue, v);
        out.maxValue = std::max(out.maxValue, v);
    }

    if (out.finiteCount == 0) {
        out.available = false;
        out.minValue = 0.0;
        out.maxValue = 0.0;
        return out;
    }

    const double invCount = 1.0 / static_cast<double>(out.finiteCount);
    out.mean = sum * invCount;
    out.rms = std::sqrt(sumSq * invCount);

    double var = 0.0;
    for (float value : values) {
        const double v = static_cast<double>(value);
        if (!std::isfinite(v)) {
            continue;
        }
        const double d = v - out.mean;
        var += d * d;
    }
    out.stddev = std::sqrt(var * invCount);
    return out;
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
    double valueSum = 0.0;
    double refSum = 0.0;

    for (size_t i = 0; i < values.size(); ++i) {
        const double v = static_cast<double>(values[i]);
        const double r = static_cast<double>(reference[i]);
        if (!std::isfinite(v) || !std::isfinite(r)) {
            ++out.nonFiniteCount;
            continue;
        }

        ++out.finiteCount;
        valueSum += v;
        refSum += r;

        const double e = std::abs(v - r);
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

    const double meanValue = valueSum / denom;
    const double meanRef = refSum / denom;
    double cov = 0.0;
    double varValue = 0.0;
    double varRef = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        const double v = static_cast<double>(values[i]);
        const double r = static_cast<double>(reference[i]);
        if (!std::isfinite(v) || !std::isfinite(r)) {
            continue;
        }
        const double dv = v - meanValue;
        const double dr = r - meanRef;
        cov += dv * dr;
        varValue += dv * dv;
        varRef += dr * dr;
    }

    if (varValue > 1e-12 && varRef > 1e-12) {
        out.correlationAvailable = true;
        out.correlation = cov / std::sqrt(varValue * varRef);
    }
    return out;
}

double computeReductionPercent(double ratio)
{
    return (1.0 - ratio) * 100.0;
}

bool comparePositions(const std::vector<float>& refPositions,
                      const std::vector<float>& otherPositions,
                      double tolerance,
                      double& maxAbsDelta)
{
    maxAbsDelta = 0.0;
    if (refPositions.size() != otherPositions.size()) {
        return false;
    }

    for (size_t i = 0; i < refPositions.size(); ++i) {
        const double delta = std::abs(static_cast<double>(refPositions[i]) -
                                      static_cast<double>(otherPositions[i]));
        maxAbsDelta = std::max(maxAbsDelta, delta);
    }
    return maxAbsDelta <= tolerance;
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

void printConfig(const Options& opt)
{
    std::cout << "Config"
              << " dataset=" << opt.defaultDatasetPath
              << " assoc=" << assocName(opt.assoc)
              << " reference=" << opt.reference.datasetPath << "::" << opt.reference.arrayName
              << " baselineLabel=" << opt.baselineLabel
              << " strictGeometry=" << (opt.strictGeometry ? "ON" : "OFF")
              << " positionTol=" << opt.positionTolerance
              << " csv=" << opt.csvPath
              << std::endl;
    for (const auto& series : opt.series) {
        std::cout << "  Series label=" << series.label
                  << " spec=" << series.spec.datasetPath << "::" << series.spec.arrayName
                  << std::endl;
    }
}

bool writeCsvReport(const std::string& path,
                    CAEFieldAssociation assoc,
                    const ReferenceInfo& ref,
                    const std::string& baselineLabel,
                    const std::vector<SeriesEvaluation>& evals)
{
    const std::filesystem::path csvPath(path);
    if (!ensureParentDirectory(csvPath)) {
        return false;
    }

    std::ofstream out(csvPath);
    if (!out) {
        return false;
    }

    out << "reference_dataset,reference_array,baseline_label,label,dataset,array,association,components,tuples,success,failure_reason,"
           "geometry_match,max_position_delta,finite_samples,nonfinite_samples,"
           "value_mean,value_std,value_rms,value_min,value_max,roughness,"
           "mae,rmse,max_abs,nmae,nrmse,correlation,"
           "mae_ratio_vs_baseline,rmse_ratio_vs_baseline,roughness_ratio_vs_baseline,"
           "mae_reduction_pct,rmse_reduction_pct,roughness_reduction_pct\n";

    out << std::setprecision(10);
    for (const auto& eval : evals) {
        out << csvEscape(ref.datasetPath) << ','
            << csvEscape(ref.arrayName) << ','
            << csvEscape(baselineLabel) << ','
            << csvEscape(eval.label) << ','
            << csvEscape(eval.datasetPath) << ','
            << csvEscape(eval.arrayName) << ','
            << assocName(assoc) << ',';

        if (eval.success) {
            out << eval.components << ','
                << eval.tupleCount << ','
                << "1,"
                << "\"\","
                << (eval.geometryMatch ? "1" : "0") << ','
                << eval.maxPositionDelta << ','
                << eval.valueStats.finiteCount << ','
                << eval.valueStats.nonFiniteCount << ','
                << eval.valueStats.mean << ','
                << eval.valueStats.stddev << ','
                << eval.valueStats.rms << ','
                << eval.valueStats.minValue << ','
                << eval.valueStats.maxValue << ','
                << eval.roughness << ','
                << eval.error.mae << ','
                << eval.error.rmse << ','
                << eval.error.maxAbs << ','
                << eval.error.nmae << ','
                << eval.error.nrmse << ','
                << (eval.error.correlationAvailable ? eval.error.correlation : 0.0) << ','
                << (eval.baselineRatiosAvailable ? eval.maeRatioVsBaseline : 0.0) << ','
                << (eval.baselineRatiosAvailable ? eval.rmseRatioVsBaseline : 0.0) << ','
                << (eval.baselineRatiosAvailable ? eval.roughnessRatioVsBaseline : 0.0) << ','
                << (eval.baselineRatiosAvailable ? eval.maeReductionPct : 0.0) << ','
                << (eval.baselineRatiosAvailable ? eval.rmseReductionPct : 0.0) << ','
                << (eval.baselineRatiosAvailable ? eval.roughnessReductionPct : 0.0)
                << '\n';
        } else {
            out << "0,"
                << "0,"
                << "0,"
                << csvEscape(eval.failureReason) << ','
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0,"
                << "0\n";
        }
    }

    return true;
}

bool resolveReference(const Options& opt,
                      std::unordered_map<std::string, std::unique_ptr<LoadedDataset>>& cache,
                      ReferenceInfo& outRef)
{
    LoadedDataset* dataset = nullptr;
    std::string error;
    if (!loadDatasetCached(opt.reference.datasetPath, cache, dataset, error)) {
        std::cerr << error << std::endl;
        return false;
    }

    GraphCache* graph = nullptr;
    if (!ensureGraphCache(*dataset, opt.assoc, graph, error)) {
        std::cerr << error << std::endl;
        return false;
    }

    std::vector<float> data;
    int comps = 0;
    if (!getSeriesData(*dataset, opt.reference, opt.assoc, data, comps, error)) {
        std::cerr << error << std::endl;
        return false;
    }

    outRef.datasetPath = opt.reference.datasetPath;
    outRef.arrayName = opt.reference.arrayName;
    outRef.components = comps;
    outRef.tupleCount = data.size() / static_cast<size_t>(comps);
    outRef.valueStats = computeValueStats(data);
    outRef.roughness = computeGraphRoughness(data, comps, graph->offsets, graph->neighbors);
    outRef.data = std::move(data);
    outRef.positions = graph->positions;
    outRef.offsets = graph->offsets;
    outRef.neighbors = graph->neighbors;
    return true;
}

SeriesEvaluation evaluateSeries(const SeriesRequest& request,
                                const Options& opt,
                                const ReferenceInfo& ref,
                                std::unordered_map<std::string, std::unique_ptr<LoadedDataset>>& cache)
{
    SeriesEvaluation eval;
    eval.label = request.label;
    eval.datasetPath = request.spec.datasetPath;
    eval.arrayName = request.spec.arrayName;

    LoadedDataset* dataset = nullptr;
    std::string error;
    if (!loadDatasetCached(request.spec.datasetPath, cache, dataset, error)) {
        eval.failureReason = error;
        return eval;
    }

    GraphCache* graph = nullptr;
    if (!ensureGraphCache(*dataset, opt.assoc, graph, error)) {
        eval.failureReason = error;
        return eval;
    }

    std::vector<float> values;
    int comps = 0;
    if (!getSeriesData(*dataset, request.spec, opt.assoc, values, comps, error)) {
        eval.failureReason = error;
        return eval;
    }

    eval.components = comps;
    eval.tupleCount = values.size() / static_cast<size_t>(comps);

    if (comps != ref.components) {
        eval.failureReason = "component count mismatch with reference";
        return eval;
    }
    if (values.size() != ref.data.size()) {
        eval.failureReason = "sample count mismatch with reference";
        return eval;
    }

    eval.geometryMatch = comparePositions(ref.positions, graph->positions, opt.positionTolerance, eval.maxPositionDelta);
    if (!eval.geometryMatch && opt.strictGeometry) {
        eval.failureReason = "sample coordinates differ from reference";
        return eval;
    }

    eval.valueStats = computeValueStats(values);
    eval.roughness = computeGraphRoughness(values, comps, graph->offsets, graph->neighbors);
    eval.error = computeErrorMetrics(values, ref.data);
    eval.success = eval.valueStats.available && eval.error.available;
    if (!eval.success && eval.failureReason.empty()) {
        eval.failureReason = "failed to compute metrics";
    }
    return eval;
}

void applyBaselineRatios(const std::string& baselineLabel,
                         std::vector<SeriesEvaluation>& evals)
{
    const SeriesEvaluation* baseline = nullptr;
    for (const auto& eval : evals) {
        if (eval.success && eval.label == baselineLabel) {
            baseline = &eval;
            break;
        }
    }
    if (!baseline) {
        return;
    }

    for (auto& eval : evals) {
        if (!eval.success) {
            continue;
        }
        eval.baselineRatiosAvailable = baseline->error.available;

        if (baseline->error.mae > 1e-12) {
            eval.maeRatioVsBaseline = eval.error.mae / baseline->error.mae;
            eval.maeReductionPct = computeReductionPercent(eval.maeRatioVsBaseline);
        }
        if (baseline->error.rmse > 1e-12) {
            eval.rmseRatioVsBaseline = eval.error.rmse / baseline->error.rmse;
            eval.rmseReductionPct = computeReductionPercent(eval.rmseRatioVsBaseline);
        }
        if (baseline->roughness > 1e-12) {
            eval.roughnessRatioVsBaseline = eval.roughness / baseline->roughness;
            eval.roughnessReductionPct = computeReductionPercent(eval.roughnessRatioVsBaseline);
        }
    }
}

void printReferenceSummary(const ReferenceInfo& ref)
{
    std::cout << "Reference"
              << " dataset=" << ref.datasetPath
              << " array=" << ref.arrayName
              << " comps=" << ref.components
              << " tuples=" << ref.tupleCount
              << std::endl;
    std::cout << "  Stats mean=" << ref.valueStats.mean
              << " std=" << ref.valueStats.stddev
              << " rms=" << ref.valueStats.rms
              << " min=" << ref.valueStats.minValue
              << " max=" << ref.valueStats.maxValue
              << " roughness=" << ref.roughness
              << std::endl;
}

void printSeriesSummary(const std::string& baselineLabel,
                        const std::vector<SeriesEvaluation>& evals)
{
    std::cout << "BaselineLabel=" << baselineLabel << std::endl;
    for (const auto& eval : evals) {
        if (!eval.success) {
            std::cout << "SeriesFailed"
                      << " label=" << eval.label
                      << " array=" << eval.arrayName
                      << " reason=" << eval.failureReason
                      << std::endl;
            continue;
        }

        std::cout << "Series"
                  << " label=" << eval.label
                  << " dataset=" << eval.datasetPath
                  << " array=" << eval.arrayName
                  << " comps=" << eval.components
                  << " tuples=" << eval.tupleCount
                  << std::endl;
        std::cout << "  Value mean=" << eval.valueStats.mean
                  << " std=" << eval.valueStats.stddev
                  << " rms=" << eval.valueStats.rms
                  << " min=" << eval.valueStats.minValue
                  << " max=" << eval.valueStats.maxValue
                  << std::endl;
        std::cout << "  Error mae=" << eval.error.mae
                  << " rmse=" << eval.error.rmse
                  << " max=" << eval.error.maxAbs
                  << " nmae=" << eval.error.nmae
                  << " nrmse=" << eval.error.nrmse;
        if (eval.error.correlationAvailable) {
            std::cout << " corr=" << eval.error.correlation;
        }
        std::cout << std::endl;
        std::cout << "  Roughness value=" << eval.roughness;
        if (eval.baselineRatiosAvailable) {
            std::cout << " mae/base=" << eval.maeRatioVsBaseline
                      << " rmse/base=" << eval.rmseRatioVsBaseline
                      << " rough/base=" << eval.roughnessRatioVsBaseline
                      << " rmse_reduction_pct=" << eval.rmseReductionPct;
        }
        if (!eval.geometryMatch) {
            std::cout << " geometry_match=NO max_position_delta=" << eval.maxPositionDelta;
        }
        std::cout << std::endl;
    }
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    if (!parseCommandLine(argc, argv, opt)) {
        return 1;
    }

    if (opt.showConfig) {
        printConfig(opt);
    }

    std::unordered_map<std::string, std::unique_ptr<LoadedDataset>> cache;

    if (opt.listFields) {
        LoadedDataset* dataset = nullptr;
        std::string error;
        if (!loadDatasetCached(opt.defaultDatasetPath, cache, dataset, error)) {
            std::cerr << error << std::endl;
            return 1;
        }
        std::vector<CAEFieldInfo> fields;
        if (!dataset->facade.listFields(dataset->datasetId, opt.assoc, fields)) {
            std::cerr << "failed to list fields" << std::endl;
            return 1;
        }
        printFieldList(fields, opt.assoc);
        return 0;
    }

    ReferenceInfo ref;
    if (!resolveReference(opt, cache, ref)) {
        return 1;
    }

    std::vector<SeriesEvaluation> evals;
    evals.reserve(opt.series.size());
    for (const auto& series : opt.series) {
        evals.push_back(evaluateSeries(series, opt, ref, cache));
    }

    applyBaselineRatios(opt.baselineLabel, evals);
    printReferenceSummary(ref);
    printSeriesSummary(opt.baselineLabel, evals);

    if (!writeCsvReport(opt.csvPath, opt.assoc, ref, opt.baselineLabel, evals)) {
        std::cerr << "failed to write CSV: " << opt.csvPath << std::endl;
        return 1;
    }

    std::cout << "CSV=" << opt.csvPath << std::endl;
    return 0;
}
