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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#define LOG_TAG "hwcomposer-drm"

#include "drmhwcomposer.h"
#include "drmeventlistener.h"
#include "drmresources.h"
#include "platform.h"
#include "virtualcompositorworker.h"
#include "vsyncworker.h"

#include <stdlib.h>

#include <cinttypes>
#include <map>
#include <vector>
#include <sstream>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <sw_sync.h>
#include <sync/sync.h>
#include <utils/Trace.h>
#include <drm_fourcc.h>
#if RK_DRM_GRALLOC
#include "gralloc_drm_handle.h"
#endif
#include <linux/fb.h>

#include "hwc_util.h"
#include "hwc_rockchip.h"

#define UM_PER_INCH 25400

namespace android {

static int hwc_set_active_config(struct hwc_composer_device_1 *dev, int display,
                                 int index);


#if SKIP_BOOT
static unsigned int g_boot_cnt = 0;
#endif
#if RK_INVALID_REFRESH
hwc_context_t* g_ctx = NULL;
#endif

class DummySwSyncTimeline {
 public:
  int Init() {
    int ret = timeline_fd_.Set(sw_sync_timeline_create());
    if (ret < 0)
      return ret;
    return 0;
  }

  UniqueFd CreateDummyFence() {
    int ret = sw_sync_fence_create(timeline_fd_.get(), "dummy fence",
                                   timeline_pt_ + 1);
    if (ret < 0) {
      ALOGE("Failed to create dummy fence %d", ret);
      return ret;
    }

    UniqueFd ret_fd(ret);

    ret = sw_sync_timeline_inc(timeline_fd_.get(), 1);
    if (ret) {
      ALOGE("Failed to increment dummy sync timeline %d", ret);
      return ret;
    }

    ++timeline_pt_;
    return ret_fd;
  }

 private:
  UniqueFd timeline_fd_;
  int timeline_pt_ = 0;
};

struct CheckedOutputFd {
  CheckedOutputFd(int *fd, const char *description,
                  DummySwSyncTimeline &timeline)
      : fd_(fd), description_(description), timeline_(timeline) {
  }
  CheckedOutputFd(CheckedOutputFd &&rhs)
      : description_(rhs.description_), timeline_(rhs.timeline_) {
    std::swap(fd_, rhs.fd_);
  }

  CheckedOutputFd &operator=(const CheckedOutputFd &rhs) = delete;

  ~CheckedOutputFd() {
    if (fd_ == NULL)
      return;

    if (*fd_ >= 0)
      return;

    *fd_ = timeline_.CreateDummyFence().Release();

    if (*fd_ < 0)
      ALOGE("Failed to fill %s (%p == %d) before destruction",
            description_.c_str(), fd_, *fd_);
  }

 private:
  int *fd_ = NULL;
  std::string description_;
  DummySwSyncTimeline &timeline_;
};

/**
 * sys.3d_resolution.main 1920x1080p60-114693:148500
 * width x height p|i refresh-flag:clock
 */
static int update_display_bestmode(hwc_drm_display_t *hd, int display, DrmConnector *c)
{
  char resolution[PROPERTY_VALUE_MAX];
  char resolution_3d[PROPERTY_VALUE_MAX];
  uint32_t width, height, vrefresh;
  uint32_t width_3d, height_3d, vrefresh_3d, flag_3d, clk_3d;
  bool interlaced, interlaced_3d;
  char val,val_3d;

  if (display == HWC_DISPLAY_PRIMARY)
  {
    property_get("persist.sys.resolution.main", resolution, "0x0p0");
    property_get("sys.3d_resolution.main", resolution_3d, "0x0p0-0:0");
  }
  else
  {
    property_get("persist.sys.resolution.aux", resolution, "0x0p0");
    property_get("sys.3d_resolution.aux", resolution_3d, "0x0p0-0:0");
  }

 if(hd->is_3d && strcmp(resolution_3d,"0x0p0-0:0"))
 {
      ALOGD_IF(log_level(DBG_DEBUG), "Enter 3d resolution=%s",resolution_3d);
      sscanf(resolution_3d, "%dx%d%c%d-%d:%d", &width_3d, &height_3d, &val_3d,
            &vrefresh_3d, &flag_3d, &clk_3d);

      if (val_3d == 'i')
        interlaced_3d = true;
      else
        interlaced_3d = false;

      if (width_3d != 0 && height_3d != 0) {
        for (const DrmMode &conn_mode : c->modes()) {
          if (conn_mode.equal(width_3d, height_3d, vrefresh_3d,  flag_3d, clk_3d, interlaced_3d)) {
             ALOGD_IF(log_level(DBG_DEBUG), "Match 3D parameters: w=%d,h=%d,val=%c,vrefresh_3d=%d,flag=%d,clk=%d",
                   width_3d,height_3d,val_3d,vrefresh_3d,flag_3d,clk_3d);
            c->set_best_mode(conn_mode);
            return 0;
          }
        }
      }
 }
 else
 {
      sscanf(resolution, "%dx%d%c%d", &width, &height, &val, &vrefresh);
      if (val == 'i')
        interlaced = true;
      else
        interlaced = false;

      if (width != 0 && height != 0) {
        for (const DrmMode &conn_mode : c->modes()) {
          if (conn_mode.equal(width, height, vrefresh, interlaced)) {
            c->set_best_mode(conn_mode);
            return 0;
          }
        }
      }
 }

  for (const DrmMode &conn_mode : c->modes()) {
    if (conn_mode.type() & DRM_MODE_TYPE_PREFERRED) {
      c->set_best_mode(conn_mode);
      return 0;
    }
  }

  for (const DrmMode &conn_mode : c->modes()) {
     c->set_best_mode(conn_mode);
     return 0;
  }

  ALOGE("Error: Should not get here display=%d %s %d\n", display, __FUNCTION__, __LINE__);
  DrmMode mode;
  c->set_best_mode(mode);

  return -ENOENT;
}


// map of display:hwc_drm_display_t
typedef std::map<int, hwc_drm_display_t> DisplayMap;
class DrmHotplugHandler : public DrmEventHandler {
 public:
  void Init(DisplayMap* displays, DrmResources *drm, const struct hwc_procs *procs) {
    displays_ = displays;
    drm_ = drm;
    procs_ = procs;
  }

  void HandleEvent(uint64_t timestamp_us) {
    int ret;
    DrmConnector *extend = NULL;

    for (auto &conn : drm_->connectors()) {
      drmModeConnection old_state = conn->state();

      conn->UpdateModes();

      drmModeConnection cur_state = conn->state();

      if (cur_state == old_state)
        continue;
      ALOGI("%s event @%" PRIu64 " for connector %u\n",
            cur_state == DRM_MODE_CONNECTED ? "Plug" : "Unplug", timestamp_us,
            conn->id());

      if (cur_state == DRM_MODE_CONNECTED) {
        if (conn->display())
          extend = conn.get();
      }
    }

    /*
     * status changed?
     */
    drm_->DisplayChanged();

    DrmConnector *primary = drm_->GetConnectorFromType(HWC_DISPLAY_PRIMARY);
    if (!primary) {
      ALOGE("%s %d Failed to find primary display\n", __FUNCTION__, __LINE__);
      return;
    }
    DrmConnector *old_extend = drm_->GetConnectorFromType(HWC_DISPLAY_EXTERNAL);
    extend = extend ? extend : old_extend;
    if (!extend || extend->state() != DRM_MODE_CONNECTED) {
      extend = NULL;
      for (auto &conn : drm_->connectors()) {
        if (conn->id() == primary->id())
          continue;
        if (conn->state() == DRM_MODE_CONNECTED) {
          extend = conn.get();
          break;
        }
      }
    }
    drm_->SetExtendDisplay(extend);
    if (!extend) {
      procs_->hotplug(procs_, HWC_DISPLAY_EXTERNAL, 0);
      procs_->invalidate(procs_);
      return;
    }

    hwc_drm_display_t *hd = &(*displays_)[extend->display()];
    update_display_bestmode(hd, HWC_DISPLAY_EXTERNAL, extend);
    DrmMode mode = extend->best_mode();

    if (mode.h_display() > mode.v_display() && mode.v_display() >= 2160) {
      hd->framebuffer_width = mode.h_display() * (1080.0 / mode.v_display());
      hd->framebuffer_height = 1080;
    } else {
      hd->framebuffer_width = mode.h_display();
      hd->framebuffer_height = mode.v_display();
    }
    hd->rel_xres = mode.h_display();
    hd->rel_yres = mode.v_display();
    hd->v_total = mode.v_total();
    hd->active = false;
    procs_->hotplug(procs_, HWC_DISPLAY_EXTERNAL, 0);
    hd->active = true;
    procs_->hotplug(procs_, HWC_DISPLAY_EXTERNAL, 1);
    procs_->invalidate(procs_);
  }

