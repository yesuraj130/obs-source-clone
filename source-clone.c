#include <obs-module.h>
#include <obs-frontend-api.h>
#include "util/dstr.h"
#include "source-clone.h"

const char *source_clone_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("SourceClone");
}

static void source_clone_remove(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(cd);
	struct source_clone *context = data;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (source) {
		if (obs_source_showing(context->source))
			obs_source_dec_showing(source);
		if (context->active_clone && obs_source_active(context->source))
			obs_source_dec_active(source);
		obs_source_release(source);
	}
	obs_weak_source_release(context->clone);
	context->clone = NULL;
	obs_weak_source_release(context->current_scene);
	context->current_scene = NULL;
	obs_weak_source_release(context->previous_scene);
	context->previous_scene = NULL;
}

static void *source_clone_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct source_clone *context = bzalloc(sizeof(struct source_clone));
	context->source = source;
	context->cx = 1;
	context->cy = 1;
	obs_source_update(source, NULL);
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "remove", source_clone_remove, context);
	return context;
}

static void source_clone_destroy(void *data)
{
	struct source_clone *context = data;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (source) {
		if (obs_source_showing(context->source))
			obs_source_dec_showing(source);
		if (context->active_clone && obs_source_active(context->source))
			obs_source_dec_active(source);
		obs_source_release(source);
	}
	obs_weak_source_release(context->clone);
	obs_weak_source_release(context->current_scene);
	obs_weak_source_release(context->previous_scene);
	if (context->render) {
		obs_enter_graphics();
		gs_texrender_destroy(context->render);
		obs_leave_graphics();
	}
	bfree(context);
}

void source_clone_switch_source(struct source_clone *context, obs_source_t *source)
{
	obs_source_t *prev_source = obs_weak_source_get_source(context->clone);
	if (prev_source) {
		if (obs_source_showing(context->source))
			obs_source_dec_showing(prev_source);
		if (context->active_clone && obs_source_active(context->source))
			obs_source_dec_active(prev_source);
		obs_source_release(prev_source);
	}
	obs_weak_source_release(context->clone);
	context->clone = source ? obs_source_get_weak_source(source) : NULL;
	if (source && obs_source_showing(context->source))
		obs_source_inc_showing(source);
	if (source && context->active_clone && obs_source_active(context->source))
		obs_source_inc_active(source);
}

void source_clone_load(void *data, obs_data_t *settings)
{
	struct source_clone *context = data;
	obs_source_update(context->source, settings);
}

