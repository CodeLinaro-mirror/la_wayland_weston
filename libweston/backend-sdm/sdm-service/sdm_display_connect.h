/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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
*
* Changes from Qualcomm Technologies, Inc. are provided under the following license:
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#ifndef SDM_DISPLAY_CONNECT_H
#define SDM_DISPLAY_CONNECT_H

#include "sdm-service/sdm_display_interface.h"

/*! @brief This enum represents different error codes that display interfaces may return.
*/
typedef enum {
  kErrorNone,             //!< Call executed successfully.
  kErrorUndefined,        //!< An unspecified error has occured.
  kErrorNotSupported,     //!< Requested operation is not supported.
  kErrorPermission,       //!< Operation is not permitted in current state.
  kErrorVersion,          //!< Client is using advanced version of interfaces and calling into an
                          //!< older version of display library.
  kErrorDataAlignment,    //!< Client data structures are not aligned on naturual boundaries.
  kErrorInstructionSet,   //!< 32-bit client is calling into 64-bit library or vice versa.
  kErrorParameters,       //!< Invalid parameters passed to a method.
  kErrorFileDescriptor,   //!< Invalid file descriptor.
  kErrorMemory,           //!< System is running low on memory.
  kErrorResources,        //!< Not enough hardware resources available to execute call.
  kErrorHardware,         //!< A hardware error has occured.
  kErrorTimeOut,          //!< The operation has timed out to prevent client from waiting forever.
  kErrorShutDown,         //!< Driver is processing shutdown sequence
  kErrorPerfValidation,   //!< Bandwidth or Clock requirement validation failure.
  kErrorNoAppLayers,      //!< No App layer(s) in the draw cycle.
  kErrorRotatorValidation,  //!< Rotator configuration validation failure.
  kErrorNotValidated,     //!< Draw cycle has not been validated.
  kErrorCriticalResource,   //!< Critical resource allocation has failed.
  kErrorDeviceRemoved,    //!< A device was removed unexpectedly.
  kErrorDriverData,       //!< Expected information from the driver is missing.
  kErrorDeferred,         //!< Call's intended action is being deferred to a later time.
  kErrorNeedsCommit,      //!< Display is expecting a Commit() to be issued.
  kErrorNeedsValidate,    //!< Validate Phase is needed for this draw cycle.
  kErrorNeedsLutRegen,    //!< Tonemapping LUT regen is needed for this draw cycle.
  kErrorNeedsQosRecalc,   //!< QoS data recalculation is needed for this draw cycle.
  kErrorNeedsQosRecalcAndLutRegen,  //!< QoS data recalculation and Tonemapping LUT regen is needed
                                    //   for this draw cycle.
  kSeamlessNotAllowed,    //!< Seemless switch between configs not allowed.
  kErrorDeviceBusy,       //!< Device is currently busy with other tasks.
  kErrorTryAgain,         //!< Try the task again.
  kErrorConfigMismatch,   //!< Inform client when config index between SDM and DAL are different
}  DisplayError;

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief Method to create and internally store the handle to display core interface.

    @details This method is the entry point into the display core. Client can create
    and operate on different display devices only after valid interface handle is
    internally stored after invoking this method. Successfully obtaining valid interface
    handle and storing internally is indicated by return code kErrorNone. An object of
    display core is created and handle to this object returned internally by the display
    core interface via output parameter is internally stored. This interface shall be
    called only once. Parameters used in this method internally and not exposed to Client
    are noted below for information purposes only.

    @param[in] use_pixman \link bool \endlink
    @return \link DisplayError \endlink

    @sa DestroyCore
*/
DisplayError CreateCore(bool use_pixman);

