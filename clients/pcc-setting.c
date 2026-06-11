// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wayland-client.h>
#include "pcc-control-client-protocol.h"

static struct pcc_control *pcc = NULL;

/*
 * Convert a double to S3.15 fixed-point format.
 * The result is an 18-bit unsigned value representing a signed 3.15 number.
 */
static uint32_t
double_to_s3_15(double val)
{
	return ((uint32_t)((int32_t)(round(val * (1LL << 15))))) & 0x3FFFF;
}

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
			const char *interface, uint32_t version)
{
	if (strcmp(interface, "pcc_control") == 0) {
		pcc = wl_registry_bind(registry, id, &pcc_control_interface, 1);
		printf("[Client] Bound to pcc_control interface\n");
	}
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
	printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_remover
};

int main(int argc, char **argv)
{
	uint32_t coeffs[9];
	double vals[9];
	struct wl_display *display;
	struct wl_registry *registry;

	if (argc != 10) {
		fprintf(stderr, "Usage: %s rr rg rb gr gg gb br bg bb\n", argv[0]);
		fprintf(stderr, "  Coefficients must be floating-point values in [-1.87, 1.87].\n");
		fprintf(stderr, "  Identity matrix: 1.0 0.0 0.0  0.0 1.0 0.0  0.0 0.0 1.0\n");
		return -1;
	}

	for (int i = 0; i < 9; i++) {
		char *endptr;
		vals[i] = strtod(argv[i + 1], &endptr);

		/* Detect non-numeric input */
		if (endptr == argv[i + 1] || *endptr != '\0') {
			fprintf(stderr, "Error: argument %d '%s' is not a valid number\n",
				i + 1, argv[i + 1]);
			return -1;
		}

		/* Validate range [-1.87, 1.87] */
		if (vals[i] < -1.87 || vals[i] > 1.87) {
			fprintf(stderr, "Error: argument %d (%.4f) is out of range [-1.0, 1.0]\n",
				i + 1, vals[i]);
			return -1;
		}

		coeffs[i] = double_to_s3_15(vals[i]);
		printf("coeff[%d] = %.4f -> fixed = 0x%X\n", i, vals[i], coeffs[i]);
	}

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "Can't connect to display\n");
		return 1;
	}
	printf("connected to display\n");

	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (pcc == NULL) {
		fprintf(stderr, "Can't find pcc_control interface\n");
		wl_display_disconnect(display);
		return -1;
	}

	pcc_control_set_pcc(pcc,
			    coeffs[0], coeffs[1], coeffs[2],
			    coeffs[3], coeffs[4], coeffs[5],
			    coeffs[6], coeffs[7], coeffs[8]);

	wl_display_flush(display);
	wl_display_roundtrip(display);

	pcc_control_destroy(pcc);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	printf("disconnected from display\n");
	return 0;
}
