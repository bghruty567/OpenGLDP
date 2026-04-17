#include "GLGradientEngine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

/*
* @brief 浠庢枃浠朵腑璇诲彇鏂囨湰鍐呭
* @param p 鏂囦欢璺緞
*/
static std::string readFileText(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
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
    if (id == 0)
    {
        glGenBuffers(1, &id);//鍒涘缓鏂扮紦鍐插尯
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);//缁戝畾缂撳啿鍖哄璞″埌GL_SHADER_STORAGE_BUFFER鐩爣
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, usage);//鍒嗛厤鎴栭噸鏂板垎閰嶇紦鍐插尯鏁版嵁瀛樺偍
}

GLuint GLGradientEngine::buildComputeFromFile(const std::string& path)
{
    //妫€鏌penGL涓婁笅鏂囩増鏈紝纭繚鏀寔璁＄畻鐫€鑹插櫒
    GLint major = 0;
    GLint minor = 0;

    glGetIntegerv(GL_MAJOR_VERSION, &major);//鑾峰彇OpenGL涓荤増鏈彿
    glGetIntegerv(GL_MINOR_VERSION, &minor);//鑾峰彇OpenGL娆＄増鏈彿

    if (major < 4 || (major == 4 && minor < 3))
    {
        std::cerr << "[GL] Context version "
                  << major << "." << minor
                  << " < 4.3, cannot create compute shader\n";
        return 0;
    }

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);//鍒涘缓璁＄畻鐫€鑹插櫒瀵硅薄
    GLenum err = glGetError();

    if (sh == 0 || err != GL_NO_ERROR)
    {
        std::cerr << "[GL] glCreateShader(COMPUTE) failed. sh="
                  << sh << " glError=0x" << std::hex << err << std::dec << "\n";
        return 0;
    }

    std::string src = readFileText(path);//浠庢寚瀹氳矾寰勮鍙栫潃鑹插櫒婧愪唬鐮佹枃鏈?
    if (src.empty())
    {
        std::cerr << "[GL] shader source is empty: " << path << "\n";
        glDeleteShader(sh);
        return 0;
    }

    const char* c = src.c_str();

    glShaderSource(sh, 1, &c, nullptr);//璁剧疆鐫€鑹插櫒婧愪唬鐮?
    glCompileShader(sh);//缂栬瘧鐫€鑹插櫒

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);//妫€鏌ョ潃鑹插櫒缂栬瘧鏄惁鎴愬姛
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

    GLuint pr = glCreateProgram();//鍒涘缓鐫€鑹插櫒绋嬪簭瀵硅薄

    glAttachShader(pr, sh);//灏嗙紪璇戝ソ鐨勭潃鑹插櫒瀵硅薄闄勫姞鍒扮▼搴忓璞′笂
    glLinkProgram(pr);//閾炬帴绋嬪簭瀵硅薄
    glDeleteShader(sh);//閾炬帴瀹屾垚鍚庡彲浠ュ垹闄ょ潃鑹插櫒瀵硅薄

    glGetProgramiv(pr, GL_LINK_STATUS, &ok);//妫€鏌ョ▼搴忛摼鎺ユ槸鍚︽垚鍔?
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

    if (progSparseReconstruct == 0)
    {
        progSparseReconstruct = buildComputeFromFile(shaderDir + "\\SparseReconstruct.glsl");
    }

    if (progSparseGradient == 0)
    {
        progSparseGradient = buildComputeFromFile(shaderDir + "\\SparseGradient.glsl");
    }

    return progRegular != 0 &&
           progWLS != 0 &&
           progAdaptiveWLS != 0 &&
           progSparseReconstruct != 0 &&
           progSparseGradient != 0;
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
    if (progSparseReconstruct)
    {
        glDeleteProgram(progSparseReconstruct);
        progSparseReconstruct = 0;
    }
    if (progSparseGradient)
    {
        glDeleteProgram(progSparseGradient);
        progSparseGradient = 0;
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
    int64_t n = int64_t(p.dims[0]) * p.dims[1] * p.dims[2];//璁＄畻缃戞牸鐐规€绘暟
    if (n <= 0) return false;
    if (positions.size() != size_t(n) * 3) return false;
    if (values.empty() || (int64_t)(values.size() % n) != 0) return false;

    int comps = int((int64_t)values.size() / n);//纭畾鏁版嵁鍒嗛噺鏁?
    outGrad.resize(size_t(n) * size_t(3 * comps));//璋冩暣杈撳嚭姊害鏁扮粍澶у皬

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
    GLint ld = glGetUniformLocation(progRegular, "uDims");//鑾峰彇鐫€鑹插櫒涓璾Dims鍙橀噺鐨勪綅缃?
    GLint lc = glGetUniformLocation(progRegular, "uNumComponents");//鑾峰彇鐫€鑹插櫒涓璾NumComponents鍙橀噺鐨勪綅缃?
    glUniform3ui(ld, (GLuint)p.dims[0], (GLuint)p.dims[1], (GLuint)p.dims[2]);//灏嗙綉鏍肩淮搴︿俊鎭紶閫掔粰鐫€鑹插櫒
    glUniform1i(lc, comps);//灏嗘暟鎹垎閲忔暟浼犻€掔粰鐫€鑹插櫒

    GLuint gx = (p.dims[0] + 7) / 8;
    GLuint gy = (p.dims[1] + 7) / 8;
    GLuint gz = (p.dims[2] + 7) / 8;
    if (enableGpuTiming) {
        if (timeQuery == 0) glGenQueries(1, &timeQuery);
        glBeginQuery(GL_TIME_ELAPSED, timeQuery);
    }

    glDispatchCompute(gx, gy, gz);//鍚姩璁＄畻鐫€鑹插櫒
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    if (enableGpuTiming) {
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
        lastGpuTimeMs = static_cast<double>(ns) / 1e6; // ns -> ms
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());//浠嶨PU缂撳啿鍖鸿鍙栬绠楃粨鏋?
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

    // WLS璁＄畻鐫€鑹插櫒涓瘡涓偣鐨勪綅缃娇鐢╲ec4鏍煎紡瀛樺偍锛岀鍥涘垎閲忓～鍏呬负0锛屼互婊¤冻std140甯冨眬瑕佹眰
    std::vector<float> pos4(np * 4);
    for (size_t i = 0; i < np; ++i)
    {
        pos4[i * 4 + 0] = positions[i * 3 + 0];
        pos4[i * 4 + 1] = positions[i * 3 + 1];
        pos4[i * 4 + 2] = positions[i * 3 + 2];
        pos4[i * 4 + 3] = 0.0f;
    }

    ensureBuffer(ssbo0, pos4.size() * sizeof(float), GL_DYNAMIC_DRAW);//鐐?
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pos4.size() * sizeof(float), pos4.data());

    ensureBuffer(ssbo1, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);//閭诲煙鍋忕Щ
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    offsets.size() * sizeof(int), offsets.data());

    ensureBuffer(ssbo2, neighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);//閭诲煙鐐圭储寮?
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    neighbors.size() * sizeof(int), neighbors.data());

    ensureBuffer(ssbo3, phi.size() * sizeof(float), GL_DYNAMIC_DRAW);//杈撳叆鏁版嵁
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

    glUniform1i(luN, (int)np);//灏嗙偣鏁颁紶閫掔粰鐫€鑹插櫒
    glUniform1f(luE, p.wExponent);//灏嗘潈閲嶆寚鏁颁紶閫掔粰鐫€鑹插櫒
    glUniform1f(luL, p.lambda);//灏嗘鍒欏寲鍙傛暟浼犻€掔粰鐫€鑹插櫒
    glUniform1i(luC, comps);//灏嗘暟鎹垎閲忔暟浼犻€掔粰鐫€鑹插櫒

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

