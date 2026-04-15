#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer BaseBuf { float baseVal[]; };
layout(std430, binding = 1) readonly buffer D0Buf { float d0Val[]; };
layout(std430, binding = 2) readonly buffer D1Buf { float d1Val[]; };
layout(std430, binding = 3) readonly buffer D2Buf { float d2Val[]; };
layout(std430, binding = 4) writeonly buffer OutBuf { float outVal[]; };

uniform int uTotalCount;
uniform int uLevelCount;
uniform float uEdgeSigma;
uniform vec3 uDetailGains;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (int(idx) >= uTotalCount) return;

    float base = baseVal[idx];
    float d0 = (uLevelCount > 0) ? d0Val[idx] : 0.0;
    float d1 = (uLevelCount > 1) ? d1Val[idx] : 0.0;
    float d2 = (uLevelCount > 2) ? d2Val[idx] : 0.0;

    float m0 = abs(d0);
    float m1 = abs(d1);
    float m2 = abs(d2);

    float feature = m0 + m1 + m2;
    float atten = feature / (feature + max(uEdgeSigma, 1e-8));
    float sumM = feature + 1e-8;

    float w0 = atten * (m0 / sumM) * uDetailGains.x;
    float w1 = atten * (m1 / sumM) * uDetailGains.y;
    float w2 = atten * (m2 / sumM) * uDetailGains.z;

    outVal[idx] = base + w0 * d0 + w1 * d1 + w2 * d2;
}
