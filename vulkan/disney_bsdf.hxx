#pragma once
#include "util.hxx"
#include "lcg_rng.hxx"

// #include "util.glsl"
// #include "lcg_rng.glsl"

/* Disney BSDF functions, for additional details and examples see:
 * - https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
 * - https://www.shadertoy.com/view/XdyyDd
 * - https://github.com/wdas/brdf/blob/master/src/brdfs/disney.brdf
 * - https://schuttejoe.github.io/post/disneybsdf/
 *
 * Variable naming conventions with the Burley course notes:
 * V -> w_o
 * L -> w_i
 * H -> w_h
 */

struct DisneyMaterial {
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
};


inline bool same_hemisphere(vec3 w_o, vec3 w_i, vec3 n) {
	return dot(w_o, n) * dot(w_i, n) > 0.f;
}

// Sample the hemisphere using a cosine weighted distribution,
// returns a vector in a hemisphere oriented about (0, 0, 1)
inline vec3 cos_sample_hemisphere(vec2 u) {
	vec2 s = 2 * u - 1;
	vec2 d;
	float radius = 0;
	float theta = 0;
	if (s.x == 0.f && s.y == 0.f) {
		d = s;
	} else {
		if (abs(s.x) > abs(s.y)) {
			radius = s.x;
			theta  = M_PIf32 / 4 * (s.y / s.x);
		} else {
			radius = s.y;
			theta  = M_PIf32 / 2 - M_PIf32 / 4 * (s.x / s.y);
		}
	}
	d = radius * vec2(cos(theta), sin(theta));
	return vec3(d.x, d.y, sqrt(max(0.f, 1.f - d.x * d.x - d.y * d.y)));
}

