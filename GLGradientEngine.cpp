#include "GLGradientEngine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
// 读取 compute shader 源码文本。
//
// 梯度引擎的三个 GPU 算法（FD / WLS / AWLS）都会走这里读取文件，
// 便于后续统一处理编译和链接错误。
static std::string readFileText(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}

GLGradientEngine::GLGradientEngine() {}
GLGradientEngine::~GLGradientEngine() { release(); }

bool GLGradientEngine::setShaderDir(const std::string& dir)
{
    shaderDir = dir;
    return true;
}

void GLGradientEngine::ensureBuffer(GLuint& id, size_t bytes, GLenum usage)
{
    // 统一封装 SSBO 的创建和重分配逻辑。
    // 这样三条算法路径只需要关注“我要传什么数据”，
    // 不用每次都重复写 glGenBuffers / glBufferData。
    if (id == 0)
    {
        glGenBuffers(1, &id);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, usage);
}

GLuint GLGradientEngine::buildComputeFromFile(const std::string& path)
{
    // 计算着色器至少要求 OpenGL 4.3。
    GLint major = 0;
    GLint minor = 0;

    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major < 4 || (major == 4 && minor < 3))
    {
        std::cerr << "[GL] Context version "
                  << major << "." << minor
                  << " < 4.3, cannot create compute shader\n";
        return 0;
    }

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    GLenum err = glGetError();

    if (sh == 0 || err != GL_NO_ERROR)
    {
        std::cerr << "[GL] glCreateShader(COMPUTE) failed. sh="
                  << sh << " glError=0x" << std::hex << err << std::dec << "\n";
        return 0;
    }

    std::string src = readFileText(path);
    if (src.empty())
    {
        std::cerr << "[GL] shader source is empty: " << path << "\n";
        glDeleteShader(sh);
        return 0;
    }

    const char* c = src.c_str();

    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(std::max(0, logLen)), '\0');
        if (logLen > 1) {
            glGetShaderInfoLog(sh, logLen, nullptr, log.data());
        }
        std::cerr << "[GL] compute shader compile failed: " << path << "\n" << log << "\n";
        glDeleteShader(sh);
        return 0;
    }

    GLuint pr = glCreateProgram();

    glAttachShader(pr, sh);
    glLinkProgram(pr);
    glDeleteShader(sh);

    glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        GLint logLen = 0;
        glGetProgramiv(pr, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(std::max(0, logLen)), '\0');
        if (logLen > 1) {
            glGetProgramInfoLog(pr, logLen, nullptr, log.data());
        }
        std::cerr << "[GL] compute shader link failed: " << path << "\n" << log << "\n";
        glDeleteProgram(pr);
        return 0;
    }

    return pr;
}

bool GLGradientEngine::init()
{
    if (shaderDir.empty())
        return false;

    // 梯度模块当前固定维护三份 compute shader：
    // 1. 规则网格有限差分；
    // 2. 传统三维 WLS；
    // 3. 带局部维度/质量信息的 AWLS。
    if (progRegular == 0)
    {
        progRegular = buildComputeFromFile(shaderDir + "\\FD.glsl");
    }

    if (progWLS == 0)
    {
        progWLS = buildComputeFromFile(shaderDir + "\\WLS.glsl");
    }

    if (progAdaptiveWLS == 0)
    {
        progAdaptiveWLS = buildComputeFromFile(shaderDir + "\\AdaptiveWLS.glsl");
    }

    return progRegular != 0 &&
           progWLS != 0 &&
           progAdaptiveWLS != 0;
}

void GLGradientEngine::release()
{
    if (progRegular)
    {
        glDeleteProgram(progRegular);
        progRegular = 0;
    }
    if (progWLS)
    {
        glDeleteProgram(progWLS);
        progWLS = 0;
    }
    if (progAdaptiveWLS)
    {
        glDeleteProgram(progAdaptiveWLS);
        progAdaptiveWLS = 0;
    }
    if (ssbo0) { glDeleteBuffers(1, &ssbo0); ssbo0 = 0; }
    if (ssbo1) { glDeleteBuffers(1, &ssbo1); ssbo1 = 0; }
    if (ssbo2) { glDeleteBuffers(1, &ssbo2); ssbo2 = 0; }
    if (ssbo3) { glDeleteBuffers(1, &ssbo3); ssbo3 = 0; }
    if (ssbo4) { glDeleteBuffers(1, &ssbo4); ssbo4 = 0; }
    if (ssbo5) { glDeleteBuffers(1, &ssbo5); ssbo5 = 0; }
    if (ssbo6) { glDeleteBuffers(1, &ssbo6); ssbo6 = 0; }
    if (ssbo7) { glDeleteBuffers(1, &ssbo7); ssbo7 = 0; }
    if (ssbo8) { glDeleteBuffers(1, &ssbo8); ssbo8 = 0; }
}

