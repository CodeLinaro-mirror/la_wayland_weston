/*
 * Copyright © 2021 The Linux Foundation. All rights reserved.
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
 * Copyright © 2014 Collabora Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "xdg-shell-client-protocol.h"
#include <sys/types.h>
#include <unistd.h>

#include "shared/helpers.h"
#include "shared/platform.h"
#include "shared/weston-egl-ext.h"

#include "shared/xalloc.h"
#include <libweston/zalloc.h>
#include "xdg-output-unstable-v1-client-protocol.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define MAX_PACKET_SIZE 256
#define CFG_SIZE        44
#define PORT      8845

#ifndef MIN
#  define MIN(a, b) ((a < b) ? a : b)
#endif

static int running = 1;
static bool repaint_once = false;
int32_t listener_handler_;
int32_t socket_handler_;
pthread_t thread_;
int32_t sig_flag_;

struct window;

typedef struct {
	GLfloat r;
	GLfloat g;
	GLfloat b;
	GLfloat bg_r;
	GLfloat bg_g;
	GLfloat bg_b;
	GLint x;
	GLint y;
	GLint w;
	GLint h;
	GLint depth;
} TPGConfig;

struct WideColorModeConfig {
	EGLint  space_;
	EGLint  r_, g_, b_, a_;
};

enum IndexRGB
{
	SRGB_TOSRGB = 0,
	SRGB_TO_DISPLAYP3,
	SRGB_TO_DCIP3
};

static const float RGB2RGB_TRANSFORM[2][3][3] =
{
	{ // sRGB to sRGB(0)
		{  1.0F,   0.0F,   0.0F },
		{  0.0F,   1.0F,   0.0F },
		{  0.0F,   0.0F,   1.0F }
	},
	{ // sRGB to DisplayP3 (1)
		{  0.8225F,   0.1775F,   0.0F },
		{  0.0332F,   0.9668F,   -0.0F },
		{  0.0171F,   0.0724F,   0.9105F }
	},
	{ // sRGB to DCI_P3 (2)
		{  0.8686F,   0.1289F,   0.0025F },
		{  0.0345F,   0.9618F,  -0.0036F },
		{  0.0168F,   0.0710F,   0.9121F }
	}
};

struct output_info {
	struct wl_output *output;
	struct wl_list global_link;

	int32_t version;

	struct {
		int32_t x, y;
		int32_t scale;
		int32_t physical_width, physical_height;
		enum wl_output_subpixel subpixel;
		enum wl_output_transform output_transform;
		char *make;
		char *model;
	} geometry;

	struct wl_list modes;
};

struct xdg_output_v1_info {
	struct wl_list link;

	struct zxdg_output_v1 *xdg_output;
	struct output_info *output;

	struct {
		int32_t x, y;
		int32_t width, height;
	} logical;

	char *name, *description;
};

struct xdg_output_manager_v1_info {
	struct zxdg_output_manager_v1 *manager;
	struct weston_info *info;
	struct wl_list outputs;
};


struct output {
	struct wl_output *wl_output;
	int x, y;
	int width, height;
	int physical_width, physical_height;
	const char *make, *model, *name;
	int transform;
	int subpixel;
	int scale;
	int refresh;
	bool initialized;
};

struct weston_info {
	struct wl_display *display;
	struct wl_registry *registry;

	bool roundtrip_needed;
	/* required for xdg-output-unstable-v1 */
	struct wl_list outputs;
	struct xdg_output_manager_v1_info *xdg_output_manager_v1_info;
};

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct {
		EGLDisplay dpy;
		EGLContext ctx;
		EGLConfig conf;
	} egl;
	struct window *window;
	struct output output;
	struct weston_info info;

	PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	GLint egl_width, egl_height;
	struct {
		GLuint rotation_uniform;
		GLuint pos;
		GLuint col;
		GLuint program;
	} gl;

	struct wl_egl_window *native;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	EGLSurface egl_surface;
	struct wl_callback *callback;
	int fullscreen, maximized, buffer_size, delay;
	bool wait_for_configure;
	TPGConfig *cfg;
	int index_rgb;
	const char *output_name;
	struct wl_output *fs_output;
};

static const char *gVertexShader =
        "attribute vec4 vPosition;\n"
        "attribute vec4 vColor;\n"
        "varying vec4 shapeColor;\n"
        "void main()\n"
        "{\n"
        "  gl_Position = vPosition;\n"
        "  shapeColor = vColor;\n"
        "}\n";

