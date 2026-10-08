#pragma once

#include "version.h"
#include <obs-module.h>

enum clone_type {
	CLONE_SOURCE,
	CLONE_PROGRAM_OUTPUT,
};

struct source_clone {
	obs_source_t *source;
	enum clone_type clone_type;
	obs_weak_source_t *clone;
	bool rendering;
	bool no_filter;
};
