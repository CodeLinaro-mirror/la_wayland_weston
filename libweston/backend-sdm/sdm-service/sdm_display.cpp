/*
* Copyright (c) 2017, 2021 The Linux Foundation. All rights reserved.
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
*/
/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <assert.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <string>
#include <vector>
#include <map>
#include <ostream>
#include <fstream>
#include <utility>
#include <bitset>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sstream>

#include "sdm-service/sdm_display.h"
#include "sdm-service/uevent.h"

#include "sdm-internal.h"
#include "gbm-buffer-backend.h"

#define __CLASS__ "SdmDisplay"
extern "C" void NotifyOnRefresh(struct drm_output *);
extern "C" void NotifyOnQdcmRefresh(struct drm_output *);

namespace sdm {
#define GET_GPU_TARGET_SLOT(max_layers) ((max_layers) - 1)
/* Cursor is fixed in (gpu_target_index-1) slot in SDM */
#define GET_CURSOR_SLOT(max_layers) ((max_layers) - 2)
#define LEN_LOCAL 2048

#define SDM_DISPLAY_DUMP_LAYER_STACK 0

#define SDM_DEAFULT_NULL_DISPLAY_WIDTH 1920
#define SDM_DEAFULT_NULL_DISPLAY_HEIGHT 1080
#define SDM_DEAFULT_NULL_DISPLAY_FPS 60
#define SDM_DEAFULT_NULL_DISPLAY_X_DPI 25.4
#define SDM_DEAFULT_NULL_DISPLAY_Y_DPI 25.4
#define SDM_DEAFULT_NULL_DISPLAY_IS_YUV false

#define MAX_PROP_STR_SIZE 64
#define SDM_NULL_DISPLAY_RESOLUTON_PROP_NAME "weston.sdm.default.resolution"
#define SDM_DISABLE_HDR_HANDLING "vendor.display.disable_hdr"
#define SDM_DISABLE_HDR_TM "vendor.display.disable_hdr_tm"
#define SDM_DUMP_CONFIG "vendor.display.dump_config"

SdmDisplay::SdmDisplay(DisplayType type, CoreInterface *core_intf,
                                         SdmDisplayBufferAllocator *buffer_allocator) {
    display_type_ = type;
    core_intf_    = core_intf;
    buffer_allocator_ = buffer_allocator;
    drm_output_   = NULL;
    vblank_cb_    = NULL;

    cwb_config_.tap_point = kLmTapPoint;
    cwb_config_.cwb_roi.left = FLOAT(0);
    cwb_config_.cwb_roi.top = FLOAT(0);
    cwb_config_.cwb_roi.right = FLOAT(0);
    cwb_config_.cwb_roi.bottom = FLOAT(0);
    cwb_config_.pu_as_cwb_roi = false;

    output_buffer_.buffer_id = -1;
}

SdmDisplay::~SdmDisplay() {
    FreeLayerStack();
}

const char * SdmDisplay::FourccToString(uint32_t fourcc)
{
    static __thread char s[4];
    uint32_t fmt = htole32(fourcc);

    memcpy(s, &fmt, 4);

    return s;
}

DisplayError SdmDisplay::CreateDisplay(uint32_t display_id) {
    DisplayError error = kErrorNone;
    char property[MAX_PROP_STR_SIZE] = {0};
    struct DisplayHdrInfo display_hdr_info = {};

    error = core_intf_->CreateDisplay(display_type_, this, &display_intf_);

    if (error != kErrorNone) {
        DLOGE("Display creation failed. Error = %d", error);

        return error;
    }

    SdmDisplayDebugger::Get()->GetProperty(SDM_DISABLE_HDR_HANDLING, property);
    if (std::string(property)==std::string("1")) {
        disable_hdr_handling_ = 1;
    }

    GetHdrInfo(&display_hdr_info);
    if (display_hdr_info.hdr_supported) {
        DLOGI("Display device supports HDR functionality");
    } else {
        DLOGI("Display device doesn't support HDR functionality");
    }

    if (disable_hdr_handling_) {
        DLOGI("HDR Handling disabled");
    } else {
        SdmDisplayDebugger::Get()->GetProperty(SDM_DISABLE_HDR_TM, property);
        if (std::string(property)==std::string("0")) {
            disable_tone_mapper_ = 0;
        }
        #ifndef HAS_HDR_SUPPORT
            disable_tone_mapper_ = 1;
        #endif
        if (!disable_tone_mapper_) {
            DLOGI("Tone Mapper Enabled");
            tone_mapper_ = new SdmDisplayToneMapper(buffer_allocator_);

            if (!tone_mapper_) {
                DLOGE("Failed to create tone_mapper instance");
            }
        }
    }

    DLOGI("display_id = %d display_type_=%d", display_id, display_type_);
    if (display_type_ == kBuiltIn) {
        display_colormode_ = new SDMColorModeStc(display_intf_);
    } else {
        display_colormode_ = new SDMColorMode(display_intf_);
    }

    display_colormode_->Init();

    frame_dumper_ = new SdmFrameDumper(display_id, GetDisplayString(), buffer_allocator_);

    return kErrorNone;
}

DisplayError SdmDisplay::DestroyDisplay() {
    DisplayError error = kErrorNone;

    delete frame_dumper_;
    frame_dumper_ = nullptr;
    display_colormode_->DeInit();
    delete display_colormode_;
    display_colormode_ = NULL;
    error = core_intf_->DestroyDisplay(display_intf_);
    display_intf_ = NULL;

    if (tone_mapper_) {
        delete tone_mapper_;
        tone_mapper_ = nullptr;
    }

    return error;
}

DisplayError SdmDisplay::VSync(const DisplayEventVSync &vsync) {
    DTRACE_SCOPED();
    if (vblank_cb_)
      vblank_cb_(display_id_, vsync.timestamp, drm_output_);
    else
      DLOGE("vsync not registered");

    return kErrorNone;
}

DisplayError SdmDisplay::VSync(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, void *data) {

    DLOGW("Not implemented\n");

    return kErrorNone;
}

DisplayError SdmDisplay::PFlip(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, void *data) {

    DLOGW("Not implemented");
    return kErrorNone;
}

DisplayError SdmDisplay::Refresh() {
    if (client_event_handler_) {
        client_event_handler_->Refresh();
    }

    return kErrorNone;
}

DisplayError SdmDisplay::CECMessage(char *message) {
    DLOGW("Not implemented");

    return kErrorNone;
}

DisplayError SdmDisplay::SetDisplayState(DisplayState state, bool teardown,
                                         shared_ptr<Fence> *release_fence) {
    DisplayError error;

    error = display_intf_->SetDisplayState(state, teardown, release_fence);
    if (error != kErrorNone) {
        DLOGE("function failed. Error = %d", error);
        return error;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::HistogramEvent(int /* fd */, uint32_t /* blob_fd */) {
  return kErrorNone;
}

void SdmDisplay::RefreshCallback()
{
    if (drm_output_) {
      NotifyOnQdcmRefresh(drm_output_);
    }
}

void SdmDisplay::RefreshWithCachedLayerstack()
{
    DisplayError error = kErrorNone;
    layer_stack_ = prev_layer_stack_;
    error = display_intf_->Prepare(&layer_stack_);
    if (error != kErrorNone) {
        DLOGE("Prepare failed with error %d", error);
        return;
    }

    error = display_intf_->Commit(&layer_stack_);
    if (error != kErrorNone) {
        DLOGE("Commit failed with error %d", error);
        return;
    } else{
        PostCommit();
        NotifyOnRefresh(prev_output_);
    }
}

void SdmDisplay::HandlePanelDead()
{
    uint32_t i = 0;
    //TODO(user): extend for multi display if needed. currently handle for primary display.
    if (display_type_ != kPrimary) {
      DLOGE("Current display is not primary");
      return;
    }

    esd_reset_panel_ = true;
    shared_ptr<Fence> *release_fence;
    DisplayError error = kErrorNone;

    DisplayState last_display_state = {};
    error = display_intf_->GetDisplayState(&last_display_state);
    if (error != kErrorNone) {
        DLOGE("Failed to get last display state with error %d", error);
        return;
    }

    error = SetDisplayState(kStateOff, true, release_fence);
    if (error != kErrorNone) {
        DLOGE("Failed to power off the display with error %d", error);
        return;
    }

    error = SetDisplayState(last_display_state, false, release_fence);
    if (error != kErrorNone) {
        DLOGE("Failed to SetDisplayState with error %d", error);
        return;
    }

    do {
        error = display_intf_->SetVSyncState(true);
        if (error != kErrorNone) {
            usleep(1000);
            i++;
        }
    } while (error && i < 16);

    if (error) {
        DLOGE("Failed to SetVSyncState  with error %d", error);
    }

    RefreshWithCachedLayerstack();
    esd_reset_panel_ = false;
}

DisplayError SdmDisplay::HandleEvent(DisplayEvent event) {
    switch (event) {
        case kPanelDeadEvent:
            HandlePanelDead();
            break;
        default:
            DLOGW("Unknown event: %d", event);
            break;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::SetVSyncState(bool VSyncState, struct drm_output *output) {
    DisplayError error;

    if (!output) {
        DLOGE("No output to set VSync");
        return kErrorNone;
    }

    drm_output_ = output;
    error = display_intf_->SetVSyncState(VSyncState);
    if ((error != kErrorNone) && (display_type_ != kVirtual)) {
        DLOGE("VSync state setting failed. Error = %d", error);
        return error;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::SetPanelBrightness(float brightness) {
    return display_intf_->SetPanelBrightness(brightness);
}

DisplayError SdmDisplay::GetPanelBrightness(float *brightness) {
    return display_intf_->GetPanelBrightness(brightness);
}

DisplayError SdmDisplay::GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
    DisplayError error = kErrorNone;
    DisplayConfigVariableInfo disp_config;
    uint32_t active_index = 0;

    error = display_intf_->GetActiveConfig(&active_index);

    if (error != kErrorNone) {
        DLOGE("Active Index not found. Error = %d", error);
        return error;
    }

    error = display_intf_->GetConfig(active_index, &disp_config);

    if (error != kErrorNone) {
        DLOGE("Display Configuration failed. Error = %d", error);
        return error;
    }

    display_config->x_pixels     = disp_config.x_pixels;
    display_config->y_pixels     = disp_config.y_pixels;
    display_config->x_dpi        = disp_config.x_dpi;
    display_config->y_dpi        = disp_config.y_dpi;
    display_config->fps          = disp_config.fps;
    display_config->vsync_period_ns = disp_config.vsync_period_ns;
    display_config->is_yuv       = disp_config.is_yuv;
    fps_                         = disp_config.fps;
    display_config->is_connected = true;

    DLOGD("[%s] get W*H(%dx%d)", __FUNCTION__, display_config->x_pixels, display_config->y_pixels);

    return kErrorNone;
}

DisplayError SdmDisplay::SetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
    DisplayError error = kErrorNone;
    DisplayConfigVariableInfo disp_config;
    uint32_t active_index = 0;

    disp_config.x_pixels        = display_config->x_pixels;
    disp_config.y_pixels        = display_config->y_pixels;
    disp_config.x_dpi           = display_config->x_dpi;
    disp_config.y_dpi           = display_config->y_dpi;
    disp_config.fps             = display_config->fps;
    disp_config.vsync_period_ns = display_config->vsync_period_ns;
    disp_config.is_yuv          = display_config->is_yuv;

    error = display_intf_->SetActiveConfig(&disp_config);
    if (error != kErrorNone) {
        DLOGE("Set active config. Error = %d", error);
        return error;
    }

    DLOGD("[%s] set W*H(%dx%d)", __FUNCTION__, display_config->x_pixels, display_config->y_pixels);

    return kErrorNone;
}

void SdmDisplay::ParseAndSetDumpConfig() {
    std::stringstream ss;
    std::string token, dump_config;
    char property[MAX_PROP_STR_SIZE] = "";
    int index = 0;
    uint32_t display_id, frame_count;
    int32_t output_format;
    DumpMode dump_mode = INPUT_LAYER_DUMP;
    std::vector<int> configs;
    DisplayError error = kErrorNone;

    SdmDisplayDebugger::Get()->GetProperty(SDM_DUMP_CONFIG, property);
    dump_config = property;

    if (!dump_config.size()) {
        return;
    } else {
        DLOGD("Dump config = %s\n", property);
        ss.str(dump_config);
    }

    while (std::getline(ss, token, ',')) {
        configs.push_back(std::stoi(token));
    }

    display_id = configs[index++];
    //Check if it is specifc CWB display ID
    if (display_id != display_id_)
        return;

    SdmDisplayDebugger::Get()->SetProperty(SDM_DUMP_CONFIG, nullptr);

    error = frame_dumper_->CreateDumpDir();
    if (error != kErrorNone) {
        DLOGE("Failed to create dump dir");
        return;
    }

    frame_count = configs[index++];
    dump_mode = static_cast<DumpMode>(configs[index++]);

    if (dump_mode == INPUT_LAYER_DUMP) {
        dump_frame_count_ = frame_count;
        dump_input_layers_ = true;
        return;
    } else if (dump_mode == OUTPUT_LAYER_DUMP) {
        output_format = configs[index++];
        CwbConfig cwb_config = {};
        cwb_config.tap_point = static_cast<CwbTapPoint>(configs[index++]);

        //Optional CWB config
        cwb_config.pu_as_cwb_roi = static_cast<bool>(configs[index++]);
        LayerRect &cwb_roi = cwb_config.cwb_roi;
        if (configs.size() != index)
            cwb_roi.left = static_cast<float>(configs[index++]);
        if (configs.size() != index)
            cwb_roi.top = static_cast<float>(configs[index++]);
        if (configs.size() != index)
            cwb_roi.right = static_cast<float>(configs[index++]);
        if (configs.size() != index)
            cwb_roi.bottom = static_cast<float>(configs[index++]);

        error = SetFrameDumpConfig(frame_count, cwb_config, output_format);
        if (error != kErrorNone) {
            DLOGE("Failed to set frame dump config");
            return;
        }
    } else {
        DLOGE("Invalid dump mode set");
        return;
    }
}

DisplayError SdmDisplay::SetOutputBuffer(void *buf, shared_ptr<Fence> &release_fence) {
    struct gbm_bo *bo = NULL;
    int data_fd = -1;
    uint32_t width = 0, height = 0;
    uint32_t alignedWidth = 0;
    uint32_t alignedHeight = 0;
    uint32_t secure_status = 0;
    void *color_meta = reinterpret_cast<void *>(&output_buffer_.color_metadata);
    int gbm_format = GBM_FORMAT_XBGR8888;
    int sdm_format = SDM_BUFFER_FORMAT_INVALID;
    struct LayerGeometryFlags ubwc_flags;
    uint32_t ubwc_status = 0;

    if (buf == nullptr) {
        DLOGE("[%s] configed buffer is null",__FUNCTION__);
        return kErrorParameters;
    }

    bo = static_cast<struct gbm_bo *>(buf);

    data_fd = gbm_bo_get_fd(bo);

    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);

    gbm_format = gbm_bo_get_format(bo);

    gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_WIDTH, bo, &alignedWidth);
    gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_HEIGHT, bo, &alignedHeight);
    gbm_perform(GBM_PERFORM_GET_SECURE_BUFFER_STATUS, bo, &secure_status);
    gbm_perform(GBM_PERFORM_GET_UBWC_STATUS, bo, &ubwc_status);
    gbm_perform(GBM_PERFORM_GET_METADATA, bo, GBM_METADATA_GET_COLOR_METADATA, color_meta);

    ubwc_flags.has_ubwc_buf = ubwc_status;

    sdm_format = GetMappedFormatFromGbm(gbm_format);

    // output_buffer_ update. for present/validate or other.
    output_buffer_.flags.secure = secure_status;
    output_buffer_.flags.video = GetVideoPresenceByFormatFromGbm(gbm_format);
    output_buffer_.format = GetSDMFormat(sdm_format, ubwc_flags.has_ubwc_buf);
    output_buffer_.buffer_id = reinterpret_cast<uint64_t>(bo);
    output_buffer_.handle_id = bo->ion_fd;

    output_buffer_.unaligned_width = width;
    output_buffer_.unaligned_height = height;
    output_buffer_.width = alignedWidth;
    output_buffer_.height = alignedHeight;

    output_buffer_.planes[0].fd = data_fd;
    output_buffer_.planes[0].offset = 0;
    output_buffer_.planes[0].stride = width;

    output_buffer_.acquire_fence = release_fence;

    return kErrorNone;
}

DisplayError SdmDisplay::SetReadbackBuffer(void *gbm_buf, shared_ptr<Fence> acquire_fence,
                                           CwbConfig cwb_config) {
    DisplayError error = kErrorNone;

    error = SetOutputBuffer(gbm_buf, acquire_fence);
    if(error != kErrorNone) {
        DLOGE("Failed to set output buffer ");
        return error;
    }

    error = display_intf_->CaptureCwb(output_buffer_, cwb_config);
    if (error != kErrorNone) {
        DLOGE("Failed to captureCwb");
        return error;
    }

    //CWB config debug block
    {
        CwbConfig config = cwb_config;
        LayerRect &roi = config.cwb_roi;
        LayerRect &full_rect = config.cwb_full_rect;
        CwbTapPoint &tap_point = config.tap_point;
        DLOGV_IF(kTagCwb,"CWB config from client: tap_point %d, CWB ROI Rect(%f %f %f %f), "
                 "PU_as_CWB_ROI %d, Cwb full rect : (%f %f %f %f)", tap_point,
                 roi.left, roi.top, roi.right, roi.bottom, config.pu_as_cwb_roi,
                 full_rect.left, full_rect.top, full_rect.right, full_rect.bottom);

        DLOGV_IF(kTagCwb,"Successfully configured the output buffer");
    }

    return error;
}

DisplayError SdmDisplay::SetFrameDumpConfig(uint32_t count, CwbConfig &cwb_config,
                                            int32_t format) {
    const CwbTapPoint &point = cwb_config_.tap_point;
    BufferConfig &base_buffer_config = output_buffer_info_.buffer_config;
    DisplayError error = kErrorNone;

    if (point < CwbTapPoint::kLmTapPoint || point > CwbTapPoint::kDemuraTapPoint) {
        DLOGE("Invalid CWB tap point config");
        return kErrorParameters;
    }

    error = display_intf_->GetCwbBufferResolution(&cwb_config, &base_buffer_config.width,
                                                  &base_buffer_config.height);
    if (error != kErrorNone) {
        DLOGE("Buffer Resolution setting failed.");
        return error;
    }

    base_buffer_config.format = GetSDMFormat(format, 0);
    const char *format_string = GetFormatString(base_buffer_config.format);
    base_buffer_config.buffer_count = 1;

    if (base_buffer_config.format == kFormatInvalid) {
        DLOGE("Format %d is not supported by SDM", format);
        return kErrorParameters;
    }

    if (!display_intf_->IsWriteBackSupportedFormat(base_buffer_config.format)) {
        DLOGE("WB doesn't support color format : %s .", format_string);
        return kErrorParameters;
    }

    DLOGV_IF(kTagCwb,"CWB output buffer resolution: width:%d height:%d tap point:%s \
             format: %s", base_buffer_config.width, base_buffer_config.height,
             UINT32(point) ? (UINT32(point) == 1) ? "DSPP" : "DEMURA" : "LM",
             format_string);

    //Frame dumper allocate buffer from and freed by dump thread
    error = frame_dumper_->CreateReadbackBuffer(output_buffer_info_, &output_buffer_base_);
    if (error != kErrorNone) {
        DLOGE("Readback-Buffer creating failed");
        return error;
    }

    error = SetReadbackBuffer(output_buffer_info_.private_data, nullptr, cwb_config);
    if (error != kErrorNone) {
        DLOGE("Readback-Buffer setting failed");
        frame_dumper_->FreeReadbackBuffer(output_buffer_info_);
        output_buffer_info_ = {};
        return error;
    }

    dump_frame_count_ = count;
    cwb_config_ = cwb_config;
    dump_input_layers_ = false;

    return kErrorNone;
}

void SdmDisplay::HandleFrameDump() {
    DisplayError error = kErrorNone;

    if (dump_input_layers_ || !dump_frame_count_)
        return;

    shared_ptr<SdmFrameDumper::DumpOutputData> thread_data;
    thread_data = std::make_shared<SdmFrameDumper::DumpOutputData>();

    thread_data->buffer_info = output_buffer_info_;
    thread_data->base = output_buffer_base_;
    thread_data->retire_fence = layer_stack_.retire_fence;
    thread_data->frame_index = dump_frame_index_;
    thread_data->fd = output_buffer_.planes[0].fd;

    frame_dumper_->CreateDumpThread(thread_data);

    bool stop_frame_dump = false;
    if (0 == (dump_frame_count_ - 1)) {
        stop_frame_dump = true;
    } else {
        error = frame_dumper_->CreateReadbackBuffer(output_buffer_info_, &output_buffer_base_);
        if (error != kErrorNone) {
            stop_frame_dump = true;
            DLOGE("Readback-Buffer creating failed");
        } else {
            error = SetReadbackBuffer(output_buffer_info_.private_data, nullptr, cwb_config_);
            if (error != kErrorNone) {
                DLOGE("Readback-Buffer setting failed");
                frame_dumper_->FreeReadbackBuffer(output_buffer_info_);
                stop_frame_dump = true;
            }
        }

        if (stop_frame_dump)
            DLOGE("Unexpectedly stopped dumping of remaining %d frames for frame index[%d] onward!",
                  dump_frame_count_, dump_frame_index_);

        dump_frame_count_--;
        dump_frame_index_++;
    }

    if (stop_frame_dump) {
        output_buffer_info_ = {};
        output_buffer_base_ = nullptr;
        dump_frame_count_ = 0;
        dump_frame_index_ = 0;
    }
}

void SdmDisplay::HandleFrameOutput() {
    /*TODO:CWB client request handle*/
    HandleFrameDump();
}

void SdmDisplay::HandleInputDump(struct drm_output *output) {
    if (!dump_input_layers_ || !dump_frame_count_)
        return;

    frame_dumper_->HandleInputDump(output, dump_frame_index_);

    bool stop_frame_dump = false;
    if (0 == (dump_frame_count_ - 1)) {
        stop_frame_dump = true;
    } else {
        dump_frame_count_--;
        dump_frame_index_++;
    }

    if (stop_frame_dump) {
        dump_frame_count_ = 0;
        dump_frame_index_ = 0;
    }
}

DisplayError SdmDisplay::RegisterCb(int display_id, vblank_cb_t vbcb) {
    DisplayError error = kErrorNone;

    vblank_cb_   = vbcb;
    display_id_  = display_id;

    return error;
}

int SdmDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = display_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
  if (error != kErrorNone) {
    DLOGE("Failed. Error = %d", error);
    return -1;
  }

  return 0;
}

DisplayError SdmDisplay::FreeLayerStack() {
  /* block main thread execution until async commit finishes */
  if (display_intf_)
    display_intf_->DestroyLayer();

  for (uint32_t i = 0; i < layer_stack_.layers.size(); i++) {
    Layer *layer = layer_stack_.layers.at(i);

    layer->visible_regions.erase(layer->visible_regions.begin(),
                layer->visible_regions.end());
    layer->dirty_regions.erase(layer->dirty_regions.begin(),
                layer->dirty_regions.end());

    delete layer;
  }
  layer_stack_ = {};

  return kErrorNone;
}

DisplayError SdmDisplay::FreeLayerGeometry(struct LayerGeometry *glayer) {
    if (glayer->dirty_regions.count)
        free(glayer->dirty_regions.rects);
    if (glayer->visible_regions.count)
        free(glayer->visible_regions.rects);

    free(glayer);

    return kErrorNone;
}

DisplayError SdmDisplay::AllocLayerStackMemory(struct drm_output *output) {
    for (size_t i = 0; i < output->view_count; i++) {
         Layer *layer = new Layer();
         layer_stack_.layers.push_back(layer);
    }

    return kErrorNone;
}

static void SetRect(sdm::LayerRect *dst, struct Rect *src)
{
    dst->left = src->left;
    dst->top = src->top;
    dst->right = src->right;
    dst->bottom = src->bottom;
}

DisplayError SdmDisplay::PopulateLayerGeometryOnToLayerStack(struct drm_output *output,
                                                             uint32_t index,
                                                             struct LayerGeometry *glayer,
                                                             bool is_skip) {
    DisplayError error = kErrorNone;
    sdm::Layer *layer = layer_stack_.layers.at(index);
    struct LayerGeometry *layer_geometry = glayer;
    LayerBuffer *layer_buffer, &layer_buffer_ = layer->input_buffer;
    layer_buffer = &layer_buffer_;

    /* 1. Fill buffer information */
    layer_buffer->format = GetSDMFormat(layer_geometry->format, layer_geometry->flags.has_ubwc_buf);
    layer_buffer->width = layer_geometry->width;
    layer_buffer->height = layer_geometry->height;
    layer_buffer->unaligned_width = layer_geometry->unaligned_width;
    layer_buffer->unaligned_height = layer_geometry->unaligned_height;
    /* TODO (user): Obtain metadata and then */
    // TODO: (user)  if (SetMetaData(metadatar layer) != kErrorNone) {
    // TODO: (user)      return kErrorUndefined;
    // TODO: (user)  }

    if (index != layer_stack_.layers.size()-1)
        layer_buffer->planes[0].fd = layer_geometry->ion_fd;

    /* TODO: Below information should be set according to the real user scenario */
    layer_buffer->flags.secure = layer_geometry->flags.secure_present;
    layer_buffer->flags.video = layer_geometry->flags.video_present;
    layer_buffer->flags.hdr = layer_geometry->flags.hdr_present;

    layer_buffer->color_metadata = layer_geometry->color_metadata;

    if (layer_buffer->color_metadata.colorPrimaries == 0 &&
        layer_buffer->color_metadata.transfer == 0) {
        DLOGD("Invalid color_metadata, reset to default");
        layer_buffer->color_metadata.colorPrimaries = ColorPrimaries_BT709_5;
        layer_buffer->color_metadata.transfer = Transfer_sRGB;
    }

    DLOGD("color_metadata: ColorPrimaries: %d",
            layer_buffer->color_metadata.colorPrimaries);
    DLOGD("color_metadata: Transfer: %d is_skip:%d",
            layer_buffer->color_metadata.transfer, is_skip);

    layer_buffer->flags.macro_tile = false;
    layer_buffer->flags.interlace = false;
    layer_buffer->flags.secure_display = false;
    layer_buffer->flags.secure_camera = false;
    /* 2. Fill layer information */
    if (layer_geometry->composition == SDM_COMPOSITION_FB_TARGET)
        layer->composition = sdm::kCompositionGPUTarget;
    else
        layer->composition = sdm::kCompositionGPU;

    SetRect(&layer->src_rect, &layer_geometry->src_rect);
    SetRect(&layer->dst_rect, &layer_geometry->dst_rect);

    for (uint32_t i = 0; i < layer_geometry->visible_regions.count; i++) {
         SetRect(&layer->visible_regions.at(i), &layer_geometry->visible_regions.rects[i]);
    }

    for (uint32_t i = 0; i < layer_geometry->dirty_regions.count; i++) {
         SetRect(&layer->dirty_regions.at(i), &layer_geometry->dirty_regions.rects[i]);
    }

    layer->blending = GetSDMBlending(layer_geometry->blending);
    layer->plane_alpha = layer_geometry->plane_alpha;

    layer->transform.flip_horizontal =
              ((layer_geometry->transform & SDM_TRANSFORM_FLIP_H) > 0);
    layer->transform.flip_vertical =
              ((layer_geometry->transform & SDM_TRANSFORM_FLIP_V) > 0);
    layer->transform.rotation =
              ((layer_geometry->transform & SDM_TRANSFORM_90) ? 90.0f : 0.0f);

    layer->frame_rate = fps_;
    layer->solid_fill_color = 0;

    layer->flags.skip = is_skip;
    if (is_skip)
       layer_stack_.flags.skip_present = is_skip;
    if (layer_buffer->flags.video)
       layer_stack_.flags.video_present = true;
    if (layer_buffer->flags.hdr)
       layer_stack_.flags.hdr_present = 1;
    if (layer_buffer->flags.secure)
        layer_stack_.flags.secure_present = true;
    layer->flags.updating = true;
    layer->flags.solid_fill = false;
    layer->flags.cursor = false;
    layer->flags.single_buffer = false;
    layer->layer_id = index;

    return error;
}

DisplayError SdmDisplay::AllocateMemoryForLayerGeometry(struct \
                                drm_output *output,
                                uint32_t index,
                                struct LayerGeometry \
                                *glayer) {
    /* Configure each layer */
    uint32_t num_visible_rects = 0;
    uint32_t num_dirty_rects = 0;

    if (index < output->view_count) {
     if (glayer == NULL) {
          DLOGE("no layer geometry for the display!\n");
          return kErrorParameters;
     }
     /* It's permissive the visible/dirty region can be NULL */
     num_visible_rects = glayer->visible_regions.count;
     for (uint32_t j = 0; j < num_visible_rects; j++) {
          LayerRect visible_rect {};
          layer_stack_.layers.at(index)->visible_regions.push_back(visible_rect);
     }

     num_dirty_rects = glayer->dirty_regions.count;
     for (uint32_t j = 0; j < num_dirty_rects; j++) {
          LayerRect dirty_rect {};
          layer_stack_.layers.at(index)->dirty_regions.push_back(dirty_rect);
     }
    }

    return kErrorNone;
}

DisplayError SdmDisplay::AddGeometryLayerToLayerStack(struct drm_output *output, uint32_t index,
                                                      struct LayerGeometry *glayer, bool is_skip)
{
    DisplayError error = kErrorNone;

