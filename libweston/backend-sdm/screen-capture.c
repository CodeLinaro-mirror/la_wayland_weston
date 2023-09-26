/*
*    Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
*
*    Redistribution and use in source and binary forms, with or without
*    modification, are permitted provided that the following conditions are
*    met:
*    * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.

*    THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
*    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
*    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
*    BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
*    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
*    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
*    IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*    Changes from Qualcomm Innovation Center are provided under the following license:
*    Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
*    SPDX-License-Identifier: BSD-3-Clause-Clear
*/


#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <libweston/libweston.h>
#include "gbm_priv.h"
#include "gbm-buffer-backend.h"
#include "linux-dmabuf.h"
#include "screen-capture.h"
#include "gbm-buffer-backend-server-protocol.h"
#include "screen-capture-server-protocol.h"
#include "sdm-internal.h"

static struct wl_resource *g_resource = NULL;

static void
screen_capture_draw_next(struct wl_client *client,
		struct wl_resource *resource);
static void
screen_capture_destroy_screen(struct wl_client *client,
		struct wl_resource *resource);

static void
screen_capture_destroy(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;
	struct drm_backend *b = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (screen_cap && screen_cap->bo) {
		screen_capture_destroy_screen(client, resource);
		b = (struct drm_backend *)screen_cap->compositor->backend;
		free(screen_cap);
		b->screen_cap = NULL;
	}

	wl_resource_set_user_data(resource, NULL);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy::Exited\n");
}

static void
screen_capture_create_screen(struct wl_client *client,
		struct wl_resource *resource,
		int32_t fd,
		int32_t meta_fd,
		uint32_t width,
		uint32_t height,
		uint32_t format,
		int32_t flags)
{
	struct screen_capture *screen_cap = NULL;
	struct drm_backend *b = NULL;
	struct gbm_buf_info gbmbuf_info = {
			.fd = fd,
			.metadata_fd = meta_fd,
			.width = width,
			.height = height,
			.format = format
		};

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_create_screen::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);

	if (!screen_cap) {
		wl_resource_post_error(resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		screen_capture_send_failed(resource);
		return;
	}

	b = (struct drm_backend *)screen_cap->compositor->backend;
	b->screen_cap = screen_cap;

	screen_cap->bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_GBM_BUF_TYPE,
			&gbmbuf_info, flags);
	screen_cap->width = width;
	screen_cap->height = height;

	drm_backend_update_virtual(b);
	screen_capture_send_created(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_create_screen::Exited\n");
}