void source_clone_update(void *data, obs_data_t *settings)
{
	struct source_clone *context = data;
	bool active_clone = obs_data_get_bool(settings, "active_clone");
	context->clone_type = (enum clone_type)obs_data_get_int(settings, "clone_type");
	bool async = true;
	bool custom_draw = true;
	const char *canvas_name = obs_data_get_string(settings, "canvas");
	if (canvas_name && strlen(canvas_name)) {
		obs_canvas_t *canvas = NULL;
		if (context->canvas) {
			canvas = obs_weak_canvas_get_canvas(context->canvas);
			if (canvas && strcmp(obs_canvas_get_name(canvas), canvas_name) != 0) {
				obs_canvas_release(canvas);
				canvas = NULL;
			}
		}
		if (!canvas) {
			canvas = obs_get_canvas_by_name(canvas_name);
			obs_weak_canvas_release(context->canvas);
			context->canvas = canvas ? obs_canvas_get_weak_canvas(canvas) : NULL;
		}
		obs_canvas_release(canvas);
	} else if (context->canvas) {
		obs_weak_canvas_release(context->canvas);
		context->canvas = NULL;
	}

	if (context->clone_type == CLONE_SOURCE) {
		const char *source_name = obs_data_get_string(settings, "clone");
		obs_source_t *source = NULL;
		if (context->canvas) {
			obs_canvas_t *canvas = obs_weak_canvas_get_canvas(context->canvas);
			if (canvas) {
				source = obs_canvas_get_source_by_name(canvas, source_name);
				obs_canvas_release(canvas);
			}
		}
		if (!source)
			source = obs_get_source_by_name(source_name);
		if (source == context->source) {
			obs_source_release(source);
			source = NULL;
		}
		if (source) {
			uint32_t output_flags = obs_source_get_output_flags(source);
			async = (output_flags & OBS_SOURCE_ASYNC) != 0;
			custom_draw = (output_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
			if (!obs_weak_source_references_source(context->clone, source)) {
				source_clone_switch_source(context, source);
			}
			obs_source_release(source);
		}
	} else if (context->clone_type == CLONE_PROGRAM_OUTPUT) {
		obs_source_t *source = NULL;
		if (context->canvas) {
			obs_canvas_t *canvas = obs_weak_canvas_get_canvas(context->canvas);
			if (canvas) {
				source = obs_canvas_get_channel(canvas, 0);
				obs_canvas_release(canvas);
			}
		} else {
			source = obs_get_output_source(0);
		}
		if (source == context->source) {
			obs_source_release(source);
			source = NULL;
		}
		if (source) {
			uint32_t output_flags = obs_source_get_output_flags(source);
			async = (output_flags & OBS_SOURCE_ASYNC) != 0;
			custom_draw = (output_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
			if (!obs_weak_source_references_source(context->clone, source)) {
				source_clone_switch_source(context, source);
			}
			obs_source_release(source);
		}
	}
	if (active_clone != context->active_clone) {
		if (obs_source_active(context->source)) {
			obs_source_t *clone = obs_weak_source_get_source(context->clone);
			if (clone) {
				if (active_clone)
					obs_source_inc_active(clone);
				else
					obs_source_dec_active(clone);
				obs_source_release(clone);
			}
		}
		context->active_clone = active_clone;
	}
	context->buffer_frame = (uint8_t)obs_data_get_int(settings, "buffer_frame");
	context->no_filter = obs_data_get_bool(settings, "no_filters") && !async && !custom_draw;
}

void source_clone_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

bool source_clone_list_add_source(void *data, obs_source_t *source)
{
	obs_property_t *prop = data;

	const char *name = obs_source_get_name(source);
	size_t count = obs_property_list_item_count(prop);
	size_t idx = 0;
	while (idx < count && strcmp(name, obs_property_list_item_string(prop, idx)) > 0)
		idx++;
	obs_property_list_insert_string(prop, idx, name, name);
	return true;
}

bool source_clone_list_add_canvas_scene(void *data, obs_canvas_t *canvas)
{
	obs_canvas_enum_scenes(canvas, source_clone_list_add_source, data);
	return true;
}

bool source_clone_list_add_canvas(void *data, obs_canvas_t *canvas)
{
	obs_property_t *prop = data;

	const char *name = obs_canvas_get_name(canvas);
	size_t count = obs_property_list_item_count(prop);
	size_t idx = 0;
	while (idx < count && strcmp(name, obs_property_list_item_string(prop, idx)) > 0)
		idx++;
	obs_property_list_insert_string(prop, idx, name, name);
	return true;
}

struct same_clones {
	obs_data_t *settings;
	DARRAY(const char *) clones;
};

bool find_clones(void *data, obs_source_t *source)
{
	if (strcmp(obs_source_get_unversioned_id(source), "source-clone") != 0) {
		return true;
	}
	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings)
		return true;
	struct same_clones *sc = data;
	if (settings == sc->settings) {
		obs_data_release(settings);
		return true;
	}
	if (obs_data_get_int(sc->settings, "clone_type") == CLONE_SOURCE) {
		if (obs_data_get_int(settings, "clone_type") == CLONE_SOURCE &&
		    strcmp(obs_data_get_string(sc->settings, "clone"), obs_data_get_string(settings, "clone")) == 0) {
			const char *name = obs_source_get_name(source);
			da_push_back(sc->clones, &name);
		}
	} else if (obs_data_get_int(sc->settings, "clone_type") == obs_data_get_int(settings, "clone_type")) {
		const char *name = obs_source_get_name(source);
		da_push_back(sc->clones, &name);
	}
	obs_data_release(settings);
	return true;
}

void find_same_clones(obs_properties_t *props, obs_data_t *settings)
{
	struct same_clones sc;
	sc.settings = settings;
	da_init(sc.clones);
	obs_enum_sources(find_clones, &sc);
	obs_property_t *prop = obs_properties_get(props, "same_clones");
	if (sc.clones.num) {
		struct dstr names;
		dstr_init_copy(&names, sc.clones.array[0]);
		for (size_t i = 1; i < sc.clones.num; i++) {
			dstr_cat(&names, "\n");
			dstr_cat(&names, sc.clones.array[i]);
		}
		obs_data_set_string(settings, "same_clones", names.array);
		dstr_free(&names);
		obs_property_set_visible(prop, true);
	} else {
		obs_data_unset_user_value(settings, "same_clones");
		obs_property_set_visible(prop, false);
	}
	da_free(sc.clones);
}

bool source_clone_source_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	struct source_clone *context = priv;
	const char *canvas_name = obs_data_get_string(settings, "canvas");
	const char *source_name = obs_data_get_string(settings, "clone");
	bool async = true;
	bool custom_draw = true;
	obs_source_t *source = NULL;
	if (canvas_name && strlen(canvas_name)) {
		obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
		if (canvas) {
			source = obs_canvas_get_source_by_name(canvas, source_name);
			obs_canvas_release(canvas);
		}
	}
	if (!source)
		source = obs_get_source_by_name(source_name);
	if (source == context->source) {
		obs_source_release(source);
		source = NULL;
	}
	if (source) {
		uint32_t output_flags = obs_source_get_output_flags(source);
		async = (output_flags & OBS_SOURCE_ASYNC) != 0;
		custom_draw = (output_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		obs_source_release(source);
	}

	obs_property_t *no_filters = obs_properties_get(props, "no_filters");
	obs_property_set_visible(no_filters, !async && !custom_draw);

	find_same_clones(props, settings);
	return true;
}

bool source_clone_type_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	obs_property_t *clone = obs_properties_get(props, "clone");
	const bool clone_source = obs_data_get_int(settings, "clone_type") == CLONE_SOURCE;
	obs_property_set_visible(clone, clone_source);
	if (clone_source) {
		source_clone_source_changed(priv, props, NULL, settings);
	} else {
		obs_property_t *no_filters = obs_properties_get(props, "no_filters");
		obs_property_set_visible(no_filters, false);
		find_same_clones(props, settings);
	}
	return true;
}

