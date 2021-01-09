#include <cstdint>
#include <cmath>
#include "shaders.hxx"
#include "../util/texture_channel_mask.h"
#include "disney_bsdf.hxx"
#include "lights.hxx"

////////////////////////////////////////////////////////////////////////////////



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

[[using spirv: rayPayload, location(PRIMARY_RAY)]]
RayPayload rayPayload;

[[using spirv: rayPayload, location(OCCLUSION_RAY)]]
bool occlusion_hit;

[[spirv::shaderRecord]]
uint32_t num_lights;


inline float textured_scalar_param(float x, vec2 uv) {
  const uint32_t mask = floatBitsToUint(x);
  if (IS_TEXTURED_PARAM(mask) != 0) {
    uint32_t tex_id = GET_TEXTURE_ID(mask);
    uint32_t channel = GET_TEXTURE_CHANNEL(mask);
    return texture(textures[tex_id], uv)[channel];
  }
  return x;
}

inline DisneyMaterial unpack_material(uint id, vec2 uv) {
  MaterialParams p = material_params[id];

  DisneyMaterial mat;

  uint32_t mask = floatBitsToUint(p.base_color.x);
  if (IS_TEXTURED_PARAM(mask) != 0) {
    uint32_t tex_id = GET_TEXTURE_ID(mask);
    uint32_t channel = GET_TEXTURE_CHANNEL(mask);
    mat.base_color = texture(textures[tex_id], uv).rgb;
  } else {
    mat.base_color = p.base_color;
  }

  mat.metallic = textured_scalar_param(p.metallic, uv);
  mat.specular = textured_scalar_param(p.specular, uv);
  mat.roughness = textured_scalar_param(p.roughness, uv);
  mat.specular_tint = textured_scalar_param(p.specular_tint, uv);
  mat.anisotropy = textured_scalar_param(p.anisotropy, uv);
  mat.sheen = textured_scalar_param(p.sheen, uv);
  mat.sheen_tint = textured_scalar_param(p.sheen_tint, uv);
  mat.clearcoat = textured_scalar_param(p.clearcoat, uv);
  mat.clearcoat_gloss = textured_scalar_param(p.clearcoat_gloss, uv);
  mat.ior = textured_scalar_param(p.ior, uv);
  mat.specular_transmission = textured_scalar_param(p.specular_transmission, uv);

  return mat;
}

inline vec3 sample_direct_light(DisneyMaterial mat, vec3 hit_p, vec3 n, 
  vec3 v_x, vec3 v_y, vec3 w_o, uint& ray_count, uint& rng) {

  vec3 illum = vec3(0.f);

  uint32_t light_id = uint32_t(lcg_randomf(rng) * num_lights);
  light_id = min(light_id, num_lights - 1);
  QuadLight light = lights[light_id];

  uint32_t occlusion_flags = 
    gl_RayFlagsOpaque | 
    gl_RayFlagsTerminateOnFirstHit | 
    gl_RayFlagsSkipClosestHitShader;

  // Sample the light to compute an incident light ray to this point
  {
    vec3 light_pos = sample_quad_light_position(light,
                vec2(lcg_randomf(rng), lcg_randomf(rng)));
    vec3 light_dir = light_pos - hit_p;
    float light_dist = length(light_dir);
    light_dir = normalize(light_dir);

    float light_pdf = quad_light_pdf(light, light_pos, hit_p, light_dir);
    float bsdf_pdf = disney_pdf(mat, n, w_o, light_dir, v_x, v_y);

    occlusion_hit = true;
    glray_Trace(scene, occlusion_flags, 0xff, PRIMARY_RAY, 1, OCCLUSION_RAY,
      hit_p, EPSILON, light_dir, light_dist, OCCLUSION_RAY);

    ++ray_count;

    if (light_pdf >= EPSILON && bsdf_pdf >= EPSILON && !occlusion_hit) {
      vec3 bsdf = disney_brdf(mat, n, w_o, light_dir, v_x, v_y);
      float w = power_heuristic(1.f, light_pdf, 1.f, bsdf_pdf);
      illum = bsdf * light.emission.rgb * abs(dot(light_dir, n)) * w / light_pdf;
    }
  }

  // Sample the BRDF to compute a light sample as well
  {
    vec3 w_i;
    float bsdf_pdf;
    vec3 bsdf = sample_disney_brdf(mat, n, w_o, v_x, v_y, rng, w_i, bsdf_pdf);
    
    float light_dist;
    vec3 light_pos;
    if (any(bsdf > 0) && bsdf_pdf >= EPSILON && 
      quad_intersect(light, hit_p, w_i, light_dist, light_pos)) {

      float light_pdf = quad_light_pdf(light, light_pos, hit_p, w_i);
      if (light_pdf >= EPSILON) {
        float w = power_heuristic(1.f, bsdf_pdf, 1.f, light_pdf);
        occlusion_hit = true;
        glray_Trace(scene, occlusion_flags, 0xff, PRIMARY_RAY, 1, 
          OCCLUSION_RAY, hit_p, EPSILON, w_i, light_dist, OCCLUSION_RAY);

        ++ray_count;

        if (!occlusion_hit) 
          illum += bsdf * light.emission.rgb * abs(dot(w_i, n)) * w / bsdf_pdf;
      }
    }
  }
  return illum;
}


