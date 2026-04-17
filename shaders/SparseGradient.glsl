#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer O { int off[]; };
layout(std430, binding = 1) readonly buffer I { int srcIndex[]; };
layout(std430, binding = 2) readonly buffer C { vec4 coeff[]; };
layout(std430, binding = 3) readonly buffer V { float srcVal[]; };
layout(std430, binding = 4) writeonly buffer G { float grad[]; };

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
    int outBase = target * (3 * uNumComponents);

    for (int compId = 0; compId < uNumComponents; ++compId) {
        vec3 g = vec3(0.0);
        for (int k = begin; k < end; ++k) {
            int src = srcIndex[k];
            if (src < 0 || src >= uSourceCount) {
                continue;
            }
            g += coeff[k].xyz * srcVal[src * uNumComponents + compId];
        }

        grad[outBase + compId * 3 + 0] = g.x;
        grad[outBase + compId * 3 + 1] = g.y;
        grad[outBase + compId * 3 + 2] = g.z;
    }
}
