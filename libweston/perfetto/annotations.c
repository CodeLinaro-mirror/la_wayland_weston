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

#include <libweston/libweston.h>

#include "perfetto/annotations.h"
#include "weston-trace.h"

WL_EXPORT void
perfetto_annotate_int(struct weston_debug_annotations *annots,
		      const char *key,
		      int value)
{
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_INT_VAL;
	annot->ivalue = value;
	annot->key = key;

	annots->count++;
}

WL_EXPORT void
perfetto_annotate_float(struct weston_debug_annotations *annots,
			const char *key,
			float value)
{
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_FLOAT_VAL;
	annot->fvalue = value;
	annot->key = key;

	annots->count++;
}

WL_EXPORT void
perfetto_annotate_string(struct weston_debug_annotations *annots,
			 const char *key,
			 const char *value)
{
	struct weston_debug_annotation *annot = &annots->annots[annots->count];

	annot->type = WESTON_DEBUG_ANNOTATION_STR_VAL;
	annot->svalue = value;
	annot->key = key;

	annots->count++;
}
