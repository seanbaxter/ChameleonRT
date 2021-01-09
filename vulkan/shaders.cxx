#include <cstdint>
#include <cmath>
#include "shaders.hxx"
#include "../util/texture_channel_mask.h"

////////////////////////////////////////////////////////////////////////////////


// https://github.com/ospray/ospray/blob/master/ospray/math/random.ih
uint32_t murmur_hash3_mix(uint32_t hash, uint32_t k) {
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint32_t r1 = 15;
    const uint32_t r2 = 13;
    const uint32_t m = 5;
    const uint32_t n = 0xe6546b64;

    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;

    hash ^= k;
    hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;

    return hash;
}

uint32_t murmur_hash3_finalize(uint32_t hash) {
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;

    return hash;
}

uint32_t lcg_random(uint& state) {
    const uint32_t m = 1664525;
    const uint32_t n = 1013904223;
    state = state * m + n;
    return state;
}

float lcg_randomf(uint& state) {
  return ldexp(float(lcg_random(state)), -32);
}

uint get_rng(int frame_id, ivec2 pixel, ivec2 dims) {
  uint state = murmur_hash3_mix(0, pixel.x + pixel.y * dims.x);
  state = murmur_hash3_mix(state, frame_id);
  state = murmur_hash3_finalize(state);

  return state;
}

////////////////////////////////////////////////////////////////////////////////

struct MaterialParams {
  vec3 base_color;
  float metallic;

  float specular;
  float roughness;
  float specular_tint;
  float anisotropy;

  float sheen;
  float sheen_tint;
  float clearcoat;
  float clearcoat_gloss;

  float ior;
  float specular_transmission;
  vec2 pad;
};

struct ViewParams {
  vec4 cam_pos;
  vec4 cam_du;
  vec4 cam_dv;
  vec4 cam_dir_top_left;
  int frame_id;
};

// Quad-shaped light source
struct QuadLight {
  vec4 emission;
  vec4 position;
  vec4 normal;
  vec4 v_x;
  vec4 v_y;
};

struct RayPayload {
  vec3 normal;
  float dist;
  vec2 uv;
  uint material_id;
  float pad;
};

[[using spirv: uniform, binding(0)]] 
accelerationStructure scene;

[[using spirv: uniform, binding(1), writeonly, format(rgba8)]]
image2D framebuffer;

[[using spirv: uniform, binding(2), format(rgba32f)]]
image2D accum_buffer;

[[using spirv: uniform, binding(3)]]
ViewParams viewParams;

[[using spirv: buffer, binding(4)]]
MaterialParams material_params[];

[[using spirv: buffer, binding(5)]]
QuadLight lights[];

[[using spirv: uniform, writeonly, binding(6), format(r16ui)]]
uimage2D ray_stats;

[[using spirv: uniform, binding(0), set(1)]]
sampler2D textures[];

[[using spriv: RayPayload]]
RayPayload rayPayload;

[[spirv::shaderRecord]]
uint32_t num_lights;

template<bool report_stats>
[[spirv::rgen]]
void rgen_shader() {
  const ivec2 pixel = ivec2(glray_LaunchID.xy);
  const ivec2 dims = ivec2(glray_LaunchSize.xy);

  // Initialize the RNG.
  uint rng = get_rng(viewParams.frame_id, pixel, dims);

  // Perturb the ray.
  vec2 d = (vec2(pixel) + vec2(lcg_randomf(rng), lcg_randomf(rng))) / 
    vec2(dims);

  vec3 ray_origin = viewParams.cam_pos.xyz;
  vec3 ray_dir = normalize(
    d.x * viewParams.cam_du.xyz + 
    d.y * viewParams.cam_dv.xyz +
          viewParams.cam_dir_top_left.xyz
  );

  float t_min = 0;
  float t_max = 1e20f;

  int ray_count;

  for(int bounce = 0; bounce < MAX_PATH_DEPTH; bounce) {
    glray_Trace(scene, gl_RayFlagsOpaque, 0xff, PRIMARY_RAY, 1, PRIMARY_RAY,
      ray_origin, t_min, ray_dir, t_max, PRIMARY_RAY);

    ++ray_count;
  }

  if constexpr(report_stats)
    imageStore(ray_stats, pixel, uvec4(ray_count));
}


////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////

[[using spirv: rayPayloadIn, location(PRIMARY_RAY)]]
RayPayload rayPayloadIn;

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

[[spirv::hitAttribute]]
vec2 attrib;      // barycentric .y and .z coordinates of the hit.

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

////////////////////////////////////////////////////////////////////////////////

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

[[using spirv: rayPayloadIn, location(OCCLUSION_RAY)]]
bool occlusion_hit;

[[spirv::rmiss]]
void rmiss_occlusion() {
  occlusion_hit = false;
}

const shaders_t shaders {
  __spirv_data,
  __spirv_size,

  @spirv(rgen_shader<true>),
  @spirv(rchit_shader),
  @spirv(rmiss_shader),
  @spirv(rmiss_occlusion)
};
