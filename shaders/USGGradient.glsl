#version 430
layout(local_size_x=128) in;

/*
 * 非结构化网格的加权最小二乘（LS）梯度拟合
 * 思路：
 * - 在每个点 p0 处，拟合 f(x) ≈ f0 + ?f · (x - x0)
 * - 用邻域点集合 {pi} 构造正规方程：(A? W A) ?f = A? W b
 *   其中 A 行为 dx=(pi-x0) 的分量，b 为 (fi - f0)，W 为权重（可按距离衰减）
 * - 计算 3×3 矩阵 G=A?WA 及右端项 rhs=A?Wb，然后 ?f = G^{-1} rhs
 * - 支持 ghost：隐藏点不写输出，隐藏邻居不参与拟合
 *
 * SSBO 约定：
 * - binding=0 PointsBuf：点坐标数组，长度 3*N
 * - binding=1 ValuesBuf：输入数据（点关联），长度 N*numComponents（AOS）
 * - binding=2 OffsetsBuf：CSR 偏移，长度 N+1，OffsetsBuf[i] 到 OffsetsBuf[i+1]-1 是 i 的邻居索引区间
 * - binding=3 NeighborsBuf：邻居索引数组（0..N-1）
 * - binding=4 GhostBuf：可选 ghost 掩码（vtkGhostType），长度 N（32-bit）
 * - binding=5 GradBuf：输出梯度数组，长度 N*(3*numComponents)
 */

layout(std430, binding=0) readonly buffer PointsBufSSBO { float PointsBuf[]; };
layout(std430, binding=1) readonly buffer ValuesSSBO    { float ValuesBuf[]; };
layout(std430, binding=2) readonly buffer OffsetsSSBO   { uint  OffsetsBuf[]; };
layout(std430, binding=3) readonly buffer NeighborsSSBO { uint  NeighborsBuf[]; };
layout(std430, binding=4) readonly buffer GhostSSBO     { uint  GhostBuf[]; };
layout(std430, binding=5) writeonly  buffer GradSSBO    { float GradBuf[]; };

// Uniform 参数
uniform int   NumComponents;    // 输入分量数
uniform int   NumPoints;        // 点数量 N
uniform int   UseGhost;         // 是否启用 ghost（0/1）
uniform uint  HiddenMask;       // 隐藏点掩码（VTK: HIDDENPOINT=2）
uniform int   UseWeights;       // 是否使用距离权重（0/1）
uniform float WeightAlpha;      // 权重控制参数：w = 1 / max(L,eps)^alpha
uniform float Epsilon;          // 数值容差（避免除零/奇异）
uniform float Lambda;           // 正则项（对角线），提升病态邻域稳定性

// 工具函数
vec3  loadCoord(uint id){ uint b=id*3u; return vec3(PointsBuf[b], PointsBuf[b+1u], PointsBuf[b+2u]); }
float loadVal(uint id,int c){ return ValuesBuf[id*NumComponents + c]; }
bool  isHidden(uint id){ return (UseGhost!=0) && ((GhostBuf[id] & HiddenMask)!=0u); }

// 距离权重：默认 1/距离^alpha（alpha=1 或 2）；UseWeights=0 时权重恒为 1
float weightFromLength(float L){
  if(UseWeights==0) return 1.0;
  float d = max(L, Epsilon);
  return 1.0 / pow(d, WeightAlpha);
}

void main(){
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(NumPoints)) return;

  // 当前点隐藏：不写输出
  if (isHidden(idx)) return;

  vec3 x0 = loadCoord(idx);
  uint off0 = OffsetsBuf[idx];
  uint off1 = OffsetsBuf[idx+1u];

  // 构造正规方程的左端 G=A?WA （3×3，对称）并加正则项 Lambda
  float g00=Lambda, g01=0.0, g02=0.0, g11=Lambda, g12=0.0, g22=Lambda;
  for(uint k=off0;k<off1;k++){
    uint n = NeighborsBuf[k];
    if(n==idx) continue;
    if(isHidden(n)) continue;

    vec3 dx = loadCoord(n) - x0;
    float L = length(dx);
    if(L<=Epsilon) continue;

    float w = weightFromLength(L);
    g00 += w*dx.x*dx.x;
    g01 += w*dx.x*dx.y;
    g02 += w*dx.x*dx.z;
    g11 += w*dx.y*dx.y;
    g12 += w*dx.y*dx.z;
    g22 += w*dx.z*dx.z;
  }

  // 以 mat3 表示 G 并检测奇异
  mat3 G = mat3(g00,g01,g02, g01,g11,g12, g02,g12,g22);
  float detG = determinant(G);
  if (abs(detG) < Epsilon) {
    // 邻域退化：梯度写零（也可在未来改为扩大邻域或一维/二维退化解）
    uint base = idx*uint(3*NumComponents);
    for(int c=0;c<NumComponents;c++){ uint o=base+uint(c*3); GradBuf[o]=0.0; GradBuf[o+1u]=0.0; GradBuf[o+2u]=0.0; }
    return;
  }
  mat3 invG = inverse(G);

  // 先取当前点的各分量值 f0[c]
  float f0[64]; // 假定分量数不超过 64，可按需增大
  for(int c=0;c<NumComponents;c++){ f0[c]=loadVal(idx,c); }

  // 右端项 rhs=A?Wb：累积 w * dx * (fi - f0)
  uint base = idx*uint(3*NumComponents);
  for(int c=0;c<NumComponents;c++){
    float rhs0=0.0, rhs1=0.0, rhs2=0.0;

    for(uint k=off0;k<off1;k++){
      uint n = NeighborsBuf[k];
      if(n==idx) continue;
      if(isHidden(n)) continue;

      vec3 dx = loadCoord(n) - x0;
      float L = length(dx);
      if(L<=Epsilon) continue;

      float w = weightFromLength(L);
      float dv = loadVal(n,c) - f0[c];

      rhs0 += w*dx.x*dv;
      rhs1 += w*dx.y*dv;
      rhs2 += w*dx.z*dv;
    }

    vec3 rhs = vec3(rhs0,rhs1,rhs2);
    vec3 g = invG * rhs;   // ?f = G^{-1} rhs

    uint o = base + uint(c*3);
    GradBuf[o]=g.x; GradBuf[o+1u]=g.y; GradBuf[o+2u]=g.z;
  }
}