/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 * Copyright © 2017, 2018 Collabora, Ltd.
 * Copyright © 2017, 2018 General Electric Company
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <dlfcn.h>

#include "drm-internal.h"
#include "renderer-pixman/pixman-renderer.h"
#include "pixel-formats.h"
#include "renderer-gl/gl-renderer.h"
#include "renderer-vulkan/vulkan-renderer.h"
#include "shared/string-helpers.h"
#include "shared/weston-assert.h"
#include "shared/weston-egl-ext.h"
#include "linux-dmabuf.h"
#include "linux-explicit-synchronization.h"
#include "shared/fd-util.h"
#include "shared/xalloc.h"
#include "weston-trace.h"

/* When initializing EGL, if the preferred buffer format isn't available
 * we may be able to substitute an ARGB format for an XRGB one.
 *
 * This returns NULL if substitution isn't possible. The caller is responsible
 * for checking for NULL before calling gl_renderer->create().
 *
 * This works around https://bugs.freedesktop.org/show_bug.cgi?id=89689
 * but it's entirely possible we'll see this again on other implementations.
 */
static const struct pixel_format_info *
fallback_format_for(const struct pixel_format_info *format)
{
	return pixel_format_get_info_by_opaque_substitute(format->format);
}

static int
drm_backend_create_gl_renderer(struct drm_backend *b)
{
	const struct pixel_format_info *format[3] = {
		b->format,
		fallback_format_for(b->format),
	};
	struct gl_renderer_display_options options = {
		.egl_platform = EGL_PLATFORM_SURFACELESS_MESA,
		.egl_native_display = NULL,
		.formats = format,
		.formats_count = format[1] ? 2 : 1,
	};
	struct gl_renderer_display_options legacy_options = {
		.egl_platform = EGL_PLATFORM_GBM_KHR,
		.egl_native_display = b->gbm,
		.egl_surface_type = EGL_WINDOW_BIT,
		.formats = format,
		.formats_count = format[1] ? 2 : 1,
	};
	int ret;

	b->platform_type = DRM_PLATFORM_SURFACELESS;
	ret = weston_compositor_init_renderer(b->compositor,
					      WESTON_RENDERER_GL,
					      &options.base);
	if (ret == 0)
		return 0;

	b->platform_type = DRM_PLATFORM_HAS_SURFACE;
	return weston_compositor_init_renderer(b->compositor,
					       WESTON_RENDERER_GL,
					       &legacy_options.base);
}

static int
drm_backend_create_vulkan_renderer(struct drm_backend *b)
{
	const struct pixel_format_info *format[3] = {
		b->format,
		fallback_format_for(b->format),
	};
	struct vulkan_renderer_display_options options = {
		.formats = format,
		.formats_count = 1,
	};

	if (format[1])
		options.formats_count = 2;

	b->platform_type = DRM_PLATFORM_SURFACELESS;
	return weston_compositor_init_renderer(b->compositor,
					       WESTON_RENDERER_VULKAN,
					       &options.base);
}

int
init_egl(struct drm_backend *b)
{
	if (!b->gbm)
		return -1;

	if (drm_backend_create_gl_renderer(b) < 0)
		return -1;

	return 0;
}

int
init_vulkan(struct drm_backend *b)
{
	if (!b->gbm)
		return -1;

	if (drm_backend_create_vulkan_renderer(b) < 0)
		return -1;

	return 0;
}

bool
drm_gbm_alloc_cursor_fb(struct drm_output *output, struct drm_backend *b,
			struct drm_cursor *cursor)
{
	struct drm_device *device = output->device;
	struct gbm_bo *bo;

