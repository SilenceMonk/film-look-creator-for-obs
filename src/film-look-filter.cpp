#include "film-look-filter.h"

#include "plugin-support.h"

#include <graphics/graphics.h>
#include <util/dstr.h>


static const char *film_look_effect_string = R"(
// =========================================================================
//  Cinematic Look Shader for OBS Studio (HLSL - Native Plugin Version)
// =========================================================================

// --- Uniforms ---
uniform float4x4 ViewProj;
uniform texture2d image;

// --- Helper Uniforms ---
uniform float elapsed_time;
uniform float2 uv_size;

// -- Color & Contrast --
uniform float contrast;
uniform float teal_amount;
uniform float orange_amount;

// -- Bloom / White Glow --
uniform float bloom_intensity;
uniform float bloom_threshold;
uniform int   bloom_radius;

// -- Halation / Red-Orange Glow --
uniform float halation_intensity;
uniform float halation_threshold;
uniform int   halation_radius;

// -- Secondary Glow (Cool Tint) --
uniform float secondary_glow_intensity;
uniform float secondary_glow_threshold;
uniform int   secondary_glow_radius;

// -- Texture --
uniform float grain_intensity;

// -- Camera Shake --
uniform float shake_intensity;
uniform float shake_speed;

sampler_state textureSampler {
    Filter = Linear;
    AddressU = Border;
    AddressV = Border;
    BorderColor = 00000000;
};

