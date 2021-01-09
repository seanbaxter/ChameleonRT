#pragma once
#include <cmath>

#define EPSILON 0.0001f
#define PRIMARY_RAY 0
#define OCCLUSION_RAY 1
#define MAX_PATH_DEPTH 5

inline float linear_to_srgb(float x) {
	if (x <= 0.0031308f) {
		return 12.92f * x;
	}
	return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

inline void ortho_basis(vec3& v_x, vec3& v_y, vec3 n) {
	v_y = vec3(0, 0, 0);

	if (n.x < 0.6f && n.x > -0.6f) {
		v_y.x = 1.f;
	} else if (n.y < 0.6f && n.y > -0.6f) {
		v_y.y = 1.f;
	} else if (n.z < 0.6f && n.z > -0.6f) {
		v_y.z = 1.f;
	} else {
		v_y.x = 1.f;
	}
	v_x = normalize(cross(v_y, n));
	v_y = normalize(cross(n, v_x));
}

inline float luminance(vec3 c) {
  return dot(vec3(0.2126f, 0.7152f,  0.0722f), c);
}

inline float pow2(float x) {
	return x * x;
}