    AllocateMemoryForLayerGeometry(output, index, glayer);
    error = PopulateLayerGeometryOnToLayerStack(output, index, glayer, is_skip);

    return error;
}

int SdmDisplay::PrepareFbLayerGeometry(struct drm_output *output,
                        struct LayerGeometry **fb_glayer) {
    struct LayerGeometry *fb_layer = NULL;
    *fb_glayer = fb_layer = reinterpret_cast<struct LayerGeometry *> \
                              (zalloc(sizeof *fb_layer));
    if (!fb_layer) {
        DLOGE("fb_layer is NULL\n");
        return -1;
    }

    fb_layer->width = output->base.current_mode->width;
    fb_layer->height = output->base.current_mode->height;
    fb_layer->unaligned_width = output->base.current_mode->width;
    fb_layer->unaligned_height = output->base.current_mode->height;

    fb_layer->format = GetMappedFormatFromGbm(output->gbm_format);
    fb_layer->composition = SDM_COMPOSITION_FB_TARGET;

    fb_layer->src_rect.left = (float)0.0;
    fb_layer->src_rect.top = (float)0.0;
    fb_layer->src_rect.right = (float)output->base.current_mode->width;
    fb_layer->src_rect.bottom = (float)output->base.current_mode->height;
    fb_layer->dst_rect.left = (float)0.0;
    fb_layer->dst_rect.top = (float)0.0;
    fb_layer->dst_rect.right = (float)output->base.current_mode->width;
    fb_layer->dst_rect.bottom = (float)output->base.current_mode->height;