// --- Helper Functions ---
float random(float2 st) {
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

float3 BlendScreen(float3 base, float3 blend) {
    return 1.0 - ((1.0 - base) * (1.0 - blend));
}

// --- Vertex Shader ---
struct VertData {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertData mainTransform(VertData v_in) {
	VertData vert_out;
	vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv  = v_in.uv;
	return vert_out;
}

// --- Pixel Shader ---
float4 mainImage(VertData v_in) : TARGET {
    // === PART 0: CAMERA SHAKE ===
    float2 shake_offset = float2(0.0, 0.0);
    if (shake_intensity > 0.0) {
        float time = elapsed_time * shake_speed;
        float shake_x = (sin(time * 1.3 + 0.5) + sin(time * 2.7 + 1.2)) * 0.5;
        float shake_y = (cos(time * 1.7 - 0.8) + cos(time * 3.1 - 0.3)) * 0.5;
        shake_offset = float2(shake_x, shake_y) * shake_intensity;
    }
    float2 shaken_uv = v_in.uv + shake_offset;

    // === PART 1: CINEMATIC COLOR GRADING ===
    float4 original_color = image.Sample(textureSampler, shaken_uv);
    float3 graded_color = original_color.rgb;

    graded_color = pow(graded_color, float3(contrast, contrast, contrast));
    float luma = dot(graded_color, float3(0.299, 0.587, 0.114));
    float3 teal_color = float3(0.7, 0.85, 1.0);
    float3 orange_color = float3(1.0, 0.9, 0.7);
    graded_color = lerp(graded_color, teal_color, smoothstep(0.5, 1.0, luma) * teal_amount);
    graded_color = lerp(graded_color, orange_color, smoothstep(0.4, 0.0, luma) * orange_amount);

    // === PART 2: CALCULATE EFFECTS (BLOOM, HALATION, SECONDARY GLOW) ===
    float3 bloom_accum = float3(0,0,0);
    float3 halation_accum = float3(0,0,0);
    float3 secondary_glow_accum = float3(0,0,0);
    float2 pixel_size = 1.0 / uv_size;

    int max_radius_for_loop = max(bloom_radius, halation_radius);
    max_radius_for_loop = max(max_radius_for_loop, secondary_glow_radius);

    float bloom_sample_count = 0.0;
    float halation_sample_count = 0.0;
    float secondary_glow_sample_count = 0.0;

    [loop]
    for (int x = -max_radius_for_loop; x <= max_radius_for_loop; x++) {
        [loop]
        for (int y = -max_radius_for_loop; y <= max_radius_for_loop; y++) {
            float2 offset = float2(x, y);
            float2 sample_uv = shaken_uv + offset * pixel_size;
            float3 sample_color = image.Sample(textureSampler, sample_uv).rgb;
            float sample_luma = dot(sample_color, float3(0.299, 0.587, 0.114));

            if (bloom_intensity > 0.0 && abs(x) <= bloom_radius && abs(y) <= bloom_radius) {
                float bright_factor = smoothstep(bloom_threshold, 1.0, sample_luma);
                bloom_accum += sample_color * bright_factor;
                bloom_sample_count += 1.0;
            }

            if (halation_intensity > 0.0 && abs(x) <= halation_radius && abs(y) <= halation_radius) {
                float halation_bright_factor = smoothstep(halation_threshold, 1.0, sample_luma);
                float3 tinted_color = sample_color * float3(1.0, 0.2, 0.1);
                halation_accum += tinted_color * halation_bright_factor;
                halation_sample_count += 1.0;
            }

            if (secondary_glow_intensity > 0.0 && abs(x) <= secondary_glow_radius && abs(y) <= secondary_glow_radius) {
                float sg_bright_factor = smoothstep(secondary_glow_threshold, 1.0, sample_luma);
                float3 sg_tint = float3(0.6, 0.8, 1.0);
                secondary_glow_accum += (sample_color * sg_tint) * sg_bright_factor;
                secondary_glow_sample_count += 1.0;
            }
        }
    }

    if (bloom_sample_count > 0.0) { bloom_accum /= bloom_sample_count; }
    if (halation_sample_count > 0.0) { halation_accum /= halation_sample_count; }
    if (secondary_glow_sample_count > 0.0) { secondary_glow_accum /= secondary_glow_sample_count; }

    // === PART 3: COMBINE EVERYTHING ===
    float3 final_color = graded_color;

    if (bloom_intensity > 0.0) {
        final_color += bloom_accum * bloom_intensity;
    }

    if (halation_intensity > 0.0) {
        final_color = BlendScreen(final_color, halation_accum * halation_intensity);
    }

    if (secondary_glow_intensity > 0.0) {
        final_color = BlendScreen(final_color, secondary_glow_accum * secondary_glow_intensity);
    }

    float2 grain_seed_uv = shaken_uv + frac(elapsed_time);
    float grain = (random(grain_seed_uv) - 0.5) * 2.0;
    final_color += grain * grain_intensity;

    return float4(clamp(final_color, 0.0, 1.0), original_color.a);
}

technique Draw {
	pass {
		vertex_shader = mainTransform(v_in);
		pixel_shader  = mainImage(v_in);
	}
}
)";

// 保存滤镜实例数据的结构体
struct film_look_data {
	obs_source_t *context;
	gs_effect_t *effect;

	// 用于存储从UI设置中获取的值
	float contrast;
	float teal_amount;
	float orange_amount;
	float bloom_intensity;
	float bloom_threshold;
	int bloom_radius;
	float halation_intensity;
	float halation_threshold;
	int halation_radius;
	float secondary_glow_intensity;
	float secondary_glow_threshold;
	int secondary_glow_radius;
	float grain_intensity;
	float shake_intensity;
	float shake_speed;

	// 新增成员
	float total_elapsed_time;

	// 指向effect文件中uniform变量的指针，用于高效更新
	gs_eparam_t *param_contrast;
	gs_eparam_t *param_teal_amount;
	gs_eparam_t *param_orange_amount;
	gs_eparam_t *param_bloom_intensity;
	gs_eparam_t *param_bloom_threshold;
	gs_eparam_t *param_bloom_radius;
	gs_eparam_t *param_halation_intensity;
	gs_eparam_t *param_halation_threshold;
	gs_eparam_t *param_halation_radius;
	gs_eparam_t *param_secondary_glow_intensity;
	gs_eparam_t *param_secondary_glow_threshold;
	gs_eparam_t *param_secondary_glow_radius;
	gs_eparam_t *param_grain_intensity;
	gs_eparam_t *param_shake_intensity;
	gs_eparam_t *param_shake_speed;
	gs_eparam_t *param_uv_size;
	gs_eparam_t *param_elapsed_time;
};

// 返回滤镜在UI中的显示名称
static const char *film_look_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("FilmLook.Filter");
}