/*! @brief Method to release internally stored handle to display core interface.

    @details The object of corresponding display core is destroyed when this method is
    invoked and internally stored handle is released. Client must explicitly destroy
    all created display device objects associated with this handle before invoking this
    method even though this method makes a modest effort to destroy all existing and
    created display device objects.

    @param[in] interface (internally used by this method) \link CoreInterface \endlink

    @return \link DisplayError \endlink

    @sa CreateCore
*/
DisplayError DestroyCore();

/*! @brief Method to get characteristics of the first display.

    @details Client shall use this method to determine type of the first display.

    @param[in] display_id that this method will fill up with info.

    @return \link DisplayError \endlink

*/
DisplayError GetFirstDisplayType(int *display_id);

/*! @brief Method to create a display device for a given display id.

    @details Client shall use this method to create each of the display id.
    display_id must be valid to create display.

    @param[in] display_id \link int \endlink
    @return \link DisplayError \endlink

    @sa DestroyDisplay
*/
DisplayError CreateDisplay(uint32_t display_id);

/*! @brief Method to destroy a display device.

    @details Client shall use this method to destroy each of the created
    display device objects.

    @param[in] display_id \link int \endlink

    @return \link DisplayError \endlink

    @sa CreateDisplay
*/
DisplayError DestroyDisplay(uint32_t display_id);

/*! @brief Method to create a display device for a given display id even if
    display was already created. This method forces the display to be re-created
    after destroying the one which was previously created, if so.

    @details Client shall use this method to force create each of the display id.
    display_id must be valid to create display.

    @param[in] display_id \link int \endlink
    @return \link DisplayError \endlink

    @sa DestroyDisplay
*/
int ReconfigureDisplay(uint32_t display_id);

/*! @brief Method to compose layers associated with given frame.

    @details Client shall send all layers associated with a frame
    targeted for current display using this method and check the layers
    which can be handled completely in display manager.

    This method can be called multiple times but only last call prevails. This method must be
    followed by Commit().

    @param[in] display_id \link int \endlink
    @param[in] drm_output \link struct drm_output \endlink

    @return \link DisplayError \endlink

    @sa Commit
*/
DisplayError Prepare(uint32_t display_id, struct drm_output *output);

/*! @brief Method to commit layers of a frame submitted in a former call to Prepare().

    @details Client shall call this method to submit layers for final composition.
    The composed output shall be displayed on the panel or written in output buffer.

    This method shall be called only once for each frame.

    In the event of an error as well, this call will cause any fences returned in the
    previous call to Commit() to eventually become signaled, so the client's wait on
    fences can be released to prevent deadlocks.

    @param[in] display_id \link int \endlink
    @param[inout] drm_output \link struct drm_output \endlink

    @return \link DisplayError \endlink

    @sa Prepare
*/
DisplayError Commit(uint32_t display_id, struct drm_output *output);

/*! @brief Method to obtain display property count for a display_id requested.
    @details Client shall use this method to display properties of requested
    display id.

    @param[in] display_id \link int \endlink
    @param[in] count \link display mode count \endlink

    @return \link DisplayError \endlink

    @sa
*/
DisplayError GetDisplayConfigCount(uint32_t display_id, uint32_t *count);

/*! @brief Method to obtain display property for a display_id requested by index.
    @details Client shall use this method to get display properties of requested
    display id by index.

    @param[in] display_id \link int \endlink
    @param[in] index \link uint \endlink
    @param[in] display_config \link struct DisplayConfigInfo \endlink

    @return \link DisplayError \endlink

    @sa
*/
DisplayError GetDisplayConfigurationByIndex(uint32_t display_id, uint32_t index,
                                            struct DisplayConfigInfo *display_config);

/*! @brief Method to set display property for a display_id requested by index.
    @details Client shall use this method to set display properties of requested
    display id by index.

    @param[in] display_id \link int \endlink
    @param[in] index \link uint \endlink

    @return \link DisplayError \endlink

    @sa
*/
DisplayError SetDisplayConfigurationByIndex(uint32_t display_id, uint32_t index);

