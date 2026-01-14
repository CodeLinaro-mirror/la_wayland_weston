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
#include <wayland-client.h>
#include "weston-qti-extn-client-protocol.h"

#define MAX_STRING_SIZE 32
#define MAX_NUM_SIZE    5
#define DECIMAL         10

struct display {
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct weston_qti_extn *qti_extn;
};
struct display display;

static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version) {
  printf("Got a registry event for %s id %d\n", interface, id);
  if (strcmp(interface, "wl_compositor") == 0) {
    display.compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
  } else if (strcmp(interface, "weston_qti_extn") == 0) {
    display.qti_extn = wl_registry_bind(registry, id, &weston_qti_extn_interface, 1);
  }
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
  printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
  global_registry_handler,
  global_registry_remover
};

static int ops_get_input_num()
{
  int num = -1;
  char buf[MAX_NUM_SIZE] = "";

  char *buf_ptr = fgets(buf, MAX_NUM_SIZE, stdin);
  if (buf_ptr == NULL) {
    printf("Failed to get input num\n");
    clearerr(stdin);
    return -1;
  }

  int len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    buf[len - 1] = '\0';
    len--;
  } else if (len == MAX_NUM_SIZE) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
  }

  if (len == 0 || len >= MAX_NUM_SIZE - 1) {
    printf("Invalid input num length\n");
    return -1;
  }

  char *end_ptr = NULL;
  num = strtol(buf_ptr, &end_ptr, DECIMAL);
  if (*end_ptr != '\0' || end_ptr == buf_ptr) {
    printf("Invalied num\n");
    return -1;
  }

  return num;
}

static void ops_set_output_state(void)
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

  printf("Enter output state: \n \
          0. power off\n \
          1. power on \n ");
  printf("enter your value : ");
  unsigned int state = (unsigned int)ops_get_input_num();
  if (state != 0 && state != 1) {
    printf("Invalid input. Please try again.\n");
    return;
  }

  weston_qti_extn_set_output_state(display.qti_extn,
                                  (const char *)output_name, state);
  printf("set output(%s) state to %u\n", output_name, state);
}

static void ops_set_output_brightness(void)
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

  printf("Enter brightness value [0~255] : ");
  unsigned int value = (unsigned int)ops_get_input_num();
  if (value > 255) {
    printf("Invalid input. Please try again.\n");
    return;
  }

  weston_qti_extn_set_brightness(display.qti_extn,
                                (const char *)output_name, value);
  printf("set output(%s) brightness to %u\n", output_name, value);
}

int main(int argc, char **argv) {
  display.display = wl_display_connect(NULL);
  if (display.display == NULL) {
    fprintf(stderr, "Can't connect to display\n");
    exit(1);
  }
  printf("connected to display\n");

  // get registry handle
  struct wl_registry *registry = wl_display_get_registry(display.display);
  wl_registry_add_listener(registry, &registry_listener, NULL);

  wl_display_dispatch(display.display);
  wl_display_roundtrip(display.display);

  if (display.compositor == NULL) {
    fprintf(stderr, "Can't find compositor\n");
    exit(1);
  }

  if (display.qti_extn == NULL) {
    fprintf(stderr, "Can't find weston_qti_extn\n");
    exit(1);
  }

  printf("Enter the test case no : \n \
            1. Power On \n \
            2. Power Off \n \
            3. set output state \n \
            4. set brightness \n \
            5. Exit \n");
  printf("enter your choice : ");
  unsigned int choice = (unsigned int)ops_get_input_num();
  int loop = 1;
  while (1) {
    switch (choice) {
      case 1:
        weston_qti_extn_power_on(display.qti_extn);
      break;
      case 2:
        weston_qti_extn_power_off(display.qti_extn);
      break;
      case 3:
        ops_set_output_state();
      break;
      case 4:
        ops_set_output_brightness();
      break;
      case 5:
        loop = 0;
      break;
      default :
        printf("wrong choice Enter again\n");
      break;
    }
    if (loop == 0) {
      break;
    }

    wl_display_roundtrip(display.display);

    printf("Enter the test case no : \n \
              1. Power On \n \
              2. Power Off \n \
              3. set output state \n \
              4. set brightness \n \
              5. Exit \n");
    printf("enter your choice : ");
    choice = (unsigned int)ops_get_input_num();
  }

  wl_display_disconnect(display.display);
  printf("disconnected from display\n");

  exit(0);
}
