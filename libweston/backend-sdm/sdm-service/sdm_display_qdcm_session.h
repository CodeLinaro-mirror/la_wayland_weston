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
  int32_t RefreshScreen(const android::Parcel *input_parcel);
  int32_t RefreshScreen(uint32_t display_id);
  SDMColorManager *color_mgr_ = nullptr;
};

}
#endif // SDM_DISPLAY_QDCM_SESSION_H