// 加载/重载 .effect 文件
static void update_effect(struct film_look_data *filter)
{
	/// 1. 创建一个 dstr 对象来处理字符串
	dstr effect_text = {nullptr};
	dstr_copy(&effect_text, film_look_effect_string);

	// 2. 根据平台进行字符串替换
	obs_enter_graphics();
	int device_type = gs_get_device_type();
	obs_leave_graphics();

	if (device_type == GS_DEVICE_OPENGL) {
		// 在 OpenGL (macOS, Linux) 上，移除 [loop]
		dstr_replace(&effect_text, "[loop]", "");
	}
	// 对于 D3D11 (Windows)，[loop] 通常是支持的，所以我们不需要移除它。
	// 这段代码现在是跨平台的。

	// 3. 使用处理过的字符串创建 effect
	obs_enter_graphics();

	if (filter->effect) {
		gs_effect_destroy(filter->effect);
	}

	// 使用 effect_text.array 而不是 film_look_effect_string
	filter->effect = gs_effect_create(effect_text.array, nullptr, nullptr);

	obs_leave_graphics();

	// 4. 释放 dstr 对象
	dstr_free(&effect_text);


	if (!filter->effect) {
		blog(LOG_WARNING, "[%s] some ugly shit happening in the shader string", PLUGIN_NAME);
		return;
	}

	// 获取所有uniform参数的指针，以便快速访问
	filter->param_contrast = gs_effect_get_param_by_name(filter->effect, "contrast");
	filter->param_teal_amount = gs_effect_get_param_by_name(filter->effect, "teal_amount");
	filter->param_orange_amount = gs_effect_get_param_by_name(filter->effect, "orange_amount");
	filter->param_bloom_intensity = gs_effect_get_param_by_name(filter->effect, "bloom_intensity");
	filter->param_bloom_threshold = gs_effect_get_param_by_name(filter->effect, "bloom_threshold");
	filter->param_bloom_radius = gs_effect_get_param_by_name(filter->effect, "bloom_radius");
	filter->param_halation_intensity = gs_effect_get_param_by_name(filter->effect, "halation_intensity");
	filter->param_halation_threshold = gs_effect_get_param_by_name(filter->effect, "halation_threshold");
	filter->param_halation_radius = gs_effect_get_param_by_name(filter->effect, "halation_radius");
	filter->param_secondary_glow_intensity =
		gs_effect_get_param_by_name(filter->effect, "secondary_glow_intensity");
	filter->param_secondary_glow_threshold =
		gs_effect_get_param_by_name(filter->effect, "secondary_glow_threshold");
	filter->param_secondary_glow_radius = gs_effect_get_param_by_name(filter->effect, "secondary_glow_radius");
	filter->param_grain_intensity = gs_effect_get_param_by_name(filter->effect, "grain_intensity");
	filter->param_shake_intensity = gs_effect_get_param_by_name(filter->effect, "shake_intensity");
	filter->param_shake_speed = gs_effect_get_param_by_name(filter->effect, "shake_speed");
	filter->param_uv_size = gs_effect_get_param_by_name(filter->effect, "uv_size");
	filter->param_elapsed_time = gs_effect_get_param_by_name(filter->effect, "elapsed_time");
}

// 当滤镜实例被创建时调用
static void *film_look_create(obs_data_t *settings, obs_source_t *source)
{
	struct film_look_data *filter = static_cast<struct film_look_data *>(bzalloc(sizeof(struct film_look_data)));
	filter->context = source;
	filter->total_elapsed_time = 0.0f;

	// 立即加载effect
	update_effect(filter);

	// 从设置加载初始值
	obs_source_update(source, settings);

	return filter;
}

