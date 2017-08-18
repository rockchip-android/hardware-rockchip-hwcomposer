#ifndef _HWC_ROCKCHIP_H_
#define _HWC_ROCKCHIP_H_

#include <map>
#include <vector>
#include "drmhwcomposer.h"
#include "drmresources.h"
#include "vsyncworker.h"


namespace android {
//G6110_SUPPORT_FBDC
#define FBDC_BGRA_8888                  0x125 //HALPixelFormatSetCompression(HAL_PIXEL_FORMAT_BGRA_8888,HAL_FB_COMPRESSION_DIRECT_16x4)
#define FBDC_RGBA_8888                  0x121 //HALPixelFormatSetCompression(HAL_PIXEL_FORMAT_RGBA_8888,HAL_FB_COMPRESSION_DIRECT_16x4)

#define MOST_WIN_ZONES                  4
#if RK_STEREO
#define READ_3D_MODE  			(0)
#define WRITE_3D_MODE 			(1)
#endif

#if RK_VIDEO_SKIP_LINE
#define SKIP_LINE_NUM_NV12_10		(4)
#define SKIP_LINE_NUM_NV12		(2)
#endif

typedef std::map<int, std::vector<DrmHwcLayer*>> LayerMap;
typedef LayerMap::iterator LayerMapIter;
struct hwc_context_t;
class VSyncWorker;

typedef enum attribute_flag {
    ATT_WIDTH = 0,
    ATT_HEIGHT,
    ATT_STRIDE,
    ATT_FORMAT,
    ATT_SIZE,
    ATT_BYTE_STRIDE
}attribute_flag_t;

typedef enum tagMixMode
{
    HWC_DEFAULT,
    HWC_MIX_DOWN,
    HWC_MIX_UP,
    HWC_MIX_CROSS,
    HWC_MIX_3D,
    HWC_POLICY_NUM
}MixMode;

enum HDMI_STAT
{
    HDMI_INVALID,
    HDMI_ON,
    HDMI_OFF
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

typedef struct hwc_drm_display {
  struct hwc_context_t *ctx;
  int display;
#if RK_VIDEO_UI_OPT
  int iUiFd;
  bool bHideUi;
#endif
  bool is10bitVideo;
  MixMode mixMode;
  bool isVideo;
  bool isHdr;
  int framebuffer_width;
  int framebuffer_height;
  int rel_xres;
  int rel_yres;
  int v_total;
  int vrefresh;
  int iPlaneSize;
  float w_scale;
  float h_scale;
  bool active;
  bool is_3d;
  bool is_interlaced;
  Mode3D stereo_mode;
  HDMI_STAT last_hdmi_status;
  int display_timeline;
  int hotplug_timeline;
} hwc_drm_display_t;

int hwc_init_version();

#if USE_AFBC_LAYER
bool isAfbcInternalFormat(uint64_t internal_format);
#endif

int init_thread_pamaters(threadPamaters* mThreadPamaters);
int free_thread_pamaters(threadPamaters* mThreadPamaters);

#if RK_INVALID_REFRESH
int hwc_static_screen_opt_set(bool isGLESComp);
#endif

#if 1
int detect_3d_mode(hwc_drm_display_t *hd, hwc_display_contents_1_t *display_content, int display);
#endif
#if 0
int hwc_control_3dmode(int fd_3d, int value, int flag);
#endif
float getPixelWidthByAndroidFormat(int format);
int hwc_get_handle_width(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_height(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_format(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_stride(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_byte_stride(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_usage(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_size(const gralloc_module_t *gralloc, buffer_handle_t hnd);
int hwc_get_handle_attributes(const gralloc_module_t *gralloc, buffer_handle_t hnd, std::vector<int> *attrs);
int hwc_get_handle_attibute(const gralloc_module_t *gralloc, buffer_handle_t hnd, attribute_flag_t flag);
int hwc_get_handle_primefd(const gralloc_module_t *gralloc, buffer_handle_t hnd);
#if RK_DRM_GRALLOC
uint32_t hwc_get_handle_phy_addr(const gralloc_module_t *gralloc, buffer_handle_t hnd);
#endif
bool vop_support_format(uint32_t hal_format);
bool vop_support_scale(hwc_layer_1_t *layer);
bool GetCrtcSupported(const DrmCrtc &crtc, uint32_t possible_crtc_mask);
bool match_process(DrmResources* drm, DrmCrtc *crtc, bool is_interlaced,
                        std::vector<DrmHwcLayer>& layers, int iPlaneSize, int fbSize,
                        std::vector<DrmCompositionPlane>& composition_planes);
bool mix_policy(DrmResources* drm, DrmCrtc *crtc, hwc_drm_display_t *hd,
                std::vector<DrmHwcLayer>& layers, int iPlaneSize, int fbSize,
                std::vector<DrmCompositionPlane>& composition_planes);
#if RK_VIDEO_UI_OPT
void video_ui_optimize(const gralloc_module_t *gralloc, hwc_display_contents_1_t *display_content, hwc_drm_display_t *hd);
#endif
void hwc_list_nodraw(hwc_display_contents_1_t  *list);
void hwc_sync_release(hwc_display_contents_1_t  *list);


}

#endif
