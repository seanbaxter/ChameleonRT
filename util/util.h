#pragma once

#include <string>
#include <vector>
#include <stdint.h>
#include <glm/glm.hpp>

// Format the count as #G, #M, #K, depending on its magnitude
std::string pretty_print_count(const double count);

uint64_t align_to(uint64_t val, uint64_t align);

void ortho_basis(glm::vec3 &v_x, glm::vec3 &v_y, const glm::vec3 &n);

void canonicalize_path(std::string &path);

std::string get_file_extension(const std::string &fname);

std::string get_cpu_brand();

float srgb_to_linear(const float x);

float linear_to_srgb(const float x);

#ifdef ENABLE_OPEN_IMAGE_DENOISE
void oidn_init();
void oidn_denoise(std::vector<float> &input, uint32_t width, uint32_t height, std::vector<float> &output);
void oidn_denoise(std::vector<uint32_t> &input, uint32_t width, uint32_t height, std::vector<uint32_t> &output);
#endif