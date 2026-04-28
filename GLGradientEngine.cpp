#include "GLGradientEngine.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
std::string readFileText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

double dispatchComputeAndMeasure(bool enableGpuTiming,
                                 GLuint& timeQuery,
                                 GLuint gx,
                                 GLuint gy = 1,
                                 GLuint gz = 1)
{
    if (enableGpuTiming) {
        if (timeQuery == 0) {
            glGenQueries(1, &timeQuery);
        }
        glBeginQuery(GL_TIME_ELAPSED, timeQuery);
    }

    glDispatchCompute(gx, gy, gz);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    if (!enableGpuTiming) {
        return 0.0;
    }

    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 ns = 0;
    glGetQueryObjectui64v(timeQuery, GL_QUERY_RESULT, &ns);
    return static_cast<double>(ns) / 1e6;
}

bool validateOffsets(const std::vector<int>& offsets, size_t valueCount)
{
    if (offsets.empty() || offsets.front() != 0) {
        return false;
    }

    int prev = 0;
    for (int off : offsets) {
        if (off < prev || static_cast<size_t>(off) > valueCount) {
            return false;
        }
        prev = off;
    }

    return static_cast<size_t>(offsets.back()) == valueCount;
}

std::vector<float> packPositionsToVec4(const std::vector<float>& positions)
{
    const size_t count = positions.size() / 3;
    std::vector<float> pos4(count * 4u, 0.0f);
    for (size_t i = 0; i < count; ++i) {
        pos4[i * 4u + 0u] = positions[i * 3u + 0u];
        pos4[i * 4u + 1u] = positions[i * 3u + 1u];
        pos4[i * 4u + 2u] = positions[i * 3u + 2u];
    }
    return pos4;
}

bool hasAnyFiniteNonZero(const std::vector<float>& values)
{
    for (float v : values) {
        if (std::isfinite(v) && v != 0.0f) {
            return true;
        }
    }
    return false;
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
    if (bytes == 0) {
        bytes = 4;
    }
    if (id == 0) {
        glGenBuffers(1, &id);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, usage);
}

GLuint GLGradientEngine::buildComputeFromFile(const std::string& path)
{
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major < 4 || (major == 4 && minor < 3)) {
        std::cerr << "[GL] Context version "
                  << major << "." << minor
                  << " < 4.3, cannot create compute shader\n";
        return 0;
    }

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    GLenum err = glGetError();
    if (sh == 0 || err != GL_NO_ERROR) {
        std::cerr << "[GL] glCreateShader(COMPUTE) failed. sh="
                  << sh << " glError=0x" << std::hex << err << std::dec << "\n";
        return 0;
    }

    const std::string src = readFileText(path);
    if (src.empty()) {
        std::cerr << "[GL] shader source is empty: " << path << "\n";
        glDeleteShader(sh);
        return 0;
    }

    const char* csrc = src.c_str();
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
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
    if (!ok) {
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
    if (shaderDir.empty()) {
        return false;
    }

    if (progRegular == 0) {
        progRegular = buildComputeFromFile(shaderDir + "\\FD.glsl");
    }
    if (progWLS == 0) {
        progWLS = buildComputeFromFile(shaderDir + "\\WLS.glsl");
    }
    if (progAdaptiveWLS == 0) {
        progAdaptiveWLS = buildComputeFromFile(shaderDir + "\\AdaptiveWLS.glsl");
    }
    if (progShapePoint == 0) {
        progShapePoint = buildComputeFromFile(shaderDir + "\\ShapePointGradient.glsl");
    }
    if (progCellToPoint == 0) {
        progCellToPoint = buildComputeFromFile(shaderDir + "\\CellDataToPointLift.glsl");
    }
    if (progShapeCell == 0) {
        progShapeCell = buildComputeFromFile(shaderDir + "\\ShapeCellGradient.glsl");
    }

    return progRegular != 0 &&
           progWLS != 0 &&
           progAdaptiveWLS != 0 &&
           progShapePoint != 0 &&
           progCellToPoint != 0 &&
           progShapeCell != 0;
}

