#version 430
layout(local_size_x=8, local_size_y=8, local_size_z=8) in;
layout(std430, binding=0) readonly buffer Phi { float phi[]; };
layout(std430, binding=1) writeonly buffer Grad { vec4 grad[]; };
uniform ivec3 uDims;
uniform vec3 uSpacing;

int idx(int i,int j,int k){ return (k*uDims.y + j)*uDims.x + i; }

void main(){
  uvec3 g=gl_GlobalInvocationID;
  int i=int(g.x), j=int(g.y), k=int(g.z);
  if(i>=uDims.x||j>=uDims.y||k>=uDims.z) return;
  int id=idx(i,j,k);
  float dx=uSpacing.x, dy=uSpacing.y, dz=uSpacing.z;
  float gx=0.0, gy=0.0, gz=0.0;
  if(uDims.x>1){
    if(i==0) gx=(phi[idx(i+1,j,k)]-phi[id])/dx;
    else if(i==uDims.x-1) gx=(phi[id]-phi[idx(i-1,j,k)])/dx;
    else gx=(phi[idx(i+1,j,k)]-phi[idx(i-1,j,k)])/(2.0*dx);
  }
  if(uDims.y>1){
    if(j==0) gy=(phi[idx(i,j+1,k)]-phi[id])/dy;
    else if(j==uDims.y-1) gy=(phi[id]-phi[idx(i,j-1,k)])/dy;
    else gy=(phi[idx(i,j+1,k)]-phi[idx(i,j-1,k)])/(2.0*dy);
  }
  if(uDims.z>1){
    if(k==0) gz=(phi[idx(i,j,k+1)]-phi[id])/dz;
    else if(k==uDims.z-1) gz=(phi[id]-phi[idx(i,j,k-1)])/dz;
    else gz=(phi[idx(i,j,k+1)]-phi[idx(i,j,k-1)])/(2.0*dz);
  }
  grad[id]=vec4(gx,gy,gz,0.0);
}