#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/glad.h>

class GLGradientEngine {
public:
    struct RegularParams {
        int dims[3];
        float origin[3];
        float spacing[3];
    };

    struct WLSParams {
        float wExponent = 1.0f;
        float lambda = 1e-3f;
        float planeEigenRatio = 0.06f;
        float lineEigenRatio = 0.02f;
        float lambdaAmplify = 4.0f;
        int enableAdaptiveDimension = 1;
        int enableAdaptiveRegularization = 1;
    };

    GLGradientEngine();
    ~GLGradientEngine();

    bool setShaderDir(const std::string& dir);
    bool init();
    void release();

    bool computeRegularFD(const std::vector<float>& positions,
                          const std::vector<float>& values,
                          const RegularParams& p,
                          std::vector<float>& outGrad);

    bool computeUnstructuredWLS(const std::vector<float>& positions,
                                const std::vector<int>& offsets,
                                const std::vector<int>& neighbors,
                                const std::vector<float>& phi,
                                const WLSParams& p,
                                std::vector<float>& outGrad);

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

    bool computeUnstructuredShapeFunctionPoint(const std::vector<float>& points,
                                               const std::vector<int>& cellOffsets,
                                               const std::vector<int>& cells,
                                               const std::vector<int>& cellTypes,
                                               const std::vector<int>& pointInCellOffsets,
                                               const std::vector<int>& pointInCellNeighbors,
                                               const std::vector<float>& pointValues,
                                               std::vector<float>& outGrad);

    bool computeUnstructuredShapeFunctionCell(const std::vector<float>& points,
                                              const std::vector<int>& cellOffsets,
                                              const std::vector<int>& cells,
                                              const std::vector<int>& cellTypes,
                                              const std::vector<int>& pointInCellOffsets,
                                              const std::vector<int>& pointInCellNeighbors,
                                              const std::vector<float>& cellValues,
                                              std::vector<float>& outGrad);

    void setEnableGpuTiming(bool on);
    double getLastGpuTimeMs() const;

private:
    std::string shaderDir;

    GLuint progRegular = 0;
    GLuint progWLS = 0;
    GLuint progAdaptiveWLS = 0;
    GLuint progShapePoint = 0;
    GLuint progCellToPoint = 0;
    GLuint progShapeCell = 0;

    GLuint ssbo0 = 0;
    GLuint ssbo1 = 0;
    GLuint ssbo2 = 0;
    GLuint ssbo3 = 0;
    GLuint ssbo4 = 0;
    GLuint ssbo5 = 0;
    GLuint ssbo6 = 0;
    GLuint ssbo7 = 0;
    GLuint ssbo8 = 0;

    GLuint buildComputeFromFile(const std::string& path);
    void ensureBuffer(GLuint& id, size_t bytes, GLenum usage);
    bool computePointGradientsViaCellLift(const std::vector<float>& points,
                                          const std::vector<int>& cellOffsets,
                                          const std::vector<int>& cells,
                                          const std::vector<int>& cellTypes,
                                          const std::vector<int>& pointInCellOffsets,
                                          const std::vector<int>& pointInCellNeighbors,
                                          const std::vector<float>& pointValues,
                                          std::vector<float>& outGrad,
                                          double& gpuTimeMs);

    bool enableGpuTiming = false;
    GLuint timeQuery = 0;
    double lastGpuTimeMs = 0.0;
};
