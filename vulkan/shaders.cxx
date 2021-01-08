#include <cstdint>
#include <cmath>
#include "shaders.hxx"
#include "../util/texture_channel_mask.h"

struct RayPayload {
  vec3 normal;
  float dist;
  vec2 uv;
  uint material_id;
  float pad;
};

[[using spirv: rayPayloadIn, location(PRIMARY_RAY)]]
RayPayload rayPayloadIn;

[[spirv::hitAttribute]]
vec3 attrib;      // barycentric .y and .z coordinates of the hit.

struct SBT {
  // These are physical storage buffer pointers.
  const vec3* verts;
  const uvec3* indices;
  const vec3* normals;
  const vec2* uvs;
  uint32_t num_normals;
  uint32_t num_uvs;
  uint32_t material_id;
};

[[spirv::shaderRecord]]
SBT sbt;

[[spirv::rchit]]
void rchit_shader() {
  uvec3 idx = sbt.indices[glray_PrimitiveID]; 
  vec3  va  = sbt.verts[idx.x];
  vec3  vb  = sbt.verts[idx.y];
  vec3  vc  = sbt.verts[idx.z];
  vec3  n   = normalize(cross(vb - va, vc - va));

  vec2 uv { };
  if(sbt.num_uvs > 0) {
    vec2 uva = sbt.uvs[idx.x];
    vec2 uvb = sbt.uvs[idx.y];
    vec2 uvc = sbt.uvs[idx.z];

    vec3 bary(1 - attrib.x - attrib.y, attrib.xy);
    uv = mat3x2(uva, uvb, uvc) * bary;
  }

  mat3 inv_transp = transpose(mat3(glray_WorldToObject));
  
  rayPayloadIn.normal = normalize(inv_transp * n);
  rayPayloadIn.dist = glray_Tmax;
  rayPayloadIn.uv = uv;
  rayPayloadIn.material_id = sbt.material_id;
}

[[spirv::rmiss]]
void rmiss_shader() {
  rayPayloadIn.dist = -1;

  vec3 dir = glray_WorldRayDirection;
  float u = (1 + atan2(dir.x, -dir.z) * M_1_PIf32);
  float v = acos(dir.y) * M_1_PIf32;

  int check_x = (int)(u * 10);
  int check_y = (int)(v * 10);
  if(dir.y > -.1f && (check_x + check_y) % 2 == 0)
    rayPayloadIn.normal.rgb = vec3(.5f);
  else
    rayPayloadIn.normal.rgb = vec3(.1f);
}

//[[using spirv: rayPayloadIn, location(OCCLUSION_RAY)]]
//bool occlusion_hit;
//
//[[spirv::rmiss]]
//void rmiss_occlusion() {
//  occlusion_hit = false;
//}
//
const shaders_t shaders {
  __spirv_data,
  __spirv_size,

  @spirv(rchit_shader),
  nullptr
  // @spirv(rmiss_shader)
};
