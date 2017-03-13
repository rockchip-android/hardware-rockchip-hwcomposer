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
#include <cutils/log.h>
#if RK_DRM_HWC_DEBUG | RK_DRM_HWC
#include <drm_fourcc.h>
#include "gralloc_drm_handle.h"
#include <linux/fb.h>
#endif

#if RK_VR
#include "hwcutil.h"
#endif

#define UM_PER_INCH 25400

namespace android {

static int hwc_set_active_config(struct hwc_composer_device_1 *dev, int display,
                                 int index);
#if USE_AFBC_LAYER
bool isAfbcInternalFormat(uint64_t internal_format)
{
    return (internal_format & GRALLOC_ARM_INTFMT_AFBC);
}
#endif

hwc_context_t* g_ctx = NULL;

#if RK_DRM_HWC_DEBUG
unsigned int g_log_level;
unsigned int g_frame;

static int init_log_level()
{
    char value[PROPERTY_VALUE_MAX];
    int iValue;
    property_get("sys.hwc.log", value, "0");
    g_log_level = atoi(value);
    return 0;
}

/**
 * @brief Dump Layer data.
 *
 * @param layer_index   layer index
 * @param layer 		layer data
 * @return 				Errno no
 */
 #define DUMP_LAYER_CNT (10)
int DumpLayer(const char* layer_name,buffer_handle_t handle)
{
    char pro_value[PROPERTY_VALUE_MAX];

    property_get("sys.dump",pro_value,0);

    if(handle && !strcmp(pro_value,"true"))
    {
        //static int test = 0;
        static int DumpSurfaceCount = 0;
       // int32_t SrcStride = 2 ;
        FILE * pfile = NULL;
        char data_name[100] ;
        const gralloc_module_t *gralloc;
        void* cpu_addr;
        int i;

        int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc);
        if (ret) {
            ALOGE("Failed to open gralloc module");
            return ret;
        }

        gralloc_drm_handle_t *gr_handle = gralloc_drm_handle(handle);
        if (!gr_handle)
            return -EINVAL;

        struct gralloc_drm_bo_t *gralloc_bo = gr_handle->data;
        if (!gralloc_bo) {
            ALOGE("Could not get drm bo from handle");
            gralloc_drm_unlock_handle(handle);
            return -EINVAL;
        }

        system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
        DumpSurfaceCount++;
        sprintf(data_name,"/data/dump/dmlayer%d_%d_%d.bin", DumpSurfaceCount,
                gr_handle->pixel_stride,gr_handle->height);
        gralloc->lock(gralloc, handle, GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK, //gr_handle->usage,
                        0, 0, gr_handle->width, gr_handle->height, (void **)&cpu_addr);
        pfile = fopen(data_name,"wb");
        if(pfile)
        {
            fwrite((const void *)cpu_addr,(size_t)(gr_handle->size),1,pfile);
            fflush(pfile);
            fclose(pfile);
            ALOGD(" dump surface layer_name: %s,data_name %s,w:%d,h:%d,stride :%d,size=%d,cpu_addr=%p",
                layer_name,data_name,gr_handle->width,gr_handle->height,gr_handle->stride,gr_handle->size,cpu_addr);
        }
        gralloc->unlock(gralloc, handle);
        //only dump once time.
        if(DumpSurfaceCount == DUMP_LAYER_CNT)
            property_set("sys.dump","0");
        gralloc_drm_unlock_handle(handle);
    }

   // property_set("sys.dump","false");
    return 0;
}

