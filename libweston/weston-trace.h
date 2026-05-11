/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

/* This code was taken from the Mesa project, and heavily modified to
 * suit weston's needs.
 */

#ifndef WESTON_TRACE_H
#define WESTON_TRACE_H

#include "perfetto/u_perfetto.h"
#include <string.h>

#if defined(HAVE_PERFETTO)

#if !defined(HAVE___BUILTIN_EXPECT)
#  define __builtin_expect(x, y) (x)
#endif

#ifndef likely
#  ifdef HAVE___BUILTIN_EXPECT
#    define likely(x)   __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x)   (x)
#    define unlikely(x) (x)
#  endif
#endif

/* maximum allowed debug annotations */
#define WESTON_MAX_DEBUG_ANNOTS      128

/* note that util_perfetto_is_tracing_enabled always returns false until
 * util_perfetto_init is called
 */
#define _WESTON_TRACE_BEGIN(name)                                             \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_begin(name);                      \
	} while (0)

#define _WESTON_TRACE_FLOW_BEGIN(name, id)                                    \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_begin_flow(name, id);             \
	} while (0)

#define _WESTON_TRACE_END()                                                   \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_end();                            \
	} while (0)

#define _WESTON_TRACE_SET_COUNTER(name, value)                                \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_counter_set(name, value);               \
	} while (0)

#define _WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp) \
	do {                                                                     \
		if (unlikely(util_perfetto_is_tracing_enabled()))                \
			util_perfetto_trace_full_begin(name, track_id, flow_id,  \
						       clock, timestamp);        \
	} while (0)

#define _WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)         \
	do {                                                                  \
		if (unlikely(util_perfetto_is_tracing_enabled()))             \
			util_perfetto_trace_full_end(name, track_id,          \
						     clock, timestamp);       \
	} while (0)

#define _WESTON_TRACE_BEGIN_ANNOTATION()                                        \
	struct weston_debug_annotation __pd_annot[WESTON_MAX_DEBUG_ANNOTS];     \
	struct weston_debug_annotations __pd_annots = {                         \
		.annots = __pd_annot,                                           \
		.count = 0,                                                     \
	}

#define _WESTON_TRACE_ANNOTATE_ADD_INT(k, v)                           \
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, __pd_annots.count);   \
	__pd_annots.annots[__pd_annots.count].type = WESTON_DEBUG_ANNOTATION_INT_VAL;    \
	__pd_annots.annots[__pd_annots.count].ivalue = v;                                \
	__pd_annots.annots[__pd_annots.count].key = k;                                   \
	__pd_annots.count++

#define _WESTON_TRACE_ANNOTATE_ADD_FLOAT(k, v)                         \
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, __pd_annots.count);   \
	__pd_annots.annots[__pd_annots.count].type = WESTON_DEBUG_ANNOTATION_FLOAT_VAL;  \
	__pd_annots.annots[__pd_annots.count].fvalue = v;                                \
	__pd_annots.annots[__pd_annots.count].key = k;                                   \
	__pd_annots.count++

#define _WESTON_TRACE_ANNOTATE_ADD_STR(k, v)                           \
	weston_assert_u8_gt(NULL, WESTON_MAX_DEBUG_ANNOTS, __pd_annots.count);   \
	__pd_annots.annots[__pd_annots.count].type = WESTON_DEBUG_ANNOTATION_STR_VAL;    \
	__pd_annots.annots[__pd_annots.count].svalue = v;                                \
	__pd_annots.annots[__pd_annots.count].key = k;                                   \
	__pd_annots.count++

#define _WESTON_TRACE_COMMIT_ANNOTATION(id, name)                                                       \
	do {                                                                                            \
		if (unlikely(util_perfetto_is_tracing_enabled())) {                                     \
			_weston_trace_scope_annotate_commit(id, name, &__pd_annots);                     \
		}                                                                                       \
	} while (0)

/* annotated funcs */
#define _WESTON_TRACE_ANNOTATE_FUNC_BEGIN(name, annots)                                                 \
	do {                                                                                            \
		if (unlikely(util_perfetto_is_tracing_enabled())) {                                     \
			util_perfetto_trace_commit_annotate_func(name, annots);                         \
		}                                                                                       \
	} while (0)

#define _WESTON_TRACE_ANNOTATE_FUNC_BEGIN_FLOW(name, id, annots)                                        \
	do {                                                                                            \
		if (unlikely(util_perfetto_is_tracing_enabled())) {                                     \
			util_perfetto_trace_commit_annotate_func_flow(id, name, annots);                \
		}                                                                                       \
	} while (0)

#if __has_attribute(cleanup) && __has_attribute(unused)

#define _WESTON_TRACE_SCOPE_VAR_CONCAT(name, suffix) name##suffix
#define _WESTON_TRACE_SCOPE_VAR(suffix)                                       \
	_WESTON_TRACE_SCOPE_VAR_CONCAT(_weston_trace_scope_, suffix)

/* This must expand to a single non-scoped statement for
 *
 *    if (cond)
 *       _WESTON_TRACE_SCOPE(...)
 *
 * to work.
 */
#define _WESTON_TRACE_SCOPE(name)                                             \
	int _WESTON_TRACE_SCOPE_VAR(__LINE__)                                 \
		__attribute__((cleanup(_weston_trace_scope_end), unused)) =   \
			_weston_trace_scope_begin(name)