bool GLGradientEngine::reconstructSparseValues(const std::vector<int>& offsets,
                                               const std::vector<int>& sourceIndices,
                                               const std::vector<float>& weights,
                                               int sourceTupleCount,
                                               const std::vector<float>& sourceValues,
                                               std::vector<float>& outValues)
{
    if (progSparseReconstruct == 0 || sourceTupleCount <= 0) {
        return false;
    }

    const size_t targetCount = offsets.empty() ? 0 : (offsets.size() - 1);
    if (targetCount == 0 ||
        offsets.back() < 0 ||
        static_cast<size_t>(offsets.back()) > sourceIndices.size() ||
        sourceIndices.size() != weights.size() ||
        sourceValues.empty() ||
        (sourceValues.size() % static_cast<size_t>(sourceTupleCount)) != 0) {
        return false;
    }

    const int comps = static_cast<int>(sourceValues.size() / static_cast<size_t>(sourceTupleCount));
    outValues.assign(targetCount * static_cast<size_t>(comps), 0.0f);

    ensureBuffer(ssbo0, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, offsets.size() * sizeof(int), offsets.data());

    ensureBuffer(ssbo1, sourceIndices.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sourceIndices.size() * sizeof(int), sourceIndices.data());

    ensureBuffer(ssbo2, weights.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, weights.size() * sizeof(float), weights.data());

    ensureBuffer(ssbo3, sourceValues.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sourceValues.size() * sizeof(float), sourceValues.data());

    ensureBuffer(ssbo4, outValues.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progSparseReconstruct);
    glUniform1i(glGetUniformLocation(progSparseReconstruct, "uTargetCount"), static_cast<int>(targetCount));
    glUniform1i(glGetUniformLocation(progSparseReconstruct, "uSourceCount"), sourceTupleCount);
    glUniform1i(glGetUniformLocation(progSparseReconstruct, "uNumComponents"), comps);

    const GLuint gx = static_cast<GLuint>((targetCount + 255u) / 256u);
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
    } else {
        lastGpuTimeMs = 0.0;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outValues.size() * sizeof(float), outValues.data());
    return true;
}

