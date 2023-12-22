/*
* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#include <cutils/sockets.h>
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <QService.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <sys/un.h>

#include "sdm_display_color_manager.h"
#include "sdm_display_buffer_allocator.h"
#include "sdm_display.h"

#define __CLASS__ "SDMColorManager"

using snapdragoncolor::ColorMode;
using snapdragoncolor::ColorModeList;
using snapdragoncolor::RenderIntent;

namespace sdm {

const SDMQDCMModeManager::ActiveFeatureCMD SDMQDCMModeManager::kActiveFeatureCMD[] = {
    SDMQDCMModeManager::ActiveFeatureCMD("cabl:on", "cabl:off", "cabl:status", "running"),
    SDMQDCMModeManager::ActiveFeatureCMD("ad:on", "ad:off", "ad:query:status", "running"),
    SDMQDCMModeManager::ActiveFeatureCMD("svi:on", "svi:off", "svi:status", "running"),
};

const char *const SDMQDCMModeManager::kSocketName = "pps";

SDMQDCMModeManager *SDMQDCMModeManager::CreateQDCMModeMgr() {
  struct sockaddr_un addr = {};
  socklen_t alen;
  SDMQDCMModeManager *mode_mgr = new SDMQDCMModeManager();

  if (!mode_mgr) {
    DLOGW("No memory to create SDMQDCMModeManager.");
    return NULL;
  } else {
    mode_mgr->socket_fd_ = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (mode_mgr->socket_fd_ < 0) {
      // it should not be disastrous and we still can grab wakelock in QDCM mode.
      DLOGW("Unable to open dpps socket!");
    } else {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_LOCAL;
        memcpy(addr.sun_path, kSocketName, strlen(kSocketName));
        alen = strlen(addr.sun_path) + sizeof(addr.sun_family);
        if (connect(mode_mgr->socket_fd_, (struct sockaddr *) &addr, alen) < 0) {
          DLOGW("connect dpps socket server failed!");
        }
    }

    // retrieve system GPU idle timeout value for later to recover.
    mode_mgr->entry_timeout_ = UINT32(SdmDisplayDebugger::GetIdleTimeoutMs());
  }

  return mode_mgr;
}

SDMQDCMModeManager::~SDMQDCMModeManager() {
  if (socket_fd_ >= 0)
    ::close(socket_fd_);
}

int SDMQDCMModeManager::EnableActiveFeatures(bool enable,
                                             const SDMQDCMModeManager::ActiveFeatureCMD &cmds,
                                             bool *was_running) {
  int ret = 0;
  ssize_t size = 0;
  char response[kSocketCMDMaxLength] = {
      0,
  };

  if (socket_fd_ < 0) {
    DLOGW("No socket connection available - assuming dpps is not enabled");
    return 0;
  }

  if (!enable) {  // if client requesting to disable it.
    // query CABL status, if off, no action. keep the status.
    size = ::write(socket_fd_, cmds.cmd_query_status, strlen(cmds.cmd_query_status));
    if (size < 0) {
      DLOGW("Unable to send data over socket %s", ::strerror(errno));
      ret = -EFAULT;
    } else {
      size = ::read(socket_fd_, response, kSocketCMDMaxLength);
      if (size < 0) {
        DLOGW("Unable to read data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      } else if (!strncmp(response, cmds.running, strlen(cmds.running))) {
        *was_running = true;
      }
    }

    if (*was_running) {  // if was running, it's requested to disable it.
      size = ::write(socket_fd_, cmds.cmd_off, strlen(cmds.cmd_off));
      if (size < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  } else {  // if was running, need enable it back.
    if (*was_running) {
      size = ::write(socket_fd_, cmds.cmd_on, strlen(cmds.cmd_on));
      if (size < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  }

  return ret;
}

int SDMQDCMModeManager::EnableQDCMMode(bool enable, SdmDisplayProxy *sdmdisp) {
  int ret = 0;

  ret = EnableActiveFeatures((enable ? false : true), kActiveFeatureCMD[kCABLFeature],
                             &cabl_was_running_);

  // if enter QDCM mode, disable GPU fallback idle timeout.
  if (sdmdisp) {
    int inactive_ms = IDLE_TIMEOUT_INACTIVE_MS;
    Debug::Get()->GetProperty(IDLE_TIME_INACTIVE_PROP, &inactive_ms);
    uint32_t timeout = enable ? 0 : entry_timeout_;
    sdmdisp->SetIdleTimeoutMs(timeout, inactive_ms);
  }

  return ret;
}

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

int SDMColorManager::EnableQDCMMode(bool enable, SdmDisplayProxy *sdmdisplay) {
  int ret = 0;

  if (!qdcm_mode_mgr_) {
    qdcm_mode_mgr_ = SDMQDCMModeManager::CreateQDCMModeMgr();
    if (!qdcm_mode_mgr_) {
      DLOGE("Unable to create QDCM operating mode manager.");
      ret = -EFAULT;
    }
  }

  if (qdcm_mode_mgr_) {
    ret = qdcm_mode_mgr_->EnableQDCMMode(enable, sdmdisplay);
  }

  return ret;
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

int SDMColorManager::SetHWDetailedEnhancerConfig(void *params, SdmDisplayProxy *sdmdisp) {
  int err = 0;
  if (sdmdisp) {
    // Move DE config converting to display. Here to send tuning params.
    err = sdmdisp->SetHWDetailedEnhancerConfig(params);
    if (err) {
      DLOGW("SetDetailEnhancerConfig failed. err = %d", err);
    }
  }
  return err;
}

int SDMColorManager::SetDetailedEnhancer(void *params, SdmDisplayProxy *sdmdisp) {
  SCOPE_LOCK(locker_);
  int err = -1;
  err = SetHWDetailedEnhancerConfig(params, sdmdisp);
  return err;
}

DisplayError SDMColorMode::Init() {
    PopulateColorModes();
    return kErrorNone;
}

DisplayError SDMColorMode::DeInit() {
    color_mode_map_.clear();
    return kErrorNone;
}

void SDMColorMode::PopulateColorModes() {
  uint32_t color_mode_count = 0;
  // SDM returns modes which have attributes defining mode and rendering intent
  DisplayError error = display_intf_->GetColorModeCount(&color_mode_count);
  if (error != kErrorNone || (color_mode_count == 0)) {
    DLOGW("GetColorModeCount failed, use native color mode");
    color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB]
      [snapdragoncolor::kColorimetric] = "hal_native_identity";
    return;
  }

  DLOGI("Color Modes supported count = %d", color_mode_count);

  std::vector<std::string> color_modes(color_mode_count);
  error = display_intf_->GetColorModes(&color_mode_count, &color_modes);
  for (uint32_t i = 0; i < color_mode_count; i++) {
    std::string &mode_string = color_modes.at(i);
    DLOGI("Color Mode[%d] = %s", i, mode_string.c_str());
    AttrVal attr = {};
    error = display_intf_->GetColorModeAttr(mode_string, &attr);
    std::string color_gamut = kNative, dynamic_range = kSdr,
    pic_quality = kStandard, transfer = kSrgb;
    int int_render_intent = -1;
    if (!attr.empty()) {
      for (auto &it : attr) {
        if (it.first.find(kColorGamutAttribute) != std::string::npos) {
          color_gamut = it.second;
        } else if (it.first.find(kDynamicRangeAttribute) != std::string::npos) {
            dynamic_range = it.second;
        } else if (it.first.find(kPictureQualityAttribute) != std::string::npos) {
            pic_quality = it.second;
        } else if (it.first.find(kGammaTransferAttribute) != std::string::npos) {
            transfer = it.second;
        } else if (it.first.find(kRenderIntentAttribute) != std::string::npos) {
            int_render_intent = std::stoi(it.second);
        }
      }

      if (int_render_intent < 0 || int_render_intent > MAX_EXTENDED_RENDER_INTENT) {
        DLOGW("Invalid render intent %d for mode %s", int_render_intent, mode_string.c_str());
        continue;
      }
      DLOGI("color_gamut : %s, dynamic_range : %s, pic_quality : %s, "
            "render_intent : %d", color_gamut.c_str(), dynamic_range.c_str(),
            pic_quality.c_str(), int_render_intent);
      auto render_intent = static_cast<RenderIntent>(int_render_intent);
      if (color_gamut == kNative) {
        color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][render_intent] = mode_string;
      }

      if (color_gamut == kSrgb && dynamic_range == kSdr) {
        color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][render_intent] = mode_string;
      }

      if (color_gamut == kDcip3 && dynamic_range == kSdr) {
        color_mode_map_[ColorPrimaries_DCIP3][Transfer_sRGB][render_intent] = mode_string;
      }
      if (color_gamut == kBt2020) {
        if (transfer == kSt2084) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_SMPTE_ST2084]
            [snapdragoncolor::kColorimetric] = mode_string;
        } else if (transfer == kHlg) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_HLG]
            [snapdragoncolor::kColorimetric] = mode_string;
        } else if (transfer == kSrgb) {
          color_mode_map_[ColorPrimaries_BT2020][Transfer_sRGB]
            [snapdragoncolor::kColorimetric] = mode_string;
        }
      }
    } else {
        // Look at the mode names, if no attributes are found
        if (mode_string.find("hal_native") != std::string::npos) {
          color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB]
            [snapdragoncolor::kColorimetric] = mode_string;
        }
    }
  }
}

DisplayError SDMColorMode::ValidateColorMode(ColorMode color_mode) {
    ColorPrimaries primary = color_mode.gamut;
    GammaTransfer transfer = color_mode.gamma;
    RenderIntent intent = color_mode.intent;

    if (primary < ColorPrimaries_BT709_5 || primary >= ColorPrimaries_Max) {
        DLOGI("Invalid color primary: %d", primary);
        return kErrorParameters;
    }

    if (color_mode_map_.find(primary) == color_mode_map_.end()) {
        DLOGI("Could not find color primary: %d", primary);
        return kErrorNotSupported;
    }

    if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
        DLOGI("Could not find transfer %d in primary %d", transfer, primary);
        return kErrorNotSupported;
    }

    if (color_mode_map_[primary][transfer].find(intent) ==
                                        color_mode_map_[primary][transfer].end()) {
        DLOGI("Could not find intent %d in primary %d, transfer %d",
                                                    intent, primary, transfer);
        return kErrorNotSupported;
    }

    return kErrorNone;
}

DisplayError SDMColorMode::SetColorModeWithRenderIntent(ColorMode color_mode) {
    if (current_color_mode_.gamut == color_mode.gamut &&
        current_color_mode_.gamma == color_mode.gamma &&
        current_color_mode_.intent == color_mode.intent) {
        return kErrorNone;
    }

    DisplayError error = ValidateColorMode(color_mode);
    if (error != kErrorNone) {
        return error;
    }

    auto mode_string =
        color_mode_map_[color_mode.gamut][color_mode.gamma][color_mode.intent];
    error = display_intf_->SetColorMode(mode_string);
    if (error != kErrorNone) {
        DLOGE("failed for primary = %d transfer = %d intent = %d name = %s",
            color_mode.gamut, color_mode.gamma,
            color_mode.intent, mode_string.c_str());
        return kErrorNotSupported;
    }

    current_color_mode_ = color_mode;
    DLOGV("Successfully applied primary = %d transfer = %d intent = %d name = %s",
           color_mode.gamut, color_mode.gamma,
            color_mode.intent, mode_string.c_str());
    return kErrorNone;
}

ColorMode SDMColorMode::SelectBestColorSpace(bool isHdrSupported, LayerStack *layerStack) {
  snapdragoncolor::ColorMode best_color_mode = {};
  snapdragoncolor::ColorMode best_hdr_color_mode = {};

  best_color_mode.gamut = best_hdr_color_mode.gamut = ColorPrimaries_BT709_5;
  best_color_mode.gamma = best_hdr_color_mode.gamma = Transfer_sRGB;
  best_color_mode.intent = best_hdr_color_mode.intent = snapdragoncolor::kColorimetric;
  LayerStack *layer_stack = layerStack;
  for (uint32_t i = 0; i < layer_stack->layers.size(); i++) {
    struct Layer *layer = nullptr;
    struct LayerBuffer buffer = {};
    layer = layer_stack->layers.at(i);
    buffer = layer->input_buffer;

    if (layer->flags.skip) {
        continue;
    }

    snapdragoncolor::ColorMode color_mode = {};
    color_mode.gamut = buffer.color_metadata.colorPrimaries;
    color_mode.gamma = buffer.color_metadata.transfer;
    color_mode.intent = snapdragoncolor::kColorimetric;

    switch (color_mode.gamut) {
      case ColorPrimaries_DCIP3:
        best_color_mode.gamut = ColorPrimaries_DCIP3;
        best_color_mode.gamma = Transfer_sRGB;
        break;
      case ColorPrimaries_BT2020:
        if (color_mode.gamma == Transfer_sRGB) {
          best_color_mode.gamut = ColorPrimaries_BT2020;
          best_color_mode.gamma = Transfer_sRGB;
        } else if (color_mode.gamma == Transfer_SMPTE_ST2084) {
          best_hdr_color_mode = color_mode;
        } else if (color_mode.gamma == Transfer_HLG &&
          best_hdr_color_mode.gamma != Transfer_SMPTE_ST2084) {
          best_hdr_color_mode = color_mode;
        }
        break;
      default:
        break;
    }
  }
  return isHdrSupported ? best_hdr_color_mode : best_color_mode;
}

DisplayError SDMColorModeStc::Init() {
  DisplayError error = display_intf_->GetStcColorModes(&stc_mode_list_);
  if (error != kErrorNone) {
      DLOGW("Failed to get Stc color modes, error %d", error);
      stc_mode_list_.list.clear();
  } else {
      DLOGI("Stc mode count %zu", stc_mode_list_.list.size());
  }
  PopulateColorModes();
  return kErrorNone;
}


DisplayError SDMColorModeStc::DeInit() {
  stc_mode_list_.list.clear();
  color_mode_map_.clear();
  return kErrorNone;
}


void SDMColorModeStc::PopulateColorModes() {
  if (stc_mode_list_.list.size() == 0) {
    ColorMode default_mode;
    default_mode.gamut = ColorPrimaries_BT709_5;
    default_mode.gamma = Transfer_sRGB;
    default_mode.intent = snapdragoncolor::kColorimetric;
    color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB]
      [snapdragoncolor::kColorimetric] = default_mode;
    return;
  }

  for (uint32_t i = 0; i < stc_mode_list_.list.size(); i++) {
    snapdragoncolor::ColorMode stc_mode = stc_mode_list_.list[i];
    ColorPrimaries gamut = static_cast<ColorPrimaries>(stc_mode.gamut);
    GammaTransfer gamma = static_cast<GammaTransfer>(stc_mode.gamma);
    RenderIntent intent = static_cast<RenderIntent>(stc_mode.intent);
    color_mode_map_[gamut][gamma][intent] = stc_mode;
    DLOGI("SDMColorModeStc::PopulateColorModes color mode[%d]: gamut %d gamma %d intent %d",
            i,stc_mode.gamut, stc_mode.gamma, stc_mode.intent);
  }
}

DisplayError SDMColorModeStc::ValidateColorMode(ColorMode color_mode) {
    ColorPrimaries primary = color_mode.gamut;
    GammaTransfer transfer = color_mode.gamma;
    RenderIntent intent = color_mode.intent;

    if (primary < ColorPrimaries_BT709_5 || primary >= ColorPrimaries_Max) {
        DLOGE("Invalid color primary: %d", primary);
        return kErrorParameters;
    }

    if (color_mode_map_.find(primary) == color_mode_map_.end()) {
        DLOGE("Could not find color primary: %d", primary);
        return kErrorNotSupported;
    }

    if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
        DLOGE("Could not find transfer %d in primary %d", transfer, primary);
        return kErrorNotSupported;
    }

    if (color_mode_map_[primary][transfer].find(intent) ==
                                        color_mode_map_[primary][transfer].end()) {
        DLOGE("Could not find intent %d in primary %d, transfer %d",
            intent, primary, transfer);
        return kErrorNotSupported;
    }

    return kErrorNone;
}


DisplayError SDMColorModeStc::SetColorModeWithRenderIntent(ColorMode color_mode) {
    if (current_color_mode_.gamut == color_mode.gamut &&
        current_color_mode_.gamma == color_mode.gamma &&
        current_color_mode_.intent == color_mode.intent) {
        return kErrorNone;
    }

    DisplayError error = ValidateColorMode(color_mode);
    if (error != kErrorNone) {
        return error;
    }

    snapdragoncolor::ColorMode stc_mode = \
                color_mode_map_[color_mode.gamut][color_mode.gamma][color_mode.intent];
    error = display_intf_->SetStcColorMode(stc_mode);
    if (error != kErrorNone) {
        DLOGE("Failed to apply Stc color mode: gamut %d gamma %d intent %d err %d",
            stc_mode.gamut, stc_mode.gamma, stc_mode.intent, error);
        return kErrorNotSupported;
    }

    current_color_mode_ = color_mode;
    DLOGI("Successfully applied mode gamut = %d, gamma = %d, intent = %d",
           color_mode.gamut, color_mode.gamma, color_mode.intent);
    return kErrorNone;
}

ColorMode SDMColorModeStc::SelectBestColorSpace(bool isHdrSupported, LayerStack *layerStack) {
  snapdragoncolor::ColorMode best_color_mode =
      color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][snapdragoncolor::kColorimetric];

  // for DSI, if panel support P3 then always works in P3 area.
  if (color_mode_map_.find(ColorPrimaries_DCIP3) != color_mode_map_.end()) {
      best_color_mode = color_mode_map_[ColorPrimaries_DCIP3][Transfer_sRGB]
                                       [snapdragoncolor::kColorimetric];
  }

  return best_color_mode;
}
}  // namespace sdm
