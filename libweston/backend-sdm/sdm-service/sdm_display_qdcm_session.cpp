/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <QService.h>
#include <binder/Parcel.h>
#include <binder/IPCThreadState.h>
#include "sdm_display.h"
#include "sdm_display_qdcm_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __CLASS__ "QDCMSession"

namespace sdm {

QDCMSession::QDCMSession() {
}

int QDCMSession::Init(BufferAllocator *buffer_allocator_) {
  const char *qservice_name = "display.qservice";
  DLOGI("Initializing QService");
  qService::QService::init();
  DLOGI("Initializing QService...done!");

  DLOGI("Getting IQService");
  android::sp<qService::IQService> iqservice = android::interface_cast<qService::IQService>(
      android::defaultServiceManager()->getService(android::String16(qservice_name)));
  DLOGI("Getting IQService...done!");

  if (iqservice.get()) {
    iqservice->connect(android::sp<qClient::IQClient>(this));
    DLOGI("Acquired %s", qservice_name);
  } else {
    DLOGE("Failed to acquire %s", qservice_name);
    return -EINVAL;
  }
  color_mgr_ = SDMColorManager::CreateColorManager(buffer_allocator_);
  if (!color_mgr_) {
    DLOGW("Failed to load SDMColorManager.");
    return -EINVAL;
  }

  // Start service
  android::ProcessState::self()->setThreadPoolMaxThreadCount(4);
  android::ProcessState::self()->startThreadPool();
  android::ProcessState::self()->giveThreadPoolName();

  return 0;
}

int32_t QDCMSession::Deinit() {
  return 0;
}

int32_t QDCMSession::QdcmCMDDispatch(uint32_t display_id,
                                     const PPDisplayAPIPayload &req_payload,
                                     struct PPDisplayAPIPayload *resp_payload,
                                     PPPendingParams *pending_action) {
  int ret = 0;
  SdmDisplayProxy *dpy = GetDisplayFromIndex(display_id);
  if (!dpy) {
    DLOGE("Failed as Display (%d) not created yet.", display_id);
    return kErrorNotSupported;
  }
  ret = dpy->ColorSVCRequestRoute(req_payload, resp_payload, pending_action);

  return ret;
}

int32_t QDCMSession::QdcmCMDHandler(const android::Parcel *input_parcel,
                                    android::Parcel *output_parcel) {
  int ret = 0;
  float *brightness = NULL;
  uint32_t display_id(0), numdisps = 0, id = 0;
  PPPendingParams pending_action;
  struct PPDisplayAPIPayload resp_payload, req_payload;
  uint8_t *disp_id = NULL;
  int32_t *mode_id = NULL;

  if (!color_mgr_) {
    DLOGW("color_mgr_ not initialized.");
    return -ENOENT;
  }

  pending_action.action = kNoAction;
  pending_action.params = NULL;

  // Read display_id, payload_size and payload from in_parcel.
  ret = SDMColorManager::CreatePayloadFromParcel(*input_parcel, &display_id, &req_payload);
  if (!ret) {
    ret = QdcmCMDDispatch(display_id, req_payload, &resp_payload, &pending_action);
  }

  if (ret) {
    output_parcel->writeInt32(ret);  // first field in out parcel indicates return code.
    req_payload.DestroyPayload();
    resp_payload.DestroyPayload();
    return ret;
  }

  SdmDisplayProxy *dpy = GetDisplayFromIndex(display_id);
  if (!dpy) {
    DLOGE("Failed as Display (%d) not created yet.", display_id);
    return kErrorNotSupported;
  }

  if (kNoAction != pending_action.action) {
    int32_t action = pending_action.action;
    int count = -1;
    while (action > 0) {
      count++;
      int32_t bit = (action & 1);
      action = action >> 1;

      if (!bit)
        continue;

      DLOGV_IF(kTagQDCM, "pending action = %d, display_id = %d", BITMAP(count), display_id);
      switch (BITMAP(count)) {
        case kInvalidating:
          dpy->RefreshWithCachedLayerstack();
          break;
        case kEnterQDCMMode:
          ret = 0;
          break;
        case kExitQDCMMode:
          ret = 0;
          break;
        case kApplySolidFill:
          ret = 0;
          break;
        case kDisableSolidFill:
          ret = 0;
          break;
        case kSetPanelBrightness:
          ret = -EINVAL;
          brightness = reinterpret_cast<float *>(resp_payload.payload);
          if (brightness == NULL) {
            DLOGE("Brightness payload is Null");
          } else {
            ret = dpy->SetPanelBrightness(*brightness);
          }
          break;
        case kEnableFrameCapture:
          ret = 0;
          break;
        case kDisableFrameCapture:
          ret = 0;
          break;
        case kConfigureDetailedEnhancer:
          ret = color_mgr_->SetDetailedEnhancer(pending_action.params, dpy);
          dpy->RefreshWithCachedLayerstack();
          break;
        case kModeSet:
          ret = dpy->RestoreColorTransform();
          dpy->RefreshWithCachedLayerstack();
          break;
        case kNoAction:
          break;
        case kMultiDispProc:
          numdisps = GetDisplayCount();
          for (id = 1; id < numdisps; id++) {
            dpy = GetDisplayFromIndex(id);
            if (dpy && (dpy->GetDisplayType() == kBuiltIn)) {
              int result = 0;
              resp_payload.DestroyPayload();
              result = dpy->ColorSVCRequestRoute(req_payload, &resp_payload,
                                                 &pending_action);
              if (result) {
                DLOGW("Failed to dispatch action to disp %d ret %d", id, result);
                ret = result;
              }
            }
          }
          break;
        case kMultiDispGetId:
          numdisps = GetDisplayCount();
          ret = resp_payload.CreatePayloadBytes(kNumDisplays, &disp_id);
          if (ret) {
            DLOGW("Unable to create response payload!");
          } else {
            for (int i = 0; i < kNumDisplays; i++) {
              disp_id[i] = kNumDisplays;
            }
            for (id = 0; id < numdisps; id++) {
              dpy = GetDisplayFromIndex(id);
              if (dpy && (dpy->GetDisplayType() == kBuiltIn)) {
                disp_id[id] = (uint8_t)id;
              }
            }
          }
          break;
        case kSetModeFromClient:
          {
            mode_id = reinterpret_cast<int32_t *>(resp_payload.payload);
            if (mode_id) {
              ret = dpy->SetColorModeFromClientApi(*mode_id);
            } else {
              DLOGE("mode_id is Null");
              ret = -EINVAL;
            }
          }
          if (!ret) {
            dpy->RefreshWithCachedLayerstack();
          }
          break;
        default:
          DLOGW("Invalid pending action = %d!", pending_action.action);
          break;
      }
    }
  }
  // for display API getter case, marshall returned params into out_parcel.
  output_parcel->writeInt32(ret);
  SDMColorManager::MarshallStructIntoParcel(resp_payload, output_parcel);
  req_payload.DestroyPayload();
  resp_payload.DestroyPayload();

  return ret;
}

int32_t QDCMSession::notifyCallback(uint32_t command,
                                    const android::Parcel *input_parcel,
                                    android::Parcel *output_parcel) {
  int32_t status = -EINVAL;

  DLOGI("command = %d", command);
  switch (command) {

    case qService::IQService::QDCM_SVC_CMDS:
      if (!input_parcel || !output_parcel) {
        DLOGE("QService command = %d: input_parcel and output_parcel needed.", command);
        break;
      }
      status = QdcmCMDHandler(input_parcel, output_parcel);
      break;
    default:
      DLOGW("QService command = %d is not supported.", command);
      break;
  }

  return status;
}
}  // namespace sdm
#ifdef __cplusplus
}
#endif