    fb_layer->visible_regions.rects = reinterpret_cast<struct Rect *> \
                              (zalloc(sizeof(struct Rect)));
    if (fb_layer->visible_regions.rects == NULL) {
        DLOGE("out of memory for allocating fb visible region\n");
        return -1;
    }
    fb_layer->visible_regions.rects[0] = fb_layer->dst_rect;
    fb_layer->visible_regions.count = 1;

    fb_layer->dirty_regions.rects = reinterpret_cast<struct Rect *> \
                              (zalloc(sizeof(struct Rect)));
    if (fb_layer->dirty_regions.rects == NULL) {
        DLOGE("out of memory for allocating fb dirty region\n");
        return -1;
    }

    fb_layer->dirty_regions.rects[0] = fb_layer->dst_rect;
    fb_layer->dirty_regions.count = 1;

    fb_layer->blending = SDM_BLENDING_PREMULTIPLIED;
    /*
     * output->base.width/height should be already transformed, so just
     * keep no transform for output.
     */
    fb_layer->transform = SDM_TRANSFORM_NORMAL;
    fb_layer->plane_alpha = 0xFF;

    fb_layer->flags.skip = 0;
    fb_layer->flags.is_cursor = 0;
    fb_layer->flags.has_ubwc_buf = false;

    return 0;
}

int SdmDisplayInterface::GetDrmMasterFd() {
    DRMMaster *master = nullptr;
    int ret = DRMMaster::GetInstance(&master);
    int fd;

    if (ret < 0) {
        DLOGE("Failed to acquire DRMMaster instance");
        return kErrorNotSupported;
        }
    master->GetHandle(&fd);
    return fd;
}

uint32_t SdmDisplay::GetSDMTransform(uint32_t wl_transform)
{
    uint32_t sdm_transform = SDM_TRANSFORM_NORMAL;

    switch (wl_transform) {
        case WL_OUTPUT_TRANSFORM_90:
            sdm_transform = SDM_TRANSFORM_270;
            break;
        case WL_OUTPUT_TRANSFORM_180:
            sdm_transform = SDM_TRANSFORM_180;
            break;
        case WL_OUTPUT_TRANSFORM_270:
            sdm_transform = SDM_TRANSFORM_90;
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED:
            sdm_transform = SDM_TRANSFORM_FLIP_H;
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            sdm_transform = SDM_TRANSFORM_FLIP_H ^ SDM_TRANSFORM_270;
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            sdm_transform = SDM_TRANSFORM_FLIP_H ^ SDM_TRANSFORM_180;
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            sdm_transform = SDM_TRANSFORM_FLIP_H ^ SDM_TRANSFORM_90;
            break;
        case WL_OUTPUT_TRANSFORM_NORMAL:
        default:
            sdm_transform = SDM_TRANSFORM_NORMAL;
            break;
    }

    return sdm_transform;
}

int SdmDisplay::PrepareNormalLayerGeometry(struct drm_output *output,
                         struct LayerGeometry **glayer,
                         struct sdm_layer *sdm_layer) {
    struct LayerGeometry *layer;
    struct weston_view *ev = sdm_layer->view;
    struct weston_surface *es = ev->surface;
    bool is_cursor = sdm_layer->is_cursor;
    uint32_t format = GBM_FORMAT_XBGR8888;
    pixman_region32_t r;

