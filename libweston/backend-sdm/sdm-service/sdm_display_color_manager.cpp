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
#include <sys/un.h>

#include "sdm_display_color_manager.h"
#include "sdm_display_buffer_allocator.h"
#include "sdm_display.h"

#define __CLASS__ "SDMColorManager"

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

}  // namespace sdm
