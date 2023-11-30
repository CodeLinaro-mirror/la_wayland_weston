/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#ifndef __SDM_DISPLAY_COLOR_MANAGER_H__
#define __SDM_DISPLAY_COLOR_MANAGER_H__

#include <binder/Parcel.h>
#include <binder/BinderService.h>
#include <core/sdm_types.h>
#include <utils/locker.h>
#include <private/color_params.h>
#include <dlfcn.h>
#include <utils/debug.h>
#include <utils/constants.h>
#include <utils/formats.h>
#include <stdio.h>
#include <string>
#include <utility>
#include <map>
#include <vector>
#include <iostream>
#include <thread>
#include <utils/sys.h>
#include <fcntl.h>
#include <unistd.h>

#include "sdm-service/sdm_display_buffer_allocator.h"

#define DISPLAY_API_INTERFACE_LIBRARY_NAME "libsdm-disp-vndapis.so"
#define DISPLAY_API_FUNC_TABLES "display_color_apis_ftables"

using std::fstream;

typedef class BufferAllocator BufferAllocator;
typedef class DynLib DynLib;

namespace sdm {

class SDMColorManager {
 public:
  static SDMColorManager *CreateColorManager(BufferAllocator *buffer_allocator);
  static int CreatePayloadFromParcel(const android::Parcel &in, uint32_t *disp_id,
                                     struct PPDisplayAPIPayload *sink);
  static void MarshallStructIntoParcel(const struct PPDisplayAPIPayload &data,
                                       android::Parcel *out_parcel);

  explicit SDMColorManager(BufferAllocator *buffer_allocator);
  ~SDMColorManager();
  void DestroyColorManager();
 private:
  DynLib color_apis_lib_;
  void *color_apis_ = NULL;
  BufferAllocator *buffer_allocator_ = NULL;
  BufferInfo buffer_info = {};
  Locker locker_;
};
} // namespace sdm
#endif // SDM_DISPLAY_COLOR_MANAGER_H
