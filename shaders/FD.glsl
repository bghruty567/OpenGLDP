#version 430

// 规则网格用三维工作组并行，每个线程处理一个样本点。
layout(local_size_x=8, local_size_y=8, local_size_z=8) in;

// binding 约定与 GLGradientEngine::computeRegularFD 一致：
// 0: 样本位置（点坐标或单元中心坐标），扁平 float 数组；
// 1: 输入字段值；
// 2: 输出梯度。
layout(std430, binding=0) readonly buffer PointsBuf { float P[]; };
layout(std430, binding=1) readonly buffer ValuesBuf { float V[]; };
layout(std430, binding=2) writeonly  buffer GradBuf { float G[]; };

uniform uvec3 uDims;         // 规则网格尺寸 [nx, ny, nz]
uniform int   uNumComponents; // 输入字段分量数

// 把三维索引映射为一维线性索引。
uint idx(uint i,uint j,uint k)
{
    return (k*uDims.y + j)*uDims.x + i;
}

// 从扁平坐标数组里读取一个样本位置。
vec3 loadP(uint id)
{
    uint b=id*3u;
    return vec3(P[b],P[b+1u],P[b+2u]);
}

// 从扁平字段数组里读取“第 id 个样本的第 c 个分量”。
float loadV(uint id,int c)
{
    return V[id*uint(uNumComponents)+uint(c)];
}

// 在某一个坐标方向上选取“前后邻居 + 差分系数”。
//
// 内部点使用中心差分，所以 factor=0.5；
// 边界点退化为单边差分，所以 factor=1.0。
void pick1D(uint id, uint step, uint size, uint coord,
            out uint ip, out uint im, out float factor)
{
    if(size<=1u){ ip=id; im=id; factor=1.0; return; }
    if(coord==0u){ ip=id+step; im=id; factor=1.0; }
    else if(coord==size-1u){ ip=id; im=id-step; factor=1.0; }
    else { ip=id+step; im=id-step; factor=0.5; }
}

void main(){
    uint i=gl_GlobalInvocationID.x;
    uint j=gl_GlobalInvocationID.y;
    uint k=gl_GlobalInvocationID.z;
    if(i>=uDims.x||j>=uDims.y||k>=uDims.z) return;

    uint id = idx(i,j,k);
    uint ip,im,jp,jm,kp,km;
    float fXi,fEta,fZeta;

    // 在参数空间 ξ/η/ζ 三个方向上各找一对前后邻居。
    pick1D(id, 1u,               uDims.x, i, ip, im, fXi);
    pick1D(id, uDims.x,          uDims.y, j, jp, jm, fEta);
    pick1D(id, uDims.x*uDims.y,  uDims.z, k, kp, km, fZeta);

    vec3 xp = loadP(ip), xm = loadP(im);
    vec3 yp = loadP(jp), ym = loadP(jm);
    vec3 zp = loadP(kp), zm = loadP(km);

    // 先在参数空间上估计几何导数，构造局部 Jacobian。
    float xxi   = fXi  * (xp.x - xm.x);
    float yxi   = fXi  * (xp.y - xm.y);
    float zxi   = fXi  * (xp.z - xm.z);
    float xeta  = fEta * (yp.x - ym.x);
    float yeta  = fEta * (yp.y - ym.y);
    float zeta  = fEta * (yp.z - ym.z);
    float xzeta = fZeta* (zp.x - zm.x);
    float yzeta = fZeta* (zp.y - zm.y);
    float zzeta = fZeta* (zp.z - zm.z);

    // Jacobian 行列式。
    float aj = xxi*yeta*zzeta + yxi*zeta*xzeta + zxi*xeta*yzeta
             - zxi*yeta*xzeta - yxi*xeta*zzeta - xxi*zeta*yzeta;

    // 若 Jacobian 退化，则直接把逆 Jacobian 压成 0，避免 NaN 扩散。
    float invAj = (aj!=0.0)?(1.0/aj):0.0;

    // 逆 Jacobian，把参数空间导数映射到物理空间导数。
    float xix   =  invAj*(yeta*zzeta - zeta*yzeta);
    float xiy   = -invAj*(xeta*zzeta - zeta*xzeta);
    float xiz   =  invAj*(xeta*yzeta - yeta*xzeta);
    float etax  = -invAj*(yxi*zzeta - zxi*yzeta);
    float etay  =  invAj*(xxi*zzeta - zxi*xzeta);
    float etaz  = -invAj*(xxi*yzeta - yxi*xzeta);
    float zetax =  invAj*(yxi*zeta - zxi*yeta);
    float zetay = -invAj*(xxi*zeta - zxi*xeta);
    float zetaz =  invAj*(xxi*yeta - yxi*xeta);

    uint base = id*uint(3*uNumComponents);

    for(int c=0;c<uNumComponents;++c){
        // 先在参数方向上做一维差分。
        float dXi   = (uDims.x>1u)? (fXi  * (loadV(ip,c)-loadV(im,c))) : 0.0;
        float dEta  = (uDims.y>1u)? (fEta * (loadV(jp,c)-loadV(jm,c))) : 0.0;
        float dZeta = (uDims.z>1u)? (fZeta* (loadV(kp,c)-loadV(km,c))) : 0.0;

        // 再通过链式法则映射到物理空间梯度。
        float gx = xix*dXi + etax*dEta + zetax*dZeta;
        float gy = xiy*dXi + etay*dEta + zetay*dZeta;
        float gz = xiz*dXi + etaz*dEta + zetaz*dZeta;

        uint o = base + uint(c*3);
        G[o+0]=gx; G[o+1]=gy; G[o+2]=gz;
    }
}
