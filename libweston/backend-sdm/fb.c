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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 */

#include "config.h"
#include <stdint.h>
#include <libweston/libweston.h>
#include <libweston/pixel-formats.h>
#include <libweston/linux-dmabuf.h>
#include "shared/helpers.h"
#include "sdm-internal.h"
#include "linux-dmabuf.h"
#include "gbm-buffer-backend.h"
#include <display/drm/sde_drm.h>

#include <gbm.h>
#include <gbm_priv.h>

static void
drm_fb_destroy(struct drm_fb *fb)
{
	if (fb->fb_id != 0)
		drmModeRmFB(fb->fd, fb->fb_id);
	if (fb->dma_fd > 0)
		close(fb->dma_fd);
	weston_buffer_reference(&fb->buffer_ref, NULL, BUFFER_WILL_NOT_BE_ACCESSED);
	weston_buffer_release_reference(&fb->buffer_release_ref, NULL);
	free(fb);
}

static void
drm_fb_destroy_dumb(struct drm_fb *fb)
{
	struct drm_mode_destroy_dumb destroy_arg;

	assert(fb->type == BUFFER_PIXMAN_DUMB);

	if (fb->map && fb->size > 0)
		munmap(fb->map, fb->size);

	memset(&destroy_arg, 0, sizeof(destroy_arg));
	destroy_arg.handle = fb->handles[0];
	drmIoctl(fb->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

	drm_fb_destroy(fb);
}

#ifdef BUILD_SDM_GBM
static int gem_handle_get(struct drm_device *device, int handle)
{
	unsigned int *ref_count;

	ref_count = hash_table_lookup(device->gem_handle_refcnt, handle);
	if (!ref_count) {
		ref_count = zalloc(sizeof(*ref_count));
		hash_table_insert(device->gem_handle_refcnt, handle, ref_count);
	}
	(*ref_count)++;

	return handle;
}

static void gem_handle_put(struct drm_device *device, int handle)
{
	unsigned int *ref_count;

	if (handle == 0)
		return;

	ref_count = hash_table_lookup(device->gem_handle_refcnt, handle);
	if (!ref_count) {
		weston_log("failed to find GEM handle %d for device %s\n",
			   handle, device->drm.filename);
		return;
	}
	(*ref_count)--;

	if (*ref_count == 0) {
		hash_table_remove(device->gem_handle_refcnt, handle);
		free(ref_count);
		drmCloseBufferHandle(device->drm.fd, handle);
	}
}

static int
drm_fb_import_plane(struct drm_device *device, struct drm_fb *fb, int plane)
{
	int bo_fd;
	uint32_t handle;
	int ret;

	bo_fd = gbm_bo_get_fd_for_plane(fb->bo, plane);
	if (bo_fd < 0)
		return bo_fd;

	/*
	 * drmPrimeFDToHandle is dangerous, because the GEM handles are
	 * not reference counted by the kernel and user space needs a
	 * single reference counting implementation to avoid double
	 * closing of GEM handles.
	 *
	 * It is not desirable to use a GBM device here, because this
	 * requires a GBM device implementation, which might not be
	 * available for simple or custom DRM devices that only support
	 * scanout and no rendering.
	 *
	 * We are only importing the buffers from the render device to
	 * the scanout device if the devices are distinct, since
	 * otherwise no import is necessary. Therefore, we are the only
	 * instance using the handles and we can implement reference
	 * counting for the handles per device. See gem_handle_get and
	 * gem_handle_put for the implementation.
	 */
	ret = drmPrimeFDToHandle(fb->fd, bo_fd, &handle);
	if (ret)
		goto out;

	fb->handles[plane] = gem_handle_get(device, handle);

out:
	close(bo_fd);
	return ret;
}
#endif

static int
drm_fb_addfb(struct drm_device *device, struct drm_fb *fb)
{
    int ret = -EINVAL;
    uint64_t mods[4] = { };
    size_t i;

    /* If we have a modifier set, we must only use the WithModifiers
     * entrypoint; we cannot import it through legacy ioctls. */
    if (device->fb_modifiers && fb->modifier != DRM_FORMAT_MOD_INVALID) {
        /* KMS demands that if a modifier is set, it must be the same
         * for all planes. */
        for (i = 0; i < ARRAY_LENGTH(mods) && fb->handles[i]; i++)
            mods[i] = fb->modifier;
        ret = drmModeAddFB2WithModifiers(fb->fd, fb->width, fb->height,
                                         fb->format->format,
                                         fb->handles, fb->strides,
                                         fb->offsets, mods, &fb->fb_id,
                                         DRM_MODE_FB_MODIFIERS);
        return ret;
    }

    ret = drmModeAddFB2(fb->fd, fb->width, fb->height, fb->format->format,
                        fb->handles, fb->strides, fb->offsets, &fb->fb_id, 0);

    if (ret == 0)
        return 0;

    /* Legacy AddFB can't always infer the format from depth/bpp alone, so
     * check if our format is one of the lucky ones. */
    if (!fb->format->addfb_legacy_depth || !fb->format->bpp)
        return ret;

    /* Cannot fall back to AddFB for multi-planar formats either. */
    if (fb->handles[1] || fb->handles[2] || fb->handles[3])
        return ret;

    ret = drmModeAddFB(fb->fd, fb->width, fb->height,
                       fb->format->addfb_legacy_depth, fb->format->bpp,
                       fb->strides[0], fb->handles[0], &fb->fb_id);
    return ret;
}

struct drm_fb *
drm_fb_create_dumb(struct drm_device *device, int width, int height,
		   uint32_t format)
{
	struct drm_fb *fb;
	int ret;

	struct drm_mode_create_dumb create_arg;
	struct drm_mode_destroy_dumb destroy_arg;
	struct drm_mode_map_dumb map_arg;

	fb = zalloc(sizeof *fb);
	if (!fb)
		return NULL;
	fb->refcnt = 1;

	fb->backend = device->backend;

	fb->format = pixel_format_get_info(format);
	if (!fb->format) {
		weston_log("failed to look up format 0x%lx\n",
			   (unsigned long) format);
		goto err_fb;
	}

	if (!fb->format->addfb_legacy_depth || !fb->format->bpp) {
		weston_log("format 0x%lx is not compatible with dumb buffers\n",
			   (unsigned long) format);
		goto err_fb;
	}

	memset(&create_arg, 0, sizeof create_arg);
	create_arg.bpp = fb->format->bpp;
	create_arg.width = width;
	create_arg.height = height;

	ret = drmIoctl(device->drm.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
	if (ret)
		goto err_fb;

	fb->type = BUFFER_PIXMAN_DUMB;
	fb->modifier = DRM_FORMAT_MOD_INVALID;
	fb->handles[0] = create_arg.handle;
	fb->strides[0] = create_arg.pitch;
	fb->num_planes = 1;
	fb->size = create_arg.size;
	fb->width = width;
	fb->height = height;
	fb->fd = device->drm.fd;

	if (drm_fb_addfb(device, fb) != 0) {
		weston_log("failed to create kms fb: %s\n", strerror(errno));
		goto err_bo;
	}

	memset(&map_arg, 0, sizeof map_arg);
	map_arg.handle = fb->handles[0];
	ret = drmIoctl(fb->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	if (ret)
		goto err_add_fb;

	fb->map = mmap(NULL, fb->size, PROT_WRITE,
		       MAP_SHARED, device->drm.fd,map_arg.offset);
	if (fb->map == MAP_FAILED)
		goto err_add_fb;

	return fb;

err_add_fb:
	drmModeRmFB(device->drm.fd, fb->fb_id);
err_bo:
	memset(&destroy_arg, 0, sizeof(destroy_arg));
	destroy_arg.handle = create_arg.handle;
	drmIoctl(device->drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
err_fb:
	free(fb);
	return NULL;
}

struct drm_fb *
drm_fb_ref(struct drm_fb *fb)
{
	fb->refcnt++;
	return fb;
}

#ifdef BUILD_SDM_GBM
static void
drm_fb_destroy_gbm(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;

	drm_fb_destroy(fb);
}

static void
drm_fb_destroy_dmabuf(struct drm_fb *fb)
{
	/* We deliberately do not close the GEM handles here; GBM manages
	 * their lifetime through the BO. */
	if (fb->bo)
		gbm_bo_destroy(fb->bo);
	drm_fb_destroy(fb);
}

static struct drm_fb *
drm_fb_get_from_dmabuf(struct linux_dmabuf_buffer *dmabuf,
		       struct drm_device *device, bool is_opaque,
		       uint32_t *try_view_on_plane_failure_reasons)
{
	struct drm_backend *backend = device->backend;
	struct drm_fb *fb;
	int i;

	if (!dmabuf)
		return NULL;
	struct gbm_import_fd_data import_legacy = {
		.width = dmabuf->attributes.width,
		.height = dmabuf->attributes.height,
		.format = dmabuf->attributes.format,
		.stride = dmabuf->attributes.stride[0],
		.fd = dmabuf->attributes.fd[0],
	};

	struct gbm_import_fd_modifier_data import_mod = {
		.width = dmabuf->attributes.width,
		.height = dmabuf->attributes.height,
		.format = dmabuf->attributes.format,
		.num_fds = dmabuf->attributes.n_planes,
		.modifier = dmabuf->attributes.modifier[0],
	};

	fb = zalloc(sizeof *fb);
	if (fb == NULL)
		return NULL;

	fb->refcnt = 1;
	fb->type = BUFFER_DMABUF;
	fb->backend = device->backend;

	static_assert(ARRAY_LENGTH(import_mod.fds) ==
		      ARRAY_LENGTH(dmabuf->attributes.fd),
		      "GBM and linux_dmabuf FD size must match");
	static_assert(sizeof(import_mod.fds) == sizeof(dmabuf->attributes.fd),
		      "GBM and linux_dmabuf FD size must match");
	memcpy(import_mod.fds, dmabuf->attributes.fd, sizeof(import_mod.fds));

	static_assert(ARRAY_LENGTH(import_mod.strides) ==
		      ARRAY_LENGTH(dmabuf->attributes.stride),
		      "GBM and linux_dmabuf stride size must match");
	static_assert(sizeof(import_mod.strides) ==
		      sizeof(dmabuf->attributes.stride),
		      "GBM and linux_dmabuf stride size must match");
	memcpy(import_mod.strides, dmabuf->attributes.stride,
	       sizeof(import_mod.strides));

	static_assert(ARRAY_LENGTH(import_mod.offsets) ==
		      ARRAY_LENGTH(dmabuf->attributes.offset),
		      "GBM and linux_dmabuf offset size must match");
	static_assert(sizeof(import_mod.offsets) ==
		      sizeof(dmabuf->attributes.offset),
		      "GBM and linux_dmabuf offset size must match");
	memcpy(import_mod.offsets, dmabuf->attributes.offset,
	       sizeof(import_mod.offsets));

	/* The legacy FD-import path does not allow us to supply modifiers,
	 * multiple planes, or buffer offsets. */
	if (dmabuf->attributes.modifier[0] != DRM_FORMAT_MOD_INVALID ||
	    dmabuf->attributes.n_planes > 1 ||
	    dmabuf->attributes.offset[0] > 0) {
		fb->bo = gbm_bo_import(backend->gbm, GBM_BO_IMPORT_FD_MODIFIER,
				       &import_mod,
				       GBM_BO_USE_SCANOUT);
	} else {
		fb->bo = gbm_bo_import(backend->gbm, GBM_BO_IMPORT_FD,
				       &import_legacy,
				       GBM_BO_USE_SCANOUT);
	}

	if (!fb->bo)
		goto err_free;

	fb->width = dmabuf->attributes.width;
	fb->height = dmabuf->attributes.height;
	fb->modifier = dmabuf->attributes.modifier[0];
	fb->size = 0;
	fb->fd = device->drm.fd;
	fb->dma_fd = gbm_bo_get_fd(fb->bo);
	static_assert(ARRAY_LENGTH(fb->strides) ==
		      ARRAY_LENGTH(dmabuf->attributes.stride),
		      "drm_fb and dmabuf stride size must match");
	static_assert(sizeof(fb->strides) == sizeof(dmabuf->attributes.stride),
		      "drm_fb and dmabuf stride size must match");
	memcpy(fb->strides, dmabuf->attributes.stride, sizeof(fb->strides));
	static_assert(ARRAY_LENGTH(fb->offsets) ==
		      ARRAY_LENGTH(dmabuf->attributes.offset),
		      "drm_fb and dmabuf offset size must match");
	static_assert(sizeof(fb->offsets) == sizeof(dmabuf->attributes.offset),
		      "drm_fb and dmabuf offset size must match");
	memcpy(fb->offsets, dmabuf->attributes.offset, sizeof(fb->offsets));

	fb->format = pixel_format_get_info(dmabuf->attributes.format);
	if (!fb->format) {
		weston_log("couldn't look up format info for 0x%lx\n",
			   (unsigned long) dmabuf->attributes.format);
		goto err_free;
	}

	if (is_opaque)
		fb->format = pixel_format_get_opaque_substitute(fb->format);

	fb->num_planes = dmabuf->attributes.n_planes;
	for (i = 0; i < dmabuf->attributes.n_planes; i++) {
		union gbm_bo_handle handle;

	        handle = gbm_bo_get_handle_for_plane(fb->bo, i);
		if (handle.s32 == -1) {
			*try_view_on_plane_failure_reasons |=
				FAILURE_REASONS_GBM_BO_GET_HANDLE_FAILED;
			goto err_free;
		}
		fb->handles[i] = handle.u32;
	}

	if (drm_fb_addfb(device, fb) != 0) {
		if (try_view_on_plane_failure_reasons)
			*try_view_on_plane_failure_reasons |=
				FAILURE_REASONS_ADD_FB_FAILED;

		weston_log("[%s]:failed to create kms fb: %s\n", __func__,
				strerror(errno));
		weston_log("[%s] fmt(%s:0x%x) num_planes(%d)\n", __FUNCTION__,
				fb->format->drm_format_name, fb->format->format,
				fb->num_planes);
		for (int i = 0; i < fb->num_planes; i++) {
			weston_log("[%s] fb strides[%d] = %d, handles[%d] = %d,\
					offset[%d] = %d\n", __FUNCTION__,
					i, fb->strides[i],
					i, fb->handles[i],
					i, fb->offsets[i]);
		}
		goto err_free;
	}

	return fb;

err_free:
	if (fb->dma_fd > 0) {
		close(fb->dma_fd);
	}
	drm_fb_destroy_dmabuf(fb);
	return NULL;
}

static void
get_drm_format(uint32_t format, struct drm_fb *fb)
{
	switch (format) {
		case GBM_FORMAT_YCbCr_420_TP10_UBWC:
			fb->format = pixel_format_get_info(DRM_FORMAT_NV12);
			fb->modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED |
					DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT;
			break;
		case GBM_FORMAT_YCbCr_420_P010_UBWC:
			fb->format = pixel_format_get_info(DRM_FORMAT_NV12);
			fb->modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED |
					DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT;
			break;
		case GBM_FORMAT_YCbCr_420_P010_VENUS:
		case GBM_FORMAT_P010:
			fb->format = pixel_format_get_info(DRM_FORMAT_NV12);
			fb->modifier = DRM_FORMAT_MOD_QCOM_DX;
			break;
		default:
			fb->format = NULL;
	}
}

struct drm_fb *
drm_fb_get_from_bo(struct gbm_bo *bo, struct drm_device *device,
		   bool is_opaque, enum drm_fb_type type)
{
	struct drm_fb *fb = gbm_bo_get_user_data(bo);
	int i;

	if (fb) {
		assert(fb->type == type);
		return drm_fb_ref(fb);
	}

	fb = zalloc(sizeof *fb);
	if (fb == NULL)
		return NULL;

	fb->type = type;
	fb->refcnt = 1;
	fb->bo = bo;
	fb->backend = device->backend;
	fb->fd = device->drm.fd;

	fb->width = gbm_bo_get_width(bo);
	fb->height = gbm_bo_get_height(bo);
	fb->format = pixel_format_get_info(gbm_bo_get_format(bo));
	fb->dma_fd = gbm_bo_get_fd(bo);

	fb->modifier = gbm_bo_get_modifier(bo);
	fb->num_planes = gbm_bo_get_plane_count(bo);
	/*
	  TODO: A more elegant solution would be to create a GBM API call
		to get number of non-meta planes.
		For UBWC formats, 2 planes represent the actual format component,
		and 2 planes contain meta information. We only want to iterate over
		the actual YUV planes for the purposes of strides/offsets calculations.
	*/
	fb->num_planes = fb->num_planes > 3 ? 2 : fb->num_planes;
	for (i = 0; i < fb->num_planes; i++) {
		fb->strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		fb->handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		fb->offsets[i] = gbm_bo_get_offset(bo, i);
	}

	if (!fb->format) {
		get_drm_format(gbm_bo_get_format(bo), fb);
		if (!fb->format) {
			weston_log("couldn't look up format 0x%lx\n",
				   (unsigned long) gbm_bo_get_format(bo));
			goto err_free;
		}
	}

	fb->size = fb->strides[0] * fb->height;

	if (is_opaque)
		fb->format = pixel_format_get_opaque_substitute(fb->format);

	if (drm_fb_addfb(device, fb) != 0) {
		weston_log("failed to create kms fb: %s\n",
				strerror(errno));
		weston_log("[%s] fmt(%s:0x%x) num_planes(%d)\n", __FUNCTION__,
				fb->format->drm_format_name, fb->format->format,
				fb->num_planes);
		for (int i = 0; i < fb->num_planes; i++) {
			weston_log("[%s] fb strides[%d] = %d, handles[%d] = %d,\
					offset[%d] = %d\n", __FUNCTION__,
					i, fb->strides[i],
					i, fb->handles[i],
					i, fb->offsets[i]);
		}
		goto err_free;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_gbm);

	return fb;

err_free:
	if (fb->dma_fd > 0) {
		close(fb->dma_fd);
	}
	free(fb);
	return NULL;
}

static void
drm_fb_set_buffer(struct drm_fb *fb, struct weston_buffer *buffer,
		  struct weston_buffer_release *buffer_release)
{
	assert(fb->buffer_ref.buffer == NULL);
	weston_buffer_reference(&fb->buffer_ref, buffer, BUFFER_MAY_BE_ACCESSED);
	weston_buffer_release_reference(&fb->buffer_release_ref,
					buffer_release);
}
#endif

void
drm_fb_unref(struct drm_fb *fb)
{
	if (!fb)
		return;

	assert(fb->refcnt > 0);
	if (--fb->refcnt > 0)
		return;

	switch (fb->type) {
	case BUFFER_PIXMAN_DUMB:
		drm_fb_destroy_dumb(fb);
		break;
	case BUFFER_PIXMAN_GBM:
		break;
#ifdef BUILD_SDM_GBM
	case BUFFER_CURSOR:
	case BUFFER_CLIENT:
		gbm_bo_destroy(fb->bo);
		break;
	case BUFFER_GBM_SURFACE:
		gbm_surface_release_buffer(fb->gbm_surface, fb->bo);
		break;
	case BUFFER_DMABUF:
		drm_fb_destroy_dmabuf(fb);
		break;
#endif
	default:
		assert(NULL);
		break;
	}
}

#ifdef BUILD_SDM_GBM
bool
drm_can_scanout_dmabuf(struct weston_backend *backend,
		       struct linux_dmabuf_buffer *dmabuf)
{
	struct drm_backend *b = container_of(backend, struct drm_backend, base);
	struct drm_fb *fb;
	struct drm_device *device = b->drm;
	bool ret = false;
	uint32_t try_reason = 0x0;

	fb = drm_fb_get_from_dmabuf(dmabuf, device, true, &try_reason);
	if (fb)
		ret = true;

	drm_fb_unref(fb);
	drm_debug(b, "[dmabuf] dmabuf %p, import test %s, with reason 0x%x\n", dmabuf,
		      ret ? "succeeded" : "failed", try_reason);
	return ret;
}

struct drm_fb *
drm_fb_get_from_paint_node(struct drm_output *output,
			   struct weston_paint_node *pnode)
{
	struct drm_backend *b = output->backend;
	struct drm_device *device = output->device;
	struct weston_view *ev = pnode->view;
	struct weston_buffer *buffer = ev->surface->buffer_ref.buffer;
	bool is_opaque = weston_view_is_opaque(ev, &ev->transform.boundingbox);
	struct drm_fb *fb;

	if (ev->surface->protection_mode == WESTON_SURFACE_PROTECTION_MODE_ENFORCED &&
	    ev->surface->desired_protection > output->base.current_protection) {
		pnode->try_view_on_plane_failure_reasons |=
			FAILURE_REASONS_INADEQUATE_CONTENT_PROTECTION;
		return NULL;
	}

	if (!buffer) {
		pnode->try_view_on_plane_failure_reasons |= FAILURE_REASONS_NO_BUFFER;
		return NULL;
	}

	/* GBM is used for dmabuf import as well as from client wl_buffer. */
	if (!b->gbm) {
		pnode->try_view_on_plane_failure_reasons |= FAILURE_REASONS_NO_GBM;
		return NULL;
	}

	if (buffer->type == WESTON_BUFFER_DMABUF) {
		fb = drm_fb_get_from_dmabuf(buffer->dmabuf, device, is_opaque,
					    &pnode->try_view_on_plane_failure_reasons);
		if (!fb) {
			return NULL;
		}
	} else if (buffer->type == WESTON_BUFFER_RENDERER_OPAQUE) {
		struct gbm_bo *bo;

		bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_WL_BUFFER,
				   buffer->resource, GBM_BO_USE_SCANOUT);
		if (!bo)
			return NULL;

		fb = drm_fb_get_from_bo(bo, device, is_opaque, BUFFER_CLIENT);
		if (!fb) {
			pnode->try_view_on_plane_failure_reasons |=
				(1 << FAILURE_REASONS_ADD_FB_FAILED);
			gbm_bo_destroy(bo);
			return NULL;
		}
	} else if (buffer->type == WESTON_BUFFER_GBMBUF) {
		struct gbm_bo *bo = NULL;
		struct gbm_buffer *gbmbuf = buffer->gbmbuf;

		if (gbmbuf) {
			//gstreamer will use this for buffer sharing
			struct gbm_buf_info gbmbuf_info = {
				.fd = gbmbuf->fd,
				.metadata_fd = gbmbuf->metadata_fd,
				.width = gbmbuf->width,
				.height = gbmbuf->height,
				.format = gbmbuf->format
			};

			bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_GBM_BUF_TYPE,
					&gbmbuf_info, GBM_BO_USE_SCANOUT);
		}

		if (!bo) {
			pnode->try_view_on_plane_failure_reasons |=
					FAILURE_REASONS_GBM_BO_IMPORT_FAILED;
			return NULL;
		}

		fb = drm_fb_get_from_bo(bo, device, is_opaque, BUFFER_CLIENT);
		if (!fb) {
			pnode->try_view_on_plane_failure_reasons |=
				(1 << FAILURE_REASONS_ADD_FB_FAILED);
			gbm_bo_destroy(bo);
			return NULL;
		}
	} else {
		pnode->try_view_on_plane_failure_reasons |= FAILURE_REASONS_BUFFER_TYPE;
		return NULL;
	}

	drm_debug(b, "\t\t\t[view] view %p format: %s\n",
		  ev, fb->format->drm_format_name);
	drm_fb_set_buffer(fb, buffer,
			ev->surface->buffer_release_ref.buffer_release);

	return fb;

}

#endif
