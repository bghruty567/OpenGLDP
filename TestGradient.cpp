#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkGradientFilter.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkSMPTools.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "CAEProcessingFacade.h"
#include "TestHarnessUtils.h"

namespace
{
using namespace testharness;

class TeeStreamBuf : public std::streambuf
{
public:
    TeeStreamBuf(std::streambuf* a, std::streambuf* b)
        : first(a), second(b)
    {
    }

protected:
    int overflow(int ch) override
    {
        if (ch == EOF) {
            return 0;
        }

        const int outA = this->first ? this->first->sputc(static_cast<char>(ch)) : ch;
        const int outB = this->second ? this->second->sputc(static_cast<char>(ch)) : ch;
        return (outA == EOF || outB == EOF) ? EOF : ch;
    }

    int sync() override
    {
        const int syncA = this->first ? this->first->pubsync() : 0;
        const int syncB = this->second ? this->second->pubsync() : 0;
        return (syncA == 0 && syncB == 0) ? 0 : -1;
    }

private:
    std::streambuf* first = nullptr;
    std::streambuf* second = nullptr;
};

class ScopedRunLog
{
public:
    explicit ScopedRunLog(const std::filesystem::path& path)
        : logPath(path)
    {
        std::error_code ec;
        if (this->logPath.has_parent_path()) {
            std::filesystem::create_directories(this->logPath.parent_path(), ec);
        }

        this->file.open(this->logPath, std::ios::out | std::ios::trunc);
        if (!this->file) {
            return;
        }

        this->oldCout = std::cout.rdbuf();
        this->oldCerr = std::cerr.rdbuf();
        this->coutBuf = std::make_unique<TeeStreamBuf>(this->oldCout, this->file.rdbuf());
        this->cerrBuf = std::make_unique<TeeStreamBuf>(this->oldCerr, this->file.rdbuf());
        std::cout.rdbuf(this->coutBuf.get());
        std::cerr.rdbuf(this->cerrBuf.get());
        this->active = true;

        std::cout << "RunLog=" << this->logPath.string() << std::endl;
    }

    ~ScopedRunLog()
    {
        if (!this->active) {
            return;
        }

        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(this->oldCout);
        std::cerr.rdbuf(this->oldCerr);
    }

private:
    std::filesystem::path logPath;
    std::ofstream file;
    std::streambuf* oldCout = nullptr;
    std::streambuf* oldCerr = nullptr;
    std::unique_ptr<TeeStreamBuf> coutBuf;
    std::unique_ptr<TeeStreamBuf> cerrBuf;
    bool active = false;
};

enum class ReferenceMode
{
    Auto,
    Analytic,
    Vtk,
    None
};

enum class ResultSource
{
    Gl,
    Vtk
};

enum class RunMode
{
    Single,
    Benchmarks,
    Fields
};

enum class VtkBackendMode
{
    Auto,
    Current,
    Sequential,
    StdThread,
    OpenMP,
    TBB
};

// 命令行配置集合。
//
// 这个结构把一次实验需要的“数据集选择、参考来源、算法参数、输出方式”
// 全部收拢到一起，方便：
// 1. 命令行解析；
// 2. 控制台打印配置；
// 3. CSV 报告记录当前实验条件。
struct Options
{
    std::string file="timing_struct_20x20x20";
    std::string path = "Data\\timing\\"+file+".vtk";
    CAEFieldAssociation assoc = CAEFieldAssociation::Cell;
    std::string arrayName;
    int reps = 1;
    bool enableAnalyticBenchmarks = false;
    ReferenceMode referenceMode = ReferenceMode::Auto;
    ResultSource resultSource = ResultSource::Gl;
    int maxSamplesToPrint = 12;
    RunMode runMode = RunMode::Single;
    bool listFields = false;
    bool listBenchmarks = false;
    bool showConfig = false;
    std::string nameFilter;
    std::string csvPath = "results\\"+file+"cell.csv";
    int vtkParallelThreads = 0;
    VtkBackendMode vtkBackendMode = VtkBackendMode::StdThread;

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

// 某个参考梯度来源的完整描述。
//
// 参考来源可能是：
// 1. 解析真值；
// 2. vtkGradientFilter；
// 3. 不提供参考。
struct ReferenceData
{
    bool available = false;         ///< 当前参考数据是否成功取得
    bool analytic = false;          ///< 是否来自解析真值，而不是 VTK 对照
    std::string label;              ///< 参考来源标签，例如 analytic / vtk
    std::vector<float> values;      ///< 参考梯度数据
    int comps = 0;                  ///< 参考梯度每个 tuple 的分量数
    double timeAvgMs = 0.0;         ///< 参考方法平均耗时
    double timeMinMs = 0.0;         ///< 参考方法最小耗时
    bool vtkParallelInfoAvailable = false; ///< 是否携带 VTK SMP 计时信息
    std::string vtkBackend;         ///< VTK SMP backend
    int vtkSingleThreads = 0;       ///< VTK 单线程估计线程数
    int vtkParallelThreads = 0;     ///< VTK 并行估计线程数
    int vtkRequestedParallelThreads = 0; ///< 请求的 VTK 并行线程数
    double vtkSingleAvgMs = 0.0;    ///< VTK 单线程平均耗时
    double vtkSingleMinMs = 0.0;    ///< VTK 单线程最小耗时
    double vtkParallelAvgMs = 0.0;  ///< VTK 并行平均耗时
    double vtkParallelMinMs = 0.0;  ///< VTK 并行最小耗时
};

// 梯度对比时汇总的误差指标。
//
// 之所以同时保留绝对误差、归一化误差、软相对误差、方向夹角等多组指标，
// 是因为单一指标很难完整解释非结构网格上的误差来源。
//
// 例如：
// - 低梯度区域会把普通相对误差放大；
// - 曲面数据上法向分量会影响环境梯度对比；
// - 局部奇异点会把最大误差拉得很高。
struct CompareMetrics
{
    bool haveReference = false;     ///< 当前案例是否存在可比较的参考梯度
    std::string referenceLabel;     ///< 参考来源名称
    size_t tupleCount = 0;          ///< 实际参与比较的 tuple 数
    int compareComponents = 0;      ///< 实际参与比较的分量数
    int vecsPerTuple = 0;           ///< 每个 tuple 中可视为多少个 3D 梯度向量
    size_t rawVecCount = 0;         ///< 理论应比较的梯度向量数量
    size_t finiteVecCount = 0;      ///< 结果和参考都为有限值的向量数量
    size_t nonFiniteVecCount = 0;   ///< 含 NaN/Inf 的向量数量
    size_t lowRefCount = 0;         ///< 参考梯度过小、容易放大相对误差的向量数量
    double lowRefRatio = 0.0;       ///< 低参考梯度向量占比

