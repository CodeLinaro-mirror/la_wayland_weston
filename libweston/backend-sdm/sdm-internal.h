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
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 */

#ifndef SDM_INTERNAL_H_
#define SDM_INTERNAL_H_

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <assert.h>
#include <sys/mman.h>
#include <time.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#ifdef BUILD_DRM_GBM
#include <gbm.h>
#endif
#include <libudev.h>

#include <libweston/libweston.h>
#include <libweston/backend-drm.h>
#include <libweston/weston-log.h>
#include "shared/helpers.h"
#include "libinput-seat.h"
#include "backend.h"
#include "libweston-internal.h"

#include "sdm-service/sdm_display_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A small wrapper to print information into the 'drm-backend' debug scope.
 *
 * The following conventions are used to print variables:
 *
 *  - fixed uint32_t values, including Weston object IDs such as weston_output
 *    IDs, DRM object IDs such as CRTCs or properties, and GBM/DRM formats:
 *      "%lu (0x%lx)" (unsigned long) value, (unsigned long) value
 *
 *  - fixed uint64_t values, such as DRM property values (including object IDs
 *    when used as a value):
 *      "%llu (0x%llx)" (unsigned long long) value, (unsigned long long) value
 *
 *  - non-fixed-width signed int:
 *      "%d" value
 *
 *  - non-fixed-width unsigned int:
 *      "%u (0x%x)" value, value
 *
 *  - non-fixed-width unsigned long:
 *      "%lu (0x%lx)" value, value
 *
 * Either the integer or hexadecimal forms may be omitted if it is known that
 * one representation is not useful (e.g. width/height in hex are rarely what
 * you want).
 *
 * This is to avoid implicit widening or narrowing when we use fixed-size
 * types: uint32_t can be resolved by either unsigned int or unsigned long
 * on a 32-bit system but only unsigned int on a 64-bit system, with uint64_t
 * being unsigned long long on a 32-bit system and unsigned long on a 64-bit
 * system. To avoid confusing side effects, we explicitly cast to the widest
 * possible type and use a matching format specifier.
 */
#define drm_debug(b, ...) \
	weston_log_scope_printf((b)->debug, __VA_ARGS__)

#define MAX_CLONED_CONNECTORS 4

/**
 * Represents the values of an enum-type KMS property
 */
struct drm_property_enum_info {
	const char *name; /**< name as string (static, not freed) */
	bool valid; /**< true if value is supported; ignore if false */
	uint64_t value; /**< raw value */
};

enum wdrm_content_protection_state {
	WDRM_CONTENT_PROTECTION_UNDESIRED = 0,
	WDRM_CONTENT_PROTECTION_DESIRED,
	WDRM_CONTENT_PROTECTION_ENABLED,
	WDRM_CONTENT_PROTECTION__COUNT
};

enum wdrm_hdcp_content_type {
	WDRM_HDCP_CONTENT_TYPE0 = 0,
	WDRM_HDCP_CONTENT_TYPE1,
	WDRM_HDCP_CONTENT_TYPE__COUNT
};

enum wdrm_dpms_state {
	WDRM_DPMS_STATE_OFF = 0,
	WDRM_DPMS_STATE_ON,
	WDRM_DPMS_STATE_STANDBY, /* unused */
	WDRM_DPMS_STATE_SUSPEND, /* unused */
	WDRM_DPMS_STATE__COUNT
};

enum wdrm_panel_orientation {
	WDRM_PANEL_ORIENTATION_NORMAL = 0,
	WDRM_PANEL_ORIENTATION_UPSIDE_DOWN,
	WDRM_PANEL_ORIENTATION_LEFT_SIDE_UP,
	WDRM_PANEL_ORIENTATION_RIGHT_SIDE_UP,
	WDRM_PANEL_ORIENTATION__COUNT
};

enum wdrm_content_type {
	WDRM_CONTENT_TYPE_NO_DATA = 0,
	WDRM_CONTENT_TYPE_GRAPHICS,
	WDRM_CONTENT_TYPE_PHOTO,
	WDRM_CONTENT_TYPE_CINEMA,
	WDRM_CONTENT_TYPE_GAME,
	WDRM_CONTENT_TYPE__COUNT
};

