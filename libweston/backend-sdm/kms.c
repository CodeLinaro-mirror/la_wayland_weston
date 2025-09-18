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

#include <stdint.h>
#include <libweston/libweston.h>
#include "shared/helpers.h"
#include "sdm-internal.h"
#include "pixel-formats.h"
#include "presentation-time-server-protocol.h"

extern int display_id;

struct drm_property_enum_info dpms_state_enums[] = {
	[WDRM_DPMS_STATE_OFF] = {
		.name = "Off",
	},
	[WDRM_DPMS_STATE_ON] = {
		.name = "On",
	},
	[WDRM_DPMS_STATE_STANDBY] = {
		.name = "Standby",
	},
	[WDRM_DPMS_STATE_SUSPEND] = {
		.name = "Suspend",
	},
};

struct drm_property_enum_info content_protection_enums[] = {
	[WDRM_CONTENT_PROTECTION_UNDESIRED] = {
		.name = "Undesired",
	},
	[WDRM_CONTENT_PROTECTION_DESIRED] = {
		.name = "Desired",
	},
	[WDRM_CONTENT_PROTECTION_ENABLED] = {
		.name = "Enabled",
	},
};

void
drm_output_update_msc(struct drm_output *output, unsigned int seq)
{
	uint64_t msc_hi = output->base.msc >> 32;

	if (seq < (output->base.msc & 0xffffffff))
		msc_hi++;

	output->base.msc = (msc_hi << 32) + seq;
}

int
on_drm_input(int fd, uint32_t mask, void *data)
{
	//SDM takes care of vsync
	return 1;
}

int
init_kms_caps(struct drm_device *device)
{
	struct drm_backend *b = device->backend;
	uint64_t cap;
	int ret;
	clockid_t clk_id;

	weston_log("using %s\n", device->drm.filename);

	ret = drmGetCap(device->drm.fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1)
		clk_id = CLOCK_MONOTONIC;
	else
		clk_id = CLOCK_REALTIME;

	weston_log("using clk %s\n", (clk_id == CLOCK_MONOTONIC)? "MONOTONIC":"REALTIME");
	b->base.supported_presentation_clocks = 1 << clk_id;

	ret = drmGetCap(device->drm.fd, DRM_CAP_CURSOR_WIDTH, &cap);
	if (ret == 0)
		device->cursor_width = cap;
	else
		device->cursor_width = 64;

	ret = drmGetCap(device->drm.fd, DRM_CAP_CURSOR_HEIGHT, &cap);
	if (ret == 0)
		device->cursor_height = cap;
	else
		device->cursor_height = 64;

	device->repaint_data = NULL;

	ret = drmSetClientCap(device->drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (ret) {
		weston_log("Error: drm card doesn't support universal planes!\n");
		return -1;
	}

	if (!getenv("WESTON_DISABLE_ATOMIC")) {
		ret = drmGetCap(device->drm.fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap);
		if (ret != 0)
			cap = 0;
		ret = drmSetClientCap(device->drm.fd, DRM_CLIENT_CAP_ATOMIC, 1);
		device->atomic_modeset = ((ret == 0) && (cap == 1));
	}
	weston_log("DRM: %s atomic modesetting\n",
		   device->atomic_modeset ? "supports" : "does not support");

	if (!getenv("WESTON_DISABLE_GBM_MODIFIERS")) {
		ret = drmGetCap(device->drm.fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
		if (ret == 0)
			device->fb_modifiers = cap;
	}
	weston_log("DRM: %s GBM modifiers\n",
		   device->fb_modifiers ? "supports" : "does not support");

	/*
	 * KMS support for hardware planes cannot properly synchronize
	 * without nuclear page flip. Without nuclear/atomic, hw plane
	 * and cursor plane updates would either tear or cause extra
	 * waits for vblanks which means dropping the compositor framerate
	 * to a fraction. For cursors, it's not so bad, so they are
	 * enabled.
	 */
	if (!device->atomic_modeset || getenv("WESTON_FORCE_RENDERER"))
		device->sprites_are_broken = true;

	ret = drmSetClientCap(device->drm.fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);
	device->aspect_ratio_supported = (ret == 0);
	weston_log("DRM: %s picture aspect ratio\n",
		   device->aspect_ratio_supported ? "supports" : "does not support");

	return 0;
}