static unsigned int HWC_Clockms(void)
{
	struct timespec t = { .tv_sec = 0, .tv_nsec = 0 };
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static void hwc_dump_fps(void)
{
	static unsigned int n_frames = 0;
	static unsigned int lastTime = 0;

	++n_frames;

	if (property_get_bool("sys.hwc.fps", 0))
	{
		unsigned int time = HWC_Clockms();
		unsigned int intv = time - lastTime;
		if (intv >= HWC_DEBUG_FPS_INTERVAL_MS)
		{
			unsigned int fps = n_frames * 1000 / intv;
			ALOGD_IF(log_level(DBG_DEBUG),"fps %u", fps);

			n_frames = 0;
			lastTime = time;
		}
	}
}

#endif

#if RK_DRM_HWC

#if SKIP_BOOT
static unsigned int g_boot_cnt = 0;
#endif

int hwc_init_version()
{
    char acVersion[50];
    char acCommit[50];
    memset(acVersion,0,sizeof(acVersion));
    if(sizeof(GHWC_VERSION) > 12) {
        strncpy(acVersion,GHWC_VERSION,12);
    } else {
        strcpy(acVersion,GHWC_VERSION);
    }

    strcat(acVersion,"-rk3399");
    /* RK_GRAPHICS_VER=commit-id:067e5d0: only keep string after '=' */
    sscanf(RK_GRAPHICS_VER, "%*[^=]=%s", acCommit);

    property_set("sys.ghwc.version", acVersion);
    property_set("sys.ghwc.commit", acCommit);
    ALOGD(RK_GRAPHICS_VER);
    return 0;
}
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

typedef struct hwc_drm_display {
  struct hwc_context_t *ctx;
#if RK_VIDEO_UI_OPT
  int iUiFd;
  bool bHideUi;
#endif
#if RK_10BIT_BYPASS
  bool is10bitVideo;
#endif
#if RK_MIX
  MixMode mixMode;
#endif
  int framebuffer_width;
  int framebuffer_height;
  int vrefresh;
  float w_scale;
  float h_scale;
  bool active;

} hwc_drm_display_t;

static int update_display_bestmode(int display, DrmConnector *c)
{
  char resolution[PROPERTY_VALUE_MAX];
  uint32_t width, height, vrefresh;
  bool interlaced;
  char val;

  if (display == HWC_DISPLAY_PRIMARY)
    property_get("persist.sys.resolution.main", resolution, "0x0p0");
  else
    property_get("persist.sys.resolution.aux", resolution, "0x0p0");

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
    update_display_bestmode(HWC_DISPLAY_EXTERNAL, extend);
    DrmMode mode = extend->best_mode();

    if (mode.h_display() > mode.v_display() && mode.v_display() >= 2160) {
      hd->framebuffer_width = mode.h_display() * (1080.0 / mode.v_display());
      hd->framebuffer_height = 1080;
    } else {
      hd->framebuffer_width = mode.h_display();
      hd->framebuffer_height = mode.v_display();
    }
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

#if RK_INVALID_REFRESH
typedef struct _threadPamaters
{
    int count;
    pthread_mutex_t mlk;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
}threadPamaters;
#endif

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
#if RK_DRM_HWC
  int fb_fd;
  int fb_blanked;
#endif
#if RK_INVALID_REFRESH
    bool                isGLESComp;
    bool                mOneWinOpt;
    threadPamaters      mRefresh;
#endif

#if RK_STEREO
    int is_3d;
    int fd_3d;
    threadPamaters mControlStereo;
#endif

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

#if RK_DRM_HWC_DEBUG

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
#endif

#if RK_DRM_HWC
int DrmHwcLayer::InitFromHwcLayer(struct hwc_context_t *ctx, int display, hwc_layer_1_t *sf_layer, Importer *importer,
                                  const gralloc_module_t *gralloc) {
    DrmConnector *c;
    DrmMode mode;
    unsigned int size;
#else
int DrmHwcLayer::InitFromHwcLayer(hwc_layer_1_t *sf_layer, Importer *importer,
                                    const gralloc_module_t *gralloc) {
#endif
  int ret = 0;
#if RK_VIDEO_SKIP_LINE
  bSkipLine = false;
#endif
  sf_handle = sf_layer->handle;
  alpha = sf_layer->planeAlpha;
  frame_no = g_frame;
  source_crop = DrmHwcRect<float>(
      sf_layer->sourceCropf.left, sf_layer->sourceCropf.top,
      sf_layer->sourceCropf.right, sf_layer->sourceCropf.bottom);

  DrmConnector *conn = ctx->drm.GetConnectorFromType(display);
  if (!conn) {
    ALOGE("Failed to get connector for display %d line=%d", display,__LINE__);
    return -ENODEV;
  }

  hwc_drm_display_t *hd = &ctx->displays[conn->display()];
  display_frame = DrmHwcRect<int>(
      hd->w_scale * sf_layer->displayFrame.left, hd->h_scale * sf_layer->displayFrame.top,
      hd->w_scale * sf_layer->displayFrame.right, hd->h_scale * sf_layer->displayFrame.bottom);

#if RK_DRM_HWC
    c = ctx->drm.GetConnectorFromType(HWC_DISPLAY_PRIMARY);
    if (!c) {
        ALOGE("Failed to get DrmConnector for display %d", 0);
        return -ENODEV;
    }
    mode = c->active_mode();
    format = hwc_get_handle_attibute(ctx,sf_layer->handle,ATT_FORMAT);
    if(format == HAL_PIXEL_FORMAT_YCrCb_NV12 || format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
        is_yuv = true;
    else
        is_yuv = false;

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
    width = hwc_get_handle_attibute(ctx,sf_layer->handle,ATT_WIDTH);
    height = hwc_get_handle_attibute(ctx,sf_layer->handle,ATT_HEIGHT);
    stride = hwc_get_handle_attibute(ctx,sf_layer->handle,ATT_STRIDE);

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
#if RK_MIX
    bMix = false;
    raw_sf_layer = sf_layer;
#endif
    bpp = android::bytesPerPixel(format);
    size = (source_crop.right - source_crop.left) * (source_crop.bottom - source_crop.top) * bpp;
    is_large = (mode.h_display()*mode.v_display()*4*3/4 > size)? true:false;
    name = sf_layer->LayerName;
    mlayer = sf_layer;

    ALOGV("\t layerName=%s,sourceCropf(%f,%f,%f,%f)",sf_layer->LayerName,
    source_crop.left,source_crop.top,source_crop.right,source_crop.bottom);
    ALOGV("h_scale_mul=%f,v_scale_mul=%f,is_scale=%d,is_large=%d",h_scale_mul,v_scale_mul,is_scale,is_large);

#endif

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

  ret = buffer.ImportBuffer(sf_layer->handle, importer
#if RK_VIDEO_SKIP_LINE
  , bSkipLine
#endif
  );
  if (ret)
    return ret;

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

static void hwc_dump(struct hwc_composer_device_1 *dev, char *buff,
                     int buff_len) {
#if 0
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  std::ostringstream out;

  ctx->drm.compositor()->Dump(&out);
  std::string out_str = out.str();
  strncpy(buff, out_str.c_str(),
          std::min((size_t)buff_len, out_str.length() + 1));
  buff[buff_len - 1] = '\0';
#endif
}

static bool hwc_skip_layer(const std::pair<int, int> &indices, int i) {
  return indices.first >= 0 && i >= indices.first && i <= indices.second;
}

#if RK_DRM_HWC_DEBUG
static void dump_layer(struct hwc_context_t *ctx, bool bDump, hwc_layer_1_t *layer, int index) {
    size_t i;
  std::ostringstream out;


    if(layer->flags & HWC_SKIP_LAYER)
    {
        ALOGD_IF(log_level(DBG_VERBOSE),"layer %p skipped", layer);
    }
    else
    {
        if(layer->handle)
        {
            out << "layer[" << index << "]=" << layer->LayerName
                << "\n\tlayer=" << layer
                << ",type=" << layer->compositionType
                << ",hints=" << layer->compositionType
                << ",flags=" << layer->flags
                << ",handle=" << layer->handle
                << ",format=0x" << std::hex << hwc_get_handle_attibute(ctx, layer->handle, ATT_FORMAT)
                << ",fd =" << std::dec << hwc_get_handle_primefd(ctx, layer->handle)
                << ",transform=0x" <<  std::hex << layer->transform
                << ",blend=0x" << layer->blending
                << ",sourceCropf{" << std::dec
                    << layer->sourceCropf.left << "," << layer->sourceCropf.top << ","
                    << layer->sourceCropf.right << "," << layer->sourceCropf.bottom
                << "},sourceCrop{"
                    << layer->sourceCrop.left << ","
                    << layer->sourceCrop.top << ","
                    << layer->sourceCrop.right << ","
                    << layer->sourceCrop.bottom
                << "},displayFrame{"
                    << layer->displayFrame.left << ","
                    << layer->displayFrame.top << ","
                    << layer->displayFrame.right << ","
                    << layer->displayFrame.bottom << "},";
        }
        else
        {
            out << "layer[" << index << "]=" << layer->LayerName
                << "\n\tlayer=" << layer
                << ",type=" << layer->compositionType
                << ",hints=" << layer->compositionType
                << ",flags=" << layer->flags
                << ",handle=" << layer->handle
                << ",transform=0x" <<  std::hex << layer->transform
                << ",blend=0x" << layer->blending
                << ",sourceCropf{" << std::dec
                    << layer->sourceCropf.left << "," << layer->sourceCropf.top << ","
                    << layer->sourceCropf.right << "," << layer->sourceCropf.bottom
                << "},sourceCrop{"
                    << layer->sourceCrop.left << ","
                    << layer->sourceCrop.top << ","
                    << layer->sourceCrop.right << ","
                    << layer->sourceCrop.bottom
                << "},displayFrame{"
                    << layer->displayFrame.left << ","
                    << layer->displayFrame.top << ","
                    << layer->displayFrame.right << ","
                    << layer->displayFrame.bottom << "},";
        }
        for (i = 0; i < layer->visibleRegionScreen.numRects; i++)
        {
            out << "rect[" << i << "]={"
                << layer->visibleRegionScreen.rects[i].left << ","
                << layer->visibleRegionScreen.rects[i].top << ","
                << layer->visibleRegionScreen.rects[i].right << ","
                << layer->visibleRegionScreen.rects[i].bottom << "},";
        }
        out << "\n";
        ALOGD_IF(log_level(DBG_VERBOSE) || bDump,"%s",out.str().c_str());
    }
}
#endif

#if RK_DRM_HWC
//return property value of pcProperty
static int hwc_get_int_property(const char* pcProperty,const char* default_value)
{
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;

    if(pcProperty == NULL || default_value == NULL)
    {
        ALOGE("hwc_get_int_property: invalid param");
        return -1;
    }

    property_get(pcProperty, value, default_value);
    new_value = atoi(value);

    return new_value;
}

static int hwc_get_string_property(const char* pcProperty,const char* default_value,char* retult)
{
    if(pcProperty == NULL || default_value == NULL || retult == NULL)
    {
        ALOGE("hwc_get_string_property: invalid param");
        return -1;
    }

    property_get(pcProperty, retult, default_value);

    return 0;
}

static bool vop_support_scale(hwc_layer_1_t *layer) {
    float hfactor;
    float vfactor;
    DrmHwcRect<float> source_crop;
    DrmHwcRect<int> display_frame;

    source_crop = DrmHwcRect<float>(
      layer->sourceCropf.left, layer->sourceCropf.top,
      layer->sourceCropf.right, layer->sourceCropf.bottom);
    display_frame = DrmHwcRect<int>(
      layer->displayFrame.left, layer->displayFrame.top,
      layer->displayFrame.right, layer->displayFrame.bottom);

    if((layer->transform == HWC_TRANSFORM_ROT_90)
       ||(layer->transform == HWC_TRANSFORM_ROT_270)){
        hfactor = (float) (source_crop.bottom - source_crop.top)
                    / (display_frame.right - display_frame.left);
        vfactor = (float) (source_crop.right - source_crop.left)
                    / (display_frame.bottom - display_frame.top);
    } else {
            hfactor = (float) (source_crop.right - source_crop.left)
                        / (display_frame.right - display_frame.left);
            vfactor = (float) (source_crop.bottom - source_crop.top)
                        / (display_frame.bottom - display_frame.top);
    }
    if(hfactor >= 8.0 || vfactor >= 8.0 || hfactor <= 0.125 || vfactor <= 0.125  ){
        ALOGD_IF(log_level(DBG_DEBUG), "scale [%f,%f]not support! at line=%d", hfactor, vfactor, __LINE__);
        return false;
    }

    return true;
}

static bool vop_support_format(uint32_t hal_format) {
  switch (hal_format) {
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_YCrCb_NV12:
    case HAL_PIXEL_FORMAT_YCrCb_NV12_10:
        return true;
    default:
      return false;
  }
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
#if RK_DRM_HWC_DEBUG
            DumpLayer(layer->LayerName,layer->handle);
#endif
            format = hwc_get_handle_attibute(ctx,layer->handle,ATT_FORMAT);
            if(!vop_support_format(format))
            {
                ALOGD_IF(log_level(DBG_DEBUG),"layer's format=0x%x is not support,go to GPU GLES at line=%d", format, __LINE__);
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

#if RK_MIX
static bool is_rec1_intersect_rec2(DrmHwcRect<int>* rec1,DrmHwcRect<int>* rec2)
{
    int iMaxLeft,iMaxTop,iMinRight,iMinBottom;
    ALOGD_IF(log_level(DBG_DEBUG),"is_not_intersect: rec1[%d,%d,%d,%d],rec2[%d,%d,%d,%d]",rec1->left,rec1->top,
        rec1->right,rec1->bottom,rec2->left,rec2->top,rec2->right,rec2->bottom);

    iMaxLeft = rec1->left > rec2->left ? rec1->left: rec2->left;
    iMaxTop = rec1->top > rec2->top ? rec1->top: rec2->top;
    iMinRight = rec1->right <= rec2->right ? rec1->right: rec2->right;
    iMinBottom = rec1->bottom <= rec2->bottom ? rec1->bottom: rec2->bottom;

    if(iMaxLeft > iMinRight || iMaxTop > iMinBottom)
        return false;
    else
        return true;

    return false;
}

static bool is_layer_combine(DrmHwcLayer * layer_one,DrmHwcLayer * layer_two)
{
    //Don't care format.
    if(/*layer_one->format != layer_two->format
        ||*/ layer_one->alpha!= layer_two->alpha
        || layer_one->is_scale || layer_two->is_scale
        || is_rec1_intersect_rec2(&layer_one->display_frame,&layer_two->display_frame))
    {
        ALOGD_IF(log_level(DBG_SILENT),"is_layer_combine layer one alpha=%d,is_scale=%d",layer_one->alpha,layer_one->is_scale);
        ALOGD_IF(log_level(DBG_SILENT),"is_layer_combine layer two alpha=%d,is_scale=%d",layer_two->alpha,layer_two->is_scale);
        return false;
    }

    return true;
}

static bool has_layer(std::vector<DrmHwcLayer*>& layer_vector,DrmHwcLayer &layer)
{
        for (std::vector<DrmHwcLayer*>::const_iterator iter = layer_vector.begin();
               iter != layer_vector.end(); ++iter) {
            if((*iter)->sf_handle==layer.sf_handle)
                return true;
          }

          return false;
}

#define MOST_WIN_ZONES                  4
typedef std::map<int, std::vector<DrmHwcLayer*>> LayerMap;
typedef LayerMap::iterator LayerMapIter;
int combine_layer(LayerMap& layer_map,std::vector<DrmHwcLayer>& layers)
{
    /*Group layer*/
    int zpos = 0;
    size_t i,j;
    uint32_t sort_cnt=0;
    bool is_combine = false;
    size_t min_size = (MOST_WIN_ZONES<layers.size())?MOST_WIN_ZONES:layers.size();

    layer_map.clear();

    for (i = 0; i < layers.size(); ) {
        sort_cnt=0;
        if(i == 0)
        {
            layer_map[zpos].push_back(&layers[0]);
        }

        if(i == min_size)
        {
            //We can use pre-comp to optimise.
            ALOGD_IF(log_level(DBG_DEBUG),"combine_layer fail: it remain layer i=%zu, min_size=%zu",i,min_size);
            return -1;
        }

        for(j = i+1; j < min_size; j++) {
            DrmHwcLayer &layer_one = layers[j];
            layer_one.index = j;
            is_combine = false;
            for(size_t k = 0; k <= sort_cnt; k++ ) {
                DrmHwcLayer &layer_two = layers[j-1-k];
                layer_two.index = j-1-k;
                //juage the layer is contained in layer_vector
                bool bHasLayerOne = has_layer(layer_map[zpos],layer_one);
                bool bHasLayerTwo = has_layer(layer_map[zpos],layer_two);

                //If it contain both of layers,then don't need to go down.
                if(bHasLayerOne && bHasLayerTwo)
                    continue;

                if(is_layer_combine(&layer_one,&layer_two)) {
                    //append layer into layer_vector of layer_map_.
                    if(!bHasLayerOne && !bHasLayerTwo)
                    {
                        layer_map[zpos].emplace_back(&layer_one);
                        layer_map[zpos].emplace_back(&layer_two);
                        is_combine = true;
                    }
                    else if(!bHasLayerTwo)
                    {
                        is_combine = true;
                        for(std::vector<DrmHwcLayer*>::const_iterator iter= layer_map[zpos].begin();
                            iter != layer_map[zpos].end();++iter)
                        {
                            if((*iter)->sf_handle==layer_one.sf_handle)
                                continue;

                            if(!is_layer_combine(*iter,&layer_two))
                            {
                                is_combine = false;
                                break;
                            }
                        }

                        if(is_combine)
                            layer_map[zpos].emplace_back(&layer_two);
                    }
                    else if(!bHasLayerOne)
                    {
                        is_combine = true;
                        for(std::vector<DrmHwcLayer*>::const_iterator iter= layer_map[zpos].begin();
                            iter != layer_map[zpos].end();++iter)
                        {
                            if((*iter)->sf_handle==layer_two.sf_handle)
                                continue;

                            if(!is_layer_combine(*iter,&layer_one))
                            {
                                is_combine = false;
                                break;
                            }
                        }

                        if(is_combine)
                            layer_map[zpos].emplace_back(&layer_one);
                    }
                }

                if(!is_combine)
                {
                    //if it cann't combine two layer,it need start a new group.
                    if(!bHasLayerOne)
                    {
                        zpos++;
                        layer_map[zpos].emplace_back(&layer_one);
                    }
                    is_combine = false;
                    break;
                }
             }
             sort_cnt++; //update sort layer count
             if(!is_combine)
             {
                break;
             }
        }

        if(is_combine)  //all remain layer or limit MOST_WIN_ZONES layer is combine well,it need start a new group.
            zpos++;
        if(sort_cnt)
            i+=sort_cnt;    //jump the sort compare layers.
        else
            i++;
    }

  //sort layer by xpos
  for (LayerMap::iterator iter = layer_map.begin();
       iter != layer_map.end(); ++iter) {
        if(iter->second.size() > 1) {
            for(uint32_t i=0;i < iter->second.size()-1;i++) {
                for(uint32_t j=i+1;j < iter->second.size();j++) {
                     if(iter->second[i]->display_frame.left > iter->second[j]->display_frame.left) {
                        ALOGD_IF(log_level(DBG_DEBUG),"swap %s and %s",iter->second[i]->name.c_str(),iter->second[j]->name.c_str());
                        std::swap(iter->second[i],iter->second[j]);
                     }
                 }
            }
        }
  }

#if RK_DRM_HWC_DEBUG
  for (LayerMap::iterator iter = layer_map.begin();
       iter != layer_map.end(); ++iter) {
        ALOGD_IF(log_level(DBG_DEBUG),"layer map id=%d,size=%zu",iter->first,iter->second.size());
        for(std::vector<DrmHwcLayer*>::const_iterator iter_layer = iter->second.begin();
            iter_layer != iter->second.end();++iter_layer)
        {
             ALOGD_IF(log_level(DBG_DEBUG),"\tlayer name=%s",(*iter_layer)->name.c_str());
        }
  }
#endif

    return 0;
}

static std::vector<DrmPlane *> rkGetNoYuvUsablePlanes(DrmCrtc *crtc) {
    DrmResources* drm = crtc->getDrmReoources();
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    std::vector<DrmPlane *> usable_planes;
    //loop plane groups.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
            if(!(*iter)->bUse)
                //only count the first plane in plane group.
                std::copy_if((*iter)->planes.begin(), (*iter)->planes.begin()+1,
                       std::back_inserter(usable_planes),
                       [=](DrmPlane *plane) {
                       return !plane->is_use() && plane->GetCrtcSupported(*crtc) && !plane->get_yuv(); }
                       );
  }
  return usable_planes;
}

static std::vector<DrmPlane *> rkGetNoScaleUsablePlanes(DrmCrtc *crtc) {
    DrmResources* drm = crtc->getDrmReoources();
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    std::vector<DrmPlane *> usable_planes;
    //loop plane groups.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
            if(!(*iter)->bUse)
                //only count the first plane in plane group.
                std::copy_if((*iter)->planes.begin(), (*iter)->planes.begin()+1,
                       std::back_inserter(usable_planes),
                       [=](DrmPlane *plane) {
                       return !plane->is_use() && plane->GetCrtcSupported(*crtc) && !plane->get_scale(); }
                       );
  }
  return usable_planes;
}

static std::vector<DrmPlane *> rkGetNoAlphaUsablePlanes(DrmCrtc *crtc) {
    DrmResources* drm = crtc->getDrmReoources();
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    std::vector<DrmPlane *> usable_planes;
    //loop plane groups.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
            if(!(*iter)->bUse)
                //only count the first plane in plane group.
                std::copy_if((*iter)->planes.begin(), (*iter)->planes.begin()+1,
                       std::back_inserter(usable_planes),
                       [=](DrmPlane *plane) {
                       return !plane->is_use() && plane->GetCrtcSupported(*crtc) && !plane->alpha_property().id(); }
                       );
  }
  return usable_planes;
}

//According to zpos and combine layer count,find the suitable plane.
bool MatchPlane(std::vector<DrmHwcLayer*>& layer_vector,
                               uint64_t* zpos,
                               DrmCrtc *crtc,
                               DrmResources *drm)
{
    uint32_t combine_layer_count = 0;
    uint32_t layer_size = layer_vector.size();
    bool b_yuv=false,b_scale=false,b_alpha=false;
    std::vector<PlaneGroup *> ::const_iterator iter;
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    uint64_t rotation = 0;
    uint64_t alpha = 0xFF;

    //loop plane groups.
    for (iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
       ALOGD_IF(log_level(DBG_DEBUG),"line=%d,last zpos=%" PRIu64 ",group(%" PRIu64 ") zpos=%d,group bUse=%d,crtc=0x%x,possible_crtcs=0x%x",
                    __LINE__, *zpos, (*iter)->share_id, (*iter)->zpos, (*iter)->bUse, (1<<crtc->pipe()), (*iter)->possible_crtcs);
        //find the match zpos plane group
        if(!(*iter)->bUse
#if !RK_ZPOS_SUPPORT
        && (*iter)->zpos >= *zpos
#endif
        )
        {
            ALOGD_IF(log_level(DBG_DEBUG),"line=%d,layer_size=%d,planes size=%zu",__LINE__,layer_size,(*iter)->planes.size());

            //find the match combine layer count with plane size.
            if(layer_size <= (*iter)->planes.size())
            {
                //loop layer
                for(std::vector<DrmHwcLayer*>::const_iterator iter_layer= layer_vector.begin();
                    iter_layer != layer_vector.end();++iter_layer)
                {
                    if((*iter_layer)->is_match)
                        continue;

                    //loop plane
                    for(std::vector<DrmPlane*> ::const_iterator iter_plane=(*iter)->planes.begin();
                        !(*iter)->planes.empty() && iter_plane != (*iter)->planes.end(); ++iter_plane)
                    {
                        ALOGD_IF(log_level(DBG_DEBUG),"line=%d,crtc=0x%x,plane(%d) is_use=%d,possible_crtc_mask=0x%x",__LINE__,(1<<crtc->pipe()),
                                (*iter_plane)->id(),(*iter_plane)->is_use(),(*iter_plane)->get_possible_crtc_mask());
                        if(!(*iter_plane)->is_use() && (*iter_plane)->GetCrtcSupported(*crtc))
                        {

                            bool bNeed = false;
                            b_yuv  = (*iter_plane)->get_yuv();
                            if((*iter_layer)->is_yuv)
                            {
                                if(!b_yuv)
                                {
                                    ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) cann't support yuv",(*iter_plane)->id());
                                    continue;
                                }
                                else
                                    bNeed = true;
                            }

                            b_scale = (*iter_plane)->get_scale();
                            if((*iter_layer)->is_scale)
                            {
                                if(!b_scale)
                                {
                                    ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) cann't support scale",(*iter_plane)->id());
                                    continue;
                                }
                                else
                                {
                                    if((*iter_layer)->h_scale_mul >= 8.0 || (*iter_layer)->v_scale_mul >= 8.0 ||
                                        (*iter_layer)->h_scale_mul <= 0.125 || (*iter_layer)->v_scale_mul <= 0.125)
                                    {
                                        ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) cann't support scale factor(%f,%f)",
                                                (*iter_plane)->id(), (*iter_layer)->h_scale_mul, (*iter_layer)->v_scale_mul);
                                        continue;
                                    }
                                    else
                                        bNeed = true;
                                }
                            }

                            if ((*iter_layer)->blending == DrmHwcBlending::kPreMult)
                                alpha = (*iter_layer)->alpha;

                            b_alpha = (*iter_plane)->alpha_property().id()?true:false;
                            if(alpha != 0xFF)
                            {
                                if(!b_alpha)
                                {
                                    ALOGV("layer name=%s,plane id=%d",(*iter_layer)->name.c_str(),(*iter_plane)->id());
                                    ALOGV("layer alpha=0x%x,alpha id=%d",(*iter_layer)->alpha,(*iter_plane)->alpha_property().id());
                                    continue;
                                }
                                else
                                    bNeed = true;
                            }

#if RK_ZPOS_SUPPORT
                            if(!bNeed)
                            {
                                if(!(*iter_layer)->is_yuv && b_yuv)
                                {
                                    std::vector<DrmPlane *> no_yuv_planes = rkGetNoYuvUsablePlanes(crtc);
                                    if(no_yuv_planes.size() > 0)
                                    {
                                        ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) don't need use yuv feature",(*iter_plane)->id());
                                        continue;
                                    }
                                }


                                if(!(*iter_layer)->is_scale && b_scale)
                                {
                                    std::vector<DrmPlane *> no_scale_planes = rkGetNoScaleUsablePlanes(crtc);
                                    if(no_scale_planes.size() > 0)
                                    {
                                        ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) don't need use scale feature",(*iter_plane)->id());
                                        continue;
                                    }
                                }



                                if(alpha == 0xFF && b_alpha)
                                {
                                    std::vector<DrmPlane *> no_alpha_planes = rkGetNoAlphaUsablePlanes(crtc);
                                    if(no_alpha_planes.size() > 0)
                                    {
                                        ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) don't need use alpha feature",(*iter_plane)->id());
                                        continue;
                                    }
                                }
                            }

#endif

#if RK_RGA
                            if(!drm->isSupportRkRga()
#if USE_AFBC_LAYER
                               || isAfbcInternalFormat((*iter_layer)->internal_format)
#endif
                               )
#endif
                            {
                                rotation = 0;
                                if ((*iter_layer)->transform & DrmHwcTransform::kFlipH)
                                    rotation |= 1 << DRM_REFLECT_X;
                                if ((*iter_layer)->transform & DrmHwcTransform::kFlipV)
                                    rotation |= 1 << DRM_REFLECT_Y;
                                if ((*iter_layer)->transform & DrmHwcTransform::kRotate90)
                                    rotation |= 1 << DRM_ROTATE_90;
                                else if ((*iter_layer)->transform & DrmHwcTransform::kRotate180)
                                    rotation |= 1 << DRM_ROTATE_180;
                                else if ((*iter_layer)->transform & DrmHwcTransform::kRotate270)
                                    rotation |= 1 << DRM_ROTATE_270;
                                if(rotation && !(rotation & (*iter_plane)->get_rotate()))
                                    continue;
                            }

                            ALOGD_IF(log_level(DBG_DEBUG),"MatchPlane: match layer=%s,plane=%d,(*iter_layer)->index=%zu",(*iter_layer)->name.c_str(),
                                (*iter_plane)->id(),(*iter_layer)->index);
                            //Find the match plane for layer,it will be commit.
                            //composition_plane->emplace_back(DrmCompositionPlane::Type::kLayer, (*iter_plane), crtc, (*iter_layer)->index);
                            (*iter_layer)->is_match = true;
                            (*iter_plane)->set_use(true);
#if RK_ZPOS_SUPPORT
                            //composition_plane->back().set_zpos((*iter_layer)->zpos);
#endif
                            combine_layer_count++;
                            break;

                        }
                    }
                }
                if(combine_layer_count == layer_size)
                {
                    ALOGD_IF(log_level(DBG_DEBUG),"line=%d all match",__LINE__);
                    //update zpos for the next time.
                     *zpos += 1;
                    (*iter)->bUse = true;
                    return true;
                }
            }
            /*else
            {
                //1. cut out combine_layer_count to (*iter)->planes.size().
                //2. combine_layer_count layer assign planes.
                //3. extern layers assign planes.
                return false;
            }*/
        }

    }

    return false;
}