	bo = gbm_bo_create(b->gbm, device->cursor_width, device->cursor_height,
			   GBM_FORMAT_ARGB8888,
			   GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
	if (!bo)
		return false;

	cursor->fb = drm_fb_get_from_bo(bo, device, BUFFER_CURSOR);
	if (!cursor->fb) {
		gbm_bo_destroy(bo);
		return false;
	}
	cursor->handle = gbm_bo_get_handle(bo).s32;

	return true;
}

static int
create_gbm_surface(struct gbm_device *gbm, struct drm_output *output)
{
	struct weston_mode *mode = output->base.current_mode;
	struct drm_plane *plane = output->scanout_handle->plane;
	struct drm_device *device = output->device;
	struct weston_drm_format *fmt;
	struct gbm_bo *legacy_test_bo = NULL;
	const uint64_t *modifiers;
	unsigned int num_modifiers;

	fmt = weston_drm_format_array_find_format(&plane->formats,
						  output->format->format);
	if (!fmt) {
		weston_log("format %s not supported by output %s\n",
			   output->format->drm_format_name,
			   output->base.name);
		return -1;
	}

	if (!weston_drm_format_has_modifier(fmt, DRM_FORMAT_MOD_INVALID)) {
		modifiers = weston_drm_format_get_modifiers(fmt, &num_modifiers);
		output->gbm_surface =
			gbm_surface_create_with_modifiers2(gbm,
							   mode->width, mode->height,
							   output->format->format,
							   modifiers, num_modifiers,
							   output->gbm_bo_flags);
		/* We allocate a dummy buffer as something we can use for
		 * drm test commits. We can't just lock the front buffer,
		 * because you can't do that before any rendering takes
		 * place.
		 *
		 * We create an fb from it to get a reference, so the
		 * plane state code won't free it later.
		 */
		if (output->gbm_surface) {
			legacy_test_bo =
				gbm_bo_create_with_modifiers2(gbm, mode->width, mode->height,
							      output->format->format,
							      modifiers, num_modifiers,
							      output->gbm_bo_flags);
			if (!legacy_test_bo)
				goto err;

			output->legacy_test_fb = drm_fb_get_from_bo(legacy_test_bo,
								    device, BUFFER_GBM_TEST);
			if (!output->legacy_test_fb)
				goto err;

			return 0;
		}
	}

	/*
	 * If we cannot use modifiers to allocate the GBM surface and the GBM
	 * device differs from the KMS display device (because we are rendering
	 * on a different GPU), we have to use linear buffers to make sure that
	 * the allocated GBM surface is correctly displayed on the KMS device.
	 */
	if (gbm_device_get_fd(gbm) != output->device->kms_device->fd)
		output->gbm_bo_flags |= GBM_BO_USE_LINEAR;

	/* We may allocate with no modifiers in the following situations:
	 *
	 * 1. the KMS driver does not support modifiers;
	 * 2. if allocating with modifiers failed, what can happen when the KMS
	 *    display device supports modifiers but the GBM driver does not,
	 *    e.g. the old i915 Mesa driver.
	 */
	if (!output->gbm_surface)
		output->gbm_surface = gbm_surface_create(gbm,
							 mode->width, mode->height,
							 output->format->format,
							 output->gbm_bo_flags);

	if (output->gbm_surface) {
		legacy_test_bo =
			gbm_bo_create(gbm, mode->width, mode->height,
				      output->format->format,
				      output->gbm_bo_flags);

		if (!legacy_test_bo)
			goto err;

		output->legacy_test_fb = drm_fb_get_from_bo(legacy_test_bo,
							    device, BUFFER_GBM_TEST);
		if (!output->legacy_test_fb)
			goto err;
	}

	return 0;

err:
	if (legacy_test_bo)
		gbm_bo_destroy(legacy_test_bo);

	if (output->gbm_surface) {
		gbm_surface_destroy(output->gbm_surface);
		output->gbm_surface = NULL;
	}
	return -1;
}

void
drm_init_gbm(struct drm_backend *b)
{
	struct drm_device *device = b->drm;

	b->gbm = gbm_create_device(device->kms_device->fd);
}

void
drm_fini_gbm(struct drm_backend *b)
{
	if (b->gbm)
		gbm_device_destroy(b->gbm);
	b->gbm = NULL;
}

enum format_alpha_required {
	FORMAT_ALPHA_REQUIRED = true,
	FORMAT_ALPHA_NOT_REQUIRED = false,
};

enum format_component_type {
	FORMAT_COMPONENT_TYPE_ANY,
	FORMAT_COMPONENT_TYPE_FLOAT_ONLY,
};

static const struct pixel_format_info *
find_compatible_format(struct weston_compositor *compositor,
		       struct wl_array *formats, int min_bpc,
		       enum format_component_type component_type,
		       enum format_alpha_required alpha_required)
{
	const struct pixel_format_info **tmp, *p;
	const struct pixel_format_info *candidate = NULL;

