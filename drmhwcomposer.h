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

#ifndef ANDROID_DRM_HWCOMPOSER_H_
#define ANDROID_DRM_HWCOMPOSER_H_

#include <stdbool.h>
#include <stdint.h>

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include "autofd.h"
#include "separate_rects.h"
#include "drmhwcgralloc.h"
#include "hwc_debug.h"

/*hwc version*/
#define GHWC_VERSION                    "0.34"

/* hdr usage */
#define HDRUSAGE                                       0x3000000

/* msleep for hotplug in event */
#define HOTPLUG_MSLEEP			(200)

// hdmi status path
#define HDMI_STATUS_PATH		"/sys/devices/platform/display-subsystem/drm/card0/card0-HDMI-A-1/status"

struct hwc_import_context;

int hwc_import_init(struct hwc_import_context **ctx);
int hwc_import_destroy(struct hwc_import_context *ctx);

int hwc_import_bo_create(int fd, struct hwc_import_context *ctx,
                         buffer_handle_t buf, struct hwc_drm_bo *bo);
bool hwc_import_bo_release(int fd, struct hwc_import_context *ctx,
                           struct hwc_drm_bo *bo);

namespace android {

#define UN_USED(arg)     (arg=arg)

#if USE_AFBC_LAYER
#ifdef TARGET_BOARD_PLATFORM_RK3368
#define HAL_FB_COMPRESSION_NONE                0
#else
#define GRALLOC_ARM_INTFMT_EXTENSION_BIT_START          32
/* This format will use AFBC */
#define	    GRALLOC_ARM_INTFMT_AFBC                     (1ULL << (GRALLOC_ARM_INTFMT_EXTENSION_BIT_START+0))
#endif

#define SKIP_BOOT                                       (1)
#define MAGIC_USAGE_FOR_AFBC_LAYER                      (0x88)
#endif

#if SKIP_BOOT
#define BOOT_COUNT       (2)
#endif

typedef enum tagMode3D
{
	NON_3D = 0,
	H_3D=1,
	V_3D=2,
	FPS_3D=8,
}Mode3D;

class Importer;

class DrmHwcBuffer {
 public:
  DrmHwcBuffer() = default;
  DrmHwcBuffer(const hwc_drm_bo &bo, Importer *importer)
      : bo_(bo), importer_(importer) {
  }
  DrmHwcBuffer(DrmHwcBuffer &&rhs) : bo_(rhs.bo_), importer_(rhs.importer_) {
    rhs.importer_ = NULL;
  }

  ~DrmHwcBuffer() {
    Clear();
  }

  DrmHwcBuffer &operator=(DrmHwcBuffer &&rhs) {
    Clear();
    importer_ = rhs.importer_;
    rhs.importer_ = NULL;
    bo_ = rhs.bo_;
    return *this;
  }

  operator bool() const {
    return importer_ != NULL;
  }

  const hwc_drm_bo *operator->() const;

  void Clear();

#if RK_VIDEO_SKIP_LINE
  int ImportBuffer(buffer_handle_t handle, Importer *importer, bool bSkipLine);
#else
  int ImportBuffer(buffer_handle_t handle, Importer *importer);
#endif

 private:
  hwc_drm_bo bo_;
  Importer *importer_ = NULL;
};

class DrmHwcNativeHandle {
 public:
  DrmHwcNativeHandle() = default;

  DrmHwcNativeHandle(const gralloc_module_t *gralloc, native_handle_t *handle)
      : gralloc_(gralloc), handle_(handle) {
  }

  DrmHwcNativeHandle(DrmHwcNativeHandle &&rhs) {
    gralloc_ = rhs.gralloc_;
    rhs.gralloc_ = NULL;
    handle_ = rhs.handle_;
    rhs.handle_ = NULL;
  }

  ~DrmHwcNativeHandle();

  DrmHwcNativeHandle &operator=(DrmHwcNativeHandle &&rhs) {
    Clear();
    gralloc_ = rhs.gralloc_;
    rhs.gralloc_ = NULL;
    handle_ = rhs.handle_;
    rhs.handle_ = NULL;
    return *this;
  }

  int CopyBufferHandle(buffer_handle_t handle, const gralloc_module_t *gralloc);

  void Clear();

  buffer_handle_t get() const {
    return handle_;
  }

 private:
  const gralloc_module_t *gralloc_ = NULL;
  native_handle_t *handle_ = NULL;
};

template <typename T>
using DrmHwcRect = separate_rects::Rect<T>;

enum DrmHwcTransform {
  kIdentity = 0,
  kFlipH = 1 << 0,
  kFlipV = 1 << 1,
  kRotate90 = 1 << 2,
  kRotate180 = 1 << 3,
  kRotate270 = 1 << 4,
  kRotate0 = 1 << 5
};

enum class DrmHwcBlending : int32_t {
  kNone = HWC_BLENDING_NONE,
  kPreMult = HWC_BLENDING_PREMULT,
  kCoverage = HWC_BLENDING_COVERAGE,
};

struct DrmHwcLayer {
  buffer_handle_t sf_handle = NULL;
  int gralloc_buffer_usage = 0;
  DrmHwcBuffer buffer;
  DrmHwcNativeHandle handle;
  uint32_t transform;
  DrmHwcBlending blending = DrmHwcBlending::kNone;
  uint8_t alpha = 0xff;
  uint32_t frame_no = 0;
  DrmHwcRect<float> source_crop;
  DrmHwcRect<int> display_frame;
  std::vector<DrmHwcRect<int>> source_damage;

  UniqueFd acquire_fence;
  OutputFd release_fence;

  bool bSkipLayer;
  bool is_match;
  bool is_take;
  bool is_yuv;
  bool is_scale;
  bool is_large;
  int zpos;

#if USE_AFBC_LAYER
  uint64_t internal_format;
  bool is_afbc;
#endif

#if RK_RGA
  bool is_rotate_by_rga;
#endif
  float h_scale_mul;
  float v_scale_mul;
#if RK_VIDEO_SKIP_LINE
  bool bSkipLine;
#endif
  bool bClone_;
  bool bFbTarget_;
  bool bUse;
  bool bMix;
  int stereo;
  hwc_layer_1_t *raw_sf_layer;
  int format;
  int width;
  int height;
  int stride;
  int bpp;
  int group_id;
  int share_id;
  uint32_t colorspace;
  uint16_t eotf;
  std::string name;
  size_t index;
  hwc_layer_1_t *mlayer;

  int ImportBuffer(struct hwc_context_t *ctx, hwc_layer_1_t *sf_layer, Importer *importer);
  int InitFromHwcLayer(struct hwc_context_t *ctx, int display, hwc_layer_1_t *sf_layer, Importer *importer,
                        const gralloc_module_t *gralloc, bool bClone);

void dump_drm_layer(int index, std::ostringstream *out) const;

  buffer_handle_t get_usable_handle() const {
    return handle.get() != NULL ? handle.get() : sf_handle;
  }

  bool protected_usage() const {
    return (gralloc_buffer_usage & GRALLOC_USAGE_PROTECTED) ==
           GRALLOC_USAGE_PROTECTED;
  }
};

struct DrmHwcDisplayContents {
  OutputFd retire_fence;
  std::vector<DrmHwcLayer> layers;
};
}

#endif
