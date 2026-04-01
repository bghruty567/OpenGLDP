#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <glad/glad.h>

class GLGradientEngine {
public:
    struct RegularParams { int dims[3]; float origin[3]; float spacing[3]; };
    struct WLSParams { float wExponent; float lambda; };
    GLGradientEngine();
    ~GLGradientEngine();
    bool setShaderDir(const std::string& dir);
    bool init();
    void release();
    bool computeRegularFD(const std::vector<float>& positions, const std::vector<float>& values, const RegularParams& p, std::vector<float>& outGrad);
    bool computeUnstructuredWLS(const std::vector<float>& positions, const std::vector<int>& offsets, const std::vector<int>& neighbors, const std::vector<float>& phi, const WLSParams& p, std::vector<float>& outGrad);

    void   setEnableGpuTiming(bool on);
    double getLastGpuTimeMs() const;

private:
    std::string shaderDir;
    GLuint progRegular = 0;
    GLuint progWLS = 0;
    GLuint ssbo0 = 0, ssbo1 = 0, ssbo2 = 0, ssbo3 = 0, ssbo4 = 0;
    GLuint buildComputeFromFile(const std::string& path);
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);

    bool   enableGpuTiming = false;
    GLuint timeQuery = 0;
    double lastGpuTimeMs = 0.0;
};