	/**
	 * Given a format array, this looks for a format respecting a few
	 * criteria. First of all, this ignores formats that do not contain an
	 * alpha channel when alpha_required == FORMAT_ALPHA_REQUIRED. Similar
	 * for formats that are not floating point when component_type ==
	 * FORMAT_COMPONENT_TYPE_FLOAT_ONLY. Also, it ignores formats that do
	 * not have bits per color channel (bpc) bigger or equal to min_bpc.
	 *
	 * When we have multiple formats matching these criteria, we use the
	 * following to choose:
	 *
	 * 1. a format with lower bytes per pixel (bpp) is favored.
	 *
	 * 2. if FORMAT_ALPHA_REQUIRED:
	 *	  we prefer the format with more bits on the alpha channel
	 *    else
	 *        we prefer the format with more bits on the color channels
	 */
	wl_array_for_each(tmp, formats) {
		p = *tmp;

		/* Skip candidates that do not match minimum criteria. */
		if (component_type == FORMAT_COMPONENT_TYPE_FLOAT_ONLY &&
		    p->component_type != PIXEL_COMPONENT_TYPE_FLOAT)
			continue;
		if (alpha_required == FORMAT_ALPHA_REQUIRED && p->bits.a == 0)
			continue;
		if (p->bits.r < min_bpc || p->bits.g < min_bpc || p->bits.b < min_bpc)
			continue;

		/* No other good candidate so far, so pick this one. */
		if (!candidate) {
			candidate = p;
			continue;
		}

		/**
		 * New candidate, let's compare with old and untie.
		 */

		if (p->bpp > candidate->bpp)
			continue;

		if (alpha_required == FORMAT_ALPHA_REQUIRED) {
			if (p->bits.a <= candidate->bits.a)
				continue;
		} else {
			if (p->bits.r + p->bits.g + p->bits.b <=
			    candidate->bits.r + candidate->bits.g + candidate->bits.b)
				continue;
		}

		candidate = p;
	}

