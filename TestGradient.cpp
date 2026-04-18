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
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "CAEProcessingFacade.h"

namespace
{
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
    std::string filename="ShipHull_0";
    std::string path = "Data\\"+filename+".vtk";
    CAEFieldAssociation assoc = CAEFieldAssociation::Point;
    std::string arrayName;
    int reps = 5;
    bool enableAnalyticBenchmarks = false;
    ReferenceMode referenceMode = ReferenceMode::Auto;
    int maxSamplesToPrint = 20;
    RunMode runMode = RunMode::Fields;
    bool listFields = false;
    bool listBenchmarks = false;
    bool showConfig = false;
    std::string nameFilter;
    std::string csvPath="results\\"+filename+".csv";
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
    size_t vecCount = 0;
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
    size_t lowRefCount = 0;
    double angleMeanDeg = 0.0;
    double angleP90Deg = 0.0;
    size_t angleCount = 0;
    double scaleBias = 0.0;
    double refTimeAvgMs = 0.0;
    double refTimeMinMs = 0.0;
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
    CompareMetrics metrics;
};

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
        value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

const char* assocName(CAEFieldAssociation assoc)
{
    return assoc == CAEFieldAssociation::Point ? "POINT" : "CELL";
}

const char* referenceModeName(ReferenceMode mode)
{
    switch (mode) {
    case ReferenceMode::Analytic:
        return "analytic";
    case ReferenceMode::Vtk:
        return "vtk";
    case ReferenceMode::None:
        return "none";
    default:
        return "auto";
    }
}

const char* runModeName(RunMode mode)
{
    switch (mode) {
    case RunMode::Benchmarks:
        return "benchmarks";
    case RunMode::Fields:
        return "fields";
    default:
        return "single";
    }
}

bool parseBoolSwitch(std::string value, bool& out)
{
    value = toLower(std::move(value));
    if (value == "1" || value == "on" || value == "true" ||
        value == "yes" || value == "enable" || value == "enabled") {
        out = true;
        return true;
    }
    if (value == "0" || value == "off" || value == "false" ||
        value == "no" || value == "disable" || value == "disabled") {
        out = false;
        return true;
    }
    return false;
}

