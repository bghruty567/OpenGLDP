#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer O { int off[]; };
layout(std430, binding = 1) readonly buffer I { int srcIndex[]; };
layout(std430, binding = 2) readonly buffer W { float weights[]; };
layout(std430, binding = 3) readonly buffer V { float srcVal[]; };
layout(std430, binding = 4) writeonly buffer R { float dstVal[]; };

uniform int uTargetCount;
uniform int uSourceCount;
uniform int uNumComponents;

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uTargetCount) {
        return;
    }

    int target = int(iu);
    int begin = off[target];
    int end = off[target + 1];
    int outBase = target * uNumComponents;

    for (int comp = 0; comp < uNumComponents; ++comp) {
        float sum = 0.0;
        for (int k = begin; k < end; ++k) {
            int src = srcIndex[k];
            if (src < 0 || src >= uSourceCount) {
                continue;
            }
            sum += weights[k] * srcVal[src * uNumComponents + comp];
        }
        dstVal[outBase + comp] = sum;
    }
}