/*! @brief Method to obtain display property for a display_id requested.
    @details Client shall use this method to display properties of requested
    display id.

    @param[in] display_id \link int \endlink
    @param[in] display_config \link struct DisplayConfigInfo \endlink

    @return \link DisplayError \endlink

    @sa
*/
DisplayError GetDisplayConfiguration(uint32_t display_id, struct DisplayConfigInfo *display_config);

/*! @brief Method to Set display property for a display_id requested.
    @details Client shall use this method to display properties of requested
    display id.

    @param[in] display_id \link int \endlink
    @param[in] display_config \link struct DisplayConfigInfo \endlink

    @return \link DisplayError \endlink

    @sa
*/
DisplayError SetDisplayQsyncMode(uint32_t display_id, uint32_t mode);

/*! @brief Method to obtain display's HDR information parameters for requested display_id.
    @details Client shall use this method to obtain display's HDR capability parameters
    for requested display_id.

    @param[in] display_id \link int \endlink
    @param[in] display_config \link struct DisplayHdrInfo \endlink

    @return \link DisplayError \endlink

    @sa
*/
bool GetDisplayHdrInfo(uint32_t display_id, struct DisplayHdrInfo *display_hdr_info);

/*! @brief Method to register callbacks: VBlank Handler function to be called on
    enabling VBlank (VSync), and hotplug handler function to be called on hotplug
    uevent. SDM shall trigger a call back through this interface function.

    @param[in] display_id \link int \endlink
    @param[in] cbs \link sdm_cbs_t \endlink

    @return \link DisplayError \endlink

    @sa
*/
DisplayError RegisterCbs(uint32_t display_id, sdm_cbs_t *cbs);

/*! @brief Method to turn on power of display

    @details Client shall use this method to turn on display. DisplayError
    must have been created previously.
    to be called by hardware composer.

    @param[in] display_id \link int \endlink
    @param[in] power_mode \link int \endlink

    @return \link int \endlink

    @sa
*/
DisplayError SetDisplayState(uint32_t display_id, int power_mode);

/*! @brief Method to enable VSync State, i.e. whether to generate callback
    on next frame.

    @details Client shall use this method for enable VSync (VBlank) callback.

    @param[in] display_id \link int \endlink
    @param[in] enable \link bool \endlink
    @param[in] b \link (struct drm_backend *) \endlink

    @return \link int \endlink

    @sa
*/
DisplayError SetVSyncState(uint32_t display_id, bool enable, struct drm_output *output);

/*! @brief Method to set panel brightness..

    @details Client shall use this method to set panel brightness.

    @param[in] display_id \link int \endlink
    @param[in] brightness \link float \endlink

    @return \link int \endlink

    @sa
*/
DisplayError SetPanelBrightness(int display_id, float brightness);

/*! @brief Method to get panel brightness

    @details Client shall use this method to get panel brightness.

    @param[in] display_id \link int \endlink
    @param[in] enable \link bool \endlink

    @return \link int \endlink

    @sa
*/
DisplayError GetPanelBrightness(int display_id, float *brightness);

/*! @brief Method for obtaining master fd.

    @details client to obtaining master fd.

    @return \link int \endlink

    @sa
*/
int get_drm_master_fd(void);

uint32_t GetDisplayCount(void);

int GetDisplayInfos(void);

char* GetConnectorName(uint32_t display_id);

uint32_t GetConnectorId(uint32_t display_id);

uint32_t GetConnectorType(uint32_t display_id);

bool IsVirtualOutput(uint32_t display_id);

DisplayError SetOutputBuffer(uint32_t display_id, void *gbm_bo);

void ClearSDMLayers(struct drm_output *output);

int GetConnectedDisplaysIds(int num_displays, uint32_t *connector_ids);

bool IsDisplayCreated(uint32_t display_id);

#ifdef __cplusplus
}
#endif

#endif // SDM_DISPLAY_CONNECT_H