bool source_clone_canvas_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	obs_property_t *clone = obs_properties_get(props, "clone");
	const char *canvas_name = obs_data_get_string(settings, "canvas");
	obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
	obs_property_list_clear(clone);
	if (canvas) {
		obs_canvas_enum_scenes(canvas, source_clone_list_add_source, clone);
		obs_canvas_release(canvas);
	} else {
		obs_enum_scenes(source_clone_list_add_source, clone);
	}
	obs_enum_sources(source_clone_list_add_source, clone);
	obs_property_list_insert_string(0, 0, "", "");
	return true;
}

obs_properties_t *source_clone_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(props, "clone_type", obs_module_text("CloneType"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Source"), CLONE_SOURCE);
	obs_property_list_add_int(p, obs_module_text("CurrentScene"), CLONE_CURRENT_SCENE);
	obs_property_list_add_int(p, obs_module_text("PreviousScene"), CLONE_PREVIOUS_SCENE);
	obs_property_list_add_int(p, obs_module_text("ProgramOutput"), CLONE_PROGRAM_OUTPUT);

	obs_property_set_modified_callback2(p, source_clone_type_changed, data);

	p = obs_properties_add_list(props, "canvas", obs_module_text("Canvas"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_enum_canvases(source_clone_list_add_canvas, p);
	obs_property_list_insert_string(p, 0, "", "");

	obs_property_set_modified_callback2(p, source_clone_canvas_changed, data);

	p = obs_properties_add_list(props, "clone", obs_module_text("Clone"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(source_clone_list_add_source, p);
	obs_enum_canvases(source_clone_list_add_canvas_scene, p);
	obs_property_list_insert_string(p, 0, "", "");
	obs_property_set_modified_callback2(p, source_clone_source_changed, data);

	p = obs_properties_add_list(props, "buffer_frame", obs_module_text("VideoBuffer"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("None"), 0);
	obs_property_list_add_int(p, obs_module_text("Full"), 1);
	obs_property_list_add_int(p, obs_module_text("Half"), 2);
	obs_property_list_add_int(p, obs_module_text("Third"), 3);
	obs_property_list_add_int(p, obs_module_text("Quarter"), 4);

	obs_properties_add_bool(props, "active_clone", obs_module_text("ActiveClone"));

	obs_properties_add_bool(props, "no_filters", obs_module_text("NoFilters"));

	p = obs_properties_add_text(props, "same_clones", obs_module_text("SameClones"), OBS_TEXT_INFO);
	obs_property_set_visible(p, false);

	obs_properties_add_text(
		props, "plugin_info",
		"<a href=\"https://obsproject.com/forum/resources/source-clone.1632/\">Source Clone</a> (" PROJECT_VERSION
		") by <a href=\"https://www.exeldro.com\">Exeldro</a>",
		OBS_TEXT_INFO);
	return props;
}

static const char *get_tech_name_and_multiplier(enum gs_color_space current_space, enum gs_color_space source_space,
						float *multiplier)
{
	const char *tech_name = "Draw";
	*multiplier = 1.f;

	switch (source_space) {
	case GS_CS_SRGB:
	case GS_CS_SRGB_16F:
		switch (current_space) {
		case GS_CS_709_SCRGB:
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		default:;
		}
		break;
	case GS_CS_709_EXTENDED:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawTonemap";
			break;
		case GS_CS_709_SCRGB:
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		default:;
		}
		break;
	case GS_CS_709_SCRGB:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawMultiplyTonemap";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		case GS_CS_709_EXTENDED:
			tech_name = "DrawMultiply";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
		default:;
		}
	}

	return tech_name;
}

static void source_clone_draw_frame(struct source_clone *context)
{
	const enum gs_color_space current_space = gs_get_color_space();
	float multiplier;
	const char *technique = get_tech_name_and_multiplier(current_space, context->space, &multiplier);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(context->render);
	if (!tex)
		return;
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), tex);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "multiplier"), multiplier);

	while (gs_effect_loop(effect, technique))
		gs_draw_sprite(tex, 0, context->cx, context->cy);

	gs_enable_framebuffer_srgb(previous);
}

