#version 430

// AWLS：一个线程处理一个样本。
// 与普通 WLS 相比，这个版本额外拿到了“局部主方向 + 局部维度 + 邻域质量”。
layout(local_size_x = 256) in;

// binding 约定与 GLGradientEngine::computeUnstructuredAdaptiveWLS 一致。
layout(std430, binding = 0) readonly buffer P { vec4 pos[]; };
layout(std430, binding = 1) readonly buffer O { int  off[]; };
layout(std430, binding = 2) readonly buffer N { int  nbr[]; };
layout(std430, binding = 3) readonly buffer V { float val[]; };
layout(std430, binding = 4) writeonly buffer G { float grad[]; };
layout(std430, binding = 5) readonly buffer F { float frame[]; };   // 每个样本 3x3 局部正交框架
layout(std430, binding = 6) readonly buffer D { uint dimTag[]; };   // 1=线状，2=面状，3=体状
layout(std430, binding = 7) readonly buffer Q { float qual[]; };    // 邻域质量评分
layout(std430, binding = 8) readonly buffer M { float meanDist[]; }; // 局部平均邻距

uniform int   uN;
uniform float uWExp;
uniform float uLambda;
uniform int   uNumComponents;
uniform float uPlaneEigenRatio;
uniform float uLineEigenRatio;
uniform float uLambdaAmplify;
uniform int   uEnableAdaptiveDimension;
uniform int   uEnableAdaptiveRegularization;

bool chol3(in mat3 A, out mat3 L)
{
    float l00 = A[0][0];
    if (l00 <= 0.0) return false;
    l00 = sqrt(l00);
    L[0][0] = l00;
    L[1][0] = A[1][0] / l00;
    L[2][0] = A[2][0] / l00;

    float a11 = A[1][1] - L[1][0] * L[1][0];
    if (a11 <= 0.0) return false;
    float l11 = sqrt(a11);
    L[1][1] = l11;
    L[2][1] = (A[2][1] - L[2][0] * L[1][0]) / l11;

    float a22 = A[2][2] - L[2][0] * L[2][0] - L[2][1] * L[2][1];
    if (a22 <= 0.0) return false;
    float l22 = sqrt(a22);
    L[2][2] = l22;

    L[0][1] = 0.0;
    L[0][2] = 0.0;
    L[1][2] = 0.0;
    return true;
}

// 对 3x3 Cholesky 分解结果做前向替换，求解 L y = b。
vec3 fwd3(in mat3 L, vec3 b)
{
    vec3 y;
    y.x = b.x / L[0][0];
    y.y = (b.y - L[1][0] * y.x) / L[1][1];
    y.z = (b.z - L[2][0] * y.x - L[2][1] * y.y) / L[2][2];
    return y;
}

// 对 3x3 Cholesky 分解结果做后向替换，求解 L^T x = y。
vec3 bwd3(in mat3 L, vec3 y)
{
    vec3 x;
    x.z = y.z / L[2][2];
    x.y = (y.y - L[2][1] * x.z) / L[1][1];
    x.x = (y.x - L[1][0] * x.y - L[2][0] * x.z) / L[0][0];
    return x;
}

// 2x2 线性方程组求解器，供二维局部拟合使用。
bool solve2x2(in mat2 A, in vec2 b, out vec2 x)
{
    float det = A[0][0] * A[1][1] - A[1][0] * A[0][1];
    if (abs(det) <= 1e-12) {
        x = vec2(0.0);
        return false;
    }
    x.x = ( A[1][1] * b.x - A[1][0] * b.y) / det;
    x.y = (-A[0][1] * b.x + A[0][0] * b.y) / det;
    return true;
}

float getv(int i, int c)
{
    return val[i * uNumComponents + c];
}

// 从 frame 缓冲区中读取第 i 个样本的第 axis 根局部主方向。
vec3 getAxis(int i, int axis)
{
    int base = i * 9 + axis * 3;
    return vec3(frame[base + 0], frame[base + 1], frame[base + 2]);
}

vec3 safeNormalize(vec3 v, vec3 fallbackAxis)
{
    float n = length(v);
    if (n <= 1e-7) {
        return fallbackAxis;
    }
    return v / n;
}

// 距离权重。
// uWExp 越大，说明越偏向近邻样本。
float computeDistanceWeight(float len)
{
    return 1.0 / pow(max(len, 1e-6), max(uWExp, 0.0));
}

