#version 430
layout(local_size_x=8, local_size_y=8, local_size_z=8) in;
layout(std430, binding=0) readonly buffer PointsBuf { float P[]; };
layout(std430, binding=1) readonly buffer ValuesBuf { float V[]; };
layout(std430, binding=2) writeonly  buffer GradBuf   { float G[]; };
uniform uvec3 uDims;
uniform int   uNumComponents;

uint idx(uint i,uint j,uint k){ return (k*uDims.y + j)*uDims.x + i; }
vec3 loadP(uint id){ uint b=id*3u; return vec3(P[b],P[b+1u],P[b+2u]); }
float loadV(uint id,int c){ return V[id*uint(uNumComponents)+uint(c)]; }

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
  pick1D(id, 1u,          uDims.x, i, ip, im, fXi);
  pick1D(id, uDims.x,     uDims.y, j, jp, jm, fEta);
  pick1D(id, uDims.x*uDims.y, uDims.z, k, kp, km, fZeta);

  vec3 xp = loadP(ip), xm = loadP(im);
  vec3 yp = loadP(jp), ym = loadP(jm);
  vec3 zp = loadP(kp), zm = loadP(km);

  float xxi  = fXi  * (xp.x - xm.x);
  float yxi  = fXi  * (xp.y - xm.y);
  float zxi  = fXi  * (xp.z - xm.z);
  float xeta = fEta * (yp.x - ym.x);
  float yeta = fEta * (yp.y - ym.y);
  float zeta = fEta * (yp.z - ym.z);
  float xzeta= fZeta* (zp.x - zm.x);
  float yzeta= fZeta* (zp.y - zm.y);
  float zzeta= fZeta* (zp.z - zm.z);

  float aj = xxi*yeta*zzeta + yxi*zeta*xzeta + zxi*xeta*yzeta
           - zxi*yeta*xzeta - yxi*xeta*zzeta - xxi*zeta*yzeta;
  float invAj = (aj!=0.0)?(1.0/aj):0.0;

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