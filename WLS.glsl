#version 430
layout(local_size_x=256) in;
layout(std430, binding=0) readonly buffer Pos { vec4 pos[]; };
layout(std430, binding=1) readonly buffer Off { int off[]; };
layout(std430, binding=2) readonly buffer Nbr { int nbr[]; };
layout(std430, binding=3) readonly buffer Phi { float phi[]; };
layout(std430, binding=4) writeonly buffer Grad { vec4 grad[]; };
uniform int uN;
uniform float uWExp;
uniform float uLambda;

bool chol3(in mat3 A, out mat3 L){
  float l00=A[0][0]; if(l00<=0.0) return false; l00=sqrt(l00); L[0][0]=l00; L[1][0]=A[1][0]/l00; L[2][0]=A[2][0]/l00;
  float a11=A[1][1]-L[1][0]*L[1][0]; if(a11<=0.0) return false; float l11=sqrt(a11); L[1][1]=l11; L[2][1]=(A[2][1]-L[2][0]*L[1][0])/l11;
  float a22=A[2][2]-L[2][0]*L[2][0]-L[2][1]*L[2][1]; if(a22<=0.0) return false; float l22=sqrt(a22); L[2][2]=l22;
  L[0][1]=0.0; L[0][2]=0.0; L[1][2]=0.0; return true;
}
vec3 fwd3(in mat3 L, vec3 b){ vec3 y; y.x=b.x/L[0][0]; y.y=(b.y-L[1][0]*y.x)/L[1][1]; y.z=(b.z-L[2][0]*y.x-L[2][1]*y.y)/L[2][2]; return y; }
vec3 bwd3(in mat3 L, vec3 y){ vec3 x; x.z=y.z/L[2][2]; x.y=(y.y-L[2][1]*x.z)/L[1][1]; x.x=(y.x-L[1][0]*x.y-L[2][0]*x.z)/L[0][0]; return x; }

void main(){
  uint iu=gl_GlobalInvocationID.x;
  if(int(iu)>=uN) return;
  int i=int(iu);
  int b=off[i], e=off[i+1];
  vec3 pi=pos[i].xyz;
  float phii=phi[i];
  mat3 S=mat3(0.0);
  vec3 t=vec3(0.0);
  int cnt=0;
  for(int k=b;k<e;++k){
    int j=nbr[k];
    vec3 d=pos[j].xyz-pi;
    float n2=dot(d,d);
    if(n2<=1e-12) continue;
    float len=sqrt(n2);
    float w=1.0/pow(max(len,1e-6), uWExp);
    float bv=phi[j]-phii;
    S+=w*mat3(d.x*d.x, d.x*d.y, d.x*d.z,
              d.y*d.x, d.y*d.y, d.y*d.z,
              d.z*d.x, d.z*d.y, d.z*d.z);
    t+=w*bv*d;
    cnt++;
  }
  S+=uLambda*mat3(1.0);
  vec3 g=vec3(0.0);
  if(cnt>=3){
    mat3 L;
    if(chol3(S,L)){
      vec3 y=fwd3(L,t);
      g=bwd3(L,y);
    }
  }
  grad[i]=vec4(g,0.0);
}