template<bool report_stats>
[[spirv::rgen]]
void rgen_shader() {
  ivec2 pixel = ivec2(glray_LaunchID.xy);
  ivec2 dims = ivec2(glray_LaunchSize.xy);

  // Initialize the RNG.
  uint rng = get_rng(viewParams.frame_id, pixel, dims);

  // Perturb the ray.
  vec2 d = (vec2(pixel) + vec2(lcg_randomf(rng), lcg_randomf(rng))) / 
    vec2(dims);

  vec3 ray_origin = viewParams.cam_pos.xyz;
  vec3 ray_dir = normalize(
    (d.x + d.y) * viewParams.cam_du.xyz + viewParams.cam_dir_top_left.xyz
  );

  float t_min = 0;
  float t_max = 1e20f;

  uint ray_count = 0;
  vec3 illum(0);
  vec3 path_throughput(1);
  for(int bounce = 0; bounce < MAX_PATH_DEPTH; bounce) {
    glray_Trace(scene, gl_RayFlagsOpaque, 0xff, PRIMARY_RAY, 1, PRIMARY_RAY,
      ray_origin, t_min, ray_dir, t_max, PRIMARY_RAY);

    ++ray_count;

    // If we hit nothing, include the scene background color from the miss 
    // shader
    if (rayPayload.dist < 0) {
      illum += path_throughput * rayPayload.normal.rgb;
      break;
    }

    vec3 w_o = -ray_dir;
    vec3 hit_p = ray_origin + rayPayload.dist * ray_dir;
    DisneyMaterial mat = unpack_material(rayPayload.material_id, rayPayload.uv);

    vec3 v_x, v_y;
    vec3 v_z = rayPayload.normal;
    // For opaque objects (or in the future, thin ones) make the normal face forward
    if (mat.specular_transmission == 0 && dot(w_o, v_z) < 0)
      v_z = -v_z;
    
    ortho_basis(v_x, v_y, v_z);

    illum += path_throughput * sample_direct_light(mat, hit_p, v_z, v_x,
      v_y, w_o, ray_count, rng);

    vec3 w_i;
    float pdf;
    vec3 bsdf = sample_disney_brdf(mat, v_z, w_o, v_x, v_y, rng, w_i, pdf);
    if (pdf == 0.f || bsdf == vec3(0)) {
      break;
    }
    path_throughput *= bsdf * abs(dot(w_i, v_z)) / pdf;

    ray_origin = hit_p;
    ray_dir = w_i;
    t_min = EPSILON;
    t_max = 1e20f;
    ++bounce;

    // Russian roulette termination
    if (bounce > 3) {
      const float q = max(0.05f, 1 - max(path_throughput.x, max(path_throughput.y, path_throughput.z)));
      if (lcg_randomf(rng) < q) {
          break;
      }
      path_throughput = path_throughput / (1.f - q);
    }
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
bool occlusion_hit_in;

[[spirv::rmiss]]
void rmiss_occlusion() {
  occlusion_hit_in = false;
}

const shaders_t shaders {
  __spirv_data,
  __spirv_size,

  @spirv(rgen_shader<true>),
  @spirv(rchit_shader),
  @spirv(rmiss_shader),
  @spirv(rmiss_occlusion)
};