static void
screen_capture_destroy_screen(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;
	struct drm_backend *b = NULL;
	struct weston_output *virtual_output = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy_screen::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		wl_resource_post_error(resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		screen_capture_send_destroyed(resource);
		return;
	}

	// trigger atomic complete.
	virtual_output = screen_cap->virtual_head->output;
	virtual_vblank(virtual_output);

	gbm_bo_destroy(screen_cap->bo);
	screen_cap->bo = NULL;

	b = (struct drm_backend *)screen_cap->compositor->backend;
	drm_backend_update_virtual(b);
	screen_capture_send_destroyed(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_destroy_screen::Exited\n");
}

static void
screen_capture_start(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_start::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		SC_PROTOCOL_LOG(SC_LOG_INFO,"screen_cap not inited!\n");
		wl_resource_post_error(resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		return;
	}

	screen_cap->enabled = true;
	screen_capture_draw_next(client, resource);
	screen_capture_send_started(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_start::Exited\n");
}

static void
screen_capture_stop(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_stop::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		SC_PROTOCOL_LOG(SC_LOG_INFO,"screen_cap not inited!\n");
		wl_resource_post_error(resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		return;
	}

	screen_cap->enabled = false;
	screen_capture_send_stopped(resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_stop::Exited\n");
}

static void
screen_capture_draw_next(struct wl_client *client,
		struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;
	struct weston_output *virtual_output = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_draw_next::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (!screen_cap) {
		SC_PROTOCOL_LOG(SC_LOG_INFO,"screen_cap not inited!\n");
		wl_resource_post_error(resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT, "screen capture is null!");
		return;
	}

	if (!screen_cap->virtual_head) {
		SC_PROTOCOL_LOG(SC_LOG_INFO,"virtual head not ready\n");
		return;
	}

	if (!screen_cap->virtual_head->output) {
		SC_PROTOCOL_LOG(SC_LOG_INFO,"virtual output not ready\n");
		return;
	}

	virtual_output = screen_cap->virtual_head->output;
	virtual_vblank(virtual_output);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_draw_next::Exited\n");
}

static const struct screen_capture_interface screen_capture_implementation = {
	screen_capture_destroy,
	screen_capture_create_screen,
	screen_capture_destroy_screen,
	screen_capture_start,
	screen_capture_stop,
	screen_capture_draw_next
};

static void
destroy_screen_capture(struct wl_resource *resource)
{
	struct screen_capture *screen_cap = NULL;
	struct drm_backend *b = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"destroy_screen_capture::Invoked\n");
	screen_cap = wl_resource_get_user_data(resource);
	if (screen_cap) {
		screen_capture_destroy(NULL, resource);
	}
	wl_resource_set_user_data(resource, NULL);
	g_resource = NULL;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"destroy_screen_capture::Exited\n");
}

static void screen_capture_init(struct screen_capture *sc, struct weston_compositor *compositor)
{
	sc->width = 0;
	sc->height = 0;
	sc->compositor = compositor;
	sc->enabled = false;
	sc->virtual_head = NULL;
	sc->bo = NULL;
}

static void
bind_screen_capture(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct weston_compositor *compositor = data;
	struct wl_resource *resource = NULL;
	struct screen_capture *sc = NULL;
	struct drm_backend *b = (struct drm_backend *)compositor->backend;

	SC_PROTOCOL_LOG(SC_LOG_DBG,"bind_screen_capture::Invoked\n");
	/* User needs to guarantee only one instance is running */
	if (b->screen_cap) {
		SC_PROTOCOL_LOG(SC_LOG_INFO,"Has already started screen capture!\n");
		wl_client_post_no_memory(client);
		return;
	}

	sc = zalloc(sizeof(struct screen_capture));
	if (sc == NULL) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"Create screen capture fail\n");
		wl_client_post_no_memory(client);
		return;
	}

	resource = wl_resource_create(client, &screen_capture_interface,
					version, id);
	if (resource == NULL) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"Create wl_resource fail\n");
		wl_client_post_no_memory(client);
		free(sc);
		return;
	}

	screen_capture_init(sc, compositor);
	g_resource = resource;

	wl_resource_set_implementation(resource,
			&screen_capture_implementation,
			sc, destroy_screen_capture);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"bind_screen_capture::Exited\n");
}

int
screen_capture_setup(struct weston_compositor *compositor)
{
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_setup::Invoked\n");

	if (!wl_global_create(compositor->wl_display,
			&screen_capture_interface, 1,
			compositor, bind_screen_capture)) {
		SC_PROTOCOL_LOG(SC_LOG_ERR,"screen_capture_setup::Failed\n");
		return -1;
	}

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_setup::Exited\n");
	return 0;
}

void
screen_capture_notify_committed(struct screen_capture *screen_cap)
{
	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_notify_committed::Invoked\n");
	if (!screen_cap) {
		SC_PROTOCOL_LOG(SC_LOG_DBG, "screen_cap not initialized.\n");
		return;
	}

	if (!screen_cap->enabled) {
		SC_PROTOCOL_LOG(SC_LOG_INFO, "screen_cap not enabled.\n");
		return;
	}

	if (!g_resource) {
		SC_PROTOCOL_LOG(SC_LOG_INFO, "resource not ready.\n");
		return;
	}

	screen_capture_send_buffer_committed(g_resource);

	SC_PROTOCOL_LOG(SC_LOG_DBG,"screen_capture_notify_committed::Exited\n");
}