/**
 * Reasons why placing a view on a plane failed. Needed by the dma-buf feedback.
 */
enum try_view_on_plane_failure_reasons {
	FAILURE_REASONS_NONE = 0,
	FAILURE_REASONS_FORCE_RENDERER = 1 << 0,
	FAILURE_REASONS_FB_FORMAT_INCOMPATIBLE = 1 << 1,
	FAILURE_REASONS_DMABUF_MODIFIER_INVALID = 1 << 2,
	FAILURE_REASONS_ADD_FB_FAILED = 1 << 3,
	FAILURE_REASONS_NO_PLANES_AVAILABLE = 1 << 4,
	FAILURE_REASONS_PLANES_REJECTED = 1 << 5,
	FAILURE_REASONS_INADEQUATE_CONTENT_PROTECTION = 1 << 6,
	FAILURE_REASONS_INCOMPATIBLE_TRANSFORM = 1 << 7,
	FAILURE_REASONS_NO_BUFFER = 1 << 8,
	FAILURE_REASONS_BUFFER_TYPE = 1 << 9,
	FAILURE_REASONS_GLOBAL_ALPHA = 1 << 10,
	FAILURE_REASONS_NO_GBM = 1 << 11,
	FAILURE_REASONS_GBM_BO_IMPORT_FAILED = 1 << 12,
	FAILURE_REASONS_GBM_BO_GET_HANDLE_FAILED = 1 << 13,
};

struct drm_device {
	struct drm_backend *backend;
	struct {
		int id;
		int fd;
		char *filename;
		dev_t devnum;
	} drm;

	/* Track the GEM handles if the device does not have a gbm device, which
	 * tracks the handles for us.
	 */
	struct hash_table *gem_handle_refcnt;

	bool state_invalid;
	bool atomic_modeset;
	bool tearing_supported;
	bool aspect_ratio_supported;
	int32_t cursor_width;
	int32_t cursor_height;
	bool cursors_are_broken;
	bool sprites_are_broken;
	void *repaint_data;
	bool fb_modifiers;

	/* we need these parameters in order to not fail drmModeAddFB2()
	 * due to out of bounds dimensions, and then mistakenly set
	 * sprites_are_broken:
	 */
	int min_width, max_width;
	int min_height, max_height;

	/* drm_backend::kms_list */
	struct wl_list link;
};


struct drm_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;

	struct udev *udev;

	struct udev_monitor *udev_monitor;
	struct wl_event_source *udev_drm_source;

	struct drm_device *drm;
	/* drm_device::link */
	struct wl_list kms_list;
	struct gbm_device *gbm;
	struct wl_listener session_listener;
	const struct pixel_format_info *format;
	bool use_pixman_shadow;
	struct udev_input input;
	uint32_t pageflip_timeout;
	struct weston_log_scope *debug;
};

struct drm_mode {
	struct weston_mode base;
	drmModeModeInfo mode_info;
	uint32_t blob_id;
};

enum drm_fb_type {
	BUFFER_INVALID = 0, /**< never used */
	BUFFER_CLIENT, /**< directly sourced from client */
	BUFFER_DMABUF, /**< imported from linux_dmabuf client */
	BUFFER_PIXMAN_DUMB, /**< internal Pixman rendering */
	BUFFER_PIXMAN_GBM,  /**< internal Pixman rendering with GBM */
	BUFFER_GBM_SURFACE, /**< internal EGL rendering */
	BUFFER_CURSOR, /**< internal cursor buffer */
};

struct drm_fb {
	enum drm_fb_type type;
	struct drm_backend *backend;
	int refcnt;
	uint32_t fb_id, size;
	uint32_t handles[4];
	uint32_t strides[4];
	uint32_t offsets[4];
	int num_planes;
	const struct pixel_format_info *format;
	uint64_t modifier;
	int width, height;
	int fd;
	int dma_fd;
	struct weston_buffer_reference buffer_ref;
	struct weston_buffer_release_reference buffer_release_ref;
	/* Used by gbm fbs */
	struct gbm_bo *bo;
	struct gbm_surface *gbm_surface;
	/* Used by dumb fbs */
	void *map;
};

