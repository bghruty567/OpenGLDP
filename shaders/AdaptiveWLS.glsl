#version 430

layout(local_size_x = 256) in;

layout(std430, binding = 0) readonly buffer P { vec4 pos[]; };
layout(std430, binding = 1) readonly buffer O { int  off[]; };
layout(std430, binding = 2) readonly buffer N { int  nbr[]; };
layout(std430, binding = 3) readonly buffer V { float val[]; };
layout(std430, binding = 4) writeonly buffer G { float grad[]; };
layout(std430, binding = 5) readonly buffer F { float frame[]; };
layout(std430, binding = 6) readonly buffer D { uint dimTag[]; };
layout(std430, binding = 7) readonly buffer Q { float qual[]; };
layout(std430, binding = 8) readonly buffer M { float meanDist[]; };

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

vec3 fwd3(in mat3 L, vec3 b)
{
    vec3 y;
    y.x = b.x / L[0][0];
    y.y = (b.y - L[1][0] * y.x) / L[1][1];
    y.z = (b.z - L[2][0] * y.x - L[2][1] * y.y) / L[2][2];
    return y;
}

vec3 bwd3(in mat3 L, vec3 y)
{
    vec3 x;
    x.z = y.z / L[2][2];
    x.y = (y.y - L[2][1] * x.z) / L[1][1];
    x.x = (y.x - L[1][0] * x.y - L[2][0] * x.z) / L[0][0];
    return x;
}

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

vec3 getAxis(int i, int axis)
{
    int base = i * 9 + axis * 3;
    return vec3(frame[base + 0], frame[base + 1], frame[base + 2]);
}

float computeWeight(vec3 local, float len, float h, float alpha, uint dim)
{
    float sigma = max(h, 1e-6);
    float sigma2 = sigma * sigma;
    float spatial = exp(-0.5 * len * len / sigma2);
    float tangentPenalty = 1.0;

    if (dim == 2u) {
        float ns = max(0.35 * sigma, 1e-6);
        tangentPenalty = exp(-0.5 * local.z * local.z / (ns * ns));
    } else if (dim == 1u) {
        float ns = max(0.35 * sigma, 1e-6);
        float ortho2 = local.y * local.y + local.z * local.z;
        tangentPenalty = exp(-0.5 * ortho2 / (ns * ns));
    }

    return spatial * tangentPenalty / pow(max(len, 1e-6), alpha);
}

void main()
{
    uint iu = gl_GlobalInvocationID.x;
    if (int(iu) >= uN) return;

    int i = int(iu);
    int b = off[i];
    int e = off[i + 1];
    vec3 pi = pos[i].xyz;

    vec3 e1 = normalize(getAxis(i, 0));
    vec3 e2 = normalize(getAxis(i, 1));
    vec3 e3 = normalize(getAxis(i, 2));

    uint dim = (uEnableAdaptiveDimension != 0) ? clamp(dimTag[i], 1u, 3u) : 3u;
    float q = clamp(qual[i], 0.0, 1.0);
    float h = max(meanDist[i], 1e-6);
    float alpha = uWExp + 0.75 * (1.0 - q);
    float lambdaScale = 1.0;
    if (uEnableAdaptiveRegularization != 0) {
        lambdaScale = 1.0 + uLambdaAmplify * (1.0 - q);
    }

    int n = uNumComponents;
    int base = i * (3 * n);

    for (int c = 0; c < n; ++c)
    {
        float f0 = getv(i, c);
        vec3 g = vec3(0.0);
        int cnt = 0;

        if (dim == 1u)
        {
            float s11 = 0.0;
            float rhs = 0.0;

            for (int k = b; k < e; ++k)
            {
                int j = nbr[k];
                if (j < 0 || j >= uN) continue;

                vec3 d = pos[j].xyz - pi;
                float x = dot(d, e1);
                float y = dot(d, e2);
                float z = dot(d, e3);
                float len = length(vec3(x, y, z));
                if (len <= 1e-6) continue;

                float dv = getv(j, c) - f0;
                float w = computeWeight(vec3(x, y, z), len, h, alpha, dim);

                s11 += w * x * x;
                rhs += w * dv * x;
                cnt++;
            }

            float lambdaEff = max(uLambda * lambdaScale, 1e-8);
            if (cnt >= 2 && s11 > 1e-10) {
                g = (rhs / (s11 + lambdaEff)) * e1;
            }
        }
        else if (dim == 2u)
        {
            mat2 A = mat2(0.0);
            vec2 rhs = vec2(0.0);

            for (int k = b; k < e; ++k)
            {
                int j = nbr[k];
                if (j < 0 || j >= uN) continue;

                vec3 d = pos[j].xyz - pi;
                vec3 local = vec3(dot(d, e1), dot(d, e2), dot(d, e3));
                float len = length(local);
                if (len <= 1e-6) continue;

                float dv = getv(j, c) - f0;
                float w = computeWeight(local, len, h, alpha, dim);

                A[0][0] += w * local.x * local.x;
                A[0][1] += w * local.x * local.y;
                A[1][0] += w * local.y * local.x;
                A[1][1] += w * local.y * local.y;
                rhs += w * dv * local.xy;
                cnt++;
            }

            float tr = A[0][0] + A[1][1];
            float lambdaEff = max(uLambda * lambdaScale, max(1e-6 * tr, 1e-8));
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
            mat3 S = mat3(0.0);
            vec3 rhs = vec3(0.0);

            for (int k = b; k < e; ++k)
            {
                int j = nbr[k];
                if (j < 0 || j >= uN) continue;

                vec3 d = pos[j].xyz - pi;
                vec3 local = vec3(dot(d, e1), dot(d, e2), dot(d, e3));
                float len = length(local);
                if (len <= 1e-6) continue;

                float dv = getv(j, c) - f0;
                float w = computeWeight(local, len, h, alpha, dim);

                S[0][0] += w * local.x * local.x;
                S[0][1] += w * local.x * local.y;
                S[0][2] += w * local.x * local.z;
                S[1][0] += w * local.y * local.x;
                S[1][1] += w * local.y * local.y;
                S[1][2] += w * local.y * local.z;
                S[2][0] += w * local.z * local.x;
                S[2][1] += w * local.z * local.y;
                S[2][2] += w * local.z * local.z;
                rhs += w * dv * local;
                cnt++;
            }

            float tr = S[0][0] + S[1][1] + S[2][2];
            float lambdaEff = max(uLambda * lambdaScale, max(1e-6 * tr, 1e-8));
            S += lambdaEff * mat3(1.0);

            mat3 L;
            bool ok = (cnt >= 4) && chol3(S, L);
            if (ok) {
                vec3 y = fwd3(L, rhs);
                vec3 coeff = bwd3(L, y);
                g = coeff.x * e1 + coeff.y * e2 + coeff.z * e3;
            }
        }

        grad[base + c * 3 + 0] = g.x;
        grad[base + c * 3 + 1] = g.y;
        grad[base + c * 3 + 2] = g.z;
    }
}