 private:
  DrmResources *drm_ = NULL;
  const struct hwc_procs *procs_ = NULL;
  DisplayMap* displays_ = NULL;
};

struct hwc_context_t {
  // map of display:hwc_drm_display_t
  typedef std::map<int, hwc_drm_display_t> DisplayMap;

  ~hwc_context_t() {
    virtual_compositor_worker.Exit();
  }

  hwc_composer_device_1_t device;
  hwc_procs_t const *procs = NULL;

  DisplayMap displays;
  DrmResources drm;
  std::unique_ptr<Importer> importer;
  const gralloc_module_t *gralloc;
  DummySwSyncTimeline dummy_timeline;
  VirtualCompositorWorker virtual_compositor_worker;
  DrmHotplugHandler hotplug_handler;
  VSyncWorker primary_vsync_worker;
  VSyncWorker extend_vsync_worker;

  int fb_fd;
  int fb_blanked;
#if RK_INVALID_REFRESH
    bool                isGLESComp;
    bool                mOneWinOpt;
    threadPamaters      mRefresh;
#endif

#if RK_STEREO
    bool is_3d;
    //int fd_3d;
    //threadPamaters mControlStereo;
#endif

    std::vector<DrmCompositionDisplayPlane> comp_plane_group;
    std::vector<DrmHwcDisplayContents> layer_contents;
};

static native_handle_t *dup_buffer_handle(buffer_handle_t handle) {
  native_handle_t *new_handle =
      native_handle_create(handle->numFds, handle->numInts);
  if (new_handle == NULL)
    return NULL;

  const int *old_data = handle->data;
  int *new_data = new_handle->data;
  for (int i = 0; i < handle->numFds; i++) {
    *new_data = dup(*old_data);
    old_data++;
    new_data++;
  }
  memcpy(new_data, old_data, sizeof(int) * handle->numInts);

  return new_handle;
}

static void free_buffer_handle(native_handle_t *handle) {
  int ret = native_handle_close(handle);
  if (ret)
    ALOGE("Failed to close native handle %d", ret);
  ret = native_handle_delete(handle);
  if (ret)
    ALOGE("Failed to delete native handle %d", ret);
}

const hwc_drm_bo *DrmHwcBuffer::operator->() const {
  if (importer_ == NULL) {
    ALOGE("Access of non-existent BO");
    exit(1);
    return NULL;
  }
  return &bo_;
}

void DrmHwcBuffer::Clear() {
  if (importer_ != NULL) {
    importer_->ReleaseBuffer(&bo_);
    importer_ = NULL;
  }
}

int DrmHwcBuffer::ImportBuffer(buffer_handle_t handle, Importer *importer
#if RK_VIDEO_SKIP_LINE
, bool bSkipLine
#endif
) {
  hwc_drm_bo tmp_bo;

  int ret = importer->ImportBuffer(handle, &tmp_bo
#if RK_VIDEO_SKIP_LINE
  , bSkipLine
#endif
  );
  if (ret)
    return ret;

  if (importer_ != NULL) {
    importer_->ReleaseBuffer(&bo_);
  }

  importer_ = importer;

  bo_ = tmp_bo;

  return 0;
}

int DrmHwcNativeHandle::CopyBufferHandle(buffer_handle_t handle,
                                         const gralloc_module_t *gralloc) {
  native_handle_t *handle_copy = dup_buffer_handle(handle);
  if (handle_copy == NULL) {
    ALOGE("Failed to duplicate handle");
    return -ENOMEM;
  }

  int ret = gralloc->registerBuffer(gralloc, handle_copy);
  if (ret) {
    ALOGE("Failed to register buffer handle %d", ret);
    free_buffer_handle(handle_copy);
    return ret;
  }

  Clear();

  gralloc_ = gralloc;
  handle_ = handle_copy;

  return 0;
}

DrmHwcNativeHandle::~DrmHwcNativeHandle() {
  Clear();
}

void DrmHwcNativeHandle::Clear() {
  if (gralloc_ != NULL && handle_ != NULL) {
    gralloc_->unregisterBuffer(gralloc_, handle_);
    free_buffer_handle(handle_);
    gralloc_ = NULL;
    handle_ = NULL;
  }
}

static const char *DrmFormatToString(uint32_t drm_format) {
  switch (drm_format) {
    case DRM_FORMAT_BGR888:
      return "DRM_FORMAT_BGR888";
    case DRM_FORMAT_ARGB8888:
      return "DRM_FORMAT_ARGB8888";
    case DRM_FORMAT_XBGR8888:
      return "DRM_FORMAT_XBGR8888";
    case DRM_FORMAT_ABGR8888:
      return "DRM_FORMAT_ABGR8888";
    case DRM_FORMAT_BGR565:
      return "DRM_FORMAT_BGR565";
    case DRM_FORMAT_YVU420:
      return "DRM_FORMAT_YVU420";
    case DRM_FORMAT_NV12:
      return "DRM_FORMAT_NV12";
    default:
      return "<invalid>";
  }
}

static void DumpBuffer(const DrmHwcBuffer &buffer, std::ostringstream *out) {
  if (!buffer) {
    *out << "buffer=<invalid>";
    return;
  }

  *out << "buffer[w/h/format]=";
  *out << buffer->width << "/" << buffer->height << "/" << DrmFormatToString(buffer->format);
}

static const char *TransformToString(uint32_t transform) {
  switch (transform) {
    case DrmHwcTransform::kIdentity:
      return "IDENTITY";
    case DrmHwcTransform::kFlipH:
      return "FLIPH";
    case DrmHwcTransform::kFlipV:
      return "FLIPV";
    case DrmHwcTransform::kRotate90:
      return "ROTATE90";
    case DrmHwcTransform::kRotate180:
      return "ROTATE180";
    case DrmHwcTransform::kRotate270:
      return "ROTATE270";
    default:
      return "<invalid>";
  }
}

static const char *BlendingToString(DrmHwcBlending blending) {
  switch (blending) {
    case DrmHwcBlending::kNone:
      return "NONE";
    case DrmHwcBlending::kPreMult:
      return "PREMULT";
    case DrmHwcBlending::kCoverage:
      return "COVERAGE";
    default:
      return "<invalid>";
  }
}

void DrmHwcLayer::dump_drm_layer(int index, std::ostringstream *out) const {
    *out << "DrmHwcLayer[" << index << "] ";
    DumpBuffer(buffer,out);

    *out << " transform=" << TransformToString(transform)
         << " blending[a=" << (int)alpha
         << "]=" << BlendingToString(blending) << " source_crop";
    source_crop.Dump(out);
    *out << " display_frame";
    display_frame.Dump(out);

    *out << "\n";
}

int DrmHwcLayer::InitFromHwcLayer(struct hwc_context_t *ctx, int display, hwc_layer_1_t *sf_layer, Importer *importer,
                                  const gralloc_module_t *gralloc, bool bClone) {
    DrmConnector *c;
    DrmMode mode;
    unsigned int size;

    int ret = 0;

  UN_USED(importer);

  bClone_ = bClone;
  stereo = sf_layer->alreadyStereo;
  if(sf_layer->compositionType == HWC_FRAMEBUFFER_TARGET)
   bFbTarget_ = true;
  else
   bFbTarget_ = false;

  if(sf_layer->flags & HWC_SKIP_LAYER)
    bSkipLayer = true;
  else
    bSkipLayer = false;
#if RK_VIDEO_SKIP_LINE
  bSkipLine = false;
#endif
  bUse = true;
  sf_handle = sf_layer->handle;
  alpha = sf_layer->planeAlpha;
  frame_no = get_frame();
  source_crop = DrmHwcRect<float>(
      sf_layer->sourceCropf.left, sf_layer->sourceCropf.top,
      sf_layer->sourceCropf.right, sf_layer->sourceCropf.bottom);

  DrmConnector *conn = ctx->drm.GetConnectorFromType(display);
  if (!conn) {
    ALOGE("Failed to get connector for display %d line=%d", display,__LINE__);
    return -ENODEV;
  }

  hwc_drm_display_t *hd = &ctx->displays[conn->display()];

  if(bClone)
  {
      //int panle_height = hd->rel_yres + hd->v_total;
      //int y_offset =  (panle_height - panle_height * 3 / 147) / 2 + panle_height * 3 / 147;
      int y_offset = hd->v_total;
      display_frame = DrmHwcRect<int>(
          hd->w_scale * sf_layer->displayFrame.left, hd->h_scale * sf_layer->displayFrame.top + y_offset,
          hd->w_scale * sf_layer->displayFrame.right, hd->h_scale * sf_layer->displayFrame.bottom + y_offset);
  }
  else
  {
      if(stereo == FPS_3D)
      {
        int y_offset = hd->v_total;
        display_frame = DrmHwcRect<int>(
          hd->w_scale * sf_layer->displayFrame.left, hd->h_scale * sf_layer->displayFrame.top,
          hd->w_scale * sf_layer->displayFrame.right, hd->h_scale * sf_layer->displayFrame.bottom + y_offset);
      }
      else
      {
        display_frame = DrmHwcRect<int>(
          hd->w_scale * sf_layer->displayFrame.left, hd->h_scale * sf_layer->displayFrame.top,
          hd->w_scale * sf_layer->displayFrame.right, hd->h_scale * sf_layer->displayFrame.bottom);
      }
  }

    c = ctx->drm.GetConnectorFromType(HWC_DISPLAY_PRIMARY);
    if (!c) {
        ALOGE("Failed to get DrmConnector for display %d", 0);
        return -ENODEV;
    }
    mode = c->active_mode();


   if((sf_layer->transform == HWC_TRANSFORM_ROT_90)
       ||(sf_layer->transform == HWC_TRANSFORM_ROT_270)){
	    h_scale_mul = (float) (source_crop.bottom - source_crop.top)
                        / (display_frame.right - display_frame.left);
	    v_scale_mul = (float) (source_crop.right - source_crop.left)
	                    / (display_frame.bottom - display_frame.top);
    } else {
        h_scale_mul = (float) (source_crop.right - source_crop.left)
	                    / (display_frame.right - display_frame.left);
        v_scale_mul = (float) (source_crop.bottom - source_crop.top)
	                    / (display_frame.bottom - display_frame.top);
    }
#if RK_DRM_GRALLOC
    width = hwc_get_handle_attibute(ctx->gralloc,sf_layer->handle,ATT_WIDTH);
    height = hwc_get_handle_attibute(ctx->gralloc,sf_layer->handle,ATT_HEIGHT);
    stride = hwc_get_handle_attibute(ctx->gralloc,sf_layer->handle,ATT_STRIDE);
    format = hwc_get_handle_attibute(ctx->gralloc,sf_layer->handle,ATT_FORMAT);
#else
    width = hwc_get_handle_width(ctx->gralloc,sf_layer->handle);
    height = hwc_get_handle_height(ctx->gralloc,sf_layer->handle);
    stride = hwc_get_handle_stride(ctx->gralloc,sf_layer->handle);
    format = hwc_get_handle_format(ctx->gralloc,sf_layer->handle);
#endif
    if(format == HAL_PIXEL_FORMAT_YCrCb_NV12 || format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
        is_yuv = true;
    else
        is_yuv = false;

#if RK_VIDEO_SKIP_LINE
    if(format == HAL_PIXEL_FORMAT_YCrCb_NV12 || format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
    {
        if(width >= 3840)
        {
            if(h_scale_mul > 1.0 || v_scale_mul > 1.0)
            {
                bSkipLine = true;
            }
        }
    }
#endif

    is_scale = (h_scale_mul != 1.0) || (v_scale_mul != 1.0);
    is_match = false;
    is_take = false;
#if USE_AFBC_LAYER
    is_afbc = false;
#endif
#if RK_RGA
    is_rotate_by_rga = false;
#endif
    bMix = false;
    raw_sf_layer = sf_layer;
    bpp = android::bytesPerPixel(format);
    size = (source_crop.right - source_crop.left) * (source_crop.bottom - source_crop.top) * bpp;
    is_large = (mode.h_display()*mode.v_display()*4*3/4 > size)? true:false;
    name = sf_layer->LayerName;
    mlayer = sf_layer;

    ALOGV("\t layerName=%s,sourceCropf(%f,%f,%f,%f)",sf_layer->LayerName,
    source_crop.left,source_crop.top,source_crop.right,source_crop.bottom);
    ALOGV("h_scale_mul=%f,v_scale_mul=%f,is_scale=%d,is_large=%d",h_scale_mul,v_scale_mul,is_scale,is_large);

  transform = 0;

  // 270* and 180* cannot be combined with flips. More specifically, they
  // already contain both horizontal and vertical flips, so those fields are
  // redundant in this case. 90* rotation can be combined with either horizontal
  // flip or vertical flip, so treat it differently
  if (sf_layer->transform == HWC_TRANSFORM_ROT_270) {
    transform = DrmHwcTransform::kRotate270;
  } else if (sf_layer->transform == HWC_TRANSFORM_ROT_180) {
    transform = DrmHwcTransform::kRotate180;
  } else {
    if (sf_layer->transform & HWC_TRANSFORM_FLIP_H)
      transform |= DrmHwcTransform::kFlipH;
    if (sf_layer->transform & HWC_TRANSFORM_FLIP_V)
      transform |= DrmHwcTransform::kFlipV;
    if (sf_layer->transform & HWC_TRANSFORM_ROT_90)
      transform |= DrmHwcTransform::kRotate90;
  }

#if RK_RGA_TEST
  if((format==HAL_PIXEL_FORMAT_RGB_565) && strstr(sf_layer->LayerName,"SurfaceView"))
    transform |= DrmHwcTransform::kRotate90;

#endif

  switch (sf_layer->blending) {
    case HWC_BLENDING_NONE:
      blending = DrmHwcBlending::kNone;
      break;
    case HWC_BLENDING_PREMULT:
      blending = DrmHwcBlending::kPreMult;
      break;
    case HWC_BLENDING_COVERAGE:
      blending = DrmHwcBlending::kCoverage;
      break;
    default:
      ALOGE("Invalid blending in hwc_layer_1_t %d", sf_layer->blending);
      return -EINVAL;
  }

#if 0
  ret = buffer.ImportBuffer(sf_layer->handle, importer
#if RK_VIDEO_SKIP_LINE
  , bSkipLine
#endif
  );
  if (ret)
    return ret;
#endif

  ret = handle.CopyBufferHandle(sf_layer->handle, gralloc);
  if (ret)
    return ret;

#if 0
  gralloc_buffer_usage= drm_handle->usage;
#else
  ret = gralloc->perform(gralloc, GRALLOC_MODULE_PERFORM_GET_USAGE,
                         handle.get(), &gralloc_buffer_usage);
  if (ret) {
    ALOGE("Failed to get usage for buffer %p (%d)", handle.get(), ret);
    return ret;
  }
#endif

#if USE_AFBC_LAYER
    ret = gralloc->perform(gralloc, GRALLOC_MODULE_PERFORM_GET_INTERNAL_FORMAT,
                         handle.get(), &internal_format);
    if (ret) {
        ALOGE("Failed to get internal_format for buffer %p (%d)", handle.get(), ret);
        return ret;
    }

    if(isAfbcInternalFormat(internal_format))
        is_afbc = true;
#endif

  return 0;
}

int DrmHwcLayer::ImportBuffer(hwc_layer_1_t *sf_layer, Importer *importer)
{
  int ret = buffer.ImportBuffer(sf_layer->handle, importer
#if RK_VIDEO_SKIP_LINE
  , bSkipLine
#endif
  );

  return ret;
}

static void hwc_dump(struct hwc_composer_device_1 *dev, char *buff,
                     int buff_len) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  std::ostringstream out;

  ctx->drm.compositor()->Dump(&out);
  std::string out_str = out.str();
  strncpy(buff, out_str.c_str(),
          std::min((size_t)buff_len, out_str.length() + 1));
  buff[buff_len - 1] = '\0';
}

static bool hwc_skip_layer(const std::pair<int, int> &indices, int i) {
  return indices.first >= 0 && i >= indices.first && i <= indices.second;
}

static bool is_use_gles_comp(struct hwc_context_t *ctx, hwc_display_contents_1_t *display_content, int display_id)
{
    int num_layers = display_content->numHwLayers;

    //force go into GPU
    /*
        <=0: DISPLAY_PRIMARY & DISPLAY_EXTERNAL both go into GPU.
        =1: DISPLAY_PRIMARY go into overlay,DISPLAY_EXTERNAL go into GPU.
        =2: DISPLAY_EXTERNAL go into overlay,DISPLAY_PRIMARY go into GPU.
        others: DISPLAY_PRIMARY & DISPLAY_EXTERNAL both go into overlay.
    */
    int iMode = hwc_get_int_property("sys.hwc.compose_policy","0");
    if( iMode <= 0 || (iMode == 1 && display_id == 1) || (iMode == 2 && display_id == 0) )
        return true;

    iMode = hwc_get_int_property("sys.hwc","1");
    if( iMode <= 0 )
        return true;

    if(num_layers == 1)
    {
        ALOGD_IF(log_level(DBG_DEBUG),"No layer,go to GPU GLES at line=%d", __LINE__);
        return true;
    }

#if RK_INVALID_REFRESH
    if(ctx->mOneWinOpt)
    {
        ALOGD_IF(log_level(DBG_DEBUG),"Enter static screen opt,go to GPU GLES at line=%d", __LINE__);
        return true;
    }
#endif

#if RK_STEREO
    if(ctx->is_3d)
    {
        ALOGD_IF(log_level(DBG_DEBUG),"Is 3d mode,go to GPU GLES at line=%d", __LINE__);
        return true;
    }
#endif

    //If the transform nv12 layers is bigger than one,then go into GPU GLES.
    //If the transform normal layers is bigger than zero,then go into GPU GLES.
    int transform_nv12 = 0;
    int transform_normal = 0;
    int ret = 0;
    int format = 0;
    int usage = 0;
#if USE_AFBC_LAYER
    uint64_t internal_format = 0;
    int iFbdcCnt = 0;
#endif

    for (int j = 0; j < num_layers-1; j++) {
        hwc_layer_1_t *layer = &display_content->hwLayers[j];

        if (layer->flags & HWC_SKIP_LAYER)
        {
            ALOGD_IF(log_level(DBG_DEBUG),"layer is skipped,go to GPU GLES at line=%d", __LINE__);
            return true;
        }

        if(
#if RK_RGA
            !ctx->drm.isSupportRkRga() && layer->transform
#else
            layer->transform
#endif
          )
        {
            ALOGD_IF(log_level(DBG_DEBUG),"layer's transform=0x%x,go to GPU GLES at line=%d", layer->transform, __LINE__);
            return true;
        }

        if( (layer->blending == HWC_BLENDING_PREMULT)&& layer->planeAlpha!=0xFF )
        {
            ALOGD_IF(log_level(DBG_DEBUG),"layer's blending planeAlpha=0x%x,go to GPU GLES at line=%d", layer->planeAlpha, __LINE__);
            return true;
        }

        if(layer->handle)
        {
            //DumpLayer(layer->LayerName,layer->handle);
#if RK_DRM_GRALLOC
            format = hwc_get_handle_attibute(ctx->gralloc,layer->handle,ATT_FORMAT);
#else
            format = hwc_get_handle_format(ctx->gralloc,layer->handle);
#endif
            if(!vop_support_format(format))
            {
                ALOGD_IF(log_level(DBG_DEBUG),"layer's format=0x%x is not support,go to GPU GLES at line=%d", format, __LINE__);
                return true;
            }

            ret = ctx->gralloc->perform(ctx->gralloc, GRALLOC_MODULE_PERFORM_GET_USAGE,
                                   layer->handle, &usage);
            if (ret) {
              ALOGE("Failed to get usage for buffer %p (%d)", layer->handle, ret);
              return ret;
            }

            if(format == HAL_PIXEL_FORMAT_YCrCb_NV12_10 && (usage & HDRUSAGE))
            {
                ALOGD_IF(log_level(DBG_DEBUG), "layer is hdr video usage=0x%x,go to GPU GLES at line=%d", usage, __LINE__);
                return true;
            }

#if 1
            if(!vop_support_scale(layer))
            {
                ALOGD_IF(log_level(DBG_DEBUG),"layer's scale is not support,go to GPU GLES at line=%d", __LINE__);
                return true;
            }
#endif
            if(layer->transform)
            {
                if(format == HAL_PIXEL_FORMAT_YCrCb_NV12 || format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
                    transform_nv12++;
                else
                    transform_normal++;
            }

#if USE_AFBC_LAYER
            ret = ctx->gralloc->perform(ctx->gralloc, GRALLOC_MODULE_PERFORM_GET_INTERNAL_FORMAT,
                                 layer->handle, &internal_format);
            if (ret) {
                ALOGE("Failed to get internal_format for buffer %p (%d)", layer->handle, ret);
                return false;
            }

            if(internal_format & GRALLOC_ARM_INTFMT_AFBC)
                iFbdcCnt++;
#endif
        }
    }
    if(transform_nv12 > 1 || transform_normal > 0)
    {
        return true;
    }

#if USE_AFBC_LAYER
    if(iFbdcCnt > 1)
    {
        ALOGD_IF(log_level(DBG_DEBUG),"iFbdcCnt=%d,go to GPU GLES",iFbdcCnt);
        return true;
    }
#endif

    return false;
}

static int hwc_prepare(hwc_composer_device_1_t *dev, size_t num_displays,
                       hwc_display_contents_1_t **display_contents) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;

    init_log_level();
    hwc_dump_fps();
    ALOGD_IF(log_level(DBG_VERBOSE),"----------------------------frame=%d start ----------------------------",get_frame());
    ctx->layer_contents.clear();
    ctx->layer_contents.reserve(num_displays);
    ctx->comp_plane_group.clear();
  for (int i = 0; i < (int)num_displays; ++i) {
    bool use_framebuffer_target = false;
    drmModeConnection state;

    if (!display_contents[i])
      continue;

    int num_layers = display_contents[i]->numHwLayers;
    for (int j = 0; j < num_layers; j++) {
      hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];
      dump_layer(ctx->gralloc, false, layer, j);
    }

    ctx->layer_contents.emplace_back();
    DrmHwcDisplayContents &layer_content = ctx->layer_contents.back();
    ctx->comp_plane_group.emplace_back();
    DrmCompositionDisplayPlane &comp_plane = ctx->comp_plane_group.back();
    comp_plane.display = i;

    DrmConnector *connector = ctx->drm.GetConnectorFromType(i);
    if (!connector) {
      ALOGE("Failed to get connector for display %d line=%d", i,__LINE__);
      continue;
    }

    DrmCrtc *crtc = ctx->drm.GetCrtcFromConnector(connector);
    if (connector->state() != DRM_MODE_CONNECTED || !crtc) {
      hwc_list_nodraw(display_contents[i]);
      continue;
    }

    hwc_drm_display_t *hd = &ctx->displays[connector->display()];
    update_display_bestmode(hd, i, connector);
    DrmMode mode = connector->best_mode();
    connector->set_current_mode(mode);
    hd->rel_xres = mode.h_display();
    hd->rel_yres = mode.v_display();
    hd->v_total = mode.v_total();
    hd->w_scale = (float)mode.h_display() / hd->framebuffer_width;
    hd->h_scale = (float)mode.v_display() / hd->framebuffer_height;
    //get plane size for display
    std::vector<PlaneGroup *>& plane_groups = ctx->drm.GetPlaneGroups();
    hd->iPlaneSize = 0;
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
        iter != plane_groups.end(); ++iter)
    {
        if(GetCrtcSupported(*crtc, (*iter)->possible_crtcs))
            hd->iPlaneSize++;
    }

#if SKIP_BOOT
    if(g_boot_cnt < BOOT_COUNT)
    {
        hwc_list_nodraw(display_contents[i]);
        ALOGD_IF(log_level(DBG_DEBUG),"prepare skip %d",g_boot_cnt);
        return 0;
    }
#endif

    for (int j = 0; j < num_layers-1; j++) {
        hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];

        if(layer->handle)
        {
            if(layer->compositionType == HWC_NODRAW)
                layer->compositionType = HWC_FRAMEBUFFER;
        }
    }