// 当滤镜实例被销毁时调用
static void film_look_destroy(void *data)
{
	auto *filter = static_cast<struct film_look_data *>(data);

	obs_enter_graphics();
	if (filter->effect) {
		gs_effect_destroy(filter->effect);
	}
	obs_leave_graphics();

	bfree(filter);
}

// 当用户在UI中更改设置时调用
static void film_look_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<struct film_look_data *>(data);

	filter->contrast = (float)obs_data_get_double(settings, "contrast");
	filter->teal_amount = (float)obs_data_get_double(settings, "teal_amount");
	filter->orange_amount = (float)obs_data_get_double(settings, "orange_amount");
	filter->bloom_intensity = (float)obs_data_get_double(settings, "bloom_intensity");
	filter->bloom_threshold = (float)obs_data_get_double(settings, "bloom_threshold");
	filter->bloom_radius = (int)obs_data_get_int(settings, "bloom_radius");
	filter->halation_intensity = (float)obs_data_get_double(settings, "halation_intensity");
	filter->halation_threshold = (float)obs_data_get_double(settings, "halation_threshold");
	filter->halation_radius = (int)obs_data_get_int(settings, "halation_radius");
	filter->secondary_glow_intensity = (float)obs_data_get_double(settings, "secondary_glow_intensity");
	filter->secondary_glow_threshold = (float)obs_data_get_double(settings, "secondary_glow_threshold");
	filter->secondary_glow_radius = (int)obs_data_get_int(settings, "secondary_glow_radius");
	filter->grain_intensity = (float)obs_data_get_double(settings, "grain_intensity");
	filter->shake_intensity = (float)obs_data_get_double(settings, "shake_intensity");
	filter->shake_speed = (float)obs_data_get_double(settings, "shake_speed");
}

// 设置默认值
static void film_look_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "contrast", 1.2);
	obs_data_set_default_double(settings, "teal_amount", 0.2);
	obs_data_set_default_double(settings, "orange_amount", 0.15);
	obs_data_set_default_double(settings, "bloom_intensity", 0.5);
	obs_data_set_default_double(settings, "bloom_threshold", 0.8);
	obs_data_set_default_int(settings, "bloom_radius", 2);
	obs_data_set_default_double(settings, "halation_intensity", 0.4);
	obs_data_set_default_double(settings, "halation_threshold", 0.95);
	obs_data_set_default_int(settings, "halation_radius", 4);
	obs_data_set_default_double(settings, "secondary_glow_intensity", 0.3);
	obs_data_set_default_double(settings, "secondary_glow_threshold", 0.75);
	obs_data_set_default_int(settings, "secondary_glow_radius", 3);
	obs_data_set_default_double(settings, "grain_intensity", 0.04);
	obs_data_set_default_double(settings, "shake_intensity", 0.002);
	obs_data_set_default_double(settings, "shake_speed", 5.0);
}

