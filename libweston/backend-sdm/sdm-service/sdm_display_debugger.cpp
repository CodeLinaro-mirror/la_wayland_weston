/*
* Copyright (c) 2017,2021 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <stdlib.h>
#include <display_properties.h>
#include <cutils/properties.h>
#include <utils/constants.h>

extern "C" {
#include <libweston/libweston.h>
#include "weston-log-internal.h"
};

#include "sdm-service/sdm_display_debugger.h"

#define __CLASS__ "SdmDisplayDebugger"
#define PATH_MAX 256
#define DISPLAY_TRACE_DEBUG_PROP "persist.display.trace.debug"

using std::string;

namespace sdm {

SdmDisplayDebugger SdmDisplayDebugger::debug_handler_;

void SdmDisplayDebugger::DebugAll(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_ = 0x7FFFFFFF;
    if (verbose_level) {
      // Enable verbose scalar logs only when explicitly enabled
      debug_handler_.log_mask_[kTagScalar] = 0;
    }
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_ = 0x1;   // kTagNone should always be printed.
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugResources(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagResources] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagResources] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugStrategy(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagStrategy] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagStrategy] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugCompManager(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagCompManager] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagCompManager] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugDriverConfig(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagDriverConfig] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagDriverConfig] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugRotator(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagRotator] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagRotator] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugScalar(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagScalar] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagScalar] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugQdcm(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagQDCM] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagQDCM] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugClient(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagClient] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagClient] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugDisplay(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagDisplay] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagDisplay] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

void SdmDisplayDebugger::DebugQos(bool enable, int verbose_level) {
  if (enable) {
    debug_handler_.log_mask_[kTagQOSClient] = 1;
    debug_handler_.log_mask_[kTagQOSImpl] = 1;
    debug_handler_.verbose_level_ = verbose_level;
  } else {
    debug_handler_.log_mask_[kTagQOSClient] = 0;
    debug_handler_.log_mask_[kTagQOSImpl] = 0;
    debug_handler_.verbose_level_ = 0;
  }

  DebugHandler::SetLogMask(debug_handler_.log_mask_);
}

static void Log(const char *prefix, const char *format, va_list list) {
  weston_vlog(format, list);
  weston_log_continue("%s", "\n");
}

void SdmDisplayDebugger::config_debug_level(void) {
  FILE *fp = NULL;
  //file to configure debug level
  fp = fopen("/data/misc/display/sdm_dbg_cfg.txt", "r");
  if (fp) {
      fscanf(fp, "%d", &verbose_level_);
      DebugAll(true /* enable */, verbose_level_);
      printf("\n sdm debug level configured to %d\n", verbose_level_);
      fclose(fp);
  }
}

void SdmDisplayDebugger::config_trace_debug(void) {
  if (GetProperty(DISPLAY_TRACE_DEBUG_PROP, &trace_debug_) == kErrorNone) {
    printf("found trace prop, set sdm trace debug:%s\n", trace_debug_?"enable":"disable");
  } else {
    printf("trace prop not found, default is disable! -%d\n", trace_debug_);
  }
}

void SdmDisplayDebugger::Error(const char *format, ...) {
  if (verbose_level_ > NONE) {
      va_list list;
      va_start(list, format);
      Log("SDM_E: ", format, list);
      va_end(list);
  }
}

void SdmDisplayDebugger::Warning(const char *format, ...) {
  if (verbose_level_ > ERROR) {
      va_list list;
      va_start(list, format);
      Log("SDM_W: ", format, list);
      va_end(list);
  }
}

void SdmDisplayDebugger::Info(const char *format, ...) {
  if (verbose_level_ > WARNING) {
      va_list list;
      va_start(list, format);
      Log("SDM_I: ", format, list);
      va_end(list);
  }
}

void SdmDisplayDebugger::Debug(const char *format, ...) {
  if (verbose_level_ > INFO) {
      va_list list;
      va_start(list, format);
      Log("SDM_D: ", format, list);
      va_end(list);
  }
}

void SdmDisplayDebugger::Verbose(const char *format, ...) {
  if (verbose_level_ > DEBUG) {
      va_list list;
      va_start(list, format);
      Log("SDM_V: ", format, list);
      va_end(list);
  }
}

int SdmDisplayDebugger::GetProperty(const char *property_name, int *value) {
  char property[PROPERTY_VALUE_MAX];

  if (property_get(property_name, property, NULL) > 0) {
    *value = atoi(property);
    return kErrorNone;
  }

  return kErrorNotSupported;
}

int SdmDisplayDebugger::GetProperty(const char *property_name, char *value) {
  if (property_get(property_name, value, NULL) > 0) {
    return kErrorNone;
  }

  return kErrorNotSupported;
}

SdmDisplayDebugger::SdmDisplayDebugger() {
  DebugHandler::Set(SdmDisplayDebugger::Get());
  config_debug_level();
  config_trace_debug();
}

int SdmDisplayDebugger::GetIdleTimeoutMs() {
  int value = IDLE_TIMEOUT_DEFAULT_MS;
  debug_handler_.GetProperty(IDLE_TIME_PROP, &value);

  return value;
}

void SdmDisplayDebugger::BeginTrace(const char *class_name, const char *function_name,
                                                  const char *custom_string) {
  if (trace_debug_) {
    char name[PATH_MAX] = {0};
    snprintf(name, sizeof(name), "%s::%s::%s", class_name, function_name, custom_string);
    atrace_begin(ATRACE_TAG, name);
  }
}

void SdmDisplayDebugger::EndTrace() {
  if (trace_debug_) {
    atrace_end(ATRACE_TAG);
  }
}

}  // namespace sdm