    // Since we can't composite HWC_SKIP_LAYERs by ourselves, we'll let SF
    // handle all layers in between the first and last skip layers. So find the
    // outer indices and mark everything in between as HWC_FRAMEBUFFER
    std::pair<int, int> skip_layer_indices(-1, -1);

    int format = 0;
    hd->is10bitVideo = false;
    hd->isVideo = false;
    for (int j = 0; j < num_layers-1; j++) {
        hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];

        if(layer->handle)
        {
#if RK_DRM_GRALLOC
            format = hwc_get_handle_attibute(ctx->gralloc,layer->handle, ATT_FORMAT);
#else
            format = hwc_get_handle_format(ctx->gralloc,layer->handle);
#endif
           if(format == HAL_PIXEL_FORMAT_YCrCb_NV12)
           {
                hd->isVideo = true;
           }
           if(format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
           {
                hd->is10bitVideo = true;
                hd->isVideo = true;
                break;
           }
        }
    }

#if 1
    hd->stereo_mode = NON_3D;
    hd->is_3d = detect_3d_mode(hd, display_contents[i], i);
#endif

    int iLastFps = num_layers-1;
    if(hd->stereo_mode == FPS_3D)
    {
        for(int j=num_layers-1; j>=0; j--) {
            hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];
            if(layer->alreadyStereo == FPS_3D) {
                iLastFps = j;
                break;
            }
        }

