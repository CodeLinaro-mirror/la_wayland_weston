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

/* Mode information structure */
struct mode_info {
	int32_t width;
	int32_t height;
	int32_t refresh;
	bool current;
	struct wl_list link;
};

/* Output information structure */
struct output_info {
	struct wl_output *output;
	char *name;
	int32_t width, height;
	int32_t refresh;
	bool has_name;
	struct wl_list link;
	struct wl_list modes;
};

/* Global output list */
static struct wl_list output_list;

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

/* wl_output event handlers */
static void
output_handle_geometry(void *data, struct wl_output *wl_output,
		       int32_t x, int32_t y,
		       int32_t physical_width, int32_t physical_height,
		       int32_t subpixel,
		       const char *make, const char *model,
		       int32_t transform)
{
}

static void
output_handle_mode(void *data, struct wl_output *wl_output,
		   uint32_t flags, int32_t width, int32_t height,
		   int32_t refresh)
{
	struct output_info *output = data;
		struct mode_info *mode, *existing_mode;
	bool duplicate = false;

	/* Check if this mode already exists (deduplication) */
	wl_list_for_each(existing_mode, &output->modes, link) {
		if (existing_mode->width == width &&
		    existing_mode->height == height &&
		    existing_mode->refresh == refresh) {
			duplicate = true;
			/* Update current flag if needed */
			if (flags & WL_OUTPUT_MODE_CURRENT) {
				existing_mode->current = true;
			}
			break;
		}
	}

	if (duplicate) {
		return;
	}

	/* Allocate new mode */
	mode = calloc(1, sizeof(*mode));
	if (!mode) {
		printf("ERR: Failed to allocate memory for mode\n");
		return;
	}

	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;
	mode->current = (flags & WL_OUTPUT_MODE_CURRENT);

	wl_list_insert(&output->modes, &mode->link);

	/* Update current mode info */
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		output->width = width;
		output->height = height;
		output->refresh = refresh;
	}
}

static void
output_handle_done(void *data, struct wl_output *wl_output)
{
}

static void
output_handle_scale(void *data, struct wl_output *wl_output,
		    int32_t factor)
{
}

static void
output_handle_name(void *data, struct wl_output *wl_output,
		   const char *name)
{
	struct output_info *output = data;
	output->name = strdup(name);
	if (output->name) {
		output->has_name = true;
        }
}

static void
output_handle_description(void *data, struct wl_output *wl_output,
			  const char *description)
{
}

