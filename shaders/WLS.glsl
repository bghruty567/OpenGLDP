#version 430

layout(local_size_x = 256) in;//每个工作组包含256个线程

layout(std430, binding = 0) readonly buffer P { vec4 pos[]; };//点坐标(处理单元数据时是单元中心坐标)
layout(std430, binding = 1) readonly buffer O { int  off[]; };//邻域偏移，off[i]是第i个点的邻域在nbr数组中的起始索引，off[i+1]是结束索引
layout(std430, binding = 2) readonly buffer N { int  nbr[]; };//邻域索引，nbr[off[i]]到nbr[off[i+1]-1]是第i个点的邻域点索引
layout(std430, binding = 3) readonly buffer V { float val[]; };//点值
layout(std430, binding = 4) writeonly buffer G { float grad[]; };//梯度

uniform int   uN;//点的数量
uniform float uWExp;//权重衰减指数，邻域点的权重为1/(距离^uWExp)，默认值为2.0
uniform float uLambda;//正则化参数，默认值为1e-3，实际使用时可能需要调整以获得更好的结果
uniform int   uNumComponents;//每个点的数据的分量数，例如标量场为1，向量场为3

// 对3x3矩阵A进行Cholesky分解，返回下三角矩阵L，使得A=LL^T
bool chol3(in mat3 A, out mat3 L)
{
    float l00 = A[0][0]; 
    if (l00 <= 0.0) 
    return false; 
    l00 = sqrt(l00); 
    L[0][0] = l00;
    L[1][0] = A[1][0] / l00; 
    L[2][0] = A[2][0] / l00;
    float a11 = A[1][1] - L[1][0]*L[1][0]; 
    if (a11 <= 0.0) 
    return false; 
    float l11 = sqrt(a11); 
    L[1][1] = l11;
    L[2][1] = (A[2][1] - L[2][0]*L[1][0]) / l11;
    float a22 = A[2][2] - L[2][0]*L[2][0] - L[2][1]*L[2][1]; 
    if (a22 <= 0.0) 
    return false; 
    float l22 = sqrt(a22); 
    L[2][2] = l22;
    L[0][1] = 0.0; 
    L[0][2] = 0.0; 
    L[1][2] = 0.0; 
    return true;
}

// 前向替换求解Ly=t，后向替换求解L^T x=y
vec3 fwd3(in mat3 L, vec3 b)
{
    vec3 y;
    y.x = b.x / L[0][0];
    y.y = (b.y - L[1][0]*y.x) / L[1][1];
    y.z = (b.z - L[2][0]*y.x - L[2][1]*y.y) / L[2][2];
    return y;
}

// 后向替换求解L^T x=y
vec3 bwd3(in mat3 L, vec3 y)
{
    vec3 x;
    x.z = y.z / L[2][2];
    x.y = (y.y - L[2][1]*x.z) / L[1][1];
    x.x = (y.x - L[1][0]*x.y - L[2][0]*x.z) / L[0][0];
    return x;
}

// 获取第i个点的第c个分量的值
float getv(int i, int c) { return val[i * uNumComponents + c]; }

void main()
{

    uint iu = gl_GlobalInvocationID.x;//当前线程处理第iu个点
    if (int(iu) >= uN) return;
    int i = int(iu);
    // 获取第i个点的邻域范围
    int b = off[i];
    int e = off[i + 1];
    // 获取第i个点的坐标
    vec3  pi   = pos[i].xyz;
    float wexp = uWExp;
    // 构建加权协方差矩阵S
    mat3 S = mat3(0.0);
    int  cnt = 0;//邻域点的数量

    for (int k = b; k < e; ++k)
    {
        int j = nbr[k];
        if (j < 0 || j >= uN) continue;

        vec3 d = pos[j].xyz - pi;//邻域点与中心点的距离向量
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
    // 添加正则化项，确保S是正定的
    float tr = S[0][0] + S[1][1] + S[2][2];
    float lamEff = max(uLambda, max(1e-6 * tr, 1e-8));
    S += lamEff * mat3(1.0);
    // 对S进行Cholesky分解，得到下三角矩阵L
    mat3 L;
    bool ok = (cnt >= 3) && chol3(S, L);
    // 计算每个分量的梯度
    int n    = uNumComponents;
    int base = i * (3 * n);
    // 对于每个分量，计算加权差值向量t，然后通过L求解梯度g
    for (int c = 0; c < n; ++c)
    {
        vec3  t  = vec3(0.0);//加权差值向量
        float f0 = getv(i, c);//中心点的值

        for (int k = b; k < e; ++k)
        {
            int j = nbr[k];
            if (j < 0 || j >= uN) continue;

            vec3 d = pos[j].xyz - pi;//邻域点与中心点的距离向量
            float n2 = dot(d, d);
            if (n2 <= 1e-12) continue;

            float len = sqrt(n2);
            float w   = 1.0 / pow(max(len, 1e-6), wexp);
            float dv  = getv(j, c) - f0;//邻域点与中心点的值的差

            t += w * dv * d;//加权差值向量是邻域点的值差乘以距离向量再乘以权重的和
        }

        vec3 g = vec3(0.0);
        if (ok)//如果S的Cholesky分解成功，说明邻域点分布良好，可以通过L求解梯度
        {
            vec3 y = fwd3(L, t);//前向替换求解Ly=t，得到中间变量y
            g = bwd3(L, y);//后向替换求解L^T g=y，得到梯度g
        }
        // 将计算得到的梯度g存储到输出缓冲区中，按照每个点3个分量的格式存储
        grad[base + c*3 + 0] = g.x;
        grad[base + c*3 + 1] = g.y;
        grad[base + c*3 + 2] = g.z;
    }
}