void source_clone_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct source_clone *context = data;
	if (context->clone_type == CLONE_SOURCE && !context->clone)
		return;

	if (context->buffer_frame > 0 && context->processed_frame) {
		source_clone_draw_frame(context);
		return;
	}
	if (context->rendering)
		return;
	context->rendering = true;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source || source == context->source) {
		if (source)
			obs_source_release(source);
		context->rendering = false;
		return;
	}
	if (context->buffer_frame == 0) {
		if (context->no_filter) {
			obs_source_default_render(source);
		} else {
			obs_source_video_render(source);
		}
		obs_source_release(source);
		context->rendering = false;
		return;
	}

	if (!context->source_cx || !context->source_cy) {
		obs_source_release(source);
		context->rendering = false;
		return;
	}

	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};
	const enum gs_color_space space =
		obs_source_get_color_space(source, OBS_COUNTOF(preferred_spaces), preferred_spaces);
	const enum gs_color_format format = gs_get_format_from_space(space);
	if (!context->render || gs_texrender_get_format(context->render) != format) {
		gs_texrender_destroy(context->render);
		context->render = gs_texrender_create(format, GS_ZS_NONE);
	} else {
		gs_texrender_reset(context->render);
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	if (gs_texrender_begin_with_color_space(context->render, context->cx, context->cy, space)) {

		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		if (context->source_cx && context->source_cy) {
			gs_ortho(0.0f, (float)context->source_cx, 0.0f, (float)context->source_cy, -100.0f, 100.0f);
			if (context->no_filter) {
				obs_source_default_render(source);
			} else {
				obs_source_video_render(source);
			}
		}
		gs_texrender_end(context->render);

		context->space = space;
	}

	gs_blend_state_pop();

	context->processed_frame = true;
	obs_source_release(source);
	context->rendering = false;
	source_clone_draw_frame(context);
}