// 自适应正则项。
//
// 这里把三个量揉在一起：
// 1. 用户给的基础正则 `uLambda`；
// 2. 当前局部几何尺度 `h`；
// 3. 当前邻域质量 `quality`。
float computeLambdaEff(float traceValue, float h, float quality, int count)
{
    float lambdaScale = 1.0;
    if (uEnableAdaptiveRegularization != 0) {
        lambdaScale += uLambdaAmplify * (1.0 - clamp(quality, 0.0, 1.0));
    }

    float h2 = max(h * h, 1e-8);
    float scaledTrace = traceValue > 0.0 ? (traceValue / max(float(count), 1.0)) : h2;
    float base = max(uLambda * lambdaScale * max(scaledTrace, h2), 1e-8);
    return max(base, max(1e-6 * max(traceValue, 0.0), 1e-8));
}

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uN) {
        return;
    }

    int i = int(iu);
    int b = off[i];
    int e = off[i + 1];
    vec3 pi = pos[i].xyz;

    // 从 CPU 端预分析结果中取出局部主方向。
    vec3 e1 = safeNormalize(getAxis(i, 0), vec3(1.0, 0.0, 0.0));
    vec3 e2 = safeNormalize(getAxis(i, 1), vec3(0.0, 1.0, 0.0));
    vec3 e3 = safeNormalize(getAxis(i, 2), vec3(0.0, 0.0, 1.0));

    // 若启用局部维度自适应，则优先相信 CPU 端给出的 dimTag；
    // 否则强制按三维 WLS 解。
    uint dim = (uEnableAdaptiveDimension != 0) ? clamp(dimTag[i], 1u, 3u) : 3u;
    float q = clamp(qual[i], 0.0, 1.0);
    float h = max(meanDist[i], 1e-6);

    int n = uNumComponents;
    int base = i * (3 * n);

    for (int c = 0; c < n; ++c)
    {
        float f0 = getv(i, c);
        vec3 g = vec3(0.0);
        int cnt = 0;

        if (dim == 1u)
        {
            // 一维局部拟合：只沿主方向 e1 恢复变化率。
            float a11 = 0.0;
            float rhs = 0.0;

            for (int k = b; k < e; ++k)
            {
                int j = nbr[k];
                if (j < 0 || j >= uN) continue;

                vec3 d = pos[j].xyz - pi;
                float len = length(d);
                if (len <= 1e-6) continue;

                float x = dot(d, e1);
                float dv = getv(j, c) - f0;
                float w = computeDistanceWeight(len);

                a11 += w * x * x;
                rhs += w * dv * x;
                cnt++;
            }

            float lambdaEff = computeLambdaEff(a11, h, q, cnt);
            if (cnt >= 2 && a11 > 1e-10) {
                g = (rhs / (a11 + lambdaEff)) * e1;
            }
        }
        else if (dim == 2u)
        {
            // 二维局部拟合：只在切平面 (e1, e2) 上求梯度。
            mat2 A = mat2(0.0);
            vec2 rhs = vec2(0.0);

            for (int k = b; k < e; ++k)
            {
                int j = nbr[k];
                if (j < 0 || j >= uN) continue;

                vec3 d = pos[j].xyz - pi;
                float len = length(d);
                if (len <= 1e-6) continue;

                vec2 local = vec2(dot(d, e1), dot(d, e2));
                float dv = getv(j, c) - f0;
                float w = computeDistanceWeight(len);

                A[0][0] += w * local.x * local.x;
                A[0][1] += w * local.x * local.y;
                A[1][0] += w * local.y * local.x;
                A[1][1] += w * local.y * local.y;
                rhs += w * dv * local;
                cnt++;
            }

            float traceValue = A[0][0] + A[1][1];
            float lambdaEff = computeLambdaEff(traceValue, h, q, cnt);
            A[0][0] += lambdaEff;
            A[1][1] += lambdaEff;

            vec2 coeff = vec2(0.0);
            bool ok = (cnt >= 3) && solve2x2(A, rhs, coeff);
            if (ok) {
                g = coeff.x * e1 + coeff.y * e2;
            }
        }
        else
        {
            // 三维局部拟合：退回完整 3D WLS。
            mat3 A = mat3(0.0);
            vec3 rhs = vec3(0.0);

            for (int k = b; k < e; ++k)
            {
                int j = nbr[k];
                if (j < 0 || j >= uN) continue;

                vec3 d = pos[j].xyz - pi;
                float len = length(d);
                if (len <= 1e-6) continue;

                vec3 local = vec3(dot(d, e1), dot(d, e2), dot(d, e3));
                float dv = getv(j, c) - f0;
                float w = computeDistanceWeight(len);

                A[0][0] += w * local.x * local.x;
                A[0][1] += w * local.x * local.y;
                A[0][2] += w * local.x * local.z;
                A[1][0] += w * local.y * local.x;
                A[1][1] += w * local.y * local.y;
                A[1][2] += w * local.y * local.z;
                A[2][0] += w * local.z * local.x;
                A[2][1] += w * local.z * local.y;
                A[2][2] += w * local.z * local.z;
                rhs += w * dv * local;
                cnt++;
            }

            float traceValue = A[0][0] + A[1][1] + A[2][2];
            float lambdaEff = computeLambdaEff(traceValue, h, q, cnt);
            A += lambdaEff * mat3(1.0);

            mat3 L;
            bool ok = (cnt >= 4) && chol3(A, L);
            if (ok) {
                vec3 y = fwd3(L, rhs);
                vec3 coeff = bwd3(L, y);
                g = coeff.x * e1 + coeff.y * e2 + coeff.z * e3;
            }
        }

        // 输出格式与其他梯度 shader 保持一致：
        // 每个输入分量对应一个 3D 梯度向量。
        grad[base + c * 3 + 0] = g.x;
        grad[base + c * 3 + 1] = g.y;
        grad[base + c * 3 + 2] = g.z;
    }
}
