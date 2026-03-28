#version 430
layout(local_size_x=8, local_size_y=8, local_size_z=4) in;

/*
 * 规则网格的梯度计算（仿照 vtkGradientFilter 的结构化路径）
 * 思路：
 * - 对 i/j/k 三方向分别取“±”邻居的坐标差，构造局部雅可比矩阵 J 及其逆（度量系数）。
 * - 在参数方向 ξ/η/ζ 上用两点差分计算场的导数 dXi/dEta/dZeta：
 *   - 内部点用中心差分（因子 factor=0.5）
 *   - 边界点用单边差分（factor=1.0）
 *   - 若邻居被隐藏（ghost HIDDENPOINT），退化到内侧索引并调整因子，避免伪中心差分
 * - 使用链式法则：?f = [xix etax zetax; xiy etay zetay; xiz etaz zetaz] · [dXi dEta dZeta]^T
 *
 * SSBO 约定：
 * - binding=0 PointsBuf：点坐标数组，长度为 3*N
 * - binding=1 ValuesBuf：输入数据（点关联），长度为 N*numComponents（AOS）
 * - binding=2 GradBuf：输出梯度数组，长度为 N*(3*numComponents)
 * - binding=3 GhostBuf：可选 ghost 掩码（vtkGhostType），长度为 N（32-bit）
 */

layout(std430, binding=0) readonly buffer PointsBufSSBO { float PointsBuf[]; };
layout(std430, binding=1) readonly buffer ValuesSSBO    { float ValuesBuf[]; };
layout(std430, binding=2) writeonly  buffer GradSSBO    { float GradBuf[]; };
layout(std430, binding=3) readonly buffer GhostSSBO     { uint  GhostBuf[]; };

// Uniform 参数
uniform int   NumComponents;     // 输入数组的分量数（1=标量，3=向量等）
uniform uvec3 Dims;              // 规则网格维度 (nx, ny, nz)
uniform int   HasGhost;          // 是否启用 ghost（0/1）
uniform uint  HiddenMask;        // 隐藏点掩码（VTK: HIDDENPOINT=2）

// 工具函数：加载点坐标与字段值
vec3 loadCoord(uint id){
  uint b = id*3u; return vec3(PointsBuf[b], PointsBuf[b+1u], PointsBuf[b+2u]);
}
float loadVal(uint id, int c){
  return ValuesBuf[id*NumComponents + c];
}
bool isHidden(uint id){
  return (HasGhost != 0) && ((GhostBuf[id] & HiddenMask) != 0u);
}

// 选择 1D 方向的“±”邻居索引与差分因子（中心/单边/ghost退化）
void pick1D(uint idx, uint step, uint coordSize, uint coordIdx,
            out uint idPlus, out uint idMinus, out float factor)
{
  // 维度退化：无梯度
  if (coordSize==1u){ idPlus=idx; idMinus=idx; factor=1.0; return; }

  // 边界：单边差分；内部：中心差分
  if (coordIdx==0u){ factor=1.0; idPlus=idx+step; idMinus=idx; }
  else if (coordIdx==coordSize-1u){ factor=1.0; idPlus=idx; idMinus=idx-step; }
  else { factor=0.5; idPlus=idx+step; idMinus=idx-step; }

  // ghost 处理：若邻居隐藏，退化到当前索引并提高因子，避免伪中心差分
  if (HasGhost != 0){
    if (isHidden(idMinus)){ idMinus=idx; factor += 0.5; }
    if (isHidden(idPlus )){ idPlus =idx; factor += 0.5; }
  }
}

void main(){
  uint nx=Dims.x, ny=Dims.y, nz=Dims.z;
  uint i=gl_GlobalInvocationID.x, j=gl_GlobalInvocationID.y, k=gl_GlobalInvocationID.z;
  if(i>=nx||j>=ny||k>=nz) return;

  uint ijs = nx*ny;
  uint idx = i + j*nx + k*ijs;

  // 当前点隐藏：不写输出
  if (isHidden(idx)) return;

  // 取 ξ/η/ζ 三方向的“±”邻居索引与差分因子
  uint ip, im; float fXi;    pick1D(idx, 1u,   nx, i, ip, im, fXi);
  uint jp, jm; float fEta;   pick1D(idx, nx,   ny, j, jp, jm, fEta);
  uint kp, km; float fZeta;  pick1D(idx, ijs,  nz, k, kp, km, fZeta);

  // 坐标差（构造雅可比 J 的列向量）
  vec3 xp = loadCoord(ip), xm = loadCoord(im);
  vec3 yp = loadCoord(jp), ym = loadCoord(jm);
  vec3 zp = loadCoord(kp), zm = loadCoord(km);

  float xxi  = fXi  * (xp.x - xm.x);
  float yxi  = fXi  * (xp.y - xm.y);
  float zxi  = fXi  * (xp.z - xm.z);

  float xeta = fEta * (yp.x - ym.x);
  float yeta = fEta * (yp.y - ym.y);
  float zeta = fEta * (yp.z - ym.z);

  float xzeta= fZeta* (zp.x - zm.x);
  float yzeta= fZeta* (zp.y - zm.y);
  float zzeta= fZeta* (zp.z - zm.z);

  // 雅可比行列式与逆（若退化则设为 0）
  float aj = xxi*yeta*zzeta + yxi*zeta*xzeta + zxi*xeta*yzeta
           - zxi*yeta*xzeta - yxi*xeta*zzeta - xxi*zeta*yzeta;
  if (aj!=0.0) aj = 1.0/aj; else aj = 0.0;

  // 度量系数（J^{-1}）：把参数导数映射到物理坐标
  float xix   =  aj*(yeta*zzeta - zeta*yzeta);
  float xiy   = -aj*(xeta*zzeta - zeta*xzeta);
  float xiz   =  aj*(xeta*yzeta - yeta*xzeta);

  float etax  = -aj*(yxi*zzeta - zxi*yzeta);
  float etay  =  aj*(xxi*zzeta - zxi*xzeta);
  float etaz  = -aj*(xxi*yzeta - yxi*xzeta);

  float zetax =  aj*(yxi*zeta - zxi*yeta);
  float zetay = -aj*(xxi*zeta - zxi*xeta);
  float zetaz =  aj*(xxi*yeta - yxi*xeta);

  // 输出写入基址
  uint base = idx * uint(3*NumComponents);

  // 遍历每个分量，计算参数方向的差分并映射到物理坐标
  for(int c=0;c<NumComponents;c++){
    float dXi   = (nx>1u) ? (fXi  * (loadVal(ip,c) - loadVal(im,c)))   : 0.0;
    float dEta  = (ny>1u) ? (fEta * (loadVal(jp,c) - loadVal(jm,c)))   : 0.0;
    float dZeta = (nz>1u) ? (fZeta* (loadVal(kp,c) - loadVal(km,c)))   : 0.0;

    float gx = xix*dXi + etax*dEta + zetax*dZeta;
    float gy = xiy*dXi + etay*dEta + zetay*dZeta;
    float gz = xiz*dXi + etaz*dEta + zetaz*dZeta;

    uint o = base + uint(c*3);
    GradBuf[o]   = gx;
    GradBuf[o+1] = gy;
    GradBuf[o+2] = gz;
  }
}