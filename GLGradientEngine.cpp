#include "GLGradientEngine.h"

#include <iostream>
#include <fstream>
#include <sstream>

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
        return 0;

    const char* c = src.c_str();

    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
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

    return progRegular && progWLS;
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

    if (ssbo0) { glDeleteBuffers(1, &ssbo0); ssbo0 = 0; }
    if (ssbo1) { glDeleteBuffers(1, &ssbo1); ssbo1 = 0; }
    if (ssbo2) { glDeleteBuffers(1, &ssbo2); ssbo2 = 0; }
    if (ssbo3) { glDeleteBuffers(1, &ssbo3); ssbo3 = 0; }
    if (ssbo4) { glDeleteBuffers(1, &ssbo4); ssbo4 = 0; }
}

bool GLGradientEngine::computeRegularFD(const std::vector<float>& positions,
    const std::vector<float>& values,
    const RegularParams& p,
    std::vector<float>& outGrad)
{
    if (progRegular == 0) return false;
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
    glDispatchCompute(gx, gy, gz);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

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

    glDispatchCompute(gx, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo4);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        outGrad.size() * sizeof(float), outGrad.data());

    return true;
}