    *glayer = layer = reinterpret_cast<struct LayerGeometry *> \
                           (zalloc(sizeof *layer));
    if (!layer) {
        DLOGE("layer is NULL\n");
        return -1;
    }

    /* Prepare layer buffer information */
    layer->width = es->width;
    layer->height = es->height;
    layer->unaligned_width = es->width;
    layer->unaligned_height = es->height;
    layer->fb_id = -1;
    layer->format = SDM_BUFFER_FORMAT_RGBX_8888;

    if (sdm_layer->fb && sdm_layer->fb->bo) {
        struct gbm_bo *bo = NULL;

        bo = sdm_layer->fb->bo;

        if (bo == NULL) {
            DLOGE("fail to import gbm bo!\n");
            return -1;
        } else {
            uint32_t width, height;

            //save gbm bo in sdm layer for future reference.
            sdm_layer->bo = bo;

            width = gbm_bo_get_width(bo);
            height = gbm_bo_get_height(bo);
            format = gbm_bo_get_format(bo);

            uint32_t alignedWidth = 0;
            uint32_t alignedHeight = 0;
            uint32_t secure_status = 0;
            uint32_t ubwc_status = 0;
            void *prm = reinterpret_cast<void *> (&layer->color_metadata);

            gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_WIDTH, bo, &alignedWidth);
            gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_HEIGHT, bo, &alignedHeight);
            gbm_perform(GBM_PERFORM_GET_SECURE_BUFFER_STATUS, bo, &secure_status);
            gbm_perform(GBM_PERFORM_GET_METADATA, bo, GBM_METADATA_GET_COLOR_METADATA, prm);
            gbm_perform(GBM_PERFORM_GET_UBWC_STATUS, bo, &ubwc_status);

            // Override buffer width/height to reflect aligned width and aligned height.
            layer->width = alignedWidth;
            layer->height = alignedHeight;
            layer->unaligned_width = width;
            layer->unaligned_height = height;
            layer->ion_fd = sdm_layer->fb->ion_fd;
            layer->flags.secure_present = secure_status;
            layer->flags.has_ubwc_buf = ubwc_status;

            bool hdr_layer = layer->color_metadata.colorPrimaries == ColorPrimaries_BT2020 &&
                             (layer->color_metadata.transfer == Transfer_SMPTE_ST2084 ||
                             layer->color_metadata.transfer == Transfer_HLG);

            // Set to true if incoming layer has HDR support and Display supports HDR functionality
            if (!disable_hdr_handling_) {
                layer->flags.hdr_present = hdr_layer;
            }
        }
    }

    if (NeedConvertGbmFormat(ev, format))
        format = ConvertToOpaqueGbmFormat(format);
    layer->format = GetMappedFormatFromGbm(format);

    /* Get src/dst rect */
    ComputeSrcDstRect(output, ev, &layer->src_rect, &layer->dst_rect);
    /* Get visible rects */
    if (GetVisibleRegion(output, ev, &sdm_layer->overlap, &layer->visible_regions))
        return -1;
    /*
     * Get dirty rects. It's not surprised a view has null damage region, that means
     * the buffer content of view is not changed since last frame, eg. cursor.
     */
    if (ComputeDirtyRegion(ev, &layer->dirty_regions))
        return -1;

    /* Set no transform for view since bounding box already been applied by transform */
    layer->transform = SDM_TRANSFORM_NORMAL;
    /* Get global alpha */
    layer->plane_alpha = GetGlobalAlpha(ev);

    /* TODO: update property src_config, csc, color_fill, scaler ... */
    layer->flags.is_cursor = is_cursor;
    layer->flags.video_present = GetVideoPresenceByFormatFromGbm(format);

    /* compute whether this view has no blending */
    pixman_region32_init_rect(&r, 0, 0, ev->surface->width, ev->surface->height);
    pixman_region32_subtract(&r, &r, &ev->surface->opaque);

    if (!pixman_region32_not_empty(&r) && (layer->plane_alpha == 0xFF))
        layer->blending = SDM_BLENDING_NONE;
    else
        layer->blending = SDM_BLENDING_COVERAGE;

    pixman_region32_fini(&r);

    // Video layers are always opaque
    if (layer->flags.video_present) {
        layer->blending = SDM_BLENDING_NONE;
        // Use inline rotator to transform video layer
        layer->transform = GetSDMTransform(output->base.transform);
    }

    /* Initialize all views with GPU composition first, SDM will update them after prepare */
    layer->composition = SDM_COMPOSITION_GPU;

    return 0;
}

DisplayError SdmDisplay::PrePrepareLayerStack(struct drm_output *output) {
    DisplayError error = kErrorNone;
    struct sdm_layer *sdm_layer = NULL;
    struct LayerGeometry *glayer = NULL;
    uint32_t gpu_target_index = GET_GPU_TARGET_SLOT(output->view_count);
    uint32_t index = 0;
    LayerBufferFlags layerBufferFlags;

    if (shutdown_pending_) {
        return kErrorShutDown;
    }

    FreeLayerStack();
    AllocLayerStackMemory(output);

    DLOGI("gpu_target_index = %d\n", gpu_target_index);

    /* If no view can be handled by SDM, just skip below and prepare fb target directly. */
    if (gpu_target_index > 0) {
        wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
            glayer = NULL;
            if(PrepareNormalLayerGeometry(output, &glayer, sdm_layer)) {
                DLOGE("fail to prepare normal layer geometry.");
                return kErrorUndefined;
            }

            error = AddGeometryLayerToLayerStack(output, index, glayer, sdm_layer->is_skip);
            if (error) {
                DLOGE("failed add Geometry Layer to LayerStack.");
                FreeLayerGeometry(glayer);
                return kErrorUndefined;
            }
            FreeLayerGeometry(glayer);

            // Pass the wl_resource handle from sdm layer to layer stack
            // to use it for egl image creation in tone mapping
            layerBufferFlags = layer_stack_.layers.at(index)->input_buffer.flags;
            if (layerBufferFlags.video && layerBufferFlags.hdr) {
                layer_stack_.layers.at(index)->userdata =
                        sdm_layer->view->surface->buffer_ref.buffer->resource;
            }

            //Acquire fence fd is not received for frame buffer target layer as gl-renderer is not
            //sending it. Hence not adding acquire fence for frame buffer target
            layer_stack_.layers.at(index)->input_buffer.acquire_fence =
                               Fence::Create(dup(sdm_layer->acquire_fence_fd), "App_Layer_Fence");
            DLOGD_IF(kTagNone, "Acquire fence fd: %s for layer index %d",
                 Fence::GetStr(layer_stack_.layers.at(index)->input_buffer.acquire_fence).c_str(),
                 index);

            index++;
            if (sdm_layer->is_skip)
                layer_stack_.flags.skip_present = true;
        }
    }

    int err = PrepareFbLayerGeometry(output, &glayer);
    if (err) {
        DLOGE("failed to prepare Layer Geometry Fb target\n");
        return kErrorUndefined;
    } else {
        error = AddGeometryLayerToLayerStack(output, index, glayer, false);
        if (error) {
            DLOGE("fail to prepare Fb target: Add Geometry failure fb\n");
            FreeLayerGeometry(glayer);
            return kErrorUndefined;
        }
        FreeLayerGeometry(glayer);
    }

    return error;
}

DisplayError SdmDisplay::PrePrepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;

    error = PrePrepareLayerStack(output);
    if (error ) {
        DLOGE("function failed!\n", error);
    }

    if (hdr_supported_ && !disable_hdr_handling_) {
        ColorMode best_color_mode =
            display_colormode_->SelectBestColorSpace(
                layer_stack_.flags.hdr_present, &layer_stack_);
        error = display_colormode_->SetColorModeWithRenderIntent(best_color_mode);
        if (error != kErrorNone) {
            DLOGE("Setting color mode failed.");
        }
    }

    return error;
}

DisplayError SdmDisplay::PostPrepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;
    int index = 0;
    struct sdm_layer *sdm_layer = NULL;

    wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
      Layer *layer = layer_stack_.layers.at(index);

      if (layer->composition == kCompositionGPU) {
        sdm_layer->composition_type = SDM_COMPOSITION_GPU;
      } else {
        sdm_layer->composition_type = SDM_COMPOSITION_OVERLAY;
      }
      index++;
    }

    if (output->backend->use_pixman)
    {
        // Pixman uses double buffer,it would repaint FBT after prepare
        // So need wait pre-reitre_fence here to avoid tearing issue
        if (prev_layer_stack_.retire_fence) {
            int ret = -1;
            ret = Fence::Wait(prev_layer_stack_.retire_fence);
            if (ret != kErrorNone) {
                DLOGE("retire_fence wait timeout! ret=%d\n", ret);
            }
        }
    }

    return error;
}