#define _WESTON_TRACE_SCOPE_FLOW(name, id)                                    \
	int _WESTON_TRACE_SCOPE_VAR(__LINE__)                                 \
		__attribute__((cleanup(_weston_trace_scope_end), unused)) =   \
			_weston_trace_scope_flow_begin(name, id)

#define _WESTON_TRACE_ANNOTATE_FUNC(name)                                     \
	int _WESTON_TRACE_SCOPE_VAR(__LINE__)                                 \
		__attribute__((cleanup(_weston_trace_scope_end), unused)) =   \
			_weston_trace_annotate_func_begin(name, &__pd_annots)

#define _WESTON_TRACE_ANNOTATE_FUNC_FLOW(id, name)                            \
	int _WESTON_TRACE_SCOPE_VAR(__LINE__)                                 \
		__attribute__((cleanup(_weston_trace_scope_end), unused)) =   \
			_weston_trace_annotate_func_begin_flow(name, id, &__pd_annots)

static inline int
_weston_trace_scope_begin(const char *name)
{
	_WESTON_TRACE_BEGIN(name);
	return 0;
}

static inline int
_weston_trace_scope_flow_begin(const char *name, uint64_t *id)
{
	if (*id == 0)
		*id = util_perfetto_next_id();
	_WESTON_TRACE_FLOW_BEGIN(name, *id);
	return 0;
}

static inline void
_weston_trace_scope_annotate_commit(uint64_t *id, const char *name,
				    struct weston_debug_annotations *annots)
{
	if (id && *id == 0) {
		*id = util_perfetto_next_id();
		util_perfetto_trace_commit_debug_annots(*id, name, annots);
		goto reset_entries;
	}

	util_perfetto_trace_commit_debug_annots(0, name, annots);

reset_entries:
	annots->count = 0;
}

static inline int
_weston_trace_annotate_func_begin(const char *name,
				  struct weston_debug_annotations *annots)
{
	_WESTON_TRACE_ANNOTATE_FUNC_BEGIN(name, annots);

	annots->count = 0;
	return 0;
}

static inline int
_weston_trace_annotate_func_begin_flow(const char *name, uint64_t *id,
				       struct weston_debug_annotations *annots)
{
	if (*id == 0)
		*id = util_perfetto_next_id();

	_WESTON_TRACE_ANNOTATE_FUNC_BEGIN_FLOW(name, *id, annots);

	annots->count = 0;
	return 0;
}

static inline void
_weston_trace_scope_end(int *scope)
{
	_WESTON_TRACE_END();
}

#else

#define _WESTON_TRACE_SCOPE(name)

#endif /* __has_attribute(cleanup) && __has_attribute(unused) */

#else /* No perfetto, make these all do nothing */

#define _WESTON_TRACE_SCOPE(name)
#define _WESTON_TRACE_SCOPE_FLOW(name, id)
#define _WESTON_TRACE_FUNC()
#define _WESTON_TRACE_FUNC_FLOW(id)
#define _WESTON_TRACE_SET_COUNTER(name, value)
#define _WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define _WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)

#define _WESTON_TRACE_BEGIN_ANNOTATION()
#define _WESTON_TRACE_COMMIT_ANNOTATION(id, name)
#define _WESTON_TRACE_ANNOTATE_ADD_INT(k, v)
#define _WESTON_TRACE_ANNOTATE_ADD_FLOAT(k, v)
#define _WESTON_TRACE_ANNOTATE_ADD_STR(k, v)
#define _WESTON_TRACE_ANNOTATE_FUNC()
#define _WESTON_TRACE_ANNOTATE_FUNC_FLOW(id, name)

#endif /* HAVE_PERFETTO */

#define WESTON_TRACE_SCOPE(name) _WESTON_TRACE_SCOPE(name)
#define WESTON_TRACE_SCOPE_FLOW(name, id) _WESTON_TRACE_SCOPE_FLOW(name, id)
#define WESTON_TRACE_FUNC() _WESTON_TRACE_SCOPE(__func__)
#define WESTON_TRACE_FUNC_FLOW(id) _WESTON_TRACE_SCOPE_FLOW(__func__, id)
#define WESTON_TRACE_SET_COUNTER(name, value) _WESTON_TRACE_SET_COUNTER(name, value)
#define WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp) \
	_WESTON_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp) \
	_WESTON_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)

#define WESTON_TRACE_BEGIN_ANNOTATION() \
        _WESTON_TRACE_BEGIN_ANNOTATION()

#define WESTON_TRACE_ANNOTATE_ADD_INT(k, v) \
        _WESTON_TRACE_ANNOTATE_ADD_INT(k, v)

#define WESTON_TRACE_ANNOTATE_ADD_FLOAT(k, v) \
        _WESTON_TRACE_ANNOTATE_ADD_FLOAT(k, v)

#define WESTON_TRACE_ANNOTATE_ADD_STR(k, v) \
        _WESTON_TRACE_ANNOTATE_ADD_STR(k, v)

#define WESTON_TRACE_COMMIT_ANNOTATION(id) \
        _WESTON_TRACE_COMMIT_ANNOTATION(id, __func__)

#define WESTON_TRACE_ANNOTATE_FUNC() \
        _WESTON_TRACE_ANNOTATE_FUNC(__func__)

#define WESTON_TRACE_ANNOTATE_FUNC_FLOW(id) \
        _WESTON_TRACE_ANNOTATE_FUNC_FLOW(id, __func__)

#endif /* WESTON_TRACE_H */