bool MatchPlanes(
  std::map<int, std::vector<DrmHwcLayer*>> &layer_map,
  DrmCrtc *crtc,
  DrmResources *drm)
{
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    uint64_t last_zpos=0;
    bool bMatch = false;
    uint32_t planes_can_use=0;

    //set use flag to false.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
        (*iter)->bUse=false;
        for(std::vector<DrmPlane *> ::const_iterator iter_plane=(*iter)->planes.begin();
            iter_plane != (*iter)->planes.end(); ++iter_plane) {
            if((*iter_plane)->GetCrtcSupported(*crtc))  //only init the special crtc's plane
                (*iter_plane)->set_use(false);
        }
    }

    for (LayerMap::iterator iter = layer_map.begin();
        iter != layer_map.end(); ++iter) {
        bMatch = MatchPlane(iter->second, &last_zpos, crtc, drm);
        if(!bMatch)
        {
            ALOGD_IF(log_level(DBG_DEBUG),"hwc_prepare: Cann't find the match plane for layer group %d",iter->first);
            return false;
        }
    }

    return true;
}

static float getPixelWidthByAndroidFormat(int format)
{
       float pixelWidth = 0.0;
       switch (format) {
               case HAL_PIXEL_FORMAT_RGBA_8888:
               case HAL_PIXEL_FORMAT_RGBX_8888:
               case HAL_PIXEL_FORMAT_BGRA_8888:
                       pixelWidth = 4.0;
                       break;

               case HAL_PIXEL_FORMAT_RGB_888:
                       pixelWidth = 3.0;
                       break;

               case HAL_PIXEL_FORMAT_RGB_565:
                       pixelWidth = 2.0;
                       break;

               case HAL_PIXEL_FORMAT_sRGB_A_8888:
               case HAL_PIXEL_FORMAT_sRGB_X_8888:
                       ALOGE("format 0x%x not support",format);
                       break;

               case HAL_PIXEL_FORMAT_YCbCr_422_SP:
               case HAL_PIXEL_FORMAT_YCrCb_420_SP:
               case HAL_PIXEL_FORMAT_YCbCr_422_I:
               case HAL_PIXEL_FORMAT_YCrCb_NV12:
               case HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO:
                       pixelWidth = 1.0;
                       break;

               case HAL_PIXEL_FORMAT_YCrCb_NV12_10:
                       pixelWidth = 2;
                       break;
               case HAL_PIXEL_FORMAT_YCbCr_422_SP_10:
               case HAL_PIXEL_FORMAT_YCrCb_420_SP_10:
                       pixelWidth = 1.0;
                       break;

               default:
                       pixelWidth = 0.0;
                       ALOGE("format 0x%x not support",format);
                       break;
       }
       return pixelWidth;
}