	return candidate;
}

static bool
drm_output_pick_format_egl(struct drm_output *output)
{
	struct drm_device *device = output->device;
	struct drm_backend *b = device->backend;
	struct weston_compositor *compositor = b->compositor;
	const struct weston_renderer *renderer = compositor->renderer;
	const struct pixel_format_info **renderer_formats;
	const struct pixel_format_info **f;
	unsigned int renderer_formats_count;
	struct wl_array supported_formats;
	enum format_component_type component_type;
	uint32_t min_bpc;
	unsigned int i;
	bool ret = true;
	bool found;

	wl_array_init(&supported_formats);

	/**
	 * This computes the intersection between renderer formats supported by
	 * EGL and the output scanout plane supported formats. We need that as
	 * we want to select a format supported by both.
	 */
	renderer_formats =
		renderer->gl->get_supported_rendering_formats(b->compositor,
							      &renderer_formats_count);
	for (i = 0; i < renderer_formats_count; i++) {
		struct drm_plane *scanout_plane = output->scanout_handle->plane;

		if (!weston_drm_format_array_find_format(&scanout_plane->formats,
							 renderer_formats[i]->format))
			continue;

		f = wl_array_add(&supported_formats, sizeof(*f));
		*f = renderer_formats[i];
	}

	if (output->base.from_blend_to_output_by_backend) {
		if ((compositor->capabilities & WESTON_CAP_SHADER_BLENDING) &&
		    (compositor->test_data.test_quirks.blending_impl !=
		     WESTON_BLENDING_IMPL_FF))
			component_type = FORMAT_COMPONENT_TYPE_ANY;
		else
			component_type = FORMAT_COMPONENT_TYPE_FLOAT_ONLY;
		min_bpc = 16;
	} else if (output->base.eotf_mode != WESTON_EOTF_MODE_SDR) {
		component_type = FORMAT_COMPONENT_TYPE_ANY;
		min_bpc = 10;
	} else {
		/**
		 * If no requirements, we simply use b->format instead of
		 * looking for a format with bpc >= min_bpc.
		 */
		min_bpc = 0;
	}

	if (min_bpc != 0) {
		if (output->has_underlay) {
			output->format =
				find_compatible_format(compositor, &supported_formats,
						       min_bpc, component_type,
						       FORMAT_ALPHA_REQUIRED);
			if (output->format)
				goto done;

			weston_log("Disabling underlay planes: EGL GBM or the primary plane for output '%s'\n" \
				   "does not support format with min bpc %u and alpha channel.\n",
				   output->base.name, min_bpc);
			output->has_underlay = false;
		}

		output->format =
			find_compatible_format(compositor, &supported_formats,
					       min_bpc, component_type,
					       FORMAT_ALPHA_NOT_REQUIRED);
		if (output->format)
			goto done;

		weston_log("Error: EGL GBM or the primary plane for output '%s' does not support format\n" \
			   "with min bpc %u.\n", output->base.name, min_bpc);
		ret = false;
		goto done;
	}

	found = false;
	wl_array_for_each(f, &supported_formats) {
		if ((*f)->format == b->format->format) {
			found = true;
			break;
		}
	}
	if (!found) {
		weston_log("Error: format %s unsupported by EGL GBM or the primary plane for output '%s'.\n",
			   b->format->drm_format_name, output->base.name);
		ret = false;
		goto done;
	}

	if (output->has_underlay && (b->format->bits.a == 0)) {
		weston_log("Disabling underlay planes: b->format %s does not have alpha channel,\n"
			   "which is required to support underlay planes.\n",
			   b->format->drm_format_name);
		output->has_underlay = false;
	}

	output->format = b->format;

done:
	wl_array_release(&supported_formats);
	return ret;
}

static struct gbm_bo *
drm_gbm_create_bo(struct gbm_device *gbm, struct drm_output *output)
{
	struct weston_mode *mode = output->base.current_mode;
	struct drm_plane *plane = output->scanout_handle->plane;
	struct weston_drm_format *fmt;
	const uint64_t *modifiers;
	unsigned int num_modifiers;
	struct gbm_bo *bo = NULL;

	/*
	 * TODO: Currently, this method allocates a buffer based on the list
	 * of acceptable modifiers received from the DRM backend but does not
	 * check it against formats renderable by the renderer.
	 * To support cases where the renderer may not support the same
	 * modifiers (e.g. Vulkan software renderer) it should match against
	 * renderer modifiers.
	 */

	fmt = weston_drm_format_array_find_format(&plane->formats,
						  output->format->format);
	if (!fmt) {
		weston_log("format %s not supported by output %s\n",
			   output->format->drm_format_name,
			   output->base.name);
		return NULL;
	}

	if (!weston_drm_format_has_modifier(fmt, DRM_FORMAT_MOD_INVALID)) {
		modifiers = weston_drm_format_get_modifiers(fmt, &num_modifiers);
		bo = gbm_bo_create_with_modifiers2(gbm, mode->width, mode->height,
						   output->format->format,
						   modifiers, num_modifiers,
						   output->gbm_bo_flags);
	}

	/*
	 * If we cannot use modifiers to allocate the GBM surface and
	 * the GBM device differs from the KMS display device, try to
	 * use linear buffers and hope that the allocated GBM surface
	 * is correctly displayed on the KMS device.
	 */
	if (gbm_device_get_fd(gbm) != output->device->kms_device->fd)
		output->gbm_bo_flags |= GBM_BO_USE_LINEAR;

	if (!bo) {
		bo = gbm_bo_create(gbm, mode->width, mode->height,
				   output->format->format, output->gbm_bo_flags);
	}

	return bo;
}

struct drm_gbm_dmabuf {
	struct linux_dmabuf_memory base;
	struct gbm_bo *bo;
};

static void
drm_gbm_dmabuf_destroy(struct linux_dmabuf_memory *dmabuf)
{
	struct dmabuf_attributes *attributes;
	struct drm_gbm_dmabuf *drm_gbm_dmabuf;
	struct gbm_bo *bo;

	drm_gbm_dmabuf = container_of(dmabuf, struct drm_gbm_dmabuf, base);
	bo = drm_gbm_dmabuf->bo;
	assert(bo);

	gbm_bo_destroy(bo);

	attributes = dmabuf->attributes;
	for (int i = 0; i < attributes->n_planes; ++i)
		close(attributes->fd[i]);
	free(dmabuf->attributes);

	free(drm_gbm_dmabuf);
}

static struct drm_gbm_dmabuf *
drm_gbm_bo_get_dmabuf(struct gbm_device *gbm, struct drm_output *output, struct gbm_bo *bo)
{
	struct drm_gbm_dmabuf *drm_gbm_dmabuf;
	struct dmabuf_attributes *attributes;

	attributes = xzalloc(sizeof(*attributes));
	attributes->width = gbm_bo_get_width(bo);
	attributes->height = gbm_bo_get_height(bo);
	attributes->format = gbm_bo_get_format(bo);
	attributes->n_planes = gbm_bo_get_plane_count(bo);
	for (int i = 0; i < attributes->n_planes; ++i) {
		attributes->fd[i] = gbm_bo_get_fd(bo);
		attributes->stride[i] = gbm_bo_get_stride_for_plane(bo, i);
		attributes->offset[i] = gbm_bo_get_offset(bo, i);
	}
	attributes->modifier = gbm_bo_get_modifier(bo);

	drm_gbm_dmabuf = xzalloc(sizeof(*drm_gbm_dmabuf));
	drm_gbm_dmabuf->base.attributes = attributes;
	drm_gbm_dmabuf->base.destroy = drm_gbm_dmabuf_destroy;
	drm_gbm_dmabuf->bo = bo;

	return drm_gbm_dmabuf;
}

static int
create_renderbuffers(struct gbm_device *gbm, struct drm_output *output)
{
	struct weston_compositor *compositor = output->base.compositor;
	struct weston_renderer *renderer = compositor->renderer;
	unsigned int num_buffers = ARRAY_LENGTH(output->renderbuffer);
	struct drm_device *device = output->device;

	for (unsigned int i = 0; i < num_buffers; i++) {
		struct drm_gbm_dmabuf *drm_gbm_dmabuf;
		struct gbm_bo *bo;

		/* To clean up on failure, we need to start from a clean state */
		weston_assert_ptr_null(compositor, output->renderbuffer[i]);
		weston_assert_ptr_null(compositor, output->renderbuffer_fb[i]);

		bo = drm_gbm_create_bo(gbm, output);
		if (!bo) {
			weston_log("failed to allocate bo\n");
			goto err;
		}

		/* This one can't fail, so needs no check. */
		drm_gbm_dmabuf = drm_gbm_bo_get_dmabuf(gbm, output, bo);

		output->renderbuffer[i] =
			renderer->create_renderbuffer_dmabuf(&output->base,
							     &drm_gbm_dmabuf->base,
							     NULL, NULL);
		if (!output->renderbuffer[i]) {
			/* This would be owned by the renderbuffer, but we didn't
			 * get that far.
			 */
			drm_gbm_dmabuf->base.destroy(&drm_gbm_dmabuf->base);
			weston_log("failed to allocate renderbuffer\n");
			goto err;
		}

		output->renderbuffer_fb[i] =
			drm_fb_get_from_dmabuf_attributes(drm_gbm_dmabuf->base.attributes, device,
							  false, true, NULL);
		if (!output->renderbuffer_fb[i]) {
			weston_log("failed to create renderbuffer fb\n");
			goto err;
		}
	}
	return 0;
err:
	for (unsigned int i = 0; i < num_buffers; i++) {
		if (output->renderbuffer[i]) {
			drm_fb_unref(output->renderbuffer_fb[i]);
			renderer->destroy_renderbuffer(output->renderbuffer[i]);
			output->renderbuffer[i] = NULL;
		}
	}
	return -1;
}

static int
drm_output_init_egl_surfaceless(struct drm_output *output, struct drm_backend *b,
				struct weston_geometry area, struct weston_size fb_size)
{
	const struct weston_renderer *renderer = b->compositor->renderer;
	struct gl_renderer_fbo_options options = {
		.area = area,
		.fb_size = fb_size,
	};

