#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/glad.h>

/// GPU 梯度计算引擎。
///
/// 这个类只负责“把数据送到 OpenGL 计算着色器中执行并取回结果”，
/// 不负责更高层的算法调度、字段命名和数据集管理。
///
/// 它目前承载三类计算：
/// 1. 规则网格有限差分 `computeRegularFD`
/// 2. 非结构网格传统 WLS `computeUnstructuredWLS`
/// 3. 非结构网格自适应 AWLS `computeUnstructuredAdaptiveWLS`
class GLGradientEngine {
public:
    /// 规则网格有限差分所需几何参数。
    ///
    /// 当前实现真正依赖的是 `dims` 和 `spacing`。
    /// `origin` 被保留下来主要是为了接口语义完整和后续扩展。
    struct RegularParams {
        int dims[3];        ///< 网格维度 `[nx, ny, nz]`
        float origin[3];    ///< 网格原点 `[ox, oy, oz]`
        float spacing[3];   ///< 网格步长 `[sx, sy, sz]`
    };

    /// 非结构网格 WLS/AWLS 的控制参数。
    struct WLSParams {
        float wExponent = 1.0f;              ///< 距离权重指数，越大越强调近邻
        float lambda = 1e-3f;                ///< 基础正则项，缓解病态矩阵求解
        float planeEigenRatio = 0.06f;       ///< 判定局部近二维结构的特征值比例阈值
        float lineEigenRatio = 0.02f;        ///< 判定局部近一维结构的特征值比例阈值
        float lambdaAmplify = 4.0f;          ///< 邻域质量较差时的正则放大量
        int enableAdaptiveDimension = 1;     ///< 是否启用局部维度自适应
        int enableAdaptiveRegularization = 1;///< 是否启用局部正则强度自适应
    };

    GLGradientEngine();
    ~GLGradientEngine();

    /// 设置着色器目录。
    ///
    /// 一般由上层传入 `Shaders` 目录，内部再拼接具体 shader 文件名。
    bool setShaderDir(const std::string& dir);

    /// 编译并初始化梯度相关的计算着色器和计时资源。
    bool init();

    /// 释放程序对象、SSBO 和计时查询对象。
    void release();

    /// 计算规则网格字段梯度。
    ///
    /// 输入字段既可以是标量，也可以是多分量向量场。
    /// 输出统一按“每个输入分量对应一个 3D 梯度向量”排布，
    /// 因此输出分量数恒为 `3 * 输入分量数`。
    bool computeRegularFD(const std::vector<float>& positions,
                          const std::vector<float>& values,
                          const RegularParams& p,
                          std::vector<float>& outGrad);

    /// 计算非结构网格的传统全 3D WLS 梯度。
    ///
    /// 这个接口主要用作：
    /// 1. 传统方法基线；
    /// 2. AWLS 失败时的回退路径。
    bool computeUnstructuredWLS(const std::vector<float>& positions,
                                const std::vector<int>& offsets,
                                const std::vector<int>& neighbors,
                                const std::vector<float>& phi,
                                const WLSParams& p,
                                std::vector<float>& outGrad);

    /// 计算非结构网格的自适应 AWLS 梯度。
    ///
    /// 与传统 WLS 相比，这个版本会额外利用：
    /// - 局部主方向框架 `frames`
    /// - 局部维度标签 `dimTags`
    /// - 局部质量估计 `quality`
    /// - 局部平均邻距 `meanNeighborDistance`
    ///
    /// 其核心目的是在曲面样本、近共面样本和病态邻域上提升稳定性。
    bool computeUnstructuredAdaptiveWLS(const std::vector<float>& positions,
                                        const std::vector<int>& offsets,
                                        const std::vector<int>& neighbors,
                                        const std::vector<float>& phi,
                                        const std::vector<float>& frames,
                                        const std::vector<std::uint32_t>& dimTags,
                                        const std::vector<float>& quality,
                                        const std::vector<float>& meanNeighborDistance,
                                        const WLSParams& p,
                                        std::vector<float>& outGrad);

    /// 是否启用 GPU 计时。
    ///
    /// 开启后，每次计算会在 OpenGL 查询对象中记录 shader 执行耗时，
    /// 供测试程序统计性能。
    void setEnableGpuTiming(bool on);

    /// 读取最近一次 GPU 计算耗时，单位毫秒。
    double getLastGpuTimeMs() const;

private:
    std::string shaderDir;     ///< 着色器目录

    GLuint progRegular = 0;    ///< 规则网格 FD 对应的计算着色器程序
    GLuint progWLS = 0;        ///< 传统 WLS 对应的计算着色器程序
    GLuint progAdaptiveWLS = 0;///< 自适应 AWLS 对应的计算着色器程序

    /// GPU 计算中复用的 SSBO。
    ///
    /// 不同算法对这些缓冲区的绑定语义不同，但整体思路一致：
    /// 把位置、邻域、输入场和输出梯度都表示成连续缓冲区传给 shader。
    ///
    /// 其中 `ssbo5 ~ ssbo8` 主要服务于自适应 AWLS，
    /// 用来传递局部坐标框架、维度标签和质量估计等辅助信息。
    GLuint ssbo0 = 0;
    GLuint ssbo1 = 0;
    GLuint ssbo2 = 0;
    GLuint ssbo3 = 0;
    GLuint ssbo4 = 0;
    GLuint ssbo5 = 0;
    GLuint ssbo6 = 0;
    GLuint ssbo7 = 0;
    GLuint ssbo8 = 0;

    /// 从文件读取并编译单个计算着色器，成功时返回程序对象 ID。
    GLuint buildComputeFromFile(const std::string& path);

    /// 保证指定 SSBO 已创建且容量足够。
    ///
    /// 若当前缓冲不存在或空间不足，则重新分配。
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);

    bool enableGpuTiming = false; ///< 是否启用 GPU 计时
    GLuint timeQuery = 0;         ///< OpenGL 时间查询对象
    double lastGpuTimeMs = 0.0;   ///< 最近一次 GPU 计算耗时
};