void GLGradientEngine::release()
{
    if (progRegular) {
        glDeleteProgram(progRegular);
        progRegular = 0;
    }
    if (progWLS) {
        glDeleteProgram(progWLS);
        progWLS = 0;
    }
    if (progAdaptiveWLS) {
        glDeleteProgram(progAdaptiveWLS);
        progAdaptiveWLS = 0;
    }
    if (progShapePoint) {
        glDeleteProgram(progShapePoint);
        progShapePoint = 0;
    }
    if (progCellToPoint) {
        glDeleteProgram(progCellToPoint);
        progCellToPoint = 0;
    }
    if (progShapeCell) {
        glDeleteProgram(progShapeCell);
        progShapeCell = 0;
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

    if (timeQuery) {
        glDeleteQueries(1, &timeQuery);
        timeQuery = 0;
    }
}

bool GLGradientEngine::computeRegularFD(const std::vector<float>& positions,
                                        const std::vector<float>& values,
                                        const RegularParams& p,
                                        std::vector<float>& outGrad)
{
    if (progRegular == 0) {
        return false;
    }

    const int64_t n = int64_t(p.dims[0]) * p.dims[1] * p.dims[2];
    if (n <= 0) {
        return false;
    }
    if (positions.size() != static_cast<size_t>(n) * 3u) {
        return false;
    }
    if (values.empty() || (int64_t(values.size()) % n) != 0) {
        return false;
    }

    const int comps = static_cast<int>(int64_t(values.size()) / n);
    outGrad.resize(static_cast<size_t>(n) * static_cast<size_t>(3 * comps));

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
    glUniform3ui(glGetUniformLocation(progRegular, "uDims"),
                 static_cast<GLuint>(p.dims[0]),
                 static_cast<GLuint>(p.dims[1]),
                 static_cast<GLuint>(p.dims[2]));
    glUniform1i(glGetUniformLocation(progRegular, "uNumComponents"), comps);

    const GLuint gx = static_cast<GLuint>((p.dims[0] + 7) / 8);
    const GLuint gy = static_cast<GLuint>((p.dims[1] + 7) / 8);
    const GLuint gz = static_cast<GLuint>((p.dims[2] + 7) / 8);

    lastGpuTimeMs = dispatchComputeAndMeasure(enableGpuTiming, timeQuery, gx, gy, gz);

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
    const size_t np = positions.size() / 3u;
    if (progWLS == 0 || np == 0) {
        return false;
    }
    if (positions.size() != np * 3u) {
        return false;
    }
    if (offsets.size() != np + 1u || !validateOffsets(offsets, neighbors.size())) {
        return false;
    }
    if (phi.empty() || (phi.size() % np) != 0u) {
        return false;
    }

    const int comps = static_cast<int>(phi.size() / np);
    outGrad.resize(np * static_cast<size_t>(3 * comps));

    const std::vector<float> pos4 = packPositionsToVec4(positions);

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

    ensureBuffer(ssbo3, phi.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, phi.size() * sizeof(float), phi.data());

    ensureBuffer(ssbo4, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progWLS);
    glUniform1i(glGetUniformLocation(progWLS, "uN"), static_cast<int>(np));
    glUniform1f(glGetUniformLocation(progWLS, "uWExp"), p.wExponent);
    glUniform1f(glGetUniformLocation(progWLS, "uLambda"), p.lambda);
    glUniform1i(glGetUniformLocation(progWLS, "uNumComponents"), comps);

    const GLuint gx = static_cast<GLuint>((np + 255u) / 256u);
    lastGpuTimeMs = dispatchComputeAndMeasure(enableGpuTiming, timeQuery, gx);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
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
    const size_t np = positions.size() / 3u;
    if (progAdaptiveWLS == 0 || np == 0) {
        return false;
    }
    if (positions.size() != np * 3u) {
        return false;
    }
    if (offsets.size() != np + 1u || !validateOffsets(offsets, neighbors.size())) {
        return false;
    }
    if (phi.empty() || (phi.size() % np) != 0u) {
        return false;
    }
    if (frames.size() != np * 9u ||
        dimTags.size() != np ||
        quality.size() != np ||
        meanNeighborDistance.size() != np) {
        return false;
    }

    const int comps = static_cast<int>(phi.size() / np);
    outGrad.resize(np * static_cast<size_t>(3 * comps));

    const std::vector<float> pos4 = packPositionsToVec4(positions);

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

    ensureBuffer(ssbo3, phi.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, phi.size() * sizeof(float), phi.data());

    ensureBuffer(ssbo4, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    ensureBuffer(ssbo5, frames.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo5);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, frames.size() * sizeof(float), frames.data());

    ensureBuffer(ssbo6, dimTags.size() * sizeof(std::uint32_t), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo6);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    dimTags.size() * sizeof(std::uint32_t), dimTags.data());

    ensureBuffer(ssbo7, quality.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo7);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, quality.size() * sizeof(float), quality.data());

    ensureBuffer(ssbo8, meanNeighborDistance.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo8);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    meanNeighborDistance.size() * sizeof(float), meanNeighborDistance.data());

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

    const GLuint gx = static_cast<GLuint>((np + 255u) / 256u);
    lastGpuTimeMs = dispatchComputeAndMeasure(enableGpuTiming, timeQuery, gx);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
    return true;
}

