#pragma once
#include <string>
#include <vector>
#include <glad/glad.h>

class GLFilterEngine {
public:
    struct BilateralParams {
        float spatialSigma = 1.0f;
        float rangeSigma = 0.1f;
    };

    struct FusionParams {
        int levelCount = 3;
        float edgeSigma = 0.05f;
        float detailGains[3];

        FusionParams()
        {
            detailGains[0] = 1.0f;
            detailGains[1] = 0.75f;
            detailGains[2] = 0.5f;
        }
    };

    GLFilterEngine();
    ~GLFilterEngine();

    bool setShaderDir(const std::string& dir);
    bool init();
    void release();

    bool bilateralGraph(const std::vector<float>& positions,
                        const std::vector<int>& offsets,
                        const std::vector<int>& neighbors,
                        const std::vector<float>& values,
                        const BilateralParams& p,
                        std::vector<float>& outValues);

    bool fuseMultiScale(const std::vector<float>& base,
                        const std::vector<float>& detail0,
                        const std::vector<float>& detail1,
                        const std::vector<float>& detail2,
                        const FusionParams& p,
                        std::vector<float>& outValues);

    void setEnableGpuTiming(bool on);
    double getLastGpuTimeMs() const;

private:
    std::string shaderDir;

    GLuint progBilateral = 0;
    GLuint progFusion = 0;

    GLuint ssbo0 = 0;
    GLuint ssbo1 = 0;
    GLuint ssbo2 = 0;
    GLuint ssbo3 = 0;
    GLuint ssbo4 = 0;

    bool enableGpuTiming = false;
    GLuint timeQuery = 0;
    double lastGpuTimeMs = 0.0;

    GLuint buildComputeFromFile(const std::string& path);
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);
};
