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

#include "config.h"

#include "colorops.h"
#include "color-properties.h"
#include "drm-internal.h"
#include "shared/string-helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"

static void
drm_colorop_3x1d_lut_blob_destroy(struct drm_colorop_3x1d_lut_blob *lut)
{
	wl_list_remove(&lut->destroy_listener.link);
	wl_list_remove(&lut->link);
	drmModeDestroyPropertyBlob(lut->device->kms_device->fd, lut->blob_id);
	free(lut);
}

static void
drm_colorop_3x1d_lut_blob_destroy_handler(struct wl_listener *l, void *data)
{
	struct drm_colorop_3x1d_lut_blob *lut;

	lut = wl_container_of(l, lut, destroy_listener);
	assert(lut->xform == data);

	drm_colorop_3x1d_lut_blob_destroy(lut);
}

/**
 * Search for a 3x1D LUT colorop blob in a DRM device.
 *
 * \param device The DRM device in which we want to look for the blob.
 * \param xform The xform from which the LUT comes from.
 * \param curve_step What curve step from the xform originated the 3x1D LUT.
 * \param lut_len How many taps each of the 1D LUT has.
 */
struct drm_colorop_3x1d_lut_blob *
drm_colorop_3x1d_lut_blob_search(struct drm_device *device,
				 struct weston_color_transform *xform,
				 enum weston_color_curve_step curve_step,
				 uint32_t lut_len)
{
	struct drm_colorop_3x1d_lut_blob *lut;

	wl_list_for_each(lut, &device->drm_colorop_3x1d_lut_blob_list, link)
		if (lut->xform == xform && lut->curve_step == curve_step &&
		    lut->lut_len == lut_len)
			return lut;

	return NULL;
}

/**
 * Create a 3x1D LUT colorop blob.
 *
 * A Weston colorop is an object associated with a step from a struct
 * weston_color_transform and that can be used to program KMS color operations.
 * This function creates a blob for such kind of object and cache that in the
 * given DRM device, so we can avoid re-creating it.
 *
 * \param device The DRM device in which this colorop blob is stored.
 * \param xform The xform from which the LUT comes from. This object matches its
 * lifetime.
 * \param curve_step What xform curve step originated the 3x1D LUT.
 * \param lut_len The number of taps for each of the 1D LUT.
 * \param blob_id The KMS blob id (associated to the DRM device).
 * \return The 3x1D LUT colorop blob.
 */
struct drm_colorop_3x1d_lut_blob *
drm_colorop_3x1d_lut_blob_create(struct drm_device *device,
				 struct weston_color_transform *xform,
				 enum weston_color_curve_step curve_step,
				 uint32_t lut_len, uint32_t blob_id)
{
	struct drm_colorop_3x1d_lut_blob *lut;

	lut = xzalloc(sizeof(*lut));

	lut->device = device;
	lut->xform = xform;
	lut->curve_step = curve_step;
	lut->lut_len = lut_len;
	lut->blob_id = blob_id;

	wl_list_insert(&device->drm_colorop_3x1d_lut_blob_list, &lut->link);

	lut->destroy_listener.notify = drm_colorop_3x1d_lut_blob_destroy_handler;
	wl_signal_add(&lut->xform->destroy_signal, &lut->destroy_listener);

	return lut;
}

static void
drm_colorop_destroy(struct drm_colorop *colorop)
{
	wl_list_remove(&colorop->link);
	drm_property_info_free(colorop->props, WDRM_COLOROP__COUNT);

	free(colorop);
}

static struct drm_colorop *
drm_colorop_create(struct drm_color_pipeline *pipeline, uint32_t colorop_id,
		   uint32_t *next_colorop_id)
{
	struct drm_device *device = pipeline->plane->device;
	drmModeObjectPropertiesPtr props_drm;
	struct drm_colorop *colorop;

	*next_colorop_id = 0;

	props_drm = drmModeObjectGetProperties(device->kms_device->fd, colorop_id,
					       DRM_MODE_OBJECT_COLOROP);
	if (!props_drm)
		return NULL;

	colorop = xzalloc(sizeof(*colorop));

	wl_list_insert(pipeline->colorop_list.prev, &colorop->link);

	colorop->id = colorop_id;
	colorop->pipeline = pipeline;

	drm_property_info_populate(device, colorop_props, colorop->props,
				   WDRM_COLOROP__COUNT, props_drm);

	colorop->type = drm_property_get_value(&colorop->props[WDRM_COLOROP_TYPE],
					       props_drm, WDRM_COLOROP_TYPE__COUNT);
	if (colorop->type == WDRM_COLOROP_TYPE__COUNT) {
		drm_colorop_destroy(colorop);
		drmModeFreeObjectProperties(props_drm);
		return NULL;
	}

	colorop->size = drm_property_get_value(&colorop->props[WDRM_COLOROP_SIZE],
					       props_drm, 0);
	if (colorop->size == 0 && (colorop->type == WDRM_COLOROP_TYPE_1D_LUT ||
				   colorop->type == WDRM_COLOROP_TYPE_3D_LUT)) {
		drm_colorop_destroy(colorop);
		drmModeFreeObjectProperties(props_drm);
		return NULL;
	}

	colorop->can_bypass = (colorop->props[WDRM_COLOROP_BYPASS].prop_id != 0);

	*next_colorop_id =
		drm_property_get_value(&colorop->props[WDRM_COLOROP_NEXT],
				       props_drm, 0);

	drmModeFreeObjectProperties(props_drm);

	return colorop;
}

