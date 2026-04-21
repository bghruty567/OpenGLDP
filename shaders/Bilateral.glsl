#version 430

// 一个线程处理一个样本（点或单元中心）。
layout(local_size_x = 256) in;

// binding 约定与 GLFilterEngine::bilateralGraph 一一对应：
// 0: 样本位置，按 vec4 读取，xyz 有效，w 仅用于对齐；
// 1: CSR 邻域偏移；
// 2: CSR 邻域索引；
// 3: 输入字段值；
// 4: 输出字段值。
layout(std430, binding = 0) readonly buffer P { vec4 pos[]; };
layout(std430, binding = 1) readonly buffer O { int off[]; };
layout(std430, binding = 2) readonly buffer N { int nbr[]; };
layout(std430, binding = 3) readonly buffer Vin { float inVal[]; };
layout(std430, binding = 4) writeonly buffer Vout { float outVal[]; };

uniform int uN;               // 样本总数
uniform int uNumComponents;   // 每个样本的分量数：标量=1，向量=3，等等
uniform float uSpatialSigma;  // 空间尺度，控制“多远算近邻”
uniform float uRangeSigma;    // 数值尺度，控制“数值差多大算同一结构”

// 从扁平数组中读取“第 i 个样本的第 c 个分量”。
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

    // 预先把 sigma^2 算好，避免在循环里重复乘法。
    float sigmaS2 = max(uSpatialSigma * uSpatialSigma, 1e-12);
    float sigmaR2 = max(uRangeSigma * uRangeSigma, 1e-12);

    // 每个分量独立滤波：
    // 这样既支持标量场，也支持向量场各分量分别平滑。
    for (int c = 0; c < uNumComponents; ++c) {
        float center = getVal(i, c);

        // 把中心样本自己也纳入加权平均，权重固定为 1。
        // 这样在邻域很小时不会完全被邻居“拖跑”。
        float sumW = 1.0;
        float sumV = center;

        for (int k = b; k < e; ++k) {
            int j = nbr[k];
            if (j < 0 || j >= uN) continue;

            vec3 d = pos[j].xyz - pi;
            float dist2 = dot(d, d);
            float dv = getVal(j, c) - center;

            // 双边滤波的核心：
            // 空间上越近，权重越大；
            // 数值上越接近，权重越大。
            float wSpatial = exp(-0.5 * dist2 / sigmaS2);
            float wRange = exp(-0.5 * (dv * dv) / sigmaR2);
            float w = wSpatial * wRange;

            sumW += w;
            sumV += w * getVal(j, c);
        }

        outVal[i * uNumComponents + c] = sumV / max(sumW, 1e-12);
    }
}