#if SDM_DISPLAY_DUMP_LAYER_STACK
static void GetLayerStackDump(void *layerStack, char *buffer, uint32_t length) {
  if (!buffer || !length) {
    return;
  }
  static int frame_count=0;
  buffer[0] = '\0';
  struct LayerStack *layer_stack;
  layer_stack = reinterpret_cast<struct LayerStack *>(layerStack);
  DLOGI("\n-------- Display Manager: Layer Stack Dump --------");
  fprintf(stderr,"Frame:%d LayerStack: NumLayers:%d flags:0x%x\n",
                frame_count, layer_stack->layers.size(), layer_stack->flags);

  for (uint32_t i = 0; i < layer_stack->layers.size(); i++) {
      char buf[LEN_LOCAL] = {0};

      struct Layer *layer;
      struct LayerBuffer buffer;
      layer = layer_stack->layers.at(i);
      buffer = layer->input_buffer;

      memset(buf, '\0', LEN_LOCAL);
      snprintf(buf, sizeof(buf), "Layer: %d\n    width  = %d,     height = %d", i,
        layer->input_buffer.width, layer->input_buffer.height);
      snprintf(buf, sizeof(buf), "%s\n LayerComposition = %#x", buf, layer->composition);
      snprintf(buf, sizeof(buf), "%s\n src_rect (LTRB) = %4.2f, %4.2f, %4.2f, %4.2f",
        buf, layer->src_rect.left, layer->src_rect.top,
        layer->src_rect.right, layer->src_rect.bottom);
      snprintf(buf, sizeof(buf), "%s\n dst_rect (LTRB) = %4.2f, %4.2f, %4.2f, %4.2f", buf,
        layer->dst_rect.left, layer->dst_rect.top, layer->dst_rect.right,
        layer->dst_rect.bottom);
      snprintf(buf, sizeof(buf), "%s\n LayerBlending = %#x", buf, layer->blending);
      snprintf(buf, sizeof(buf), "%s\n LayerTransform:rotation= %f,flip_horizontal=%s,flip_vertical=%s",
        buf, layer->transform.rotation, (layer->transform.flip_horizontal? \
        "true":"false"), (layer->transform.flip_vertical? "true":"false"));
      snprintf(buf, sizeof(buf), "%s\n Plane Alpha = %#x, frame_rate = %d,  solid_fill_color = %d",
        buf, layer->plane_alpha, layer->frame_rate, layer->solid_fill_color);
      snprintf(buf, sizeof(buf), "%s\n LayerFlags = %#x", buf, layer->flags);
      snprintf(buf, sizeof(buf), "%s\t LayerFlags.skip = %d", buf, layer->flags.skip);
      snprintf(buf, sizeof(buf), "%s\n LayerBuffer Flags: hdr:%d secure:%d video:%d", buf,
                    buffer.flags.hdr, buffer.flags.secure, buffer.flags.video);

      fprintf(stderr,"\n%s\n", buf);
  }
  frame_count++;

  return;
}
#endif

DisplayError SdmDisplay::Prepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;

    if (esd_reset_panel_) {
      return kErrorNotSupported;
    }

#if SDM_DISPLAY_DUMP_LAYER_STACK
    char dump_buffer[8192] = {0};
#endif

    error = PrePrepare(output);
    if (error != kErrorNone) {
        DLOGE("failed during PrePrepare");
    }

    HandleInputDump(output);

#if SDM_DISPLAY_DUMP_LAYER_STACK
    // Dump all input layers of the layer stack:
    GetLayerStackDump(&layer_stack_, dump_buffer, sizeof(dump_buffer));
#endif

    if (display_type_ == kVirtual) {
        layer_stack_.output_buffer = &output_buffer_;
        layer_stack_.cwb_config = &cwb_config_;
        DLOGD("display(%d) output(%d) buffer_id(%p)", output->display_id, output->base.id,
              layer_stack_.output_buffer->buffer_id);
    }

    ParseAndSetDumpConfig();

    error = display_intf_->Prepare(&layer_stack_);
    if (error != kErrorNone) {
        DLOGE("failed during Prepare error:%d\n",error);
    }

#if SDM_DISPLAY_DUMP_LAYER_STACK
    DumpInterface::GetDump(dump_buffer, sizeof(dump_buffer));
#endif

    error = PostPrepare(output);
    if (error != kErrorNone)
	DLOGE("function failed Error= %d\n", error);

    return error;
}

DisplayError SdmDisplay::PreCommit()
{
    DisplayError error = kErrorNone;
    if (layer_stack_.flags.hdr_present) {
        int status = -1;
        if (tone_mapper_) {
            error = tone_mapper_->HandleToneMap(&layer_stack_);
            if (error != kErrorNone) {
                DLOGE("Error handling HDR in ToneMapper, status code = %d", status);
            }
        } else {
            DLOGD("HandleToneMap failed due to invalid tone_mapper_ instance");
        }
    } else {
        if (tone_mapper_) {
            tone_mapper_->Terminate();
        }
        else
            DLOGD("ToneMap Terminate failed due to invalid tone_mapper_ instance");
    }

    return error;
}

shared_ptr<Fence> SdmDisplay::GetReleaseFence()
{
    return current_release_fence_;
}

DisplayError SdmDisplay::PostCommit()
{
    DisplayError error = kErrorNone;

    HandleFrameOutput();

    if (tone_mapper_ && tone_mapper_->IsActive()) {
        tone_mapper_->PostCommit(&layer_stack_);
    }

    for (int i = 0; i < layer_stack_.layers.size(); i++) {
      if (!layer_stack_.layers[i]->input_buffer.release_fence) {
        continue;
      }
      current_release_fence_ = Fence::Merge(current_release_fence_,
                layer_stack_.layers[i]->input_buffer.release_fence);
    }

    //Wait for retire fence fds
    if (prev_layer_stack_.retire_fence) {
        int ret = -1;
        ret = Fence::Wait(prev_layer_stack_.retire_fence);
        if (ret != kErrorNone) {
            DLOGE("retire_fence wait timeout! ret=%d\n", ret);
        }
    }

    prev_layer_stack_ = layer_stack_;
    //Iterate through the layer buffer and close release fences
    for (uint32_t i = 0; i < layer_stack_.layers.size(); i++) {
        Layer *layer = layer_stack_.layers.at(i);
        LayerBuffer *layer_buffer = &layer->input_buffer;
    }

    return error;
}

DisplayError SdmDisplay::Commit(struct drm_output *output)
{
    DTRACE_SCOPED();
    DisplayError ret = kErrorNone;
    DisplayState state = kStateOff;

    if (esd_reset_panel_) {
      return kErrorNotSupported;
    }

    uint32_t layer_count = layer_stack_.layers.size();
    uint32_t GPUTarget_index = layer_count-1;
    Layer *GpuTargetlayer;

    if (!output->next_fb) {
      DLOGE("Scanout state fb not found for output=%d", output->base.id);
      return kErrorUndefined;
    }

    GpuTargetlayer = layer_stack_.layers.at(GPUTarget_index);
    GpuTargetlayer->input_buffer.planes[0].fd = output->next_fb->ion_fd;

    display_intf_->GetDisplayState(&state);
    DLOGI("state=%d commiting ion fd = %d, layer count=%d",
                  state, output->next_fb->ion_fd, layer_count);

    ret = PreCommit();
    if (ret != kErrorNone) {
        DLOGE("PreCommit failed!");
        ret = kErrorNone;
    }

    prev_output_ = output;
    ret = display_intf_->Commit(&layer_stack_);

    if (output->first_cycle && ret == kErrorNone && state == kStateOn) {
      output->first_cycle = false;
    }

    PostCommit();

    DLOGV("success");
    return ret;
}

/* Adding following  support functions */
LayerBufferFormat SdmDisplay::GetSDMFormat(uint32_t src_fmt, uint32_t has_ubwc_buf)
{
    LayerBufferFormat format = kFormatInvalid;

    if (has_ubwc_buf) {
        switch (src_fmt) {
            case SDM_BUFFER_FORMAT_RGBA_8888:
                format = sdm::kFormatRGBA8888Ubwc;
                break;
            case SDM_BUFFER_FORMAT_RGBX_8888:
                format = sdm::kFormatRGBX8888Ubwc;
                break;
            case SDM_BUFFER_FORMAT_BGR_565:
                format = sdm::kFormatBGR565Ubwc;
                break;
            case SDM_BUFFER_FORMAT_RGBA_2101010:
                format = sdm::kFormatRGBA1010102Ubwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
            case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
                format = sdm::kFormatYCbCr420SPVenusUbwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC:
                format = sdm::kFormatYCbCr420TP10Ubwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC:
                format = sdm::kFormatYCbCr420P010Ubwc;
                break;
            default:
                DLOGE("Unsupported UBWC format %d\n", src_fmt);
                return sdm::kFormatInvalid;
        }

        return format;
    }

    switch (src_fmt) {
        case SDM_BUFFER_FORMAT_RGBA_8888:
            format = sdm::kFormatRGBA8888;
            break;
        case SDM_BUFFER_FORMAT_RGBA_5551:
            format = sdm::kFormatRGBA5551;
            break;
        case SDM_BUFFER_FORMAT_RGBA_4444:
            format = sdm::kFormatRGBA4444;
            break;
        case SDM_BUFFER_FORMAT_BGRA_8888:
            format = sdm::kFormatBGRA8888;
            break;
        case SDM_BUFFER_FORMAT_RGBX_8888:
            format = sdm::kFormatRGBX8888;
            break;
        case SDM_BUFFER_FORMAT_BGRX_8888:
            format = sdm::kFormatBGRX8888;
            break;
        case SDM_BUFFER_FORMAT_RGB_888:
            format = sdm::kFormatRGB888;
            break;
        case SDM_BUFFER_FORMAT_RGB_565:
            format = sdm::kFormatRGB565;
            break;
        case SDM_BUFFER_FORMAT_BGR_565:
            format = sdm::kFormatBGR565;
            break;
        case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
        case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
            format = sdm::kFormatYCbCr420SemiPlanarVenus;
            break;
        case SDM_BUFFER_FORMAT_ABGR_2101010:
            format = sdm::kFormatABGR2101010;
            break;
        case SDM_BUFFER_FORMAT_RGBA_2101010:
            format = sdm::kFormatRGBA1010102;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC:
            format = sdm::kFormatYCbCr420TP10Ubwc;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC:
            format = sdm::kFormatYCbCr420P010Ubwc;
            break;
        case SDM_BUFFER_FORMAT_YV12:
            format = sdm::kFormatYCrCb420PlanarStride16;
            break;
        case SDM_BUFFER_FORMAT_YCrCb_420_SP:
            format = sdm::kFormatYCrCb420SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_SP:
            format = sdm::kFormatYCbCr420SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_422_SP:
            format = sdm::kFormatYCbCr422H2V1SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_422_I:
            format = sdm::kFormatYCbCr422H2V1Packed;
            break;
        case SDM_BUFFER_FORMAT_P010:
            format = sdm::kFormatYCbCr420P010;
            break;
        case SDM_BUFFER_FORMAT_P010_VENUS:
            format = sdm::kFormatYCbCr420P010Venus;
            break;
        default:
            DLOGE("Unsupported format %d\n", src_fmt);
            return sdm::kFormatInvalid;
    }

    return format;
}

LayerBlending SdmDisplay::GetSDMBlending(uint32_t source)
{
    sdm::LayerBlending blending = sdm::kBlendingPremultiplied;

    switch (source) {
    case SDM_BLENDING_PREMULTIPLIED:
         blending = sdm::kBlendingPremultiplied;
         break;
    case SDM_BLENDING_COVERAGE:
         blending = sdm::kBlendingCoverage;
         break;
    case SDM_BLENDING_NONE:
    default:
         blending = sdm::kBlendingOpaque;
         break;
    }

    return blending;
}

bool SdmDisplay::GetVideoPresenceByFormatFromGbm(uint32_t fmt)
{
    bool is_video_present = false;

    switch (fmt) {
       case GBM_FORMAT_NV12:
       case GBM_FORMAT_YCbCr_420_TP10_UBWC:
       case GBM_FORMAT_YCbCr_420_P010_UBWC:
       case GBM_FORMAT_YCbCr_420_P010_VENUS:
       case GBM_FORMAT_P010:
            is_video_present = true;
            break;
       default:
            is_video_present = false;
            break;
    }

    return is_video_present;
}