uint32_t source_clone_get_width(void *data)
{
	struct source_clone *context = data;
	if (!context->clone)
		return 1;
	if (context->buffer_frame > 0)
		return context->cx;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return 1;
	uint32_t width = context->no_filter ? obs_source_get_base_width(source) : obs_source_get_width(source);
	obs_source_release(source);
	return width ? width : 1;
}

uint32_t source_clone_get_height(void *data)
{
	struct source_clone *context = data;
	if (!context->clone)
		return 1;
	if (context->buffer_frame > 0)
		return context->cy;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return 1;
	uint32_t height = context->no_filter ? obs_source_get_base_height(source) : obs_source_get_height(source);
	obs_source_release(source);
	return height ? height : 1;
}

void source_clone_show(void *data)
{
	struct source_clone *context = data;
	if (!context->clone)
		return;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return;
	obs_source_inc_showing(source);
	obs_source_release(source);
}

void source_clone_hide(void *data)
{
	struct source_clone *context = data;
	if (!context->clone)
		return;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return;
	obs_source_dec_showing(source);
	obs_source_release(source);
}

void source_clone_activate(void *data)
{
	struct source_clone *context = data;
	if (!context->clone || !context->active_clone)
		return;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return;
	obs_source_inc_active(source);
	obs_source_release(source);
}

void source_clone_deactivate(void *data)
{
	struct source_clone *context = data;
	if (!context->clone || !context->active_clone)
		return;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return;
	obs_source_dec_active(source);
	obs_source_release(source);
}

void source_clone_save(void *data, obs_data_t *settings)
{
	struct source_clone *context = data;
	if (context->clone_type != CLONE_SOURCE) {
		obs_data_set_string(settings, "clone", "");
		return;
	}
	if (!context->clone)
		return;
	obs_source_t *source = obs_weak_source_get_source(context->clone);
	if (!source)
		return;
	obs_data_set_string(settings, "clone", obs_source_get_name(source));
	obs_source_release(source);
}

static obs_source_t *get_active_scene_from_channel0(obs_canvas_t *canvas)
{
	obs_source_t *source = NULL;
	if (canvas) {
		source = obs_canvas_get_channel(canvas, 0);
	} else {
		source = obs_get_output_source(0);
	}
	while (source && obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_t *ts = obs_transition_get_active_source(source);
		if (ts) {
			obs_source_release(source);
			source = ts;
		} else {
			break;
		}
	}
	return source;
}

static bool scene_contains_clone(obs_source_t *scene_source, obs_source_t *clone_source)
{
	if (!scene_source || !clone_source)
		return false;
	if (scene_source == clone_source)
		return true;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return false;
	const char *clone_name = obs_source_get_name(clone_source);
	if (!clone_name)
		return false;
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, clone_name);
	return item != NULL;
}