bool GLGradientEngine::computeUnstructuredShapeFunctionPoint(const std::vector<float>& points,
                                                             const std::vector<int>& cellOffsets,
                                                             const std::vector<int>& cells,
                                                             const std::vector<int>& cellTypes,
                                                             const std::vector<int>& pointInCellOffsets,
                                                             const std::vector<int>& pointInCellNeighbors,
                                                             const std::vector<float>& pointValues,
                                                             std::vector<float>& outGrad)
{
    const size_t pointCount = points.size() / 3u;
    const size_t cellCount = cellTypes.size();
    if (progShapePoint == 0 || pointCount == 0 || cellCount == 0) {
        return false;
    }
    if (points.size() != pointCount * 3u) {
        return false;
    }
    if (cellOffsets.size() != cellCount + 1u || !validateOffsets(cellOffsets, cells.size())) {
        return false;
    }
    if (pointInCellOffsets.size() != pointCount + 1u ||
        !validateOffsets(pointInCellOffsets, pointInCellNeighbors.size())) {
        return false;
    }
    if (pointValues.empty() || (pointValues.size() % pointCount) != 0u) {
        return false;
    }

    const int comps = static_cast<int>(pointValues.size() / pointCount);
    outGrad.assign(pointCount * static_cast<size_t>(3 * comps), 0.0f);

    ensureBuffer(ssbo0, points.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, points.size() * sizeof(float), points.data());

    ensureBuffer(ssbo1, cellOffsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellOffsets.size() * sizeof(int), cellOffsets.data());

    ensureBuffer(ssbo2, cells.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    if (!cells.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cells.size() * sizeof(int), cells.data());
    }

    ensureBuffer(ssbo3, cellTypes.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellTypes.size() * sizeof(int), cellTypes.data());

    ensureBuffer(ssbo4, pointInCellOffsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pointInCellOffsets.size() * sizeof(int), pointInCellOffsets.data());

    ensureBuffer(ssbo5, pointInCellNeighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo5);
    if (!pointInCellNeighbors.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        pointInCellNeighbors.size() * sizeof(int), pointInCellNeighbors.data());
    }

    ensureBuffer(ssbo6, pointValues.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo6);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pointValues.size() * sizeof(float), pointValues.data());

    ensureBuffer(ssbo7, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo7);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    outGrad.size() * sizeof(float), outGrad.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo5);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, ssbo6);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, ssbo7);

    glUseProgram(progShapePoint);
    glUniform1i(glGetUniformLocation(progShapePoint, "uPointCount"), static_cast<int>(pointCount));
    glUniform1i(glGetUniformLocation(progShapePoint, "uCellCount"), static_cast<int>(cellCount));
    glUniform1i(glGetUniformLocation(progShapePoint, "uNumComponents"), comps);

    const GLuint gx = static_cast<GLuint>((pointCount + 255u) / 256u);
    lastGpuTimeMs = dispatchComputeAndMeasure(enableGpuTiming, timeQuery, gx);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo7);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());

    const double directGpuMs = lastGpuTimeMs;
    if (!hasAnyFiniteNonZero(outGrad)) {
        std::vector<float> liftedGrad;
        double fallbackGpuMs = 0.0;
        if (computePointGradientsViaCellLift(points,
                                             cellOffsets,
                                             cells,
                                             cellTypes,
                                             pointInCellOffsets,
                                             pointInCellNeighbors,
                                             pointValues,
                                             liftedGrad,
                                             fallbackGpuMs)) {
            outGrad.swap(liftedGrad);
            lastGpuTimeMs = directGpuMs + fallbackGpuMs;
            std::cerr << "[GL] ShapePointGradient returned an all-zero buffer; "
                         "falling back to ShapeCellGradient + CellDataToPointLift.\n";
        }
    }

    return true;
}