static const char *gFragmentShader =
        "precision mediump float;\n"
        "varying vec4 shapeColor;\n"
        "void main()\n"
        "{\n"
        "  gl_FragColor = shapeColor;\n"
        "}\n";

enum ParseRetrunValue {
	ErrorNone = 0,
	InvalidFile,
	InvalidParameter,
};

enum ParseConfigState{
	NoneChanged = 0,
	ColorChanged = 1 << 0,
	GeometryChanged = 1 << 1
};

static bool is_partial_solid_fill_enabled(struct window *window) {
	// check if width and height match config and != 0
	return (window->cfg->w != 0 && window->cfg->h != 0
			&& window->cfg->w < window->egl_width
			&& window->cfg->h < window->egl_height);
}

void set_config(struct display *display, TPGConfig *cfg) {
	memcpy(display->window->cfg, cfg, sizeof(TPGConfig));
}

void start_repaint() {
	repaint_once = true;
}

bool need_repaint() {
	return repaint_once;
}

void repaint_done() {
	int32_t ack = 0;
	int32_t ret = -1;
	ret = send(socket_handler_, &ack, sizeof(ack), 0);
	if (ret < 0) {
		fprintf(stderr, "ERROR writing to socket");
	}
	repaint_once = false;
}

int32_t connect_to_socket() {
	struct sockaddr_in address;

	listener_handler_ = -1;
	socket_handler_ = -1;
	sig_flag_ = 0;

	/* Connect to Socket */
	listener_handler_ = socket(AF_INET, SOCK_STREAM, 0);
	if (listener_handler_  < 0) {
		fprintf(stderr, "Unable to create socket: errno:%d\n", errno);
		return -1;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr("127.0.0.1");
	address.sin_port = htons(PORT);

	if (bind(listener_handler_, (struct sockaddr*)&address, sizeof(address)) < 0) {
		fprintf(stderr, "Socket bind failed\n");
		return -1;
	}

	/* Create listener for opened socket */
	printf("waiting for incoming connection...\n");
	if ( listen(listener_handler_, 1) < 0) {
		fprintf(stderr, "socket listen fail\n");
		return -1;
	}
	return 0;
}

void * command_thread_handler(struct display *display) {
	/* Detach thread */
	pthread_detach(pthread_self());
	if (socket_handler_ < 0) {
		fprintf(stderr, "no valid socket handler\n");
		return NULL;
	}

	char buffer[MAX_PACKET_SIZE];
	while (!sig_flag_){
		fd_set read_fd;
		FD_ZERO(&read_fd);
		FD_SET(socket_handler_, &read_fd);

		/* assign time out value */
		struct timeval to;
		to.tv_sec = 3600;
		to.tv_usec = 0;

		/* wait for incoming connection */
		int32_t len = select(socket_handler_ + 1, &read_fd, NULL, NULL, &to);
		if (len < 0){
			fprintf(stderr, "select() failed");
			break;
		}

		// Ensure socket is set to blocking
		int flags = fcntl(socket_handler_, F_GETFL, 0);
		if (flags < 0) {
		fprintf(stderr, "Unable to read flags. Setting flag to blocking");
		} else {
		flags = (flags & ~O_NONBLOCK);
		if (fcntl(socket_handler_, F_SETFL, flags) != 0)
			fprintf(stderr, "unable to set flags to blocking");
		}

		if (FD_ISSET(socket_handler_, &read_fd)){
		memset(buffer, 0, MAX_PACKET_SIZE);
		int32_t bytes_read = recv(socket_handler_, buffer, MAX_PACKET_SIZE, 0);
		printf("Bytes read from socket: %d\n", bytes_read);
		if (bytes_read != CFG_SIZE) {
			printf("Packet size mismatch: Expected %d but received %d\n",
			                                       CFG_SIZE, bytes_read);
			break;
		}
		if (bytes_read > 0) {
			// Parse buffer
			TPGConfig cfg;
			memcpy(&cfg, buffer, CFG_SIZE);

			// Normalize coordinates based on bit depth
			cfg.r = (cfg.depth == 8 ?
			        (GLfloat) (cfg.r / 255.0) : (GLfloat) (cfg.r / 1023.0));
			cfg.g = (cfg.depth == 8 ?
			        (GLfloat) (cfg.g / 255.0) : (GLfloat) (cfg.g / 1023.0));
			cfg.b = (cfg.depth == 8 ?
			        (GLfloat) (cfg.b / 255.0) : (GLfloat) (cfg.b / 1023.0));
			cfg.bg_r = (cfg.depth == 8 ?
			           (GLfloat) (cfg.bg_r / 255.0) : (GLfloat) (cfg.bg_r / 1023.0));
			cfg.bg_g = (cfg.depth == 8 ?
			           (GLfloat) (cfg.bg_g / 255.0) : (GLfloat) (cfg.bg_g / 1023.0));
			cfg.bg_b = (cfg.depth == 8 ?
			           (GLfloat) (cfg.bg_b / 255.0) : (GLfloat) (cfg.bg_b / 1023.0));

			printf("r:%f g:%f b:%f, bg_r:%f bg_g:%f bg_b:%f, \
					x:%d, y:%d, w:%d, h:%d, bit_depth:%d\n",
					cfg.r, cfg.g, cfg.b, cfg.bg_r, cfg.bg_g, cfg.bg_b,
					cfg.x, cfg.y, cfg.w, cfg.h, cfg.depth);

			set_config(display, &cfg);
			start_repaint();
		} else if (bytes_read == 0) {
			/* Unexpected socket closing */
			break;
		}
		} else {
		fprintf(stderr, "sockets handler not set");
		break;
		}
	}
	printf("closing client handler\n");
	close(socket_handler_);
	socket_handler_ = -1;

	return NULL;
}

void * command_start_listener(struct display *display) {
	printf("start command listener!\n");
	/* Waiting for incoming connection */
	while(!sig_flag_){
		struct sockaddr addr;
		socklen_t alen = sizeof(addr);

		int32_t handler = accept(listener_handler_, (struct sockaddr *)&addr, &alen);
		if (handler > 0) {
			/* Create handler thread for this connection */
			pthread_t thread;
			while (socket_handler_ != -1) {
				printf("Copying data...\n");
				sleep(1);
			}

			socket_handler_ = handler;
			if (pthread_create(&thread, NULL, command_thread_handler, display)) {
				fprintf(stderr, "Failed to create thread handler\n");
				break;
			}
			sched_yield();
		} else {
			fprintf(stderr, "Failed to accept incoming connection\n");
			break;
		}
	}
	printf("Stopping listener\n");
	close(listener_handler_);
	return NULL;
}

void command_start(struct display *display) {

  /* Connect to socket*/
  int32_t ret = connect_to_socket();
  if (ret < 0) {
    fprintf(stderr, "Failed to connect to socket\n");
  }

  /* Start the listener thread to handling incoming connections */
  ret = pthread_create(&thread_, NULL, command_start_listener, display);
  if (ret < 0) {
    fprintf(stderr,"Failed to initialize listener thread\n");
  }
}

static void
output_handle_geometry(void *data, struct wl_output *wl_output, int x, int y,
		       int physical_width, int physical_height, int subpixel,
		       const char *make, const char *model, int32_t transform)
{
	struct output *output = data;

	output->x = x;
	output->y = y;
	output->physical_width = physical_width;
	output->physical_height = physical_height;
	output->subpixel = subpixel;
	output->make = make;
	output->model = model;
	output->transform = transform;
}

static void
output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
		   int width, int height, int refresh)
{
	struct output *output = data;

	if (flags & WL_OUTPUT_MODE_CURRENT) {
		output->width = width;
		output->height = height;
		output->refresh = refresh;
	}
}