static const char *
drm_colorop_type_to_str(struct drm_colorop *colorop)
{
	return colorop->props[WDRM_COLOROP_TYPE].enum_values[colorop->type].name;
}

static void
drm_color_pipeline_print(struct drm_color_pipeline *pipeline, FILE *fp)
{
	struct drm_colorop *colorop;
	struct drm_property_info *curve_props;
	const char *type;
	const char *sep = "	";
	unsigned int i;

	if (!fp)
		return;

	fprintf(fp, "[colorop] color pipeline %u (owned by plane %u):\n",
		    pipeline->id, pipeline->plane->plane_id);

	wl_list_for_each(colorop, &pipeline->colorop_list, link) {
		type = drm_colorop_type_to_str(colorop);

		fprintf(fp, "%s[colorop] id %u, type %s, can bypass? %s",
			    sep, colorop->id, type, yesno(colorop->can_bypass));

		if (colorop->type == WDRM_COLOROP_TYPE_1D_CURVE) {
			curve_props = &colorop->props[WDRM_COLOROP_CURVE_1D];
			for (i = 0; i < curve_props->num_enum_values; i++) {
				if (curve_props->enum_values[i].valid)
					fprintf(fp, " [%s]",
						    curve_props->enum_values[i].name);
			}
		}

		fprintf(fp, "\n");
	}
}

/**
 * Populates the color pipelines of a DRM plane.
 *
 * This does nothing if the driver does not support color pipelines.
 *
 * @param plane The DRM plane whose pipelines this populates.
 * @param plane_props The DRM plane's props.
 */
void
drm_plane_populate_color_pipelines(struct drm_plane *plane,
				   drmModeObjectPropertiesPtr plane_props)
{
	struct weston_compositor *compositor = plane->base.compositor;
	struct drm_device *device = plane->device;
	struct drm_backend *b = device->backend;
	FILE *dbg = weston_log_scope_stream(b->debug);
	drmModePropertyRes *color_pipeline_props;
	uint32_t pipeline_i;
	unsigned int i;

	if (plane->props[WDRM_PLANE_COLOR_PIPELINE].prop_id == 0)
		return;

	color_pipeline_props =
		drmModeGetProperty(device->kms_device->fd,
				   plane->props[WDRM_PLANE_COLOR_PIPELINE].prop_id);
	if (!color_pipeline_props) {
		drm_debug(b, "failed to get color pipeline property for plane %u\n",
			     plane->plane_id);
		return;
	}

	plane->num_color_pipelines = 0;
	for (i = 0; (int)i < color_pipeline_props->count_enums; i++) {
		if (color_pipeline_props->enums[i].value != 0)
			plane->num_color_pipelines++;
	}
	plane->pipelines = xzalloc(plane->num_color_pipelines *
				   sizeof(*plane->pipelines));
	plane->pipeline_props_id = color_pipeline_props->prop_id;

	/* Populate pipelines. */
	pipeline_i = 0;
	for (i = 0; (int)i < color_pipeline_props->count_enums; i++) {
		struct drm_color_pipeline *pipeline;
		struct drm_colorop *colorop;
		uint32_t colorop_id, next_colorop_id;

		/* First colorop. */
		colorop_id = color_pipeline_props->enums[i].value;
		if (colorop_id == 0)
			continue;

		pipeline = &plane->pipelines[pipeline_i++];

		pipeline->plane = plane;
		wl_list_init(&pipeline->colorop_list);

		/* Id of the pipeline is the same of its first colorop. */
		pipeline->id = colorop_id;

		while (colorop_id != 0) {
			colorop = drm_colorop_create(pipeline, colorop_id, &next_colorop_id);
			if (!colorop) {
				drm_debug(b, "[colorop] failed to create colorop for id %u, destroying color pipelines for plane %u\n",
					     colorop_id, plane->plane_id);
				drm_plane_release_color_pipelines(plane);
				goto out;
			}
			colorop_id = next_colorop_id;
		}

		weston_assert_list_not_empty(compositor, &pipeline->colorop_list);
		if (dbg) {
			drm_color_pipeline_print(pipeline, dbg);
			fflush(dbg);
		}
	}
	weston_assert_u32_eq(b->compositor,
			     plane->num_color_pipelines, pipeline_i);

out:
	drmModeFreeProperty(color_pipeline_props);
}

/**
 * Release the color pipelines of a drm plane.
 *
 * @param plane The drm plane whose pipelines should be released.
 */
void
drm_plane_release_color_pipelines(struct drm_plane *plane)
{
	struct drm_color_pipeline *pipeline;
	struct drm_colorop *colorop, *tmp;
	unsigned int i;

	for (i = 0; i < plane->num_color_pipelines; i++) {
		pipeline = &plane->pipelines[i];
		wl_list_for_each_safe(colorop, tmp, &pipeline->colorop_list, link)
			drm_colorop_destroy(colorop);
	}

	plane->num_color_pipelines = 0;
	free(plane->pipelines);
	plane->pipelines = NULL;
}