struct sdm_layer {
	struct wl_list link; /* drm_output::sdm_layer_list */
	struct weston_paint_node *pnode;
	struct weston_buffer_reference buffer_ref;
	bool is_cursor;
	bool is_skip;
	bool is_scanout;
	struct gbm_bo *bo;
	struct drm_fb *fb;
	uint32_t composition_type; /* type: enum SDM_COMPOSITION_XXXXX */
	pixman_region32_t overlap;
	int acquire_fence_fd;
};

struct drm_head {
	struct weston_head base;
	struct drm_device *device;
	uint32_t connector_id;
	struct backlight *backlight;
	drmModeModeInfo inherited_mode;	/**< Original mode on the connector */
	uint32_t inherited_max_bpc;	/**< Original max_bpc on the connector */
	uint32_t inherited_crtc_id;	/**< Original CRTC assignment */
};

struct drm_output {
	struct weston_output base;
	struct drm_backend *backend;
	struct drm_device *device;
	bool page_flip_pending;
	bool atomic_complete_pending;
	bool destroy_pending;
	bool disable_pending;
	bool dpms_off_pending;
	bool commit_pending;
	bool mode_switch_pending;
	bool first_cycle;
	uint32_t gbm_cursor_handle[2];
	struct drm_fb *gbm_cursor_fb[2];
	struct drm_plane *cursor_plane;
	struct weston_view *cursor_view;
	int current_cursor;
	struct gbm_surface *gbm_surface;
	const struct pixel_format_info *format;
	uint32_t gbm_bo_flags;
	unsigned max_bpc;
	struct drm_fb *next_fb, *current_fb;
	struct gbm_bo *dumb_bo[2];
	struct weston_renderbuffer *renderbuffer[2];
	int current_image;
	struct vaapi_recorder *recorder;
	struct wl_listener recorder_frame_listener;
	struct wl_event_source *pageflip_timer;
	submit_frame_cb virtual_submit_frame;
	enum wdrm_content_type content_type;
	int vblank_ev_fd;
	struct wl_event_source *vblank_ev_source;
	struct {
		unsigned int frame;
		unsigned int sec;
		unsigned int usec;
	} last_vblank;
	bool virtual_output;
	int view_count; //counts number of sdm layers created
	struct wl_list sdm_layer_list;  /* sdm_layer::link      */
	struct wl_list prev_sdm_layer_list;  /* prev_sdm_layer::link  */
	enum dpms_enum dpms; //tracks dpms level of output
	int display_id;
	int retire_fence_fd;
};

void
drm_destroy(struct weston_backend *backend);

static inline struct drm_head *
to_drm_head(struct weston_head *base)
{
	if (base->backend->destroy != drm_destroy)
		return NULL;
	return container_of(base, struct drm_head, base);
}

void
drm_output_destroy(struct weston_output *output_base);

static inline struct drm_output *
to_drm_output(struct weston_output *base)
{
	if (
#ifdef BUILD_DRM_VIRTUAL
	    base->destroy != drm_virtual_output_destroy &&
#endif
	    base->destroy != drm_output_destroy)
		return NULL;
	return container_of(base, struct drm_output, base);
}

static inline struct drm_backend *
to_drm_backend(struct weston_compositor *base)
{
	struct weston_backend *backend;

	wl_list_for_each(backend, &base->backend_list, link) {
		if (backend->destroy == drm_destroy)
			return container_of(backend, struct drm_backend, base);
	}

	return NULL;
}

static inline struct drm_mode *
to_drm_mode(struct weston_mode *base)
{
	return container_of(base, struct drm_mode, base);
}

struct drm_head *
drm_head_find_by_connector(struct drm_backend *backend, uint32_t connector_id);

void
sdm_weston_region_global_to_output(pixman_region32_t *dst,
				struct drm_output *output,
				pixman_region32_t *src);

void
sdm_weston_global_transform_rect(struct weston_paint_node *node,
				pixman_box32_t *box, float *x1, float *y1,
				float *x2, float *y2);

