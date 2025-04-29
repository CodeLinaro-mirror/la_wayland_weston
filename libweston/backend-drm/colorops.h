/*
 * Copyright © 2025-2026 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "drm-internal.h"
#include "drm-kms-enums.h"

struct drm_colorop {
	struct drm_color_pipeline *pipeline;
	struct wl_list link; /* drm_pipeline::colorop_list */

	enum wdrm_colorop_type type;

	uint32_t id;

	/* Some colorop's can be bypassed. */
	bool can_bypass;

	/* Only useful for 1D and 3D LUT colorop's. */
	uint32_t size;

	/* Holds the properties for the colorop. */
	struct drm_property_info props[WDRM_COLOROP__COUNT];
};

struct drm_color_pipeline {
	struct drm_plane *plane;
	struct wl_list colorop_list; /* drm_colorop::link */
	uint32_t id;
};

#if CAN_OFFLOAD_COLOR_PIPELINE

void
drm_plane_populate_color_pipelines(struct drm_plane *plane,
				   drmModeObjectPropertiesPtr plane_props);

void
drm_plane_release_color_pipelines(struct drm_plane *plane);

#else /* CAN_OFFLOAD_COLOR_PIPELINE */

static inline void
drm_plane_populate_color_pipelines(struct drm_plane *plane,
				   drmModeObjectPropertiesPtr plane_props)
{
}

static inline void
drm_plane_release_color_pipelines(struct drm_plane *plane)
{
}

#endif /* CAN_OFFLOAD_COLOR_PIPELINE */
