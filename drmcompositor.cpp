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

#define LOG_TAG "hwc-drm-compositor"

#include "drmcompositor.h"
#include "drmdisplaycompositor.h"
#include "drmresources.h"
#include "platform.h"

#include <sstream>
#include <stdlib.h>

#include <cutils/log.h>

namespace android {

DrmCompositor::DrmCompositor(DrmResources *drm) : drm_(drm), frame_no_(0) {
}

DrmCompositor::~DrmCompositor() {
}

int DrmCompositor::Init() {
  int i;
  for (i = 0; i < HWC_NUM_PHYSICAL_DISPLAY_TYPES; i++) {
    int ret = compositor_map_[i].Init(drm_, i);
    if (ret) {
      ALOGE("Failed to initialize display compositor for %d", i);
      return ret;
    }
  }
  planner_ = Planner::CreateInstance(drm_);
  if (!planner_) {
    ALOGE("Failed to create planner instance for composition");
    return -ENOMEM;
  }

  return 0;
}

std::unique_ptr<DrmComposition> DrmCompositor::CreateComposition(
    Importer *importer) {
  std::unique_ptr<DrmComposition> composition(
      new DrmComposition(drm_, importer, planner_.get()));
  int ret = composition->Init(++frame_no_);
  if (ret) {
    ALOGE("Failed to initialize drm composition %d", ret);
    return nullptr;
  }
  return composition;
}

int DrmCompositor::QueueComposition(
    std::unique_ptr<DrmComposition> composition) {
  int i, ret;

  ret = composition->Plan(compositor_map_);
  if (ret)
    return ret;

  ret = composition->DisableUnusedPlanes();
  if (ret)
    return ret;

  for (i = 0; i < HWC_NUM_PHYSICAL_DISPLAY_TYPES; i++) {
    int ret = compositor_map_[i].QueueComposition(
        composition->TakeDisplayComposition(i));
    if (ret) {
      ALOGV("Failed to queue composition for display %d (%d)", i, ret);
      return ret;
    }
  }

  return 0;
}

int DrmCompositor::Composite() {
  /*
   * This shouldn't be called, we should be calling Composite() on the display
   * compositors directly.
   */
  ALOGE("Calling base drm compositor Composite() function");
  return -EINVAL;
}

void DrmCompositor::ClearDisplay(int display) {
  compositor_map_[display].ClearDisplay();
}

void DrmCompositor::Dump(std::ostringstream *out) const {
  *out << "DrmCompositor stats:\n";
  int i;
  for (i = 0; i < HWC_NUM_PHYSICAL_DISPLAY_TYPES; i++) {
    compositor_map_[i].Dump(out);
  }
}
}