inline vec3 spherical_dir(float sin_theta, float cos_theta, float phi) {
	return vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

inline float power_heuristic(float n_f, float pdf_f, float n_g, float pdf_g) {
	float f = n_f * pdf_f;
	float g = n_g * pdf_g;
	return (f * f) / (f * f + g * g);
}

inline float schlick_weight(float cos_theta) {
	return pow(clamp(1.f - cos_theta, 0.f, 1.f), 5.f);
}

// Complete Fresnel Dielectric computation, for transmission at ior near 1
// they mention having issues with the Schlick approximation.
// eta_i: material on incident side's ior
// eta_t: material on transmitted side's ior
inline float fresnel_dielectric(float cos_theta_i, float eta_i, float eta_t) {
	float g = pow2(eta_t) / pow2(eta_i) - 1.f + pow2(cos_theta_i);
	if (g < 0.f) {
		return 1.f;
	}
	return 0.5f * pow2(g - cos_theta_i) / pow2(g + cos_theta_i)
		* (1.f + pow2(cos_theta_i * (g + cos_theta_i) - 1.f) / pow2(cos_theta_i * (g - cos_theta_i) + 1.f));
}

// D_GTR1: Generalized Trowbridge-Reitz with gamma=1
// Burley notes eq. 4
inline float gtr_1(float cos_theta_h, float alpha) {
	if (alpha >= 1.f) {
		return M_1_PIf32;
	}
	float alpha_sqr = alpha * alpha;
	return M_1_PIf32 * (alpha_sqr - 1.f) / (log(alpha_sqr) * (1.f + (alpha_sqr - 1.f) * cos_theta_h * cos_theta_h));
}

// D_GTR2: Generalized Trowbridge-Reitz with gamma=2
// Burley notes eq. 8
inline float gtr_2(float cos_theta_h, float alpha) {
	float alpha_sqr = alpha * alpha;
	return M_1_PIf32 * alpha_sqr / pow2(1.f + (alpha_sqr - 1.f) * cos_theta_h * cos_theta_h);
}

// D_GTR2 Anisotropic: Anisotropic generalized Trowbridge-Reitz with gamma=2
// Burley notes eq. 13
inline float gtr_2_aniso(float h_dot_n, float h_dot_x, float h_dot_y, vec2 alpha) {
	return M_1_PIf32 / (alpha.x * alpha.y
			* pow2(pow2(h_dot_x / alpha.x) + pow2(h_dot_y / alpha.y) + h_dot_n * h_dot_n));
}

inline float smith_shadowing_ggx(float n_dot_o, float alpha_g) {
	float a = alpha_g * alpha_g;
	float b = n_dot_o * n_dot_o;
	return 1.f / (n_dot_o + sqrt(a + b - a * b));
}

inline float smith_shadowing_ggx_aniso(float n_dot_o, float o_dot_x, float o_dot_y, vec2 alpha) {
	return 1.f / (n_dot_o + sqrt(pow2(o_dot_x * alpha.x) + pow2(o_dot_y * alpha.y) + pow2(n_dot_o)));
}

// Sample a reflection direction the hemisphere oriented along n and spanned by v_x, v_y using the random samples in s
inline vec3 sample_lambertian_dir(vec3 n, vec3 v_x, vec3 v_y, vec2 s) {
	const vec3 hemi_dir = normalize(cos_sample_hemisphere(s));
	return hemi_dir.x * v_x + hemi_dir.y * v_y + hemi_dir.z * n;
}

// Sample the microfacet normal vectors for the various microfacet distributions
inline vec3 sample_gtr_1_h(vec3 n, vec3 v_x, vec3 v_y, float alpha, vec2 s) {
	float phi_h = 2.f * M_PIf32 * s.x;
	float alpha_sqr = alpha * alpha;
	float cos_theta_h_sqr = (1.f - pow(alpha_sqr, 1.f - s.y)) / (1.f - alpha_sqr);
	float cos_theta_h = sqrt(cos_theta_h_sqr);
	float sin_theta_h = 1.f - cos_theta_h_sqr;
	vec3 hemi_dir = normalize(spherical_dir(sin_theta_h, cos_theta_h, phi_h));
	return hemi_dir.x * v_x + hemi_dir.y * v_y + hemi_dir.z * n;
}

inline vec3 sample_gtr_2_h(vec3 n, vec3 v_x, vec3 v_y, float alpha, vec2 s) {
	float phi_h = 2.f * M_PIf32 * s.x;
	float cos_theta_h_sqr = (1.f - s.y) / (1.f + (alpha * alpha - 1.f) * s.y);
	float cos_theta_h = sqrt(cos_theta_h_sqr);
	float sin_theta_h = 1.f - cos_theta_h_sqr;
	vec3 hemi_dir = normalize(spherical_dir(sin_theta_h, cos_theta_h, phi_h));
	return hemi_dir.x * v_x + hemi_dir.y * v_y + hemi_dir.z * n;
}

inline vec3 sample_gtr_2_aniso_h(vec3 n, vec3 v_x, vec3 v_y, vec2 alpha, vec2 s) {
	float x = 2 * M_PIf32 * s.x;
	vec3 w_h = sqrt(s.y / (1 - s.y)) * (alpha.x * cos(x) * v_x + alpha.y * sin(x) * v_y) + n;
	return normalize(w_h);
}

inline float lambertian_pdf(vec3 w_i, vec3 n) {
	float d = dot(w_i, n);
	if(d > 0.f) {
		return d * M_1_PIf32;
	}
	return 0.f;
}

inline float gtr_1_pdf(vec3 w_o, vec3 w_i, vec3 n, float alpha) {
	if(!same_hemisphere(w_o, w_i, n))
		return 0;
	
	vec3 w_h = normalize(w_i + w_o);
	float cos_theta_h = dot(n, w_h);
	float d = gtr_1(cos_theta_h, alpha);
	return d * cos_theta_h / (4 * dot(w_o, w_h));
}

inline float gtr_2_pdf(vec3 w_o, vec3 w_i, vec3 n, float alpha) {
	if(!same_hemisphere(w_o, w_i, n))
		return 0;
	
	vec3 w_h = normalize(w_i + w_o);
	float cos_theta_h = dot(n, w_h);
	float d = gtr_2(cos_theta_h, alpha);
	return d * cos_theta_h / (4 * dot(w_o, w_h));
}

inline float gtr_2_transmission_pdf(vec3 w_o, vec3 w_i, vec3 n, float alpha, float ior) {
	if(same_hemisphere(w_o, w_i, n))
		return 0;
	
	bool entering = dot(w_o, n) > 0;
	float eta_o = entering ? 1 : ior;
	float eta_i = entering ? ior : 1;
	vec3 w_h = normalize(w_o + w_i * eta_i / eta_o);
	float cos_theta_h = abs(dot(n, w_h));
	float i_dot_h = dot(w_i, w_h);
	float o_dot_h = dot(w_o, w_h);
	float d = gtr_2(cos_theta_h, alpha);
	float dwh_dwi = o_dot_h * pow2(eta_o) / pow2(eta_o * o_dot_h + eta_i * i_dot_h);
	return d * cos_theta_h * abs(dwh_dwi);
}

inline float gtr_2_aniso_pdf(vec3 w_o, vec3 w_i, vec3 n, vec3 v_x, vec3 v_y, vec2 alpha) {
	if (!same_hemisphere(w_o, w_i, n))
		return 0.f;
	
	vec3 w_h = normalize(w_i + w_o);
	float cos_theta_h = dot(n, w_h);
	float d = gtr_2_aniso(cos_theta_h, abs(dot(w_h, v_x)), abs(dot(w_h, v_y)), alpha);
	return d * cos_theta_h / (4 * dot(w_o, w_h));
}

inline vec3 disney_diffuse(DisneyMaterial mat, vec3 n,	vec3 w_o, vec3 w_i) {
	vec3 w_h = normalize(w_i + w_o);
	float n_dot_o = abs(dot(w_o, n));
	float n_dot_i = abs(dot(w_i, n));
	float i_dot_h = dot(w_i, w_h);
	float fd90 = 0.5f + 2 * mat.roughness * i_dot_h * i_dot_h;
	float fi = schlick_weight(n_dot_i);
	float fo = schlick_weight(n_dot_o);
	return mat.base_color * M_1_PIf32 * mix(1.f, fd90, fi) * mix(1.f, fd90, fo);
}

inline vec3 disney_microfacet_isotropic(DisneyMaterial mat, vec3 n, 
	vec3 w_o, vec3 w_i) {

	vec3 w_h = normalize(w_i + w_o);
	float lum = luminance(mat.base_color);
	vec3 tint = lum > 0.f ? mat.base_color / lum : vec3(1, 1, 1);
	vec3 spec = mix(mat.specular * 0.08f * mix(vec3(1, 1, 1), tint, mat.specular_tint), mat.base_color, mat.metallic);

	float alpha = max(0.001f, mat.roughness * mat.roughness);
	float d = gtr_2(dot(n, w_h), alpha);
	vec3 f = mix(spec, vec3(1, 1, 1), schlick_weight(dot(w_i, w_h)));
	float g = smith_shadowing_ggx(dot(n, w_i), alpha) * smith_shadowing_ggx(dot(n, w_o), alpha);
	return d * f * g;
}

inline vec3 disney_microfacet_transmission_isotropic(DisneyMaterial mat, 
	vec3 n, vec3 w_o, vec3 w_i) {

	float o_dot_n = dot(w_o, n);
	float i_dot_n = dot(w_i, n);
	if (o_dot_n == 0 || i_dot_n == 0)
		return vec3(0.f);
	
	bool entering = o_dot_n > 0.f;
	float eta_o = entering ? 1.f : mat.ior;
	float eta_i = entering ? mat.ior : 1.f;
	vec3 w_h = normalize(w_o + w_i * eta_i / eta_o);

	float alpha = max(0.001f, mat.roughness * mat.roughness);
	float d = gtr_2(abs(dot(n, w_h)), alpha);

	float f = fresnel_dielectric(abs(dot(w_i, n)), eta_o, eta_i);
	float g = smith_shadowing_ggx(abs(dot(n, w_i)), alpha) * smith_shadowing_ggx(abs(dot(n, w_o)), alpha);

	float i_dot_h = dot(w_i, w_h);
	float o_dot_h = dot(w_o, w_h);

	float c = abs(o_dot_h) / abs(dot(w_o, n)) * abs(i_dot_h) / abs(dot(w_i, n))
		* pow2(eta_o) / pow2(eta_o * o_dot_h + eta_i * i_dot_h);

	return mat.base_color * c * (1.f - f) * g * d;
}

inline vec3 disney_microfacet_anisotropic(DisneyMaterial mat, vec3 n,
	vec3 w_o, vec3 w_i, vec3 v_x, vec3 v_y) {
	vec3 w_h = normalize(w_i + w_o);
	float lum = luminance(mat.base_color);
	vec3 tint = lum > 0.f ? mat.base_color / lum : vec3(1, 1, 1);
	vec3 spec = mix(mat.specular * 0.08f * mix(vec3(1, 1, 1), tint, mat.specular_tint), mat.base_color, mat.metallic);

	float aspect = sqrt(1.f - mat.anisotropy * 0.9f);
	float a = mat.roughness * mat.roughness;
	vec2 alpha = vec2(max(0.001f, a / aspect), max(0.001f, a * aspect));
	float d = gtr_2_aniso(dot(n, w_h), abs(dot(w_h, v_x)), abs(dot(w_h, v_y)), alpha);
	vec3 f = mix(spec, vec3(1, 1, 1), schlick_weight(dot(w_i, w_h)));
	float g = smith_shadowing_ggx_aniso(dot(n, w_i), abs(dot(w_i, v_x)), abs(dot(w_i, v_y)), alpha)
		* smith_shadowing_ggx_aniso(dot(n, w_o), abs(dot(w_o, v_x)), abs(dot(w_o, v_y)), alpha);
	return d * f * g;
}

inline float disney_clear_coat(DisneyMaterial mat, vec3 n,	vec3 w_o, vec3 w_i) {
	vec3 w_h = normalize(w_i + w_o);
	float alpha = mix(0.1f, 0.001f, mat.clearcoat_gloss);
	float d = gtr_1(dot(n, w_h), alpha);
	float f = mix(0.04f, 1.f, schlick_weight(dot(w_i, n)));
	float g = smith_shadowing_ggx(dot(n, w_i), 0.25f) * smith_shadowing_ggx(dot(n, w_o), 0.25f);
	return 0.25f * mat.clearcoat * d * f * g;
}

inline vec3 disney_sheen(DisneyMaterial mat, vec3 n, vec3 w_o, vec3 w_i) {
	vec3 w_h = normalize(w_i + w_o);
	float lum = luminance(mat.base_color);
	vec3 tint = lum > 0.f ? mat.base_color / lum : vec3(1, 1, 1);
	vec3 sheen_color = mix(vec3(1, 1, 1), tint, mat.sheen_tint);
	float f = schlick_weight(dot(w_i, n));
	return f * mat.sheen * sheen_color;
}

inline vec3 disney_brdf(DisneyMaterial mat, vec3 n, vec3 w_o, vec3 w_i, vec3 v_x, 
	vec3 v_y) {
	if (!same_hemisphere(w_o, w_i, n)) {
		if (mat.specular_transmission > 0) {
			vec3 spec_trans = disney_microfacet_transmission_isotropic(mat, n, w_o, w_i);
			return spec_trans * (1 - mat.metallic) * mat.specular_transmission;
		}
		return vec3(0.f);
	}

	float coat = disney_clear_coat(mat, n, w_o, w_i);
	vec3 sheen = disney_sheen(mat, n, w_o, w_i);
	vec3 diffuse = disney_diffuse(mat, n, w_o, w_i);
	vec3 gloss;
	if (mat.anisotropy == 0.f) {
		gloss = disney_microfacet_isotropic(mat, n, w_o, w_i);
	} else {
		gloss = disney_microfacet_anisotropic(mat, n, w_o, w_i, v_x, v_y);
	}
	return (diffuse + sheen) * (1 - mat.metallic) * (1 - mat.specular_transmission) + gloss + coat;
}

inline float disney_pdf(DisneyMaterial mat, vec3 n,	vec3 w_o, vec3 w_i, 
	vec3 v_x, vec3 v_y) {

	float alpha = max(0.001f, mat.roughness * mat.roughness);
	float aspect = sqrt(1.f - mat.anisotropy * 0.9f);
	vec2 alpha_aniso = vec2(max(0.001f, alpha / aspect), max(0.001f, alpha * aspect));

	float clearcoat_alpha = mix(0.1f, 0.001f, mat.clearcoat_gloss);

	float diffuse = lambertian_pdf(w_i, n);
	float clear_coat = gtr_1_pdf(w_o, w_i, n, clearcoat_alpha);

	float n_comp = 3.f;
	float microfacet;
	float microfacet_transmission = 0.f;
	if (mat.anisotropy == 0.f) {
		microfacet = gtr_2_pdf(w_o, w_i, n, alpha);
	} else {
		microfacet = gtr_2_aniso_pdf(w_o, w_i, n, v_x, v_y, alpha_aniso);
	}
	if (mat.specular_transmission > 0.f) {
		n_comp = 4.f;
		microfacet_transmission = gtr_2_transmission_pdf(w_o, w_i, n, alpha, mat.ior);
	}
	return (diffuse + microfacet + microfacet_transmission + clear_coat) / n_comp;
}

/* Sample a component of the Disney BRDF, returns the sampled BRDF color,
 * ray reflection direction (w_i) and sample PDF.
 */
inline vec3 sample_disney_brdf(DisneyMaterial mat, vec3 n, vec3 w_o, vec3 v_x, 
	vec3 v_y, uint& rng, vec3& w_i, float& pdf) {

	int component = 0;
	if (mat.specular_transmission == 0.f) {
		component = int(lcg_randomf(rng) * 3.f);
		component = clamp(component, 0, 2);
	} else {
		component = int(lcg_randomf(rng) * 4.f);
		component = clamp(component, 0, 3);
	}

	vec2 samples = vec2(lcg_randomf(rng), lcg_randomf(rng));
	if (component == 0) {
		// Sample diffuse component
		w_i = sample_lambertian_dir(n, v_x, v_y, samples);
	} else if (component == 1) {
		vec3 w_h;
		float alpha = max(0.001f, mat.roughness * mat.roughness);
		if (mat.anisotropy == 0.f) {
			w_h = sample_gtr_2_h(n, v_x, v_y, alpha, samples);
		} else {
			float aspect = sqrt(1.f - mat.anisotropy * 0.9f);
			vec2 alpha_aniso = vec2(max(0.001f, alpha / aspect), max(0.001f, alpha * aspect));
			w_h = sample_gtr_2_aniso_h(n, v_x, v_y, alpha_aniso, samples);
		}
		w_i = reflect(-w_o, w_h);

		// Invalid reflection, terminate ray
		if (!same_hemisphere(w_o, w_i, n)) {
			pdf = 0.f;
			w_i = vec3(0.f);
			return vec3(0.f);
		}
	} else if (component == 2) {
		// Sample clear coat component
		float alpha = mix(0.1f, 0.001f, mat.clearcoat_gloss);
		vec3 w_h = sample_gtr_1_h(n, v_x, v_y, alpha, samples);
		w_i = reflect(-w_o, w_h);

		// Invalid reflection, terminate ray
		if (!same_hemisphere(w_o, w_i, n)) {
			pdf = 0.f;
			w_i = vec3(0.f);
			return vec3(0.f);
		}
	} else {
		// Sample microfacet transmission component
		float alpha = max(0.001f, mat.roughness * mat.roughness);
		vec3 w_h = sample_gtr_2_h(n, v_x, v_y, alpha, samples);
		if (dot(w_o, w_h) < 0.f) {
			w_h = -w_h;
		}
		bool entering = dot(w_o, n) > 0.f;
		w_i = refract(-w_o, w_h, entering ? 1.f / mat.ior : mat.ior);

		// Invalid refraction, terminate ray
		if (w_i == vec3(0)) {
			pdf = 0.f;
			return vec3(0.f);
		}
	}
	pdf = disney_pdf(mat, n, w_o, w_i, v_x, v_y);
	return disney_brdf(mat, n, w_o, w_i, v_x, v_y);
}