// 定义用户UI
static obs_properties_t *film_look_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_float_slider(props, "contrast", obs_module_text("FilmLook.Contrast"), 0.5, 2.5, 0.05);
	obs_properties_add_float_slider(props, "teal_amount", obs_module_text("FilmLook.TealAmount"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, "orange_amount", obs_module_text("FilmLook.OrangeAmount"), 0.0, 1.0,
					0.01);

	obs_properties_add_float_slider(props, "bloom_intensity", obs_module_text("FilmLook.BloomIntensity"), 0.0, 4.0,
					0.05);
	obs_properties_add_float_slider(props, "bloom_threshold", obs_module_text("FilmLook.BloomThreshold"), 0.3, 1.0,
					0.01);
	obs_properties_add_int_slider(props, "bloom_radius", obs_module_text("FilmLook.BloomRadius"), 1, 5, 1);

	obs_properties_add_float_slider(props, "halation_intensity", obs_module_text("FilmLook.HalationIntensity"), 0.0,
					4.0, 0.05);
	obs_properties_add_float_slider(props, "halation_threshold", obs_module_text("FilmLook.HalationThreshold"), 0.5,
					1.0, 0.01);
	obs_properties_add_int_slider(props, "halation_radius", obs_module_text("FilmLook.HalationRadius"), 2, 8, 1);

	obs_properties_add_float_slider(props, "secondary_glow_intensity",
					obs_module_text("FilmLook.SecondaryGlowIntensity"), 0.0, 3.0, 0.05);
	obs_properties_add_float_slider(props, "secondary_glow_threshold",
					obs_module_text("FilmLook.SecondaryGlowThreshold"), 0.3, 1.0, 0.01);
	obs_properties_add_int_slider(props, "secondary_glow_radius", obs_module_text("FilmLook.SecondaryGlowRadius"),
				      1, 7, 1);

	obs_properties_add_float_slider(props, "grain_intensity", obs_module_text("FilmLook.GrainIntensity"), 0.0, 0.2,
					0.005);

	obs_properties_add_float_slider(props, "shake_intensity", obs_module_text("FilmLook.ShakeIntensity"), 0.0, 0.02,
					0.0005);
	obs_properties_add_float_slider(props, "shake_speed", obs_module_text("FilmLook.ShakeSpeed"), 0.0, 20.0, 0.5);

	UNUSED_PARAMETER(data);
	return props;
}

// 在每一视频帧更新时调用
static void film_look_tick(void *data, float seconds)
{
	auto *filter = static_cast<struct film_look_data *>(data);
	filter->total_elapsed_time += seconds;
}

// 渲染每一帧时调用
static void film_look_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *filter = static_cast<struct film_look_data *>(data);
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width = obs_source_get_width(target);
	uint32_t height = obs_source_get_height(target);
	struct vec2 uv_size = {(float)width, (float)height};

	if (!filter->effect || !target) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {

		gs_effect_set_float(filter->param_contrast, filter->contrast);
		gs_effect_set_float(filter->param_teal_amount, filter->teal_amount);
		gs_effect_set_float(filter->param_orange_amount, filter->orange_amount);
		gs_effect_set_float(filter->param_bloom_intensity, filter->bloom_intensity);
		gs_effect_set_float(filter->param_bloom_threshold, filter->bloom_threshold);
		gs_effect_set_int(filter->param_bloom_radius, filter->bloom_radius);
		gs_effect_set_float(filter->param_halation_intensity, filter->halation_intensity);
		gs_effect_set_float(filter->param_halation_threshold, filter->halation_threshold);
		gs_effect_set_int(filter->param_halation_radius, filter->halation_radius);
		gs_effect_set_float(filter->param_secondary_glow_intensity, filter->secondary_glow_intensity);
		gs_effect_set_float(filter->param_secondary_glow_threshold, filter->secondary_glow_threshold);
		gs_effect_set_int(filter->param_secondary_glow_radius, filter->secondary_glow_radius);
		gs_effect_set_float(filter->param_grain_intensity, filter->grain_intensity);
		gs_effect_set_float(filter->param_shake_intensity, filter->shake_intensity);
		gs_effect_set_float(filter->param_shake_speed, filter->shake_speed);
		gs_effect_set_vec2(filter->param_uv_size, &uv_size);
		gs_effect_set_float(filter->param_elapsed_time, filter->total_elapsed_time);

		obs_source_process_filter_end(filter->context, filter->effect, 0, 0);
	}
}

// 滤镜定义结构体
struct obs_source_info film_look_filter = {
	.id = "film_look_creator",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = film_look_get_name,
	.create = film_look_create,
	.destroy = film_look_destroy,
	.update = film_look_update,
	.get_defaults = film_look_defaults,
	.get_properties = film_look_properties,
	.video_render = film_look_render,
	.video_tick = film_look_tick,
};
