/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#include <cutils/sockets.h>
#include <utils/String16.h>
#include <binder/Parcel.h>

#include <utils/constants.h>
#include <utils/debug.h>
#include <sys/un.h>

#include "sdm_display_color_manager.h"
#include "sdm_display_buffer_allocator.h"
#include "sdm_display.h"
#include "sdm_display_debugger.h"

#include <map>
#include <string>
#include <vector>
#include <algorithm>

#define __CLASS__ "SDMColorManager"

#define DISPLAY_CM_DEBUG_PROP "vendor.display.colormode.debug"

using snapdragoncolor::ColorMode;
using snapdragoncolor::ColorModeList;
using snapdragoncolor::RenderIntent;

namespace sdm {

DisplayError SDMColorModeMgr::Init() {
    PopulateColorModes();
    return kErrorNone;
}

DisplayError SDMColorModeMgr::DeInit() {
    color_mode_map_.clear();
    return kErrorNone;
}

void SDMColorModeMgr::PopulateColorModes() {
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

DisplayError SDMColorModeMgr::ValidateColorMode(ColorMode color_mode) {
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

DisplayError SDMColorModeMgr::SetColorModeWithRenderIntent(ColorMode color_mode) {
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
    DLOGI("Successfully applied primary = %d transfer = %d intent = %d name = %s",
           color_mode.gamut, color_mode.gamma,
            color_mode.intent, mode_string.c_str());
    return kErrorNone;
}

ColorMode SDMColorModeMgr::SelectBestColorSpace(bool isHdrSupported, LayerStack *layerStack) {
  snapdragoncolor::ColorMode best_color_mode = {};
  snapdragoncolor::ColorMode best_hdr_color_mode = {};
  snapdragoncolor::ColorMode final_color_mode = {};

  best_color_mode.gamut = best_hdr_color_mode.gamut = ColorPrimaries_BT709_5;
  best_color_mode.gamma = best_hdr_color_mode.gamma = Transfer_sRGB;
  best_color_mode.intent = best_hdr_color_mode.intent = snapdragoncolor::kNative;
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
    color_mode.gamut = static_cast<ColorPrimaries>(buffer.dataspace.colorPrimaries);
    color_mode.gamma = static_cast<GammaTransfer>(buffer.dataspace.transfer);
    color_mode.intent = snapdragoncolor::kNative;

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

  final_color_mode = (isHdrSupported ? best_hdr_color_mode : best_color_mode);
  if (ValidateColorMode(final_color_mode) != kErrorNone) {
    final_color_mode.intent = snapdragoncolor::kColorimetric;
  }

  return final_color_mode;
}

DisplayError SDMColorModeMgr::RestoreColorTransform() {
  DisplayError error = display_intf_->SetColorTransform(kColorTransformMatrixCount, color_matrix_);
  if (error != kErrorNone) {
    DLOGE("Failed to set Color Transform");
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError SDMColorModeMgr::SetPreferredColorModeInternal(const std::string &mode_string,
                                                         bool from_client, ColorMode *color_mode,
                                                         DynamicRangeType *dynamic_range) {
  DisplayError error = kErrorNone;
  ColorMode mode = {};
  DynamicRangeType range = kSdrType;

  if (from_client) {
    // get blend space and dynamic range of the mode
    AttrVal attr;
    std::string color_gamut_string, dynamic_range_string;
    error = display_intf_->GetColorModeAttr(mode_string, &attr);
    if (error) {
      DLOGE("Failed to get mode attributes for mode %s", mode_string.c_str());
      return kErrorParameters;
    }

    if (!attr.empty()) {
      for (auto &it : attr) {
        if (it.first.find(kColorGamutAttribute) != std::string::npos) {
          color_gamut_string = it.second;
        } else if (it.first.find(kDynamicRangeAttribute) != std::string::npos) {
          dynamic_range_string = it.second;
        }
      }
    }

    if (color_gamut_string.empty() || dynamic_range_string.empty()) {
      DLOGE("Invalid attributes for mode %s: color_gamut = %s, dynamic_range = %s",
            mode_string.c_str(), color_gamut_string.c_str(), dynamic_range_string.c_str());
      return kErrorParameters;
    }

    if (color_gamut_string == kDcip3) {
      mode.gamut = ColorPrimaries_DCIP3;
      mode.gamma = Transfer_sRGB;
    } else if (color_gamut_string == kSrgb) {
      mode.gamut = ColorPrimaries_BT709_5;
      mode.gamma = Transfer_sRGB;
    }

    mode.intent = snapdragoncolor::kColorimetric;

    if (dynamic_range_string == kHdr) {
      range = kHdrType;
    }

    if (color_mode) {
      *color_mode = mode;
    }
    if (dynamic_range) {
      *dynamic_range = range;
    }
  }

  // apply the mode from client if it matches
  // the current blend space and dynamic range,
  // skip the check for the mode from SF.
  if ((!from_client) || ((current_color_mode_.gamut == mode.gamut) &&
     (curr_dynamic_range_ == range))) {
    DLOGI("Applying mode: %s", mode_string.c_str());
    error = display_intf_->SetColorMode(mode_string);
    if (error != kErrorNone) {
      DLOGE("Failed to apply mode: %s", mode_string.c_str());
      return kErrorParameters;
    }
  }

  return kErrorNone;
}

DisplayError SDMColorModeMgr::SetColorModeFromClientApi(std::string mode_string) {
  ColorMode mode = {};
  DynamicRangeType range = kSdrType;

  auto error = SetPreferredColorModeInternal(mode_string, true, &mode, &range);
  if (error == kErrorNone) {
    preferred_mode_[mode.gamut][mode.gamma][range] = mode_string;
    DLOGV_IF(kTagClient, "Put mode %s (range %d) into preferred_mode",
             mode_string.c_str(), range);
  }

  return error;
}

DisplayError SDMColorModeStc::Init() {
  DisplayError error = display_intf_->GetStcColorModes(&stc_mode_list_);
  if (error != kErrorNone) {
      DLOGW("Failed to get Stc color modes, error %d", error);
      stc_mode_list_.list.clear();
  } else {
      DLOGI("Stc mode count %zu", stc_mode_list_.list.size());
  }

  is_primary_display_ = display_intf_->IsPrimaryDisplay();
  SdmDisplayDebugger::Get()->GetProperty(DISPLAY_CM_DEBUG_PROP, &color_mode_debug_);

  DLOGI("is_primary_display_ = %d debug mode = %d", is_primary_display_, color_mode_debug_);

  PopulateColorModes();
  return kErrorNone;
}


DisplayError SDMColorModeStc::DeInit() {
  stc_mode_list_.list.clear();
  color_mode_map_.clear();
  return kErrorNone;
}

DynamicRangeType SDMColorModeStc::GetDynamicMode(ColorMode color_mode) {
  DynamicRangeType dynamic_range = kSdrType;

  if (std::find(color_mode.hw_assets.begin(), color_mode.hw_assets.end(),
      snapdragoncolor::kPbHdrBlob) != color_mode.hw_assets.end()) {
      dynamic_range = kHdrType;
  }

  return dynamic_range;
}

void SDMColorModeStc::PopulateColorModes() {
  if (stc_mode_list_.list.size() == 0) {
    ColorMode default_mode;
    default_mode.gamut = ColorPrimaries_BT709_5;
    default_mode.gamma = Transfer_sRGB;
    default_mode.intent = snapdragoncolor::kColorimetric;
    color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB]
      [snapdragoncolor::kColorimetric][kSdrType] = default_mode;
    return;
  }

  for (uint32_t i = 0; i < stc_mode_list_.list.size(); i++) {
    snapdragoncolor::ColorMode stc_mode = stc_mode_list_.list[i];
    ColorPrimaries gamut = static_cast<ColorPrimaries>(stc_mode.gamut);
    GammaTransfer gamma = static_cast<GammaTransfer>(stc_mode.gamma);
    RenderIntent intent = static_cast<RenderIntent>(stc_mode.intent);
    DynamicRangeType dynamic = GetDynamicMode(stc_mode);

    color_mode_map_[gamut][gamma][intent][dynamic] = stc_mode;
    DLOGI("SDMColorModeStc::PopulateColorModes color mode[%d]: gamut %d gamma %d intent %d dyn %d",
            i,stc_mode.gamut, stc_mode.gamma, stc_mode.intent, dynamic);
  }
}

DisplayError SDMColorModeStc::ValidateColorMode(ColorMode color_mode) {
    ColorPrimaries primary = color_mode.gamut;
    GammaTransfer transfer = color_mode.gamma;
    RenderIntent intent = color_mode.intent;
    DynamicRangeType dynamic = GetDynamicMode(color_mode);

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

    if (color_mode_map_[primary][transfer][intent].find(dynamic) ==
                                        color_mode_map_[primary][transfer][intent].end()) {
        DLOGE("Could not find dynamic_type %d in primary %d, transfer %d, dynamic %d",
            intent, primary, transfer, dynamic);
        return kErrorNotSupported;
    }

    return kErrorNone;
}


DisplayError SDMColorModeStc::SetColorModeWithRenderIntent(ColorMode color_mode) {
    DynamicRangeType dynamic_cur = GetDynamicMode(current_color_mode_);
    DynamicRangeType dynamic_new = GetDynamicMode(color_mode);

    if (current_color_mode_.gamut == color_mode.gamut &&
        current_color_mode_.gamma == color_mode.gamma &&
        current_color_mode_.intent == color_mode.intent &&
        dynamic_cur == dynamic_new) {
        return kErrorNone;
    }

    DisplayError error = ValidateColorMode(color_mode);
    if (error != kErrorNone) {
        return error;
    }

    snapdragoncolor::ColorMode stc_mode = \
                color_mode_map_[color_mode.gamut][color_mode.gamma][color_mode.intent][dynamic_new];
    error = display_intf_->SetStcColorMode(stc_mode);
    if (error != kErrorNone) {
        DLOGE("Failed to apply Stc color mode: gamut %d gamma %d intent %d dyn %d err %d",
            stc_mode.gamut, stc_mode.gamma, stc_mode.intent, dynamic_new, error);
        return kErrorNotSupported;
    }

    current_color_mode_ = color_mode;
    DLOGI("Successfully applied mode gamut = %d, gamma = %d, intent = %d dyn = %d",
           color_mode.gamut, color_mode.gamma, color_mode.intent, dynamic_new);
    return kErrorNone;
}


ColorMode SDMColorModeStc::SelectBestColorSpace(bool isHdrMode, LayerStack *layerStack) {
  snapdragoncolor::ColorMode best_color_mode =
      color_mode_map_[ColorPrimaries_BT709_5]
      [Transfer_sRGB][snapdragoncolor::kColorimetric][kSdrType];
  int primaries = ColorPrimaries_BT709_5;
  int transfer = Transfer_sRGB;
  int intent = snapdragoncolor::kColorimetric;
  int dynamic = kSdrType;

  if (color_mode_debug_) {
    if (is_primary_display_) {
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.primaries", &primaries);
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.transfer", &transfer);
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.intent", &intent);
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.dynamic", &dynamic);
      DLOGI("Change primary display color mode");
    } else {
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.second.primaries", &primaries);
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.second.transfer", &transfer);
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.second.intent", &intent);
      SdmDisplayDebugger::Get()->GetProperty("vendor.display.second.dynamic", &dynamic);
      DLOGI("Change secondary display color mode");
    }

    DLOGI("Use color mode property primaries %d transfer %d intent %d dynamic %d",
                                              primaries, transfer, intent, dynamic);
    best_color_mode = color_mode_map_[static_cast<ColorPrimaries>(primaries)]
                                     [static_cast<GammaTransfer>(transfer)]
                                     [static_cast<snapdragoncolor::RenderIntent>(intent)]
                                     [static_cast<sdm::DynamicRangeType>(dynamic)];
  } else {
    // for DSI, if panel support P3 then always works in P3 area.
    if (color_mode_map_.find(ColorPrimaries_DCIP3) != color_mode_map_.end()) {
      if (isHdrMode) dynamic = kHdrType;
      best_color_mode = color_mode_map_[ColorPrimaries_DCIP3][Transfer_sRGB]
                                    [snapdragoncolor::kColorimetric]
                                    [static_cast<sdm::DynamicRangeType>(dynamic)];
    }
  }

  DLOGI("Select gamut = %d, gamma = %d, intent = %d dyn = %d",
      best_color_mode.gamut, best_color_mode.gamma, best_color_mode.intent, dynamic);

  return best_color_mode;
}
}  // namespace sdm