        for (int j = 0; j < iLastFps; j++)
        {
            display_contents[i]->hwLayers[j].compositionType = HWC_NODRAW;
        }
    }

    if(!use_framebuffer_target)
        use_framebuffer_target = is_use_gles_comp(ctx, display_contents[i], i);

#if RK_VIDEO_UI_OPT
    video_ui_optimize(ctx->gralloc, display_contents[i], &ctx->displays[i]);
#endif

    int ret = -1;
    bool bHasFPS_3D_UI = false;
    int index = 0;
    for (int j = 0; j < num_layers; j++) {
      hwc_layer_1_t *sf_layer = &display_contents[i]->hwLayers[j];
      if(sf_layer->handle == NULL)
        continue;
      if(sf_layer->compositionType == HWC_NODRAW)
        continue;

        if(hd->stereo_mode == FPS_3D && iLastFps < num_layers-1)
        {
            if(j>iLastFps  && sf_layer->alreadyStereo != FPS_3D && sf_layer->displayStereo)
            {
                bHasFPS_3D_UI = true;
            }
        }

      layer_content.layers.emplace_back();
      DrmHwcLayer &layer = layer_content.layers.back();
      ret = layer.InitFromHwcLayer(ctx, i, sf_layer, ctx->importer.get(), ctx->gralloc, false);
      if (ret) {
        ALOGE("Failed to init composition from layer %d", ret);
        return ret;
      }
      layer.index = j;
      index = j;

      std::ostringstream out;
      layer.dump_drm_layer(j,&out);
      ALOGD_IF(log_level(DBG_DEBUG),"%s",out.str().c_str());
    }

    if(bHasFPS_3D_UI)
    {
      hwc_layer_1_t *sf_layer = &display_contents[i]->hwLayers[num_layers-1];
      if(sf_layer->handle == NULL)
        continue;

      layer_content.layers.emplace_back();
      DrmHwcLayer &layer = layer_content.layers.back();
      ret = layer.InitFromHwcLayer(ctx, i, sf_layer, ctx->importer.get(), ctx->gralloc, true);
      if (ret) {
        ALOGE("Failed to init composition from layer %d", ret);
        return ret;
      }
      index++;
      layer.index = index;

      std::ostringstream out;
      layer.dump_drm_layer(index,&out);
      ALOGD_IF(log_level(DBG_DEBUG),"clone layer: %s",out.str().c_str());
    }

    if(!use_framebuffer_target)
    {
        bool bAllMatch = false;
        int iUsePlane = 0;

        hd->mixMode = HWC_DEFAULT;
        if(crtc && layer_content.layers.size()>0)
        {
            bAllMatch = mix_policy(&ctx->drm, crtc, &ctx->displays[i],layer_content.layers,
                                    hd->iPlaneSize, comp_plane.composition_planes);
        }
        if(!bAllMatch)
        {
            ALOGD_IF(log_level(DBG_DEBUG),"mix_policy failed,go to GPU GLES at line=%d", __LINE__);
            use_framebuffer_target = true;
        }
    }

    for (int j = 0; j < num_layers; ++j) {
      hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];


      if (!use_framebuffer_target && layer->compositionType != HWC_MIX) {
        // If the layer is off the screen, don't earmark it for an overlay.
        // We'll leave it as-is, which effectively just drops it from the frame
        const hwc_rect_t *frame = &layer->displayFrame;
        if ((frame->right - frame->left) <= 0 ||
            (frame->bottom - frame->top) <= 0 ||
            frame->right <= 0 || frame->bottom <= 0 ||
            frame->left >= (int)hd->framebuffer_width ||
            frame->top >= (int)hd->framebuffer_height)
         {
            continue;
         }

        if (layer->compositionType == HWC_FRAMEBUFFER)
          layer->compositionType = HWC_OVERLAY;
      } else {
        switch (layer->compositionType) {
          case HWC_MIX:
          case HWC_OVERLAY:
          case HWC_BACKGROUND:
          case HWC_SIDEBAND:
          case HWC_CURSOR_OVERLAY:
            layer->compositionType = HWC_FRAMEBUFFER;
            break;
        }
      }
    }

    if(use_framebuffer_target)
        ctx->isGLESComp = true;
    else
        ctx->isGLESComp = false;

    if(ctx->isGLESComp)
    {
        //remove all layers except fb layer
        for (auto k = layer_content.layers.begin(); k != layer_content.layers.end();)
        {
            //remove gles layers
            if((*k).mlayer->compositionType != HWC_FRAMEBUFFER_TARGET)
                k = layer_content.layers.erase(k);
            else
                k++;
        }

        //match plane for gles composer.
        bool bAllMatch = match_process(&ctx->drm, crtc, layer_content.layers,
                                        hd->iPlaneSize, comp_plane.composition_planes);
        if(!bAllMatch)
            ALOGE("Fetal error when match plane for fb layer");
    }

    for (int j = 0; j < num_layers; ++j) {
        hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];

        if(layer->compositionType==HWC_FRAMEBUFFER)
            ALOGD_IF(log_level(DBG_DEBUG),"%s: HWC_FRAMEBUFFER",layer->LayerName);
        else if(layer->compositionType==HWC_OVERLAY)
            ALOGD_IF(log_level(DBG_DEBUG),"%s: HWC_OVERLAY",layer->LayerName);
        else
            ALOGD_IF(log_level(DBG_DEBUG),"%s: HWC_OTHER",layer->LayerName);
    }
  }

