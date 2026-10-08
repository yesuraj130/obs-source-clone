#include <obs-module.h>
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
	if (context->canvas) {
		obs_weak_canvas_release(context->canvas);
		context->canvas = NULL;
	}
}

static void *source_clone_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct source_clone *context = bzalloc(sizeof(struct source_clone));
	context->source = source;
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
	if (context->canvas) {
		obs_weak_canvas_release(context->canvas);
		context->canvas = NULL;
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
		} else if (context->clone) {
			source_clone_switch_source(context, NULL);
		}
	} else {
		/* CLONE_PROGRAM_OUTPUT: no target source pointer needed */
		if (context->clone) {
			source_clone_switch_source(context, NULL);
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
	context->no_filter = obs_data_get_bool(settings, "no_filters") && !async && !custom_draw;
}

struct add_source_data {
	obs_property_t *prop;
	obs_source_t *self;
};

static bool source_clone_list_add_source(void *data, obs_source_t *source)
{
	struct add_source_data *d = data;
	if (source == d->self)
		return true;
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(d->prop, name, name);
	return true;
}

static bool source_clone_list_add_canvas(void *data, obs_canvas_t *canvas)
{
	obs_property_t *p = data;
	const char *name = obs_canvas_get_name(canvas);
	obs_property_list_add_string(p, name, name);
	return true;
}

static bool source_clone_list_add_canvas_scene(void *data, obs_canvas_t *canvas)
{
	struct add_source_data *d = data;
	obs_canvas_enum_scenes(canvas, (bool (*)(void *, obs_source_t *))source_clone_list_add_source, d);
	return true;
}

static bool source_clone_source_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(priv);
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
	if (source) {
		uint32_t output_flags = obs_source_get_output_flags(source);
		async = (output_flags & OBS_SOURCE_ASYNC) != 0;
		custom_draw = (output_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		obs_source_release(source);
	}

	obs_property_t *no_filters = obs_properties_get(props, "no_filters");
	obs_property_set_visible(no_filters, !async && !custom_draw);
	return true;
}

static bool source_clone_type_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	const bool clone_source = obs_data_get_int(settings, "clone_type") == CLONE_SOURCE;
	obs_property_set_visible(obs_properties_get(props, "clone"), clone_source);
	obs_property_set_visible(obs_properties_get(props, "active_clone"), clone_source);

	if (clone_source) {
		source_clone_source_changed(priv, props, NULL, settings);
	} else {
		obs_property_set_visible(obs_properties_get(props, "no_filters"), false);
	}
	return true;
}

static bool source_clone_canvas_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	struct source_clone *context = priv;
	obs_property_t *clone = obs_properties_get(props, "clone");
	const char *canvas_name = obs_data_get_string(settings, "canvas");
	obs_canvas_t *canvas = obs_get_canvas_by_name(canvas_name);
	obs_property_list_clear(clone);

	struct add_source_data d = { .prop = clone, .self = context ? context->source : NULL };
	if (canvas) {
		obs_canvas_enum_scenes(canvas, (bool (*)(void *, obs_source_t *))source_clone_list_add_source, &d);
		obs_canvas_release(canvas);
	} else {
		obs_enum_scenes((bool (*)(void *, obs_source_t *))source_clone_list_add_source, &d);
	}
	obs_enum_sources((bool (*)(void *, obs_source_t *))source_clone_list_add_source, &d);
	obs_property_list_insert_string(clone, 0, "", "");
	return true;
}

obs_properties_t *source_clone_properties(void *data)
{
	struct source_clone *context = data;
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(props, "clone_type", obs_module_text("CloneType"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Source"), CLONE_SOURCE);
	obs_property_list_add_int(p, obs_module_text("ProgramOutput"), CLONE_PROGRAM_OUTPUT);
	obs_property_set_modified_callback2(p, source_clone_type_changed, data);

	p = obs_properties_add_list(props, "canvas", obs_module_text("Canvas"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_enum_canvases(source_clone_list_add_canvas, p);
	obs_property_list_insert_string(p, 0, "", "");
	obs_property_set_modified_callback2(p, source_clone_canvas_changed, data);

	p = obs_properties_add_list(props, "clone", obs_module_text("Clone"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	struct add_source_data d = { .prop = p, .self = context ? context->source : NULL };
	obs_enum_sources((bool (*)(void *, obs_source_t *))source_clone_list_add_source, &d);
	obs_enum_canvases((bool (*)(void *, obs_canvas_t *))source_clone_list_add_canvas_scene, &d);
	obs_property_list_insert_string(p, 0, "", "");
	obs_property_set_modified_callback2(p, source_clone_source_changed, data);

	obs_properties_add_bool(props, "active_clone", obs_module_text("ActiveClone"));
	obs_properties_add_bool(props, "no_filters", obs_module_text("NoFilters"));

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

void source_clone_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct source_clone *context = data;

	if (context->clone_type == CLONE_PROGRAM_OUTPUT) {
		gs_texture_t *main_tex = obs_get_main_texture();
		if (!main_tex)
			return;

		uint32_t cx = gs_texture_get_width(main_tex);
		uint32_t cy = gs_texture_get_height(main_tex);
		if (!cx || !cy)
			return;

		const enum gs_color_space current_space = gs_get_color_space();
		const enum gs_color_space tex_space = gs_texture_get_color_space(main_tex);
		float multiplier;
		const char *technique = get_tech_name_and_multiplier(current_space, tex_space, &multiplier);

		gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);

		gs_effect_set_texture_srgb(gs_effect_get_param_by_name(eff, "image"), main_tex);
		gs_effect_set_float(gs_effect_get_param_by_name(eff, "multiplier"), multiplier);

		while (gs_effect_loop(eff, technique)) {
			gs_draw_sprite(main_tex, 0, cx, cy);
		}

		gs_enable_framebuffer_srgb(previous);
		return;
	}

	if (!context->clone)
		return;

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

	if (context->no_filter) {
		obs_source_default_render(source);
	} else {
		obs_source_video_render(source);
	}
	obs_source_release(source);
	context->rendering = false;
}

uint32_t source_clone_get_width(void *data)
{
	struct source_clone *context = data;
	if (context->clone_type == CLONE_PROGRAM_OUTPUT) {
		gs_texture_t *main_tex = obs_get_main_texture();
		if (main_tex) {
			uint32_t width = gs_texture_get_width(main_tex);
			if (width)
				return width;
		}
		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi))
			return ovi.base_width ? ovi.base_width : 1;
		return 1;
	}

	if (!context->clone)
		return 1;
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
	if (context->clone_type == CLONE_PROGRAM_OUTPUT) {
		gs_texture_t *main_tex = obs_get_main_texture();
		if (main_tex) {
			uint32_t height = gs_texture_get_height(main_tex);
			if (height)
				return height;
		}
		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi))
			return ovi.base_height ? ovi.base_height : 1;
		return 1;
	}

	if (!context->clone)
		return 1;
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
	.show = source_clone_show,
	.hide = source_clone_hide,
	.activate = source_clone_activate,
	.deactivate = source_clone_deactivate,
	.get_properties = source_clone_properties,
};

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Source Clone] loaded version %s", PROJECT_VERSION);
	obs_register_source(&source_clone_info);
	return true;
}

void obs_module_unload(void)
{
}
