/*
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates
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
#include <libweston/libweston.h>

#include "libweston/pixel-formats.h"
#include "perfetto/annotations.h"
#include "shared/weston-assert.h"
#include "weston-trace.h"

static void
do_annotate_buffer(struct weston_debug_annotations *annots,
		   unsigned char parent,
		   const char *key,
		   unsigned char key_size,
		   const struct weston_buffer *buffer);

static void
do_annotate_int(struct weston_debug_annotations *annots,
		unsigned char parent,
		const char *key,
		unsigned char key_size,
		int value)
{
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, annots->count);
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_INT_VAL;
	annot->ivalue = value;
	annot->parent = parent;
	annot->key = key;
	annot->key_size = key_size;

	annots->count++;
}

WL_EXPORT void
perfetto_annotate_int(struct weston_debug_annotations *annots,
		      const char *key,
		      unsigned char key_size,
		      int value)
{
	do_annotate_int(annots, annots->count, key, key_size, value);
}

static void
do_annotate_float(struct weston_debug_annotations *annots,
		  unsigned char parent,
		  const char *key,
		  unsigned char key_size,
		  float value)
{
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, annots->count);
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_FLOAT_VAL;
	annot->fvalue = value;
	annot->parent = parent;
	annot->key = key;
	annot->key_size = key_size;

	annots->count++;
}

WL_EXPORT void
perfetto_annotate_float(struct weston_debug_annotations *annots,
			const char *key,
			unsigned char key_size,
			float value)
{
	do_annotate_float(annots, annots->count, key, key_size, value);
}

static void
do_annotate_string(struct weston_debug_annotations *annots,
		   unsigned char parent,
		   const char *key,
		   unsigned char key_size,
		   const char *value)
{
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, annots->count);
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_STR_VAL;
	annot->svalue = value;
	annot->parent = parent;
	annot->key = key;
	annot->key_size = key_size;

	annots->count++;
}

WL_EXPORT void
perfetto_annotate_string(struct weston_debug_annotations *annots,
			 const char *key,
			 unsigned char key_size,
			 const char *value)
{
	do_annotate_string(annots, annots->count, key, key_size, value);
}

static unsigned char
create_container(struct weston_debug_annotations *annots,
		 unsigned char parent,
		 const char *key,
		 unsigned char key_size)
{
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, annots->count);
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_CONTAINER;
	annot->key = key;
	annot->key_size = key_size;
	annot->parent = parent;

	return annots->count++;
}

#define ADD(annots, parent, key, value)                                              \
	do {                                                                         \
		static_assert(sizeof(key) < WESTON_TRACE_MAX_KEY_LENGTH);            \
		_Generic((value),                                                    \
			char *: do_annotate_string,                                  \
			const char *: do_annotate_string,                            \
			int: do_annotate_int,                                        \
			float: do_annotate_float,                                    \
			struct weston_buffer *:do_annotate_buffer,                   \
			const struct weston_buffer *:do_annotate_buffer              \
		) (annots, parent, key, sizeof(key), value);                         \
	} while (0)

static void
do_annotate_buffer(struct weston_debug_annotations *annots,
		   unsigned char parent,
		   const char *key,
		   unsigned char key_size,
		   const struct weston_buffer *buffer)
{
	unsigned char container_id = create_container(annots, parent, key, key_size);

	ADD(annots, container_id, "format", buffer->pixel_format->drm_format_name);
	ADD(annots, container_id, "modifier", buffer->format_modifier_name);
	ADD(annots, container_id, "width", buffer->width);
	ADD(annots, container_id, "height", buffer->height);
}

WL_EXPORT void
perfetto_annotate_buffer(struct weston_debug_annotations *annots,
			 const char *key,
			 unsigned char key_size,
			 const struct weston_buffer *buffer)
{
	if (!buffer)
		perfetto_annotate_string(annots, key, key_size, "None");

	do_annotate_buffer(annots, annots->count, key, key_size, buffer);
}
