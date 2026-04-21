#version 430

// 传统 WLS：一个线程处理一个样本。
layout(local_size_x = 256) in;

// binding 约定与 GLGradientEngine::computeUnstructuredWLS 一致。
layout(std430, binding = 0) readonly buffer P { vec4 pos[]; };
layout(std430, binding = 1) readonly buffer O { int  off[]; };
layout(std430, binding = 2) readonly buffer N { int  nbr[]; };
layout(std430, binding = 3) readonly buffer V { float val[]; };
layout(std430, binding = 4) writeonly buffer G { float grad[]; };

uniform int   uN;               // 样本总数
uniform float uWExp;            // 距离权重指数
uniform float uLambda;          // 基础正则项
uniform int   uNumComponents;   // 字段分量数

// 对 3x3 对称正定矩阵做 Cholesky 分解。
// 若分解失败，说明法方程矩阵过于病态。
bool chol3(in mat3 A, out mat3 L)
{
    float l00 = A[0][0];
    if (l00 <= 0.0) return false;
    l00 = sqrt(l00);
    L[0][0] = l00;
    L[1][0] = A[1][0] / l00;
    L[2][0] = A[2][0] / l00;

    float a11 = A[1][1] - L[1][0]*L[1][0];
    if (a11 <= 0.0) return false;
    float l11 = sqrt(a11);
    L[1][1] = l11;
    L[2][1] = (A[2][1] - L[2][0]*L[1][0]) / l11;

    float a22 = A[2][2] - L[2][0]*L[2][0] - L[2][1]*L[2][1];
    if (a22 <= 0.0) return false;
    float l22 = sqrt(a22);
    L[2][2] = l22;

    L[0][1] = 0.0;
    L[0][2] = 0.0;
    L[1][2] = 0.0;
    return true;
}

// 前向替换：求解 L y = b。
vec3 fwd3(in mat3 L, vec3 b)
{
    vec3 y;
    y.x = b.x / L[0][0];
    y.y = (b.y - L[1][0]*y.x) / L[1][1];
    y.z = (b.z - L[2][0]*y.x - L[2][1]*y.y) / L[2][2];
    return y;
}

// 后向替换：求解 L^T x = y。
vec3 bwd3(in mat3 L, vec3 y)
{
    vec3 x;
    x.z = y.z / L[2][2];
    x.y = (y.y - L[2][1]*x.z) / L[1][1];
    x.x = (y.x - L[1][0]*x.y - L[2][0]*x.z) / L[0][0];
    return x;
}

float getv(int i, int c) { return val[i * uNumComponents + c]; }

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uN) return;

    int i = int(iu);
    int b = off[i];
    int e = off[i + 1];
    vec3 pi = pos[i].xyz;
    float wexp = uWExp;

    // 先构造几何法方程矩阵 S = sum(w * d * d^T)。
    // 它只与几何邻域有关，所以对一个样本的所有分量都共享。
    mat3 S = mat3(0.0);
    int  cnt = 0;

    for (int k = b; k < e; ++k)
    {
        int j = nbr[k];
        if (j < 0 || j >= uN) continue;

        vec3 d = pos[j].xyz - pi;
        float n2 = dot(d, d);
        if (n2 <= 1e-12) continue;

        float len = sqrt(n2);
        float w   = 1.0 / pow(max(len, 1e-6), wexp);
        S += w * mat3(
            d.x*d.x, d.x*d.y, d.x*d.z,
            d.y*d.x, d.y*d.y, d.y*d.z,
            d.z*d.x, d.z*d.y, d.z*d.z
        );
        cnt++;
    }

    // 加上正则项，避免矩阵接近奇异时直接炸掉。
    float tr = S[0][0] + S[1][1] + S[2][2];
    float lamEff = max(uLambda, max(1e-6 * tr, 1e-8));
    S += lamEff * mat3(1.0);

    mat3 L;
    bool ok = (cnt >= 3) && chol3(S, L);

    int n    = uNumComponents;
    int base = i * (3 * n);

    // 每个分量独立求一个梯度向量。
    for (int c = 0; c < n; ++c)
    {
        vec3  t  = vec3(0.0);
        float f0 = getv(i, c);

        // t = sum(w * (fj - fi) * d)
        // 是 WLS 法方程右侧项。
        for (int k = b; k < e; ++k)
        {
            int j = nbr[k];
            if (j < 0 || j >= uN) continue;

            vec3 d = pos[j].xyz - pi;
            float n2 = dot(d, d);
            if (n2 <= 1e-12) continue;

            float len = sqrt(n2);
            float w   = 1.0 / pow(max(len, 1e-6), wexp);
            float dv  = getv(j, c) - f0;

            t += w * dv * d;
        }

        vec3 g = vec3(0.0);
        if (ok)
        {
            vec3 y = fwd3(L, t);
            g = bwd3(L, y);
        }

        grad[base + c*3 + 0] = g.x;
        grad[base + c*3 + 1] = g.y;
        grad[base + c*3 + 2] = g.z;
    }
}