bool GLGradientEngine::computePointGradientsViaCellLift(const std::vector<float>& points,
                                                        const std::vector<int>& cellOffsets,
                                                        const std::vector<int>& cells,
                                                        const std::vector<int>& cellTypes,
                                                        const std::vector<int>& pointInCellOffsets,
                                                        const std::vector<int>& pointInCellNeighbors,
                                                        const std::vector<float>& pointValues,
                                                        std::vector<float>& outGrad,
                                                        double& gpuTimeMs)
{
    const size_t pointCount = points.size() / 3u;
    const size_t cellCount = cellTypes.size();
    if (progShapeCell == 0 || progCellToPoint == 0 || pointCount == 0 || cellCount == 0) {
        return false;
    }
    if (pointValues.empty() || (pointValues.size() % pointCount) != 0u) {
        return false;
    }

    const int comps = static_cast<int>(pointValues.size() / pointCount);
    const int gradComps = 3 * comps;
    const size_t cellGradSize = cellCount * static_cast<size_t>(gradComps);
    outGrad.assign(pointCount * static_cast<size_t>(gradComps), 0.0f);
    std::vector<float> zeroCellGrad(cellGradSize, 0.0f);
    gpuTimeMs = 0.0;

    ensureBuffer(ssbo0, points.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, points.size() * sizeof(float), points.data());

    ensureBuffer(ssbo1, cellOffsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellOffsets.size() * sizeof(int), cellOffsets.data());

    ensureBuffer(ssbo2, cells.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    if (!cells.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cells.size() * sizeof(int), cells.data());
    }

    ensureBuffer(ssbo3, cellTypes.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellTypes.size() * sizeof(int), cellTypes.data());

    ensureBuffer(ssbo4, pointValues.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pointValues.size() * sizeof(float), pointValues.data());

    ensureBuffer(ssbo5, zeroCellGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo5);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    zeroCellGrad.size() * sizeof(float), zeroCellGrad.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo5);

    glUseProgram(progShapeCell);
    glUniform1i(glGetUniformLocation(progShapeCell, "uPointCount"), static_cast<int>(pointCount));
    glUniform1i(glGetUniformLocation(progShapeCell, "uCellCount"), static_cast<int>(cellCount));
    glUniform1i(glGetUniformLocation(progShapeCell, "uNumComponents"), comps);

    gpuTimeMs += dispatchComputeAndMeasure(enableGpuTiming,
                                           timeQuery,
                                           static_cast<GLuint>((cellCount + 255u) / 256u));

    ensureBuffer(ssbo0, pointInCellOffsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pointInCellOffsets.size() * sizeof(int), pointInCellOffsets.data());

    ensureBuffer(ssbo1, pointInCellNeighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    if (!pointInCellNeighbors.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        pointInCellNeighbors.size() * sizeof(int), pointInCellNeighbors.data());
    }

    ensureBuffer(ssbo2, cellTypes.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellTypes.size() * sizeof(int), cellTypes.data());

    ensureBuffer(ssbo6, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo6);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    outGrad.size() * sizeof(float), outGrad.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo5);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo6);

    glUseProgram(progCellToPoint);
    glUniform1i(glGetUniformLocation(progCellToPoint, "uPointCount"), static_cast<int>(pointCount));
    glUniform1i(glGetUniformLocation(progCellToPoint, "uCellCount"), static_cast<int>(cellCount));
    glUniform1i(glGetUniformLocation(progCellToPoint, "uNumComponents"), gradComps);

    gpuTimeMs += dispatchComputeAndMeasure(enableGpuTiming,
                                           timeQuery,
                                           static_cast<GLuint>((pointCount + 255u) / 256u));

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo6);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
    return true;
}

