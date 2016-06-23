/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_DRM_H_
#define ANDROID_DRM_H_

#include "drmcompositor.h"
#include "drmconnector.h"
#include "drmcrtc.h"
#include "drmencoder.h"
#include "drmeventlistener.h"
#include "drmplane.h"

#include <stdint.h>

namespace android {
#if RK_DRM_HWC_DEBUG
#define type_name_define(res) \
const char * res##_str(int type);
#endif

#if RK_DRM_HWC
typedef struct tagPlaneGroup{
        bool     bUse;
        uint32_t zpos;
        uint64_t share_id;
        std::vector<DrmPlane*> planes;
}PlaneGroup;
#endif

class DrmResources {
 public:
  DrmResources();
  ~DrmResources();

  int Init();

  int fd() const {
    return fd_.get();
  }

  const std::vector<std::unique_ptr<DrmConnector>> &connectors() const {
    return connectors_;
  }

  const std::vector<std::unique_ptr<DrmPlane>> &planes() const {
    return planes_;
  }

#if RK_DRM_HWC
   const std::vector<DrmPlane*> &sort_planes() const {
    return sort_planes_;
  }
#endif

  DrmConnector *GetConnectorForDisplay(int display) const;
  DrmCrtc *GetCrtcForDisplay(int display) const;
  DrmPlane *GetPlane(uint32_t id) const;
  DrmCompositor *compositor();
  DrmEventListener *event_listener();

  int GetPlaneProperty(const DrmPlane &plane, const char *prop_name,
                       DrmProperty *property);
  int GetCrtcProperty(const DrmCrtc &crtc, const char *prop_name,
                      DrmProperty *property);
  int GetConnectorProperty(const DrmConnector &connector, const char *prop_name,
                           DrmProperty *property);

  uint32_t next_mode_id();
  int SetDisplayActiveMode(int display, const DrmMode &mode);
  int SetDpmsMode(int display, uint64_t mode);

  int CreatePropertyBlob(void *data, size_t length, uint32_t *blob_id);
  int DestroyPropertyBlob(uint32_t blob_id);
#if RK_DRM_HWC_DEBUG
  type_name_define(encoder_type);
  type_name_define(connector_status);
  type_name_define(connector_type);
  int DumpPlaneProperty(const DrmPlane &plane, std::ostringstream *out);
  int DumpCrtcProperty(const DrmCrtc &crtc, std::ostringstream *out);
  int DumpConnectorProperty(const DrmConnector &connector, std::ostringstream *out);
  void dump_mode(drmModeModeInfo *mode,std::ostringstream *out);
#endif

#if RK_DRM_HWC
  std::vector<PlaneGroup *>& GetPlaneGroups();
#endif

 private:
  int TryEncoderForDisplay(int display, DrmEncoder *enc);
  int GetProperty(uint32_t obj_id, uint32_t obj_type, const char *prop_name,
                  DrmProperty *property);

  int CreateDisplayPipe(DrmConnector *connector);

#if RK_DRM_HWC_DEBUG
  void dump_blob(uint32_t blob_id, std::ostringstream *out);
  void dump_prop(drmModePropertyPtr prop,
                     uint32_t prop_id, uint64_t value, std::ostringstream *out);
  int DumpProperty(uint32_t obj_id, uint32_t obj_type, std::ostringstream *out);
#endif

  UniqueFd fd_;
  uint32_t mode_id_ = 0;

  std::vector<std::unique_ptr<DrmConnector>> connectors_;
  std::vector<std::unique_ptr<DrmEncoder>> encoders_;
  std::vector<std::unique_ptr<DrmCrtc>> crtcs_;
  std::vector<std::unique_ptr<DrmPlane>> planes_;
#if RK_DRM_HWC
  std::vector<DrmPlane*> sort_planes_;
  std::vector<PlaneGroup *> plane_groups_;
#endif
  DrmCompositor compositor_;
  DrmEventListener event_listener_;
};
}

#endif  // ANDROID_DRM_H_