int
sdm_weston_is_pixman_render(struct drm_output *output);

static inline bool
drm_view_transform_supported(struct weston_view *ev, struct weston_output *output)
{
	/* This will incorrectly disallow cases where the combination of
	 * buffer and view transformations match the output transform.
	 * Fixing this requires a full analysis of the transformation
	 * chain. */
	if (ev->transform.enabled &&
	    ev->transform.matrix.type >= WESTON_MATRIX_TRANSFORM_ROTATE)
		return false;

	return true;
}

void
drm_mode_list_destroy(struct drm_device *device, struct wl_list *mode_list);

void
drm_output_print_modes(struct drm_output *output);

struct drm_mode *
drm_output_choose_mode(struct drm_output *output,
		       struct weston_mode *target_mode);
int
drm_output_set_mode(struct weston_output *base,
		    enum weston_drm_backend_output_mode mode,
		    const char *modeline);

extern struct drm_property_enum_info dpms_state_enums[];
extern struct drm_property_enum_info content_protection_enums[];
extern struct drm_property_enum_info hdcp_content_type_enums[];

int
init_kms_caps(struct drm_device *device);

void
drm_output_update_msc(struct drm_output *output, unsigned int seq);
void
drm_output_update_complete(struct drm_output *output, uint32_t flags,
			   unsigned int sec, unsigned int usec);
int
on_drm_input(int fd, uint32_t mask, void *data);

struct drm_fb *
drm_fb_ref(struct drm_fb *fb);
void
drm_fb_unref(struct drm_fb *fb);

struct drm_fb *
drm_fb_create_dumb(struct drm_device *device, int width, int height,
		   uint32_t format);
struct drm_fb *
drm_fb_get_from_bo(struct gbm_bo *bo, struct drm_device *device,
		   bool is_opaque, enum drm_fb_type type);

#ifdef BUILD_DRM_GBM
extern struct drm_fb *
drm_fb_get_from_paint_node(struct drm_output *output,
			   struct weston_paint_node *pnode);
extern bool
drm_can_scanout_dmabuf(struct weston_backend *backend,
		       struct linux_dmabuf_buffer *dmabuf);
#else
static inline struct drm_fb *
drm_fb_get_from_paint_node(struct drm_output *output,
			   struct weston_paint_node *pnode)
{
	weston_log("Not built with BUILD_DRM_GBM set\n");
	return NULL;
}

static inline bool
drm_can_scanout_dmabuf(struct weston_backend *backend,
		       struct linux_dmabuf_buffer *dmabuf)
{
	return false;
}
#endif

void
destroy_sdm_layer(struct sdm_layer *layer);

void
drm_assign_planes(struct weston_output *output_base);

int
parse_gbm_format(const char *s, const struct pixel_format_info *default_format,
		 const struct pixel_format_info **format);

bool
is_virtual_output(int display_id);

void NotifyOnRefresh(struct drm_output *drm_output);

void NotifyOnQdcmRefresh(struct drm_output *output);

#ifdef BUILD_DRM_VIRTUAL
extern int
drm_backend_init_virtual_output_api(struct weston_compositor *compositor);
#else
inline static int
drm_backend_init_virtual_output_api(struct weston_compositor *compositor)
{
	return 0;
}
#endif

#ifdef BUILD_DRM_GBM
int
init_egl(struct drm_backend *b);

int
drm_output_init_egl(struct drm_output *output, struct drm_backend *b);

void
drm_output_fini_egl(struct drm_output *output);

struct drm_fb *
drm_output_render_gl(struct drm_output *output, pixman_region32_t *damage);
#else
inline static int
init_egl(struct drm_backend *b)
{
	weston_log("Compiled without GBM/EGL support\n");
	return -1;
}

inline static int
drm_output_init_egl(struct drm_output *output, struct drm_backend *b)
{
	return -1;
}

inline static void
drm_output_fini_egl(struct drm_output *output)
{
}

inline static struct drm_fb *
drm_output_render_gl(struct drm_output *output, pixman_region32_t *damage)
{
	return NULL;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
