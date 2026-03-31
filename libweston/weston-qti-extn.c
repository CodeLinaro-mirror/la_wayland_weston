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

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <libweston/libweston.h>

#include "weston-qti-extn-server-protocol.h"
#include "libweston-internal.h"
#include "weston-qti-extn.h"

#define MAX_OUPUT_NAME_LENGTH 256

const struct weston_qti_extn_interface weston_qti_extn_impl = {
  destroy,
  power_on,
  power_off,
  set_output_state,
  set_brightness,
  set_output_qsync_mode
};

void power_on(struct wl_client *client, struct wl_resource *resource) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  weston_compositor_wake(compositor);
}

void power_off(struct wl_client *client, struct wl_resource *resource) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  weston_compositor_sleep(compositor);
}

void set_output_state(struct wl_client *client, struct wl_resource *resource,
                      const char* output_name, unsigned int state) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  if (output_name == NULL) {
    weston_log("error: invalid output name\n");
    return;
  }

  size_t name_len = strnlen(output_name, MAX_OUPUT_NAME_LENGTH + 1);
  if (name_len == 0 || name_len > MAX_OUPUT_NAME_LENGTH) {
    weston_log("error: invalid ouput name length %zu\n", name_len);
    return;
  }

  for (size_t i = 0; i < name_len; i++) {
    if (!isprint((unsigned char)output_name[i]) && output_name[i] != '-' && output_name[i] != '_') {
      weston_log("error: output name contains invalid characters\n");
      return;
    }
  }

  if (state > 1) {
    weston_log("error: invalid state value %u (expected 0 or 1)\n", state);
    return;
  }

  struct weston_output *output;
  wl_list_for_each(output, &compositor->output_list, link) {
    if (output-> name && !strcmp(output->name, output_name)) {
      if (output->set_dpms == NULL) {
        weston_log("error: set_dpms not available for output %s\n", output->name);
        return;
      }

      if (state != 0) {
        output->set_dpms(output, WESTON_DPMS_ON);
      } else {
        output->set_dpms(output, WESTON_DPMS_OFF);
      }
      weston_log("set output(%s) to state-%d\n", output->name, state);
      return ;
    }
  }
  weston_log("set_output_state failed, output not found!\n");
}

void set_brightness(struct wl_client *client, struct wl_resource *resource,
                    const char* output_name, unsigned int brightness_value) {
  struct weston_compositor *compositor;
  compositor = wl_resource_get_user_data(resource);
  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  if (output_name == NULL) {
    weston_log("error: invalid output name\n");
    return;
  }

  size_t name_len = strnlen(output_name, MAX_OUPUT_NAME_LENGTH + 1);
  if (name_len == 0 || name_len > MAX_OUPUT_NAME_LENGTH) {
    weston_log("error: invalid output name length %zu\n", name_len);
    return;
  }

  if (brightness_value > 255) {
    weston_log("error: brightness value %u out of range (0-255)\n", brightness_value);
    return;
  }

  struct weston_output *output;
  wl_list_for_each(output, &compositor->output_list, link) {
    if (output->name && !strcmp(output->name, output_name)) {
      if (output->set_backlight == NULL) {
        weston_log("error: set_backlight not available for output %s\n", output->name);
        return;
      }
      output->set_backlight(output, brightness_value);
      return;
    }
  }
  weston_log("set_brightness failed, output not found!\n");
}

void set_output_qsync_mode(struct wl_client *client, struct wl_resource *resource,
                      const char* output_name, uint32_t mode) {
  struct weston_compositor *compositor = NULL;
  compositor = wl_resource_get_user_data(resource);

  if (compositor == NULL) {
    weston_log("error: compositor not found\n");
    return;
  }

  if (output_name == NULL) {
    weston_log("error: invalid output name\n");
    return;
  }

  size_t name_len = strnlen(output_name, MAX_OUPUT_NAME_LENGTH + 1);
  if (name_len == 0 || name_len > MAX_OUPUT_NAME_LENGTH) {
    weston_log("error: invalid ouput name length %zu\n", name_len);
    return;
  }

  for (size_t i = 0; i < name_len; i++) {
    if (!isprint((unsigned char)output_name[i]) && output_name[i] != '-' && output_name[i] != '_') {
      weston_log("error: output name contains invalid characters\n");
      return;
    }
  }

  if (mode > 1) {
    weston_log("error: invalid mode value %u (expected 0 or 1)\n", mode);
    return;
  }

  struct weston_output *output;
  wl_list_for_each(output, &compositor->output_list, link) {
    if (output-> name && !strcmp(output->name, output_name)) {
      if (output->set_qsync_mode == NULL) {
        weston_log("error: set_qsync_mode not available for output %s\n", output->name);
        return;
      }

      weston_log("%s set output(%s) qsync mode to mode: %d\n", __func__, output->name, mode);

      uint32_t error = 0;
      error = output->set_qsync_mode(output, mode);
      if (error != 0) {
        weston_log("Failed %s with error = %d\n", __func__, error);
      }

      return ;
    }
  }
  weston_log("set_output_qsync_mode failed, output not found!\n");
}

void destroy(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void
bind_weston_qti_extn(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
  struct weston_compositor *compositor = data;
  struct wl_resource *resource;

  weston_log("bind_weston_qti_extn::Invoked\n");
  resource = wl_resource_create(client, &weston_qti_extn_interface, version, id);
  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &weston_qti_extn_impl, compositor, NULL);
}

/** Advertise weston_qti_extn_setup support
 *
 * Calling this initializes the weston_qti_extn protocol support, so that
 * the interface will be advertised to clients. Essentially it creates a
 * global. Do not call this function multiple times in the compositor's
 * lifetime. There is no way to deinit explicitly, globals will be reaped
 * when the wl_display gets destroyed.
 *
 * \param compositor The compositor to init for.
 * \return Zero on success, -1 on failure.
 */
WL_EXPORT int weston_qti_extn_setup(struct weston_compositor *compositor) {
  if (!compositor) {
    weston_log("weston_qti_extn_setup: NULL compositor provided\n");
    return -1;
  }

  weston_log("weston_qti_extn_setup::Invoked\n");
  if (!wl_global_create(compositor->wl_display, &weston_qti_extn_interface, 1,
                        compositor, bind_weston_qti_extn)) {
    return -1;
  }
  return 0;
}
