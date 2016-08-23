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

#define LOG_TAG "hwc-drm-crtc"

#include "drmcrtc.h"
#include "drmresources.h"

#include <stdint.h>
#include <xf86drmMode.h>

#include <cutils/log.h>

namespace android {

DrmCrtc::DrmCrtc(DrmResources *drm, drmModeCrtcPtr c, unsigned pipe)
    : drm_(drm),
      id_(c->crtc_id),
      pipe_(pipe),
      display_(-1),
      x_(c->x),
      y_(c->y),
      width_(c->width),
      height_(c->height),
      mode_(&c->mode),
      mode_valid_(c->mode_valid),
      crtc_(c) {
}

int DrmCrtc::Init() {
  int ret = drm_->GetCrtcProperty(*this, "ACTIVE", &active_property_);
  if (ret) {
    ALOGE("Failed to get ACTIVE property");
    return ret;
  }

  ret = drm_->GetCrtcProperty(*this, "MODE_ID", &mode_property_);
  if (ret) {
    ALOGE("Failed to get MODE_ID property");
    return ret;
  }

#if RK_DRM_HWC
  ret = drm_->GetCrtcProperty(*this, "AFBDC_PLANE_ID", &afbc_property_);
  if (ret) {
    ALOGE("Failed to get AFBDC_PLANE_ID property");
    return ret;
  }
#endif

  return 0;
}

uint32_t DrmCrtc::id() const {
  return id_;
}

unsigned DrmCrtc::pipe() const {
  return pipe_;
}

int DrmCrtc::display() const {
  return display_;
}

void DrmCrtc::set_display(int display) {
  display_ = display;
}

bool DrmCrtc::can_bind(int display) const {
  return display_ == -1 || display_ == display;
}

const DrmProperty &DrmCrtc::active_property() const {
  return active_property_;
}

const DrmProperty &DrmCrtc::mode_property() const {
  return mode_property_;
}

#if RK_DRM_HWC
const DrmProperty &DrmCrtc::afbc_property() const {
  return afbc_property_;
}
#endif

#if USE_AFBC_LAYER
int DrmCrtc::set_afbc_property(uint64_t plane_id) {
    int afbc_id = afbc_property_.id();
	int ret = drmModeObjectSetProperty(drm_->fd(), id_, DRM_MODE_OBJECT_CRTC,
				       afbc_id, plane_id);
	if (ret < 0)
		ALOGE("failed to set %s[%d] property id[%d] to %d: %s\n",
			"DRM_MODE_OBJECT_CRTC", id_, afbc_id, plane_id, strerror(errno));

    return ret;
}
#endif

#if RK_DRM_HWC_DEBUG
void DrmCrtc::dump_crtc(std::ostringstream *out) const
{
	uint32_t j;

	*out << crtc_->crtc_id << "\t"
	     << crtc_->buffer_id << "\t"
	     << "(" << crtc_->x << "," << crtc_->y << ")\t("
	     << crtc_->width << "x" << crtc_->height << ")\n";

	drm_->dump_mode(&crtc_->mode, out);

	drm_->DumpCrtcProperty(*this,out);
}
#endif
}