uint32_t SdmDisplay::GetMappedFormatFromGbm(uint32_t fmt)
{
    uint32_t ret = SDM_BUFFER_FORMAT_INVALID;

    switch (fmt) {
    case GBM_FORMAT_ARGB8888:
         ret = SDM_BUFFER_FORMAT_BGRA_8888;
         break;
    case GBM_FORMAT_XRGB8888:
         ret = SDM_BUFFER_FORMAT_BGRX_8888;
         break;
    case GBM_FORMAT_ABGR8888:
         ret = SDM_BUFFER_FORMAT_RGBA_8888;
         break;
    case GBM_FORMAT_XBGR8888:
         ret = SDM_BUFFER_FORMAT_RGBX_8888;
         break;
    case GBM_FORMAT_BGRA8888:
    case GBM_FORMAT_RGBA8888:
         ret = SDM_BUFFER_FORMAT_ARGB_8888;
         break;
    case GBM_FORMAT_ARGB4444:
    case GBM_FORMAT_ABGR4444:
         ret = SDM_BUFFER_FORMAT_RGBA_4444;
         break;
    case GBM_FORMAT_ARGB1555:
    case GBM_FORMAT_ABGR1555:
         ret = SDM_BUFFER_FORMAT_RGBA_5551;
         break;
    case GBM_FORMAT_RGB565:
         ret = SDM_BUFFER_FORMAT_BGR_565;
         break;
    case GBM_FORMAT_BGR565:
         ret = SDM_BUFFER_FORMAT_RGB_565;
         break;
    case GBM_FORMAT_RGB888:
         ret = SDM_BUFFER_FORMAT_BGR_888;
         break;
    case GBM_FORMAT_BGR888:
         ret = SDM_BUFFER_FORMAT_RGB_888;
         break;
    case GBM_FORMAT_NV12:
         ret = SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS;
         break;
    case GBM_FORMAT_ABGR2101010:
         ret = SDM_BUFFER_FORMAT_RGBA_2101010;
         break;
    case GBM_FORMAT_YCbCr_420_TP10_UBWC:
         ret = SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC;
         break;
    case GBM_FORMAT_YCbCr_420_P010_UBWC:
        ret = SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC;
        break;
    case GBM_FORMAT_P010:
        ret = SDM_BUFFER_FORMAT_P010;
        break;
    case GBM_FORMAT_YCbCr_420_P010_VENUS:
        ret = SDM_BUFFER_FORMAT_P010_VENUS;
        break;
    default:
         DLOGE("Unsupported GBM format %s\n", FourccToString(fmt));
         break;
    }

    return ret;
}

bool SdmDisplay::NeedConvertGbmFormat(struct weston_view *ev, uint32_t format)
{
    pixman_region32_t r;
    bool need_convert = false;

    if (IsTransparentGbmFormat(format)) {
     pixman_region32_init_rect(&r, 0, 0,
             ev->surface->width,
             ev->surface->height);
     pixman_region32_subtract(&r, &r, &ev->surface->opaque);

     if (!pixman_region32_not_empty(&r))
         need_convert = true;

     pixman_region32_fini(&r);
    }

    return need_convert;
}

uint32_t SdmDisplay::ConvertToOpaqueGbmFormat(uint32_t format)
{
    uint32_t ret = GBM_FORMAT_XRGB8888;

    switch (format) {
    case GBM_FORMAT_ARGB8888:
     ret = GBM_FORMAT_XRGB8888;
     break;
    case GBM_FORMAT_BGRA8888:
     ret = GBM_FORMAT_BGRX8888;
     break;
    case GBM_FORMAT_RGBA8888:
     ret = GBM_FORMAT_RGBX8888;
     break;
    case GBM_FORMAT_ABGR8888:
     ret = GBM_FORMAT_XBGR8888;
     break;
    default:
     break;
    }

    return ret;
}

void SdmDisplay::ComputeSrcDstRect(struct drm_output *output, struct weston_view *ev,
                    struct Rect *src_ret, struct Rect *dst_ret)
{
    pixman_region32_t src_rect, dest_rect;
    float sx1, sy1, sx2, sy2;

    /* dst rect */
    pixman_region32_init(&dest_rect);
    pixman_region32_intersect(&dest_rect, &ev->transform.boundingbox, &output->base.region);

    pixman_region32_translate(&dest_rect, -output->base.x, -output->base.y);

    pixman_box32_t *box, tbox = {0};
    box = pixman_region32_extents(&dest_rect);

    sdm_weston_transformed_rect(output, &tbox, *box);

    dst_ret->left = tbox.x1;
    dst_ret->right = tbox.x2;
    dst_ret->top = tbox.y1;
    dst_ret->bottom = tbox.y2;

    pixman_region32_fini(&dest_rect);

    /* src rect */
    pixman_region32_init(&src_rect);
    pixman_region32_intersect(&src_rect, &ev->transform.boundingbox,
                              &output->base.region);
    box = pixman_region32_extents(&src_rect);

    sdm_weston_global_transform_rect(ev, box, &sx1, &sy1, &sx2, &sy2);

    pixman_region32_fini(&src_rect);

    src_ret->left = sx1;
    src_ret->top = sy1;
    src_ret->right = sx2;
    src_ret->bottom = sy2;
}

int SdmDisplay::ComputeDirtyRegion(struct weston_view *ev,
                    struct RectArray *dirty) {
    pixman_region32_t temp;
    struct weston_surface *es = ev->surface;
    pixman_box32_t *rectangles = NULL;
    int n, i;

    pixman_region32_init(&temp);
    pixman_region32_intersect_rect(&temp, &es->damage, 0,
                    0, es->width, es->height);
    rectangles = pixman_region32_rectangles(&temp, &n);
    if (!n) {
     pixman_region32_fini(&temp);
     return 0;
    }

    dirty->rects = reinterpret_cast<struct Rect *> \
                    (zalloc(n * sizeof(struct Rect)));
    if (dirty->rects == NULL) {
     DLOGE("out of memory for allocating dirty region\n");
     return -1;
    }
    for (i = 0; i < n; i++) {
     dirty->rects[i].left = (float)rectangles[i].x1;
     dirty->rects[i].top = (float)rectangles[i].y1;
     dirty->rects[i].right = (float)rectangles[i].x2;
     dirty->rects[i].bottom = (float)rectangles[i].y2;
    }
    dirty->count = n;
    pixman_region32_fini(&temp);

    return 0;
}

uint8_t SdmDisplay::GetGlobalAlpha(struct weston_view *ev)
{
    if (ev->alpha > 1.0f)
     return 0xFF;
    else if (ev->alpha < 0.0f)
     return 0;
    else
     return (uint8_t)(0xFF * ev->alpha);
}

int SdmDisplay::GetVisibleRegion(struct drm_output *output, struct weston_view *ev,
                                         pixman_region32_t *aboved_opaque,
                                         struct RectArray *visible)
{
    pixman_region32_t temp;
    pixman_box32_t *rectangles = NULL;
    int n, i;

    pixman_region32_init(&temp);
    pixman_region32_copy(&temp, &ev->transform.boundingbox);
    pixman_region32_subtract(&temp, &temp, aboved_opaque);
    pixman_region32_intersect(&temp, &output->base.region, &temp);
    pixman_region32_translate(&temp, -output->base.x, -output->base.y);

    rectangles = pixman_region32_rectangles(&temp, &n);
    if (!n) {
     pixman_region32_fini(&temp);
     return 0;
    }

    visible->rects = reinterpret_cast<struct Rect *> (zalloc(n * sizeof(struct Rect)));
    if (visible->rects == NULL) {
     DLOGE("out of memory for allocating visible region\n");
     return -1;
    }
    for (i = 0; i < n; i++) {
     visible->rects[i].left = (float)rectangles[i].x1;
     visible->rects[i].top = (float)rectangles[i].y1;
     visible->rects[i].right = (float)rectangles[i].x2;
     visible->rects[i].bottom = (float)rectangles[i].y2;
    }
    visible->count = n;
    pixman_region32_fini(&temp);

    return 0;
}

bool SdmDisplay::IsTransparentGbmFormat(uint32_t format)
{
    return false;
}

const char *SdmDisplay::GetDisplayString() {
    switch (display_type_) {
    case kPrimary:
      return "primary";
    case kHDMI:
      return "hdmi";
    case kVirtual:
      return "virtual";
    default:
      return "invalid";
  }
}

int SdmDisplay::ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                     PPDisplayAPIPayload *out_payload,
                                     PPPendingParams *pending_action) {
  int ret = 0;

  if (display_intf_) {
    ret = display_intf_->ColorSVCRequestRoute(in_payload, out_payload, pending_action);
  } else {
    ret = -EINVAL;
  }

  return ret;
}

void SdmDisplay::SetIdleTimeoutMs(uint32_t timeout_ms, uint32_t inactive_ms) {
  display_intf_->SetIdleTimeoutMs(timeout_ms, inactive_ms);
}

DisplayError SdmDisplay::SetDetailEnhancerConfig
                                   (const DisplayDetailEnhancerData &de_data) {
  DisplayError error = kErrorNotSupported;

  if (display_intf_) {
    error = display_intf_->SetDetailEnhancerData(de_data);
  }
  return error;
}

