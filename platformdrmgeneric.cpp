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

#define LOG_TAG "hwc-platform-drm-generic"

#include "drmresources.h"
#include "platform.h"
#include "platformdrmgeneric.h"

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/log.h>
#include <gralloc_drm_handle.h>
#include <hardware/gralloc.h>
#include "hwcutil.h"

namespace android {

#ifdef USE_DRM_GENERIC_IMPORTER
// static
Importer *Importer::CreateInstance(DrmResources *drm) {
  DrmGenericImporter *importer = new DrmGenericImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the nv importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}
#endif

DrmGenericImporter::DrmGenericImporter(DrmResources *drm) : drm_(drm) {
}

DrmGenericImporter::~DrmGenericImporter() {
}

int DrmGenericImporter::Init() {
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc_);
  if (ret) {
    ALOGE("Failed to open gralloc module");
    return ret;
  }
  return 0;
}

uint32_t DrmGenericImporter::ConvertHalFormatToDrm(uint32_t hal_format) {
  switch (hal_format) {
    case HAL_PIXEL_FORMAT_RGB_888:
      return DRM_FORMAT_BGR888;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return DRM_FORMAT_ARGB8888;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return DRM_FORMAT_XBGR8888;
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return DRM_FORMAT_ABGR8888;
    //Fix color error in NenaMark2.
    case HAL_PIXEL_FORMAT_RGB_565:
      return DRM_FORMAT_RGB565;
    case HAL_PIXEL_FORMAT_YV12:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_YCrCb_NV12:
      return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_YCrCb_NV12_10:
      return DRM_FORMAT_NV12_10;
    default:
      ALOGE("Cannot convert hal format to drm format %u", hal_format);
      return -EINVAL;
  }
}

#ifndef u64
#define u64 uint64_t
#endif
int DrmGenericImporter::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo
#if RK_VIDEO_SKIP_LINE
, bool bSkipLine
#endif
) {
  gralloc_drm_handle_t *gr_handle = gralloc_drm_handle(handle);
  if (!gr_handle)
  {
    gralloc_drm_unlock_handle(handle);
    return -EINVAL;
  }

  uint32_t gem_handle;
  int ret = drmPrimeFDToHandle(drm_->fd(), gr_handle->prime_fd, &gem_handle);
  if (ret) {
    gralloc_drm_unlock_handle(handle);
    ALOGE("failed to import prime fd %d ret=%d", gr_handle->prime_fd, ret);
    return ret;
  }

  memset(bo, 0, sizeof(hwc_drm_bo_t));
  if(gr_handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
  {
      bo->width = gr_handle->width/1.25;
      bo->width = ALIGN_DOWN(bo->width,2);
  }
  else
  {
      bo->width = gr_handle->width;
  }

#if RK_VIDEO_SKIP_LINE
  if(bSkipLine)
  {
      bo->pitches[0] = gr_handle->stride*2;
      bo->height = gr_handle->height/2;
  }
  else
#endif
  {
      bo->pitches[0] = gr_handle->stride;
      bo->height = gr_handle->height;
  }

  bo->format = ConvertHalFormatToDrm(gr_handle->format);
  bo->gem_handles[0] = gem_handle;
  bo->offsets[0] = 0;

  if(gr_handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12 || gr_handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
  {
    bo->pitches[1] = bo->pitches[0];
    bo->gem_handles[1] = gem_handle;
    bo->offsets[1] = bo->pitches[1] * bo->height;
  }
#if USE_AFBC_LAYER
  __u64 modifier[4];
  memset(modifier, 0, sizeof(modifier));

  if (isAfbcInternalFormat(gr_handle->internal_format))
    modifier[0] = DRM_FORMAT_MOD_ARM_AFBC;

  ret = drmModeAddFB2_ext(drm_->fd(), bo->width, bo->height, bo->format,
                      bo->gem_handles, bo->pitches, bo->offsets, modifier,
		      &bo->fb_id, DRM_MODE_FB_MODIFIERS);
#else
  ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                      bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id, 0);
#endif
  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    ALOGE("ImportBuffer fd=%d,w=%d,h=%d,format=0x%x,bo->format=0x%x,gem_handle=%d,bo->pitches[0]=%d,fb_id=%d",
        drm_->fd(), bo->width, bo->height, gr_handle->format,bo->format,
        gem_handle, bo->pitches[0], bo->fb_id);
#if RK_VIDEO_SKIP_LINE
    ALOGE("bSkipLine=%d",bSkipLine);
#endif
    gralloc_drm_unlock_handle(handle);
    return ret;
  }

  //Fix "Failed to close gem handle" bug which lead by no reference counting.
#if 1
    struct drm_gem_close gem_close;
    int num_gem_handles;
    memset(&gem_close, 0, sizeof(gem_close));
    num_gem_handles = 1;

    for (int i = 0; i < num_gem_handles; i++) {
        if (!bo->gem_handles[i])
            continue;

        gem_close.handle = bo->gem_handles[i];
        int ret = drmIoctl(drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
        if (ret)
            ALOGE("Failed to close gem handle %d %d", i, ret);
        else
            bo->gem_handles[i] = 0;
    }
#endif
  gralloc_drm_unlock_handle(handle);

  return ret;
}

int DrmGenericImporter::ReleaseBuffer(hwc_drm_bo_t *bo) {
  if (bo->fb_id)
    if (drmModeRmFB(drm_->fd(), bo->fb_id))
      ALOGE("Failed to rm fb");

#if 0
  struct drm_gem_close gem_close;
  memset(&gem_close, 0, sizeof(gem_close));
  int num_gem_handles = sizeof(bo->gem_handles) / sizeof(bo->gem_handles[0]);
  for (int i = 0; i < num_gem_handles; i++) {
    if (!bo->gem_handles[i])
      continue;

    gem_close.handle = bo->gem_handles[i];
    int ret = drmIoctl(drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
    if (ret)
      ALOGE("Failed to close gem handle %d %d", i, ret);
    else
      bo->gem_handles[i] = 0;
  }
#endif
  return 0;
}

#ifdef USE_DRM_GENERIC_IMPORTER
std::unique_ptr<Planner> Planner::CreateInstance(DrmResources *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}
#endif
}
