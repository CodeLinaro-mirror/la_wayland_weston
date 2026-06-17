/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 * Copyright © 2017, 2018 Collabora, Ltd.
 * Copyright © 2017, 2018 General Electric Company
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "config.h"

#include "sdm-internal.h"
#include "sdm-service/sdm_display_connect.h"

static const char *const aspect_ratio_as_string[] = {
	[WESTON_MODE_PIC_AR_NONE] = "",
	[WESTON_MODE_PIC_AR_4_3] = " 4:3",
	[WESTON_MODE_PIC_AR_16_9] = " 16:9",
	[WESTON_MODE_PIC_AR_64_27] = " 64:27",
	[WESTON_MODE_PIC_AR_256_135] = " 256:135",
};

static const char *
aspect_ratio_to_string(enum weston_mode_aspect_ratio ratio)
{
	if (ratio < 0 || ratio >= ARRAY_LENGTH(aspect_ratio_as_string) ||
	    !aspect_ratio_as_string[ratio])
		return " (unknown aspect ratio)";

	return aspect_ratio_as_string[ratio];
}

/**
 * Destroys a mode, and removes it from the list.
 */
static void
drm_output_destroy_mode(struct drm_device *device, struct drm_mode *mode)
{
	if (mode->blob_id)
		drmModeDestroyPropertyBlob(device->drm.fd, mode->blob_id);
	wl_list_remove(&mode->base.link);
	free(mode);
}

/** Destroy a list of drm_modes
 *
 * @param device The device for releasing mode property blobs.
 * @param mode_list The list linked by drm_mode::base.link.
 */
void
drm_mode_list_destroy(struct drm_device *device, struct wl_list *mode_list)
{
	struct drm_mode *mode, *next;

	wl_list_for_each_safe(mode, next, mode_list, base.link)
		drm_output_destroy_mode(device, mode);
}

void
drm_output_print_modes(struct drm_output *output)
{
	struct weston_mode *m;
	struct drm_mode *dm;
	const char *aspect_ratio;

	wl_list_for_each(m, &output->base.mode_list, link) {
		dm = to_drm_mode(m);

		aspect_ratio = aspect_ratio_to_string(m->aspect_ratio);
		weston_log_continue(STAMP_SPACE "%dx%d@%.1f%s%s%s, %.1f MHz\n",
				    m->width, m->height, m->refresh / 1000.0,
				    aspect_ratio,
				    m->flags & WL_OUTPUT_MODE_PREFERRED ?
				    ", preferred" : "",
				    m->flags & WL_OUTPUT_MODE_CURRENT ?
				    ", current" : "",
				    dm->mode_info.clock / 1000.0);
	}
}

/**
 * Find the closest-matching mode for a given target
 *
 * Given a target mode, find the most suitable mode amongst the output's
 * current mode list to use, preferring the current mode if possible, to
 * avoid an expensive mode switch.
 *
 * @param output DRM output
 * @param target_mode Mode to attempt to match
 * @returns Pointer to a mode from the output's mode list
 */
struct drm_mode *
drm_output_choose_mode(struct drm_output *output,
		       struct weston_mode *target_mode)
{
	struct drm_mode *mode = NULL;
	int ret = 0;

	wl_list_for_each(mode, &output->base.mode_list, base.link) {
		if (mode->base.width == target_mode->width &&
		    mode->base.height == target_mode->height &&
		    mode->base.refresh == target_mode->refresh) {
			/* Check if this is already the current mode */
			if (output->base.current_mode->width == mode->base.width &&
			    output->base.current_mode->height == mode->base.height &&
			    output->base.current_mode->refresh == mode->base.refresh) {
				return mode;
			}

			ret = SetDisplayConfigurationByIndex(output->display_id, mode->index);
			if (ret == kErrorNone) {
				return mode;
			} else {
				weston_log("Failed to set display configuration\n");
				return NULL;
			}
		}
	}

	return NULL;
}

static struct drm_mode *
drm_output_add_mode(struct drm_output *output,
		    struct DisplayConfigInfo *display_config,
		    uint32_t index)
{
	struct drm_mode *mode = NULL;

	mode = malloc(sizeof *mode);
	if (mode == NULL)
		return NULL;

	mode->base.flags = 0;
	mode->base.width = display_config->x_pixels;
	mode->base.height = display_config->y_pixels;

	mode->base.refresh = display_config->fps*1000;
	mode->display_config = *display_config;
	mode->index = index;
	mode->blob_id = 0;

	wl_list_insert(output->base.mode_list.prev, &mode->base.link);

	return mode;
}

int
drm_output_set_mode(struct weston_output *base,
		    enum weston_drm_backend_output_mode mode,
		    const char *modeline)
{
	struct drm_output *output = to_drm_output(base);
	struct drm_mode *current_mode = NULL;
	struct drm_head *head = to_drm_head(weston_output_get_first_head(base));
	struct DisplayConfigInfo current_config = {0};
	struct DisplayConfigInfo display_config = {0};
	uint32_t count = 0, index = 0;
	int rc = 0;

	output->display_id = head->connector_id;

	if (output->virtual_output)
		return -1;

	/* Get current active display configuration */
	rc = GetDisplayConfiguration(output->display_id, &current_config);
	if (rc != kErrorNone) {
		/* Failed to get config, use default */
		current_config.x_pixels = 1920;
		current_config.y_pixels = 1080;
		current_config.fps = 60;
	}

	/* Enumerate all available display configurations */
	rc = GetDisplayConfigCount(output->display_id, &count);
	if (rc == kErrorNone && count > 0) {
		uint32_t modes_added = 0;
		for (index = 0; index < count; index++) {
			struct drm_mode *mode = NULL;

			rc = GetDisplayConfigurationByIndex(output->display_id,
							    index, &display_config);
			if (rc == kErrorNone) {
				mode = drm_output_add_mode(output, &display_config, index);
				if (!mode) {
					weston_log("Failed to add mode %u for output %s\n",
						   index, base->name);
					continue;
				}
				modes_added++;

				/* Mark the current active mode */
				if (mode->base.width == current_config.x_pixels &&
				    mode->base.height == current_config.y_pixels &&
				    mode->base.refresh == current_config.fps * 1000) {
					mode->base.flags |= WL_OUTPUT_MODE_CURRENT;
					current_mode = mode;
				}
			}
		}

		if (modes_added == 0) {
			weston_log("Warning: No modes could be enumerated for output %s\n",
				   base->name);
		}
	} else {
		weston_log("Failed to get display config count or count is 0 for output %s\n",
			   base->name);
	}

	/* Set current_mode, fallback to default if not found */
	if (current_mode) {
		output->base.current_mode = &current_mode->base;
	} else {
		/* Fallback: create a default mode if enumeration failed */
		struct drm_mode *fallback = zalloc(sizeof(struct drm_mode));
		if (!fallback)
			return -1;

		fallback->base.width = current_config.x_pixels;
		fallback->base.height = current_config.y_pixels;
		fallback->base.refresh = current_config.fps * 1000;
		fallback->base.flags = WL_OUTPUT_MODE_CURRENT;

		wl_list_insert(output->base.mode_list.prev, &fallback->base.link);
		output->base.current_mode = &fallback->base;
	}

	output->base.native_mode = output->base.current_mode;
	output->base.native_scale = output->base.current_scale;

	return 0;
}
