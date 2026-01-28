/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Changes from Qualcomm Technologies, Inc. are provided under the following license:
*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <wayland-client.h>
#include "weston-qti-extn-client-protocol.h"

#define MAX_STRING_SIZE 32
#define MAX_NUM_SIZE    5
#define DECIMAL         10

static enum ops_index {
	OPS_POWER_ON = 1,
	OPS_POWER_OFF,
	OPS_SET_OUTPUT_STATE,
	OPS_SET_OUTPUT_BRIGHTNESS,
	OPS_SET_OUTPUT_QSYNC_MODE,
	OPS_SET_OUTPUT_FPS,
	OPS_EXIT
};

static struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct weston_qti_extn *qti_extn;
} display;

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
					const char *interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0) {
		display.compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, "weston_qti_extn") == 0) {
		display.qti_extn = wl_registry_bind(registry, id, &weston_qti_extn_interface, 1);
	}
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_remover
};

static int
ops_get_input_num(void)
{
	long num;
	char buf[MAX_NUM_SIZE];
	char *end_ptr = NULL;
	size_t len;

	if (fgets(buf, sizeof(buf), stdin) == NULL) {
		printf("ERR: failed to get input num\n");
		clearerr(stdin);
		return -1;
	}

	len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len--;
	} else if (len == sizeof(buf) - 1 && buf[len - 1] != '\n') {
		int c;
		while ((c = getchar()) != '\n' && c != EOF);
		printf("ERR: invalid input num length\n");
		return -1;
	}

	if (len == 0) {
		printf("ERR: invalid input num length 0\n");
		return -1;
	}

	num = strtol(buf, &end_ptr, DECIMAL);
	if (*end_ptr != '\0' || end_ptr == buf) {
		printf("ERR: failed to convert num\n");
		return -1;
	}

	if (num > INT_MAX || num < INT_MIN) {
		printf("ERR: input exceeds integer limits\n");
		return -1;
	}

	return (int)num;
}

static void
ops_set_output_state(void)
{
	char output_name[MAX_STRING_SIZE];
	size_t len;
	int state;

	printf("Enter your output name: ");
	if (fgets(output_name, sizeof(output_name), stdin) == NULL) {
		printf("ERR: failed to get output name\n");
		clearerr(stdin);
		return;
	}

	len = strlen(output_name);
	if (len > 0 && output_name[len - 1] == '\n') {
		output_name[len - 1] = '\0';
		len--;
	}

	if (len == 0 || len >= sizeof(output_name) - 1) {
		printf("ERR: invalid output name length\n");
		return;
	}

	printf("Enter output state:\n"
		"  0. power off\n"
		"  1. power on \n"
		"Enter your value: ");
	state = ops_get_input_num();

	if (state < 0) {
		printf("ERR: invalid state. Please try again.\n");
		return;
	}

	if (state != 0 && state != 1) {
		printf("ERR: invalid state. Please try [0/1] again.\n");
		return;
	}

	weston_qti_extn_set_output_state(display.qti_extn, (const char *)output_name, (unsigned int)state);
	printf("INFO: set output(%s) state to %u\n", output_name, (unsigned int)state);
}

static void
ops_set_output_brightness(void)
{
	char output_name[MAX_STRING_SIZE];
	size_t len;
	int br_val;

	printf("Enter your output name: ");
	if (fgets(output_name, sizeof(output_name), stdin) == NULL) {
		printf("ERR: failed to get output name\n");
		clearerr(stdin);
		return;
	}

	len = strlen(output_name);
	if (len > 0 && output_name[len - 1] == '\n') {
		output_name[len - 1] = '\0';
		len--;
	}

	if (len == 0 || len >= sizeof(output_name) - 1) {
		printf("ERR: invalid output name length\n");
		return;
	}

	printf("Enter brightness value [0~255]: ");
	br_val = ops_get_input_num();
	if (br_val < 0 || br_val > 255) {
		printf("ERR: invalid brightness. Please try again.\n");
		return;
	}

	weston_qti_extn_set_brightness(display.qti_extn, (const char *)output_name, (unsigned int)br_val);
	printf("INFO: set output(%s) brightness to %u\n", output_name, (unsigned int)br_val);
}

