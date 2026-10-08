#pragma once

#include "version.h"
#include <obs-module.h>

enum clone_type {
	CLONE_SOURCE,
	CLONE_CURRENT_SCENE,
	CLONE_PREVIOUS_SCENE,
	CLONE_PROGRAM_OUTPUT,
};

struct source_clone {
	obs_source_t *source;
	enum clone_type clone_type;
	obs_weak_canvas_t *canvas;
	obs_weak_source_t *clone;
	obs_weak_source_t *current_scene;
	obs_weak_source_t *previous_scene;
	gs_texrender_t *render;
	bool processed_frame;
	uint8_t buffer_frame;
	uint32_t cx;
	uint32_t cy;
	uint32_t source_cx;
	uint32_t source_cy;
	enum gs_color_space space;
	bool rendering;
	bool active_clone;
	bool no_filter;
	bool is_recursive;
};