bool GLGradientEngine::applySparseGradientOperator(const std::vector<int>& offsets,
                                                   const std::vector<int>& sourceIndices,
                                                   const std::vector<float>& coeffs4,
                                                   int sourceTupleCount,
                                                   const std::vector<float>& sourceValues,
                                                   std::vector<float>& outGrad)
{
    if (progSparseGradient == 0 || sourceTupleCount <= 0) {
        return false;
    }

    const size_t targetCount = offsets.empty() ? 0 : (offsets.size() - 1);
    if (targetCount == 0 ||
        offsets.back() < 0 ||
        static_cast<size_t>(offsets.back()) > sourceIndices.size() ||
        coeffs4.size() != sourceIndices.size() * 4u ||
        sourceValues.empty() ||
        (sourceValues.size() % static_cast<size_t>(sourceTupleCount)) != 0) {
        return false;
    }

    const int comps = static_cast<int>(sourceValues.size() / static_cast<size_t>(sourceTupleCount));
    outGrad.assign(targetCount * static_cast<size_t>(3 * comps), 0.0f);

    ensureBuffer(ssbo0, offsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, offsets.size() * sizeof(int), offsets.data());

    ensureBuffer(ssbo1, sourceIndices.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sourceIndices.size() * sizeof(int), sourceIndices.data());

    ensureBuffer(ssbo2, coeffs4.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, coeffs4.size() * sizeof(float), coeffs4.data());

    ensureBuffer(ssbo3, sourceValues.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sourceValues.size() * sizeof(float), sourceValues.data());

    ensureBuffer(ssbo4, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progSparseGradient);
    glUniform1i(glGetUniformLocation(progSparseGradient, "uTargetCount"), static_cast<int>(targetCount));
    glUniform1i(glGetUniformLocation(progSparseGradient, "uSourceCount"), sourceTupleCount);
    glUniform1i(glGetUniformLocation(progSparseGradient, "uNumComponents"), comps);

    const GLuint gx = static_cast<GLuint>((targetCount + 255u) / 256u);
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
    } else {
        lastGpuTimeMs = 0.0;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
    return true;
}

void GLGradientEngine::setEnableGpuTiming(bool on)
{
    enableGpuTiming = on;
    //濡傛灉鍚敤GPU璁℃椂浣嗘煡璇㈠璞″皻鏈垱寤猴紝鍒欏垱寤轰竴涓柊鐨勬煡璇㈠璞?
    if (enableGpuTiming && timeQuery == 0) {
        glGenQueries(1, &timeQuery);
    }
}

double GLGradientEngine::getLastGpuTimeMs() const
{
    return lastGpuTimeMs;
}
