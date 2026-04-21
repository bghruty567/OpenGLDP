#include "GLFilterEngine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
// 读取 shader 源码文本。
//
// 这里只做最轻量的文件读取，具体的“版本检查、编译失败日志、链接失败日志”
// 都交给 buildComputeFromFile 统一处理。
std::string readFileText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}

GLFilterEngine::GLFilterEngine() {}
GLFilterEngine::~GLFilterEngine() { release(); }

bool GLFilterEngine::setShaderDir(const std::string& dir)
{
    shaderDir = dir;
    return true;
}

void GLFilterEngine::ensureBuffer(GLuint& id, size_t bytes, GLenum usage)
{
    // OpenGL 不允许申请 0 字节缓冲。
    // 某些极端情况下输入为空时，也给一个最小占位容量。
    if (bytes == 0) {
        bytes = 4;
    }
    if (id == 0) {
        glGenBuffers(1, &id);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, usage);
}

GLuint GLFilterEngine::buildComputeFromFile(const std::string& path)
{
    // 计算着色器依赖 OpenGL 4.3+。
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        std::cerr << "[GL] Context version < 4.3, cannot create compute shader\n";
        return 0;
    }

    const std::string src = readFileText(path);
    if (src.empty()) {
        std::cerr << "[GL] Shader file empty or missing: " << path << "\n";
        return 0;
    }

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    const char* csrc = src.c_str();
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char logBuf[2048] = {};
        glGetShaderInfoLog(sh, sizeof(logBuf), nullptr, logBuf);
        std::cerr << "[GL] Compute shader compile failed: " << path << "\n" << logBuf << "\n";
        glDeleteShader(sh);
        return 0;
    }

    GLuint pr = glCreateProgram();
    glAttachShader(pr, sh);
    glLinkProgram(pr);
    glDeleteShader(sh);

    glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok) {
        char logBuf[2048] = {};
        glGetProgramInfoLog(pr, sizeof(logBuf), nullptr, logBuf);
        std::cerr << "[GL] Program link failed: " << path << "\n" << logBuf << "\n";
        glDeleteProgram(pr);
        return 0;
    }

    return pr;
}

bool GLFilterEngine::init()
{
    if (shaderDir.empty()) {
        return false;
    }

    // 数据优化模块只依赖两份 compute shader：
    // 1. Bilateral.glsl：图双边滤波；
    // 2. MultiScaleFuse.glsl：多尺度细节融合。
    if (progBilateral == 0) {
        progBilateral = buildComputeFromFile(shaderDir + "\\Bilateral.glsl");
    }
    if (progFusion == 0) {
        progFusion = buildComputeFromFile(shaderDir + "\\MultiScaleFuse.glsl");
    }

    return progBilateral != 0 && progFusion != 0;
}

void GLFilterEngine::release()
{
    if (progBilateral) {
        glDeleteProgram(progBilateral);
        progBilateral = 0;
    }
    if (progFusion) {
        glDeleteProgram(progFusion);
        progFusion = 0;
    }

    if (ssbo0) { glDeleteBuffers(1, &ssbo0); ssbo0 = 0; }
    if (ssbo1) { glDeleteBuffers(1, &ssbo1); ssbo1 = 0; }
    if (ssbo2) { glDeleteBuffers(1, &ssbo2); ssbo2 = 0; }
    if (ssbo3) { glDeleteBuffers(1, &ssbo3); ssbo3 = 0; }
    if (ssbo4) { glDeleteBuffers(1, &ssbo4); ssbo4 = 0; }

    if (timeQuery) {
        glDeleteQueries(1, &timeQuery);
        timeQuery = 0;
    }
}

