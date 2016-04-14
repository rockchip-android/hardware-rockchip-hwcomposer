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

#ifndef ANDROID_DRM_PLANE_H_
#define ANDROID_DRM_PLANE_H_

#include "drmcrtc.h"
#include "drmproperty.h"

#include <stdint.h>
#include <xf86drmMode.h>
#include <vector>

namespace android {

enum DrmWinType {
        DRM_WIN0,
        DRM_WIN1,
        DRM_WIN2,
        DRM_WIN3,
        DRM_CURSOR,
};

enum DrmAreaType {
        DRM_AREA0,
        DRM_AREA1,
        DRM_AREA2_0,
        DRM_AREA2_1,
        DRM_AREA2_2,
        DRM_AREA2_3,
        DRM_AREA3_0,
        DRM_AREA3_1,
        DRM_AREA3_2,
        DRM_AREA3_3,
};

class DrmResources;

class DrmPlane {
 public:
  DrmPlane(DrmResources *drm, drmModePlanePtr p);
  ~DrmPlane();

  int Init();

  uint32_t id() const;

  bool GetCrtcSupported(const DrmCrtc &crtc) const;

  uint32_t type() const;

  const DrmProperty &crtc_property() const;
  const DrmProperty &fb_property() const;
  const DrmProperty &crtc_x_property() const;
  const DrmProperty &crtc_y_property() const;
  const DrmProperty &crtc_w_property() const;
  const DrmProperty &crtc_h_property() const;
  const DrmProperty &src_x_property() const;
  const DrmProperty &src_y_property() const;
  const DrmProperty &src_w_property() const;
  const DrmProperty &src_h_property() const;
  const DrmProperty &rotation_property() const;
  const DrmProperty &alpha_property() const;
  const DrmProperty &yuv_property() const;
  const DrmProperty &scale_property() const;
  bool is_reserved();
  void set_reserved(bool b_reserved);

 private:
  DrmPlane(const DrmPlane &);

  DrmResources *drm_;
  uint32_t id_;

  uint32_t possible_crtc_mask_;

  uint32_t type_;

  DrmProperty crtc_property_;
  DrmProperty fb_property_;
  DrmProperty crtc_x_property_;
  DrmProperty crtc_y_property_;
  DrmProperty crtc_w_property_;
  DrmProperty crtc_h_property_;
  DrmProperty src_x_property_;
  DrmProperty src_y_property_;
  DrmProperty src_w_property_;
  DrmProperty src_h_property_;
  DrmProperty rotation_property_;
  DrmProperty alpha_property_;

  /*rockchip*/
  DrmProperty yuv_property_;
  DrmProperty scale_property_;
  bool b_reserved_;
};
}

#endif  // ANDROID_DRM_PLANE_H_