static void
output_handle_scale(void *data, struct wl_output *wl_output, int scale)
{
	struct output *output = data;

	output->scale = scale;
}

static void
output_handle_done(void *data, struct wl_output *wl_output)
{
	struct output *output = data;
	output->initialized = true;
}

static void
output_handle_name(void *data, struct wl_output *wl_output, const char *name)
{
	struct output *output = data;
	output->name = name;
}

static void
handle_xdg_output_v1_logical_position(void *data, struct zxdg_output_v1 *output,
                                      int32_t x, int32_t y)
{
	struct xdg_output_v1_info *xdg_output = data;
	xdg_output->logical.x = x;
	xdg_output->logical.y = y;
}

static void
handle_xdg_output_v1_logical_size(void *data, struct zxdg_output_v1 *output,
                                      int32_t width, int32_t height)
{
	struct xdg_output_v1_info *xdg_output = data;
	xdg_output->logical.width = width;
	xdg_output->logical.height = height;
}

static void
handle_xdg_output_v1_done(void *data, struct zxdg_output_v1 *output)
{
	/* Don't bother waiting for this; there's no good reason a
	 * compositor will wait more than one roundtrip before sending
	 * these initial events. */
}

static void
handle_xdg_output_v1_name(void *data, struct zxdg_output_v1 *output,
                          const char *name)
{
	struct xdg_output_v1_info *xdg_output = data;
	xdg_output->name = strdup(name);
}