static const struct wl_output_listener output_listener = {
	output_handle_geometry,
	output_handle_mode,
	output_handle_done,
	output_handle_scale,
	output_handle_name,
	output_handle_description
};

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
					const char *interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0) {
		display.compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	} else if (strcmp(interface, "weston_qti_extn") == 0) {
		display.qti_extn = wl_registry_bind(registry, id, &weston_qti_extn_interface, 1);
	} else if (strcmp(interface, "wl_output") == 0) {
		struct output_info *output;

		output = calloc(1, sizeof(*output));
		if (!output) {
			printf("ERR: Failed to allocate memory for output\n");
			return;
		}

		wl_list_init(&output->modes);
		output->output = wl_registry_bind(registry, id, &wl_output_interface,
		                                  version >= 4 ? 4 : version);
		wl_output_add_listener(output->output, &output_listener, output);
		wl_list_insert(&output_list, &output->link);
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
	struct output_info *output, *selected_out = NULL;
	struct mode_info *mode, *selected_mode = NULL;
	struct output_info **output_array = NULL;
	struct mode_info **mode_array = NULL;
	int output_count = 0, mode_count = 0;
	int choice, i, target_fps;
	bool menu_loop = true;

	/* Ensure output information is loaded */
	wl_display_roundtrip(display.display);

	/* Count outputs */
	wl_list_for_each(output, &output_list, link) {
		output_count++;
	}

	if (output_count == 0) {
		printf("ERR: No outputs found\n");
		return;
	}

	/* Build output array for indexing */
	output_array = calloc(output_count, sizeof(struct output_info *));
	if (!output_array) {
		printf("ERR: Failed to allocate memory\n");
		return;
	}

	i = 0;
	wl_list_for_each(output, &output_list, link) {
		/* Skip DP (DisplayPort) outputs */
		if (output->has_name && strncmp(output->name, "DP-", 3) == 0) {
			continue;
		}
		output_array[i++] = output;
	}
	output_count = i;

	if (output_count == 0) {
		printf("ERR: No non-DP outputs found\n");
		goto cleanup;
	}

	while (menu_loop) {
		if (!selected_out) {
			/* Display output selection menu */
			printf("\n=== Select Output ===\n");
			for (i = 0; i < output_count; i++) {
				struct output_info *out = output_array[i];
				if (out->has_name) {
					printf("  %d. %s (%dx%d@%d.%03dHz)\n",
					       i + 1, out->name,
					       out->width, out->height,
					       out->refresh / 1000, out->refresh % 1000);
				} else {
					printf("  %d. Output-%d (%dx%d@%d.%03dHz)\n",
					       i + 1, i,
					       out->width, out->height,
					       out->refresh / 1000, out->refresh % 1000);
				}
			}
			printf("  0. Back to main menu\n");
			printf("Enter your choice: ");

			choice = ops_get_input_num();
			if (choice < 0) {
				printf("ERR: Invalid choice\n");
				continue;
			}

			if (choice == 0) {
				goto cleanup;
			}

			if (choice < 1 || choice > output_count) {
				printf("ERR: Choice out of range\n");
				continue;
			}

			selected_out = output_array[choice - 1];
			printf("INFO: Selected %s\n",
			       selected_out->has_name ? selected_out->name : "Output");

		} else {
			/* Count modes for selected output */
			mode_count = 0;
			wl_list_for_each(mode, &selected_out->modes, link) {
				mode_count++;
			}

			if (mode_count == 0) {
				printf("ERR: No modes available for this output\n");
				selected_out = NULL;
				continue;
			}

			/* Build mode array for indexing */
			if (mode_array)
				free(mode_array);
			mode_array = calloc(mode_count, sizeof(struct mode_info *));
			if (!mode_array) {
				printf("ERR: Failed to allocate memory\n");
				goto cleanup;
			}

			i = 0;
			wl_list_for_each(mode, &selected_out->modes, link) {
				mode_array[i++] = mode;
			}

			/* Display mode selection menu */
			printf("\n=== Select Refresh Rate (Output: %s) ===\n",
			       selected_out->has_name ? selected_out->name : "Output");

			for (i = 0; i < mode_count; i++) {
				struct mode_info *m = mode_array[i];
				printf("  %d. %dx%d@%d.%03dHz%s\n",
				       i + 1,
				       m->width, m->height,
				       m->refresh / 1000, m->refresh % 1000,
				       m->current ? " (current)" : "");
			}
			printf("  %d. Reselect output\n", mode_count + 1);
			printf("  0. Back to main menu\n");
			printf("Enter your choice: ");

			choice = ops_get_input_num();
			if (choice < 0) {
				printf("ERR: Invalid choice\n");
				continue;
			}

			if (choice == 0) {
				goto cleanup;
			}

			if (choice == mode_count + 1) {
				selected_out = NULL;
				continue;
			}

			if (choice < 1 || choice > mode_count) {
				printf("ERR: Choice out of range\n");
				continue;
			}

			selected_mode = mode_array[choice - 1];
			target_fps = selected_mode->refresh / 1000;

			/* Execute FPS setting - only if output has a name */
			if (!selected_out->has_name) {
				printf("ERR: Output has no name, cannot set FPS\n");
				printf("     This output may not support dynamic FPS switching\n");
				selected_out = NULL;
				continue;
			}

			printf("\nSetting %s refresh rate to %d Hz...\n",
			       selected_out->name, target_fps);
			weston_qti_extn_set_output_fps(display.qti_extn,
						       selected_out->name,
						       (uint32_t)target_fps);

			/* Refresh output information to get updated current mode */
			wl_display_roundtrip(display.display);

			/* Update current mode flags */
			wl_list_for_each(mode, &selected_out->modes, link) {
				if (mode->refresh == selected_mode->refresh &&
					mode->width == selected_mode->width &&
					mode->height == selected_mode->height) {
					mode->current = true;
				} else {
					mode->current = false;
				}
			}

			printf("INFO: Set output(%s) fps to %u Hz\n",
			       selected_out->name, (uint32_t)target_fps);

			/* Free mode array and continue with same output */
			if (mode_array) {
				free(mode_array);
				mode_array = NULL;
			}
		}
	}

cleanup:
	if (mode_array)
		free(mode_array);
	if (output_array)
		free(output_array);
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
destroy_output_info(struct output_info *output)
{
	struct mode_info *mode, *tmp;

	/* Cleanup modes list */
	wl_list_for_each_safe(mode, tmp, &output->modes, link) {
		wl_list_remove(&mode->link);
		free(mode);
	}

	/* Cleanup output */
	free(output->name);
	if (output->output)
		wl_output_destroy(output->output);
	wl_list_remove(&output->link);
	free(output);
}

static void
destroy_outputs(void)
{
	struct output_info *output, *tmp;

	wl_list_for_each_safe(output, tmp, &output_list, link) {
		destroy_output_info(output);
	}
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

	wl_list_init(&output_list);

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

	/* Cleanup outputs */
	destroy_outputs();

	wl_registry_destroy(display.registry);
	wl_display_disconnect(display.display);
	printf("INFO: disconnected from display\n");

	return 0;
}
