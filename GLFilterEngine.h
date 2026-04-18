#pragma once

#include <string>
#include <vector>

#include <glad/glad.h>

/// GPU 滤波与多尺度融合引擎。
///
/// 它对应数据优化模块的底层执行层，当前负责两类任务：
/// 1. 图双边滤波；
/// 2. 多尺度细节融合。
///
/// 上层的“构建几层、怎么命名结果、是否保存中间层”等策略，
/// 都由 `CAEProcessingFacade` 和测试程序负责。
class GLFilterEngine {
public:
    /// 图双边滤波参数。
    ///
    /// - `spatialSigma`：控制几何邻近样本的影响范围；
    /// - `rangeSigma`：控制数值相近样本的影响范围。
    struct BilateralParams {
        float spatialSigma = 1.0f;
        float rangeSigma = 0.1f;
    };

    /// 多尺度融合参数。
    ///
    /// 当前系统最多保留 3 层细节，因此 `detailGains` 固定为长度 3。
    struct FusionParams {
        int levelCount = 3;       ///< 实际参与融合的层数
        float edgeSigma = 0.05f;  ///< 细节抑制/保边相关阈值
        float detailGains[3];     ///< 每层细节的增益系数

        FusionParams()
        {
            detailGains[0] = 1.0f;
            detailGains[1] = 0.75f;
            detailGains[2] = 0.5f;
        }
    };

    GLFilterEngine();
    ~GLFilterEngine();

    /// 设置着色器目录。
    bool setShaderDir(const std::string& dir);

    /// 编译并初始化滤波/融合相关计算着色器。
    bool init();

    /// 释放程序对象、SSBO 与计时资源。
    void release();

    /// 在图结构上执行一次双边滤波。
    ///
    /// `positions + offsets + neighbors` 共同定义了图结构和几何关系，
    /// `values` 是待平滑字段，输出写入 `outValues`。
    bool bilateralGraph(const std::vector<float>& positions,
                        const std::vector<int>& offsets,
                        const std::vector<int>& neighbors,
                        const std::vector<float>& values,
                        const BilateralParams& p,
                        std::vector<float>& outValues);

    /// 将 base 层和 3 层细节按给定权重融合为最终结果。
    ///
    /// 若实际层数小于 3，上层会把不存在的细节层传成零数组。
    bool fuseMultiScale(const std::vector<float>& base,
                        const std::vector<float>& detail0,
                        const std::vector<float>& detail1,
                        const std::vector<float>& detail2,
                        const FusionParams& p,
                        std::vector<float>& outValues);

    /// 是否启用 GPU 计时。
    void setEnableGpuTiming(bool on);

    /// 获取最近一次 GPU 执行时间，单位毫秒。
    double getLastGpuTimeMs() const;

private:
    std::string shaderDir;   ///< 着色器目录

    GLuint progBilateral = 0;///< 图双边滤波 shader 程序
    GLuint progFusion = 0;   ///< 多尺度融合 shader 程序

    /// GPU 计算复用的 SSBO。
    ///
    /// 具体绑定意义会因滤波或融合而变化，但总体都是输入、邻接和输出缓冲。
    GLuint ssbo0 = 0;
    GLuint ssbo1 = 0;
    GLuint ssbo2 = 0;
    GLuint ssbo3 = 0;
    GLuint ssbo4 = 0;

    bool enableGpuTiming = false; ///< 是否启用 GPU 计时
    GLuint timeQuery = 0;         ///< OpenGL 时间查询对象
    double lastGpuTimeMs = 0.0;   ///< 最近一次 GPU 计算耗时

    /// 编译单个计算着色器并返回程序对象 ID。
    GLuint buildComputeFromFile(const std::string& path);

    /// 保证 SSBO 已创建且空间足够。
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);
};
