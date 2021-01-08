#pragma once


// TODO: Refactor into header.
#define PRIMARY_RAY 0
#define OCCLUSION_RAY 1
#define MAX_PATH_DEPTH 5

struct shaders_t {
  const char* spirv_data;
  size_t spirv_size;

  const char* rchit;
  const char* rmiss;
};

extern const shaders_t shaders;