#if RK_INVALID_REFRESH
  if(ctx->mOneWinOpt)
    ctx->mOneWinOpt = false;
#endif

  return 0;
}

static void hwc_add_layer_to_retire_fence(
    hwc_layer_1_t *layer, hwc_display_contents_1_t *display_contents) {
  if (layer->releaseFenceFd < 0)
    return;

  if (display_contents->retireFenceFd >= 0) {
    int old_retire_fence = display_contents->retireFenceFd;
    display_contents->retireFenceFd =
        sync_merge("dc_retire", old_retire_fence, layer->releaseFenceFd);
    close(old_retire_fence);
  } else {
    display_contents->retireFenceFd = dup(layer->releaseFenceFd);
  }
}

static int hwc_set(hwc_composer_device_1_t *dev, size_t num_displays,
                   hwc_display_contents_1_t **sf_display_contents) {
  ATRACE_CALL();
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  int ret = 0;

 ALOGD_IF(log_level(DBG_VERBOSE),"----------------------------frame=%d end----------------------------",get_frame());
 inc_frame();

  std::vector<CheckedOutputFd> checked_output_fences;
  std::vector<DrmHwcDisplayContents> displays_contents;
  std::vector<DrmCompositionDisplayLayersMap> layers_map;
  std::vector<std::vector<size_t>> layers_indices;
  std::vector<uint32_t> fail_displays;

  // layers_map.reserve(num_displays);
  layers_indices.reserve(num_displays);

  // Phase one does nothing that would cause errors. Only take ownership of FDs.
  for (size_t i = 0; i < num_displays; ++i) {
    hwc_display_contents_1_t *dc = sf_display_contents[i];
    DrmHwcDisplayContents &display_contents = ctx->layer_contents[i];
    displays_contents.emplace_back();
    DrmHwcDisplayContents &display_contents_tmp = displays_contents.back();
    layers_indices.emplace_back();

    if (!sf_display_contents[i])
      continue;
#if SKIP_BOOT
    if(g_boot_cnt < BOOT_COUNT) {
        hwc_sync_release(sf_display_contents[i]);
        if(0 == i)
            g_boot_cnt++;
        ALOGD_IF(log_level(DBG_DEBUG),"set skip %d",g_boot_cnt);
        return 0;
    }
#endif

    if (i == HWC_DISPLAY_VIRTUAL) {
      ctx->virtual_compositor_worker.QueueComposite(dc);
      continue;
    }

    DrmConnector *c = ctx->drm.GetConnectorFromType(i);
    if (!c || c->state() != DRM_MODE_CONNECTED) {
      hwc_sync_release(sf_display_contents[i]);
      continue;
    }
    hwc_drm_display_t *hd = &ctx->displays[c->display()];

    std::ostringstream display_index_formatter;
    display_index_formatter << "retire fence for display " << i;
    std::string display_fence_description(display_index_formatter.str());
    checked_output_fences.emplace_back(&dc->retireFenceFd,
                                       display_fence_description.c_str(),
                                       ctx->dummy_timeline);
    display_contents.retire_fence = OutputFd(&dc->retireFenceFd);

    size_t num_dc_layers = dc->numHwLayers;
    int framebuffer_target_index = -1;
    for (size_t j = 0; j < num_dc_layers; ++j) {
      hwc_layer_1_t *sf_layer = &dc->hwLayers[j];
      if (sf_layer->compositionType == HWC_FRAMEBUFFER_TARGET) {
        framebuffer_target_index = j;
        break;
      }
    }

    for (size_t j = 0; j < num_dc_layers; ++j) {
      size_t k = 0;
      hwc_layer_1_t *sf_layer = &dc->hwLayers[j];

      // In prepare() we marked all layers FRAMEBUFFER between SKIP_LAYER's.
      // This means we should insert the FB_TARGET layer in the composition
      // stack at the location of the first skip layer, and ignore the rest.

    if (sf_layer->flags & HWC_SKIP_LAYER) {
        //rk: SurfaceFlinger will create acquireFenceFd for nodraw skip layer.
        //    Close it here to avoid anon_inode:sync_fence fd leak.
        if(sf_layer->compositionType == HWC_NODRAW)
        {
            if(sf_layer->acquireFenceFd >= 0)
            {
                close(sf_layer->acquireFenceFd);
                sf_layer->acquireFenceFd = -1;
            }
        }

        if (framebuffer_target_index < 0)
          continue;
        int idx = framebuffer_target_index;
        framebuffer_target_index = -1;
        hwc_layer_1_t *fbt_layer = &dc->hwLayers[idx];
        if (!fbt_layer->handle || (fbt_layer->flags & HWC_SKIP_LAYER)) {
          ALOGE("Invalid HWC_FRAMEBUFFER_TARGET with HWC_SKIP_LAYER present");
          continue;
        }
        continue;
      }

#if 0
        // rk: wait acquireFenceFd at hwc_set.
        if(sf_layer->acquireFenceFd > 0)
        {
            sync_wait(sf_layer->acquireFenceFd, -1);
            close(sf_layer->acquireFenceFd);
            sf_layer->acquireFenceFd = -1;
        }
#endif

      for (k = 0; k < display_contents.layers.size(); ++k)
      {
         DrmHwcLayer &layer = display_contents.layers[k];
         if(j == layer.index)
         {
            //  sf_layer = layer.raw_sf_layer;
              layer.acquire_fence.Set(sf_layer->acquireFenceFd);
              sf_layer->acquireFenceFd = -1;

              std::ostringstream layer_fence_formatter;
              layer_fence_formatter << "release fence for layer " << j << " of display "
                                    << i;
              std::string layer_fence_description(layer_fence_formatter.str());
              checked_output_fences.emplace_back(&sf_layer->releaseFenceFd,
                                                 layer_fence_description.c_str(),
                                                 ctx->dummy_timeline);
              layer.release_fence = OutputFd(&sf_layer->releaseFenceFd);
            break;
         }
      }

      if(k == display_contents.layers.size())
      {
          display_contents_tmp.layers.emplace_back();
          DrmHwcLayer &layer = display_contents_tmp.layers.back();

          layer.acquire_fence.Set(sf_layer->acquireFenceFd);
          sf_layer->acquireFenceFd = -1;

          std::ostringstream layer_fence_formatter;
          layer_fence_formatter << "release fence for layer " << j << " of display "
                                << i;
          std::string layer_fence_description(layer_fence_formatter.str());
          checked_output_fences.emplace_back(&sf_layer->releaseFenceFd,
                                             layer_fence_description.c_str(),
                                             ctx->dummy_timeline);
          layer.release_fence = OutputFd(&sf_layer->releaseFenceFd);
      }
    }


    if(display_contents.layers.size() == 0 && framebuffer_target_index >= 0)
    {
      hwc_layer_1_t *sf_layer = &dc->hwLayers[framebuffer_target_index];
      if (!sf_layer->handle || (sf_layer->flags & HWC_SKIP_LAYER)) {
        ALOGE(
            "Expected valid layer with HWC_FRAMEBUFFER_TARGET when all "
            "HWC_OVERLAY layers are skipped.");
        fail_displays.emplace_back(i);
        ret = -EINVAL;
      }
    }
  }

#if 0
  if (ret)
    return ret;
#endif
  for (size_t i = 0; i < num_displays; ++i) {
    hwc_display_contents_1_t *dc = sf_display_contents[i];
    DrmHwcDisplayContents &display_contents = ctx->layer_contents[i];
    bool bFindDisplay = false;
    if (!sf_display_contents[i] || i == HWC_DISPLAY_VIRTUAL)
      continue;

    for (auto &fail_display : fail_displays) {
        if( i == fail_display )
        {
            bFindDisplay = true;
            ALOGD_IF(log_level(DBG_VERBOSE),"%s:line=%d,Find fail display %zu",__FUNCTION__,__LINE__,i);
            break;
        }
    }
    if(bFindDisplay)
        continue;

    layers_map.emplace_back();
    DrmCompositionDisplayLayersMap &map = layers_map.back();
    map.display = i;
    map.geometry_changed =
        (dc->flags & HWC_GEOMETRY_CHANGED) == HWC_GEOMETRY_CHANGED;
    for (size_t j=0; j< display_contents.layers.size(); j++) {
      DrmHwcLayer &layer = display_contents.layers[j];
      if(!layer.bClone_)
        layer.ImportBuffer(layer.raw_sf_layer, ctx->importer.get());
      map.layers.emplace_back(std::move(layer));
    }
  }

  ctx->drm.UpdateDisplayRoute();
  ctx->drm.ClearDisplay();
  std::unique_ptr<DrmComposition> composition(
      ctx->drm.compositor()->CreateComposition(ctx->importer.get()));
  if (!composition) {
    ALOGE("Drm composition init failed");
    return -EINVAL;
  }

  ret = composition->SetLayers(layers_map.size(), layers_map.data());
  if (ret) {
    return -EINVAL;
  }

  for (size_t i = 0; i < num_displays; ++i) {
    if (!sf_display_contents[i])
        continue;

    DrmConnector *c = ctx->drm.GetConnectorFromType(i);
    if (!c || c->state() != DRM_MODE_CONNECTED) {
        continue;
    }
    hwc_drm_display_t *hd = &ctx->displays[c->display()];
    composition->SetMode3D(i, hd->stereo_mode);
  }

  for (size_t i = 0; i < ctx->comp_plane_group.size(); ++i) {
      if(ctx->comp_plane_group[i].composition_planes.size() > 0)
      {
          ret = composition->SetCompPlanes(ctx->comp_plane_group[i].display, ctx->comp_plane_group[i].composition_planes);
          if (ret) {
            return -EINVAL;
          }
      }
      else
      {
          if (sf_display_contents[i])
              hwc_sync_release(sf_display_contents[i]);
      }
  }

  ret = ctx->drm.compositor()->QueueComposition(std::move(composition));
  if (ret) {
    return -EINVAL;
  }

  for (size_t i = 0; i < num_displays; ++i) {
    hwc_display_contents_1_t *dc = sf_display_contents[i];
    bool bFindDisplay = false;
    if (!dc)
      continue;

    DrmConnector *c = ctx->drm.GetConnectorFromType(i);
    if (!c || c->state() != DRM_MODE_CONNECTED) {
      hwc_sync_release(sf_display_contents[i]);
      continue;
    }
    hwc_drm_display_t *hd = &ctx->displays[c->display()];

    for (auto &fail_display : fail_displays) {
        if( i == fail_display )
        {
            bFindDisplay = true;
            ALOGD_IF(log_level(DBG_DEBUG),"%s:line=%d,Find fail display %zu",__FUNCTION__,__LINE__,i);
            break;
        }
    }
    if(bFindDisplay)
        continue;

    size_t num_dc_layers = dc->numHwLayers;
    for (size_t j = 0; j < num_dc_layers; ++j) {
      hwc_layer_1_t *layer = &dc->hwLayers[j];
      if (layer->flags & HWC_SKIP_LAYER)
        continue;
      hwc_add_layer_to_retire_fence(layer, dc);
    }
  }

  composition.reset(NULL);

#if RK_INVALID_REFRESH
  hwc_static_screen_opt_set(ctx->isGLESComp);
#endif

  return ret;
}