    double vecMaeAbs = 0.0;         ///< 绝对向量误差 MAE
    double vecRmseAbs = 0.0;        ///< 绝对向量误差 RMSE
    double vecMaxAbs = 0.0;         ///< 最大绝对向量误差
    double refScaleRms = 0.0;       ///< 参考梯度模长的 RMS，用于归一化
    double nmaeVec = 0.0;           ///< 归一化 MAE
    double nrmseVec = 0.0;          ///< 归一化 RMSE
    double softRelTau = 0.0;        ///< soft relative error 的稳定阈值
    double softRelMean = 0.0;       ///< 全部向量的 soft relative error 均值
    double softRelMedian = 0.0;     ///< 全部向量的 soft relative error 中位数
    double softRelP90 = 0.0;        ///< 全部向量的 soft relative error 90 分位

    size_t stableVecCount = 0;      ///< 参考梯度不太小、适合稳定比较的向量数量
    double stableMaeAbs = 0.0;      ///< stable 子集上的绝对 MAE
    double stableRmseAbs = 0.0;     ///< stable 子集上的绝对 RMSE
    double stableSoftRelMean = 0.0; ///< stable 子集上的 soft relative error 均值
    double stableSoftRelMedian = 0.0; ///< stable 子集上的 soft relative error 中位数
    double stableSoftRelP90 = 0.0;  ///< stable 子集上的 soft relative error 90 分位

    double angleMeanDeg = 0.0;      ///< 梯度方向夹角平均值，单位度
    double angleP90Deg = 0.0;       ///< 梯度方向夹角 90 分位，单位度
    size_t angleCount = 0;          ///< 参与角度统计的向量数量
    double scaleBias = 0.0;         ///< 整体尺度偏差，接近 1 更理想

    size_t worstTuple = std::numeric_limits<size_t>::max(); ///< 最坏误差对应的 tuple 索引
    int worstVector = -1;           ///< 最坏误差对应 tuple 内的第几个梯度向量
    double worstErrAbs = 0.0;       ///< 最坏向量的绝对误差
    double worstRefNorm = 0.0;      ///< 最坏向量对应参考梯度模长