	if (renderer->gl->output_fbo_create(&output->base, &options) < 0) {
		return -1;
	}

	if (create_renderbuffers(b->gbm, output) < 0) {
		weston_log("failed to create dmabufs\n");
		return -1;
	}

	return 0;
}

static int
drm_output_init_egl_legacy(struct drm_output *output, struct drm_backend *b,
			   struct weston_geometry area, struct weston_size fb_size)
{
	const struct weston_renderer *renderer = b->compositor->renderer;
	const struct pixel_format_info *format[2] = { 0 };
	struct gl_renderer_output_options options = {
		.area = area,
		.fb_size = fb_size,
	};

	format[0] = output->format;
	if (!output->has_underlay)
		format[1] = fallback_format_for(output->format);

	options.formats = format;
	options.formats_count = format[1] ? 2 : 1;

	assert(output->gbm_surface == NULL);
	if (create_gbm_surface(b->gbm, output) < 0) {
		weston_log("failed to create gbm surface\n");
		return -1;
	}

	options.window_for_legacy = (EGLNativeWindowType) output->gbm_surface;
	options.window_for_platform = output->gbm_surface;
	if (renderer->gl->output_window_create(&output->base, &options) < 0) {
		weston_log("failed to create gl renderer output state\n");
		gbm_surface_destroy(output->gbm_surface);
		drm_fb_unref(output->legacy_test_fb);
		output->gbm_surface = NULL;
		return -1;
	}