static int hwc_event_control(struct hwc_composer_device_1 *dev, int display,
                             int event, int enabled) {
  if (event != HWC_EVENT_VSYNC || (enabled != 0 && enabled != 1))
    return -EINVAL;

  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  if (display == HWC_DISPLAY_PRIMARY)
    return ctx->primary_vsync_worker.VSyncControl(enabled);
  else if (display == HWC_DISPLAY_EXTERNAL)
    return ctx->extend_vsync_worker.VSyncControl(enabled);

  ALOGE("Can't support vsync control for display %d\n", display);
  return -EINVAL;
}

static int hwc_set_power_mode(struct hwc_composer_device_1 *dev, int display,
                              int mode) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;

  uint64_t dpmsValue = 0;
  switch (mode) {
    case HWC_POWER_MODE_OFF:
      dpmsValue = DRM_MODE_DPMS_OFF;
      break;

    /* We can't support dozing right now, so go full on */
    case HWC_POWER_MODE_DOZE:
    case HWC_POWER_MODE_DOZE_SUSPEND:
    case HWC_POWER_MODE_NORMAL:
      dpmsValue = DRM_MODE_DPMS_ON;
      break;
  };

    int fb_blank = 0;
    if(dpmsValue == DRM_MODE_DPMS_OFF)
        fb_blank = FB_BLANK_POWERDOWN;
    else if(dpmsValue == DRM_MODE_DPMS_ON)
        fb_blank = FB_BLANK_UNBLANK;
    else
        ALOGE("dpmsValue is invalid value= %" PRIu64 "",dpmsValue);
    int err = ioctl(ctx->fb_fd, FBIOBLANK, fb_blank);
    ALOGD_IF(log_level(DBG_DEBUG),"%s Notice fb_blank to fb=%d", __FUNCTION__, fb_blank);
    if (err < 0) {
        if (errno == EBUSY)
            ALOGD("fb_blank ioctl failed display=%d,fb_blank=%d,dpmsValue=%" PRIu64 "",
                    display,fb_blank,dpmsValue);
        else
            ALOGE("fb_blank ioctl failed(%s) display=%d,fb_blank=%d,dpmsValue=%" PRIu64 "",
                    strerror(errno),display,fb_blank,dpmsValue);
        return -errno;
    }
    else
    {
        ctx->fb_blanked = fb_blank;
    }

  DrmConnector *connector = ctx->drm.GetConnectorFromType(display);
  if (!connector) {
    ALOGE("Failed to get connector for display %d line=%d", display,__LINE__);
    return -ENODEV;
  }

  connector->force_disconnect(dpmsValue == DRM_MODE_DPMS_OFF);
  ctx->drm.DisplayChanged();
  ctx->drm.UpdateDisplayRoute();
  ctx->drm.ClearDisplay();

  return 0;
}