static void
ops_set_output_fps(void)
{
	char output_name[MAX_STRING_SIZE] = {};
	size_t len = 0;
	int fps = 0;

	printf("Enter your output name: ");
	if (fgets(output_name, sizeof(output_name), stdin) == NULL) {
		printf("ERR: failed to get output name\n");
		clearerr(stdin);
		return;
	}

	len = strlen(output_name);
	if (len > 0 && output_name[len - 1] == '\n') {
		output_name[len - 1] = '\0';
		len--;
	}

	if (len == 0 || len >= sizeof(output_name) - 1) {
		printf("ERR: invalid output name length\n");
		return;
	}

	printf("Enter fps value: ");
	fps = ops_get_input_num();
	if (fps < 0) {
		printf("ERR: invalid fps. Please try again.\n");
		return;
	}

	weston_qti_extn_set_output_fps(display.qti_extn, (const char *)output_name, (uint32_t)fps);
	printf("INFO: set output(%s) fps to %u\n", output_name, (uint32_t)fps);
}

static void
ops_set_output_qsync_mode(void)
{
	char output_name[MAX_STRING_SIZE] = "";

	printf("enter your output name : ");
	if (fgets(output_name, MAX_STRING_SIZE, stdin) == NULL) {
		printf("Failed to read output name\n");
		clearerr(stdin);
		return;
	}

	int len = strlen(output_name);
	if (len > 0 && output_name[len - 1] == '\n') {
		output_name[len - 1] = '\0';
		len--;
	}

	if (len == 0 || len >= MAX_STRING_SIZE - 1) {
		printf("Invalid output name length\n");
		return;
	}

	printf("Enter output qsync mode: \n \
	      0. Qsync mode off\n \
	      1. Qsync mode continuous \n ");
	printf("enter your value : ");
	uint32_t mode = (uint32_t)ops_get_input_num();
	if (mode != 0 && mode != 1) {
		printf("Invalid input. Please try again.\n");
		return;
	}

	weston_qti_extn_set_output_qsync_mode(display.qti_extn,
	                              (const char *)output_name, mode);
	printf("INFO: set output(%s) qsync mode to %u\n", output_name, mode);
}

static void
print_menu(void)
{
	printf("\n=== Weston QTI Extension Test ===\n"
		"  1. Power On\n"
		"  2. Power Off\n"
		"  3. Set Output State\n"
		"  4. Set Brightness\n"
		"  5. Set Qsync mode\n"
		"  6. Set Output FPS\n"
		"  7. Exit\n"
		"Enter your choice: ");
}

int
main(int argc, char **argv)
{
	bool loop;
	int choice;

	display.display = wl_display_connect(NULL);
	if (display.display == NULL) {
		printf("ERR: can't connect to display\n");
		return -1;
	}

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry, &registry_listener, NULL);

	wl_display_roundtrip(display.display);

	if (display.compositor == NULL || display.qti_extn == NULL) {
		printf("ERR: required interfaces (compositor or qti_extn) not found\n");
		wl_registry_destroy(display.registry);
		wl_display_disconnect(display.display);
		return -1;
	}

	loop = true;
	while (loop) {
		print_menu();

		choice = ops_get_input_num();
		if (choice < 0) {
			printf("ERR: invalid choice, please try again.\n");
			continue;
		}

		switch (choice) {
		case OPS_POWER_ON:
			weston_qti_extn_power_on(display.qti_extn);
			break;
		case OPS_POWER_OFF:
			weston_qti_extn_power_off(display.qti_extn);
			break;
		case OPS_SET_OUTPUT_STATE:
			ops_set_output_state();
			break;
		case OPS_SET_OUTPUT_BRIGHTNESS:
			ops_set_output_brightness();
			break;
		case OPS_SET_OUTPUT_QSYNC_MODE:
			ops_set_output_qsync_mode();
			break;
		case OPS_SET_OUTPUT_FPS:
			ops_set_output_fps();
			break;
		case OPS_EXIT:
			loop = false;
			break;
		default:
			printf("ERR: invalid choice, please try again.\n");
			break;
		}

		if (loop) {
			wl_display_roundtrip(display.display);
		}
	}

	wl_registry_destroy(display.registry);
	wl_display_disconnect(display.display);
	printf("INFO: disconnected from display\n");

	return 0;
}
