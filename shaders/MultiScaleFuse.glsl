#version 430

// 一个线程处理一个“标量样本”。
// 注意这里的总长度已经是 tupleCount * numComponents，
// 所以 shader 不再关心几何邻域，只关心逐样本融合。
layout(local_size_x = 256) in;

// 绑定约定与 GLFilterEngine::fuseMultiScale 一致：
// 0: base 层（最深平滑层）
// 1~3: 三个 detail 层
// 4: 融合输出
layout(std430, binding = 0) readonly buffer BaseBuf { float baseVal[]; };
layout(std430, binding = 1) readonly buffer D0Buf { float d0Val[]; };
layout(std430, binding = 2) readonly buffer D1Buf { float d1Val[]; };
layout(std430, binding = 3) readonly buffer D2Buf { float d2Val[]; };
layout(std430, binding = 4) writeonly buffer OutBuf { float outVal[]; };

uniform int uTotalCount;    // 标量样本总数
uniform int uLevelCount;    // 实际参与融合的 detail 层数（0~3）
uniform float uEdgeSigma;   // 细节抑制阈值，越大越保守
uniform vec3 uDetailGains;  // 各层 detail 的增益

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (int(idx) >= uTotalCount) return;

    float base = baseVal[idx];
    float d0 = (uLevelCount > 0) ? d0Val[idx] : 0.0;
    float d1 = (uLevelCount > 1) ? d1Val[idx] : 0.0;
    float d2 = (uLevelCount > 2) ? d2Val[idx] : 0.0;

    // 用 detail 的绝对值近似表示“该位置的局部特征强度”。
    float m0 = abs(d0);
    float m1 = abs(d1);
    float m2 = abs(d2);

    float feature = m0 + m1 + m2;

    // atten 接近 0 表示当前位置基本没什么细节，应更靠近 base；
    // atten 接近 1 表示当前位置细节丰富，可以加回更多 detail。
    float atten = feature / (feature + max(uEdgeSigma, 1e-8));
    float sumM = feature + 1e-8;

    // 三层 detail 不直接用固定系数，而是按各自幅值占比分配权重。
    float w0 = atten * (m0 / sumM) * uDetailGains.x;
    float w1 = atten * (m1 / sumM) * uDetailGains.y;
    float w2 = atten * (m2 / sumM) * uDetailGains.z;

    outVal[idx] = base + w0 * d0 + w1 * d1 + w2 * d2;
}