void source_clone_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct source_clone *context = data;
	context->processed_frame = false;

	obs_canvas_t *canvas = context->canvas ? obs_weak_canvas_get_canvas(context->canvas) : NULL;
	obs_source_t *current_prog_scene = get_active_scene_from_channel0(canvas);

	/* Maintain current and previous scene history for transition tracking */
	if (current_prog_scene) {
		if (!obs_weak_source_references_source(context->current_scene, current_prog_scene)) {
			obs_source_t *old_current = obs_weak_source_get_source(context->current_scene);
			if (old_current) {
				obs_weak_source_release(context->previous_scene);
				context->previous_scene = obs_source_get_weak_source(old_current);
				obs_source_release(old_current);
			}
			obs_weak_source_release(context->current_scene);
			context->current_scene = obs_source_get_weak_source(current_prog_scene);
		}
	}

	if (context->clone_type == CLONE_CURRENT_SCENE) {
		if (current_prog_scene) {
			if (!obs_weak_source_references_source(context->clone, current_prog_scene)) {
				source_clone_switch_source(context, current_prog_scene);
			}
		}
	} else if (context->clone_type == CLONE_PREVIOUS_SCENE) {
		obs_source_t *prev = obs_weak_source_get_source(context->previous_scene);
		if (prev) {
			if (!obs_weak_source_references_source(context->clone, prev)) {
				source_clone_switch_source(context, prev);
			}
			obs_source_release(prev);
		} else if (context->clone) {
			source_clone_switch_source(context, NULL);
		}
	} else if (context->clone_type == CLONE_PROGRAM_OUTPUT) {
		/* Recursion detection: is this clone inside the active program scene? */
		bool recursive = scene_contains_clone(current_prog_scene, context->source);
		context->is_recursive = recursive;

		if (recursive) {
			/* When recursion is detected, seamlessly fall back to previous scene */
			obs_source_t *prev = obs_weak_source_get_source(context->previous_scene);
			if (prev) {
				if (!obs_weak_source_references_source(context->clone, prev)) {
					source_clone_switch_source(context, prev);
				}
				obs_source_release(prev);
			} else {
				/* Initial launch before first scene transition */
				if (context->clone) {
					source_clone_switch_source(context, NULL);
				}
			}
		} else {
			/* Not recursive: clone Channel 0 (Program Output) directly */
			obs_source_t *chan0 = canvas ? obs_canvas_get_channel(canvas, 0) : obs_get_output_source(0);
			if (chan0 == context->source) {
				obs_source_release(chan0);
				chan0 = NULL;
			}
			if (chan0) {
				if (!obs_weak_source_references_source(context->clone, chan0)) {
					source_clone_switch_source(context, chan0);
				}
				obs_source_release(chan0);
			}
		}
	}

	if (current_prog_scene)
		obs_source_release(current_prog_scene);
	if (canvas)
		obs_canvas_release(canvas);

	if (context->buffer_frame > 0) {
		uint32_t cx = context->buffer_frame;
		uint32_t cy = context->buffer_frame;
		if (context->clone) {
			obs_source_t *s = obs_weak_source_get_source(context->clone);
			if (s) {
				context->source_cx = context->no_filter ? obs_source_get_base_width(s)
									: obs_source_get_width(s);
				context->source_cy = context->no_filter ? obs_source_get_base_height(s)
									: obs_source_get_height(s);

				cx = context->source_cx;
				cy = context->source_cy;
				obs_source_release(s);
			}
		}
		if (context->buffer_frame > 1) {
			cx /= context->buffer_frame;
			cy /= context->buffer_frame;
		}
		if (cx != context->cx || cy != context->cy) {
			context->cx = cx;
			context->cy = cy;
			obs_enter_graphics();
			gs_texrender_destroy(context->render);
			context->render = NULL;
			obs_leave_graphics();
		}
	}
}

struct obs_source_info source_clone_info = {
	.id = "source-clone",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = source_clone_get_name,
	.create = source_clone_create,
	.destroy = source_clone_destroy,
	.update = source_clone_update,
	.load = source_clone_load,
	.save = source_clone_save,
	.video_render = source_clone_video_render,
	.get_width = source_clone_get_width,
	.get_height = source_clone_get_height,
	.video_tick = source_clone_video_tick,
	.show = source_clone_show,
	.hide = source_clone_hide,
	.activate = source_clone_activate,
	.deactivate = source_clone_deactivate,
	.get_defaults = source_clone_defaults,
	.get_properties = source_clone_properties,
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("source-clone", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("SourceClone");
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Source Clone] loaded version %s", PROJECT_VERSION);
	obs_register_source(&source_clone_info);
	return true;
}

void obs_module_unload(void)
{
}