bool parsePositiveInt(const std::string& value, int& out)
{
    char* endPtr = nullptr;
    const long parsed = std::strtol(value.c_str(), &endPtr, 10);
    if (!endPtr || *endPtr != '\0' || parsed < 0 ||
        parsed > static_cast<long>(std::numeric_limits<int>::max())) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
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

bool isOptionToken(const std::string& token)
{
    return startsWith(token, "--") || token.find('=') != std::string::npos;
}

void printHelp()
{
    std::cout
        << "Usage:\n"
        << "  opengldp_benchmark [dataset] [point|cell] [array] [reps] [options]\n\n"
        << "Common options:\n"
        << "  --dataset=<path>            VTK dataset path\n"
        << "  --assoc=point|cell          Array association\n"
        << "  --array=<name>              Input array name for single-case mode\n"
        << "  --reps=<n>                  Benchmark repetitions\n"
        << "  --reference=auto|analytic|vtk|none\n"
        << "  --analytic-bench=on|off     Inject analytic benchmark arrays on load\n"
        << "  --dump=<n>                  Print first n tuples for single-case mode\n"
        << "  --run=single|benchmarks|fields\n"
        << "  --filter=<text>             Keep only array names containing text\n"
        << "  --list-fields               List arrays for the selected association and exit\n"
        << "  --list-benchmarks           List analytic benchmark arrays and exit\n"
        << "  --csv=<path>                Save summary table as CSV\n"
        << "  --show-config               Print resolved runtime configuration\n"
        << "  --help                      Show this message\n\n"
        << "Examples:\n"
        << "  opengldp_benchmark Data\\AngularSector.vtk cell benchmark_quadratic 5 --reference=analytic\n"
        << "  opengldp_benchmark --dataset=Data\\AngularSector.vtk --assoc=cell --run=benchmarks --csv=results\\angular_bench.csv\n"
        << "  opengldp_benchmark --dataset=Data\\ShipHull_0.vtk --assoc=point --run=fields --reference=vtk --filter=stress\n";
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
        if (arg == "--analytic-bench" || arg == "--analytic-benchmark" ||
            arg == "analytic" || arg == "benchmark") {
            opt.enableAnalyticBenchmarks = true;
            continue;
        }
        if (arg == "--no-analytic-bench" || arg == "--no-analytic-benchmark") {
            opt.enableAnalyticBenchmarks = false;
            continue;
        }

        auto parseValueOption = [&](const std::string& key, std::string& out) -> bool {
            if (startsWith(arg, key)) {
                out = arg.substr(key.size());
                return true;
            }
            return false;
        };

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
        if (parseValueOption("--dump=", value) || parseValueOption("--show=", value) ||
            parseValueOption("dump=", value) || parseValueOption("show=", value)) {
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
        if (parseValueOption("--analytic-bench=", value) || parseValueOption("--analytic-benchmark=", value) ||
            parseValueOption("analytic=", value) || parseValueOption("benchmark=", value)) {
            if (!parseBoolSwitch(value, opt.enableAnalyticBenchmarks)) {
                std::cerr << "invalid analytic benchmark switch: " << arg << std::endl;
                return false;
            }
            continue;
        }

        std::cerr << "unknown option: " << arg << std::endl;
        return false;
    }

    if (opt.reps <= 0) {
        opt.reps = 1;
    }
    if ((opt.runMode == RunMode::Benchmarks || opt.listBenchmarks) && !opt.enableAnalyticBenchmarks) {
        opt.enableAnalyticBenchmarks = true;
    }
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

std::string csvEscape(const std::string& value)
{
    const bool needsQuotes = value.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    const double t = pos - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}

ReferenceData buildAnalyticReference(CAEProcessingFacade& facade,
                                     const std::string& datasetId,
                                     const std::string& arrayName,
                                     CAEFieldAssociation assoc)
{
    ReferenceData ref;
    ref.label = "ANALYTIC";
    const std::string refArrayName = arrayName + "_exact_grad";
    if (facade.getArrayData(datasetId, refArrayName, assoc, ref.values, ref.comps)) {
        ref.available = true;
        ref.analytic = true;
        ref.label = "ANALYTIC:" + refArrayName;
    }
    return ref;
}

ReferenceData buildVtkReference(vtkDataSet* dataset,
                                const std::string& arrayName,
                                CAEFieldAssociation assoc,
                                int reps)
{
    ReferenceData ref;
    ref.label = "VTK";
    if (!dataset) {
        return ref;
    }

    vtkNew<vtkGradientFilter> gf;
    gf->SetResultArrayName("__vtk_grad_ref");
    const int vtkAssoc = (assoc == CAEFieldAssociation::Point)
        ? vtkDataObject::FIELD_ASSOCIATION_POINTS
        : vtkDataObject::FIELD_ASSOCIATION_CELLS;
    gf->SetInputArrayToProcess(0, 0, 0, vtkAssoc, arrayName.c_str());
    gf->SetInputData(dataset);

    double vtkSum = 0.0;
    double vtkMin = std::numeric_limits<double>::max();
    for (int i = 0; i < std::max(reps, 1); ++i) {
        gf->Modified();
        auto t0 = std::chrono::high_resolution_clock::now();
        gf->Update();
        auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        vtkSum += ms;
        vtkMin = std::min(vtkMin, ms);
    }

    vtkDataSet* out = vtkDataSet::SafeDownCast(gf->GetOutput());
    if (!out) {
        return ref;
    }
    vtkDataArray* ga = (assoc == CAEFieldAssociation::Point)
        ? out->GetPointData()->GetArray("__vtk_grad_ref")
        : out->GetCellData()->GetArray("__vtk_grad_ref");
    if (!ga) {
        return ref;
    }

    ref.comps = ga->GetNumberOfComponents();
    const vtkIdType tuples = ga->GetNumberOfTuples();
    ref.values.resize(static_cast<size_t>(tuples) * static_cast<size_t>(ref.comps));
    for (vtkIdType i = 0; i < tuples; ++i) {
        for (int c = 0; c < ref.comps; ++c) {
            ref.values[static_cast<size_t>(i) * static_cast<size_t>(ref.comps) + static_cast<size_t>(c)] =
                static_cast<float>(ga->GetComponent(i, c));
        }
    }

    ref.available = true;
    ref.timeAvgMs = vtkSum / static_cast<double>(std::max(reps, 1));
    ref.timeMinMs = vtkMin;
    return ref;
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

CompareMetrics compareGradients(const std::vector<float>& gradGl,
                                int glComps,
                                const ReferenceData& ref)
{
    CompareMetrics metrics;
    metrics.haveReference = ref.available;
    metrics.referenceLabel = ref.label;
    metrics.refTimeAvgMs = ref.timeAvgMs;
    metrics.refTimeMinMs = ref.timeMinMs;

    if (!ref.available || glComps <= 0 || ref.comps <= 0) {
        return metrics;
    }

    const size_t glTuples = gradGl.size() / static_cast<size_t>(glComps);
    const size_t refTuples = ref.values.size() / static_cast<size_t>(ref.comps);
    const size_t nTuple = std::min(glTuples, refTuples);
    const int nComp = std::min(glComps, ref.comps);
    if (nComp < 3) {
        return metrics;
    }

    const int vecsPerTuple = nComp / 3;
    const int usedComps = vecsPerTuple * 3;
    const size_t vecCount = nTuple * static_cast<size_t>(vecsPerTuple);
    const double vecDenom = static_cast<double>(std::max<size_t>(vecCount, 1));

    double vecMaeAbs = 0.0;
    double vecRmseAccum = 0.0;
    double vecMaxAbs = 0.0;
    double refNormSum = 0.0;
    double refNormSqSum = 0.0;
    double dotRefGl = 0.0;
    std::vector<double> angleValues;
    angleValues.reserve(vecCount);

    for (size_t i = 0; i < nTuple; ++i) {
        for (int v = 0; v < vecsPerTuple; ++v) {
            const size_t glBase = i * static_cast<size_t>(glComps) + static_cast<size_t>(v) * 3u;
            const size_t refBase = i * static_cast<size_t>(ref.comps) + static_cast<size_t>(v) * 3u;

            const double gx = static_cast<double>(gradGl[glBase + 0]);
            const double gy = static_cast<double>(gradGl[glBase + 1]);
            const double gz = static_cast<double>(gradGl[glBase + 2]);
            const double rx = static_cast<double>(ref.values[refBase + 0]);
            const double ry = static_cast<double>(ref.values[refBase + 1]);
            const double rz = static_cast<double>(ref.values[refBase + 2]);

            const double dx = gx - rx;
            const double dy = gy - ry;
            const double dz = gz - rz;
            const double errNorm = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double refNorm = std::sqrt(rx * rx + ry * ry + rz * rz);
            const double glNorm = std::sqrt(gx * gx + gy * gy + gz * gz);

            vecMaeAbs += errNorm;
            vecRmseAccum += errNorm * errNorm;
            vecMaxAbs = std::max(vecMaxAbs, errNorm);
            refNormSum += refNorm;
            refNormSqSum += refNorm * refNorm;
            dotRefGl += gx * rx + gy * ry + gz * rz;

            if (glNorm > 1e-12 && refNorm > 1e-12) {
                double cosTheta = (gx * rx + gy * ry + gz * rz) / (glNorm * refNorm);
                cosTheta = std::clamp(cosTheta, -1.0, 1.0);
                angleValues.push_back(std::acos(cosTheta) * 180.0 / 3.14159265358979323846);
            }
        }
    }

    const double refRms = std::sqrt(refNormSqSum / vecDenom);
    const double tau = std::max(1e-12, 0.05 * refRms);
    std::vector<double> softRelValues;
    softRelValues.reserve(vecCount);
    size_t lowRefCount = 0;
    double softRelSum = 0.0;

    for (size_t i = 0; i < nTuple; ++i) {
        for (int v = 0; v < vecsPerTuple; ++v) {
            const size_t glBase = i * static_cast<size_t>(glComps) + static_cast<size_t>(v) * 3u;
            const size_t refBase = i * static_cast<size_t>(ref.comps) + static_cast<size_t>(v) * 3u;

            const double gx = static_cast<double>(gradGl[glBase + 0]);
            const double gy = static_cast<double>(gradGl[glBase + 1]);
            const double gz = static_cast<double>(gradGl[glBase + 2]);
            const double rx = static_cast<double>(ref.values[refBase + 0]);
            const double ry = static_cast<double>(ref.values[refBase + 1]);
            const double rz = static_cast<double>(ref.values[refBase + 2]);

            const double dx = gx - rx;
            const double dy = gy - ry;
            const double dz = gz - rz;
            const double errNorm = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double refNorm = std::sqrt(rx * rx + ry * ry + rz * rz);
            const double softRel = errNorm / std::max(refNorm, tau);
            softRelValues.push_back(softRel);
            softRelSum += softRel;
            if (refNorm < tau) {
                ++lowRefCount;
            }
        }
    }

    metrics.tupleCount = nTuple;
    metrics.compareComponents = usedComps;
    metrics.vecsPerTuple = vecsPerTuple;
    metrics.vecCount = vecCount;
    metrics.vecMaeAbs = vecMaeAbs / vecDenom;
    metrics.vecRmseAbs = std::sqrt(vecRmseAccum / vecDenom);
    metrics.vecMaxAbs = vecMaxAbs;
    metrics.refScaleRms = refRms;
    metrics.nmaeVec = refNormSum > 1e-12 ? (vecMaeAbs / refNormSum) : 0.0;
    metrics.nrmseVec = refNormSqSum > 1e-12 ? std::sqrt(vecRmseAccum / refNormSqSum) : 0.0;
    metrics.softRelTau = tau;
    metrics.softRelMean = softRelValues.empty() ? 0.0 : softRelSum / static_cast<double>(softRelValues.size());
    metrics.softRelMedian = percentile(softRelValues, 0.5);
    metrics.softRelP90 = percentile(softRelValues, 0.9);
    metrics.lowRefCount = lowRefCount;
    metrics.angleCount = angleValues.size();
    metrics.angleMeanDeg = angleValues.empty()
        ? 0.0
        : std::accumulate(angleValues.begin(), angleValues.end(), 0.0) / static_cast<double>(angleValues.size());
    metrics.angleP90Deg = percentile(angleValues, 0.9);
    metrics.scaleBias = refNormSqSum > 1e-12 ? (dotRefGl / refNormSqSum) : 0.0;
    return metrics;
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

bool writeCsvReport(const std::string& path, const std::vector<CaseRecord>& records)
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
           "gl_wall_avg_ms,gl_wall_min_ms,gl_gpu_avg_ms,gl_gpu_min_ms,"
           "result_tuples,result_components,reference,compare_tuples,compare_components,vec_count,"
           "vec_mae_abs,vec_rmse_abs,vec_max_abs,ref_scale_rms,nmae_vec,nrmse_vec,"
           "softrel_tau,softrel_mean,softrel_median,softrel_p90,low_ref_count,"
           "angle_mean_deg,angle_p90_deg,angle_count,scale_bias,ref_time_avg_ms,ref_time_min_ms\n";

    for (const auto& rec : records) {
        out << csvEscape(rec.dataset) << ','
            << csvEscape(rec.association) << ','
            << csvEscape(rec.arrayName) << ','
            << rec.inputComponents << ','
            << (rec.success ? "1" : "0") << ','
            << csvEscape(rec.failureReason) << ','
            << rec.glWallAvgMs << ','
            << rec.glWallMinMs << ','
            << rec.glGpuAvgMs << ','
            << rec.glGpuMinMs << ','
            << rec.resultTuples << ','
            << rec.resultComponents << ','
            << csvEscape(rec.metrics.referenceLabel) << ','
            << rec.metrics.tupleCount << ','
            << rec.metrics.compareComponents << ','
            << rec.metrics.vecCount << ','
            << rec.metrics.vecMaeAbs << ','
            << rec.metrics.vecRmseAbs << ','
            << rec.metrics.vecMaxAbs << ','
            << rec.metrics.refScaleRms << ','
            << rec.metrics.nmaeVec << ','
            << rec.metrics.nrmseVec << ','
            << rec.metrics.softRelTau << ','
            << rec.metrics.softRelMean << ','
            << rec.metrics.softRelMedian << ','
            << rec.metrics.softRelP90 << ','
            << rec.metrics.lowRefCount << ','
            << rec.metrics.angleMeanDeg << ','
            << rec.metrics.angleP90Deg << ','
            << rec.metrics.angleCount << ','
            << rec.metrics.scaleBias << ','
            << rec.metrics.refTimeAvgMs << ','
            << rec.metrics.refTimeMinMs << '\n';
    }

    return true;
}

CaseRecord runSingleCase(CAEProcessingFacade& facade,
                         const std::string& datasetId,
                         const CAEDatasetSummary& summary,
                         vtkDataSet* vtkDataset,
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
    req.method = CAEGradientMethod::Auto;

    CAEGradientResultMeta meta;
    double glWallSum = 0.0;
    double glWallMin = std::numeric_limits<double>::max();
    double glGpuSum = 0.0;
    double glGpuMin = std::numeric_limits<double>::max();

    for (int i = 0; i < std::max(opt.reps, 1); ++i) {
        if (!facade.computeGradient(req, meta)) {
            rec.failureReason = "computeGradient failed";
            return rec;
        }
        const double wall = facade.getLastComputeWallMs();
        const double gpu = facade.getLastComputeGpuMs();
        glWallSum += wall;
        glWallMin = std::min(glWallMin, wall);
        glGpuSum += gpu;
        glGpuMin = std::min(glGpuMin, gpu);
    }

    std::vector<float> gradGl;
    int glComps = 0;
    if (!facade.getArrayData(datasetId, meta.resultArrayName, opt.assoc, gradGl, glComps)) {
        rec.failureReason = "failed to fetch OpenGL result array";
        return rec;
    }

    ReferenceData ref = resolveReference(
        facade, datasetId, vtkDataset, field.name, opt.assoc, opt.referenceMode, opt.reps);
    if (opt.referenceMode != ReferenceMode::Auto &&
        opt.referenceMode != ReferenceMode::None &&
        !ref.available) {
        rec.failureReason = "requested reference is unavailable";
        return rec;
    }

    rec.success = true;
    rec.glWallAvgMs = glWallSum / static_cast<double>(std::max(opt.reps, 1));
    rec.glWallMinMs = glWallMin;
    rec.glGpuAvgMs = glGpuSum / static_cast<double>(std::max(opt.reps, 1));
    rec.glGpuMinMs = glGpuMin;
    rec.resultComponents = glComps;
    rec.resultTuples = glComps > 0 ? gradGl.size() / static_cast<size_t>(glComps) : 0;
    rec.metrics = compareGradients(gradGl, glComps, ref);

    std::cout << "Case=" << field.name
              << " InputComps=" << field.numComponents
              << " GL_wall_ms_avg=" << rec.glWallAvgMs
              << " GL_gpu_ms_avg=" << rec.glGpuAvgMs
              << " ResultTuples=" << rec.resultTuples
              << " ResultComps=" << rec.resultComponents << std::endl;

    if (rec.metrics.haveReference) {
        std::cout << "  Reference=" << rec.metrics.referenceLabel;
        if (startsWith(rec.metrics.referenceLabel, "VTK")) {
            std::cout << " VTK_time_ms_avg=" << rec.metrics.refTimeAvgMs
                      << " VTK_time_ms_min=" << rec.metrics.refTimeMinMs;
        }
        std::cout << std::endl;
        std::cout << "  Compare tuples=" << rec.metrics.tupleCount
                  << " comps=" << rec.metrics.compareComponents
                  << " vec_count=" << rec.metrics.vecCount << std::endl;
        std::cout << "  VecErr_MAE_abs=" << rec.metrics.vecMaeAbs
                  << " VecErr_RMSE_abs=" << rec.metrics.vecRmseAbs
                  << " VecErr_MAX_abs=" << rec.metrics.vecMaxAbs << std::endl;
        std::cout << "  RefScale_RMS=" << rec.metrics.refScaleRms
                  << " NMAE_vec=" << rec.metrics.nmaeVec
                  << " NRMSE_vec=" << rec.metrics.nrmseVec << std::endl;
        std::cout << "  SoftRel_tau=" << rec.metrics.softRelTau
                  << " SoftRel_mean=" << rec.metrics.softRelMean
                  << " SoftRel_median=" << rec.metrics.softRelMedian
                  << " SoftRel_P90=" << rec.metrics.softRelP90
                  << " LowRefCount=" << rec.metrics.lowRefCount << std::endl;
        std::cout << "  Angle_mean_deg=" << rec.metrics.angleMeanDeg
                  << " Angle_P90_deg=" << rec.metrics.angleP90Deg
                  << " AngleCount=" << rec.metrics.angleCount << std::endl;
        std::cout << "  ScaleBias=" << rec.metrics.scaleBias << std::endl;
    } else {
        std::cout << "  Reference=SKIPPED mode=" << referenceModeName(opt.referenceMode) << std::endl;
    }

    if (verboseSamples && opt.maxSamplesToPrint > 0) {
        size_t show = std::min(rec.resultTuples, static_cast<size_t>(opt.maxSamplesToPrint));
        if (ref.available && ref.comps > 0) {
            const size_t refTuples = ref.values.size() / static_cast<size_t>(ref.comps);
            show = std::min(show, refTuples);
        }
        for (size_t i = 0; i < show; ++i) {
            std::cout << "  i=" << i << " GL=[";
            for (int c = 0; c < glComps; ++c) {
                std::cout << gradGl[i * static_cast<size_t>(glComps) + static_cast<size_t>(c)]
                          << (c + 1 < glComps ? "," : "");
            }
            std::cout << "]";
            if (ref.available) {
                std::cout << " REF=[";
                for (int c = 0; c < ref.comps; ++c) {
                    std::cout << ref.values[i * static_cast<size_t>(ref.comps) + static_cast<size_t>(c)]
                              << (c + 1 < ref.comps ? "," : "");
                }
                std::cout << "]";
            }
            std::cout << std::endl;
        }
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
        std::cerr << "no dataset summary" << std::endl;
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

    std::cout << "Dataset=" << summary.displayName
              << " points=" << summary.pointCount
              << " cells=" << summary.cellCount << std::endl;
    std::cout << "Association=" << assocName(opt.assoc)
              << " RunMode=" << runModeName(opt.runMode)
              << " ReferenceMode=" << referenceModeName(opt.referenceMode)
              << " AnalyticBenchmarks=" << (opt.enableAnalyticBenchmarks ? "ON" : "OFF")
              << std::endl;

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
                      << " exact=" << field.name << "_exact_grad" << std::endl;
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
            auto matchesDefaultFilter = [&](const CAEFieldInfo& field) {
                return !isExactGradientArray(field.name) &&
                    containsCaseInsensitive(field.name, opt.nameFilter);
            };
            auto it = std::find_if(fields.begin(), fields.end(),
                [&](const CAEFieldInfo& field) {
                    return field.name == "benchmark_trig" && matchesDefaultFilter(field);
                });
            if (it == fields.end()) {
                it = std::find_if(fields.begin(), fields.end(),
                    [&](const CAEFieldInfo& field) { return matchesDefaultFilter(field); });
            }
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

    vtkSmartPointer<vtkDataSet> vtkDataset;
    if (opt.referenceMode == ReferenceMode::Vtk || opt.referenceMode == ReferenceMode::Auto) {
        if (!facade.exportDatasetToVTK(datasetId, vtkDataset)) {
            std::cout << "ReferenceHint=failed to export dataset to VTK, VTK references may be unavailable" << std::endl;
        }
    }

    std::cout << "SelectedCases=" << selected.size() << std::endl;
    for (const auto& field : selected) {
        std::cout << "  - " << field.name
                  << " comps=" << field.numComponents
                  << " tuples=" << field.tupleCount << std::endl;
    }

    std::vector<CaseRecord> records;
    records.reserve(selected.size());
    bool anyFailure = false;

    for (size_t i = 0; i < selected.size(); ++i) {
        std::cout << "CaseStart index=" << (i + 1) << "/" << selected.size()
                  << " name=" << selected[i].name << std::endl;
        CaseRecord rec = runSingleCase(
            facade,
            datasetId,
            summary,
            vtkDataset,
            selected[i],
            opt,
            opt.runMode == RunMode::Single);
        if (!rec.success) {
            anyFailure = true;
            std::cout << "CaseFailed name=" << rec.arrayName
                      << " reason=" << rec.failureReason << std::endl;
        }
        records.push_back(rec);
        std::cout << "CaseEnd index=" << (i + 1) << "/" << selected.size()
                  << " success=" << (rec.success ? "YES" : "NO") << std::endl;
    }

    if (!opt.csvPath.empty()) {
        if (writeCsvReport(opt.csvPath, records)) {
            std::cout << "CSVReport=" << opt.csvPath << std::endl;
        } else {
            std::cout << "CSVReportFailed=" << opt.csvPath << std::endl;
            anyFailure = true;
        }
    }

    size_t successCount = 0;
    for (const auto& rec : records) {
        successCount += rec.success ? 1u : 0u;
    }
    std::cout << "Summary success=" << successCount
              << " failed=" << (records.size() - successCount)
              << " total=" << records.size() << std::endl;

    if (!records.empty()) {
        std::vector<double> nmaeValues;
        std::vector<double> nrmseValues;
        std::vector<double> angleP90Values;
        for (const auto& rec : records) {
            if (!rec.success || !rec.metrics.haveReference) {
                continue;
            }
            nmaeValues.push_back(rec.metrics.nmaeVec);
            nrmseValues.push_back(rec.metrics.nrmseVec);
            angleP90Values.push_back(rec.metrics.angleP90Deg);
        }
        if (!nmaeValues.empty()) {
            const double avgNmae = std::accumulate(nmaeValues.begin(), nmaeValues.end(), 0.0) /
                static_cast<double>(nmaeValues.size());
            const double avgNrmse = std::accumulate(nrmseValues.begin(), nrmseValues.end(), 0.0) /
                static_cast<double>(nrmseValues.size());
            const double avgAngleP90 = std::accumulate(angleP90Values.begin(), angleP90Values.end(), 0.0) /
                static_cast<double>(angleP90Values.size());
            std::cout << "SummaryMetrics"
                      << " mean_NMAE_vec=" << avgNmae
                      << " mean_NRMSE_vec=" << avgNrmse
                      << " mean_Angle_P90_deg=" << avgAngleP90
                      << std::endl;
        }
    }

    return anyFailure ? 7 : 0;
}