static void
handle_xdg_output_v1_description(void *data, struct zxdg_output_v1 *output,
                          const char *description)
{
	struct xdg_output_v1_info *xdg_output = data;
	xdg_output->description = strdup(description);
}

static const struct wl_output_listener output_listener = {
	output_handle_geometry,
	output_handle_mode,
	output_handle_done,
	output_handle_scale,
	output_handle_name, // None
};

static const struct zxdg_output_v1_listener xdg_output_v1_listener = {
	.logical_position = handle_xdg_output_v1_logical_position,
	.logical_size = handle_xdg_output_v1_logical_size,
	.done = handle_xdg_output_v1_done,
	.name = handle_xdg_output_v1_name,
	.description = handle_xdg_output_v1_description,
};

static void
add_xdg_output_v1_info(struct xdg_output_manager_v1_info *manager_info,
                       struct output_info *output)
{
	struct xdg_output_v1_info *xdg_output = xzalloc(sizeof *xdg_output);

	wl_list_insert(&manager_info->outputs, &xdg_output->link);
	xdg_output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
		manager_info->manager, output->output);
	zxdg_output_v1_add_listener(xdg_output->xdg_output,
		&xdg_output_v1_listener, xdg_output);

	xdg_output->output = output;

	manager_info->info->roundtrip_needed = true;
}

static void
add_output_info(struct weston_info *info, uint32_t id, uint32_t version)
{
	struct output_info *output = xzalloc(sizeof *output);

	output->version = MIN(version, 2);
	output->geometry.scale = 1;
	wl_list_init(&output->modes);

	output->output = wl_registry_bind(info->registry, id,
					  &wl_output_interface, output->version);
	wl_output_add_listener(output->output, &output_listener,
			       output);

	wl_list_insert(&info->outputs, &output->global_link);

	if (info->xdg_output_manager_v1_info)
		add_xdg_output_v1_info(info->xdg_output_manager_v1_info,
				       output);
}

static void
destroy_xdg_output_v1_info(struct xdg_output_v1_info *info)
{
	wl_list_remove(&info->link);
	zxdg_output_v1_destroy(info->xdg_output);
	free(info->name);
	free(info->description);
	free(info);
}

static void
print_xdg_output_v1_info(const struct xdg_output_v1_info *info)
{
	printf("\txdg_output_v1\n");
	if (info->name)
		printf("\t\tname: '%s'\n", info->name);
	if (info->description)
		printf("\t\tdescription: '%s'\n", info->description);
	printf("\t\tlogical_x: %d, logical_y: %d\n",
		info->logical.x, info->logical.y);
	printf("\t\tlogical_width: %d, logical_height: %d\n",
		info->logical.width, info->logical.height);
}

static void
print_xdg_output_manager_v1_info(void *data)
{
	struct xdg_output_manager_v1_info *info = data;
	struct xdg_output_v1_info *output;

	wl_list_for_each(output, &info->outputs, link) {
		print_xdg_output_v1_info(output);
	}
}

static struct xdg_output_v1_info*
get_xdg_output_info(void *data, const char* name)
{
	struct xdg_output_manager_v1_info *info = data;
	struct xdg_output_v1_info *output;

	wl_list_for_each(output, &info->outputs, link) {
		if (name == NULL && output->name) {
			printf("Use default output - %s \n", output->name);
			return output;
		}
		if (output->name && (strcmp(output->name, name) == 0)) {
			printf("found output name:%s \n", output->name);
			return output;
		}
	}

	printf("cannot found output(%s) !\n", name);
	return NULL;
}

static void
add_xdg_output_manager_v1_info(struct weston_info *info, uint32_t id,
                               uint32_t version)
{
	struct output_info *output;
	struct xdg_output_manager_v1_info *manager = xzalloc(sizeof *manager);

	wl_list_init(&manager->outputs);
	manager->info = info;

	manager->manager = wl_registry_bind(info->registry, id,
		&zxdg_output_manager_v1_interface, version > 2 ? 2 : version);

	wl_list_for_each(output, &info->outputs, global_link)
		add_xdg_output_v1_info(manager, output);

	info->xdg_output_manager_v1_info = manager;
}

static void
destroy_xdg_output_manager_v1_info(void *data)
{
	struct xdg_output_manager_v1_info *info = data;
	struct xdg_output_v1_info *output, *tmp;

	zxdg_output_manager_v1_destroy(info->manager);

	wl_list_for_each_safe(output, tmp, &info->outputs, link)
		destroy_xdg_output_v1_info(output);
}