bool GLGradientEngine::computeUnstructuredShapeFunctionCell(const std::vector<float>& points,
                                                            const std::vector<int>& cellOffsets,
                                                            const std::vector<int>& cells,
                                                            const std::vector<int>& cellTypes,
                                                            const std::vector<int>& pointInCellOffsets,
                                                            const std::vector<int>& pointInCellNeighbors,
                                                            const std::vector<float>& cellValues,
                                                            std::vector<float>& outGrad)
{
    const size_t pointCount = points.size() / 3u;
    const size_t cellCount = cellTypes.size();
    if (progCellToPoint == 0 || progShapeCell == 0 || pointCount == 0 || cellCount == 0) {
        return false;
    }
    if (points.size() != pointCount * 3u) {
        return false;
    }
    if (cellOffsets.size() != cellCount + 1u || !validateOffsets(cellOffsets, cells.size())) {
        return false;
    }
    if (pointInCellOffsets.size() != pointCount + 1u ||
        !validateOffsets(pointInCellOffsets, pointInCellNeighbors.size())) {
        return false;
    }
    if (cellValues.empty() || (cellValues.size() % cellCount) != 0u) {
        return false;
    }

    const int comps = static_cast<int>(cellValues.size() / cellCount);
    outGrad.assign(cellCount * static_cast<size_t>(3 * comps), 0.0f);
    const size_t liftedSize = pointCount * static_cast<size_t>(comps);
    double totalGpuMs = 0.0;

    ensureBuffer(ssbo0, pointInCellOffsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    pointInCellOffsets.size() * sizeof(int), pointInCellOffsets.data());

    ensureBuffer(ssbo1, pointInCellNeighbors.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    if (!pointInCellNeighbors.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        pointInCellNeighbors.size() * sizeof(int), pointInCellNeighbors.data());
    }

    ensureBuffer(ssbo2, cellTypes.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellTypes.size() * sizeof(int), cellTypes.data());

    ensureBuffer(ssbo3, cellValues.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellValues.size() * sizeof(float), cellValues.data());

    ensureBuffer(ssbo4, liftedSize * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);

    glUseProgram(progCellToPoint);
    glUniform1i(glGetUniformLocation(progCellToPoint, "uPointCount"), static_cast<int>(pointCount));
    glUniform1i(glGetUniformLocation(progCellToPoint, "uCellCount"), static_cast<int>(cellCount));
    glUniform1i(glGetUniformLocation(progCellToPoint, "uNumComponents"), comps);

    totalGpuMs += dispatchComputeAndMeasure(enableGpuTiming,
                                            timeQuery,
                                            static_cast<GLuint>((pointCount + 255u) / 256u));

    ensureBuffer(ssbo0, points.size() * sizeof(float), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo0);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, points.size() * sizeof(float), points.data());

    ensureBuffer(ssbo1, cellOffsets.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo1);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellOffsets.size() * sizeof(int), cellOffsets.data());

    ensureBuffer(ssbo2, cells.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo2);
    if (!cells.empty()) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cells.size() * sizeof(int), cells.data());
    }

    ensureBuffer(ssbo3, cellTypes.size() * sizeof(int), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo3);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cellTypes.size() * sizeof(int), cellTypes.data());

    ensureBuffer(ssbo5, outGrad.size() * sizeof(float), GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo2);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo3);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo4);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo5);

    glUseProgram(progShapeCell);
    glUniform1i(glGetUniformLocation(progShapeCell, "uPointCount"), static_cast<int>(pointCount));
    glUniform1i(glGetUniformLocation(progShapeCell, "uCellCount"), static_cast<int>(cellCount));
    glUniform1i(glGetUniformLocation(progShapeCell, "uNumComponents"), comps);

    totalGpuMs += dispatchComputeAndMeasure(enableGpuTiming,
                                            timeQuery,
                                            static_cast<GLuint>((cellCount + 255u) / 256u));

    lastGpuTimeMs = totalGpuMs;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo5);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, outGrad.size() * sizeof(float), outGrad.data());
    return true;
}

void GLGradientEngine::setEnableGpuTiming(bool on)
{
    enableGpuTiming = on;
    if (enableGpuTiming && timeQuery == 0) {
        glGenQueries(1, &timeQuery);
    }
}

double GLGradientEngine::getLastGpuTimeMs() const
{
    return lastGpuTimeMs;
}
