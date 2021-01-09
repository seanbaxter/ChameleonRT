#pragma once
#include "util.hxx"

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