float vop_band_width(hwc_drm_display_t *hd, std::vector<DrmHwcLayer>& layers)
{
    float scale_factor = 0;
    if(hd->mixMode == HWC_MIX_DOWN || hd->mixMode == HWC_MIX_UP)
    {
        scale_factor += 1.0;
    }
    for(size_t i = 0; i < layers.size(); ++i)
    {
        if(layers[i].bMix)
            continue;
        scale_factor += layers[i].h_scale_mul * layers[i].v_scale_mul;
    }

    return scale_factor;
}

bool GetCrtcSupported(const DrmCrtc &crtc, uint32_t possible_crtc_mask) {
  return !!((1 << crtc.pipe()) & possible_crtc_mask);
}

bool mix_policy(DrmResources* drm, DrmCrtc *crtc, hwc_drm_display_t *hd, std::vector<DrmHwcLayer>& layers)
{
    LayerMap layer_map;
    bool bMatch = false;
    int iMatchCnt = 0;

    combine_layer(layer_map,layers);
    bMatch = MatchPlanes(layer_map,crtc,drm);

    if(bMatch)
    {
        for(std::vector<DrmHwcLayer>::const_iterator iter_layer= layers.begin();
                    iter_layer != layers.end();++iter_layer)
        {
            if((*iter_layer).is_match)
            {
                iMatchCnt++;
            }
        }

        if(iMatchCnt == (int)layers.size())
            return false;
    }

    //Mix
  if(layers.size() <= 4)
        return false;

    //mix down
/*
-----------+----------+------+------+----+------+-------------+--------------------------------+------------------------+------
      GLES | 711aa61e80 | 0000 | 0000 | 00 | 0100 | RGBx_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | com.android.systemui.ImageWallpaper
      GLES | 711ab1ef00 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | com.android.launcher3/com.android.launcher3.Launcher
       HWC | 711aa61e00 | 0000 | 0000 | 00 | 0100 | ? 00000017  |    0.0,    0.0, 3840.0, 2160.0 |  600,  562, 1160,  982 | SurfaceView - MediaView
       HWC | 711aa61100 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0,    2.0 |    0,    0, 2400,    2 | StatusBar
       HWC | 711ec5ad80 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0,   84.0 |    0, 1516, 2400, 1600 | taskbar
       HWC | 711ec5a900 | 0000 | 0002 | 00 | 0105 | RGBA_8888   |    0.0,    0.0,   39.0,   49.0 |  941,  810,  980,  859 | Sprite
*/

    if(layers.size() <= 6 )
    {
        bool has_10_bit_layer = false;
        int format = -1;
        for (size_t i = 0; i < layers.size()-3; ++i)
        {
           if(layers[i].format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
           {
               has_10_bit_layer = true;
               break;
           }
        }

        if(!has_10_bit_layer)
        {
            for (size_t i = 0; i < layers.size()-3; ++i)
            {
                ALOGD_IF(log_level(DBG_DEBUG), "Go into Mix down");
                layers[i].bMix = true;
                hd->mixMode = HWC_MIX_DOWN;
                layers[i].raw_sf_layer->compositionType = HWC_FRAMEBUFFER;
            }
            return true;
        }
    }

/*
need use cross policy
-----------+----------+------+------+----+------+-------------+--------------------------------+------------------------+------
       HWC | 7fb605a580 | 0000 | 0000 | 00 | 0100 | RGBx_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | com.android.systemui.ImageWallpaper
       HWC | 7fb2829880 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | com.android.launcher3/com.android.launcher3.Launcher
       HWC | 7fb2829c80 | 0000 | 0000 | 00 | 0100 | RGBA_8888   |    0.0,   88.0, 2400.0, 1499.0 |    0,  105, 2400, 1516 | android.rk.RockVideoPlayer/android.rk.RockVideoPlayer.RockVideoPlayer
      GLES | 7fa4effb80 | 0000 | 0000 | 00 | 0100 | ? 00000017  |    0.0,    0.0, 3840.0, 2160.0 |  920,  548, 1480,  968 | SurfaceView - MediaView
      GLES | 7fa4eff800 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0,  560.0,  420.0 |  920,  548, 1480,  968 | MediaView
      GLES | 7fb5dda280 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0,    2.0 |    0,    0, 2400,    2 | StatusBar
      GLES | 7fb605a880 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0,   84.0 |    0, 1516, 2400, 1600 | taskbar
      GLES | 7fb5dda380 | 0000 | 0002 | 00 | 0105 | RGBA_8888   |    0.0,    0.0,   39.0,   35.0 |  468, 1565,  507, 1600 | Sprite
 FB TARGET | 7fb605a100 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | HWC_FRAMEBUFFER_TARGET
*/

    //mix up
/*
-----------+----------+------+------+----+------+-------------+--------------------------------+------------------------+------
       HWC | 711aa61e80 | 0000 | 0000 | 00 | 0100 | RGBx_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | com.android.systemui.ImageWallpaper
       HWC | 711ab1ef00 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0, 1600.0 |    0,    0, 2400, 1600 | com.android.launcher3/com.android.launcher3.Launcher
       HWC | 711aa61700 | 0000 | 0000 | 00 | 0100 | ? 00000017  |    0.0,    0.0, 3840.0, 2160.0 |  600,  562, 1160,  982 | SurfaceView - MediaView
      GLES | 711ab1e580 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0,  560.0,  420.0 |  600,  562, 1160,  982 | MediaView
      GLES | 70b34c9c80 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0,    2.0 |    0,    0, 2400,    2 | StatusBar
      GLES | 70b34c9080 | 0000 | 0000 | 00 | 0105 | RGBA_8888   |    0.0,    0.0, 2400.0,   84.0 |    0, 1516, 2400, 1600 | taskbar
      GLES | 711ec5a900 | 0000 | 0002 | 00 | 0105 | RGBA_8888   |    0.0,    0.0,   39.0,   49.0 | 1136, 1194, 1175, 1243 | Sprite
*/
    for (size_t i = 3; i < layers.size(); ++i)
    {
        ALOGD_IF(log_level(DBG_DEBUG),"Go into Mix up");
        layers[i].bMix = true;
        hd->mixMode = HWC_MIX_UP;
        layers[i].raw_sf_layer->compositionType = HWC_FRAMEBUFFER;
    }
    return true;
}
#endif

bool log_level(LOG_LEVEL log_level)
{
#if RK_DRM_HWC_DEBUG
    return g_log_level & log_level;
#else
    UN_USED(log_level);
    return 0;
#endif
}

/*
@func hwc_get_handle_attributes:get attributes from handle.Before call this api,As far as now,
    we need register the buffer first.May be the register is good for processer I think

@param hnd:
@param attrs: if size of attrs is small than 5,it will return EINVAL else
    width  = attrs[0]
    height = attrs[1]
    stride = attrs[2]
    format = attrs[3]
    size   = attrs[4]
*/
int hwc_get_handle_attributes(struct hwc_context_t *ctx, buffer_handle_t hnd, std::vector<int> *attrs)
{
    int ret = 0;
    int op = GRALLOC_MODULE_PERFORM_GET_HADNLE_ATTRIBUTES;

    if (!ctx || !hnd)
        return -EINVAL;

    if(ctx->gralloc && ctx->gralloc->perform)
    {
        ret = ctx->gralloc->perform(ctx->gralloc, op, hnd, attrs);
    }
    else
    {
        ret = -EINVAL;
    }


    if(ret) {
       ALOGE("hwc_get_handle_attributes fail %d for:%s hnd=%p",ret,strerror(ret),hnd);
    }

    return ret;
}

int hwc_get_handle_attibute(struct hwc_context_t *ctx, buffer_handle_t hnd, attribute_flag_t flag)
{
    std::vector<int> attrs;
    int ret=0;

    if(!hnd)
    {
        ALOGE("%s handle is null",__FUNCTION__);
        return -1;
    }

    ret = hwc_get_handle_attributes(ctx, hnd, &attrs);
    if(ret < 0)
    {
        ALOGE("getHandleAttributes fail %d for:%s",ret,strerror(ret));
        return ret;
    }
    else
    {
        return attrs.at(flag);
    }
}

/*
@func getHandlePrimeFd:get prime_fd  from handle.Before call this api,As far as now, we
    need register the buffer first.May be the register is good for processer I think

@param hnd:
@return fd: prime_fd. and driver can call the dma_buf_get to get the buffer

*/
int hwc_get_handle_primefd(struct hwc_context_t *ctx, buffer_handle_t hnd)
{
    int ret = 0;
    int op = GRALLOC_MODULE_PERFORM_GET_HADNLE_PRIME_FD;
    int fd = -1;

    if (!ctx)
        return -EINVAL;

    if(ctx->gralloc && ctx->gralloc->perform)
        ret = ctx->gralloc->perform(ctx->gralloc, op, hnd, &fd);
    else
        ret = -EINVAL;

    return fd;
}

void hwc_list_nodraw(hwc_display_contents_1_t  *list)
{
    if (list == NULL)
    {
        return;
    }
    for (unsigned int i = 0; i < list->numHwLayers - 1; i++)
    {
        list->hwLayers[i].compositionType = HWC_NODRAW;
    }
    return;
}

void hwc_sync_release(hwc_display_contents_1_t  *list)
{
	for (int i=0; i< (int)list->numHwLayers; i++){
		hwc_layer_1_t* layer = &list->hwLayers[i];
		if (layer == NULL){
			return ;
		}
		if (layer->acquireFenceFd>0){
			ALOGV(">>>close acquireFenceFd:%d,layername=%s",layer->acquireFenceFd,layer->LayerName);
			close(layer->acquireFenceFd);
			list->hwLayers[i].acquireFenceFd = -1;
		}
	}

	if (list->outbufAcquireFenceFd>0){
		ALOGV(">>>close outbufAcquireFenceFd:%d",list->outbufAcquireFenceFd);
		close(list->outbufAcquireFenceFd);
		list->outbufAcquireFenceFd = -1;
	}
}

#if RK_VIDEO_UI_OPT
static int CompareLines(int *da,int w)
{
    int i,j;
    for(i = 0;i<1;i++) // compare 4 lins
    {
        for(j= 0;j<w;j+=8)
        {
            if((unsigned int)*da != 0xff000000 && (unsigned int)*da != 0x0)
            {
                return 1;
            }
            da +=8;

        }
    }
    return 0;
}

static int DetectValidData(int *data,int w,int h)
{
    int i,j;
    int *da;
    int ret;
    /*  detect model
    -------------------------
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    --------------------------
    */
    if(data == NULL)
        return 1;
    for(i = 2; i<h; i+= 8)
    {
        da = data +  i *w;
        if(CompareLines(da,w))
            return 1;
    }

    return 0;
}

static void video_ui_optimize(struct hwc_context_t *ctx, hwc_display_contents_1_t *display_content, hwc_drm_display_t *hd)
{
    int ret = 0;
    int format = 0;
    int num_layers = display_content->numHwLayers;
    if(num_layers == 3)
    {
        hwc_layer_1_t *first_layer = &display_content->hwLayers[0];
        if(first_layer->handle)
        {
            format = hwc_get_handle_attibute(ctx,first_layer->handle,ATT_FORMAT);
            if(format == HAL_PIXEL_FORMAT_YCrCb_NV12 || format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
            {
                bool bDiff = true;
                int iUiFd = 0;
                hwc_layer_1_t * second_layer =  &display_content->hwLayers[1];
                format = hwc_get_handle_attibute(ctx,second_layer->handle,ATT_FORMAT);
                if(second_layer->handle &&
                    (format == HAL_PIXEL_FORMAT_RGBA_8888 ||
                    format == HAL_PIXEL_FORMAT_RGBX_8888 ||
                    format == HAL_PIXEL_FORMAT_BGRA_8888)
                  )
                {
                    iUiFd = hwc_get_handle_primefd(ctx, second_layer->handle);
                    bDiff = (iUiFd != hd->iUiFd);

                    if(bDiff)
                    {
                        hd->bHideUi = false;
                        /* Update the backup ui fd */
                        hd->iUiFd = iUiFd;
                    }
                    else if(!hd->bHideUi)
                    {
                        int iWidth = hwc_get_handle_attibute(ctx,second_layer->handle,ATT_WIDTH);
                        int iHeight = hwc_get_handle_attibute(ctx,second_layer->handle,ATT_HEIGHT);
                        unsigned int *cpu_addr;
                        ctx->gralloc->lock(ctx->gralloc, second_layer->handle, GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK,
                                0, 0, iWidth, iHeight, (void **)&cpu_addr);
                        ret = DetectValidData((int *)(cpu_addr),iWidth,iHeight);
                        if(!ret){
                            hd->bHideUi = true;
                            ALOGD_IF(log_level(DBG_VERBOSE), "@video UI close,iWidth=%d,iHeight=%d",iWidth,iHeight);
                        }
                        ctx->gralloc->unlock(ctx->gralloc, second_layer->handle);
                    }

                    if(hd->bHideUi)
                    {
                        second_layer->compositionType = HWC_NODRAW;
                    }
                    else
                    {
                        second_layer->compositionType = HWC_FRAMEBUFFER;
                    }
                }
            }
        }
    }
}
#endif

#if RK_STEREO
static void detect_3d_mode(struct hwc_context_t *ctx, hwc_display_contents_1_t *display_content, int display)
{
    int forceStereop = 0;
    int forceStereoe = 0;
    int force3d = 0;
    int force3dp = hwc_get_int_property("sys.hwc.force3d.primary","0");
    int force3de = hwc_get_int_property("sys.hwc.force3d.external","0");

    if(1==force3dp || 2==force3dp){
        forceStereoe = forceStereop = force3dp;
    }else{
        forceStereoe = forceStereop = 0;
    }

    unsigned int numlayer = display_content->numHwLayers;
    int needStereo = 0;
    for (unsigned int j = 0; j <(numlayer - 1); j++) {
        if(display_content->hwLayers[j].alreadyStereo) {
            needStereo = display_content->hwLayers[j].alreadyStereo;
            break;
        }
    }

    if(0==display && forceStereop){
        needStereo = forceStereop;
    }else if(1==display && forceStereoe){
        needStereo = forceStereoe;
    }

    if(needStereo) {
        ctx->is_3d = true;

        for (unsigned int j = 0; j <(numlayer - 1); j++) {
            display_content->hwLayers[j].displayStereo = needStereo;
        }
    } else {
        for (unsigned int j = 0; j <(numlayer - 1); j++) {
            display_content->hwLayers[j].displayStereo = needStereo;
        }
    }

    if (needStereo & 0x8000) {
        for (unsigned int j = 0; j <(numlayer - 1); j++) {
            display_content->hwLayers[j].alreadyStereo = 0;
            display_content->hwLayers[j].displayStereo = (needStereo & (~0x8000));
        }
    }

    if(1==display && numlayer > 1) {
        ALOGD_IF(log_level(DBG_VERBOSE),"Wake up hwc control stereo");
        pthread_mutex_lock(&ctx->mControlStereo.mlk);
        ctx->mControlStereo.count = needStereo;
        pthread_mutex_unlock(&ctx->mControlStereo.mlk);
        pthread_cond_signal(&ctx->mControlStereo.cond);
    }
}
#endif

#endif

static int hwc_prepare(hwc_composer_device_1_t *dev, size_t num_displays,
                       hwc_display_contents_1_t **display_contents) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;

#if RK_DRM_HWC_DEBUG
    init_log_level();
    hwc_dump_fps();
    ALOGD_IF(log_level(DBG_VERBOSE),"----------------------------frame=%d start ----------------------------",g_frame);
#endif

  std::vector<DrmHwcDisplayContents> layer_contents;
  layer_contents.reserve(num_displays);

#if RK_STEREO
    ctx->is_3d = false;
#endif

  for (int i = 0; i < (int)num_displays; ++i) {
    if (!display_contents[i])
      continue;

    bool use_framebuffer_target = false;

    layer_contents.emplace_back();
    DrmHwcDisplayContents &layer_content = layer_contents.back();

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
    update_display_bestmode(i, connector);
    DrmMode mode = connector->best_mode();
    connector->set_current_mode(mode);
    hd->w_scale = (float)mode.h_display() / hd->framebuffer_width;
    hd->h_scale = (float)mode.v_display() / hd->framebuffer_height;

#if SKIP_BOOT
    if(g_boot_cnt < BOOT_COUNT)
    {
        hwc_list_nodraw(display_contents[i]);
        ALOGD_IF(log_level(DBG_DEBUG),"prepare skip %d",g_boot_cnt);
        return 0;
    }
#endif

    int num_layers = display_contents[i]->numHwLayers;

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

#if RK_DRM_HWC

#if RK_10BIT_BYPASS
    int format = 0;
    hd->is10bitVideo = false;
    for (int j = 0; j < num_layers-1; j++) {
        hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];

        if(layer->handle)
        {
           format = hwc_get_handle_attibute(ctx,layer->handle, ATT_FORMAT);
           if(format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
           {
                hd->is10bitVideo = true;
               // if(layer->compositionType == HWC_NODRAW)
                 //   layer->compositionType = HWC_FRAMEBUFFER;
                break;
           }
        }
    }
#endif

#if RK_STEREO
    detect_3d_mode(ctx, display_contents[i], i);
#endif

    if(!use_framebuffer_target)
        use_framebuffer_target = is_use_gles_comp(ctx, display_contents[i], i);

#if RK_VIDEO_UI_OPT
    video_ui_optimize(ctx, display_contents[i], &ctx->displays[i]);
#endif

#if RK_MIX
    if(!use_framebuffer_target)
    {
        int ret = -1;
        for (int j = 0; j < num_layers-1; j++) {
          hwc_layer_1_t *sf_layer = &display_contents[i]->hwLayers[j];
          if(sf_layer->handle == NULL)
            continue;
          if(sf_layer->compositionType == HWC_NODRAW)
            continue;

          layer_content.layers.emplace_back();
          DrmHwcLayer &layer = layer_content.layers.back();
#if RK_DRM_HWC
          ret = layer.InitFromHwcLayer(ctx, i, sf_layer, ctx->importer.get(), ctx->gralloc);
#else
          ret = layer.InitFromHwcLayer(sf_layer, ctx->importer.get(), ctx->gralloc);
#endif
          if (ret) {
            ALOGE("Failed to init composition from layer %d", ret);
            return ret;
          }
#if RK_DRM_HWC_DEBUG
          std::ostringstream out;
          layer.dump_drm_layer(j,&out);
          ALOGD_IF(log_level(DBG_DEBUG),"%s",out.str().c_str());
#endif
        }
        bool bUseMatch = false;
        int iUsePlane = 0;
        int iPlaneSize = 0;

        hd->mixMode = HWC_DEFAULT;
        bUseMatch = mix_policy(&ctx->drm, crtc, &ctx->displays[i], layer_content.layers);

        std::vector<PlaneGroup *>& plane_groups = ctx->drm.GetPlaneGroups();
        //set use flag to false.
        for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
           iter != plane_groups.end(); ++iter) {
            if(GetCrtcSupported(*crtc, (*iter)->possible_crtcs) && (*iter)->bUse)
                iUsePlane++;
            if(GetCrtcSupported(*crtc, (*iter)->possible_crtcs))
                iPlaneSize++;
        }

        if(bUseMatch)
            iUsePlane++;

        if(iUsePlane >= iPlaneSize)
        {
            float scale_factor = vop_band_width(&ctx->displays[i], layer_content.layers);
            if(scale_factor > 3.3)
            {
                //try mix down
                if(layer_content.layers.size() > 6 && !ctx->displays[i].is10bitVideo)
                {
                    bool has_10_bit_layer = false;
                    int format = -1;


                    for (size_t i = 0; i < layer_content.layers.size(); ++i)
                    {
                        layer_content.layers[i].bMix = false;
                    }

                    for (size_t i = 0; i < 2; ++i)
                    {
                        ALOGD_IF(log_level(DBG_DEBUG), "Go into Mix down");
                        layer_content.layers[i].bMix = true;
                        hd->mixMode = HWC_MIX_DOWN;
                        layer_content.layers[i].raw_sf_layer->compositionType = HWC_FRAMEBUFFER;
                    }

                    scale_factor = vop_band_width(&ctx->displays[i], layer_content.layers);
                    if(scale_factor > 3.3)
                    {
                        ALOGD_IF(log_level(DBG_DEBUG),"scale_factor=%f is bigger than scale Threshold=%f,go to GPU GLES at line=%d", scale_factor, 3.3, __LINE__);
                        use_framebuffer_target = true;
                        hd->mixMode = HWC_DEFAULT;
                        for (size_t i = 0; i < layer_content.layers.size(); ++i)
                        {
                            layer_content.layers[i].bMix = false;
                        }
                    }

                }
                else
                {
                    ALOGD_IF(log_level(DBG_DEBUG),"scale_factor=%f is bigger than scale Threshold=%f,go to GPU GLES at line=%d", scale_factor, 3.3, __LINE__);
                    use_framebuffer_target = true;
                    hd->mixMode = HWC_DEFAULT;
                    for (size_t i = 0; i < layer_content.layers.size(); ++i)
                    {
                        layer_content.layers[i].bMix = false;
                    }
                }
            }
        }
    }
#endif  //end of RK_MIX

#endif

#if RK_DRM_HWC_DEBUG
    for (int j = 0; j < num_layers; j++) {
      hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];
      dump_layer(ctx, false, layer, j);
    }
#endif

    for (int j = 0; !use_framebuffer_target && j < num_layers; ++j) {
      hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];

      if (!(layer->flags & HWC_SKIP_LAYER))
        continue;

      if (skip_layer_indices.first == -1)
        skip_layer_indices.first = j;
        skip_layer_indices.second = j;
    }

    for (int j = 0; j < num_layers; ++j) {
      hwc_layer_1_t *layer = &display_contents[i]->hwLayers[j];

      if (!use_framebuffer_target && !hwc_skip_layer(skip_layer_indices, j)) {
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
#if RK_MIX
        int layer_size = (int)layer_content.layers.size();
        if(j < layer_size)
        {
            if(layer_content.layers[j].bMix)
            {
                continue;
            }
        }
#endif

        if(ctx->isGLESComp)
            ctx->isGLESComp = false;

        if (layer->compositionType == HWC_FRAMEBUFFER)
          layer->compositionType = HWC_OVERLAY;
      } else {
        if(!ctx->isGLESComp)
            ctx->isGLESComp = true;

#if 0 //RK_10BIT_BYPASS 10bit nodraw
        if(hd->is10bitVideo)
        {
            if(layer->handle)
            {
                format = hwc_get_handle_attibute(ctx,layer->handle, ATT_FORMAT);
                if(format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
                {
#if 0
                    void* cpu_addr;
                    gralloc_drm_handle_t *gr_handle = gralloc_drm_handle(layer->handle);
                    ctx->gralloc->lock(ctx->gralloc, layer->handle, gr_handle->usage,
                        0, 0, gr_handle->width, gr_handle->height, (void **)&cpu_addr);
                    memset(cpu_addr, 0x88, gr_handle->size);
                    ALOGD("Clear 10bit video");
                    gralloc_drm_unlock_handle(layer->handle);
#endif
                    layer->compositionType = HWC_NODRAW;
                }
            }
        }
#endif
        switch (layer->compositionType) {
          case HWC_OVERLAY:
          case HWC_BACKGROUND:
          case HWC_SIDEBAND:
          case HWC_CURSOR_OVERLAY:
            layer->compositionType = HWC_FRAMEBUFFER;
            break;
        }
      }
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

#if RK_INVALID_REFRESH
void TimeInt2Obj(int imSecond, timeval *ptVal)
{
    ptVal->tv_sec=imSecond/1000;
    ptVal->tv_usec=(imSecond%1000)*1000;
}
static int hwc_static_screen_opt_set(struct hwc_context_t *ctx)
{
    struct itimerval tv = {{0,0},{0,0}};
    if (!ctx->isGLESComp) {
        int interval_value = hwc_get_int_property("sys.vwb.time", "2500");
        interval_value = interval_value > 5000? 5000:interval_value;
        interval_value = interval_value < 250? 250:interval_value;
        TimeInt2Obj(interval_value,&tv.it_value);
        ALOGD_IF(log_level(DBG_VERBOSE),"reset timer!");
    } else {
        tv.it_value.tv_usec = 0;
        ALOGD_IF(log_level(DBG_VERBOSE),"close timer!");
    }
    setitimer(ITIMER_REAL, &tv, NULL);
    return 0;
}
#endif

static int hwc_set(hwc_composer_device_1_t *dev, size_t num_displays,
                   hwc_display_contents_1_t **sf_display_contents) {
  ATRACE_CALL();
  struct hwc_context_t *ctx = (struct hwc_context_t *)&dev->common;
  int ret = 0;

#if RK_DRM_HWC_DEBUG
 ALOGD_IF(log_level(DBG_VERBOSE),"----------------------------frame=%d end----------------------------",g_frame);
 g_frame++;
#endif

  std::vector<CheckedOutputFd> checked_output_fences;
  std::vector<DrmHwcDisplayContents> displays_contents;
  std::vector<DrmCompositionDisplayLayersMap> layers_map;
  std::vector<std::vector<size_t>> layers_indices;
  std::vector<uint32_t> fail_displays;


  displays_contents.reserve(num_displays);
  // layers_map.reserve(num_displays);
  layers_indices.reserve(num_displays);

  // Phase one does nothing that would cause errors. Only take ownership of FDs.
  for (size_t i = 0; i < num_displays; ++i) {
    hwc_display_contents_1_t *dc = sf_display_contents[i];
    displays_contents.emplace_back();
    DrmHwcDisplayContents &display_contents = displays_contents.back();
    layers_indices.emplace_back();
    std::vector<size_t> &indices_to_composite = layers_indices.back();

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
      hwc_layer_1_t *sf_layer = &dc->hwLayers[j];

      display_contents.layers.emplace_back();
      DrmHwcLayer &layer = display_contents.layers.back();

      // In prepare() we marked all layers FRAMEBUFFER between SKIP_LAYER's.
      // This means we should insert the FB_TARGET layer in the composition
      // stack at the location of the first skip layer, and ignore the rest.
      if (sf_layer->flags & HWC_SKIP_LAYER) {
          //rk: SurfaceFlinger will create acquireFenceFd for nodraw skip layer.
          //    Close it here to avoid anon_inode:sync_fence fd leak.
          if(sf_layer->compositionType == HWC_NODRAW)
          {
                if(sf_layer->acquireFenceFd >= 0)
                    close(sf_layer->acquireFenceFd);
                sf_layer->acquireFenceFd = -1;
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
        indices_to_composite.push_back(idx);
        continue;
      }

      if (sf_layer->compositionType == HWC_OVERLAY)
        indices_to_composite.push_back(j);

        // rk: wait acquireFenceFd at hwc_set.
#if 0
        if(sf_layer->acquireFenceFd > 0)
        {
            sync_wait(sf_layer->acquireFenceFd, -1);
            close(sf_layer->acquireFenceFd);
            sf_layer->acquireFenceFd = -1;
        }
#endif

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

    // This is a catch-all in case we get a frame without any overlay layers, or
    // skip layers, but with a value fb_target layer. This _shouldn't_ happen,
    // but it's not ruled out by the hwc specification
#if RK_MIX
    if ((indices_to_composite.empty() ||  hd->mixMode != HWC_DEFAULT)
        && framebuffer_target_index >= 0) {
#else
    if (indices_to_composite.empty() && framebuffer_target_index >= 0) {
#endif
      hwc_layer_1_t *sf_layer = &dc->hwLayers[framebuffer_target_index];
      if (!sf_layer->handle || (sf_layer->flags & HWC_SKIP_LAYER)) {
        ALOGE(
            "Expected valid layer with HWC_FRAMEBUFFER_TARGET when all "
            "HWC_OVERLAY layers are skipped.");
        fail_displays.emplace_back(i);
        ret = -EINVAL;
      }
#if RK_MIX
      if(hd->mixMode == HWC_MIX_DOWN)
      {
        //In mix mode, fb layer need place at first.
        std::vector<size_t>::iterator it = indices_to_composite.begin();
        indices_to_composite.insert(it, framebuffer_target_index);
      }
      else
#endif
        indices_to_composite.push_back(framebuffer_target_index);
    }
  }

#if 0
  if (ret)
    return ret;
#endif
  for (size_t i = 0; i < num_displays; ++i) {
    hwc_display_contents_1_t *dc = sf_display_contents[i];
    DrmHwcDisplayContents &display_contents = displays_contents[i];
    bool bFindDisplay = false;
    if (!sf_display_contents[i] || i == HWC_DISPLAY_VIRTUAL)
      continue;

    for (auto &fail_display : fail_displays) {
        if( i == fail_display )
        {
            bFindDisplay = true;
#if RK_DRM_HWC_DEBUG
            ALOGD_IF(log_level(DBG_VERBOSE),"%s:line=%d,Find fail display %zu",__FUNCTION__,__LINE__,i);
#endif
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
    std::vector<size_t> &indices_to_composite = layers_indices[i];
#if RK_ZPOS_SUPPORT
    int zpos = 0;
#endif
    for (size_t j : indices_to_composite) {
      hwc_layer_1_t *sf_layer = &dc->hwLayers[j];

      DrmHwcLayer &layer = display_contents.layers[j];
#if RK_DRM_HWC
      ret = layer.InitFromHwcLayer(ctx, i, sf_layer, ctx->importer.get(), ctx->gralloc);
#else
      ret = layer.InitFromHwcLayer(sf_layer, ctx->importer.get(), ctx->gralloc);
#endif
      if (ret) {
        ALOGE("Failed to init composition from layer %d", ret);
        return ret;
      }
#if RK_ZPOS_SUPPORT
      layer.zpos = zpos;
      zpos++;
#endif

#if RK_DRM_HWC_DEBUG
      std::ostringstream out;
      layer.dump_drm_layer(j,&out);
      ALOGD_IF(log_level(DBG_DEBUG),"%s",out.str().c_str());
#endif
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

#if RK_10BIT_BYPASS
    ctx->drm.compositor()->setSkipPreComp(i,hd->is10bitVideo);
#endif

    for (auto &fail_display : fail_displays) {
        if( i == fail_display )
        {
            bFindDisplay = true;
#if RK_DRM_HWC_DEBUG
            ALOGD_IF(log_level(DBG_DEBUG),"%s:line=%d,Find fail display %zu",__FUNCTION__,__LINE__,i);
#endif
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
  hwc_static_screen_opt_set(ctx);
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

#if RK_DRM_HWC
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
#endif
  DrmConnector *connector = ctx->drm.GetConnectorFromType(display);
  if (!connector) {
    ALOGE("Failed to get connector for display %d line=%d", display,__LINE__);
    return -ENODEV;
  }

  connector->force_disconnect(dpmsValue == DRM_MODE_DPMS_OFF);

  ctx->drm.UpdateDisplayRoute();
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

  update_display_bestmode(display, connector);
  DrmMode mode = connector->best_mode();

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

  *num_configs = 1;
  configs[0] = connector->display();

  return 0;
}

static int hwc_get_display_attributes(struct hwc_composer_device_1 *dev,
                                      int display, uint32_t config,
                                      const uint32_t *attributes,
                                      int32_t *values) {
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

int init_thread_pamaters(threadPamaters* mThreadPamaters)
{
    if(mThreadPamaters) {
        mThreadPamaters->count = 0;
        pthread_mutex_init(&mThreadPamaters->mtx, NULL);
        pthread_mutex_init(&mThreadPamaters->mlk, NULL);
        pthread_cond_init(&mThreadPamaters->cond, NULL);
    } else {
        ALOGE("{%s}%d,mThreadPamaters is NULL",__FUNCTION__,__LINE__);
    }
    return 0;
}

int free_thread_pamaters(threadPamaters* mThreadPamaters)
{
    if(mThreadPamaters) {
        pthread_mutex_destroy(&mThreadPamaters->mtx);
        pthread_mutex_destroy(&mThreadPamaters->mlk);
        pthread_cond_destroy(&mThreadPamaters->cond);
    } else {
        ALOGE("{%s}%d,mThreadPamaters is NULL",__FUNCTION__,__LINE__);
    }
    return 0;
}

static int hwc_device_close(struct hw_device_t *dev) {
  struct hwc_context_t *ctx = (struct hwc_context_t *)dev;

#if RK_INVALID_REFRESH
    free_thread_pamaters(&ctx->mRefresh);
#endif
#if RK_STEREO
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

#if RK_STEREO
#define READ_3D_MODE  (0)
#define WRITE_3D_MODE (1)

int map_3d_mode(int value, int flag)
{
    if(flag == READ_3D_MODE)
    {
        switch (value)
        {
            case 0:
                return 8;
            case 6:
                return 2;
            case 8:
                return 1;
            default:
                return 0;
        }
    }
    else if(flag == WRITE_3D_MODE)
    {
        switch (value)
        {
            case 1:
                return 8;
            case 2:
                return 6;
            case 8:
                return 0;
            default:
                return -1;
        }
    }
    else
    {
        ALOGE("%s:line=%d invalid flag =%d", __FUNCTION__, __LINE__, flag);
        return -1;
    }

}

/*
 * get or set 3d mode.
 * flag : 0--read 1--write
 */
int hwc_control_3dmode(hwc_context_t* ctx, int value, int flag)
{
    int ret = 0;
    int iMode;
    ssize_t err;
    char buf[200];
    int fd = ctx->fd_3d;

    if(ctx->fd_3d < 0 || !ctx)
        return -1;

    switch(flag){
    case READ_3D_MODE: //read
        memset(buf,0,sizeof(buf));
        lseek(fd,0,SEEK_SET);
        err = read(fd, buf, sizeof(buf));
        if(err <= 0)
            ALOGW("read hdmi 3dmode err=%zd",err);

        int mode,hdmi3dmode;
        sscanf(buf,"3dmodes=%d cur3dmode=%d",&mode,&hdmi3dmode);
        ALOGI_IF(log_level(DBG_VERBOSE),"hdmi3dmode=%d,mode=%d",hdmi3dmode,mode);

        ret = map_3d_mode(value, READ_3D_MODE);
        break;

    case WRITE_3D_MODE: //write
        lseek(fd,0,SEEK_SET);
        iMode = map_3d_mode(value, WRITE_3D_MODE);
        char acMode[25];
        if(iMode != -1)
        {
            sprintf(acMode,"%d",iMode);
            ret = write(fd, acMode, 2);
            if(ret < 0)
            {
                ALOGE("change 3dmode to %d err is %s",value,strerror(errno));
            }
        }
        else
        {
            ALOGE("%s:line=%d invalid write mode", __FUNCTION__, __LINE__);
        }
        break;

    default:
        break;
    }
    return ret;
}


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
        ret = hwc_control_3dmode(ctx, 2, READ_3D_MODE);
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
#if RK_DRM_HWC_DEBUG
  g_log_level = 0;
  g_frame = 0;
  init_log_level();
#endif

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


#if RK_DRM_HWC
    ctx->fb_fd = open("/dev/graphics/fb0", O_RDWR, 0);
    if(ctx->fb_fd < 0)
    {
         ALOGE("Open fb0 fail in %s",__FUNCTION__);
         return -1;
    }

  hwc_init_version();
#endif

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
#if 0
    int interval_value = hwc_get_int_property("sys.vwb.time", "2500");
    interval_value = interval_value > 5000? 5000:interval_value;
    interval_value = interval_value < 250? 250:interval_value;

    struct itimerval tv = {{0,0},{0,0}};
    TimeInt2Obj(1,&tv.it_value);
    TimeInt2Obj(interval_value,&tv.it_interval);
    setitimer(ITIMER_REAL,&tv,NULL);
#endif
#endif

#if RK_STEREO
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