static void choose_output(struct display *display, const char *output_name)
{
	/* Get all wl_output info and do fullscreen */
	do {
		display->info.roundtrip_needed = false;
		wl_display_roundtrip(display->info.display);
	} while (display->info.roundtrip_needed);
	print_xdg_output_manager_v1_info(display->info.xdg_output_manager_v1_info);

	if (display->window->fullscreen == true) {
		struct xdg_output_v1_info *output;
		output = get_xdg_output_info(
					display->info.xdg_output_manager_v1_info,
					output_name);
		display->window->fs_output = output->output->output;
		display->window->egl_width = output->logical.width;
		display->window->egl_height = output->logical.height;
		xdg_toplevel_set_fullscreen(display->window->xdg_toplevel,
					display->window->fs_output);
		xdg_toplevel_set_position(display->window->xdg_toplevel,
					output->logical.x, 0);

		wl_display_roundtrip(display->info.display);
	}
}

static void
init_egl(struct display *display, struct window *window)
{
	static const struct {
		char *extension, *entrypoint;
	} swap_damage_ext_to_entrypoint[] = {
		{
			.extension = "EGL_EXT_swap_buffers_with_damage",
			.entrypoint = "eglSwapBuffersWithDamageEXT",
		},
		{
			.extension = "EGL_KHR_swap_buffers_with_damage",
			.entrypoint = "eglSwapBuffersWithDamageKHR",
		},
	};

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	const char *extensions;

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};

	EGLint major, minor, n, count, i;
	EGLConfig *configs;
	EGLBoolean ret;

	display->egl.dpy =
		weston_platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR,
						display->display, NULL);
	assert(display->egl.dpy);

	ret = eglInitialize(display->egl.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	if (!eglGetConfigs(display->egl.dpy, NULL, 0, &count) || count < 1)
		assert(0);

	configs = calloc(count, sizeof *configs);
	assert(configs);

	ret = eglChooseConfig(display->egl.dpy, config_attribs,
					configs, count, &n);
	assert(ret && n >= 1);

	for (i = 0; i < n; i++) {
		EGLint buffer_size, red_size;
		eglGetConfigAttrib(display->egl.dpy,
				   configs[i], EGL_BUFFER_SIZE, &buffer_size);
		eglGetConfigAttrib(display->egl.dpy,
				   configs[i], EGL_RED_SIZE, &red_size);
		if ((window->buffer_size == 0 ||
			 window->buffer_size == buffer_size) && red_size < 10) {
			display->egl.conf = configs[i];
			break;
		}
	}
	free(configs);
	if (display->egl.conf == NULL) {
		fprintf(stderr, "did not find config with buffer size %d\n",
			window->buffer_size);
		exit(EXIT_FAILURE);
	}

	display->egl.ctx = eglCreateContext(display->egl.dpy,
					    display->egl.conf,
					    EGL_NO_CONTEXT, context_attribs);
	assert(display->egl.ctx);

	display->swap_buffers_with_damage = NULL;
	extensions = eglQueryString(display->egl.dpy, EGL_EXTENSIONS);
	if (extensions &&
	    weston_check_egl_extension(extensions, "EGL_EXT_buffer_age")) {
		for (i = 0; i < (int) ARRAY_LENGTH(swap_damage_ext_to_entrypoint); i++) {
			if (weston_check_egl_extension(extensions,
					swap_damage_ext_to_entrypoint[i].extension)) {
				/* The EXTPROC is identical to the KHR one */
				display->swap_buffers_with_damage =
					(PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)
					eglGetProcAddress(
						swap_damage_ext_to_entrypoint[i].entrypoint);
				break;
			}
		}
	}

	if (display->swap_buffers_with_damage)
		printf("has EGL_EXT_buffer_age and %s\n",
				swap_damage_ext_to_entrypoint[i].extension);

}

static void
fini_egl(struct display *display)
{
	eglTerminate(display->egl.dpy);
	eglReleaseThread();
}

static GLuint
create_shader(struct window *window, const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	assert(shader != 0);

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %.*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		exit(1);
	}

	return shader;
}

