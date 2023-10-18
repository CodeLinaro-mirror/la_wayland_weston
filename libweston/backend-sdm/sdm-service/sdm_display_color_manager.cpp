/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#include <cutils/sockets.h>
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <QService.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "sdm_display_color_manager.h"
#include "sdm_display_buffer_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __CLASS__ "SDMColorManager"

namespace sdm {

int SDMColorManager::CreatePayloadFromParcel(const android::Parcel &in,
                                             uint32_t *disp_id,
                                             struct PPDisplayAPIPayload *sink) {
  int ret = 0;
  uint32_t id(0);
  uint32_t size(0);

  id = UINT32(in.readInt32());
  size = UINT32(in.readInt32());
  if (size > 0 && size == in.dataAvail()) {
    const void *data = in.readInplace(size);
    const uint8_t *temp = reinterpret_cast<const uint8_t *>(data);

    sink->size = size;
    sink->payload = const_cast<uint8_t *>(temp);
    *disp_id = id;
  } else {
    DLOGW("Failing size checking, size = %d", size);
    ret = -EINVAL;
  }

  return ret;
}

SDMColorManager *SDMColorManager::CreateColorManager(BufferAllocator * buffer_allocator) {
  SDMColorManager *color_mgr = new SDMColorManager(buffer_allocator);

  if (color_mgr) {
    // Load display API interface library. And retrieve color API function tables.
    DynLib &color_apis_lib = color_mgr->color_apis_lib_;
    if (color_apis_lib.Open(DISPLAY_API_INTERFACE_LIBRARY_NAME)) {
      if (!color_apis_lib.Sym(DISPLAY_API_FUNC_TABLES, &color_mgr->color_apis_)) {
        DLOGE("Fail to retrieve = %s from %s", DISPLAY_API_FUNC_TABLES,
              DISPLAY_API_INTERFACE_LIBRARY_NAME);
        delete color_mgr;
        return NULL;
      }
    } else {
      DLOGW("Unable to load = %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);
      delete color_mgr;
      return NULL;
    }
    DLOGI("Successfully loaded %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);

  } else {
    DLOGE("Unable to create SDMColorManager");
    return NULL;
  }
  return color_mgr;
}

SDMColorManager::SDMColorManager(BufferAllocator *buffer_allocator) :
                                 buffer_allocator_(buffer_allocator) {
}

SDMColorManager::~SDMColorManager() {
}

void SDMColorManager::DestroyColorManager() {
  delete this;
}

void SDMColorManager::MarshallStructIntoParcel(const struct PPDisplayAPIPayload &data,
                                               android::Parcel *out_parcel) {
  if (data.fd > 0) {
    int err = out_parcel->writeDupFileDescriptor(data.fd);
    if (err) {
      DLOGE("writeDupFileDescriptor status = %d", err);
    }
    close(data.fd);
  }

  out_parcel->writeInt32(INT32(data.size));
  if (data.payload)
    out_parcel->write(data.payload, data.size);
}

}  // namespace sdm
#ifdef __cplusplus
}
#endif
