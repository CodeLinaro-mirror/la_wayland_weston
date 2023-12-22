/*
* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "sdm_display.h"
#include "sdm-service/sdm_display_buffer_allocator.h"

#define DISPLAY_API_INTERFACE_LIBRARY_NAME "libsdm-disp-vndapis.so"
#define DISPLAY_API_FUNC_TABLES "display_color_apis_ftables"

using std::fstream;
using snapdragoncolor::ColorMode;
using snapdragoncolor::ColorModeList;
using snapdragoncolor::RenderIntent;

typedef class BufferAllocator BufferAllocator;
typedef class DynLib DynLib;

namespace sdm {

class SdmDisplayProxy;
class SDMQDCMModeManager {
 public:
  static const uint32_t kSocketCMDMaxLength = 4096;
  enum ActiveFeatureID {
    kCABLFeature,
    kADFeature,
    kSVIFeature,
    kMaxNumActiveFeature,
  };

  struct ActiveFeatureCMD {
    const char *cmd_on = NULL;
    const char *cmd_off = NULL;
    const char *cmd_query_status = NULL;
    const char *running = NULL;
    ActiveFeatureCMD(const char *arg1, const char *arg2, const char *arg3,
                     const char *arg4) : cmd_on(arg1), cmd_off(arg2),
                     cmd_query_status(arg3), running(arg4) {}
  };

  static const ActiveFeatureCMD kActiveFeatureCMD[kMaxNumActiveFeature];

  static SDMQDCMModeManager *CreateQDCMModeMgr();
  ~SDMQDCMModeManager();
  int EnableQDCMMode(bool enable, SdmDisplayProxy *sdmdisplay);

 protected:
  int EnableActiveFeatures(bool enable);
  int EnableActiveFeatures(bool enable, const ActiveFeatureCMD &cmds, bool *was_running);
 private:
  bool cabl_was_running_ = false;
  int socket_fd_ = -1;
  uint32_t entry_timeout_ = 0;
  static const char *const kSocketName;
};

class SDMColorManager {
 public:
  static SDMColorManager *CreateColorManager(BufferAllocator *buffer_allocator);
  static int CreatePayloadFromParcel(const android::Parcel &in, uint32_t *disp_id,
                                     struct PPDisplayAPIPayload *sink);
  static void MarshallStructIntoParcel(const struct PPDisplayAPIPayload &data,
                                       android::Parcel *out_parcel);

  explicit SDMColorManager(BufferAllocator *buffer_allocator);
  int EnableQDCMMode(bool enable, SdmDisplayProxy *sdmdisplay);
  int SetDetailedEnhancer(void *params, SdmDisplayProxy *sdmdisplay);
  int SetHWDetailedEnhancerConfig(void *params, SdmDisplayProxy *sdmdisplay);
  ~SDMColorManager();
  void DestroyColorManager();
 private:
  DynLib color_apis_lib_;
  void *color_apis_ = NULL;
  BufferAllocator *buffer_allocator_ = NULL;
  BufferInfo buffer_info = {};
  SDMQDCMModeManager *qdcm_mode_mgr_ = NULL;
  Locker locker_;
};

class SDMColorMode {
 public:
  explicit SDMColorMode(DisplayInterface *display_intf) : display_intf_(display_intf) {};
  virtual ~SDMColorMode() {}

  virtual DisplayError Init();
  virtual DisplayError DeInit();

  virtual DisplayError SetColorModeWithRenderIntent(ColorMode color_mode);
  virtual ColorMode SelectBestColorSpace(bool isHdrSupported, LayerStack *layerStack);
 protected:
  DisplayInterface *display_intf_ = NULL;
 private:
  void PopulateColorModes();
  DisplayError ValidateColorMode(ColorMode color_mode);

  typedef std::map<snapdragoncolor::RenderIntent, std::string> RenderIntentMap;
  typedef std::map<GammaTransfer, RenderIntentMap> GammaTransferMap;
  // <ColorPrimaries, GammaTransfer, RenderIntent> = ColorModeString
  std::map<ColorPrimaries, GammaTransferMap> color_mode_map_ = {};

  ColorMode current_color_mode_;
};


class SDMColorModeStc : public SDMColorMode {
 public:
  SDMColorModeStc(DisplayInterface *display_intf) : SDMColorMode(display_intf) {}
  ~SDMColorModeStc() {}

  DisplayError Init() override;
  DisplayError DeInit() override;

  DisplayError SetColorModeWithRenderIntent(ColorMode color_mode) override;
  ColorMode SelectBestColorSpace(bool isHdrSupported, LayerStack *layerStack) override;
 private:
    void PopulateColorModes();
    DisplayError ValidateColorMode(ColorMode color_mode);

    typedef std::map<snapdragoncolor::RenderIntent, ColorMode> RenderIntentMap;
    typedef std::map<GammaTransfer, RenderIntentMap> GammaTransferMap;
    // <ColorPrimaries, GammaTransfer, RenderIntent> = ColorMode
    std::map<ColorPrimaries, GammaTransferMap> color_mode_map_ = {};

    ColorMode current_color_mode_ = {};
    ColorModeList stc_mode_list_ = {};
};

} // namespace sdm
#endif // SDM_DISPLAY_COLOR_MANAGER_H