	return 0;
}

/* Init output state that depends on gl or gbm */
int
drm_output_init_egl(struct drm_output *output, struct drm_backend *b)
{
	struct weston_mode *mode = output->base.current_mode;
	struct weston_geometry area;
	struct weston_size fb_size;

	if (!output->format && !drm_output_pick_format_egl(output))
		return -1;

	area.x = 0;
	area.y = 0;
	area.width = mode->width;
	area.height = mode->height;
	fb_size.width = mode->width;
	fb_size.height = mode->height;

	switch (b->platform_type) {
	case DRM_PLATFORM_HAS_SURFACE:
		return drm_output_init_egl_legacy(output, b, area, fb_size);
	case DRM_PLATFORM_SURFACELESS:
		return drm_output_init_egl_surfaceless(output, b, area, fb_size);
	}
	weston_assert_not_reached(b->compositor, "Invalid platform type");
}

static bool
drm_output_pick_format_vulkan(struct drm_output *output)
{
	struct drm_device *device = output->device;
	struct drm_backend *b = device->backend;

	/* Any other value of eotf_mode requires color-management, which is not
	 * yet supported by vulkan-renderer. */
	assert(output->base.eotf_mode == WESTON_EOTF_MODE_SDR);

	if (!b->format->vulkan_format) {
		weston_log("Error: failed to pick format for output '%s', format %s unsupported by vulkan-renderer.\n",
			   output->base.name, b->format->drm_format_name);
		return false;
	}

	assert(b->format);
	output->format = b->format;

	if (output->has_underlay && (output->format->bits.a == 0)) {
		weston_log("Disabling underlay planes: output '%s' with format %s does not have alpha channel,\n"
			   "which is required to support underlay planes.\n",
			   output->base.name, output->format->drm_format_name);
		output->has_underlay = false;
	}

	return true;
}

/* Init output state that depends on vulkan */
int
drm_output_init_vulkan(struct drm_output *output, struct drm_backend *b)
{
	const struct weston_mode *mode = output->base.current_mode;
	struct weston_renderer *renderer = b->compositor->renderer;

	if (!output->format && !drm_output_pick_format_vulkan(output))
		return -1;

	const struct vulkan_renderer_surfaceless_options options = {
		.area.x = 0,
		.area.y = 0,
		.area.width = mode->width,
		.area.height = mode->height,
		.fb_size.width = mode->width,
		.fb_size.height = mode->height,
	};

	if (renderer->vulkan->output_surfaceless_create(&output->base, &options) < 0) {
		weston_log("failed to create vulkan renderer output state\n");
		return -1;
	}

	if (create_renderbuffers(b->gbm, output) < 0) {
		weston_log("failed to create dmabufs\n");
		return -1;
	}

	return 0;
}

void
drm_output_fini_egl(struct drm_output *output)
{
	const struct weston_renderer *renderer = output->base.compositor->renderer;

	renderer->gl->output_destroy(&output->base);
	if (output->backend->platform_type == DRM_PLATFORM_HAS_SURFACE) {
		gbm_surface_destroy(output->gbm_surface);
		drm_fb_unref(output->legacy_test_fb);
		output->gbm_surface = NULL;
		return;
	}

	for (unsigned int i = 0; i < ARRAY_LENGTH(output->renderbuffer); i++)
		drm_fb_unref(output->renderbuffer_fb[i]);
}

void
drm_output_fini_vulkan(struct drm_output *output)
{
	const struct weston_renderer *renderer = output->base.compositor->renderer;

	for (unsigned int i = 0; i < ARRAY_LENGTH(output->renderbuffer); i++) {
		drm_fb_unref(output->renderbuffer_fb[i]);
		renderer->destroy_renderbuffer(output->renderbuffer[i]);
	}

	renderer->vulkan->output_destroy(&output->base);
}

static struct drm_fb *
drm_output_render_gl_surfaceless(struct drm_output_state *state, pixman_region32_t *damage)
{
	WESTON_TRACE_FUNC();
	struct drm_output *output = state->output;
	struct weston_renderbuffer *renderbuffer;
	struct drm_fb *ret;

	renderbuffer = output->renderbuffer[output->current_image];
	output->base.compositor->renderer->repaint_output(&output->base,
							  damage,
							  renderbuffer);

	ret = drm_fb_ref(output->renderbuffer_fb[output->current_image]);

	output->current_image = (output->current_image + 1) % ARRAY_LENGTH(output->renderbuffer);

	return ret;
}

static struct drm_fb *
drm_output_render_gl_legacy(struct drm_output_state *state, pixman_region32_t *damage)
{
	WESTON_TRACE_FUNC();
	struct drm_output *output = state->output;
	struct drm_device *device = output->device;
	struct gbm_bo *bo;
	struct drm_fb *ret;

	output->base.compositor->renderer->repaint_output(&output->base,
							  damage, NULL);

	bo = gbm_surface_lock_front_buffer(output->gbm_surface);
	if (!bo) {
		weston_log("failed to lock front buffer: %s\n",
			   strerror(errno));
		return NULL;
	}

	/* Output transparent/opaque image according to the format required by
	 * the client. */
	ret = drm_fb_get_from_bo(bo, device, BUFFER_GBM_SURFACE);
	if (!ret) {
		weston_log("failed to get drm_fb for bo\n");
		gbm_surface_release_buffer(output->gbm_surface, bo);
		return NULL;
	}
	ret->gbm_surface = output->gbm_surface;

	return ret;
}

struct drm_fb *
drm_output_render_gl(struct drm_output_state *state, pixman_region32_t *damage)
{
	struct drm_backend *b = state->output->backend;

	switch (b->platform_type) {
	case DRM_PLATFORM_HAS_SURFACE:
		return drm_output_render_gl_legacy(state, damage);
	case DRM_PLATFORM_SURFACELESS:
		return drm_output_render_gl_surfaceless(state, damage);
	}
	weston_assert_not_reached(b->compositor, "Invalid platform type");
}

struct drm_fb *
drm_output_render_vulkan(struct drm_output_state *state, pixman_region32_t *damage)
{
	struct drm_output *output = state->output;
	struct drm_plane_state *pstate =
		drm_output_state_get_plane(state, output->scanout_handle->plane);
	struct weston_renderer *renderer = output->base.compositor->renderer;
	struct drm_fb *ret;
	int fence_fd;

	renderer->repaint_output(&output->base, damage,
				 output->renderbuffer[output->current_image]);

	ret = drm_fb_ref(output->renderbuffer_fb[output->current_image]);

	fence_fd = renderer->vulkan->create_fence_fd(&output->base);
	if (fence_fd < 0) {
		weston_log("failed to get fence fd from rendering\n");
		drm_fb_unref(ret);
		return NULL;
	}
	fd_update(&pstate->in_fence_fd, fence_fd);

	output->current_image = (output->current_image + 1) % ARRAY_LENGTH(output->renderbuffer);

	return ret;
}
