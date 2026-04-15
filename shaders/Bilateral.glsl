#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer P { vec4 pos[]; };
layout(std430, binding = 1) readonly buffer O { int off[]; };
layout(std430, binding = 2) readonly buffer N { int nbr[]; };
layout(std430, binding = 3) readonly buffer Vin { float inVal[]; };
layout(std430, binding = 4) writeonly buffer Vout { float outVal[]; };

uniform int uN;
uniform int uNumComponents;
uniform float uSpatialSigma;
uniform float uRangeSigma;

float getVal(int i, int c)
{
    return inVal[i * uNumComponents + c];
}

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uN) return;

    int i = int(iu);
    int b = off[i];
    int e = off[i + 1];
    vec3 pi = pos[i].xyz;

    float sigmaS2 = max(uSpatialSigma * uSpatialSigma, 1e-12);
    float sigmaR2 = max(uRangeSigma * uRangeSigma, 1e-12);

    for (int c = 0; c < uNumComponents; ++c) {
        float center = getVal(i, c);
        float sumW = 1.0;
        float sumV = center;

        for (int k = b; k < e; ++k) {
            int j = nbr[k];
            if (j < 0 || j >= uN) continue;

            vec3 d = pos[j].xyz - pi;
            float dist2 = dot(d, d);
            float dv = getVal(j, c) - center;

            float wSpatial = exp(-0.5 * dist2 / sigmaS2);
            float wRange = exp(-0.5 * (dv * dv) / sigmaR2);
            float w = wSpatial * wRange;

            sumW += w;
            sumV += w * getVal(j, c);
        }

        outVal[i * uNumComponents + c] = sumV / max(sumW, 1e-12);
    }
}