static void
init_gl(struct window *window)
{
	GLuint frag, vert;
	GLuint program;
	GLint status;
	EGLBoolean ret;

	printf("egl width-%d, height-%d\n", window->egl_width, window->egl_height);

	window->native = wl_egl_window_create(window->surface,
						  window->egl_width,
						  window->egl_height);
	window->egl_surface =
		weston_platform_create_egl_surface(window->display->egl.dpy,
						   window->display->egl.conf,
						   window->native, NULL);

	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
				 window->egl_surface, window->display->egl.ctx);
	assert(ret == EGL_TRUE);

	frag = create_shader(window, gFragmentShader, GL_FRAGMENT_SHADER);
	vert = create_shader(window, gVertexShader, GL_VERTEX_SHADER);

	program = glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%.*s\n", len, log);
		exit(1);
	}

	glUseProgram(program);

	window->gl.pos = 0;
	window->gl.col = 1;
	window->gl.program = program;

	glBindAttribLocation(program, window->gl.pos, "vPosition");
	glBindAttribLocation(program, window->gl.col, "vColor");
	glLinkProgram(program);
}

static void
handle_surface_configure(void *data, struct xdg_surface *surface,
			 uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(surface, serial);

	window->wait_for_configure = false;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_surface_configure
};

static void
handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
			  int32_t width, int32_t height,
			  struct wl_array *states)
{
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
};

static void
create_surface(struct window *window)
{
	struct display *display = window->display;

	window->surface = wl_compositor_create_surface(display->compositor);

	window->xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base,
							window->surface);
	xdg_surface_add_listener(window->xdg_surface,
				 &xdg_surface_listener, window);

	window->xdg_toplevel =
		xdg_surface_get_toplevel(window->xdg_surface);
	xdg_toplevel_add_listener(window->xdg_toplevel,
				&xdg_toplevel_listener, window);

	xdg_toplevel_set_title(window->xdg_toplevel, "qdcm-solid-color");
	xdg_toplevel_set_app_id(window->xdg_toplevel,
			"qdcm.weston.solid.color.app");

	if (window->maximized)
		xdg_toplevel_set_maximized(window->xdg_toplevel);

	window->wait_for_configure = true;
	wl_surface_commit(window->surface);
}

static void
destroy_surface(struct window *window)
{
	/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
	 * on eglReleaseThread(). */
	eglMakeCurrent(window->display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
				EGL_NO_CONTEXT);

	weston_platform_destroy_egl_surface(window->display->egl.dpy,
						window->egl_surface);
	wl_egl_window_destroy(window->native);

	if (window->xdg_toplevel)
		xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		xdg_surface_destroy(window->xdg_surface);
	wl_surface_destroy(window->surface);

	if (window->callback)
		wl_callback_destroy(window->callback);
}

static void rgb2rgb_conv(float *pIn, enum IndexRGB conv_ind)
{
	const float (*const pMtx)[3] = RGB2RGB_TRANSFORM[conv_ind];
	float in0 = pIn[0];
	float in1 = pIn[1];
	float in2 = pIn[2];

	pIn[0] = pMtx[0][0] * in0 + pMtx[0][1] * in1 + pMtx[0][2] * in2;
	pIn[1] = pMtx[1][0] * in0 + pMtx[1][1] * in1 + pMtx[1][2] * in2;
	pIn[2] = pMtx[2][0] * in0 + pMtx[2][1] * in1 + pMtx[2][2] * in2;
}

static void GetOGLCoordinates(struct window *window,
			int x, int y, float *posX, float *posY) {
	*posX = (2.0f / (float)window->egl_width * (float)x) - 1.0f;
	// Y needs to be inverse because screen coordinates (0,0) start at
	// top left, where as, OpenGL (0,0) NDC coordinates start at bottom left.
	*posY = 1.0f - (2.0f / (float)window->egl_height * (float)y);
}

static void SetObjLayerVertices(struct window *window,
			GLfloat *vertices_,
			float x1, float y1, float x2, float y2) {
	float posX = 0, posY = 0;
	// First Triangle
	GetOGLCoordinates(window, x1, y1, &posX, &posY);
	vertices_[0] = posX;
	vertices_[1] = posY;
	GetOGLCoordinates(window, x1, y2, &posX, &posY);
	vertices_[2] = posX;
	vertices_[3] = posY;
	GetOGLCoordinates(window, x2, y2, &posX, &posY);
	vertices_[4] = posX;
	vertices_[5] = posY;
	// Second Triangle
	GetOGLCoordinates(window, x1, y1, &posX, &posY);
	vertices_[6] = posX;
	vertices_[7] = posY;
	GetOGLCoordinates(window, x2, y2, &posX, &posY);
	vertices_[8] = posX;
	vertices_[9] = posY;
	GetOGLCoordinates(window, x2, y1, &posX, &posY);
	vertices_[10] = posX;
	vertices_[11] = posY;
}