static int hwc_query(struct hwc_composer_device_1 * /* dev */, int what,
                     int *value) {
  switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
      *value = 0; /* TODO: We should do this */
      break;
    case HWC_VSYNC_PERIOD:
      ALOGW("Query for deprecated vsync value, returning 60Hz");
      *value = 1000 * 1000 * 1000 / 60;
      break;
    case HWC_DISPLAY_TYPES_SUPPORTED:
      *value = HWC_DISPLAY_PRIMARY_BIT | HWC_DISPLAY_EXTERNAL_BIT |
               HWC_DISPLAY_VIRTUAL_BIT;
      break;
  }
  return 0;
}

static void hwc_register_procs(struct hwc_composer_device_1 *dev,
                               hwc_procs_t const *procs) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;

  ctx->procs = procs;

  ctx->primary_vsync_worker.SetProcs(procs);
  ctx->extend_vsync_worker.SetProcs(procs);
  ctx->hotplug_handler.Init(&ctx->displays, &ctx->drm, procs);
  ctx->drm.event_listener()->RegisterHotplugHandler(&ctx->hotplug_handler);
}

static int hwc_get_display_configs(struct hwc_composer_device_1 *dev,
                                   int display, uint32_t *configs,
                                   size_t *num_configs) {
  if (!num_configs)
    return 0;

  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  DrmConnector *connector = ctx->drm.GetConnectorFromType(display);
  if (!connector) {
    ALOGE("Failed to get connector for display %d line=%d", display,__LINE__);
    return -ENODEV;
  }

  hwc_drm_display_t *hd = &ctx->displays[connector->display()];
  if (!hd->active)
    return -ENODEV;

  int ret = connector->UpdateModes();
  if (ret) {
    ALOGE("Failed to update display modes %d", ret);
    return ret;
  }

  if (connector->state() != DRM_MODE_CONNECTED && display == HWC_DISPLAY_EXTERNAL) {
    ALOGE("connector is not connected with display %d", display);
    return -ENODEV;
  }

  update_display_bestmode(hd, display, connector);
  DrmMode mode = connector->best_mode();
  connector->set_current_mode(mode);

  char framebuffer_size[PROPERTY_VALUE_MAX];
  uint32_t width, height, vrefresh;
  property_get("persist.sys.framebuffer.main", framebuffer_size, "0x0@60");
  sscanf(framebuffer_size, "%dx%d@%d", &width, &height, &vrefresh);
  if (width && height) {
    hd->framebuffer_width = width;
    hd->framebuffer_height = height;
    hd->vrefresh = vrefresh ? vrefresh : 60;
  } else if (mode.h_display() && mode.v_display() && mode.v_refresh()) {
    hd->framebuffer_width = mode.h_display();
    hd->framebuffer_height = mode.v_display();
    hd->vrefresh = mode.v_refresh();
    /*
     * Limit to 1080p if large than 2160p
     */
    if (hd->framebuffer_height >= 2160 && hd->framebuffer_width >= hd->framebuffer_height) {
      hd->framebuffer_width = hd->framebuffer_width * (1080.0 / hd->framebuffer_height);
      hd->framebuffer_height = 1080;
    }
  } else {
    hd->framebuffer_width = 1920;
    hd->framebuffer_height = 1080;
    hd->vrefresh = 60;
    ALOGE("Failed to find available display mode for display %d\n", display);
  }

  hd->rel_xres = mode.h_display();
  hd->rel_yres = mode.v_display();
  hd->v_total = mode.v_total();

  *num_configs = 1;
  configs[0] = connector->display();

  return 0;
}

static int hwc_get_display_attributes(struct hwc_composer_device_1 *dev,
                                      int display, uint32_t config,
                                      const uint32_t *attributes,
                                      int32_t *values) {
  UN_USED(config);
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  DrmConnector *c = ctx->drm.GetConnectorFromType(display);
  if (!c) {
    ALOGE("Failed to get DrmConnector for display %d", display);
    return -ENODEV;
  }
  hwc_drm_display_t *hd = &ctx->displays[c->display()];
  if (!hd->active)
    return -ENODEV;
  uint32_t mm_width = c->mm_width();
  uint32_t mm_height = c->mm_height();
  int w = hd->framebuffer_width;
  int h = hd->framebuffer_height;
  int vrefresh = hd->vrefresh;

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; ++i) {
    switch (attributes[i]) {
      case HWC_DISPLAY_VSYNC_PERIOD:
        values[i] = 1000 * 1000 * 1000 / vrefresh;
        break;
      case HWC_DISPLAY_WIDTH:
        values[i] = w;
        break;
      case HWC_DISPLAY_HEIGHT:
        values[i] = h;
        break;
      case HWC_DISPLAY_DPI_X:
        /* Dots per 1000 inches */
        values[i] = mm_width ? (w * UM_PER_INCH) / mm_width : 0;
        break;
      case HWC_DISPLAY_DPI_Y:
        /* Dots per 1000 inches */
        values[i] =
            mm_height ? (h * UM_PER_INCH) / mm_height : 0;
        break;
    }
  }
  return 0;
}

static int hwc_get_active_config(struct hwc_composer_device_1 *dev,
                                 int display) {
  UN_USED(dev);
  UN_USED(display);
  return 0;
}

static int hwc_set_active_config(struct hwc_composer_device_1 *dev, int display,
                                 int index) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;

  UN_USED(index);
  DrmConnector *c = ctx->drm.GetConnectorFromType(display);
  if (!c) {
    ALOGE("Failed to get connector for display %d line=%d", display,__LINE__);
    return -ENODEV;
  }

  if (c->state() != DRM_MODE_CONNECTED) {
    /*
     * fake primary display if primary is not connected.
     */
    if (display == HWC_DISPLAY_PRIMARY)
      return 0;

    return -ENODEV;
  }

  hwc_drm_display_t *hd = &ctx->displays[c->display()];


  DrmMode mode = c->best_mode();
  if (!mode.id()) {
    ALOGE("Could not find active mode for display=%d", display);
    return -ENOENT;
  }
  hd->w_scale = (float)mode.h_display() / hd->framebuffer_width;
  hd->h_scale = (float)mode.v_display() / hd->framebuffer_height;

  c->set_current_mode(mode);
  ctx->drm.UpdateDisplayRoute();

  return 0;
}

static int hwc_device_close(struct hw_device_t *dev) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)dev;

#if RK_INVALID_REFRESH
    free_thread_pamaters(&ctx->mRefresh);
#endif
#if 0
    if(ctx->fd_3d >= 0)
    {
        close(ctx->fd_3d);
        ctx->fd_3d = -1;
    }
    free_thread_pamaters(&ctx->mControlStereo);
#endif
  delete ctx;
  return 0;
}

/*
 * TODO: This function sets the active config to the first one in the list. This
 * should be fixed such that it selects the preferred mode for the display, or
 * some other, saner, method of choosing the config.
 */
static int hwc_set_initial_config(struct hwc_context_t *ctx, int display) {
  uint32_t config;
  size_t num_configs = 1;
  int ret = hwc_get_display_configs(&ctx->device, display, &config,
                                    &num_configs);
  if (ret || !num_configs)
    return 0;

  ret = hwc_set_active_config(&ctx->device, display, 0);
  if (ret) {
    ALOGE("Failed to set active config d=%d ret=%d", display, ret);
    return ret;
  }

  return ret;
}