    double refTimeAvgMs = 0.0;      ///< 参考方法平均耗时
    double refTimeMinMs = 0.0;      ///< 参考方法最小耗时
    bool vtkParallelInfoAvailable = false; ///< 当前参考是否包含 VTK 并行信息
    std::string vtkBackend;         ///< VTK SMP backend
    int vtkSingleThreads = 0;       ///< VTK 单线程估计线程数
    int vtkParallelThreads = 0;     ///< VTK 并行估计线程数
    int vtkRequestedParallelThreads = 0; ///< 请求的 VTK 并行线程数
    double vtkSingleAvgMs = 0.0;    ///< VTK 单线程平均耗时
    double vtkSingleMinMs = 0.0;    ///< VTK 单线程最小耗时
    double vtkParallelAvgMs = 0.0;  ///< VTK 并行平均耗时
    double vtkParallelMinMs = 0.0;  ///< VTK 并行最小耗时
};

struct VtkTimingStats
{
    bool available = false;
    std::string backend;
    int estimatedThreads = 0;
    double avgMs = 0.0;
    double minMs = 0.0;
};

// 几何分析的摘要结果。
//
// 这些量不是梯度结果本身，而是帮助解释误差的“背景信息”，例如：
// - 数据更像体数据还是曲面数据；
// - 局部维度标签分布如何；
// - 是否存在明显的近二维/近一维结构。
struct GeometrySummary
{
    bool available = false;         ///< 是否成功完成几何分析
    int topoDim = 3;                ///< 由单元类型推断的拓扑维度
    int globalGeomDim = 3;          ///< 由整体协方差推断的几何维度
    bool surfaceLike = false;       ///< 是否更像曲面/壳体型数据
    std::array<double, 3> extents{ 0.0, 0.0, 0.0 }; ///< 包围盒三个方向的尺寸
    std::array<double, 3> eigenRatios{ 1.0, 1.0, 1.0 }; ///< 全局特征值比例
    size_t dim1Count = 0;           ///< 局部判定为 1D 的样本数
    size_t dim2Count = 0;           ///< 局部判定为 2D 的样本数
    size_t dim3Count = 0;           ///< 局部判定为 3D 的样本数
};

// 单个测试案例最终写入控制台与 CSV 的记录。
//
// 一个案例通常对应“一个字段 + 一组算法参数 + 一种参考来源”的组合。
struct CaseRecord
{
    std::string dataset;            ///< 数据集显示名
    std::string association;        ///< POINT 或 CELL
    std::string arrayName;          ///< 当前测试字段名
    std::string resultSource;       ///< 当前被评估结果来自 GL 还是 VTK
    int inputComponents = 0;        ///< 输入字段分量数
    bool success = false;           ///< 当前案例是否执行成功
    std::string failureReason;      ///< 失败原因
    double resultWallAvgMs = 0.0;   ///< 当前被评估结果的平均墙钟时间
    double resultWallMinMs = 0.0;   ///< 当前被评估结果的最小墙钟时间
    double resultGpuAvgMs = 0.0;    ///< 当前被评估结果的平均 GPU 时间；VTK 模式下为 0
    double resultGpuMinMs = 0.0;    ///< 当前被评估结果的最小 GPU 时间；VTK 模式下为 0
    double glWallAvgMs = 0.0;       ///< 本方法平均墙钟时间
    double glWallMinMs = 0.0;       ///< 本方法最小墙钟时间
    double glGpuAvgMs = 0.0;        ///< 本方法平均 GPU 时间
    double glGpuMinMs = 0.0;        ///< 本方法最小 GPU 时间
    size_t resultTuples = 0;        ///< 结果字段 tuple 数
    int resultComponents = 0;       ///< 结果字段分量数
    CompareMetrics ambientMetrics;  ///< 与环境三维梯度口径比较得到的指标
    CompareMetrics intrinsicMetrics;///< 与切空间投影梯度口径比较得到的指标
    bool hasIntrinsicMetrics = false; ///< 是否实际计算了 intrinsic 指标
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

const char* resultSourceName(ResultSource source)
{
    switch (source) {
    case ResultSource::Vtk: return "vtk";
    default: return "gl";
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

const char* vtkBackendModeName(VtkBackendMode mode)
{
    switch (mode) {
    case VtkBackendMode::Current: return "current";
    case VtkBackendMode::Sequential: return "sequential";
    case VtkBackendMode::StdThread: return "stdthread";
    case VtkBackendMode::OpenMP: return "openmp";
    case VtkBackendMode::TBB: return "tbb";
    default: return "auto";
    }
}

const char* methodName(CAEGradientMethod method)
{
    switch (method) {
    case CAEGradientMethod::FiniteDifference: return "fd";
    case CAEGradientMethod::AdaptiveWeightedLeastSquares: return "awls";
    case CAEGradientMethod::ShapeFunctionDerivatives: return "shape";
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

bool parseResultSource(std::string value, ResultSource& out)
{
    value = toLower(std::move(value));
    if (value == "gl" || value == "gpu" || value == "system") {
        out = ResultSource::Gl;
        return true;
    }
    if (value == "vtk" || value == "cpu-vtk") {
        out = ResultSource::Vtk;
        return true;
    }
    return false;
}

bool parseVtkBackendMode(std::string value, VtkBackendMode& out)
{
    value = toLower(std::move(value));
    if (value == "auto") {
        out = VtkBackendMode::Auto;
        return true;
    }
    if (value == "current" || value == "default") {
        out = VtkBackendMode::Current;
        return true;
    }
    if (value == "sequential" || value == "seq") {
        out = VtkBackendMode::Sequential;
        return true;
    }
    if (value == "stdthread" || value == "std" || value == "thread") {
        out = VtkBackendMode::StdThread;
        return true;
    }
    if (value == "openmp" || value == "omp") {
        out = VtkBackendMode::OpenMP;
        return true;
    }
    if (value == "tbb") {
        out = VtkBackendMode::TBB;
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
    if (value == "shape" || value == "sfd" || value == "shape-function" ||
        value == "shapefunction" || value == "shape-function-derivatives") {
        out = CAEGradientMethod::ShapeFunctionDerivatives;
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

bool parseNonNegativeInt(const std::string& value, int& out)
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

void printHelp()
{
    // 这份帮助文本基本覆盖了 TestGradient 的全部实验入口。
    // 当你想复现实验或改参数时，最先看这里通常最快。
    std::cout
        << "Usage:\n"
        << "  opengldp_benchmark [dataset] [point|cell] [array] [reps] [options]\n\n"
        << "Common options:\n"
        << "  --dataset=<path>\n"
        << "  --assoc=point|cell\n"
        << "  --array=<name>\n"
        << "  --reps=<n>\n"
        << "  --run=single|benchmarks|fields\n"
        << "  --result=gl|vtk\n"
        << "  --reference=auto|analytic|vtk|none\n"
        << "  --analytic-bench=on|off\n"
        << "  --method=auto|fd|awls|shape   (auto: regular->fd, unstructured->shape)\n"
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
        << "  --vtk-backend=auto|current|sequential|stdthread|openmp|tbb\n"
        << "  --vtk-parallel-threads=<n>\n"
        << "  --list-fields\n"
        << "  --list-benchmarks\n"
        << "  --show-config\n"
        << "  --help\n";
}

bool parseCommandLine(int argc, char** argv, Options& opt)
{
    // 参数解析分成两轮：
    // 1. 先收集 positional 参数，兼容最短命令格式；
    // 2. 再解析命名选项，让后者可以覆盖前者。
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
        if (parseValueOption("--result=", value) || parseValueOption("--subject=", value) ||
            parseValueOption("--source=", value)) {
            if (!parseResultSource(value, opt.resultSource)) {
                std::cerr << "invalid result source: " << arg << std::endl;
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
        if (parseValueOption("--vtk-parallel-threads=", value) ||
            parseValueOption("--vtk-threads=", value) ||
            parseValueOption("vtk-parallel-threads=", value) ||
            parseValueOption("vtk-threads=", value)) {
            if (!parseNonNegativeInt(value, opt.vtkParallelThreads)) {
                std::cerr << "invalid vtk parallel threads: " << arg << std::endl;
                return false;
            }
            continue;
        }
        if (parseValueOption("--vtk-backend=", value) ||
            parseValueOption("--vtk-smp-backend=", value) ||
            parseValueOption("vtk-backend=", value) ||
            parseValueOption("vtk-smp-backend=", value)) {
            if (!parseVtkBackendMode(value, opt.vtkBackendMode)) {
                std::cerr << "invalid vtk backend: " << arg << std::endl;
                return false;
            }
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

std::string currentVtkBackend()
{
    const char* backend = vtkSMPTools::GetBackend();
    return backend ? std::string(backend) : std::string("UNKNOWN");
}

std::string canonicalVtkBackendName(VtkBackendMode mode)
{
    switch (mode) {
    case VtkBackendMode::Sequential: return "Sequential";
    case VtkBackendMode::StdThread: return "STDThread";
    case VtkBackendMode::OpenMP: return "OpenMP";
    case VtkBackendMode::TBB: return "TBB";
    default: return std::string();
    }
}

std::string resolveVtkTimingBackend(VtkBackendMode mode)
{
    const std::string original = currentVtkBackend();
    if (mode == VtkBackendMode::Current) {
        return original;
    }

    if (mode == VtkBackendMode::Auto) {
        static const std::array<const char*, 3> preferredBackends{ "TBB", "OpenMP", "STDThread" };
        for (const char* backend : preferredBackends) {
            if (vtkSMPTools::SetBackend(backend)) {
                return currentVtkBackend();
            }
        }
        if (vtkSMPTools::SetBackend("Sequential")) {
            return currentVtkBackend();
        }
        return original;
    }

    const std::string requested = canonicalVtkBackendName(mode);
    if (!requested.empty() && vtkSMPTools::SetBackend(requested.c_str())) {
        return currentVtkBackend();
    }

    std::cerr << "[VTK] requested SMP backend '" << requested
              << "' is unavailable; keeping backend '" << original << "'."
              << std::endl;
    return original;
}

template <typename RunOnce>
VtkTimingStats measureVtkInScope(const vtkSMPTools::Config& config, int reps, RunOnce&& runOnce)
{
    VtkTimingStats stats;
    vtkSMPTools::LocalScope(config, [&]() {
        stats.backend = currentVtkBackend();
        stats.estimatedThreads = std::max(1, vtkSMPTools::GetEstimatedNumberOfThreads());
        double sumMs = 0.0;
        double minMs = std::numeric_limits<double>::max();
        for (int i = 0; i < std::max(reps, 1); ++i) {
            const double ms = runOnce();
            sumMs += ms;
            minMs = std::min(minMs, ms);
        }
        stats.available = true;
        stats.avgMs = sumMs / static_cast<double>(std::max(reps, 1));
        stats.minMs = minMs;
    });
    return stats;
}

std::array<double, 3> loadFrameAxis(const std::array<float, 9>& frame, int axis)
{
    const size_t base = static_cast<size_t>(std::clamp(axis, 0, 2)) * 3u;
    return {
        static_cast<double>(frame[base + 0]),
        static_cast<double>(frame[base + 1]),
        static_cast<double>(frame[base + 2])
    };
}

std::array<double, 3> loadLocalAxis(const GeometryAnalysis& geometry, size_t tupleIndex, int axis)
{
    const size_t base = tupleIndex * 9u + static_cast<size_t>(std::clamp(axis, 0, 2)) * 3u;
    return {
        static_cast<double>(geometry.frames[base + 0]),
        static_cast<double>(geometry.frames[base + 1]),
        static_cast<double>(geometry.frames[base + 2])
    };
}

std::array<double, 3> projectToLocalSupport(const std::array<double, 3>& grad,
                                            const GeometryAnalysis& geometry,
                                            size_t tupleIndex)
{
    if (!geometry.available || tupleIndex >= geometry.dimTags.size()) {
        return grad;
    }

    const std::array<double, 3> e1 = loadLocalAxis(geometry, tupleIndex, 0);
    const std::array<double, 3> e2 = loadLocalAxis(geometry, tupleIndex, 1);
    const std::array<double, 3> e3 = loadLocalAxis(geometry, tupleIndex, 2);
    const std::uint32_t dim = std::clamp<std::uint32_t>(geometry.dimTags[tupleIndex], 1u, 3u);

    std::array<double, 3> proj{
        dot3(grad, e1) * e1[0],
        dot3(grad, e1) * e1[1],
        dot3(grad, e1) * e1[2]
    };
    if (dim >= 2u) {
        const double d2 = dot3(grad, e2);
        proj[0] += d2 * e2[0];
        proj[1] += d2 * e2[1];
        proj[2] += d2 * e2[2];
    }
    if (dim >= 3u) {
        const double d3 = dot3(grad, e3);
        proj[0] += d3 * e3[0];
        proj[1] += d3 * e3[1];
        proj[2] += d3 * e3[2];
    }
    return proj;
}

bool injectSurfaceAnalyticBenchmarks(CAEProcessingFacade& facade,
                                     const std::string& datasetId,
                                     CAEFieldAssociation assoc,
                                     const GeometryAnalysis& geometry)
{
    if (!geometry.available || !geometry.surfaceLike) {
        return false;
    }
    const size_t tupleCount = geometry.positions.size() / 3u;
    if (tupleCount == 0 || geometry.positions.size() != tupleCount * 3u ||
        geometry.frames.size() != tupleCount * 9u ||
        geometry.dimTags.size() != tupleCount) {
        return false;
    }

    constexpr double kPi = 3.14159265358979323846;
    const std::array<double, 3> center = geometry.bounds.center;
    const double L = std::max(geometry.bounds.maxExtent, 1.0);

    std::array<double, 3> a = normalize3(loadFrameAxis(geometry.globalFrame, 0), { 1.0, 0.0, 0.0 });
    std::array<double, 3> b = normalize3(loadFrameAxis(geometry.globalFrame, 1), arbitraryPerpendicular(a));
    const double ab = dot3(a, b);
    b = normalize3({ b[0] - ab * a[0], b[1] - ab * a[1], b[2] - ab * a[2] }, arbitraryPerpendicular(a));

    std::vector<float> linear(tupleCount, 0.0f);
    std::vector<float> linearGrad(tupleCount * 3u, 0.0f);
    std::vector<float> trig(tupleCount, 0.0f);
    std::vector<float> trigGrad(tupleCount * 3u, 0.0f);
    std::vector<float> vecLinear(tupleCount * 3u, 0.0f);
    std::vector<float> vecLinearGrad(tupleCount * 9u, 0.0f);

    const std::array<double, 3> ambientLinearGrad{
        0.9 * a[0] - 0.35 * b[0],
        0.9 * a[1] - 0.35 * b[1],
        0.9 * a[2] - 0.35 * b[2]
    };
    const std::array<double, 3> ambientVec0{
        0.8 * a[0] - 0.2 * b[0],
        0.8 * a[1] - 0.2 * b[1],
        0.8 * a[2] - 0.2 * b[2]
    };
    const std::array<double, 3> ambientVec1{
        -0.3 * a[0] + 0.9 * b[0],
        -0.3 * a[1] + 0.9 * b[1],
        -0.3 * a[2] + 0.9 * b[2]
    };
    const std::array<double, 3> ambientVec2{
        0.25 * a[0] + 0.15 * b[0],
        0.25 * a[1] + 0.15 * b[1],
        0.25 * a[2] + 0.15 * b[2]
    };

    for (size_t i = 0; i < tupleCount; ++i) {
        const std::array<double, 3> p = loadPosition(geometry.positions, i);
        const std::array<double, 3> d = sub3(p, center);
        const double u = dot3(d, a);
        const double v = dot3(d, b);

        linear[i] = static_cast<float>(0.9 * u - 0.35 * v);
        const std::array<double, 3> linProj = projectToLocalSupport(ambientLinearGrad, geometry, i);
        linearGrad[i * 3u + 0] = static_cast<float>(linProj[0]);
        linearGrad[i * 3u + 1] = static_cast<float>(linProj[1]);
        linearGrad[i * 3u + 2] = static_cast<float>(linProj[2]);

        const double su = kPi * u / L;
        const double sv = kPi * v / L;
        trig[i] = static_cast<float>(L * (std::sin(su) + 0.35 * std::cos(sv)));
        const std::array<double, 3> ambientTrigGrad{
            kPi * std::cos(su) * a[0] - 0.35 * kPi * std::sin(sv) * b[0],
            kPi * std::cos(su) * a[1] - 0.35 * kPi * std::sin(sv) * b[1],
            kPi * std::cos(su) * a[2] - 0.35 * kPi * std::sin(sv) * b[2]
        };
        const std::array<double, 3> trigProj = projectToLocalSupport(ambientTrigGrad, geometry, i);
        trigGrad[i * 3u + 0] = static_cast<float>(trigProj[0]);
        trigGrad[i * 3u + 1] = static_cast<float>(trigProj[1]);
        trigGrad[i * 3u + 2] = static_cast<float>(trigProj[2]);

        const size_t base = i * 3u;
        const size_t gbase = i * 9u;
        vecLinear[base + 0] = static_cast<float>(0.8 * u - 0.2 * v);
        vecLinear[base + 1] = static_cast<float>(-0.3 * u + 0.9 * v);
        vecLinear[base + 2] = static_cast<float>(0.25 * u + 0.15 * v);
        const std::array<double, 3> vec0Proj = projectToLocalSupport(ambientVec0, geometry, i);
        const std::array<double, 3> vec1Proj = projectToLocalSupport(ambientVec1, geometry, i);
        const std::array<double, 3> vec2Proj = projectToLocalSupport(ambientVec2, geometry, i);
        vecLinearGrad[gbase + 0] = static_cast<float>(vec0Proj[0]);
        vecLinearGrad[gbase + 1] = static_cast<float>(vec0Proj[1]);
        vecLinearGrad[gbase + 2] = static_cast<float>(vec0Proj[2]);
        vecLinearGrad[gbase + 3] = static_cast<float>(vec1Proj[0]);
        vecLinearGrad[gbase + 4] = static_cast<float>(vec1Proj[1]);
        vecLinearGrad[gbase + 5] = static_cast<float>(vec1Proj[2]);
        vecLinearGrad[gbase + 6] = static_cast<float>(vec2Proj[0]);
        vecLinearGrad[gbase + 7] = static_cast<float>(vec2Proj[1]);
        vecLinearGrad[gbase + 8] = static_cast<float>(vec2Proj[2]);
    }

    bool ok = true;
    ok = facade.upsertArrayData(datasetId, "benchmark_surface_linear", assoc, linear, 1) && ok;
    ok = facade.upsertArrayData(datasetId, "benchmark_surface_linear_exact_grad", assoc, linearGrad, 3) && ok;
    ok = facade.upsertArrayData(datasetId, "benchmark_surface_trig", assoc, trig, 1) && ok;
    ok = facade.upsertArrayData(datasetId, "benchmark_surface_trig_exact_grad", assoc, trigGrad, 3) && ok;
    ok = facade.upsertArrayData(datasetId, "benchmark_surface_vec_linear", assoc, vecLinear, 3) && ok;
    ok = facade.upsertArrayData(datasetId, "benchmark_surface_vec_linear_exact_grad", assoc, vecLinearGrad, 9) && ok;
    return ok;
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
    // 若数据集中存在 *_exact_grad，就把它当作最高优先级参考真值。
    // 这种参考通常来自 synthetic/benchmark 场，最适合做精确误差对比。
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
                                CAEGradientMethod method,
                                int reps,
                                int vtkParallelThreads,
                                VtkBackendMode vtkBackendMode)
{
    // 当没有解析真值时，用 vtkGradientFilter 作为“工程基线”参考。
    // 同时顺便统计它的运行时间，便于和本项目 GPU 实现做对照。
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
    if (method == CAEGradientMethod::ShapeFunctionDerivatives) {
        gf->SetContributingCellOption(vtkGradientFilter::Patch);
        gf->SetFasterApproximation(false);
    }
    gf->SetInputData(dataset);

    auto runOnce = [&]() -> double {
        gf->Modified();
        const auto t0 = std::chrono::high_resolution_clock::now();
        gf->Update();
        const auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    const std::string backend = resolveVtkTimingBackend(vtkBackendMode);
    const bool nested = vtkSMPTools::GetNestedParallelism();
    const int requestedParallelThreads = std::max(vtkParallelThreads, 0);
    const int effectiveParallelThreads =
        requestedParallelThreads > 0 ? requestedParallelThreads : vtkSMPTools::GetEstimatedDefaultNumberOfThreads();

    const VtkTimingStats singleStats = measureVtkInScope(
        vtkSMPTools::Config{ 1, backend, nested }, reps, runOnce);
    const VtkTimingStats parallelStats = measureVtkInScope(
        vtkSMPTools::Config{ effectiveParallelThreads, backend, nested }, reps, runOnce);

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
    out.timeAvgMs = parallelStats.available ? parallelStats.avgMs : singleStats.avgMs;
    out.timeMinMs = parallelStats.available ? parallelStats.minMs : singleStats.minMs;
    out.vtkParallelInfoAvailable = singleStats.available || parallelStats.available;
    out.vtkBackend = parallelStats.backend.empty() ? singleStats.backend : parallelStats.backend;
    out.vtkSingleThreads = singleStats.estimatedThreads;
    out.vtkParallelThreads = parallelStats.estimatedThreads;
    out.vtkRequestedParallelThreads = requestedParallelThreads;
    out.vtkSingleAvgMs = singleStats.avgMs;
    out.vtkSingleMinMs = singleStats.minMs;
    out.vtkParallelAvgMs = parallelStats.avgMs;
    out.vtkParallelMinMs = parallelStats.minMs;
    return out;
}

ReferenceData resolveReference(CAEProcessingFacade& facade,
                               const std::string& datasetId,
                               vtkDataSet* vtkDataset,
                               const std::string& arrayName,
                               CAEFieldAssociation assoc,
                               CAEGradientMethod method,
                               ResultSource resultSource,
                               ReferenceMode mode,
                               int reps,
                               int vtkParallelThreads,
                               VtkBackendMode vtkBackendMode)
{
    // Auto 模式优先尝试解析真值，拿不到时再退回 VTK。
    // 这样 benchmark 数据和真实工程数据都能共用一套实验脚本。
    if (mode == ReferenceMode::None) {
        return ReferenceData{};
    }
    if (mode == ReferenceMode::Analytic) {
        return buildAnalyticReference(facade, datasetId, arrayName, assoc);
    }
    if (mode == ReferenceMode::Vtk) {
        if (resultSource == ResultSource::Vtk) {
            return ReferenceData{};
        }
        return buildVtkReference(vtkDataset, arrayName, assoc, method, reps, vtkParallelThreads, vtkBackendMode);
    }

    ReferenceData analytic = buildAnalyticReference(facade, datasetId, arrayName, assoc);
    if (analytic.available) {
        return analytic;
    }
    if (resultSource == ResultSource::Vtk) {
        return ReferenceData{};
    }
    return buildVtkReference(vtkDataset, arrayName, assoc, method, reps, vtkParallelThreads, vtkBackendMode);
}

CompareMetrics compareGradients(const std::vector<float>& result,
                                int resultComps,
                                const ReferenceData& ref)
{
    // 这里是 TestGradient 的核心评价逻辑。
    //
    // 评价思路分三层：
    // 1. 先做绝对向量误差，回答“差了多少”；
    // 2. 再做归一化/软相对误差，回答“相对量级如何”；
    // 3. 最后做角度与尺度偏差，回答“方向错了还是幅值错了”。
    //
    // 这样才能区分：
    // - 低梯度区相对误差虚高；
    // - 法向分量不一致；
    // - 整体尺度偏大/偏小；
    // - 少量离群点拖坏总体结果。
    CompareMetrics metrics;
    metrics.haveReference = ref.available;
    metrics.referenceLabel = ref.label;
    metrics.refTimeAvgMs = ref.timeAvgMs;
    metrics.refTimeMinMs = ref.timeMinMs;
    metrics.vtkParallelInfoAvailable = ref.vtkParallelInfoAvailable;
    metrics.vtkBackend = ref.vtkBackend;
    metrics.vtkSingleThreads = ref.vtkSingleThreads;
    metrics.vtkParallelThreads = ref.vtkParallelThreads;
    metrics.vtkRequestedParallelThreads = ref.vtkRequestedParallelThreads;
    metrics.vtkSingleAvgMs = ref.vtkSingleAvgMs;
    metrics.vtkSingleMinMs = ref.vtkSingleMinMs;
    metrics.vtkParallelAvgMs = ref.vtkParallelAvgMs;
    metrics.vtkParallelMinMs = ref.vtkParallelMinMs;

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
    // GeometryAnalysis 是较底层、更适合算法使用的结构。
    // 这里将其整理成更适合打印与写 CSV 的轻量摘要。
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
              << " result=" << resultSourceName(opt.resultSource)
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
              << " vtkBackend=" << vtkBackendModeName(opt.vtkBackendMode)
              << " vtkParallelThreads=" << opt.vtkParallelThreads
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
    if (m.vtkParallelInfoAvailable) {
        std::cout << "    vtk_backend=" << m.vtkBackend
                  << " vtk_single_threads=" << m.vtkSingleThreads
                  << " vtk_single_avg_ms=" << m.vtkSingleAvgMs
                  << " vtk_single_min_ms=" << m.vtkSingleMinMs
                  << " vtk_parallel_threads=" << m.vtkParallelThreads
                  << " vtk_parallel_avg_ms=" << m.vtkParallelAvgMs
                  << " vtk_parallel_min_ms=" << m.vtkParallelMinMs;
        if (m.vtkRequestedParallelThreads > 0) {
            std::cout << " vtk_requested_parallel_threads=" << m.vtkRequestedParallelThreads;
        }
        std::cout << std::endl;
    }
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
    // CSV 报告的设计目标不是只保存“一个误差值”，
    // 而是尽量把后续复盘需要的上下文都带上：
    // - 实验配置；
    // - 几何类别；
    // - 算法参数；
    // - 多组误差指标；
    // - 参考来源和计时。
    //
    // 这样后续无论用 Excel、Python 还是论文表格整理，都能追溯问题来源。
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
           "run_mode,result_source,reference_mode,method,reps,"
           "adaptive_neighborhood,adaptive_dimension,adaptive_regularization,"
           "min_neighbors,target_neighbors,max_neighbors,radius_scale,plane_eigen_ratio,line_eigen_ratio,lambda_amplify,vtk_requested_parallel_threads,vtk_backend_mode,"
           "geom_available,geom_topo_dim,geom_global_dim,geom_surface_like,geom_extent_x,geom_extent_y,geom_extent_z,"
           "geom_eigen_ratio21,geom_eigen_ratio31,geom_eigen_ratio32,geom_dim1_count,geom_dim2_count,geom_dim3_count,"
           "result_wall_avg_ms,result_wall_min_ms,result_gpu_avg_ms,result_gpu_min_ms,"
           "gl_wall_avg_ms,gl_wall_min_ms,gl_gpu_avg_ms,gl_gpu_min_ms,result_tuples,result_components,"
           "ambient_reference,ambient_compare_tuples,ambient_compare_components,ambient_raw_vecs,ambient_finite_vecs,ambient_nonfinite_vecs,ambient_low_ref_count,ambient_low_ref_ratio,"
           "ambient_vtk_backend,ambient_vtk_single_threads,ambient_vtk_parallel_threads,ambient_vtk_single_avg_ms,ambient_vtk_single_min_ms,ambient_vtk_parallel_avg_ms,ambient_vtk_parallel_min_ms,"
           "ambient_abs_mae,ambient_abs_rmse,ambient_abs_max,ambient_ref_rms,ambient_nmae,ambient_nrmse,"
           "ambient_softrel_tau,ambient_softrel_mean,ambient_softrel_median,ambient_softrel_p90,"
           "ambient_stable_vecs,ambient_stable_abs_mae,ambient_stable_abs_rmse,ambient_stable_softrel_mean,ambient_stable_softrel_median,ambient_stable_softrel_p90,"
           "ambient_angle_mean_deg,ambient_angle_p90_deg,ambient_angle_count,ambient_scale_bias,ambient_worst_tuple,ambient_worst_vector,ambient_worst_abs_err,ambient_worst_ref_norm,ambient_ref_time_avg_ms,ambient_ref_time_min_ms,"
           "has_intrinsic,intrinsic_reference,intrinsic_compare_tuples,intrinsic_compare_components,intrinsic_raw_vecs,intrinsic_finite_vecs,intrinsic_nonfinite_vecs,intrinsic_low_ref_count,intrinsic_low_ref_ratio,"
           "intrinsic_vtk_backend,intrinsic_vtk_single_threads,intrinsic_vtk_parallel_threads,intrinsic_vtk_single_avg_ms,intrinsic_vtk_single_min_ms,intrinsic_vtk_parallel_avg_ms,intrinsic_vtk_parallel_min_ms,"
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
            << csvEscape(rec.resultSource) << ','
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
            << opt.vtkParallelThreads << ','
            << csvEscape(vtkBackendModeName(opt.vtkBackendMode)) << ','
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
            << rec.resultWallAvgMs << ','
            << rec.resultWallMinMs << ','
            << rec.resultGpuAvgMs << ','
            << rec.resultGpuMinMs << ','
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
            << csvEscape(rec.ambientMetrics.vtkBackend) << ','
            << rec.ambientMetrics.vtkSingleThreads << ','
            << rec.ambientMetrics.vtkParallelThreads << ','
            << rec.ambientMetrics.vtkSingleAvgMs << ','
            << rec.ambientMetrics.vtkSingleMinMs << ','
            << rec.ambientMetrics.vtkParallelAvgMs << ','
            << rec.ambientMetrics.vtkParallelMinMs << ','
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
            << csvEscape(rec.intrinsicMetrics.vtkBackend) << ','
            << rec.intrinsicMetrics.vtkSingleThreads << ','
            << rec.intrinsicMetrics.vtkParallelThreads << ','
            << rec.intrinsicMetrics.vtkSingleAvgMs << ','
            << rec.intrinsicMetrics.vtkSingleMinMs << ','
            << rec.intrinsicMetrics.vtkParallelAvgMs << ','
            << rec.intrinsicMetrics.vtkParallelMinMs << ','
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
    // 一个 case 的执行顺序：
    // 1. 组装梯度请求；
    // 2. 重复执行多次，统计 wall/gpu 时间；
    // 3. 读回结果数组；
    // 4. 解析或构造参考梯度；
    // 5. 计算环境梯度误差；
    // 6. 若是曲面型解析场，再额外计算 intrinsic 指标。
    //
    // 其中第 6 步很关键，它能把“算法真的错了”和
    // “参考梯度口径与曲面问题不完全一致”这两类现象拆开看。
    CaseRecord rec;
    rec.dataset = summary.displayName;
    rec.association = assocName(opt.assoc);
    rec.arrayName = field.name;
    rec.resultSource = resultSourceName(opt.resultSource);
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

    CAEGradientMethod effectiveMethod = req.method;
    if (effectiveMethod == CAEGradientMethod::Auto) {
        effectiveMethod = (summary.gridClass == CAEGridClass::Regular)
            ? CAEGradientMethod::FiniteDifference
            : CAEGradientMethod::ShapeFunctionDerivatives;
    }

    std::vector<float> result;
    int resultComps = 0;
    ReferenceData vtkResult;

    if (opt.resultSource == ResultSource::Gl) {
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

        if (!facade.getArrayData(datasetId, meta.resultArrayName, opt.assoc, result, resultComps)) {
            rec.failureReason = "failed to fetch result array";
            return rec;
        }

        rec.resultWallAvgMs = wallSum / static_cast<double>(std::max(opt.reps, 1));
        rec.resultWallMinMs = wallMin;
        rec.resultGpuAvgMs = gpuSum / static_cast<double>(std::max(opt.reps, 1));
        rec.resultGpuMinMs = gpuMin;
        rec.glWallAvgMs = rec.resultWallAvgMs;
        rec.glWallMinMs = rec.resultWallMinMs;
        rec.glGpuAvgMs = rec.resultGpuAvgMs;
        rec.glGpuMinMs = rec.resultGpuMinMs;
    } else {
        if (opt.referenceMode == ReferenceMode::Vtk) {
            rec.failureReason = "vtk result mode cannot use vtk as reference";
            return rec;
        }
        vtkResult = buildVtkReference(
            vtkDataset, field.name, opt.assoc, effectiveMethod, opt.reps, opt.vtkParallelThreads, opt.vtkBackendMode);
        if (!vtkResult.available) {
            rec.failureReason = "failed to compute vtk result";
            return rec;
        }
        result = std::move(vtkResult.values);
        resultComps = vtkResult.comps;
        rec.resultWallAvgMs = vtkResult.timeAvgMs;
        rec.resultWallMinMs = vtkResult.timeMinMs;
        rec.resultGpuAvgMs = 0.0;
        rec.resultGpuMinMs = 0.0;
    }

    ReferenceData ref = resolveReference(
        facade,
        datasetId,
        vtkDataset,
        field.name,
        opt.assoc,
        effectiveMethod,
        opt.resultSource,
        opt.referenceMode,
        opt.reps,
        opt.vtkParallelThreads,
        opt.vtkBackendMode);
    if (opt.referenceMode != ReferenceMode::Auto &&
        opt.referenceMode != ReferenceMode::None &&
        !ref.available) {
        rec.failureReason = "requested reference unavailable";
        return rec;
    }
    if (opt.resultSource == ResultSource::Vtk &&
        opt.referenceMode == ReferenceMode::Auto &&
        !ref.available) {
        rec.failureReason = "auto reference unavailable in vtk result mode";
        return rec;
    }

    rec.success = true;
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
              << " result_source=" << rec.resultSource
              << " input_components=" << field.numComponents
              << " result_tuples=" << rec.resultTuples
              << " result_components=" << rec.resultComponents
              << " result_wall_avg_ms=" << rec.resultWallAvgMs
              << " result_gpu_avg_ms=" << rec.resultGpuAvgMs
              << std::endl;
    if (opt.resultSource == ResultSource::Vtk && vtkResult.available && vtkResult.vtkParallelInfoAvailable) {
        std::cout << "  ResultVTK"
                  << " vtk_backend=" << vtkResult.vtkBackend
                  << " vtk_single_threads=" << vtkResult.vtkSingleThreads
                  << " vtk_single_avg_ms=" << vtkResult.vtkSingleAvgMs
                  << " vtk_single_min_ms=" << vtkResult.vtkSingleMinMs
                  << " vtk_parallel_threads=" << vtkResult.vtkParallelThreads
                  << " vtk_parallel_avg_ms=" << vtkResult.vtkParallelAvgMs
                  << " vtk_parallel_min_ms=" << vtkResult.vtkParallelMinMs;
        if (vtkResult.vtkRequestedParallelThreads > 0) {
            std::cout << " vtk_requested_parallel_threads=" << vtkResult.vtkRequestedParallelThreads;
        }
        std::cout << std::endl;
    }
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
    ScopedRunLog runLog(std::filesystem::path("results") / "benchmark_last_run.log");

    // main 负责把“命令行工具”串成一条完整实验链：
    // 解析参数 -> 初始化门面 -> 读取数据 -> 做几何分析 ->
    // 选择测试字段 -> 执行 case -> 导出 CSV -> 输出总览统计。
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

    bool injectedSurfaceBenchmarks = false;
    if (opt.enableAnalyticBenchmarks && geometry.available && geometry.surfaceLike) {
        injectedSurfaceBenchmarks = injectSurfaceAnalyticBenchmarks(facade, datasetId, opt.assoc, geometry);
        if (injectedSurfaceBenchmarks) {
            std::cout << "BenchmarkHint=surface-specific analytic benchmarks injected for "
                      << assocName(opt.assoc)
                      << " association"
                      << std::endl;
            facade.exportDatasetToVTK(datasetId, vtkDataset);
        }
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
              << " cells=" << summary.cellCount
              << " association=" << assocName(opt.assoc)
              << " run=" << runModeName(opt.runMode)
              << " result=" << resultSourceName(opt.resultSource)
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