DisplayError SdmDisplay::SetHWDetailedEnhancerConfig(void *params) {
  DisplayError err = kErrorNone;
  DisplayDetailEnhancerData de_data;

  PPDETuningCfgData *de_tuning_cfg_data = reinterpret_cast<PPDETuningCfgData*>(params);
  if (de_tuning_cfg_data->cfg_pending) {
    if (!de_tuning_cfg_data->cfg_en) {
      de_data.enable = 0;
      DLOGV_IF(kTagQDCM, "Disable DE config");
    } else {
      de_data.override_flags = kOverrideDEEnable;
      de_data.enable = 1;
#ifdef DISP_DE_LPF_BLEND
      DLOGV_IF(kTagQDCM, "Enable DE: flags %u, sharp_factor %d, thr_quiet %d, thr_dieout %d, "
        "thr_low %d, thr_high %d, clip %d, quality %d, content_type %d, de_blend %d, "
        "de_lpf_h %d, de_lpf_m %d, de_lpf_l %d",
        de_tuning_cfg_data->params.flags, de_tuning_cfg_data->params.sharp_factor,
        de_tuning_cfg_data->params.thr_quiet, de_tuning_cfg_data->params.thr_dieout,
        de_tuning_cfg_data->params.thr_low, de_tuning_cfg_data->params.thr_high,
        de_tuning_cfg_data->params.clip, de_tuning_cfg_data->params.quality,
        de_tuning_cfg_data->params.content_type, de_tuning_cfg_data->params.de_blend,
        de_tuning_cfg_data->params.de_lpf_h, de_tuning_cfg_data->params.de_lpf_m,
        de_tuning_cfg_data->params.de_lpf_l);
#endif
      if (de_tuning_cfg_data->params.flags & kDeTuningFlagSharpFactor) {
        de_data.override_flags |= kOverrideDESharpen1;
        de_data.sharp_factor = de_tuning_cfg_data->params.sharp_factor;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagClip) {
        de_data.override_flags |= kOverrideDEClip;
        de_data.clip = de_tuning_cfg_data->params.clip;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrQuiet) {
        de_data.override_flags |= kOverrideDEThrQuiet;
        de_data.thr_quiet = de_tuning_cfg_data->params.thr_quiet;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrDieout) {
        de_data.override_flags |= kOverrideDEThrDieout;
        de_data.thr_dieout = de_tuning_cfg_data->params.thr_dieout;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrLow) {
        de_data.override_flags |= kOverrideDEThrLow;
        de_data.thr_low = de_tuning_cfg_data->params.thr_low;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrHigh) {
        de_data.override_flags |= kOverrideDEThrHigh;
        de_data.thr_high = de_tuning_cfg_data->params.thr_high;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagContentQualLevel) {
        switch (de_tuning_cfg_data->params.quality) {
          case kDeContentQualLow:
            de_data.quality_level = kContentQualityLow;
            break;
          case kDeContentQualMedium:
            de_data.quality_level = kContentQualityMedium;
            break;
          case kDeContentQualHigh:
            de_data.quality_level = kContentQualityHigh;
            break;
          case kDeContentQualUnknown:
          default:
            de_data.quality_level = kContentQualityUnknown;
            break;
        }
      }

      switch (de_tuning_cfg_data->params.content_type) {
        case kDeContentTypeVideo:
          de_data.content_type = kContentTypeVideo;
          break;
        case kDeContentTypeGraphics:
          de_data.content_type = kContentTypeGraphics;
          break;
        case kDeContentTypeUnknown:
        default:
          de_data.content_type = kContentTypeUnknown;
          break;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagDeBlend) {
        de_data.override_flags |= kOverrideDEBlend;
        de_data.de_blend = de_tuning_cfg_data->params.de_blend;
      }
#ifdef DISP_DE_LPF_BLEND
      if (de_tuning_cfg_data->params.flags & kDeTuningFlagDeLpfBlend) {
        de_data.override_flags |= kOverrideDELpfBlend;
        de_data.de_lpf_en = true;
        de_data.de_lpf_h = de_tuning_cfg_data->params.de_lpf_h;
        de_data.de_lpf_m = de_tuning_cfg_data->params.de_lpf_m;
        de_data.de_lpf_l = de_tuning_cfg_data->params.de_lpf_l;
      }
#endif
    }
    err = SetDetailEnhancerConfig(de_data);
    if (err) {
      DLOGW("SetDetailEnhancerConfig failed. err = %d", err);
    }
    de_tuning_cfg_data->cfg_pending = false;
  }
  return err;
}

DisplayError SdmDisplay::GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
  DisplayError error = kErrorNone;

  DisplayConfigFixedInfo fixed_info = {};
  error = display_intf_->GetConfig(&fixed_info);

  if (error != kErrorNone) {
      DLOGE("Failed to get fixed info. Error = %d", error);
      return error;
  }

  hdr_supported_ = fixed_info.hdr_supported;

  if (!fixed_info.hdr_supported) {
      DLOGI("HDR is not supported");
      return error;
  }

  static const float kLuminanceFactor = 10000.0;
  // luminance is expressed in the unit of 0.0001 cd/m2, convert it to 1cd/m2.
  max_luminance_ = FLOAT(fixed_info.max_luminance)/kLuminanceFactor;
  max_average_luminance_ = FLOAT(fixed_info.average_luminance)/kLuminanceFactor;
  min_luminance_ = FLOAT(fixed_info.min_luminance)/kLuminanceFactor;

  display_hdr_info->hdr_supported = fixed_info.hdr_supported;
  display_hdr_info->hdr_eotf = fixed_info.hdr_eotf;
  display_hdr_info->hdr_metadata_type_one = fixed_info.hdr_metadata_type_one;
  display_hdr_info->max_luminance = fixed_info.max_luminance;
  display_hdr_info->average_luminance = fixed_info.average_luminance;
  display_hdr_info->min_luminance = fixed_info.min_luminance;

  return error;
}


DisplayError SdmDisplay::RestoreColorTransform() {
  DisplayError status = display_colormode_->RestoreColorTransform();
  if (status != kErrorNone) {
    DLOGE("failed to RestoreColorTransform");
  }

  return status;
}

DisplayError SdmDisplay::SetColorModeFromClientApi(int32_t color_mode_id) {
  DisplayError error = kErrorNone;
  std::string mode_string;

  error = display_intf_->GetColorModeName(color_mode_id, &mode_string);
  if (error) {
    DLOGE("Failed to get mode name for mode %d", color_mode_id);
    return kErrorParameters;
  }

  DisplayError status = display_colormode_->SetColorModeFromClientApi(mode_string);
  if (status != kErrorNone) {
    DLOGE("Failed to set mode = %d", color_mode_id);
    return status;
  }

  return status;
}
SdmNullDisplay::SdmNullDisplay(DisplayType type, CoreInterface *core_intf) {
}

SdmNullDisplay::~SdmNullDisplay() {
}

DisplayError SdmNullDisplay::CreateDisplay(uint32_t display_id) {
  return kErrorNone;
}
DisplayError SdmNullDisplay::DestroyDisplay() {
  return kErrorNone;
}
DisplayError SdmNullDisplay::Prepare(struct drm_output *output) {
  return kErrorNone;
}
DisplayError SdmNullDisplay::Commit(struct drm_output *output) {
  /**
   * TODO: We need to handle releasing the buffer references such that
   * the video buffers/frames keep moving forward in time even though
   * not displayed. This will be done at a later point of time.
   */
  return kErrorNone;
}
DisplayError SdmNullDisplay::SetDisplayState(DisplayState state, bool teardown,
                                             shared_ptr<Fence> *release_fence) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetPanelBrightness(float brightness) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::GetPanelBrightness(float *brightness) {
  return kErrorNone;
}

int SdmNullDisplay::ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                         PPDisplayAPIPayload *out_payload,
                                         PPPendingParams *pending_action) {
  return kErrorNone;
}

void SdmNullDisplay::SetIdleTimeoutMs(uint32_t timeout_ms, uint32_t inactive_ms) {
}

DisplayError SdmNullDisplay::GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::RestoreColorTransform() {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetColorModeFromClientApi(int32_t color_mode_id) {
  return kErrorNone;
}

void SdmNullDisplay::RefreshWithCachedLayerstack() {
}

void SdmNullDisplay::RefreshCallback() {
}

DisplayError SdmNullDisplay::SetVSyncState(bool enable, struct drm_output *output) {
  /**
   * TODO: drm_output_ needs to be re-initialized based on the preferred supported mode
   *       of the plugged-in display. The recent Weston release contains better APIs
   *       to handle this case. Hence this implementation will be improved based upon
   *       the recent Weston release updates.
   */
  drm_output_ = output;
  return kErrorNone;
}

DisplayError SdmNullDisplay::GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
  uint32_t props_value[3] = {0};
  char null_display_props[MAX_PROP_STR_SIZE] = {0};
  char *prop = NULL, *saveptr = NULL;

  // sdm.null.resolution format is width:height:fps
  SdmDisplayDebugger::Get()->GetProperty(SDM_NULL_DISPLAY_RESOLUTON_PROP_NAME, null_display_props);

  prop = strtok_r(null_display_props, ":", &saveptr);
  for (int i =0; i<3 && prop != NULL; i++)
  {
    props_value[i] = UINT32(atoi(prop));
    prop = strtok_r(NULL, ":", &saveptr);
  }

  if (props_value[0] == 0 || props_value[1] == 0) {
    display_config->x_pixels = SDM_DEAFULT_NULL_DISPLAY_WIDTH;
    display_config->y_pixels = SDM_DEAFULT_NULL_DISPLAY_HEIGHT;
  } else {
    display_config->x_pixels = props_value[0];
    display_config->y_pixels = props_value[1];
  }

  if (props_value[2] == 0)
    display_config->fps = SDM_DEAFULT_NULL_DISPLAY_FPS;
  else
    display_config->fps = props_value[2];

  display_config->x_dpi = SDM_DEAFULT_NULL_DISPLAY_X_DPI;
  display_config->y_dpi = SDM_DEAFULT_NULL_DISPLAY_Y_DPI;
  display_config->vsync_period_ns = UINT32(1000000000/display_config->fps);
  display_config->is_yuv = SDM_DEAFULT_NULL_DISPLAY_IS_YUV;

  return kErrorNone;
}

DisplayError SdmNullDisplay::SetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetOutputBuffer(void *buf, shared_ptr<Fence> &release_fence) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetFrameDumpConfig(uint32_t count, CwbConfig &cwb_config,
                                                int32_t format) {
    return kErrorNone;
}

DisplayError SdmNullDisplay::SetReadbackBuffer(void *gbm_buf, shared_ptr<Fence> acquire_fence,
                                                CwbConfig cwb_config) {
    return kErrorNone;
}

DisplayError SdmNullDisplay::RegisterCb(int display_id, vblank_cb_t vbcb) {
  vblank_cb_   = vbcb;

  return kErrorNone;
}


DisplayError SdmNullDisplay::SetDetailEnhancerConfig(const DisplayDetailEnhancerData &de_data) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetHWDetailedEnhancerConfig(void *params) {
  return kErrorNone;
}
SdmDisplayProxy::SdmDisplayProxy(DisplayType type, CoreInterface *core_intf,
                                 SdmDisplayBufferAllocator *buffer_allocator)
  : disp_type_(type), core_intf_(core_intf),
    sdm_disp_(type, core_intf, buffer_allocator), null_disp_(type, core_intf) {

    display_intf_ = &sdm_disp_;
    buffer_allocator_ = buffer_allocator;
    std::thread uevent_thread(UeventThread, this);
    uevent_thread_.swap(uevent_thread);
}

SdmDisplayProxy::~SdmDisplayProxy () {
    uevent_thread_exit_ = true;
    uevent_thread_.detach();
}

int SdmDisplayProxy::HandleHotplug(bool connected) {
  return kErrorNone;
}

void *SdmDisplayProxy::UeventThread(void *context) {
  if (context) {
    return reinterpret_cast<SdmDisplayProxy *>(context)->UeventThreadHandler();
  }

  return NULL;
}

#define HAL_PRIORITY_URGENT_DISPLAY (-8)

void *SdmDisplayProxy::UeventThreadHandler() {
  static char uevent_data[4096];
  int length = 0;
  prctl(PR_SET_NAME, uevent_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGE("Failed to init uevent");
    pthread_exit(0);
    return NULL;
  }

  DLOGI("SdmDisplayProxy::UeventThreadHandler");

  while (!uevent_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);
    string s(uevent_data, length);

    if (s.find("name=DSI-1") != string::npos) {
      bool connected = s.find("status=connected") != string::npos;
      DLOGI("DSI is %s !\n", connected ? "connected" : "removed");
      HandleHotplug(connected);
    }
  }
  pthread_exit(0);

  return NULL;
}