static int hwc_initialize_display(struct hwc_context_t *ctx, int display) {
    hwc_drm_display_t *hd = &ctx->displays[display];
    hd->ctx = ctx;
#if RK_VIDEO_UI_OPT
    hd->iUiFd = -1;
    hd->bHideUi = false;
#endif
    hd->framebuffer_width = 0;
    hd->framebuffer_height = 0;
    hd->rel_xres = 0;
    hd->rel_yres = 0;
    hd->v_total = 0;
    hd->w_scale = 1.0;
    hd->h_scale = 1.0;
    hd->active = true;

  return 0;
}

static int hwc_enumerate_displays(struct hwc_context_t *ctx) {
  int ret, num_connectors = 0;

  for (auto &conn : ctx->drm.connectors()) {
    ret = hwc_initialize_display(ctx, conn->display());
    if (ret) {
      ALOGE("Failed to initialize display %d", conn->display());
      return ret;
    }
    num_connectors++;
  }
#if 0
  ret = hwc_set_initial_config(ctx, HWC_DISPLAY_PRIMARY);
  if (ret) {
    ALOGE("Failed to set initial config for primary display ret=%d", ret);
    return ret;
  }

  ret = hwc_set_initial_config(ctx, HWC_DISPLAY_EXTERNAL);
  if (ret) {
    ALOGE("Failed to set initial config for extend display ret=%d", ret);
//    return ret;
  }
#endif

  ret = ctx->primary_vsync_worker.Init(&ctx->drm, HWC_DISPLAY_PRIMARY);
  if (ret) {
    ALOGE("Failed to create event worker for primary display %d\n", ret);
    return ret;
  }

  if (num_connectors > 1) {
    ret = ctx->extend_vsync_worker.Init(&ctx->drm, HWC_DISPLAY_EXTERNAL);
    if (ret) {
      ALOGE("Failed to create event worker for extend display %d\n", ret);
      return ret;
    }
  }

  ret = ctx->virtual_compositor_worker.Init();
  if (ret) {
    ALOGE("Failed to initialize virtual compositor worker");
    return ret;
  }
  return 0;
}

#if RK_INVALID_REFRESH
static void hwc_static_screen_opt_handler(int sig)
{
    hwc_context_t* ctx = g_ctx;
    if (sig == SIGALRM) {
        ctx->mOneWinOpt = true;
        pthread_mutex_lock(&ctx->mRefresh.mlk);
        ctx->mRefresh.count = 100;
        ALOGD_IF(log_level(DBG_VERBOSE),"hwc_static_screen_opt_handler:mRefresh.count=%d",ctx->mRefresh.count);
        pthread_mutex_unlock(&ctx->mRefresh.mlk);
        pthread_cond_signal(&ctx->mRefresh.cond);
    }

    return;
}

void  *invalidate_refresh(void *arg)
{
    hwc_context_t* ctx = (hwc_context_t*)arg;
    int count = 0;
    int nMaxCnt = 25;
    unsigned int nSleepTime = 200;

    pthread_cond_wait(&ctx->mRefresh.cond,&ctx->mRefresh.mtx);
    while(true) {
        for(count = 0; count < nMaxCnt; count++) {
            usleep(nSleepTime*1000);
            pthread_mutex_lock(&ctx->mRefresh.mlk);
            count = ctx->mRefresh.count;
            ctx->mRefresh.count ++;
            ALOGD_IF(log_level(DBG_VERBOSE),"invalidate_refresh mRefresh.count=%d",ctx->mRefresh.count);
            pthread_mutex_unlock(&ctx->mRefresh.mlk);
            ctx->procs->invalidate(ctx->procs);
        }
        pthread_cond_wait(&ctx->mRefresh.cond,&ctx->mRefresh.mtx);
        count = 0;
    }

    pthread_exit(NULL);
    return NULL;
}
#endif

#if 0
void* hwc_control_3dmode_thread(void *arg)
{
    hwc_context_t* ctx = (hwc_context_t*)arg;
    int ret = -1;
    int needStereo = 0;

    ALOGD("hwc_control_3dmode_thread creat");
    pthread_cond_wait(&ctx->mControlStereo.cond,&ctx->mControlStereo.mtx);
    while(true) {
        pthread_mutex_lock(&ctx->mControlStereo.mlk);
        needStereo = ctx->mControlStereo.count;
        pthread_mutex_unlock(&ctx->mControlStereo.mlk);
        ret = hwc_control_3dmode(ctx->fb_3d, 2, READ_3D_MODE);
        if(needStereo != ret) {
            hwc_control_3dmode(ctx, needStereo,WRITE_3D_MODE);
            ALOGI_IF(log_level(DBG_VERBOSE),"change stereo mode %d to %d",ret,needStereo);
        }
        ALOGD_IF(log_level(DBG_VERBOSE),"mControlStereo.count=%d",needStereo);
        pthread_cond_wait(&ctx->mControlStereo.cond,&ctx->mControlStereo.mtx);
    }
    ALOGD("hwc_control_3dmode_thread exit");
    pthread_exit(NULL);
    return NULL;
}
#endif

static int hwc_device_open(const struct hw_module_t *module, const char *name,
                           struct hw_device_t **dev) {
  if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
    ALOGE("Invalid module name- %s", name);
    return -EINVAL;
  }

  init_rk_debug();

  std::unique_ptr<hwc_context_t> ctx(new hwc_context_t());
  if (!ctx) {
    ALOGE("Failed to allocate hwc context");
    return -ENOMEM;
  }

  int ret = ctx->drm.Init();
  if (ret) {
    ALOGE("Can't initialize Drm object %d", ret);
    return ret;
  }

  ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                      (const hw_module_t **)&ctx->gralloc);
  if (ret) {
    ALOGE("Failed to open gralloc module %d", ret);
    return ret;
  }

  ctx->drm.setGralloc(ctx->gralloc);

  ret = ctx->dummy_timeline.Init();
  if (ret) {
    ALOGE("Failed to create dummy sw sync timeline %d", ret);
    return ret;
  }

  ctx->importer.reset(Importer::CreateInstance(&ctx->drm));
  if (!ctx->importer) {
    ALOGE("Failed to create importer instance");
    return ret;
  }

  ret = hwc_enumerate_displays(ctx.get());
  if (ret) {
    ALOGE("Failed to enumerate displays: %s", strerror(ret));
    return ret;
  }

  ctx->device.common.tag = HARDWARE_DEVICE_TAG;
  ctx->device.common.version = HWC_DEVICE_API_VERSION_1_4;
  ctx->device.common.module = const_cast<hw_module_t *>(module);
  ctx->device.common.close = hwc_device_close;

  ctx->device.dump = hwc_dump;
  ctx->device.prepare = hwc_prepare;
  ctx->device.set = hwc_set;
  ctx->device.eventControl = hwc_event_control;
  ctx->device.setPowerMode = hwc_set_power_mode;
  ctx->device.query = hwc_query;
  ctx->device.registerProcs = hwc_register_procs;
  ctx->device.getDisplayConfigs = hwc_get_display_configs;
  ctx->device.getDisplayAttributes = hwc_get_display_attributes;
  ctx->device.getActiveConfig = hwc_get_active_config;
  ctx->device.setActiveConfig = hwc_set_active_config;
  ctx->device.setCursorPositionAsync = NULL; /* TODO: Add cursor */


  g_ctx = ctx.get();

    ctx->fb_fd = open("/dev/graphics/fb0", O_RDWR, 0);
    if(ctx->fb_fd < 0)
    {
         ALOGE("Open fb0 fail in %s",__FUNCTION__);
         return -1;
    }

  hwc_init_version();


#if RK_INVALID_REFRESH
    ctx->mOneWinOpt = false;
    ctx->isGLESComp = false;
    init_thread_pamaters(&ctx->mRefresh);
    pthread_t invalidate_refresh_th;
    if (pthread_create(&invalidate_refresh_th, NULL, invalidate_refresh, ctx.get()))
    {
        ALOGE("Create invalidate_refresh_th thread error .");
    }

    signal(SIGALRM, hwc_static_screen_opt_handler);
#endif

#if 0
    init_thread_pamaters(&ctx->mControlStereo);
    ctx->fd_3d = open("/sys/class/display/HDMI/3dmode", O_RDWR, 0);
    if(ctx->fd_3d < 0){
        ALOGE("open /sys/class/display/HDMI/3dmode fail");
    }

    pthread_t thread_3d;
    if (pthread_create(&thread_3d, NULL, hwc_control_3dmode_thread, ctx.get()))
    {
        ALOGE("Create hwc_control_3dmode_thread thread error .");
    }

#endif

  *dev = &ctx->device.common;
  ctx.release();

  return 0;
}
}

static struct hw_module_methods_t hwc_module_methods = {
  .open = android::hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "DRM hwcomposer module",
    .author = "The Android Open Source Project",
    .methods = &hwc_module_methods,
    .dso = NULL,
    .reserved = {0},
  }
};
