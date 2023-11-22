/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#ifndef __SDM_DISPLAY_QDCM_SESSION_H__
#define __SDM_DISPLAY_QDCM_SESSION_H__

#include <QService.h>
#include <core/ipc_interface.h>
#include <private/color_params.h>
#include "sdm_display_interface.h"
#include "sdm_display_color_manager.h"
#include "sdm_display_buffer_allocator.h"
#include "sdm_display.h"

namespace sdm {

class QDCMSession : public qClient::BnQClient {
  public:
  static const int kNumBuiltIn = 4;
  static const int kNumPluggable = 4;
  static const int kNumVirtual = 4;
  // Add 1 primary display which can be either a builtin or pluggable.
  // Async powermode update requires dummy hwc displays.
  // Limit dummy displays to builtin/pluggable type for now.
  static const int kNumRealDisplays = 1 + kNumBuiltIn + kNumPluggable + kNumVirtual;
  static const int kNumDisplays = 1 + kNumBuiltIn + kNumPluggable + kNumVirtual +
                                  1 + kNumBuiltIn + kNumPluggable;

  QDCMSession();
  int Init(BufferAllocator *buffer_allocator_);
  int Deinit();

  private:
  virtual int32_t notifyCallback(uint32_t command,
                                 const android::Parcel *input_parcel,
                                 android::Parcel *output_parcel);
  int32_t QdcmCMDHandler(const android::Parcel *input_parcel,
                         android::Parcel *output_parcel);
  int32_t QdcmCMDDispatch(uint32_t display_id,
                          const struct PPDisplayAPIPayload &req_payload,
                          struct PPDisplayAPIPayload *resp_payload,
                          struct PPPendingParams *pending_action);
  SDMColorManager *color_mgr_ = nullptr;
  SDMQDCMModeManager *qdcm_mode_mgr_ = nullptr;
};

}
#endif // SDM_DISPLAY_QDCM_SESSION_H