bool GLFilterEngine::bilateralGraph(const std::vector<float>& positions,
                                    const std::vector<int>& offsets,
                                    const std::vector<int>& neighbors,
                                    const std::vector<float>& values,
                                    const BilateralParams& p,
                                    std::vector<float>& outValues)
{
    // 双边滤波阶段的完整职责：
    // 1. 上传“几何 + 邻接图 + 输入字段”；
    // 2. 在每个样本的邻域内做空间权重和数值权重的联合加权；
    // 3. 读回平滑后的结果。
    const size_t np = positions.size() / 3;
    if (progBilateral == 0 || np == 0) {
        return false;
    }
    if (positions.size() != np * 3) {
        return false;
    }
    if (offsets.size() != np + 1) {
        return false;
    }
    if (values.empty() || (values.size() % np) != 0) {
        return false;
    }

    const int comps = static_cast<int>(values.size() / np);
    outValues.resize(values.size());

    // 位置统一打包成 vec4，最后一维留作填充。
    // 这样 shader 端按 vec4 读取时不会踩到对齐问题。
    std::vector<float> pos4(np * 4, 0.0f);
    for (size_t i = 0; i < np; ++i) {
        pos4[i * 4 + 0] = positions[i * 3 + 0];
        pos4[i * 4 + 1] = positions[i * 3 + 1];
        pos4[i * 4 + 2] = positions[i * 3 + 2];
    }

    ensureBuffer(ssbo0, pos4.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pos4.size() * sizeof(float), pos4.data());

    ensureBuffer(ssbo1, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, offsets.size() * sizeof(int), offsets.data());

    ensureBuffer(ssbo2, neighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    if (!neighbors.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, neighbors.size() * sizeof(int), neighbors.data());
    }

    ensureBuffer(ssbo3, values.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, values.size() * sizeof(float), values.data());

    ensureBuffer(ssbo4, outValues.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progBilateral);
    glUniform1i(glGetUniformLocation(progBilateral, "uN"), static_cast<int>(np));
    glUniform1i(glGetUniformLocation(progBilateral, "uNumComponents"), comps);
    glUniform1f(glGetUniformLocation(progBilateral, "uSpatialSigma"), std::max(1e-6f, p.spatialSigma));
    glUniform1f(glGetUniformLocation(progBilateral, "uRangeSigma"), std::max(1e-6f, p.rangeSigma));

    // shader 里 local_size_x 固定为 256，所以这里按一维工作网格分派即可。
    const GLuint gx = static_cast<GLuint>((np + 255) / 256);

    if (enableGpuTiming) {
        if (timeQuery == 0) {
            glGenQueries(1, &timeQuery);
        }
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
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outValues.size() * sizeof(float), outValues.data());

    return true;
}

bool GLFilterEngine::fuseMultiScale(const std::vector<float>& base,
                                    const std::vector<float>& detail0,
                                    const std::vector<float>& detail1,
                                    const std::vector<float>& detail2,
                                    const FusionParams& p,
                                    std::vector<float>& outValues)
{
    // 融合阶段已经不关心网格拓扑，而是只关心四组等长数组：
    // 一个 base 层 + 三个 detail 层。
    // shader 会根据细节强度自动决定该加回多少 detail。
    if (progFusion == 0 || base.empty()) {
        return false;
    }
    if (detail0.size() != base.size() || detail1.size() != base.size() || detail2.size() != base.size()) {
        return false;
    }

    outValues.resize(base.size());

    ensureBuffer(ssbo0, base.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, base.size() * sizeof(float), base.data());

    ensureBuffer(ssbo1, detail0.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, detail0.size() * sizeof(float), detail0.data());

    ensureBuffer(ssbo2, detail1.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, detail1.size() * sizeof(float), detail1.data());

    ensureBuffer(ssbo3, detail2.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, detail2.size() * sizeof(float), detail2.data());

    ensureBuffer(ssbo4, outValues.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progFusion);
    glUniform1i(glGetUniformLocation(progFusion, "uTotalCount"), static_cast<int>(base.size()));
    glUniform1i(glGetUniformLocation(progFusion, "uLevelCount"), std::clamp(p.levelCount, 0, 3));
    glUniform1f(glGetUniformLocation(progFusion, "uEdgeSigma"), std::max(1e-6f, p.edgeSigma));
    glUniform3f(glGetUniformLocation(progFusion, "uDetailGains"),
                p.detailGains[0], p.detailGains[1], p.detailGains[2]);

    // 与双边滤波类似，融合也是每个标量样本独立执行。
    const GLuint gx = static_cast<GLuint>((base.size() + 255) / 256);

    if (enableGpuTiming) {
        if (timeQuery == 0) {
            glGenQueries(1, &timeQuery);
        }
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
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outValues.size() * sizeof(float), outValues.data());

    return true;
}

void GLFilterEngine::setEnableGpuTiming(bool on)
{
    enableGpuTiming = on;
    if (enableGpuTiming && timeQuery == 0) {
        glGenQueries(1, &timeQuery);
    }
}

double GLFilterEngine::getLastGpuTimeMs() const
{
    return lastGpuTimeMs;
}