static void
redraw(void *data, struct wl_callback *callback, bool first_cycle)
{
	struct window *window = data;
	struct display *display = window->display;
	struct wl_region *region;
	EGLint rect[4];
	EGLint buffer_age = 0;

	assert(window->callback == callback);
	window->callback = NULL;

	if (callback)
		wl_callback_destroy(callback);

	if (display->swap_buffers_with_damage)
		eglQuerySurface(display->egl.dpy, window->egl_surface,
				EGL_BUFFER_AGE_EXT, &buffer_age);

	if (first_cycle) {
		glViewport(0, 0, window->egl_width, window->egl_height);
		glClearColor(0.5, 0.5, 0.5, 0.5);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	} else {
		if (is_partial_solid_fill_enabled(window)) {
			// Set background color
			float background_color[3] = {
					window->cfg->bg_r,
					window->cfg->bg_g,
					window->cfg->bg_b};
			glViewport(0, 0, window->egl_width, window->egl_height);
			rgb2rgb_conv(background_color, window->index_rgb);
			glClearColor((GLfloat)background_color[0],
						(GLfloat)background_color[1],
						(GLfloat)background_color[2],
						1.0);
			glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

			// Set foreground color
			float vertices_[12] = {
				// first triangle
				-0.5,  0.5,
				-0.5, -0.5,
				0.5, -0.5,
				// second triangle
				-0.5,  0.5,
				0.5, -0.5,
				0.5,  0.5
			};

			GLfloat verticesColors_[24] = {
				1.0f, 0.0f, 0.0f, 1.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
			};

			// SetObjLayerColor
			float foreground_color[3] = {
				window->cfg->r,
				window->cfg->g,
				window->cfg->b};
			rgb2rgb_conv(foreground_color, window->index_rgb);
			for (int i = 0; i < 24; i += 4) {
				verticesColors_[i]   = (GLfloat) (foreground_color[0]);
				verticesColors_[i+1] = (GLfloat) (foreground_color[1]);
				verticesColors_[i+2] = (GLfloat) (foreground_color[2]);
			}

			// SetObjLayerVertices
			SetObjLayerVertices(window, vertices_,
					(float)window->cfg->x, (float)window->cfg->y,
					(float)(window->cfg->x + window->cfg->w),
					(float)(window->cfg->y + window->cfg->h));

			// Draw foreground
			glUseProgram(window->gl.program);
			glVertexAttribPointer(window->gl.pos, 2,
					GL_FLOAT, GL_FALSE, 0 ,vertices_);
			glEnableVertexAttribArray(window->gl.pos);
			glVertexAttribPointer(window->gl.col, 4,
					GL_FLOAT, GL_FALSE, 0, verticesColors_);
			glEnableVertexAttribArray(window->gl.col);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		} else {
			// Set foreground color only
			float foreground_color[3] = {
					window->cfg->r,
					window->cfg->g,
					window->cfg->b};
			glViewport(0, 0, window->egl_width, window->egl_width);
			rgb2rgb_conv(foreground_color, window->index_rgb);
			glClearColor((GLfloat)foreground_color[0],
						 (GLfloat)foreground_color[1],
						 (GLfloat)foreground_color[2],
						 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
	}

	usleep(window->delay);

	if (window->fullscreen) {
		region = wl_compositor_create_region(window->display->compositor);
		wl_region_add(region, 0, 0,
				window->egl_width,
				window->egl_height);
		wl_surface_set_opaque_region(window->surface, region);
		wl_region_destroy(region);
	} else {
		wl_surface_set_opaque_region(window->surface, NULL);
	}

	if (display->swap_buffers_with_damage && buffer_age > 0) {
		rect[0] = window->egl_width / 4 - 1;
		rect[1] = window->egl_height / 4 - 1;
		rect[2] = window->egl_width / 2 + 2;
		rect[3] = window->egl_height / 2 + 2;
		display->swap_buffers_with_damage(display->egl.dpy,
						window->egl_surface,
						rect, 1);
	} else {
		eglSwapBuffers(display->egl.dpy, window->egl_surface);
	}
}

static void init_surface(struct window *window)
{
	int ret = 0;
	create_surface(window);

	/* we already have wait_for_configure set after create_surface() */
	while (running && ret != -1 && window->wait_for_configure) {
		ret = wl_display_dispatch(window->display->display);

		/* wait until xdg_surface::configure acks the new dimensions */
		if (window->wait_for_configure)
			continue;

		choose_output(window->display, window->output_name);
		init_gl(window);
	}
}

static void draw_frame(struct window *window, bool is_init, bool show_blank)
{
	int ret = 0;

	if (is_init) {
		init_surface(window);
		redraw(window, NULL, true);
		redraw(window, NULL, true);
	} else {
		if (show_blank)
			redraw(window, NULL, true);

		redraw(window, NULL, false);
	}
}

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	xdg_wm_base_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
			 uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface,
					 MIN(version, 4));
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		d->wm_base = wl_registry_bind(registry, name,
						&xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
	} else if (strcmp(interface, "wl_output") == 0) {
		d->info.display = d->display;
		d->info.registry = d->registry;
		add_output_info(&d->info, name, version);
	} else if (!strcmp(interface, zxdg_output_manager_v1_interface.name)) {
		d->info.display = d->display;
		d->info.registry = d->registry;
		add_xdg_output_manager_v1_info(&d->info, name, version);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
				uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
signal_int(int signum)
{
	running = 0;
}

static void destroy_display(struct display *display) {
	if (display->wm_base)
		xdg_wm_base_destroy(display->wm_base);

	if (display->compositor)
		wl_compositor_destroy(display->compositor);

	if (display->info.xdg_output_manager_v1_info)
		destroy_xdg_output_manager_v1_info(
			display->info.xdg_output_manager_v1_info);

	wl_registry_destroy(display->registry);
	wl_display_flush(display->display);
	wl_display_disconnect(display->display);
}

static int init_display(struct display *display, struct window *window) {
	display->display = wl_display_connect(NULL);
	assert(display->display);

	display->registry = wl_display_get_registry(display->display);
	wl_registry_add_listener(display->registry,
				 &registry_listener, display);

	wl_list_init(&display->info.outputs);
	display->info.xdg_output_manager_v1_info = NULL;

	wl_display_roundtrip(display->display);

	if (!display->wm_base) {
		fprintf(stderr, "xdg-shell support required. simple-egl exiting\n");
		destroy_display(display);
		return -1;
	}

	return 0;
}

static void
usage(int error_code)
{
	fprintf(stderr, "Usage: simple-egl [OPTIONS]\n\n"
		"  -d <us>\tBuffer swap delay in microseconds\n"
		"  -f\tRun in fullscreen mode\n"
		"  -m\tRun in maximized mode\n"
		"  -output\t Set output <Get more detail from weston-info>\n"
		"  -index\t Set output <Get more detail from weston-info>\n"
			"\n\t\t 0 - sRGB to sRGB (default)"
			"\n\t\t 1 - sRGB to DisplayP3"
			"\n\t\t 2 - sRGB to DCI_P3\n"
		"  -h\tThis help text\n\n");

	exit(error_code);
}

int
main(int argc, char **argv)
{
	int i = 0, ret = 0;
	struct sigaction sigint;
	struct display display = { 0 };
	struct window  window  = { 0 };
	TPGConfig cfg = {
			// default config
			// color(r,g,b)
			1.0, 1.0, 1.0,
			0.0, 0.0, 0.0,
			// position(x,y)
			0, 0,
			// size(w,h)
			250, 250,
			// depth
			8
	};

	window.display = &display;
	display.window = &window;
	window.egl_width  = cfg.w;
	window.egl_height = cfg.h;
	window.delay = 0;
	window.index_rgb = 0;
	window.cfg = &cfg;
	window.fullscreen = 1;
	window.output_name = NULL;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	for (i = 1; i < argc; i++) {
		if (strcmp("-d", argv[i]) == 0 && i+1 < argc)
			window.delay = atoi(argv[++i]);
		else if (strcmp("-f", argv[i]) == 0)
			window.fullscreen = 1;
		else if (strcmp("-m", argv[i]) == 0)
			window.maximized = 1;
		else if (strcmp("-index", argv[i]) == 0)
			window.index_rgb = atoi(argv[++i]);
		else if (strcmp("-output", argv[i]) == 0) {
			window.output_name = argv[++i];
			printf("fullscreen in output-(%s)\n",window.output_name);
		}
		else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS);
		else
			usage(EXIT_FAILURE);
	}

	ret = init_display(&display, &window);
	if (ret != 0) {
		fprintf(stderr, "Init display failed! qdcm-solid-color-tool exiting\n");
		return -1;
	}

	init_egl(&display, &window);

	draw_frame(&window, true, true); // show blank

	command_start(&display);

	while (running && ret != -1) {
		ret = wl_display_dispatch_pending(display.display);
		if (need_repaint()) {
			redraw(&window, NULL, false);
			repaint_done();
		}
	}

	fprintf(stderr, "qdcm-solid-color-tool exiting\n");

	destroy_surface(&window);
	fini_egl(&display);
	destroy_display(&display);

	return 0;
}