bool GLGradientEngine::computeRegularFD(const std::vector<float>& positions,
                                        const std::vector<float>& values,
                                        const RegularParams& p,
                                        std::vector<float>& outGrad)
{
    if (progRegular == 0) return false;
    // 规则网格上，样本天然按三维索引组织，
    // shader 可以直接按 (i,j,k) 找前后邻居做有限差分。
    int64_t n = int64_t(p.dims[0]) * p.dims[1] * p.dims[2];
    if (n <= 0) return false;
    if (positions.size() != size_t(n) * 3) return false;
    if (values.empty() || (int64_t)(values.size() % n) != 0) return false;

    int comps = int((int64_t)values.size() / n);
    outGrad.resize(size_t(n) * size_t(3 * comps));

    ensureBuffer(ssbo0, positions.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, positions.size() * sizeof(float), positions.data());

    ensureBuffer(ssbo1, values.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, values.size() * sizeof(float), values.data());

    ensureBuffer(ssbo2, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);

    glUseProgram(progRegular);
    GLint ld = glGetUniformLocation(progRegular, "uDims");
    GLint lc = glGetUniformLocation(progRegular, "uNumComponents");
    glUniform3ui(ld, (GLuint)p.dims[0], (GLuint)p.dims[1], (GLuint)p.dims[2]);
    glUniform1i(lc, comps);

    GLuint gx = (p.dims[0] + 7) / 8;
    GLuint gy = (p.dims[1] + 7) / 8;
    GLuint gz = (p.dims[2] + 7) / 8;
    if (enableGpuTiming) {
        if (timeQuery == 0) glGenQueries(1, &timeQuery);
        glBeginQuery(GL_TIME_ELAPSED, timeQuery);
    }

    glDispatchCompute(gx, gy, gz);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    if (enableGpuTiming) {
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
        lastGpuTimeMs = static_cast<double>(ns) / 1e6; // ns -> ms
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
    return true;
}

bool GLGradientEngine::computeUnstructuredWLS(const std::vector<float>& positions,
                                              const std::vector<int>& offsets,
                                              const std::vector<int>& neighbors,
                                              const std::vector<float>& phi,
                                              const WLSParams& p,
                                              std::vector<float>& outGrad)
{
    size_t np = positions.size() / 3;
    if (np == 0)
        return false;

    if (offsets.size() != np + 1)
        return false;

    if (progWLS == 0)
        return false;

    if (phi.empty() || (phi.size() % np) != 0)
        return false;

    int comps = int(phi.size() / np);

    outGrad.resize(np * size_t(3 * comps));

    // WLS/AWLS 的 shader 都按 vec4 方式读取位置。
    // 这里显式把 [x,y,z] 打包成 [x,y,z,0]，避免布局对齐带来的歧义。
    std::vector<float> pos4(np * 4);
    for (size_t i = 0; i < np; ++i)
    {
        pos4[i * 4 + 0] = positions[i * 3 + 0];
        pos4[i * 4 + 1] = positions[i * 3 + 1];
        pos4[i * 4 + 2] = positions[i * 3 + 2];
        pos4[i * 4 + 3] = 0.0f;
    }

    // ssbo0~ssbo4 是传统 WLS 的核心输入输出：
    // 位置、邻域偏移、邻域索引、输入场、输出梯度。
    ensureBuffer(ssbo0, pos4.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pos4.size() * sizeof(float), pos4.data());

    ensureBuffer(ssbo1, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    offsets.size() * sizeof(int), offsets.data());

    ensureBuffer(ssbo2, neighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    neighbors.size() * sizeof(int), neighbors.data());

    ensureBuffer(ssbo3, phi.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    phi.size() * sizeof(float), phi.data());

    ensureBuffer(ssbo4, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progWLS);

    GLint luN = glGetUniformLocation(progWLS, "uN");
    GLint luE = glGetUniformLocation(progWLS, "uWExp");
    GLint luL = glGetUniformLocation(progWLS, "uLambda");
    GLint luC = glGetUniformLocation(progWLS, "uNumComponents");

    glUniform1i(luN, (int)np);
    glUniform1f(luE, p.wExponent);
    glUniform1f(luL, p.lambda);
    glUniform1i(luC, comps);

    GLuint gx = (GLuint)((np + 255) / 256);

    if (enableGpuTiming) {
        if (timeQuery == 0) glGenQueries(1, &timeQuery);
        glBeginQuery(GL_TIME_ELAPSED, timeQuery);
    }

    glDispatchCompute(gx, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    if (enableGpuTiming) {
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
        lastGpuTimeMs = static_cast<double>(ns) / 1e6; // ns -> ms
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       outGrad.size() * sizeof(float), outGrad.data());

    return true;
}

bool GLGradientEngine::computeUnstructuredAdaptiveWLS(const std::vector<float>& positions,
                                                      const std::vector<int>& offsets,
                                                      const std::vector<int>& neighbors,
                                                      const std::vector<float>& phi,
                                                      const std::vector<float>& frames,
                                                      const std::vector<std::uint32_t>& dimTags,
                                                      const std::vector<float>& quality,
                                                      const std::vector<float>& meanNeighborDistance,
                                                      const WLSParams& p,
                                                      std::vector<float>& outGrad)
{
    const size_t np = positions.size() / 3;
    if (np == 0) {
        return false;
    }
    if (offsets.size() != np + 1) {
        return false;
    }
    if (phi.empty() || (phi.size() % np) != 0) {
        return false;
    }
    if (frames.size() != np * 9 || dimTags.size() != np || quality.size() != np || meanNeighborDistance.size() != np) {
        return false;
    }
    if (progAdaptiveWLS == 0) {
        return false;
    }

    const int comps = int(phi.size() / np);
    outGrad.resize(np * size_t(3 * comps));

    // AWLS 在普通 WLS 的基础上额外引入四类辅助信息：
    // 1. frames：局部主方向基；
    // 2. dimTags：局部更像线/面/体；
    // 3. quality：邻域质量评分；
    // 4. meanNeighborDistance：局部平均邻距。
    std::vector<float> pos4(np * 4);
    for (size_t i = 0; i < np; ++i)
    {
        pos4[i * 4 + 0] = positions[i * 3 + 0];
        pos4[i * 4 + 1] = positions[i * 3 + 1];
        pos4[i * 4 + 2] = positions[i * 3 + 2];
        pos4[i * 4 + 3] = 0.0f;
    }

    ensureBuffer(ssbo0, pos4.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pos4.size() * sizeof(float), pos4.data());

    ensureBuffer(ssbo1, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, offsets.size() * sizeof(int), offsets.data());

    ensureBuffer(ssbo2, neighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, neighbors.size() * sizeof(int), neighbors.data());

    ensureBuffer(ssbo3, phi.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, phi.size() * sizeof(float), phi.data());

    ensureBuffer(ssbo4, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);
    ensureBuffer(ssbo5, frames.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo5);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, frames.size() * sizeof(float), frames.data());

    ensureBuffer(ssbo6, dimTags.size() * sizeof(std::uint32_t), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo6);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, dimTags.size() * sizeof(std::uint32_t), dimTags.data());

    ensureBuffer(ssbo7, quality.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo7);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, quality.size() * sizeof(float), quality.data());

    ensureBuffer(ssbo8, meanNeighborDistance.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo8);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, meanNeighborDistance.size() * sizeof(float), meanNeighborDistance.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo5);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, ssbo6);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, ssbo7);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, ssbo8);

    glUseProgram(progAdaptiveWLS);

    glUniform1i(glGetUniformLocation(progAdaptiveWLS, "uN"), static_cast<int>(np));
    glUniform1f(glGetUniformLocation(progAdaptiveWLS, "uWExp"), p.wExponent);
    glUniform1f(glGetUniformLocation(progAdaptiveWLS, "uLambda"), p.lambda);
    glUniform1i(glGetUniformLocation(progAdaptiveWLS, "uNumComponents"), comps);
    glUniform1f(glGetUniformLocation(progAdaptiveWLS, "uPlaneEigenRatio"), p.planeEigenRatio);
    glUniform1f(glGetUniformLocation(progAdaptiveWLS, "uLineEigenRatio"), p.lineEigenRatio);
    glUniform1f(glGetUniformLocation(progAdaptiveWLS, "uLambdaAmplify"), p.lambdaAmplify);
    glUniform1i(glGetUniformLocation(progAdaptiveWLS, "uEnableAdaptiveDimension"), p.enableAdaptiveDimension);
    glUniform1i(glGetUniformLocation(progAdaptiveWLS, "uEnableAdaptiveRegularization"), p.enableAdaptiveRegularization);

    // AWLS 仍然保持“一样本一个线程”的执行模式。
    const GLuint gx = (GLuint)((np + 255) / 256);
    if (enableGpuTiming) {
        if (timeQuery == 0) glGenQueries(1, &timeQuery);
        glBeginQuery(GL_TIME_ELAPSED, timeQuery);
    }

    glDispatchCompute(gx, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    if (enableGpuTiming) {
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
        lastGpuTimeMs = static_cast<double>(ns) / 1e6;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
    return true;
}

void GLGradientEngine::setEnableGpuTiming(bool on)
{
    enableGpuTiming = on;
    // 如果第一次启用 GPU 计时，则顺便创建时间查询对象。
    if (enableGpuTiming && timeQuery == 0) {
        glGenQueries(1, &timeQuery);
    }
}

double GLGradientEngine::getLastGpuTimeMs() const
{
    return lastGpuTimeMs;
}