DisplayError SdmFrameDumper::DumpInputBuffer(void *buffer, InputBufferType buffer_type,
                                             uint32_t frame_index, uint32_t layer_index) {
    void *buffer_data = nullptr;
    uint32_t size = 0, width = 0, height = 0, stride = 0, format = 0;
    char dump_file_name[PATH_MAX] = "";
    int ret = 0;

    if (buffer_type == SHM_BUFFER) {
        struct wl_shm_buffer *shm = static_cast<wl_shm_buffer*>(buffer);

        format = wl_shm_buffer_get_format(shm);
        buffer_data = wl_shm_buffer_get_data(shm);
        stride = wl_shm_buffer_get_stride(shm);
        width = wl_shm_buffer_get_width(shm);
        height = wl_shm_buffer_get_height(shm);
        size = stride * height;
    } else if (buffer_type == GBM_BUFFER) {
        struct gbm_buffer *gbmbuf = static_cast<gbm_buffer*>(buffer);
        size_t bo_size = 0;

        ret = gbm_perform(GBM_PERFORM_CPU_MAP_FOR_BO, gbmbuf->bo, &buffer_data);
        if (ret != GBM_ERROR_NONE) {
            DLOGE("Failed to  mmap with err %d", ret);
            return kErrorParameters;
        }

        ret = gbm_perform(GBM_PERFORM_GET_BO_SIZE, gbmbuf->bo, &bo_size);
        if (ret != GBM_ERROR_NONE) {
            DLOGE("Failed to  get bo size with err %d", ret);
            return kErrorParameters;
        }

        size = UINT32(bo_size);
        width = gbm_bo_get_width(gbmbuf->bo);
        height = gbm_bo_get_height(gbmbuf->bo);
        format = gbm_bo_get_format(gbmbuf->bo);
    }

    snprintf(dump_file_name, sizeof(dump_file_name), "%s/input_layer%d_%dx%d_format%d_frame%d.raw",
             dump_dir_path_.c_str(), layer_index, width, height, format, frame_index);

    FILE *fp = fopen(dump_file_name, "w+");
    if (fp) {
        ret = fwrite(buffer_data, size, 1, fp);
        fclose(fp);
    }

    DLOGE("Frame Dump of %s is %s", dump_file_name, ret ? "Successful" : "Failed");

    if (!ret)
        return kErrorParameters;

    return kErrorNone;
}

void SdmFrameDumper::DumpInputThread() {
    std::thread::id thread_id = std::this_thread::get_id();
    void *buffer_data = nullptr;
    InputBufferType buffer_type = SHM_BUFFER;
    uint32_t frame_index = 0;
    uint32_t layer_index = 0;
    DisplayError error = kErrorNone;

    {
        std::unique_lock<std::mutex> lock(dump_input_thread_map_mtx_);
        while (!dump_input_thread_map_.count(thread_id)) {
            dump_input_thread_cv_.wait(lock);
        }

        auto iter = dump_input_thread_map_.find(thread_id);
        if (iter == dump_input_thread_map_.end()) {
            DLOGE("Failed to find dump thread data");
            return;
        }

        buffer_data = iter->second->buffer;
        buffer_type = iter->second->buffer_type;
        frame_index = iter->second->frame_index;
        layer_index = iter->second->layer_index;
    }

    error = DumpInputBuffer(buffer_data, buffer_type, frame_index, layer_index);
    if (error != kErrorNone) {
        DLOGE("Failed to dump frame[%d] input", frame_index);
    }

    std::lock_guard<std::mutex> lock(dump_input_thread_map_mtx_);
    dump_input_thread_map_.erase(thread_id);
    completion_cv_.notify_one();
}

void SdmFrameDumper::HandleInputDump(struct drm_output *output, uint32_t frame_index) {
    struct sdm_layer *sdm_layer = nullptr;
    uint32_t layer_index = 0;
    std::vector<shared_ptr<DumpInputData>> dump_input_data = {};

    wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
        struct weston_buffer *buffer = sdm_layer->buffer_ref.buffer;

        layer_index++;

        if (!buffer)
            continue;

        shared_ptr<DumpInputData> dump_data = std::make_shared<DumpInputData>();

        dump_data->frame_index = frame_index;
        dump_data->layer_index = layer_index;

        void *shm = static_cast<void*>(wl_shm_buffer_get(buffer->resource));
        if (shm) {
            dump_data->buffer = shm;
            dump_data->buffer_type = SHM_BUFFER;
            dump_input_data.push_back(dump_data);
            continue;
        }

        void *gbmbuf = static_cast<void*>(gbm_buffer_get(buffer->resource));
        if (gbmbuf) {
            dump_data->buffer = gbmbuf;
            dump_data->buffer_type = GBM_BUFFER;
            dump_input_data.push_back(dump_data);
        }
    }

    for (const auto &dump_data : dump_input_data) {
        CreateDumpThread(dump_data);
    }
}

void SdmFrameDumper::CreateDumpThread(shared_ptr<DumpInputData> data) {
    //Create input dump thread to dump buffer
    std::thread dump_thread(&SdmFrameDumper::DumpInputThread, this);
    std::thread::id dump_thread_id = dump_thread.get_id();

    {
        std::lock_guard<std::mutex> lock(dump_input_thread_map_mtx_);
        dump_input_thread_map_[dump_thread_id] = data;
    }

    dump_input_thread_cv_.notify_one();
    dump_thread.detach();
}

DisplayError SdmFrameDumper::DumpOutputBuffer(const BufferInfo &buffer_info, void *base,
                                              shared_ptr<Fence> &retire_fence, uint32_t index) {
    char dump_file_name[PATH_MAX] = "";
    size_t result = 0;

    if (base) {
        if (Fence::Wait(retire_fence) != kErrorNone) {
            DLOGE("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
            return kErrorParameters;
        }

        snprintf(dump_file_name, sizeof(dump_file_name), "%s/output_layer_%dx%d_%s_frame%d.raw",
                 dump_dir_path_.c_str(), buffer_info.alloc_buffer_info.aligned_width,
                 buffer_info.alloc_buffer_info.aligned_height,
                 GetFormatString(buffer_info.buffer_config.format), index);

        FILE *fp = fopen(dump_file_name, "w+");
        if (fp) {
            result = fwrite(base, buffer_info.alloc_buffer_info.size, 1, fp);
            fclose(fp);
        }

        DLOGE("Frame Dump of %s is %s", dump_file_name, result ? "Successful" : "Failed");
    } else {
        DLOGE("No available output buffer address");
        return kErrorParameters;
    }

    if (!result)
        return kErrorParameters;

    return kErrorNone;
}

void SdmFrameDumper::DumpOutputThread() {
    BufferInfo output_buffer_info = {};
    void *output_buffer_base = nullptr;
    uint32_t frame_index = 0;
    shared_ptr<Fence> retire_fence = nullptr;
    int fd = -1;
    DisplayError error = kErrorNone;
    std::thread::id thread_id = std::this_thread::get_id();

    {
        std::unique_lock<std::mutex> lock(dump_output_thread_map_mtx_);
        while (!dump_output_thread_map_.count(thread_id)) {
            dump_output_thread_cv_.wait(lock);
        }

        auto iter = dump_output_thread_map_.find(thread_id);
        if (iter == dump_output_thread_map_.end()) {
            DLOGE("cannot find dump thread data");
            return;
        }

        output_buffer_info = iter->second->buffer_info;
        output_buffer_base = iter->second->base;
        frame_index = iter->second->frame_index;
        retire_fence = iter->second->retire_fence;
        fd = iter->second->fd;
    }

    error = DumpOutputBuffer(output_buffer_info, output_buffer_base,
                             retire_fence, frame_index);
    if (error != kErrorNone) {
        DLOGE("Failed to dump frame[%d] output", frame_index);
    }

    FreeReadbackBuffer(output_buffer_info);

    std::lock_guard<std::mutex> lock(dump_output_thread_map_mtx_);
    close(fd);
    dump_output_thread_map_.erase(thread_id);
    completion_cv_.notify_one();

    return;
}

void SdmFrameDumper::CreateDumpThread(shared_ptr<DumpOutputData> data) {
    //Create ouput dump thread to dump buffer
    std::thread dump_thread(&SdmFrameDumper::DumpOutputThread, this);
    std::thread::id dump_thread_id = dump_thread.get_id();

    {
        std::lock_guard<std::mutex> lock(dump_output_thread_map_mtx_);
        dump_output_thread_map_[dump_thread_id] = data;
    }

    dump_output_thread_cv_.notify_one();
    dump_thread.detach();
}

void SdmFrameDumper::WaitDumpThreadsDone() {
    {
        //Wait output dump thread finished
        std::unique_lock<std::mutex> lock(dump_output_thread_map_mtx_);
        while(!dump_output_thread_map_.empty()) {
            DLOGE("waiting dump output thread done");
            completion_cv_.wait(lock);
        }
    }
}

DisplayError SdmFrameDumper::CreateDumpDir() {
    int status  = 0;
    const char *dir_path = dump_dir_path_.c_str();

    status = mkdir(dir_path, 777);
    if (errno == EEXIST) {
        return kErrorNone;
    }

    if ((status != 0) && errno != EEXIST) {
        DLOGE("Failed to create %s directory errno = %d, desc = %s", dir_path,
               errno, strerror(errno));
        return kErrorPermission;
    } else {
        //Even if directory exists already, need to explicitly change the permission.
        status = chmod(dir_path, 0777);
    }

    if (status != 0) {
        DLOGE("Failed to change permissions on %s directory", dir_path);
        return kErrorPermission;
    }

    return kErrorNone;
}

void SdmFrameDumper::FreeReadbackBuffer(BufferInfo &output_buffer_info) {
    int ret = 0;
    struct gbm_bo *bo = reinterpret_cast<struct gbm_bo *>(output_buffer_info.private_data);

    // Unmap and Free buffer
    if (bo != nullptr) {
        int ret = gbm_perform(GBM_PERFORM_CPU_UNMAP_FOR_BO, bo);
        if (ret != GBM_ERROR_NONE) {
            DLOGE("Failed to Unmap gbm buffer");
        }
    }

    buffer_allocator_->FreeBuffer(&output_buffer_info);
}

DisplayError SdmFrameDumper::CreateReadbackBuffer(BufferInfo &output_buffer_info, void **base) {
    int ret = 0;
    struct gbm_bo *bo = nullptr;
    DisplayError error = kErrorNone;

    //Allocate and map output buffer
    error = static_cast<DisplayError>(buffer_allocator_->AllocateBuffer(&output_buffer_info));
    if (error != kErrorNone) {
        DLOGE("Failed to allocate buffer");
        output_buffer_info = {};
        return error;
    }

    bo = reinterpret_cast<struct gbm_bo *>(output_buffer_info.private_data);
    ret = gbm_perform(GBM_PERFORM_CPU_MAP_FOR_BO, bo, base);
    if (ret != GBM_ERROR_NONE) {
        DLOGE("Failed to mmap with err %d", ret);
        buffer_allocator_->FreeBuffer(&output_buffer_info);
        output_buffer_info = {};
        return kErrorParameters;
    }

    return error;
}

SdmFrameDumper::SdmFrameDumper(int display_id, const char *display_string,
                               SdmDisplayBufferAllocator *buffer_allocator) {
    int status =0 ;
    char dir_path[PATH_MAX] = "";

    snprintf(dir_path, sizeof(dir_path), "%s/frame_dump_disp_id_%02u_%s",
             SdmDisplayDebugger::DumpDir(), UINT32(display_id), display_string);

    dump_dir_path_ = dir_path;
    buffer_allocator_ = buffer_allocator;
}

SdmFrameDumper::~SdmFrameDumper() {
    WaitDumpThreadsDone();
}

}  // namespace sdm
