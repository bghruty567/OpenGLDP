#version 430
layout(local_size_x=8, local_size_y=8, local_size_z=8) in;//设置一个工作组包含512个线程
layout(std430, binding=0) readonly buffer PointsBuf { float P[]; };//点坐标(处理单元数据时是单元中心坐标)
layout(std430, binding=1) readonly buffer ValuesBuf { float V[]; };//点值
layout(std430, binding=2) writeonly  buffer GradBuf   { float G[]; };//梯度
uniform uvec3 uDims;//数据维度
uniform int   uNumComponents;//每个点的数据的分量数

uint idx(uint i,uint j,uint k){ 
    return (k*uDims.y + j)*uDims.x + i; 
}//计算点索引
vec3 loadP(uint id){ //获取点坐标
    uint b=id*3u; return vec3(P[b],P[b+1u],P[b+2u]);
    }
float loadV(uint id,int c){ //获取点值
    return V[id*uint(uNumComponents)+uint(c)]; 
    }

    //计算每个方向的前一个和后一个点的索引以及插值因子
    //插值因子参考vtkGradientFilter中的实现，边界点的插值因子为1.0，内部点的插值因子为0.5
    //参数意思分别为：当前点索引、步长、维度大小、当前坐标、前一个点索引、后一个点索引、插值因子
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

  uint id = idx(i,j,k);//计算当前点的索引
  uint ip,im,jp,jm,kp,km;//前一个和后一个点的索引
  float fXi,fEta,fZeta;//插值因子
  
  pick1D(id, 1u,          uDims.x, i, ip, im, fXi);
  pick1D(id, uDims.x,     uDims.y, j, jp, jm, fEta);
  pick1D(id, uDims.x*uDims.y, uDims.z, k, kp, km, fZeta);

  //加载当前点和邻域点的坐标
  vec3 xp = loadP(ip), xm = loadP(im);
  vec3 yp = loadP(jp), ym = loadP(jm);
  vec3 zp = loadP(kp), zm = loadP(km);

  //计算雅可比矩阵的行列式和逆矩阵元素
  float xxi  = fXi  * (xp.x - xm.x);
  float yxi  = fXi  * (xp.y - xm.y);
  float zxi  = fXi  * (xp.z - xm.z);
  float xeta = fEta * (yp.x - ym.x);
  float yeta = fEta * (yp.y - ym.y);
  float zeta = fEta * (yp.z - ym.z);
  float xzeta= fZeta* (zp.x - zm.x);
  float yzeta= fZeta* (zp.y - zm.y);
  float zzeta= fZeta* (zp.z - zm.z);

  //雅可比矩阵的行列式
  float aj = xxi*yeta*zzeta + yxi*zeta*xzeta + zxi*xeta*yzeta
           - zxi*yeta*xzeta - yxi*xeta*zzeta - xxi*zeta*yzeta;
  //避免除以零
  float invAj = (aj!=0.0)?(1.0/aj):0.0;
  //雅可比矩阵的逆矩阵元素
  float xix   =  invAj*(yeta*zzeta - zeta*yzeta);
  float xiy   = -invAj*(xeta*zzeta - zeta*xzeta);
  float xiz   =  invAj*(xeta*yzeta - yeta*xzeta);
  float etax  = -invAj*(yxi*zzeta - zxi*yzeta);
  float etay  =  invAj*(xxi*zzeta - zxi*xzeta);
  float etaz  = -invAj*(xxi*yzeta - yxi*xzeta);
  float zetax =  invAj*(yxi*zeta - zxi*yeta);
  float zetay = -invAj*(xxi*zeta - zxi*xeta);
  float zetaz =  invAj*(xxi*yeta - yxi*xeta);
  //计算梯度
  uint base = id*uint(3*uNumComponents);

  for(int c=0;c<uNumComponents;++c){
    float dXi   = (uDims.x>1u)? (fXi  * (loadV(ip,c)-loadV(im,c)))   : 0.0;
    float dEta  = (uDims.y>1u)? (fEta * (loadV(jp,c)-loadV(jm,c)))   : 0.0;
    float dZeta = (uDims.z>1u)? (fZeta* (loadV(kp,c)-loadV(km,c)))   : 0.0;

    float gx = xix*dXi + etax*dEta + zetax*dZeta;
    float gy = xiy*dXi + etay*dEta + zetay*dZeta;
    float gz = xiz*dXi + etaz*dEta + zetaz*dZeta;

    uint o = base + uint(c*3);
    G[o+0]=gx; G[o+1]=gy; G[o+2]=gz;
  }
}

/*
整个算法的原理为：对于每个点，计算其在三个维度上的前一个和后一个点的索引以及插值因子，然后加载这些点的坐标，计算雅可比矩阵的行列式和逆矩阵元素。
最后根据这些元素计算梯度。边界点的插值因子为1.0，内部点的插值因子为0.5，以确保边界点的梯度计算正确。
实际上是一种链式法则的应用，对于非均匀的网格，先求雅可比矩阵的行列式，相当于求空间坐标与参数坐标的导数，然后求雅可比矩阵的逆矩阵元素，相当于求参数坐标与点坐标的导数。
最后根据链式法则，先计算值的差距（dXi、dEta、dZeta），再乘以雅可比矩阵的逆矩阵元素，得到点坐标的梯度（gx、gy、gz）。
*/