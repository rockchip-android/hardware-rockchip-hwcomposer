/*

* rockchip hwcomposer( 2D graphic acceleration unit) .

*

* Copyright (C) 2015 Rockchip Electronics Co., Ltd.

*/




#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "rk_hwcomposer.h"
#include "version.h"

#include <hardware/hardware.h>

#include <sys/prctl.h>
#include <stdlib.h>
#include <errno.h>
#include <cutils/properties.h> 
#include <fcntl.h>
#include <sync/sync.h>
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#include  <hardware/hal_public.h>
#include  <linux/fb.h>
#else
#include "gralloc_priv.h"
#endif
#include <time.h>
#include <poll.h>
#include "rk_hwcomposer_hdmi.h"
#include <ui/PixelFormat.h>
#include <sys/stat.h>
#include "hwc_ipp.h"
#include "hwc_rga.h"
#include <ui/PixelFormat.h>
#include "TVInfo.h"

#define MAX_DO_SPECIAL_COUNT        5
#define RK_FBIOSET_ROTATE            0x5003
#define FPS_NAME                    "com.aatt.fpsm"
#define BOTTOM_LAYER_NAME           "NavigationBar"
#define TOP_LAYER_NAME              "StatusBar"
#define WALLPAPER                   "ImageWallpaper"
#define INPUT                       "InputMethod"
#define PopWin                      "PopupWindow"
#define BOTTOM_LAYER_NAME1          "StatusBar"
#define USE_HWC_FENCE               1
#define RK_FBIOGET_DSP_ADDR      0x4630
#define RK_FBIOGET_DSP_FD          0x4630
#define RK_FBIOGET_LIST_STAT     0X4631
#define FB1_IOCTL_SET_YUV_ADDR     0x5002
#define RK_FBIOSET_DMABUF_FD     0x5004
#undef LOGV
#define LOGV(...)

static int SkipFrameCount = 0;
//static bool LastUseGpuCompose = false;
static hwcContext * gcontextAnchor[HWC_NUM_DISPLAY_TYPES] = {0};
static int skip_count = 0;
static int last_video_addr[2];
static char const* compositionModeName[] = {
        "HWC_VOP",
        "HWC_RGA",
        "HWC_VOP_RGA",
        "HWC_RGA_VOP",
        "HWC_RGA_TRSM_VOP",
        "HWC_RGA_TRSM_GPU_VOP",
        "HWC_VOP_GPU",
        "HWC_NODRAW_GPU_VOP",
        "HWC_GPU_NODRAW_VOP",
        "HWC_RGA_GPU_VOP",
        "HWC_GPU_VOP",
        "HWC_CP_FB",
        "HWC_GPU",
};


static int  hwc_blank(struct hwc_composer_device_1 *dev,int dpy,int blank);
static int  hwc_query(struct hwc_composer_device_1* dev,int what,int* value);
static int  hwc_event_control(struct hwc_composer_device_1* dev,int dpy,int event,int enabled);
static int  hwc_prepare(hwc_composer_device_1_t * dev,size_t numDisplays,hwc_display_contents_1_t** displays);
static int  hwc_set(hwc_composer_device_1_t * dev,size_t numDisplays,hwc_display_contents_1_t  ** displays);

int         hotplug_get_config(int flag);
int         hotplug_set_config();
int         hotplug_set_overscan(int flag);
int         hotplug_reset_dstpos(struct rk_fb_win_cfg_data * fb_info,int flag);
void*       hotplug_init_thread(void *arg);
void*       hotplug_invalidate_refresh(void *arg);
int         hotpulg_did_hdr_video(hwcContext *ctx,struct rk_fb_win_par *win_par, struct private_handle_t* src_handle);



static int  hwc_device_close(struct hw_device_t * dev);
static int  hwc_device_open(const struct hw_module_t * module,const char * name,struct hw_device_t ** device);
static int  DetectValidData( hwcContext * context,int *data,int w,int h);
int         getFbInfo(hwc_display_t dpy, hwc_surface_t surf, hwc_display_contents_1_t *list);
int init_tv_hdr_info(hwcContext *ctx)
{
   int r = HdmiSupportedDataSpace();

   ctx->hdrSupportType = r;    
   return 0;
}
int deinit_tv_hdr_info(hwcContext *ctx)
{
    HdmiSetHDR(0);
    HdmiSetColorimetry(HAL_DATASPACE_UNKNOWN);
    ctx->hdrStatus = 0;
    ctx->hdrFrameStatus = 0;
    return 0;
}

static struct hw_module_methods_t hwc_module_methods =
{
open:
    hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM =
{
common:
    {
tag:
        HARDWARE_MODULE_TAG,
version_major:
        1,
version_minor:
        2,
id:
        HWC_HARDWARE_MODULE_ID,
name:          "Hardware Composer Module"
        ,
author:        "Rockchip Corporation"
        ,
methods:
        &hwc_module_methods,
dso:
        NULL,
reserved:
        {
            0,
        }
    }
};

void hwc_list_nodraw(hwc_display_contents_1_t  *list)
{
    if (list == NULL)
    {
        return;
    }
    for (unsigned int i = 0; i < list->numHwLayers - 1; i++)
    {
        list->hwLayers[i].compositionType = HWC_OVERLAY;
    }
    return;
}

//return property value of pcProperty
int hwc_get_int_property(const char* pcProperty, const char* default_value)
{
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;

    if (pcProperty == NULL || default_value == NULL)
    {
        ALOGE("hwc_get_int_property: invalid param");
        return -1;
    }

    property_get(pcProperty, value, default_value);
    new_value = atoi(value);

    return new_value;
}

static int hwc_get_string_property(const char* pcProperty, const char* default_value, char* retult)
{
    if (pcProperty == NULL || default_value == NULL || retult == NULL)
    {
        ALOGE("hwc_get_string_property: invalid param");
        return -1;
    }

    property_get(pcProperty, retult, default_value);

    return 0;
}
void is_debug_log(void)
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    context->Is_debug = hwc_get_int_property("sys.hwc.log","0");
    context->Is_noi = hwc_get_int_property("sys.hwc.noi","0");

}
int is_out_log( void )
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    return context->Is_debug;
}

bool is_suit_nodraw(hwc_layer_1_t * layer,hwcContext * context)
{
    hwc_rect_t * DstRect = &layer->displayFrame;
    hwc_region_t * Region = &layer->visibleRegionScreen;
    int dst_width = DstRect->right - DstRect->left;
    int dst_height = DstRect->bottom - DstRect->top;
    int screen_area = context->fbWidth * context->fbHeight;
    if(dst_height * dst_width < screen_area / 4)
        return false;
    if(Region->numRects > 1) return false;
    return true;
}

bool is_not_suit_mix_policy(hwc_display_contents_1_t *list)
{
    struct private_handle_t * handle = NULL;
    hwc_layer_1_t * layer = NULL;

    if (!list)
        return true;

    if (list->numHwLayers <= 0)
        return true;

    layer = &list->hwLayers[list->numHwLayers - 1];
    if (layer)
        handle = (struct private_handle_t *)layer->handle;

    switch (handle && handle->format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            return false;
        default:
            break;
    }

    return true;
}

bool is_same_rect(hwc_rect_t rs,hwc_rect_t ls)
{
    bool res = true;
    res &= rs.left == ls.left;
    res &= rs.top == ls.top;
    res &= rs.right == ls.right;
    res &= rs.bottom == ls.bottom;
    return res;
}

bool is_need_draw(hwcContext * context,hwc_display_contents_1_t *list,int index,int mode)
{
    bool res = false;
    hwc_rect_t * drest = NULL;
    hwc_layer_1_t * layer = NULL;
    struct private_handle_t * handle = NULL;
    res = context->mLastStatus.ovleryLayer != index;
    res = res || context->last_composer_mode != mode;
    res = res || context->mLastStatus.numLayers != (int)(list->numHwLayers - 1);
    LOGV("[%d,%d,%d]--[%d,%d,%d]",context->mLastStatus.ovleryLayer,context->last_composer_mode,
        context->mLastStatus.numLayers,index,mode,list->numHwLayers - 1);
    for(int i = 0;i < (int)(list->numHwLayers - 1);i++)
    {
        if(res) return res;
        if(i == index) continue;
        layer = &list->hwLayers[i];
        drest = &layer->displayFrame;
        handle = (struct private_handle_t *) layer->handle;
        res = res || context->mLastStatus.fd[i] != ((handle != NULL) ? handle->share_fd:0);
        res = res || context->mLastStatus.alpha[i] != (layer->blending) >> 16;
        res = res || context->mLastStatus.drect[i].left   != drest->left;
        res = res || context->mLastStatus.drect[i].right  != drest->right;
        res = res || context->mLastStatus.drect[i].top    != drest->top;
        res = res || context->mLastStatus.drect[i].bottom != drest->bottom;
        LOGV("I=%d,res=%d:[%d,%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d,%d]",i,res,
            context->mLastStatus.fd[i],context->mLastStatus.alpha[i],
            context->mLastStatus.drect[i].left,context->mLastStatus.drect[i].right,
            context->mLastStatus.drect[i].top,context->mLastStatus.drect[i].bottom,
            (handle != NULL) ? handle->share_fd:0,
            (layer->blending) >> 16,drest->left,drest->right,drest->top,drest->bottom );
    }
    return res;
}

bool is_displayframe_intersect(hwc_layer_1_t * rs,hwc_layer_1_t * ls)
{
    bool xret = false;
    bool yret = false;
    hwc_rect_t * rDstRect = &rs->displayFrame;
    hwc_rect_t * lDstRect = &ls->displayFrame;

    xret = xret || (rDstRect->left >= lDstRect->left && rDstRect->left <= lDstRect->right);
    LOGV("%d,%d",__LINE__,xret);
    yret = yret || (rDstRect->top >= lDstRect->top && rDstRect->top <= lDstRect->bottom);
    LOGV("%d,%d",__LINE__,yret);
    xret = xret || (rDstRect->left <= lDstRect->left && rDstRect->right >= lDstRect->left);
    LOGV("%d,%d",__LINE__,xret);
    yret = yret || (rDstRect->top <= lDstRect->top && rDstRect->bottom >= lDstRect->top);
    LOGV("%d,%d",__LINE__,yret);
    LOGV("rs [%d,%d,%d,%d] ls[%d,%d,%d,%d]",rDstRect->left,rDstRect->top,rDstRect->right,rDstRect->bottom,
        lDstRect->left,lDstRect->top,lDstRect->right,lDstRect->bottom);
    return xret && yret;
}

int is_vop_yuv(int format)
{
    int ret = 0;
    switch(format){
        case 0x20:
        case 0x22:
            ret = 1;
            break;

        default:
            break;
    }
    return ret;
}

int type_of_android_format(int fmt)
{
    //1:yuv,2:rgb
    int ret = -1;
    switch(fmt){
        case HAL_PIXEL_FORMAT_YCrCb_NV12:
        case HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO:
        case HAL_PIXEL_FORMAT_YCrCb_NV12_10:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
	    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
	    case HAL_PIXEL_FORMAT_YCbCr_422_I:
	    case HAL_PIXEL_FORMAT_YCbCr_422_SP_10:
	    //case HAL_PIXEL_FORMAT_YCrCb_444_SP_10:
            ret = 1;
            break;

        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_RGB_565:
	    case HAL_PIXEL_FORMAT_BGRA_8888:
	    //case HAL_PIXEL_FORMAT_RGBA_5551:
	    //case HAL_PIXEL_FORMAT_RGBA_4444:
            ret = 2;
            break;

        default:
            ret = -1;
            break;
    }
    return ret;
}

static int LayerZoneCheck(hwc_layer_1_t * Layer,hwcContext * Context)
{
    hwc_region_t * Region = &(Layer->visibleRegionScreen);
    hwc_rect_t const * rects = Region->rects;
    hwc_rect_t * SrcRect = &Layer->sourceCrop;
    hwc_rect_t * DstRect = &Layer->displayFrame;
    hwc_rect_t  rect_merge;
    int left_min = 0; 
    int top_min = 0;
    int right_max = 0;
    int bottom_max = 0;
    hwcRECT dstRects ;
    hwcRECT srcRects ;
    float hfactor = 1.0;
    float vfactor = 1.0;

    
    unsigned int i;
    for (i = 0; i < (unsigned int) Region->numRects ;i++)
    {
        LOGV("checkzone=%s,[%d,%d,%d,%d]", \
             Layer->LayerName, rects[i].left, rects[i].top, rects[i].right, rects[i].bottom);
        if (rects[i].left < 0 || rects[i].top < 0
                || rects[i].right > Context->fbWidth
                || rects[i].bottom > Context->fbHeight)
        {
            return -1;
        }
    }


    if(rects)
    {
         left_min = rects[0].left; 
         top_min  = rects[0].top;
         right_max  = rects[0].right;
         bottom_max = rects[0].bottom;
    }
    for (unsigned int r = 0; r < (unsigned int) Region->numRects ; r++)
    {
        int r_left;
        int r_top;
        int r_right;
        int r_bottom;
       
        r_left   = hwcMAX(DstRect->left,   rects[r].left);
        left_min = hwcMIN(r_left,left_min);
        r_top    = hwcMAX(DstRect->top,    rects[r].top);
        top_min  = hwcMIN(r_top,top_min);
        r_right    = hwcMIN(DstRect->right,  rects[r].right);
        right_max  = hwcMAX(r_right,right_max);
        r_bottom = hwcMIN(DstRect->bottom, rects[r].bottom);
        bottom_max  = hwcMAX(r_bottom,bottom_max);
    }
    rect_merge.left = left_min;
    rect_merge.top = top_min;
    rect_merge.right = right_max;
    rect_merge.bottom = bottom_max;

    dstRects.left   = hwcMAX(DstRect->left,   rect_merge.left);
    dstRects.top    = hwcMAX(DstRect->top,    rect_merge.top);
    dstRects.right  = hwcMIN(DstRect->right,  rect_merge.right);
    dstRects.bottom = hwcMIN(DstRect->bottom, rect_merge.bottom);

    if((dstRects.right - dstRects.left ) <= 2
        || (dstRects.bottom - dstRects.top) <= 2)
    {
        if(is_out_log())
			ALOGW("zone is small ,LCDC can not support");
        return -1;
    }

    hfactor = (float)(Layer->sourceCrop.right - Layer->sourceCrop.left)
                     / (Layer->displayFrame.right - Layer->displayFrame.left);

    vfactor = (float)(Layer->sourceCrop.bottom - Layer->sourceCrop.top)
                     / (Layer->displayFrame.bottom - Layer->displayFrame.top);

    srcRects.left   = hwcMAX ((SrcRect->left \
                 - (int) ((DstRect->left   - dstRects.left)   * hfactor)),0);
    srcRects.top    = hwcMAX ((SrcRect->top \
                 - (int) ((DstRect->top    - dstRects.top)    * vfactor)),0);

    srcRects.right  = SrcRect->right \
                      - (int) ((DstRect->right  - dstRects.right)  * hfactor);
    srcRects.bottom = SrcRect->bottom \
                      - (int) ((DstRect->bottom - dstRects.bottom) * vfactor);

    if((srcRects.right - srcRects.left ) <= 16
        || (srcRects.bottom - srcRects.top) <= 16)
    {
        if(is_out_log())
			ALOGW("source is too small ,LCDC can not support");
        return -1;
    }

    hfactor = (float)(Layer->sourceCrop.right - Layer->sourceCrop.left)
          / (Layer->displayFrame.right - Layer->displayFrame.left);

    vfactor = (float)(Layer->sourceCrop.bottom - Layer->sourceCrop.top)
              / (Layer->displayFrame.bottom - Layer->displayFrame.top);

    if(hfactor >= 8 || hfactor <= 0.125 || vfactor >= 8 || vfactor <= 0.125)
    {
        if(is_out_log())
            ALOGD("line=%d,scale over[%d,%d]",__LINE__,hfactor,vfactor);
        return -1;
    }

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

void dump_platform_info()
{
    hwcContext * ctxp = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    hwcContext * ctxe = gcontextAnchor[HWC_DISPLAY_EXTERNAL];
    if(ctxp && is_out_log() > 3)
        ALOGI("Primary context:IsRk3188=%d,IsRk3126=%d,IsRk3128=%d,IsRk322x=%d,IsRkBox=%d",
            ctxp->IsRk3188,ctxp->IsRk3126,ctxp->IsRk3128,ctxp->IsRk322x,ctxp->IsRkBox);
    if(ctxp && ctxe && is_out_log() > 3)
        ALOGI("Primary context:IsRk3188=%d,IsRk3126=%d,IsRk3128=%d,IsRk322x=%d,IsRkBox=%d",
            ctxe->IsRk3188,ctxe->IsRk3126,ctxe->IsRk3128,ctxe->IsRk322x,ctxe->IsRkBox);
    return;
}

void sort_fbinfo_by_winid(struct rk_fb_win_cfg_data* fb_info)
{
    struct rk_fb_win_cfg_data fbinfo;
    memcpy((void*)&fbinfo,(void*)fb_info,sizeof(struct rk_fb_win_cfg_data));
    for(int i = 0;i<4;i++)
    {
        for(int j=0;j<4;j++)
        {
            if(fb_info->win_par[i].area_par[j].ion_fd || fb_info->win_par[i].area_par[j].phy_addr)
            {
                int win_id = fb_info->win_par[i].win_id;
                memcpy(&(fbinfo.win_par[win_id]),&(fb_info->win_par[i]),sizeof(struct rk_fb_win_par));
            }
        }
    }
    memcpy((void*)fb_info,(void*)&fbinfo,sizeof(struct rk_fb_win_cfg_data));
    return;
}

void dump_config_info(hwcContext * context,struct rk_fb_win_cfg_data fb_info)
{
    for(int i = 0;i<4;i++)
    {
        for(int j=0;j<4;j++)
        {
            if(fb_info.win_par[i].area_par[j].ion_fd || fb_info.win_par[i].area_par[j].phy_addr)
            {
               if(is_out_log())
                    ALOGD("%s,par[%d],area[%d],z_win_galp[%d,%d,%x],[%d,%d,%d,%d]=>[%d,%d,%d,%d],w_h_f[%d,%d,%d],acq_fence_fd=%d,fd=%d,addr=%x",
                        context == gcontextAnchor[0] ? "primary:" : "external:",
                        i,j,
                        fb_info.win_par[i].z_order,
                        fb_info.win_par[i].win_id,
                        fb_info.win_par[i].g_alpha_val,
                        fb_info.win_par[i].area_par[j].x_offset,
                        fb_info.win_par[i].area_par[j].y_offset,
                        fb_info.win_par[i].area_par[j].xact,
                        fb_info.win_par[i].area_par[j].yact,
                        fb_info.win_par[i].area_par[j].xpos,
                        fb_info.win_par[i].area_par[j].ypos,
                        fb_info.win_par[i].area_par[j].xsize,
                        fb_info.win_par[i].area_par[j].ysize,
                        fb_info.win_par[i].area_par[j].xvir,
                        fb_info.win_par[i].area_par[j].yvir,
                        fb_info.win_par[i].area_par[j].data_format,
                        fb_info.win_par[i].area_par[j].acq_fence_fd,
                        fb_info.win_par[i].area_par[j].ion_fd,
                        fb_info.win_par[i].area_par[j].phy_addr);
            }
        }
    }
    return;
}

void handle_gpu_nodraw_optimazation(hwcContext* ctx,hwc_display_contents_1_t *list,int index,int mode)
{
    unsigned int i ;
    hwc_rect_t * drest = NULL;
    int relOverlayLayer = index;

    hwc_layer_1_t * layer = NULL;
    struct private_handle_t * handle = NULL;

    if(is_need_draw(ctx,list,relOverlayLayer,mode))
    {
        for (i = 0; i < (list->numHwLayers - 1); i++)
        {
            layer = &list->hwLayers[i];
            if((int)i == relOverlayLayer)
                continue;
            else
            {
                layer->compositionType = HWC_FRAMEBUFFER;
                drest = &layer->displayFrame;
                handle = (struct private_handle_t *)layer->handle;
                ctx->mLastStatus.fd[i] = handle?handle->share_fd:0;
                ctx->mLastStatus.alpha[i] = (layer->blending) >> 16;
                ctx->mLastStatus.drect[i].left = drest->left;
                ctx->mLastStatus.drect[i].right = drest->right;
                ctx->mLastStatus.drect[i].top = drest->top;
                ctx->mLastStatus.drect[i].bottom = drest->bottom;
            }
        }
        ctx->mLastStatus.ovleryLayer = relOverlayLayer;
        ctx->mLastStatus.numLayers = list->numHwLayers - 1;
    }
    else
    {
        for (i = 0; i < (list->numHwLayers - 1); i++)
        {
            layer = &list->hwLayers[i];
            if((int)i == relOverlayLayer)
                continue;
            else
                layer->compositionType = HWC_NODRAW;
        }
    }
    return;
}

int is_need_stereo(hwcContext* ctx, hwc_display_contents_1_t *list)
{
    if(!ctx || !list)
        return -1;

    int needStereo = 0;
    unsigned int numlayer = list->numHwLayers;

    ctx->isStereo = 0;

    for (unsigned int j = 0; j <(numlayer - 1); j++)
    {
        if(list->hwLayers[j].alreadyStereo)
        {
            needStereo = list->hwLayers[j].alreadyStereo;
            break;
        }
    }

    if (needStereo)
    {
        ctx->isStereo = needStereo;
        for (unsigned int j = 0; j <(numlayer - 1); j++)
            list->hwLayers[j].displayStereo = needStereo;
    }
    else
    {
        for (unsigned int j = 0; j <(numlayer - 1); j++)
            list->hwLayers[j].displayStereo = needStereo;
    }

    return 0;
}

bool is_screen_changed(hwcContext* ctx, int dpyId)
{
    if (!ctx)
        return false;

    if (ctx->dpyAttr[dpyId].xres != ctx->dpyAttr[dpyId].relxres) {
        ctx->mScreenChanged = true;
        return true;
    }

    if (ctx->dpyAttr[dpyId].yres != ctx->dpyAttr[dpyId].relyres) {
        ctx->mScreenChanged = true;
        return true;
    }

    return false;
}

bool is_boot_skip_platform(hwcContext * context)
{
    bool skipPlatform  = false;

    skipPlatform = skipPlatform || context->IsRk3126;
    skipPlatform = skipPlatform || context->IsRk3128;
    skipPlatform = skipPlatform || (context->IsRk322x && !context->IsRk3328);
    skipPlatform = skipPlatform || context->IsRk3036;

    return skipPlatform;
}

void hwc_sync(hwc_display_contents_1_t  *list)
{
    if (list == NULL)
    {
        return ;
    }
    for (unsigned int i = 0; i < list->numHwLayers; i++)
    {
        if (list->hwLayers[i].acquireFenceFd > 0)
        {
           // if( i == 1)
             //   ALOGD("hwc_sync,name=%s",list->hwLayers[i].LayerName);

            sync_wait(list->hwLayers[i].acquireFenceFd, 500);
            ALOGV("fenceFd=%d,name=%s", list->hwLayers[i].acquireFenceFd, list->hwLayers[i].LayerName);
        }

    }
}

void hwc_sync_release(hwc_display_contents_1_t  *list)
{
    for (unsigned int i = 0; i < list->numHwLayers; i++)
    {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        if (layer == NULL)
        {
            return ;
        }
        if (layer->acquireFenceFd > 0)
        {
            ALOGV(">>>close acquireFenceFd:%d,layername=%s", layer->acquireFenceFd, layer->LayerName);
            close(layer->acquireFenceFd);
            list->hwLayers[i].acquireFenceFd = -1;
        }
    }

    if (list->outbufAcquireFenceFd > 0)
    {
        ALOGV(">>>close outbufAcquireFenceFd:%d", list->outbufAcquireFenceFd);
        close(list->outbufAcquireFenceFd);
        list->outbufAcquireFenceFd = -1;
    }

}
//static PFNEGLGETRENDERBUFFERANDROIDPROC _eglGetRenderBufferANDROID;
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
//static PFNEGLRENDERBUFFERMODIFYEDANDROIDPROC _eglRenderBufferModifiedANDROID;
#endif


/*****************************************************************************/

#if hwcDEBUG
static void
_Dump(
    hwc_display_contents_1_t* list
)
{
    size_t i, j;

    for (i = 0; list && (i < (list->numHwLayers - 1)); i++)
    {
        hwc_layer_1_t const * l = &list->hwLayers[i];

        if (l->flags & HWC_SKIP_LAYER)
        {
            LOGD("layer %p skipped", l);
        }
        else
        {
            LOGD("layer=%p, "
                 "layer name=%s, "
                 "type=%d, "
                 "hints=%08x, "
                 "flags=%08x, "
                 "handle=%p, "
                 "tr=%02x, "
                 "blend=%04x, "
                 "{%d,%d,%d,%d}, "
                 "{%d,%d,%d,%d}",
                 l,
                 l->LayerName,
                 l->compositionType,
                 l->hints,
                 l->flags,
                 l->handle,
                 l->transform,
                 l->blending,
                 l->sourceCrop.left,
                 l->sourceCrop.top,
                 l->sourceCrop.right,
                 l->sourceCrop.bottom,
                 l->displayFrame.left,
                 l->displayFrame.top,
                 l->displayFrame.right,
                 l->displayFrame.bottom);

            for (j = 0; j < l->visibleRegionScreen.numRects; j++)
            {
                LOGD("\trect%d: {%d,%d,%d,%d}", j,
                     l->visibleRegionScreen.rects[j].left,
                     l->visibleRegionScreen.rects[j].top,
                     l->visibleRegionScreen.rects[j].right,
                     l->visibleRegionScreen.rects[j].bottom);
            }
        }
    }
}
#endif

void sync_fbinfo_fence(struct rk_fb_win_cfg_data* fb_info)
{
    struct rk_fb_win_cfg_data fbinfo;
    for(int i = 0;i<4;i++)
    {
        for(int j=0;j<4;j++)
        {
            if(fb_info->win_par[i].area_par[j].ion_fd || fb_info->win_par[i].area_par[j].phy_addr)
            {
                if(fb_info->win_par[i].area_par[j].acq_fence_fd > -1)
                {
                    sync_wait(fb_info->win_par[i].area_par[j].acq_fence_fd,500);
                    fb_info->win_par[i].area_par[j].acq_fence_fd = -1;
                    if(is_out_log()) ALOGD("Sync fbinfo fence i=%d",i);
                }
            }
        }
    }
    return;
}

int hwc_reset_fb_info(struct rk_fb_win_cfg_data *fb_info, hwcContext * context)
{
    bool interleaveForVop = false;
    float hfactor;
    int scale = 1;
    int ionFd, phy_addr;
    int xact, yact, xsize, ysize, yvir, format;
    int relxres, relyres, fps;

    if (!context && context->Is_noi)
        return -EINVAL;

#if 1
    for (int i = 0; i < 4; i++) {
        for (int j= 0; j < 4; j++) {
            xact = fb_info->win_par[i].area_par[j].xact;
            yact = fb_info->win_par[i].area_par[j].yact;
            yvir = fb_info->win_par[i].area_par[j].yvir;
            xsize = fb_info->win_par[i].area_par[j].xsize;
            ysize = fb_info->win_par[i].area_par[j].ysize;
            format = fb_info->win_par[i].area_par[j].data_format;
            ionFd = fb_info->win_par[i].area_par[j].ion_fd;
            phy_addr = fb_info->win_par[i].area_par[j].phy_addr;
            hfactor = (float)ysize / (float)yact;
            fps = 1e9 / context->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period;
            relxres = context->dpyAttr[HWC_DISPLAY_PRIMARY].relxres;
            relyres = context->dpyAttr[HWC_DISPLAY_PRIMARY].relyres;

            interleaveForVop = ionFd || phy_addr;
            interleaveForVop = interleaveForVop && is_vop_yuv(format);            
            interleaveForVop = interleaveForVop && (xact >= MaxIForVop || yact >= MaxIForVop);            
            interleaveForVop = interleaveForVop && (hfactor <= 0.95);            
            interleaveForVop = interleaveForVop &&  fps >= 50 && relxres >= 3840;

            if (is_out_log())
                ALOGD("[%d,%d]=>[%d,%d][%d,%d,%d]", xact,yact,xsize,ysize,relxres,relyres,fps);
            if (is_out_log() && (yvir % 2) && interleaveForVop)
                ALOGW("We need interleave for vop but yvir is not align to 2");
            interleaveForVop = interleaveForVop && (yvir % 2 == 0);
            if (interleaveForVop) {
                    fb_info->win_par[i].area_par[j].xvir *= 2;
                    fb_info->win_par[i].area_par[j].yvir /= 2;
                    fb_info->win_par[i].area_par[j].yact /= 2;
            }
        }
    }
#else
    scale = hwc_get_int_property("sys.wzq.test", "1");
    for (int i = 0; i < 4; i++) {
        for (int j= 0; j < 4; j++) {
            if(fb_info->win_par[i].area_par[j].ion_fd ||
                                        fb_info->win_par[i].area_par[j].phy_addr) {
                    fb_info->win_par[i].area_par[j].xact /= scale;
                    fb_info->win_par[i].area_par[j].yact /= scale;
                    fb_info->win_par[i].area_par[j].xsize /= scale;
                    fb_info->win_par[i].area_par[j].ysize /= scale;
            }
        }
    }
#endif
    return 0;
}

#if hwcDumpSurface
static void
_DumpSurface(
    hwc_display_contents_1_t* list
)
{
    size_t i;
    static int DumpSurfaceCount = 0;

    char pro_value[PROPERTY_VALUE_MAX];
    property_get("sys.dump", pro_value, 0);
    //LOGI(" sys.dump value :%s",pro_value);
    if (!strcmp(pro_value, "true"))
    {
        for (i = 0; list && (i < (list->numHwLayers - 1)); i++)
        {
            hwc_layer_1_t const * l = &list->hwLayers[i];

            if (l->flags & HWC_SKIP_LAYER)
            {
                LOGI("layer %p skipped", l);
            }
            else
            {
                struct private_handle_t * handle_pre = (struct private_handle_t *) l->handle;
                int32_t SrcStride ;
                int div = 1;
                FILE * pfile = NULL;
                char layername[100] ;


                if (handle_pre == NULL)
                    continue;
                if(handle_pre->format == HAL_PIXEL_FORMAT_YCrCb_NV12 )
                {
                    SrcStride = 3;
                    div = 2;
                }
                else
                    SrcStride = android::bytesPerPixel(handle_pre->format);
                memset(layername, 0, sizeof(layername));
                system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
                //mkdir( "/data/dump/",777);
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                sprintf(layername, "/data/dump/dmlayer%d_%d_%d_%d.bin", DumpSurfaceCount, handle_pre->stride, handle_pre->height, SrcStride);
#else
                sprintf(layername, "/data/dump/dmlayer%d_%d_%d_%d.bin", DumpSurfaceCount, handle_pre->width, handle_pre->height, SrcStride);
#endif

                DumpSurfaceCount ++;
                pfile = fopen(layername, "wb");
                if (pfile)
                {
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                    fwrite((const void *)handle_pre->base, (size_t)(SrcStride * handle_pre->stride*handle_pre->height/div), 1, pfile);
#else
                    fwrite((const void *)handle_pre->iBase, (size_t)(SrcStride * handle_pre->width*handle_pre->height/div), 1, pfile);

#endif
                    fclose(pfile);
                    LOGI(" dump surface layername %s,w:%d,h:%d,formatsize :%d", layername, handle_pre->width, handle_pre->height, SrcStride);
                }
            }
        }

    }
    property_set("sys.dump", "false");


}
#endif

void
hwcDumpArea(
    IN hwcArea * Area
)
{
    hwcArea * area = Area;

    while (area != NULL)
    {
        char buf[128];
        char digit[8];
        bool first = true;

        sprintf(buf,
                "Area[%d,%d,%d,%d] owners=%08x:",
                area->rect.left,
                area->rect.top,
                area->rect.right,
                area->rect.bottom,
                area->owners);

        for (int i = 0; i < 32; i++)
        {
            /* Build decimal layer indices. */
            if (area->owners & (1U << i))
            {
                if (first)
                {
                    sprintf(digit, " %d", i);
                    strcat(buf, digit);
                    first = false;
                }
                else
                {
                    sprintf(digit, ",%d", i);
                    strcat(buf, digit);
                }
            }

            if ((unsigned int)area->owners < (1U << i))
            {
                break;
            }
        }

        LOGD("%s", buf);

        /* Advance to next area. */
        area = area->next;
    }
}

uint32_t hwc_get_vsync_period_from_fb_fps(int fbindex)
{
    const char node[] = "/sys/class/graphics/fb%u/fps";
    char nodeName[100] = {0};
    char value[100] = {0};
    int fps = 0;
    int fbFd = -1;
    int ret = 0;

    snprintf(nodeName, 64, node, fbindex);

    ALOGD("nodeName=%s", nodeName);
    fbFd = open(nodeName, O_RDONLY);
    if(fbFd > -1) {
        ret = read(fbFd, value, 80);
        if(ret <= 0) {
            ALOGE("fb%d/fps read fail %s", fbindex, strerror(errno));
        } else {
            sscanf(value, "fps:%d", &fps);
            ALOGI("fb%d/fps fps is:%d", fbindex, fps);
        }
        close(fbFd);
    }

    return (uint32_t)(1000000000 / fps);
}

int hwc_get_layer_area_info(hwc_layer_1_t * layer,hwcRECT *srcrect,hwcRECT *dstrect)
{

    hwc_region_t * Region = &layer->visibleRegionScreen;
    hwc_rect_t * SrcRect = &layer->sourceCrop;
    hwc_rect_t * DstRect = &layer->displayFrame;
    hwc_rect_t const * rects = Region->rects;
    hwc_rect_t  rect_merge;
    int left_min =0; 
    int top_min =0;
    int right_max =0;
    int bottom_max=0;

    struct private_handle_t*  handle = (struct private_handle_t*)layer->handle;

    if (!handle)
    {
        ALOGE("handle=NULL,name=%s",layer->LayerName);
        return -1;
    }

    if(rects)
    {
         left_min = rects[0].left; 
         top_min  = rects[0].top;
         right_max  = rects[0].right;
         bottom_max = rects[0].bottom;
    }
    for (unsigned int r = 0; r < (unsigned int) Region->numRects ; r++)
    {
        int r_left;
        int r_top;
        int r_right;
        int r_bottom;
       
        r_left   = hwcMAX(DstRect->left,   rects[r].left);
        left_min = hwcMIN(r_left,left_min);
        r_top    = hwcMAX(DstRect->top,    rects[r].top);
        top_min  = hwcMIN(r_top,top_min);
        r_right    = hwcMIN(DstRect->right,  rects[r].right);
        right_max  = hwcMAX(r_right,right_max);
        r_bottom = hwcMIN(DstRect->bottom, rects[r].bottom);
        bottom_max  = hwcMAX(r_bottom,bottom_max);
    }
    rect_merge.left = left_min;
    rect_merge.top = top_min;
    rect_merge.right = right_max;
    rect_merge.bottom = bottom_max;

    dstrect->left   = hwcMAX(DstRect->left,   rect_merge.left);
    dstrect->top    = hwcMAX(DstRect->top,    rect_merge.top);
    dstrect->right  = hwcMIN(DstRect->right,  rect_merge.right);
    dstrect->bottom = hwcMIN(DstRect->bottom, rect_merge.bottom);

    return 0;
}

// do base check ,all policy can not support except gpu
int try_prepare_first(hwcContext * ctx,hwc_display_contents_1_t *list)
{
    int hwc_en; 
    int static cnt = 0;
    unsigned int lpersent = 0;
    unsigned int tpersent = 0;
    unsigned int rpersent = 0;
    unsigned int bpersent = 0;

    char new_valuep[PROPERTY_VALUE_MAX];
    
    ctx->videoCnt = 0;
    ctx->Is_video = false;
    ctx->Is_Lvideo = false;
    ctx->Is_Secure = false;
    ctx->special_app = false;
    ctx->hasPlaneAlpha = false;
    is_debug_log();    
    
    if(cnt < 2)
    {
        cnt ++;
        if(is_out_log())
            ALOGW("boot cnt =%d ",cnt);
        return -1;
    }
    property_get("persist.sys.overscan.main", new_valuep, "false");

    sscanf(new_valuep,"overscan %d,%d,%d,%d",&lpersent,&tpersent,&rpersent,&bpersent);

    if(lpersent != 100 || tpersent != 100 )  
        ctx->Is_OverscanEn = true;
    else
        ctx->Is_OverscanEn = false;

    for (unsigned int i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = NULL;
        
        if(layer)
        {
            layer->dospecialflag = 0;
            handle = (struct private_handle_t *)layer->handle;
            if(is_out_log())
            {
                ALOGD("layer[%d],name=%s,hanlde=%x,tra=%d,flag=%d",i,layer->LayerName,handle,layer->transform,layer->flags);
                if(handle)
                {
                    ALOGD("layer[%d],fmt=%d,usage=%x,protect=%x",i,handle->format,handle->usage,GRALLOC_USAGE_PROTECTED);                   
                }
                
            }           
            if (handle && handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12)
            {
                ctx->Is_video = true; 
                if(handle->width > 1440 || handle->height > 1440)
                    ctx->Is_Lvideo = true;;
                if(handle->usage & GRALLOC_USAGE_PROTECTED )
                    ctx->Is_Secure = true;
            }
            if (handle && handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
            {
                ctx->Is_video = true;
                if(handle->width > 1440 || handle->height > 1440)
                    ctx->Is_Lvideo = true;;
                if(handle->usage & GRALLOC_USAGE_PROTECTED)
                    ctx->Is_Secure = true;
            }
            if(handle && (handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12 || handle->format == HAL_PIXEL_FORMAT_YCrCb_420_SP))
            {
                ctx->videoCnt ++;
            }
            #if IQIY_SPECIAL_PROCESS
            if(1==i && strstr(layer->LayerName,"com.qiyi.video/org.qiyi.android.video.MainActivity"))
            {
                ctx->special_app = true;
                int temp = hwc_get_int_property("sys.hwc.special","1");
                if(!temp)
                   ctx->special_app = false;
            } 
            #endif
	    #if !defined(TARGET_BOARD_PLATFORM_RK3328) 
            if(handle && handle->type && !ctx->iommuEn)
            {
                if(is_out_log())
                    ALOGW("kernel iommu disable,but use mmu buffer,so hwc can not support!");
                return -1;
            }
	    #endif
            if(handle && handle->format == HAL_PIXEL_FORMAT_YV12 )
            {   
                if(is_out_log())
                    ALOGW("line=%d",__LINE__);
                return -1;
            }
            if(strstr(layer->LayerName,"android.tests.devicesetup"))
            {
                if(is_out_log())
                    ALOGW("line=%d",__LINE__);
                return -1;
            }
            if(strstr(layer->LayerName,"android.app.cts.uiautomation"))
            {
                if(is_out_log())
                    ALOGW("line=%d",__LINE__);            
                return -1;
            }
            if(strstr(layer->LayerName,"com.a") && strstr(layer->LayerName,"ndrt") &&
                strstr(layer->LayerName,"oid.c") && strstr(layer->LayerName,"s.view"))
            {
                if(is_out_log())
                    ALOGW("line=%d",__LINE__);            
                return -1;
            }
	    if (layer && (layer->blending & 0xffff) == 0x105 &&
						(layer->blending >> 16) < 255)
		ctx->hasPlaneAlpha = true;
        }
                 
    }    
    
    if((list->numHwLayers - 1) <= 0 || list->numHwLayers >  RGA_REL_FENCE_NUM)  // vop not support
    {
        if(is_out_log())
            ALOGW("line=%d,num=%d",__LINE__,list->numHwLayers);
    
        return -1;
    }
    hwc_en = hwc_get_int_property("sys.hwc.enable","0");
    if(!hwc_en)
    {
        if(is_out_log())
            ALOGW("line=%d",__LINE__);            
    
        return -1;
    }
    //hwc_sync(list);  // video must sync,since UI will be used to detected

    return 0;
}

int is_need_skip_this_policy(void*ctx)
{
    hwcContext * context = (hwcContext *)ctx;
    bool IsBox = false;
    if(context->IsRk3128 && context->Is_OverscanEn && context->mScreenChanged)
    {
        #ifdef RK312X_BOX
        IsBox = true;
        #endif
        if(IsBox)
            return 1;
        else
            return 0;
    }
    return 0;
}
int try_hwc_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{
    int scale_cnt = 0;
    bool forceSkip = false;
    bool forceUiDetect = false;
    hwcContext * context = (hwcContext *)ctx;

#ifdef USE_X86
    if(getHdmiMode() == 1 && list->numHwLayers - 1 > 1 && !context->Is_video)
    {
        return -1;
    }
#endif
    if(is_need_skip_this_policy(ctx))
        return -1;
    forceSkip = context->IsRk3126;
    if(context->IsRk3188 && ONLY_USE_ONE_VOP == 1)
        forceSkip = true;

    if(getHdmiMode() == 1 && forceSkip)
    {
        if(is_out_log())
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }

    if((list->numHwLayers - 1) > VOP_WIN_NUM && !context->special_app)  // vop not support
    {
        if(is_out_log())
            ALOGD("line=%d,num=%d,special=%d",__LINE__,list->numHwLayers - 1,context->special_app);
        return -1;
    }
    
    for (unsigned int i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        float hfactor = 1.0;
        float vfactor = 1.0; 
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        
        if (layer->flags & HWC_SKIP_LAYER
            || layer->transform != 0
            || layer->handle == NULL
          )
        {
            if(is_out_log())
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }      
        if(LayerZoneCheck(layer,context) != 0)
        {
            if(is_out_log())
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }
        hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
            / (layer->displayFrame.right - layer->displayFrame.left);

        vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
            / (layer->displayFrame.bottom - layer->displayFrame.top);
        if((hfactor != 1.0 || vfactor != 1.0) && !(context->IsRk322x || context->IsRk3328))
        {
            scale_cnt ++;
            if(i>0)
            {
                context->win_swap = 1;
                ALOGV("i=%d,swap",i);
            }
        }

        if(layer->displayStereo && !layer->alreadyStereo)
        {
            if(is_out_log())
                ALOGD("line=%d,layer%d is stereo",__LINE__,i);
            return -1;
        }

        forceUiDetect = (getHdmiMode() == 1 && (context->IsRk3126 || context->IsRk3128));
        // vop has only one win support scale,or vop sacle donwe need more BW lead to vop shake
        if((scale_cnt > 1 || (context->vop_mbshake && vfactor > 1.0)) &&
							!(context->IsRk322x || context->IsRk3328))
        {
            context->win_swap = 0;
            if(is_out_log())
                ALOGD("line=%d,num=%d,is 322x=%d",__LINE__,list->numHwLayers - 1,context->IsRk322x);
            return -1;
        }
        if(i == 0)
            layer->compositionType = HWC_TOWIN0;
        else
        {
            #if VIDEO_WIN1_UI_DISABLE
            if(/*context->vop_mbshake && */context->Is_video || (context->special_app && i == 1 &&
                list->numHwLayers == 3) || forceUiDetect)
            {
                int ret = DetectValidData(context,(int *)handle->base,handle->width,handle->height); 
                if(ret) // ui need display
                {
                    return -1;
                }  
            }
            #endif

            layer->compositionType = HWC_LCDC;
        }    
        
    }
    ALOGV("hwc-prepare use HWC_VOP policy");
    context->composer_mode = HWC_VOP;
    return 0;
}

// 0\1\2\3\..\ ->rga->buffer->win0
int try_hwc_rga_policy(void * ctx,hwc_display_contents_1_t *list)
{
    float hfactor = 1.0;
    float vfactor = 1.0;
    bool isYuvMod = false;
    int  pixelSize  = 0;
    unsigned int  i ;
    hwcContext * context = (hwcContext *)ctx;

    if(context->engine_fd <= 0)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA not exit");
        return -1;
    }
    // RGA_POLICY_MAX_SIZE
    if(context->engine_err_cnt > RGA_ALLOW_MAX_ERR)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA err_cnt =%d,return to other policy",context->engine_err_cnt);
        return -1;
    }
    if(context->IsRk3328 || context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
    if(context->hasPlaneAlpha)
    {
	    if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
    for (  i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
        {
            if(is_out_log())
                ALOGD("rga policy skip,flag=%x,hanlde=%x",layer->flags,handle);
            return -1;  
        }
        if(layer->transform == 5 || layer->transform == 6)
        {
            return -1;
        }
        if(handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12)  // video use other policy
        {
            if(is_out_log())
                ALOGD("format is nv12,line=%d",__LINE__);
            return -1;
        }    
    }

    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                  / (layer->displayFrame.right - layer->displayFrame.left);

        vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                  / (layer->displayFrame.bottom - layer->displayFrame.top);
        if(hfactor != 1.0f 
           || vfactor != 1.0f
           || layer->transform != 0
          )   // because rga scale & transform  too slowly,so return to opengl        
        {
            if(is_out_log())
                ALOGD("RGA_policy not support [%f,%f,%d]",hfactor,vfactor,layer->transform );
            return -1;
        }
        pixelSize += ((layer->sourceCrop.bottom - layer->sourceCrop.top) * \
                        (layer->sourceCrop.right - layer->sourceCrop.left));
        if(pixelSize > RGA_POLICY_MAX_SIZE )  // pixel too large,RGA done use more time
        {
            if(is_out_log())
                ALOGD("pielsize=%d,max_size=%d",pixelSize ,RGA_POLICY_MAX_SIZE);
            return -1;
        }    

        layer->compositionType = HWC_BLITTER;
        
    }
    context->composer_mode = HWC_RGA;
    ALOGV("hwc-prepare use HWC_RGA policy");

    return 0;
}

int try_hwc_rga_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{

#if 1
    float hfactor = 1;
    float vfactor = 1;
    int  pixelSize  = 0;
    int  yuvPixelSize  = 0;
    unsigned int i ;
    hwcContext * context = (hwcContext *)ctx;
    int yuv_cnt = 0;

#if ONLY_USE_ONE_VOP
    if(getHdmiMode() == 1)
    {
        if(is_out_log())
            ALOGD("exit line=%d,is hdmi",__LINE__);
        return -1;
    }
#endif
    if(is_need_skip_this_policy(ctx))
        return -1;
    if(context->engine_fd <= 0)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA not exit");
        return -1;
    }

    if(list->numHwLayers - 1 < 3 || context->engine_err_cnt > RGA_ALLOW_MAX_ERR)//optimazation
    {
        if(is_out_log())
            ALOGD("line=%d,num=%d,err_cnt=%d",__LINE__,list->numHwLayers - 1,context->engine_err_cnt);
        return -1;
    }
    if(context->IsRk3328 || context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }

    if(context->isStereo)
    {
        if(is_out_log())
            ALOGD("line=%d,stereo=%d",__LINE__,context->isStereo);
        return -1;
    }

    for ( i = 0; i < (list->numHwLayers - 2); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
        {
            if(is_out_log())
            {
                ALOGD("policy skip,flag=%x,hanlde=%x,line=%d,name=%s",layer->flags,handle,__LINE__,layer->LayerName);
            }
            return -1;
        }
        if((layer->transform == 5 || layer->transform == 6))
        {
            if(is_out_log())
            {
                ALOGD("policy skip,line=%d,trfm=%d",__LINE__,layer->transform);
            }
            return -1;
        }
        if((handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12 || handle->format == HAL_PIXEL_FORMAT_YCrCb_420_SP))
        {
            yuv_cnt ++;
        }
        yuvPixelSize += handle->width * handle->height;
    }
    ALOGV("yuvPixelSize=%d",yuvPixelSize);
    if( yuvPixelSize > 1024001 || yuv_cnt != 2)  // 1280 x 800 + 1 pixels
    {
        if(is_out_log())
           ALOGD("line=%d,yuvPixelSize=%d,yuv_cnt=%d",__LINE__,yuvPixelSize,yuv_cnt);
        return -1;
    }

    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;

        if(i == list->numHwLayers - 2)
        {
            if(handle == NULL)
            {
                if(is_out_log())
                   ALOGD("line=%d,handle=%d",__LINE__,handle);
                return -1;
            }
            float hfactor = 1.0;
            float vfactor = 1.0;
            hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                      / (layer->displayFrame.right - layer->displayFrame.left);

            vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                      / (layer->displayFrame.bottom - layer->displayFrame.top);
            if(hfactor != 1.0f || vfactor != 1.0f || layer->transform != 0)// wop has only one support scale
            {
                if(is_out_log())
                {
                    ALOGD("[%f,%f,%d],nmae=%s,line=%d",hfactor,vfactor,layer->transform,layer->LayerName,__LINE__);
                }
                return -1;
            }
        }
        if(i == list->numHwLayers - 2)
            layer->compositionType = HWC_LCDC;
        else
            layer->compositionType = HWC_BLITTER;
    }
    context->composer_mode = HWC_RGA_VOP;
    ALOGV("hwc-prepare use HWC_RGA_VOP policy");

    return 0;
#else
    return -1;
#endif
}

// video two layers ,one need transform, 0->rga_trfm->buffer->win0, 1->win1
int try_hwc_rga_trfm_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{

#if 1
    float hfactor = 1;
    float vfactor = 1;
    int  pixelSize  = 0;
    unsigned int i ;
    hwcContext * context = (hwcContext *)ctx;
    int yuv_cnt = 0;

    if(is_need_skip_this_policy(ctx))
        return -1;
   // RGA_POLICY_MAX_SIZE
#if ONLY_USE_ONE_VOP
    if(getHdmiMode() == 1)
    {
        if(is_out_log())
            ALOGD("exit line=%d,is hdmi",__LINE__);
        return -1;
    }
#endif
    if(context->engine_fd <= 0)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA not exit");
        return -1;
    }
    if(context->IsRk3328 || context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
    if((list->numHwLayers - 1) > VOP_WIN_NUM || context->engine_err_cnt > RGA_ALLOW_MAX_ERR)  // vop not support
    {
        if(is_out_log())
            ALOGD("line=%d,num=%d,err_cnt=%d",__LINE__,list->numHwLayers - 1,context->engine_err_cnt);
        return -1;
    }

    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
        {
            if(is_out_log())
            {   
                ALOGD("policy skip,flag=%x,hanlde=%x,line=%d,name=%s",layer->flags,handle,__LINE__,layer->LayerName);              
            }    
            return -1;  
        }
        if(i == 0 && (layer->transform == 5 || layer->transform == 6))
        {
            return -1;
        }
        if((handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12
            || handle->format == HAL_PIXEL_FORMAT_YCrCb_420_SP)
            &&(context->vop_mbshake || layer->transform != 0))  // video use other policy
        {
            yuv_cnt ++;
        }
        if(layer->displayStereo && !layer->alreadyStereo)
        {
            if(is_out_log())
                ALOGD("line=%d,layer%d is stereo",__LINE__,i);
            return -1;
        }
    }    
    ALOGV("yuv_cnt=%d",yuv_cnt); 
    if( yuv_cnt != 1 )  // 0 or > 1 yuv skip
    {
        if(is_out_log())
           ALOGD("yuv_cnt=%d,line=%d",yuv_cnt,__LINE__);

        return -1;
    }    
        
    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;

        if(0 == i && LayerZoneCheck(layer,context) != 0)
        {
            if(is_out_log())
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }

	if(i>0)
        {
            float hfactor = 1.0;
            float vfactor = 1.0;
            hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                      / (layer->displayFrame.right - layer->displayFrame.left);

            vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                      / (layer->displayFrame.bottom - layer->displayFrame.top);
            if(hfactor != 1.0f 
                || vfactor != 1.0f
                || layer->transform != 0
            )   // wop has only one support scale        
            {
                if(is_out_log())
                    ALOGD("[%f,%f,%d],nmae=%s,line=%d",hfactor,vfactor,layer->transform,layer->LayerName,__LINE__);
                return -1;
            }
            #if VIDEO_WIN1_UI_DISABLE
            if(context->vop_mbshake && context->Is_video)
            {
                int ret = DetectValidData(context,(int *)handle->base,handle->width,handle->height); 
                if(ret) // ui need display
                {
                    if(is_out_log())
                        ALOGD("detect exit line=%d",__LINE__);
                    return -1;
                }  
            }    
            #endif
            
        }
        if(i == 0)
            layer->compositionType = HWC_BLITTER;  
        else
            layer->compositionType = HWC_LCDC;
    }
    context->composer_mode = HWC_RGA_TRSM_VOP;
    ALOGV("hwc-prepare use HWC_RGA_TRSM_VOP policy");

    return 0;
#else
    return -1;
#endif
}

// video more three layers , 0->rga_trfm->buffer->win0, 1\2\3\..\->gpu->FB->win1
int try_hwc_rga_trfm_gpu_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{
#if 1
    float hfactor = 1;
    float vfactor = 1;
    bool isYuvMod = false;
    int  pixelSize  = 0;
    unsigned int i ;
    hwcContext * context = (hwcContext *)ctx;

#if ONLY_USE_ONE_VOP
    if(getHdmiMode() == 1)
    {
        if(is_out_log())
            ALOGD("exit line=%d,is hdmi",__LINE__);
        return -1;
    }
#endif

    if(is_need_skip_this_policy(ctx))
        return -1;
    if(context->engine_fd <= 0)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA not exit");
        return -1;
    }

    hwc_rect_t * drest = NULL;
    hwc_layer_1_t * layer = &list->hwLayers[0];
    struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
    if(layer->transform == 5 || layer->transform == 6)
    {
        return -1;
    }
    if(context->videoCnt == 2)//car recorder
    {
        return -1;
    }
    if(context->engine_err_cnt > RGA_ALLOW_MAX_ERR)
    {
        if(is_out_log())
           ALOGD("exit line=%d,err_cnt=%d",__LINE__,context->engine_err_cnt);
        return -1;
    }
    if(context->IsRk3328 || context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
    if(context->isStereo)
    {
        if(is_out_log())
            ALOGD("line=%d,is stereo = %d",__LINE__,context->isStereo);
        return -1;
    }
    #if VIDEO_WIN1_UI_DISABLE
    if(context->vop_mbshake)
    {
        if(is_out_log())
            ALOGD("exit line=%d",__LINE__);

        return -1;  
    }
    #endif
    if( (list->numHwLayers - 1) > 2 && context->Is_Lvideo && !context->Is_Secure)
    {
        if(is_out_log())
            ALOGD("exit line=%d,num=%d,Is_Lvideo=%d,Is_Secure=%d",__LINE__,list->numHwLayers - 1, context->Is_Lvideo ,context->Is_Secure);
        return -1;  
    }
    if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
    {
        if(is_out_log())
          ALOGD("policy skip,flag=%x,hanlde=%x,line=%d",layer->flags,handle,__LINE__);
        return -1;  
    }
    if(handle->format != HAL_PIXEL_FORMAT_YCrCb_NV12 || layer->transform == 0)  // video use other policy
    {
        float vfactor = 1.0;
            vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
            / (layer->displayFrame.bottom - layer->displayFrame.top);

        if(context->Is_video 
            && (handle->usage & GRALLOC_USAGE_PROTECTED)
            && vfactor > 2.4f)
        {
            if(is_out_log())
                ALOGD("Video use RGA,since secure video can't back GPU,fmt=%d,[%f]",handle->format,vfactor); 
        }  
        else
        {
            if(is_out_log())
                ALOGD("exit line=%d,fmt=%x,nmae=%s",__LINE__,handle->format,layer->LayerName);
            return -1;                
        }        
    }   
    
    if((layer->sourceCrop.left % 4) && !(handle->usage & GRALLOC_USAGE_PROTECTED))
    {
        if(is_out_log())
            ALOGD("exit line=%d,left=%d",__LINE__,layer->sourceCrop.left);

        return -1; // yuv rga do  must 4 align
    }
    
    layer->compositionType = HWC_BLITTER;
    handle_gpu_nodraw_optimazation(context,list,0,HWC_RGA_TRSM_GPU_VOP);
    
    context->composer_mode = HWC_RGA_TRSM_GPU_VOP;
    ALOGV("hwc-prepare use HHWC_RGA_TRSM_GPU_VOP policy");

    return 0;
#else
    return -1;
#endif
}
    
// > 2 layers, 0->win0 ,1\2\3->RGA->FB->win1
int try_hwc_vop_rga_policy(void * ctx,hwc_display_contents_1_t *list)
{

#if 0
    float hfactor = 1;
    float vfactor = 1;
    bool isYuvMod = false;
    unsigned int i ;
   // RGA_POLICY_MAX_SIZE
    hwcContext * context = (hwcContext *)ctx;
    int  pixelSize  = 0;

    if(context->engine_fd <= 0)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA not exit");
        return -1;
    }
   // RGA_POLICY_MAX_SIZE
    if(context->engine_err_cnt > RGA_ALLOW_MAX_ERR)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA err_cnt =%d,return to other policy",context->engine_err_cnt);
        return -1;
    }    

    if(getHdmiMode() == 1 || !context->Is_video)
        return -1;

    for (  i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
        {
            if(is_out_log())
                ALOGD("rga policy skip,flag=%x,hanlde=%x,line=%d",layer->flags,handle,__LINE__);
            return -1;  
        }
        if(//handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12 
           //|| 
           layer->transform != 0
          )  // video use other policy
        {
            return -1;
        }    
    }
    if(context->IsRk3328 || context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if(i == 0)
        {
            if(context->vop_mbshake )
            {
                float vfactor = 1.0;
                vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                  / (layer->displayFrame.bottom - layer->displayFrame.top);
                if(vfactor > 1.0f  )   //   vop sacle donwe need more BW lead to vop shake      
                {
                    if(is_out_log())
                        ALOGD("vop_rga policy line=%d,vfactor=%f",__LINE__,vfactor);
                    return -1;
                }
            }    
            if(LayerZoneCheck(layer,context) != 0)
                return -1;
            else
                layer->compositionType = HWC_TOWIN0;
        }    
        else
        {
            hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                      / (layer->displayFrame.right - layer->displayFrame.left);

            vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                      / (layer->displayFrame.bottom - layer->displayFrame.top);
            if(hfactor != 1.0f 
               || vfactor != 1.0f
               || layer->transform != 0
              )   // because rga scale & transform  too slowly,so return to opengl        
            {
                if(is_out_log())
                    ALOGD("RGA_policy not support [%f,%f,%d],line=%d",hfactor,vfactor,layer->transform,__LINE__);
                return -1;
            }
            pixelSize += ((layer->sourceCrop.bottom - layer->sourceCrop.top) * \
                            (layer->sourceCrop.right - layer->sourceCrop.left));
            if(pixelSize > RGA_POLICY_MAX_SIZE )  // pixel too large,RGA done use more time
            {
                if(is_out_log())
                    ALOGD("pielsize=%d,max_size=%d,line=%d",pixelSize ,RGA_POLICY_MAX_SIZE,__LINE__);
                return -1;
            }    

            layer->compositionType = HWC_BLITTER;
        }  
    }
    context->composer_mode = HWC_VOP_RGA;

    ALOGV("hwc-prepare use HWC_VOP_RGA policy");

    return 0;
#else
    return -1;
#endif
}

// > 2 layers, 0->win0 ,1\2\3->GPU->FB->win1
int try_hwc_vop_gpu_policy(void * ctx,hwc_display_contents_1_t *list)
{
    float hfactor = 1;
    float vfactor = 1;
    bool isYuvMod = false;
    bool forceSkip = false;
    hwc_rect_t * drest = NULL;
    unsigned int i ;

    hwcContext * context = (hwcContext *)ctx;
    if(is_need_skip_this_policy(ctx))
        return -1;
#ifdef USE_X86
    if(getHdmiMode() == 1)
    {
        if(is_out_log())
            ALOGD("exit line=%d,is hdmi",__LINE__);
        return -1;
    }
#endif
    forceSkip = context->IsRk3126;

    if(context->IsRk3188 && ONLY_USE_ONE_VOP == 1)
        forceSkip = true;

    if (is_not_suit_mix_policy(list)) {
        if(is_out_log())
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }

    hwc_layer_1_t * layer = &list->hwLayers[0];
    struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
    if ((layer->flags & HWC_SKIP_LAYER) 
        || handle == NULL
        || layer->transform != 0
        // ||(list->numHwLayers - 1)>4
        ||((list->numHwLayers - 1)<3 && !(context->Is_video && (context->IsRk322x || context->IsRk3328))))
    {
        if(is_out_log())
          ALOGD("policy skip,flag=%x,hanlde=%x,tra=%d,num=%d,line=%d",
                    layer->flags,handle,layer->transform,list->numHwLayers,__LINE__);
        return -1;  
    }

    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if (layer->flags & HWC_SKIP_LAYER && !((context->IsRk322x || context->IsRk3328) && i > 0))
        {
            if(is_out_log())
                ALOGD("vop_gpu skip,flag=%x,hanlde=%x",layer->flags);
            return -1;
        }

        if(i == 0)
        {
            if((context->vop_mbshake || context->Is_video)&& !(handle->usage & GRALLOC_USAGE_PROTECTED) &&
								!(context->IsRk322x || context->IsRk3328))
            {
                float vfactor = 1.0;
                vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                  / (layer->displayFrame.bottom - layer->displayFrame.top);
                if(vfactor > 1.0f  )   //   vop sacle donwe need more BW lead to vop shake      
                {
                    if(is_out_log())
                        ALOGD("exit line=%d",__LINE__);
                    return -1;
                }
            }
            if(layer->displayStereo && !layer->alreadyStereo)
            {
                if(is_out_log())
                    ALOGD("line=%d,layer%d is stereo",__LINE__,i);
                return -1;
            }
            if(LayerZoneCheck(layer,context) != 0)
            {
                if(is_out_log())
                    ALOGD("exit line=%d",__LINE__);
                return -1;
            }    
            else
                layer->compositionType = HWC_TOWIN0;
        }    
        else
            layer->compositionType = HWC_FRAMEBUFFER;
    }

    handle_gpu_nodraw_optimazation(context,list,0,HWC_VOP_GPU);

    context->composer_mode = HWC_VOP_GPU;
    ALOGV("hwc-prepare use HWC_VOP_GPU policy");

    return 0;
}


// > 5 layers, 0\1 ->rga->buffer->win0,2\3\4\..\->gpu->FB->win1
// if 0\1 address and displayzone dont change, buffer->win0,2\3\4\..\->gpu->FB->win1
int try_hwc_nodraw_gpu_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{
#if 0
    unsigned int i;
    hwcContext * context = (hwcContext *)ctx;

    if(getHdmiMode() == 1)
        return -1;

    if(!(context->NoDrMger.composer_mode_pre == HWC_RGA_GPU_VOP
            ||context->NoDrMger.composer_mode_pre == HWC_NODRAW_GPU_VOP)
       )
    {    
        ALOGV("Not in RGA_GPU_VOP or VOP_GPU mode premoede=%d",
            context->NoDrMger.composer_mode_pre);
        return -1;
    }    

    if((list->numHwLayers - 1) < 5)  // too less
    {

        return -1;
    }

    if (is_not_suit_mix_policy(list)) {
        if(is_out_log())
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }
    
    for ( i = 0; i < 2 ; i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) 
            || handle == NULL
            || layer->transform != 0)
        {
            return -1;  
        }
        if(handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12)  // video use other policy
        {
            return -1;
        }    
    }
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if(i< 2)
            layer->compositionType = HWC_BLITTER;
        else
            layer->compositionType = HWC_FRAMEBUFFER;
        
    }

    for ( i = 0; i < 2 ; i++)
    {
        hwcRECT dstrect;
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        hwc_get_layer_area_info(layer,NULL,&dstrect);
        ALOGV("layer=%s,[%d,%d,%d,%d]",layer->LayerName,dstrect.left,dstrect.top,dstrect.right,dstrect.bottom);
        ALOGV("[%d] ori[%x,%d],cur[%x,%d]",
            i,context->NoDrMger.addr[i],context->NoDrMger.alpha[i],handle->base,layer->blending);
        if(context->NoDrMger.addr[i] != handle->base
            ||  context->NoDrMger.alpha[i] != layer->blending
          )
        {
            return -1;
        }
       
    }    
    context->composer_mode = HWC_NODRAW_GPU_VOP;    
    ALOGV("hwc-prepare use HWC_NODRAW_GPU_VOP policy");
    
    return 0;
#else
    return -1;
#endif
}

// > 5 layers, 0\1 ->rga->buffer->win0, 2\3\4\..\->gpu->FB->win1
int try_hwc_rga_gpu_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{
#if 0
    float hfactor = 1;
    float vfactor = 1;
    bool isYuvMod = false;
    unsigned int i ;
   // RGA_POLICY_MAX_SIZE
    hwcContext * context = (hwcContext *)ctx;

    if(context->engine_fd <= 0)
    {
        if(is_out_log())
            ALOGW("err !!!! RGA not exit");
        return -1;
    }
    if(getHdmiMode() == 1)
        return -1;
    if(context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
    if((list->numHwLayers - 1) < 5)  // too less
    {
        return -1;
    }
    
    for ( i = 0; i < 2 ; i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) 
            || handle == NULL
            || layer->transform != 0)
        {
            return -1;  
        }
        if(handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12)  // video use other policy
        {
            return -1;
        }    

        hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                  / (layer->displayFrame.right - layer->displayFrame.left);

        vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                  / (layer->displayFrame.bottom - layer->displayFrame.top);

        ALOGV("[%d]=%s,[%f,%f]",i,layer->LayerName,hfactor,vfactor);          
        
    }
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle =  (struct private_handle_t *)layer->handle;
        if(i< 2)
        {
            layer->compositionType = HWC_BLITTER;
            layer->dospecialflag = 1;
        }    
        else
            layer->compositionType = HWC_FRAMEBUFFER;
        
    }
    context->composer_mode = HWC_RGA_GPU_VOP;

    for ( i = 0; i < 2 ; i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle =  (struct private_handle_t *)layer->handle;
        context->NoDrMger.addr[i] = handle->base;
        context->NoDrMger.alpha[i] = layer->blending;
    }
    context->NoDrMger.uicnt = 2; 
    ALOGV("hwc-prepare use HWC_RGA_GPU_VOP policy");
    
    return 0;
#else
    return -1;
#endif
}

int try_hwc_gpu_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{

#if 0
    float hfactor = 1;
    float vfactor = 1;
    int  pixelSize  = 0;
    int  yuvPixelSize  = 0;
    unsigned int i ;
    hwcContext * context = (hwcContext *)ctx;
    int yuv_cnt = 0;
    if(context->IsRk322x || context->IsRk3126)
    {
        if(is_out_log())
            ALOGD("Hwc rga policy out,line=%d",__LINE__);
        return -1;
    }
#if ONLY_USE_ONE_VOP
    if(getHdmiMode() == 1)
    {
        if(is_out_log())
            ALOGD("exit line=%d,is hdmi",__LINE__);
        return -1;
    }
#endif
    if(list->numHwLayers - 1 < 3 || context->engine_err_cnt > RGA_ALLOW_MAX_ERR)//optimazation
    {
        if(is_out_log())
            ALOGD("line=%d,num=%d,err_cnt=%d",__LINE__,list->numHwLayers - 1,context->engine_err_cnt);
        return -1;
    }

    if (is_not_suit_mix_policy(list)) {
        if(is_out_log())
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }

    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;

        if(i == list->numHwLayers - 2)
        {
            if(handle == NULL)
            {
                if(is_out_log())
                   ALOGD("line=%d,handle=%d",__LINE__,handle);
                return -1;
            }
            float hfactor = 1.0;
            float vfactor = 1.0;
            hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                      / (layer->displayFrame.right - layer->displayFrame.left);

            vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                      / (layer->displayFrame.bottom - layer->displayFrame.top);
            if(hfactor != 1.0f || vfactor != 1.0f || layer->transform != 0)// wop has only one support scale
            {
                if(is_out_log())
                {
                    ALOGD("[%f,%f,%d],nmae=%s,line=%d",hfactor,vfactor,layer->transform,layer->LayerName,__LINE__);
                }
                return -1;
            }
            if(layer->displayStereo && !layer->alreadyStereo)
            {
                if(is_out_log())
                    ALOGD("line=%d,layer%d is stereo",__LINE__,i);
                return -1;
            }
        }
        if(i == list->numHwLayers - 2)
            layer->compositionType = HWC_LCDC;
        else
            layer->compositionType = HWC_FRAMEBUFFER;
    }
    context->win_swap = 1;
    context->composer_mode = HWC_GPU_VOP;
    ALOGV("hwc-prepare use HWC_GPU_VOP policy");

    return 0;
#else
    return -1;
#endif
}

int try_hwc_gpu_nodraw_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{
    int ret = 0;
    int yuv_cnt = 0;
    unsigned int i ;
    bool res = false;
    float hfactor = 1;
    float vfactor = 1;
    bool forceSkip =false;
    int  pixelSize  = 0;
    int  yuvPixelSize  = 0;
    int  topDimlayer = -1;
    int  relOverlayLayer = -1;
    hwc_rect_t * drest = NULL;
    int  lastOverlayLayer = -1;
    int  firstOverlayLayer = -1;
    hwc_layer_1_t * layer = NULL;
    struct private_handle_t * handle = NULL;
    hwcContext * context = (hwcContext *)ctx;

    forceSkip = context->IsRk3126;

    if(is_need_skip_this_policy(ctx))
        return -1;
    if(context->IsRk3188 && ONLY_USE_ONE_VOP == 1)
        forceSkip = true;

    if (is_not_suit_mix_policy(list)) {
        if(is_out_log())
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }

    if(getHdmiMode() == 1 && forceSkip)
    {
        if(is_out_log())
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }
    if(list->numHwLayers - 1 < 5)//optimazation
    {
        if(is_out_log() == 2)
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }

    layer = &list->hwLayers[0];
    hwc_region_t * Region = &layer->visibleRegionScreen;
    if(Region->numRects < 2 && false)
    {
        if(is_out_log() == 2)
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }
    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        layer = &list->hwLayers[i];
        if(!strcmp(layer->LayerName,"DimLayer"))
            topDimlayer = i;
        if(firstOverlayLayer == -1 && layer->visibleRegionScreen.numRects == 1)
            firstOverlayLayer = i;
    }
    for ( i = list->numHwLayers - 1; i > 0; i++)
    {
        layer = &list->hwLayers[i];
        if(lastOverlayLayer == -1 && is_suit_nodraw(layer,context))
        {
            lastOverlayLayer = i;
            break;
        }
    }
    if(topDimlayer == (int)list->numHwLayers - 2)
    {
        if(LayerZoneCheck(&list->hwLayers[topDimlayer+1],context))
        {
            if(is_out_log() == 2)
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }
        relOverlayLayer = topDimlayer + 1;
    }
    else if(topDimlayer > 0 && topDimlayer < (int)list->numHwLayers - 3)
    {
        if(LayerZoneCheck(&list->hwLayers[topDimlayer+1],context))
        {
            if(is_out_log() == 2)
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }
        for ( i = topDimlayer + 2; i < (list->numHwLayers - 1); i++)
        {
            layer = &list->hwLayers[i];
            if(is_displayframe_intersect(&list->hwLayers[topDimlayer+1],layer))
            {
                if(is_out_log() == 2)
                    ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
                return -1;
            }
        }
        relOverlayLayer = topDimlayer + 1;
    }
    else if(firstOverlayLayer == (int)list->numHwLayers - 2)
    {
        if(firstOverlayLayer > lastOverlayLayer)
        {
            if(is_out_log() == 2)
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }
        if(LayerZoneCheck(&list->hwLayers[firstOverlayLayer],context))
        {
            if(is_out_log() == 2)
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }
        relOverlayLayer = firstOverlayLayer;
    }
    else
    {
TryAgain:
        for ( i = firstOverlayLayer + 1; i < (list->numHwLayers - 1); i++)
        {
            if(firstOverlayLayer > lastOverlayLayer)
            {
                if(is_out_log() == 2)
                    ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
                return -1;
            }
            layer = &list->hwLayers[i];
            ret = is_displayframe_intersect(&list->hwLayers[firstOverlayLayer],layer);
            if(ret && firstOverlayLayer+1 <= lastOverlayLayer)
            {
                firstOverlayLayer ++;
                goto TryAgain;
            }
            else
            {
                if(is_out_log() == 2)
                    ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
                return -1;
            }
        }
        if(LayerZoneCheck(&list->hwLayers[topDimlayer+1],context))
        {
            if(is_out_log() == 2)
                ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
            return -1;
        }
        relOverlayLayer = firstOverlayLayer;
    }
    layer = &list->hwLayers[relOverlayLayer];
    hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
        / (layer->displayFrame.right - layer->displayFrame.left);
    vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
        / (layer->displayFrame.bottom - layer->displayFrame.top);
    if(hfactor != 1.0 && vfactor != 1.0)
    {
        if(is_out_log() == 2)
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }
    if(layer->transform || (layer->flags & HWC_SKIP_LAYER))
    {
        if(is_out_log() == 2)
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }
    handle = (struct private_handle_t *)layer->handle;
    if(type_of_android_format(handle?handle->format:-1) != 2)
    {
        if(is_out_log() == 2)
            ALOGD("line=%d,num=%d",__LINE__,list->numHwLayers - 1);
        return -1;
    }

    layer->compositionType = HWC_LCDC;

    handle_gpu_nodraw_optimazation(context,list,relOverlayLayer,HWC_GPU_NODRAW_VOP);

    context->win_swap = 1;
    context->composer_mode = HWC_GPU_NODRAW_VOP;
    ALOGV("hwc-prepare use HWC_GPU_VOP policy");
    return 0;
}

int try_hwc_cp_fb_policy(void * ctx,hwc_display_contents_1_t *list)
{
    return -1;
}

int try_hwc_skip_policy(void * ctx,hwc_display_contents_1_t *list)
{
    return -1;
}

int try_hwc_gpu_policy(void * ctx,hwc_display_contents_1_t *list)
{

    unsigned int i;
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];   
        layer->compositionType = HWC_FRAMEBUFFER;
        
    }
    
    return 0;
}

//extern "C" void *blend(uint8_t *dst, uint8_t *src, int dst_w, int src_w, int src_h);
static int hwc_prepare_primary(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list) 
{
    size_t i;
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;
    int iVideoCount = 0;
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    static int videoIndex = 0;
    int ret;

    /* Check device handle. */
    if (context == NULL || &context->device.common != (hw_device_t *) dev )
    {
        LOGE("%s(%d):Invalid device!", __FUNCTION__, __LINE__);
        return HWC_EGL_ERROR;
    }
    context->composer_mode = HWC_GPU;
    context->win_swap = 0;
#if hwcDEBUG
    LOGD("%s(%d):Layers to prepare:", __FUNCTION__, __LINE__);
    _Dump(list);
#endif

#if hwcDumpSurface
    _DumpSurface(list);
#endif

    if(context->bootCount < 1 && is_boot_skip_platform(context))
    {
        hwc_list_nodraw(list);
        return 0;
    }
    /* Check layer list. */
    if ((list == NULL)
            || (list->numHwLayers == 0)
            //||  !(list->flags & HWC_GEOMETRY_CHANGED)
       )
    {
        return 0; 
    }

    if(is_out_log())        
        LOGD("%s(%d):>>> prepare_primary %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);

    is_need_stereo(context,list);
    is_screen_changed(context, HWC_DISPLAY_PRIMARY);

    for (unsigned int i = 0; i < (list->numHwLayers - 1); i++)
    {
    	hwc_layer_1_t * layer = &list->hwLayers[i];
    	struct private_handle_t * handle = NULL;

    	if (layer) {
                layer->dospecialflag = 0;
                handle = (struct private_handle_t *)layer->handle;
        }
    }

    if(!try_prepare_first(context,list))
    {
        for(i = 0;i < HWC_POLICY_NUM;i++)
        {
            ret = context->fun_policy[i]((void*)context,list);
            if(!ret)
            {
                break; // find the policy
            }
        }
    }
    else
    {
        try_hwc_gpu_policy((void*)context,list);
        
    }

    context->NoDrMger.composer_mode_pre = context->composer_mode;
    if(is_out_log())    
        ALOGD("cmp_mode=%s,num=%d",compositionModeName[context->composer_mode],list->numHwLayers -1);
    if(!(context->composer_mode == HWC_NODRAW_GPU_VOP
        || context->composer_mode == HWC_RGA_GPU_VOP) )
    {
        for ( i = 0; i < 2 ; i++)
        {
            context->NoDrMger.addr[i] = 0; 
            context->NoDrMger.alpha[i] = 0;
        }
    }
    
    if(context->composer_mode == HWC_RGA || context->composer_mode == HWC_GPU )
    {
        hwc_layer_1_t * layer = &list->hwLayers[0];   
        layer->bufferCount = 1;
    }
    else   // win 0 & win 1 enable ,surfaceflinger change to one layer when 5s donot updata
    {
        hwc_layer_1_t * layer = &list->hwLayers[0];   
        layer->bufferCount = context->Is_Secure ? 1 : 2;
    }

    //if(context->composer_mode == HWC_NODRAW_GPU_VOP)
    context->last_composer_mode = context->composer_mode;
    return 0;
}
static int hwc_prepare_external(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list) 
{
    size_t i;
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;
    int iVideoCount = 0;

    hwcContext * context = gcontextAnchor[HWC_DISPLAY_EXTERNAL];
    static int videoIndex = 0;
    int ret;

    /* Check device handle. */
    if (context == NULL)
    {
        LOGV("%s(%d):hotpluging!", __FUNCTION__, __LINE__);
        return 0;
    }
    context->composer_mode = HWC_GPU;
    context->win_swap = 0;
#if hwcDEBUG
    LOGD("%s(%d):Layers to prepare:", __FUNCTION__, __LINE__);
    _Dump(list);
#endif

#if hwcDumpSurface
    _DumpSurface(list);
#endif

    /* Check layer list. */
    if ((list == NULL) || (list->numHwLayers == 0))
    {
        return 0;
    }
    if(is_out_log())        
        LOGD("%s(%d):>>> prepare_external %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);

    is_need_stereo(context,list);
    is_screen_changed(context, HWC_DISPLAY_EXTERNAL);

    if(!try_prepare_first(context,list))
    {
        for(i = 0;i < HWC_POLICY_NUM;i++)
        {
            ret = context->fun_policy[i]((void*)context,list);
            if(!ret)
            {
                break; // find the policy
            }
        }
    }
    else
    {
        try_hwc_gpu_policy((void*)context,list);
    }    

    context->NoDrMger.composer_mode_pre = context->composer_mode;
    if(is_out_log())
        ALOGD("ext:cmp_mode=%s,num=%d",compositionModeName[context->composer_mode],list->numHwLayers -1);
    if(!(context->composer_mode == HWC_NODRAW_GPU_VOP
        || context->composer_mode == HWC_RGA_GPU_VOP) )
    {
        for ( i = 0; i < 2 ; i++)
        {
            context->NoDrMger.addr[i] = 0;
            context->NoDrMger.alpha[i] = 0;
        }
    }

    if(context->composer_mode == HWC_RGA || context->composer_mode == HWC_GPU )
    {
        hwc_layer_1_t * layer = &list->hwLayers[0];
        layer->bufferCount = 1;
    }
    else   // win 0 & win 1 enable ,surfaceflinger change to one layer when 5s donot updata
    {
        hwc_layer_1_t * layer = &list->hwLayers[0];
        layer->bufferCount = context->Is_Secure ? 1 : 2;
    }

    //if(context->composer_mode == HWC_NODRAW_GPU_VOP)
    context->last_composer_mode = context->composer_mode;
    return 0;
}

static int initPlatform(hwcContext* ctx)
{
    if (!ctx)
        return -EINVAL;

    ctx->IsRk3036 = false;

#ifdef USE_X86

#elif defined(TARGET_BOARD_PLATFORM_RK312X)

#elif defined(TARGET_BOARD_PLATFORM_RK322X)

#elif defined(TARGET_BOARD_PLATFORM_RK3188)

#elif defined(TARGET_BOARD_PLATFORM_RK3036)
    ctx->IsRk3036 = true;
#elif defined(TARGET_BOARD_PLATFORM_RK3328)
    ctx->IsRk3328 = true;
#else
    ALOGE("Who is this platform?");
#endif

    ALOGI("rk3036:%s", ctx->IsRk3036 ? "Yes" : "No");

    ctx->isVr = false;
    ctx->isMid = false;
    ctx->isPhone = false;
    ctx->isDongle = false;

#ifdef RK_MID
    ctx->isMid = true;
#elif RK_BOX
    ctx->isBox = true;
#elif RK_PHONE
    ctx->isPhone = true;
#elif RK_VR
    ctx->isVr = true;
#elif RK_DONGLE
    ctx->isDongle = true;
#else
    ALOGE("Who is the platform?NOT these:box,mid,phone?");
#endif

    ALOGI("isBox:%s;  isMid:%s;  isPhone:%s;  isVr:%s; isDongle:%s",
           ctx->isBox ? "Yes" : "No",ctx->isMid ? "Yes" : "No",
           ctx->isPhone ? "Yes" : "No",ctx->isVr ? "Yes" : "No",
           ctx->isDongle ? "Yes" : "No");
    return 0;
}

int hwc_prepare_virtual(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list,hwc_display_contents_1_t *list_P) 
{
    HWC_UNREFERENCED_PARAMETER(dev);
    hwcContext * context_PRI = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    context_PRI->wfddev = 0;
    if (NULL  == list || NULL == list_P)
    {
        if(is_out_log())    
            ALOGD("list=%p,list_p=%p",list,list_P);
        return 0;
    }
    context_PRI->wfddev = 1;
    if(context_PRI->Is_Lvideo)
    {
        try_hwc_gpu_policy((void*)context_PRI,list_P);
        context_PRI->composer_mode = HWC_GPU;

        if(is_out_log())    
            ALOGD("Is large video > 1440 return GPU");

    }

    return 0;
}

int
hwc_prepare(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
    )
{

    int ret = 0;

    /* Check device handle. */
    if(is_out_log())    
        ALOGD("-----------hwc_prepare_start,numDisplays=%d -----------------",numDisplays);

    for (size_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_primary(dev, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_prepare_external(dev, list);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_prepare_virtual(dev, list,displays[HWC_DISPLAY_PRIMARY]);
                break;
            default:
                ret = -EINVAL;
        }
    }
    return ret;
}



int hwc_blank(struct hwc_composer_device_1 *dev, int dpy, int blank)
{
    // We're using an older method of screen blanking based on
    // early_suspend in the kernel.  No need to do anything here.
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    HWC_UNREFERENCED_PARAMETER(dev);

    ALOGI("dpy=%d,blank=%d",dpy,blank);
    switch (dpy)
    {
        case HWC_DISPLAY_PRIMARY:
            {
                int fb_blank = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
                int err = ioctl(context->fbFd, FBIOBLANK, fb_blank);
                ALOGV("call fb blank =%d",fb_blank);
                if (err < 0)
                {
                    if (errno == EBUSY)
                        ALOGD("%sblank ioctl failed (display already %sblanked)",
                              blank ? "" : "un", blank ? "" : "un");
                    else
                        ALOGE("%sblank ioctl failed: %s", blank ? "" : "un",
                              strerror(errno));
                    return -errno;
                }
                else
                {
                    context->fb_blanked = blank;
                }
                break;
            }

        case HWC_DISPLAY_EXTERNAL:
            {
                int fb_blank = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
                context = gcontextAnchor[HWC_DISPLAY_EXTERNAL];

                int err = ioctl(context->fbFd, FBIOBLANK, fb_blank);
                ALOGV("call fb blank =%d",fb_blank);
                if (err < 0)
                {
                    if (errno == EBUSY)
                        ALOGD("%sblank ioctl failed (display already %sblanked)",
                              blank ? "" : "un", blank ? "" : "un");
                    else
                        ALOGE("%sblank ioctl failed: %s", blank ? "" : "un",
                              strerror(errno));
                    return -errno;
                }
                else
                {
                    context->fb_blanked = blank;
                }
                break;
            }        
            #if FORCE_REFRESH
            if(0 == blank)
            {
                pthread_mutex_lock(&context->mRefresh.mlk);
                context->mRefresh.count = 0;
                pthread_mutex_unlock(&context->mRefresh.mlk);
                pthread_cond_signal(&context->mRefresh.cond);
            }
            #endif
            /*
            if (pdev->hdmi_hpd) {
                if (blank && !pdev->hdmi_blanked)
                    hdmi_disable(pdev);
                pdev->hdmi_blanked = !!blank;
            }
            */
            break;

        default:
            return -EINVAL;

    }

    return 0;
}

int hwc_query(struct hwc_composer_device_1* dev, int what, int* value)
{

    HWC_UNREFERENCED_PARAMETER(dev);

    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    switch (what)
    {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            // we support the background layer
            value[0] = 1;
            break;
        case HWC_VSYNC_PERIOD:
            // vsync period in nanosecond
            //value[0] = 1e9 / context->fb_fps;
            value[0] = context->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period;
            if (is_out_log()) ALOGD("get primary period=%d", *value);
            break;
        default:
            // unsupported query
            return -EINVAL;
    }
    return 0;
}


int hwc_set_virtual(hwc_composer_device_1_t * dev, hwc_display_contents_1_t  **contents)
{
    hwc_display_contents_1_t* list_pri = contents[0];
    hwc_display_contents_1_t* list_wfd = contents[2];
    hwc_layer_1_t *  fbLayer = &list_pri->hwLayers[list_pri->numHwLayers - 1];
    hwc_layer_1_t *  wfdLayer = NULL;
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    struct timeval tpend1, tpend2;
    long usec1 = 0;

    HWC_UNREFERENCED_PARAMETER(dev);

    gettimeofday(&tpend1, NULL);

    if(list_pri== NULL || list_wfd == NULL)
    {
        return -1;
    }
    wfdLayer = &list_wfd->hwLayers[list_wfd->numHwLayers - 1];
    
    if (fbLayer == NULL || wfdLayer == NULL)
    {
        return -1;
    }
    
    if (list_wfd)
    {
        hwc_sync(list_wfd);
    }

    if ((context->wfdOptimize > 0) && wfdLayer->handle)
    {
        hwc_cfg_t cfg;
        memset(&cfg, 0, sizeof(hwc_cfg_t));
        cfg.src_handle = (struct private_handle_t *)fbLayer->handle;
        cfg.transform = fbLayer->realtransform;

        cfg.dst_handle = (struct private_handle_t *)wfdLayer->handle;
        cfg.src_rect.left = (int)fbLayer->displayFrame.left;
        cfg.src_rect.top = (int)fbLayer->displayFrame.top;
        cfg.src_rect.right = (int)fbLayer->displayFrame.right;
        cfg.src_rect.bottom = (int)fbLayer->displayFrame.bottom;
        //cfg.src_format = cfg.src_handle->format;

        cfg.rga_fbFd = context->engine_fd;
        cfg.dst_rect.left = (int)wfdLayer->displayFrame.left;
        cfg.dst_rect.top = (int)wfdLayer->displayFrame.top;
        cfg.dst_rect.right = (int)wfdLayer->displayFrame.right;
        cfg.dst_rect.bottom = (int)wfdLayer->displayFrame.bottom;
        //cfg.dst_format = cfg.dst_handle->format;
        set_rga_cfg(&cfg);
        do_rga_transform_and_scale();
    }

    if (list_wfd)
    {
        hwc_sync_release(list_wfd);
    }

    gettimeofday(&tpend2, NULL);
    usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    ALOGV("hwc use time=%ld ms", usec1);
    return 0;
}

int getFbInfo(hwc_display_t dpy, hwc_surface_t surf, hwc_display_contents_1_t *list)
{
    android_native_buffer_t * fbBuffer = NULL;
    struct private_handle_t * fbhandle = NULL;
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    int numLayers = list->numHwLayers;
    hwc_layer_1_t *fbLayer = &list->hwLayers[numLayers - 1];
    if (!fbLayer)
    {
        ALOGE("fbLayer=NULL");
        return -1;
    }
    struct private_handle_t*  handle = (struct private_handle_t*)fbLayer->handle;
    if (!handle)
    {
        ALOGE("hanndle=NULL");
        return -1;
    }
    context->mFbFd = handle->share_fd;
    context->mFbBase = handle->base;

    return 0;

}
static int CompareLines(int *da,int w)
{
    int i,j;
    for(i = 0;i<1;i++) // compare 1 lins
    {
        for(j= 0;j<w;j++)  
        {
            if((*da & 0xFFFFFFFF) != 0xff000000 && *da != 0x0)
            {
                ALOGV("[%d,%d]=%x",i,j,*da);
                return 1;
            }            
            da ++;    
        }
    }    
    return 0;
}
static int CompareVers(int *da,int w,int h)
{
    int i,j;
    int *data ;
    for(i = 0;i<1;i++) // compare 1 lins
    {
        data = da + i;
        for(j= 0;j<h;j++)  
        {
            if((*data & 0xFFFFFFFF) != 0xff000000 && *data != 0x0 )
            {
                ALOGV("vers [%d,%d]=%x",i,j,*da);

                return 1;
            }    
            data +=w;    
        }
    }    
    return 0;
}

static int DetectValidData( hwcContext * context,int *data,int w,int h)
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
    if(context->video_ui != data)
    {
        context->video_ui = data;
        return 1;
    }    
    if(data == NULL)
        return 1;
    for(i = h/4;i<h;i+= h/4)
    {
        da = data +  i *w;
        if(CompareLines(da,w))
            return 1;
    }    
    for(i = w/4;i<w;i+= w/4)
    {
        da = data +  i ;
        if(CompareVers(da,w,h))
            return 1;
    }
    {
        da = data + int(0.865 * h) * w;
        if(CompareLines(da,w))
            return 1;
    }
    return 0; 
    
}
int Get_layer_disp_area( hwc_layer_1_t * layer, hwcRECT* dstRects)
{
    hwc_region_t * Region = &layer->visibleRegionScreen;
    hwc_rect_t * DstRect = &layer->displayFrame;
    hwc_rect_t const * rects = Region->rects;
    hwc_rect_t  rect_merge;
    int left_min =0; 
    int top_min =0;
    int right_max=0 ;
    int bottom_max=0;
    ;
    hwcRECT srcRects;

    struct private_handle_t*  handle = (struct private_handle_t*)layer->handle;

    if (!handle)
    {
        ALOGW("layer.handle=NULL,name=%s",layer->LayerName);
        return -1;
    }
    if(rects)
    {
         left_min = rects[0].left; 
         top_min  = rects[0].top;
         right_max  = rects[0].right;
         bottom_max = rects[0].bottom;
    }
    for (unsigned int r = 0; r < (unsigned int) Region->numRects ; r++)
    {
        int r_left;
        int r_top;
        int r_right;
        int r_bottom;
       
        r_left   = hwcMAX(DstRect->left,   rects[r].left);
        left_min = hwcMIN(r_left,left_min);
        r_top    = hwcMAX(DstRect->top,    rects[r].top);
        top_min  = hwcMIN(r_top,top_min);
        r_right    = hwcMIN(DstRect->right,  rects[r].right);
        right_max  = hwcMAX(r_right,right_max);
        r_bottom = hwcMIN(DstRect->bottom, rects[r].bottom);
        bottom_max  = hwcMAX(r_bottom,bottom_max);
    }
    rect_merge.left = left_min;
    rect_merge.top = top_min;
    rect_merge.right = right_max;
    rect_merge.bottom = bottom_max;

    dstRects->left   = hwcMAX(DstRect->left,   rect_merge.left);
    dstRects->top    = hwcMAX(DstRect->top,    rect_merge.top);
    dstRects->right  = hwcMIN(DstRect->right,  rect_merge.right);
    dstRects->bottom = hwcMIN(DstRect->bottom, rect_merge.bottom);
    return 0;
}

int hwc_vop_config(hwcContext * context, hwc_display_contents_1_t *list)
{
    int scale_cnt = 0;
    int start = 0,end = 0;
    int i = 0,step = 1;
    int j;
    int fd_dup = -1;
    int winIndex = 0;
    int rgaIndex = 0;
    bool fb_flag = false;
    cmpType mode = context->composer_mode;
    int dump = 0;
    struct timeval tpend1, tpend2;
    long usec1 = 0;
    int bk_index = 0;
    struct rk_fb_win_cfg_data fb_info;
    struct fb_var_screeninfo info;
    hwc_layer_1_t * fbLayer = &list->hwLayers[list->numHwLayers -1];
    struct private_handle_t*  fbhandle = NULL;
    if ((!context->dpyAttr[0].connected) 
            || (context->dpyAttr[0].fd <= 0)
            || (mode != HWC_VOP && ((!fbLayer)
            || !(fbLayer->handle))))
    {
		if(is_out_log())
			ALOGW("%s,%d out",__FUNCTION__,__LINE__);
        return -1;
    }
    if(mode != HWC_VOP)
        fbhandle = (struct private_handle_t*)fbLayer->handle;
    ALOGV("hwc_vop_config mode=%s",compositionModeName[mode]);
    info = context->info;
    memset(&fb_info, 0, sizeof(fb_info));
    fb_info.ret_fence_fd = -1;
    for(i = 0;i < RK_MAX_BUF_NUM;i++)
    {
        fb_info.rel_fence_fd[i] = -1;
    }
    switch (mode)
    {
        case HWC_VOP:
            start = 0;
            end = list->numHwLayers - 1;
            break;
        case HWC_RGA:
        case HWC_RGA_VOP:
        case HWC_RGA_TRSM_VOP:
        case HWC_RGA_TRSM_GPU_VOP:
        case HWC_NODRAW_GPU_VOP:
        case HWC_RGA_GPU_VOP:
        case HWC_VOP_RGA:
            if(HWC_VOP_RGA == mode)
            {
                rgaIndex = 1;
                start = 0;
                end = 1;        
                winIndex = 0;
            }
            if(HWC_RGA_VOP)
            {
                rgaIndex = 0;
                start = list->numHwLayers - 2;
                end = list->numHwLayers - 1;
                winIndex = 1;
            }
 			fb_info.win_par[rgaIndex].area_par[0].data_format = context->fbhandle.format;
            fb_info.win_par[rgaIndex].win_id = rgaIndex;
            fb_info.win_par[rgaIndex].z_order = rgaIndex;
            if(mode == HWC_NODRAW_GPU_VOP)
            {
                fb_info.win_par[rgaIndex].area_par[0].ion_fd = context->membk_fds[context->NoDrMger.membk_index_pre];                        
            }
            else
            {
                fb_info.win_par[rgaIndex].area_par[0].ion_fd = context->membk_fds[context->membk_index];            
            }
            // fb_info.win_par[rgaIndex].area_par[0].acq_fence_fd = -1;
            if(HWC_RGA == mode)
            {
                fd_dup = dup(context->membk_fence_acqfd[context->membk_index]);
                if(fd_dup < 0 && context->membk_fence_acqfd[context->membk_index] > -1)
                {
                    ALOGE("%s,%d,Dup fd fail for %s",__FUNCTION__,__LINE__,strerror(errno));
                }
                else if(fd_dup == context->membk_fence_acqfd[context->membk_index] && fd_dup > 0)
                    ALOGE("%s,%d,Dup fd fail for %s",__FUNCTION__,__LINE__,strerror(errno));
                else
                {
                    fb_info.win_par[rgaIndex].area_par[0].acq_fence_fd = fd_dup;
                    context->membk_fence_acqfd[context->membk_index] = -1;
                    //ALOGD("{%d,%d}",context->membk_fence_acqfd[context->membk_index],fd_dup);
                }
            }
            else
            {
                fb_info.win_par[rgaIndex].area_par[0].acq_fence_fd = context->membk_fence_acqfd[context->membk_index];
            }
            ALOGV("set vop acq_fd=%d",fb_info.win_par[0].area_par[0].acq_fence_fd);
            fb_info.win_par[rgaIndex].area_par[0].x_offset = 0;//info.xoffset;
            fb_info.win_par[rgaIndex].area_par[0].y_offset = 0;//info.yoffset;
            fb_info.win_par[rgaIndex].area_par[0].xpos = (info.nonstd >> 8) & 0xfff;
            fb_info.win_par[rgaIndex].area_par[0].ypos = (info.nonstd >> 20) & 0xfff;
            fb_info.win_par[rgaIndex].area_par[0].xsize = (info.grayscale >> 8) & 0xfff;
            fb_info.win_par[rgaIndex].area_par[0].ysize = (info.grayscale >> 20) & 0xfff;
            fb_info.win_par[rgaIndex].area_par[0].xact = info.xres;
            fb_info.win_par[rgaIndex].area_par[0].yact = info.yres;
            fb_info.win_par[rgaIndex].area_par[0].xvir = fbhandle->stride;
            fb_info.win_par[rgaIndex].area_par[0].yvir = info.yres;
            fb_info.wait_fs = 0;    
            if(mode == HWC_RGA)
            {
                start = 0;
                end = 0; 
                winIndex = 0;
            }
            else if(mode == HWC_RGA_TRSM_VOP
                    || mode == HWC_RGA_TRSM_GPU_VOP)
            {
                #if VIDEO_WIN1_UI_DISABLE               
                if(context->vop_mbshake)
                {
                    start = 0;
                    end = 0;   
                    winIndex = 0;
                }    
                else
                #endif
                {
                    if( HWC_RGA_TRSM_GPU_VOP == mode)
                    {
                        start = list->numHwLayers - 1;
                        end = list->numHwLayers;   
                        winIndex = 1;
                    }
                    else
                    {
                        start = 1;
                        end = list->numHwLayers - 1;   
                        winIndex = 1;
                    }    
                }    
                hwc_layer_1_t * layer = &list->hwLayers[0];
                hwcRECT disp_rect;                
                Get_layer_disp_area(layer,&disp_rect);
#ifdef USE_X86
                fb_info.win_par[0].area_par[0].data_format = 0x20;//dump_fmt;
#endif
                if(context->Is_bypp)
                    fb_info.win_par[0].area_par[0].x_offset = rkmALIGN(disp_rect.left,8);//info.xoffset
                else
                    fb_info.win_par[0].area_par[0].x_offset = disp_rect.left - disp_rect.left%2;//info.xoffset
                fb_info.win_par[0].area_par[0].y_offset = disp_rect.top - disp_rect.top %2;//info.yoffset;
                fb_info.win_par[0].area_par[0].xpos = disp_rect.left;
                fb_info.win_par[0].area_par[0].ypos = disp_rect.top;
                fb_info.win_par[0].area_par[0].xsize = disp_rect.right - disp_rect.left;
                fb_info.win_par[0].area_par[0].ysize = disp_rect.bottom - disp_rect.top;
                if(context->Is_bypp)
                    fb_info.win_par[0].area_par[0].xact = disp_rect.right - rkmALIGN(disp_rect.left,8);
                else
                    fb_info.win_par[0].area_par[0].xact = disp_rect.right - disp_rect.left;
                fb_info.win_par[0].area_par[0].xact -= fb_info.win_par[0].area_par[0].xact%2;
                fb_info.win_par[0].area_par[0].yact = disp_rect.bottom - disp_rect.top;  
                fb_info.win_par[0].area_par[0].yact -= fb_info.win_par[0].area_par[0].yact%2;


            }
            else if(mode == HWC_RGA_TRSM_GPU_VOP
                    || mode == HWC_NODRAW_GPU_VOP 
                    || mode == HWC_RGA_GPU_VOP)
            {
                start = list->numHwLayers - 1;
                end = list->numHwLayers;   
                winIndex = 1;
            }
            
           
            if(mode != HWC_NODRAW_GPU_VOP)
            {
                bk_index = context->NoDrMger.membk_index_pre = context->membk_index;
                if (context->membk_index >= (FB_BUFFERS_NUM - 1))
                {
                    context->membk_index = 0;
                }
                else
                {
                    context->membk_index++;
                }   
            }    
            break;
        case HWC_VOP_GPU:
            start = 0;
            end = list->numHwLayers;   
            winIndex = 0;
            step = list->numHwLayers -1;
            break;
        case HWC_GPU_VOP:
            start = list->numHwLayers - 2;
            end = list->numHwLayers;
            winIndex = 0;
            step = 1;
            break;
        case HWC_CP_FB:
            break;
        case HWC_GPU:
            start = list->numHwLayers - 1;
            end = list->numHwLayers;   
            winIndex = 0;
            break;
        case HWC_GPU_NODRAW_VOP:
            start = context->mLastStatus.ovleryLayer;
            end = list->numHwLayers;
            winIndex = 0;
            step = end - start -1;
            break;
        default:
            // unsupported query
            ALOGE("no policy set !!!");
            return -EINVAL;
    }
    if(is_out_log())
        ALOGD("[%d->%d],win_index=%d,step=%d",start,end,winIndex,step);
    for (i = start; i < end;)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        float hfactor = 1.0;
        float vfactor = 1.0;
        hwc_region_t * Region = &layer->visibleRegionScreen;
        hwc_rect_t const * SrcRect = &layer->sourceCrop;
        hwc_rect_t const * DstRect = &layer->displayFrame;
        hwc_rect_t const * rects = Region->rects;
        hwc_rect_t  rect_merge;
        int left_min =0; 
        int top_min =0;
        int right_max=0 ;
        int bottom_max=0;
        hwcRECT dstRects;
        hwcRECT srcRects;

        if(context->special_app && i == 1)
        {
            i++;
            continue;
        }    
        struct private_handle_t*  handle = (struct private_handle_t*)layer->handle;

        if (!handle)
        {
            ALOGW("layer[%d].handle=NULL,name=%s",i,layer->LayerName);
            i += step;
            continue;
        }
        #if 0   // debug ,Dont remove
        dump = hwc_get_int_property("sys.hwc.vopdump","0");        
        if(dump && (i == (list->numHwLayers -1)) && mode == HWC_VOP_GPU)
        {

            int32_t SrcStride ;
            FILE * pfile = NULL;
            char layername[100] ;
            static int DumpSurfaceCount = 0;

            SrcStride = android::bytesPerPixel(handle->format);
            memset(layername, 0, sizeof(layername));
            system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
            //mkdir( "/data/dump/",777);
            sprintf(layername, "/data/dump/vop%d_%d_%d_%d.bin", DumpSurfaceCount, handle->stride, handle->height, SrcStride);

            DumpSurfaceCount ++;
            pfile = fopen(layername, "wb");
            if (pfile)
            {
                fwrite((const void *)handle->base, (size_t)(SrcStride * handle->stride*handle->height), 1, pfile);
                fclose(pfile);
                LOGI(" dump surface layername %s,w:%d,h:%d,formatsize :%d", layername, handle->width, handle->height, SrcStride);
            }
            /*
            sprintf(layername, "/data/dump/vop%d_%d_%d_%d.bin", DumpSurfaceCount, handle->stride, handle->height, SrcStride);
            DumpSurfaceCount ++;
            pfile = fopen(layername, "wb");
            if (pfile)
            {
                fwrite((const void *)context->membk_base[context->NoDrMger.membk_index_pre], (size_t)(SrcStride * handle->stride*handle->height), 1, pfile);
                fclose(pfile);
                LOGI(" dump surface layername %s,w:%d,h:%d,formatsize :%d", layername, handle->width, handle->height, SrcStride);
            }
            */
            property_set("sys.hwc.vopdump", "0");

        }
        #endif
        if(rects)
        {
             left_min = rects[0].left; 
             top_min  = rects[0].top;
             right_max  = rects[0].right;
             bottom_max = rects[0].bottom;
        }
        for (unsigned int r = 0; r < (unsigned int) Region->numRects ; r++)
        {
            int r_left;
            int r_top;
            int r_right;
            int r_bottom;
           
            r_left   = hwcMAX(DstRect->left,   rects[r].left);
            left_min = hwcMIN(r_left,left_min);
            r_top    = hwcMAX(DstRect->top,    rects[r].top);
            top_min  = hwcMIN(r_top,top_min);
            r_right    = hwcMIN(DstRect->right,  rects[r].right);
            right_max  = hwcMAX(r_right,right_max);
            r_bottom = hwcMIN(DstRect->bottom, rects[r].bottom);
            bottom_max  = hwcMAX(r_bottom,bottom_max);
        }
        rect_merge.left = left_min;
        rect_merge.top = top_min;
        rect_merge.right = right_max;
        rect_merge.bottom = bottom_max;

        dstRects.left   = hwcMAX(DstRect->left,   rect_merge.left);
        dstRects.top    = hwcMAX(DstRect->top,    rect_merge.top);
        dstRects.right  = hwcMIN(DstRect->right,  rect_merge.right);
        dstRects.bottom = hwcMIN(DstRect->bottom, rect_merge.bottom);

        if( i == int(list->numHwLayers -1))
        {
            srcRects.left  = dstRects.left;
            srcRects.top = dstRects.top;
            srcRects.right = dstRects.right;
            srcRects.bottom = dstRects.bottom;

        }
        else
        {
            hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                / (layer->displayFrame.right - layer->displayFrame.left);

            vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                / (layer->displayFrame.bottom - layer->displayFrame.top);
            
            srcRects.left   = hwcMAX ((SrcRect->left \
            - (int) ((DstRect->left   - dstRects.left)   * hfactor)),0);
            srcRects.top    = hwcMAX ((SrcRect->top \
            - (int) ((DstRect->top    - dstRects.top)    * vfactor)),0);

            srcRects.right  = SrcRect->right \
            - (int) ((DstRect->right  - dstRects.right)  * hfactor);
            srcRects.bottom = SrcRect->bottom \
            - (int) ((DstRect->bottom - dstRects.bottom) * vfactor);
            
        
        }
        ALOGV("layer[%d] = %s",i,layer->LayerName);
        ALOGV("SRC[%d,%d,%d,%d] -> DST[%d,%d,%d,%d]",          
           srcRects.left,srcRects.top,srcRects.right,srcRects.bottom,
           dstRects.left,dstRects.top,dstRects.right,dstRects.bottom
           );
        
        ALOGV("SRC[%d,%d,%d,%d] -> src[%d,%d,%d,%d]",
           SrcRect->left,SrcRect->top,SrcRect->right,SrcRect->bottom,
           srcRects.left,srcRects.top,srcRects.right,srcRects.bottom
           );

        ALOGV("DST[%d,%d,%d,%d] -> dst[%d,%d,%d,%d]",
           DstRect->left,DstRect->top,DstRect->right, DstRect->bottom,
           dstRects.left,dstRects.top,dstRects.right,dstRects.bottom
           );

        fb_info.win_par[winIndex].win_id = context->win_swap ?(1-winIndex):winIndex;
        if(mode == HWC_GPU_VOP || mode == HWC_GPU_NODRAW_VOP)
        {
            fb_info.win_par[winIndex].z_order = 1 - winIndex;
        }
        else
            fb_info.win_par[winIndex].z_order = winIndex;
        fb_info.win_par[winIndex].area_par[0].ion_fd = handle->share_fd;
        fb_info.win_par[winIndex].area_par[0].data_format = handle->format;
        fb_info.win_par[winIndex].area_par[0].acq_fence_fd = layer->acquireFenceFd;       
        fb_info.win_par[winIndex].area_par[0].x_offset =  hwcMAX(srcRects.left, 0);
        if( i == int(list->numHwLayers -1))
        {           
            fb_info.win_par[winIndex].area_par[0].y_offset = handle->offset / (android::bytesPerPixel(handle->format) * handle->stride);//context->fbStride;    
            fb_info.win_par[winIndex].area_par[0].yvir = handle->height*NUM_FB_BUFFERS;
//            ALOGD("rk_debug fmtsize=%d",android::bytesPerPixel(handle->format)); 
//            ALOGD("rk_debug11 offset[%d,%d],stride=%d,strid2=%d",fb_info.win_par[winIndex].area_par[0].y_offset, handle->offset,context->fbStride,handle->stride);
            
        }
        else
        {        
            fb_info.win_par[winIndex].area_par[0].y_offset = hwcMAX(srcRects.top, 0);    
            fb_info.win_par[winIndex].area_par[0].yvir = handle->height;
        }        
        fb_info.win_par[winIndex].area_par[0].xpos =  hwcMAX(dstRects.left, 0);
        fb_info.win_par[winIndex].area_par[0].ypos = hwcMAX(dstRects.top , 0);
        fb_info.win_par[winIndex].area_par[0].xsize = dstRects.right - dstRects.left;
        fb_info.win_par[winIndex].area_par[0].ysize = dstRects.bottom - dstRects.top;
        fb_info.win_par[winIndex].area_par[0].xact = srcRects.right- srcRects.left;
        fb_info.win_par[winIndex].area_par[0].yact = srcRects.bottom - srcRects.top;
        fb_info.win_par[winIndex].area_par[0].xvir = handle->stride;
        if(fb_info.win_par[winIndex].area_par[0].data_format == HAL_PIXEL_FORMAT_YCrCb_NV12)
        {
            fb_info.win_par[winIndex].area_par[0].data_format = 0x20;
            if ((handle->usage & GRALLOC_USAGE_PRIVATE_2) && !context->Is_noi)
            {
                fb_info.win_par[winIndex].area_par[0].xvir *= 2;
                fb_info.win_par[winIndex].area_par[0].yvir /= 2;
                fb_info.win_par[winIndex].area_par[0].yact /= 2;
            }
            /**/
            fb_info.win_par[winIndex].area_par[0].x_offset -= fb_info.win_par[winIndex].area_par[0].x_offset%2;
            fb_info.win_par[winIndex].area_par[0].y_offset -= fb_info.win_par[winIndex].area_par[0].y_offset%2;            
            fb_info.win_par[winIndex].area_par[0].xact -= fb_info.win_par[winIndex].area_par[0].xact%2;
            fb_info.win_par[winIndex].area_par[0].yact -= fb_info.win_par[winIndex].area_par[0].yact%2;
	    //ALOGD("usage=%x,%x",handle->usage,GRALLOC_USAGE_PROTECTED);
/*
            if(handle->usage & GRALLOC_USAGE_PROTECTED)
            {
                 fb_info.win_par[winIndex].area_par[0].phy_addr = handle->phy_addr;
                 //ALOGD("video @protect phy=%x",handle->phy_addr);
            }
*/
        }
        if(fb_info.win_par[winIndex].area_par[0].data_format == HAL_PIXEL_FORMAT_YCrCb_NV12_10)
        {
            fb_info.win_par[winIndex].area_par[0].data_format = 0x22;
            if ((handle->usage & GRALLOC_USAGE_PRIVATE_2) && !context->Is_noi)
            {
                fb_info.win_par[winIndex].area_par[0].xvir *= 2;
                fb_info.win_par[winIndex].area_par[0].yvir /= 2;
                fb_info.win_par[winIndex].area_par[0].yact /= 2;
            }
            /**/
            fb_info.win_par[winIndex].area_par[0].x_offset -= fb_info.win_par[winIndex].area_par[0].x_offset%2;
            fb_info.win_par[winIndex].area_par[0].y_offset -= fb_info.win_par[winIndex].area_par[0].y_offset%2;
            fb_info.win_par[winIndex].area_par[0].xact -= fb_info.win_par[winIndex].area_par[0].xact%2;
            fb_info.win_par[winIndex].area_par[0].yact -= fb_info.win_par[winIndex].area_par[0].yact%2;
	    //ALOGD("usage=%x,%x",handle->usage,GRALLOC_USAGE_PROTECTED);
/*  
	    if(handle->usage & GRALLOC_USAGE_PROTECTED)
            {
                 fb_info.win_par[winIndex].area_par[0].phy_addr = handle->phy_addr;
                 //ALOGD("video @protect phy=%x",handle->phy_addr);
            }
*/
        }
        hotpulg_did_hdr_video(context, &fb_info.win_par[winIndex], handle);
        winIndex ++;
        i += step;
        
    }
    //fb_info.wait_fs = 1;
    //gettimeofday(&tpend1, NULL);
    #if VIDEO_WIN1_UI_DISABLE // detect UI invalid ,so close win1 ,reduce  bandwidth.
    if(
        /*fb_info.win_par[0].area_par[0].data_format == 0x20*/
        context->Is_video
        && list->numHwLayers == 3)  // @ video & 2 layers
    {
        bool IsDiff = true;
        int ret;
        hwc_layer_1_t * layer = &list->hwLayers[1];
        if(layer)
        {
            struct private_handle_t* uiHnd = (struct private_handle_t *) layer->handle;
            if(uiHnd)
            {
                IsDiff = uiHnd->share_fd != context->vui_fd;
                if(IsDiff)
                {
                    context->vui_hide = 0;  
                }
                else if(!context->vui_hide)
                {
                    ret = DetectValidData(context,(int *)uiHnd->base,uiHnd->width,uiHnd->height);
                    if(!ret)
                    {                               
                        context->vui_hide = 1;
                        ALOGD(" @video UI close");
                    }
                }
                // close UI win
                if(context->vui_hide == 1)
                {
                    for(i = 1;i<4;i++)
                    {
                        for(j=0;j<4;j++)
                        {
                            fb_info.win_par[i].area_par[j].ion_fd = 0;
                            fb_info.win_par[i].area_par[j].phy_addr = 0;
                        }
                    }
                }
                context->vui_fd = uiHnd->share_fd;
            }
        }
    }
    #endif

   // if(!context->fb_blanked)
    {
	if (context->IsRk3328 || context->IsRk3126 || context->IsRk3128)
	    hotplug_reset_dstpos(&fb_info, 5);
        else if(context->IsRk322x && context->IsRkBox)
            hotplug_reset_dstpos(&fb_info,2);

#if (defined(HOTPLUG_MODE) && !defined(TARGET_BOARD_PLATFORM_RK3188))
        if(context == gcontextAnchor[0] && gcontextAnchor[0]->mHtg.HtgOn 
            && (gcontextAnchor[1] && gcontextAnchor[1]->fb_blanked))
        {
            if(is_out_log())
                ALOGD("line=%d,enter reset display frame",__LINE__);
            hotplug_reset_dstpos(&fb_info,0);
            if(gcontextAnchor[1]->mIsVirUiResolution)
                hotplug_reset_dstpos(&fb_info,3);
        }
#endif
        if (context->mScreenChanged)
            hotplug_reset_dstpos(&fb_info, 4);

        if(getHdmiMode() && context->IsRk3188 && ONLY_USE_ONE_VOP == 1)
            sync_fbinfo_fence(&fb_info);

        if(context->IsRk3188 && mode == HWC_GPU_NODRAW_VOP)
            sort_fbinfo_by_winid(&fb_info);

        if((context->IsRk3126 || context->IsRk3128) && context == gcontextAnchor[1])
            hotplug_reset_dstpos(&fb_info,1);

        if(context->mIsVirUiResolution)
            hotplug_reset_dstpos(&fb_info,3);

        if ((context->IsRk3328 || context->IsRk322x) && context->IsRkBox)
            sync_fbinfo_fence(&fb_info);

        if (context->IsRk3328)
            hwc_reset_fb_info(&fb_info, context);

        if(ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &fb_info))
        {
            ALOGE("ioctl config done error");
        }
        ///gettimeofday(&tpend2, NULL);
        //usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
        //LOGD("config use time=%ld ms",  usec1); 

        //debug info
        dump_config_info(context,fb_info);
        if (context->hdrFrameStatus > 0) {
            context->hdrFrameStatus = 0;
        } else {
            if (context->hdrStatus > 0) {
                deinit_tv_hdr_info(context);
                context->hdrStatus = 0;
            }
        }
#if 0
        for (int k = 0;k < RK_MAX_BUF_NUM;k++)
        {
            if (fb_info.rel_fence_fd[k] > -1)
            {
                close(fb_info.rel_fence_fd[k]);
            }                 
        }
        if(fb_info.ret_fence_fd > -1)
            close(fb_info.ret_fence_fd);

#else
        if(is_out_log()>1)
        {
            for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                ALOGD("fb_info.rel_fence_fd[k]=%d",fb_info.rel_fence_fd[k]);
            ALOGD("fb_info.ret_fence_fd=%d",fb_info.ret_fence_fd);
#if RGA_USE_FENCE
            for(int k = 0; k<RGA_REL_FENCE_NUM; k++)
                ALOGD("rga_fence_fd[%d] = %d,index=%d", k, context->rga_fence_relfd[k],bk_index);
#endif
        }
        switch (mode)
        {
            case HWC_VOP:
                for (unsigned int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if( k < list->numHwLayers - 1)
                            list->hwLayers[k].releaseFenceFd = fb_info.rel_fence_fd[k];
                        else 
                            close(fb_info.rel_fence_fd[k]);
                    }                 
                }
                if(fb_info.ret_fence_fd > -1)
                    list->retireFenceFd = fb_info.ret_fence_fd;
                break;
            case HWC_RGA:
                
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        context->membk_fence_fd[bk_index] = fb_info.rel_fence_fd[k];
                    }
                }

                #if RGA_USE_FENCE
                for(unsigned int k = 0; k<RGA_REL_FENCE_NUM; k++)
            	{
                    if(context->rga_fence_relfd[k] != -1)
                    {
                        if(k < (list->numHwLayers - 1))
                        {
                            list->hwLayers[k].releaseFenceFd = context->rga_fence_relfd[k];
                            ALOGV("rga_fence_fd[%d] = %d,index=%d", k, context->rga_fence_relfd[k],bk_index);
                        }
                        else
                            close(context->rga_fence_relfd[k]);
                     }
            	}
                #endif
                if(fb_info.ret_fence_fd > 0)
                    close(fb_info.ret_fence_fd);
                // if(fb_info.ret_fence_fd > 0)
                   // list->retireFenceFd = fb_info.ret_fence_fd;      
                if(fd_dup > -1)
                {
                    sync_wait(fd_dup,-1);
                    close(fd_dup);
                    fd_dup = -1;
                }
                break;
            case HWC_RGA_VOP:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if(0 == k)
                            list->hwLayers[list->numHwLayers - 2].releaseFenceFd = fb_info.rel_fence_fd[k];
                        else
                            close(fb_info.rel_fence_fd[k]);
                    }
                }
                if(fb_info.ret_fence_fd > -1)
                    list->retireFenceFd = fb_info.ret_fence_fd;
                    //close(fb_info.ret_fence_fd);
                break;
            case HWC_RGA_TRSM_VOP:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        close(fb_info.rel_fence_fd[k]);
                    }                 
                }
                if(fb_info.ret_fence_fd > -1)
                    close(fb_info.ret_fence_fd);                                   
                break;
            case HWC_VOP_RGA:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if(0 == k)
                            list->hwLayers[k].releaseFenceFd = fb_info.rel_fence_fd[k];
                        else                        
                            close(fb_info.rel_fence_fd[k]);
                    }
                }
                if(fb_info.ret_fence_fd > -1)
                    close(fb_info.ret_fence_fd);
                break;
            case HWC_RGA_TRSM_GPU_VOP:   
            case HWC_NODRAW_GPU_VOP:
            case HWC_RGA_GPU_VOP:
            case HWC_GPU:
            //case HWC_VOP_GPU:

                for (unsigned int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if( k < list->numHwLayers && !fb_flag )
                        {
                            fb_flag = true;
                            list->hwLayers[list->numHwLayers - 1].releaseFenceFd = fb_info.rel_fence_fd[k];
                            ALOGV("set gpu_fb_fd=%d",list->hwLayers[list->numHwLayers - 1].releaseFenceFd);
                        }    
                        else 
                            close(fb_info.rel_fence_fd[k]);
                    }                 
                }
                if(fb_info.ret_fence_fd > -1)
                    list->retireFenceFd = fb_info.ret_fence_fd;      
                break;
                
            case HWC_VOP_GPU:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if( k == 0 )
                        {
                            list->hwLayers[k].releaseFenceFd = fb_info.rel_fence_fd[k];
                            //list->hwLayers[k].releaseFenceFd = -1;
                            //close(fb_info.rel_fence_fd[k]);
                            ALOGV("vo_gpu [%d]=%d",k,fb_info.rel_fence_fd[k]);
                        }                        
                        else if(k == 1)
                        {
                            list->hwLayers[list->numHwLayers - 1].releaseFenceFd = fb_info.rel_fence_fd[k];
                            //list->hwLayers[list->numHwLayers - 1].releaseFenceFd = -1;
                            //close(fb_info.rel_fence_fd[k]);
                            ALOGV("vo_gpu [%d]=%d",k,fb_info.rel_fence_fd[k]);
                        }
                        else 
                            close(fb_info.rel_fence_fd[k]);
                    }           
                }
                if(fb_info.ret_fence_fd > -1)
                {
                    list->retireFenceFd = fb_info.ret_fence_fd;
                    //list->retireFenceFd = -1;
                    //close(fb_info.ret_fence_fd);
                }
                break;
            case HWC_GPU_VOP:
		        list->hwLayers[list->numHwLayers - 1].releaseFenceFd = -1;
		        list->hwLayers[list->numHwLayers - 2].releaseFenceFd = -1;
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    ALOGD("fb_info.rel_fence_fd[%d]=%d",k,fb_info.rel_fence_fd[k]);
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if(list->hwLayers[list->numHwLayers - 2].releaseFenceFd == -1)
                            list->hwLayers[list->numHwLayers - 2].releaseFenceFd = fb_info.rel_fence_fd[k];
			            else if(list->hwLayers[list->numHwLayers - 1].releaseFenceFd == -1)
			                list->hwLayers[list->numHwLayers - 1].releaseFenceFd = fb_info.rel_fence_fd[k];
                        else
                            close(fb_info.rel_fence_fd[k]);
                    }
                }
                if(fb_info.ret_fence_fd > -1)
                    list->retireFenceFd = fb_info.ret_fence_fd;
                break;
            case HWC_CP_FB:
                break;
            case HWC_GPU_NODRAW_VOP:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > -1)
                    {
                        if( k == 0 )
                        {
                            list->hwLayers[start].releaseFenceFd = fb_info.rel_fence_fd[k];
                            //list->hwLayers[k].releaseFenceFd = -1;
                            //close(fb_info.rel_fence_fd[k]);
                            ALOGV("vo_gpu_no [%d]=%d",k,fb_info.rel_fence_fd[k]);
                        }
                        else if(k == 1)
                        {
                            list->hwLayers[list->numHwLayers - 1].releaseFenceFd = fb_info.rel_fence_fd[k];
                            //list->hwLayers[list->numHwLayers - 1].releaseFenceFd = -1;
                            //close(fb_info.rel_fence_fd[k]);
                            ALOGV("vo_gpu_no [%d]=%d",k,fb_info.rel_fence_fd[k]);
                        }
                        else
                        {
                            close(fb_info.rel_fence_fd[k]);
                            fb_info.rel_fence_fd[k] = -1;
                        }
                    }
                }
                if(fb_info.ret_fence_fd > -1)
                {
                    list->retireFenceFd = fb_info.ret_fence_fd;
                }
                break;
            default:
                return -EINVAL;
        }
        if(is_out_log()>1)
        {
            for(unsigned int k=0;k<list->numHwLayers;k++)
                ALOGD("list->hwLayers[k].releaseFenceFd=%d",list->hwLayers[k].releaseFenceFd);
            ALOGD("list->retireFenceFd=%d",list->retireFenceFd);
        }
#endif  
    }
    return 0;
}

int hwc_rga_blit( hwcContext * context ,hwc_display_contents_1_t *list)
{
    hwcSTATUS status = hwcSTATUS_OK;
    unsigned int i;
    unsigned int index = 0;

#if hwcUseTime
    struct timeval tpend1, tpend2;
    long usec1 = 0;
#endif
#if hwcBlitUseTime
    struct timeval tpendblit1, tpendblit2;
    long usec2 = 0;
#endif

    hwc_layer_1_t *fbLayer = NULL;
    struct private_handle_t * fbhandle = NULL;
    bool bNeedFlush = false;
    FenceMangrRga RgaFenceMg;

#if hwcUseTime
    gettimeofday(&tpend1, NULL);
#endif
    memset(&RgaFenceMg,0,sizeof(FenceMangrRga));

    context->Is_bypp = false;
    LOGV("%s(%d):>>> Set  %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);
    /* Prepare. */
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        /* Check whether this composition can be handled by hwcomposer. */
        if (list->hwLayers[i].compositionType >= HWC_BLITTER)
        {
#if FENCE_TIME_USE
            struct timeval tstart, tend;
            gettimeofday(&tstart, NULL);
#endif
            #if 1
            if (context->membk_fence_fd[context->membk_index] > 0)
            {
                sync_wait(context->membk_fence_fd[context->membk_index], 2000);
                ALOGV("fenceFd=%d,name=%s", context->membk_fence_fd[context->membk_index],list->hwLayers[i].LayerName);
                close(context->membk_fence_fd[context->membk_index]);
                context->membk_fence_fd[context->membk_index] = -1;
            }
            #endif

            #if 0
            if(context->membk_fence_acqfd[context->membk_index] > 0)  
            {
                sync_wait(context->membk_fence_acqfd[context->membk_index], 500);
                close(context->membk_fence_acqfd[context->membk_index]);
                context->membk_fence_acqfd[context->membk_index] = -1;
                //ALOGD("close0 rga acq_fd=%d",fb_info.win_par[0].area_par[0].acq_fence_fd);
            }                    
            #endif
#if FENCE_TIME_USE	
            gettimeofday(&tend, NULL);
            if(((tend.tv_sec - tstart.tv_sec)*1000 + (tend.tv_usec - tstart.tv_usec)/1000) > 16)
            {
                ALOGW("wait for LCDC fence too long ,spent t = %ld ms",((tend.tv_sec - tstart.tv_sec)*1000 + (tend.tv_usec - tstart.tv_usec)/1000));
            }
#endif    

#if ENABLE_HWC_WORMHOLE
            hwcRECT FbRect;
            hwcArea * area;
            hwc_region_t holeregion;
#endif
            bNeedFlush = true;

            fbLayer = &list->hwLayers[list->numHwLayers - 1];
            ALOGV("fbLyaer = %x,num=%d",fbLayer,list->numHwLayers);
            if (fbLayer == NULL)
            {
                ALOGE("fbLayer is null");
                hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
            }
            fbhandle = (struct private_handle_t*)fbLayer->handle;
            if (fbhandle == NULL)
            {
                ALOGE("fbhandle is null");
                hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
            }            
            ALOGV("i=%d,tpye=%d,hanlde=%p",i,list->hwLayers[i].compositionType,fbhandle);
#if ENABLE_HWC_WORMHOLE
            /* Reset allocated areas. */
            if (context->compositionArea != NULL)
            {
                ZoneFree(context, context->compositionArea);

                context->compositionArea = NULL;
            }

            FbRect.left = 0;
            FbRect.top = 0;
            FbRect.right = fbhandle->width;
            FbRect.bottom = fbhandle->height;

            /* Generate new areas. */
            /* Put a no-owner area with screen size, this is for worm hole,
             * and is needed for clipping. */
            context->compositionArea = zone_alloc(context,
                                       NULL,
                                       &FbRect,
                                       0U);

            /* Split areas: go through all regions. */
            for (unsigned int k = 0; k < list->numHwLayers - 1; k++)
            {
                int owner = 1U << k;
                hwc_layer_1_t *  hwLayer = &list->hwLayers[k];
                hwc_region_t * region  = &hwLayer->visibleRegionScreen;
                //struct private_handle_t* srchnd = (struct private_handle_t *) hwLayer->handle;

                //zxl:ignore PointerLocation
               // if (!strcmp(hwLayer->LayerName, "PointerLocation"))
               // {
                  //  ALOGV("ignore PointerLocation,or it will overlay the whole area");
                   // continue;
               // }
                if((hwLayer->blending & 0xFFFF) != HWC_BLENDING_NONE)
                {
                    ALOGV("ignore alpha layer");
                    continue;
                }
                /* Now go through all rectangles to split areas. */
                for (unsigned int j = 0; j < region->numRects; j++)
                {
                    /* Assume the region will never go out of dest surface. */
                    DivArea(context,
                               context->compositionArea,
                               (hwcRECT *) &region->rects[j],
                               owner);

                }

            }
#if DUMP_SPLIT_AREA
            LOGV("SPLITED AREA:");
            hwcDumpArea(context->compositionArea);
#endif

            area = context->compositionArea;

            while (area != NULL)
            {
                /* Check worm hole first. */
                if (area->owners == 0U)
                {

                    holeregion.numRects = 1;
                    holeregion.rects = (hwc_rect_t const*) & area->rect;
                    /* Setup worm hole source. */
                    LOGV(" WormHole [%d,%d,%d,%d]",
                         area->rect.left,
                         area->rect.top,
                         area->rect.right,
                         area->rect.bottom
                        );

                    hwcClear(context,
                             0xFF000000,
                             &list->hwLayers[i],
                             fbhandle,
                             (hwc_rect_t *)&area->rect,
                             &holeregion
                            );

                    /* Advance to next area. */
                }
                area = area->next;
            }
#endif
            /* Done. */
            break;
        }
        else if (list->hwLayers[i].compositionType == HWC_FRAMEBUFFER)
        {
            /* Previous swap rectangle is gone. */
            break;

        }
    }
    /* Go through the layer list one-by-one blitting each onto the FB */
    

#if RGA_USE_FENCE
    for(i = 0;i< RGA_REL_FENCE_NUM;i++)
    {
        context->rga_fence_relfd[i] = -1;
    }
    if(context->composer_mode == HWC_RGA)
        RgaFenceMg.use_fence = true;
#endif    

    
    for (i = 0; i < list->numHwLayers -1; i++)
    {
        switch (list->hwLayers[i].compositionType)
        {
            case HWC_BLITTER:
                ALOGV("%s(%d):Layer %d ,name=%s,is BLIITER", __FUNCTION__, __LINE__, i,list->hwLayers[i].LayerName);
                /* Do the blit. */
               
#if hwcBlitUseTime
                gettimeofday(&tpendblit1, NULL);
#endif
                hwcONERROR(
                    hwcBlit(context,
                            &list->hwLayers[i],
                            fbhandle,
                            &list->hwLayers[i].sourceCrop,
                            &list->hwLayers[i].displayFrame,
                            &list->hwLayers[i].visibleRegionScreen,
                            &RgaFenceMg,index));

                if(RgaFenceMg.use_fence)
                    context->rga_fence_relfd[i] =  RgaFenceMg.rel_fd;
                else
                    context->rga_fence_relfd[i] = -1;
                
        #if RGA_USE_FENCE
                if(context->rga_fence_relfd[i] > 0 && i < 1)  
                {
                    sync_wait(context->rga_fence_relfd[i], 500);
                    close(context->rga_fence_relfd[i]);
                    context->rga_fence_relfd[i] = -1;
                    //ALOGD("close0 rga acq_fd=%d",fb_info.win_par[0].area_par[0].acq_fence_fd);
                }                    
        #endif
#if hwcBlitUseTime
                gettimeofday(&tpendblit2, NULL);
                usec2 = 1000 * (tpendblit2.tv_sec - tpendblit1.tv_sec) + (tpendblit2.tv_usec - tpendblit1.tv_usec) / 1000;
                LOGD("hwcBlit compositer %d layers=%s use time=%ld ms", i, list->hwLayers[i].LayerName, usec2);
#endif
                index++;
                break;

            case HWC_CLEAR_HOLE:
                LOGV("%s(%d):Layer %d is CLEAR_HOLE", __FUNCTION__, __LINE__, i);
                /* Do the clear, color = (0, 0, 0, 1). */
                /* TODO: Only clear holes on screen.
                 * See Layer::onDraw() of surfaceflinger. */
                if (i != 0) break;

                hwcONERROR(
                    hwcClear(context,
                             0xFF000000,
                             &list->hwLayers[i],
                             fbhandle,
                             &list->hwLayers[i].displayFrame,
                             &list->hwLayers[i].visibleRegionScreen));
                break;

            case HWC_DIM:
                LOGV("%s(%d):Layer %d is DIM", __FUNCTION__, __LINE__, i);
                if (i == 0)
                {
                    /* Use clear instead of dim for the first layer. */
                    hwcONERROR(
                        hwcClear(context,
                                 ((list->hwLayers[0].blending & 0xFF0000) << 8),
                                 &list->hwLayers[i],
                                 fbhandle,
                                 &list->hwLayers[i].displayFrame,
                                 &list->hwLayers[i].visibleRegionScreen));
                }
                else
                {
                    /* Do the dim. */
                    hwcONERROR(
                        hwcDim(context,
                               &list->hwLayers[i],
                               fbhandle,
                               &list->hwLayers[i].displayFrame,
                               &list->hwLayers[i].visibleRegionScreen));
                }
                break;

            case HWC_OVERLAY:
                /* TODO: HANDLE OVERLAY LAYERS HERE. */
                LOGV("%s(%d):Layer %d is OVERLAY", __FUNCTION__, __LINE__, i);
                break;
            }
                
    }

    if(RgaFenceMg.use_fence)
    {
        context->membk_fence_acqfd[context->membk_index] = context->rga_fence_relfd[i-1];
    }
    else
    {
        context->membk_fence_acqfd[context->membk_index] = -1;
        if (bNeedFlush && !context->Is_bypp)
        {        
            if (ioctl(context->engine_fd, RGA_FLUSH, NULL) != 0)
            {
                LOGE("%s(%d):RGA_FLUSH Failed!", __FUNCTION__, __LINE__);
            }
        }
    }    
#if hwcUseTime
    gettimeofday(&tpend2, NULL);
    usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    LOGV("hwcBlit compositer %d layers use time=%ld ms", list->numHwLayers -1, usec1); 
#endif

    return 0; //? 0 : HWC_EGL_ERROR;
OnError:

    LOGE("%s(%d):Failed!", __FUNCTION__, __LINE__);
    return HWC_EGL_ERROR;
}


int hwc_policy_set(hwcContext * context,hwc_display_contents_1_t *list)
{
    ALOGV("hwc_policy_set mode=%s",compositionModeName[context->composer_mode]);
    switch (context->composer_mode)
    {
        case HWC_VOP: 
        case HWC_VOP_GPU:
        case HWC_GPU_VOP:
        case HWC_GPU:        
        case HWC_NODRAW_GPU_VOP:
        case HWC_GPU_NODRAW_VOP:
            hwc_vop_config(context,list);        
            break;
        case HWC_RGA:
        case HWC_RGA_VOP:
        case HWC_RGA_TRSM_VOP:           
        case HWC_RGA_TRSM_GPU_VOP:   
        case HWC_RGA_GPU_VOP:
        case HWC_VOP_RGA:
            hwc_rga_blit(context,list);
            hwc_vop_config(context,list);        
            break;
        case HWC_CP_FB:
            break;
        default:
            ALOGE("%d,fatal",__LINE__);
            return -EINVAL;
    }
    return 0;
}

static int hwc_set_primary(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list) 
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    bool force_skip = false;
#if hwcUseTime
    struct timeval tpend1, tpend2;
    long usec1 = 0;
#endif


    if(is_out_log())        
        LOGD("%s(%d):>>> hwc_set_primary  <<<",
         __FUNCTION__,
         __LINE__);
    /* Check device handle. */
    if (context == NULL
            || &context->device.common != (hw_device_t *) dev
       )
    {
        LOGE("%s(%d): Invalid device!", __FUNCTION__, __LINE__);
        return hwcRGA_OPEN_ERR;
    }

    /* Check layer list. */
    if(is_boot_skip_platform(context))
        force_skip = context->bootCount < 1;
    else
        force_skip = context->bootCount < 5;
    if (/*list->skipflag || */force_skip /*|| list->numHwLayers <=1*/)
    {
      
        hwc_sync_release(list);
        context->bootCount ++;
        ALOGW("hwc skip!,%d,numHwLayers=%d",list->skipflag, list->numHwLayers);
        return 0;
    }

#if ONLY_USE_ONE_VOP
    if(context->mHtg.HtgOn && gcontextAnchor[1] && !gcontextAnchor[1]->fb_blanked)
    {
        hwc_sync_release(list);
        return 0;
    }
#endif

#if hwcUseTime
    gettimeofday(&tpend1, NULL);
#endif
    //hwc_sync(list);
    switch (context->composer_mode)
    {
        case HWC_VOP:
        case HWC_VOP_GPU:
        case HWC_GPU_VOP:
        case HWC_GPU:
        case HWC_NODRAW_GPU_VOP:
            break;
        case HWC_RGA:
        case HWC_RGA_VOP:
        case HWC_RGA_TRSM_VOP:           
        case HWC_RGA_TRSM_GPU_VOP:   
        case HWC_RGA_GPU_VOP:
        case HWC_VOP_RGA:
        case HWC_GPU_NODRAW_VOP:
            hwc_sync(list);
            break;
        case HWC_CP_FB:
            break;
        default:
            ALOGE("%d fatal",__LINE__);
            return -EINVAL;
    }


    //gettimeofday(&tpend2, NULL);
    //usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    //LOGD("hwc_syncs_set use time=%ld ms",  usec1); 
    

    hwc_policy_set(context,list);
#if hwcUseTime
    gettimeofday(&tpend2, NULL);
    usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    if(is_out_log())
        LOGD("set compositer %d layers use time=%ld ms", list->numHwLayers -1, usec1); 
#endif
    hwc_sync_release(list);
    return 0; //? 0 : HWC_EGL_ERROR;
}

static int hwc_set_external(hwc_composer_device_1_t *dev, hwc_display_contents_1_t* list)
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_EXTERNAL];
    static int black_cnt = 0;
#if hwcUseTime
    struct timeval tpend1, tpend2;
    long usec1 = 0;
#endif

    if(is_out_log())        
        LOGD("%s(%d):>>> hwc_set_external  <<<",
         __FUNCTION__,
         __LINE__ );

    /* Check device handle. */
    if (context == NULL || list == NULL)
    {
        LOGV("%s(%d):hotplug not well!", __FUNCTION__, __LINE__);
        return 0;
    }

    /* Check layer list. */
    if (false && list->skipflag/* || black_cnt < 5 || list->numHwLayers <=1*/)
    {

        hwc_sync_release(list);
        black_cnt ++;
        ALOGW("hwc skipflag!!!,list->numHwLayers=%d",list->numHwLayers);
        return 0;
    }
#if hwcUseTime
    gettimeofday(&tpend1, NULL);
#endif
   // hwc_sync(list);

    switch (context->composer_mode)
    {
        case HWC_VOP:
        case HWC_VOP_GPU:
        case HWC_GPU:
        case HWC_NODRAW_GPU_VOP:
            break;
        case HWC_RGA:
        case HWC_RGA_TRSM_VOP:
        case HWC_RGA_TRSM_GPU_VOP:
        case HWC_RGA_GPU_VOP:
        case HWC_VOP_RGA:
            hwc_sync(list);
            break;
        case HWC_CP_FB:
            break;
        default:
            ALOGW("ext:composer_mode error");
            return -EINVAL;
    }


    //gettimeofday(&tpend2, NULL);
    //usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    //LOGD("hwc_syncs_set use time=%ld ms",  usec1);


#if ONLY_USE_ONE_VOP
    if(context->mHtg.HtgOn && gcontextAnchor[1] && gcontextAnchor[1]->fb_blanked)
    {
        hwc_sync_release(list);
        return 0;
    }
#endif

    hwc_policy_set(context,list);
#if hwcUseTime
    gettimeofday(&tpend2, NULL);
    usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    LOGV("set compositer %d layers use time=%ld ms", list->numHwLayers -1, usec1);
#endif
    hwc_sync_release(list);
    return 0; //? 0 : HWC_EGL_ERROR;
}
int hwc_set(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t  ** displays
    )
{

    int ret = 0;
    for (uint32_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t* list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_primary(dev, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_set_external(dev, list);
                break;
            case HWC_DISPLAY_VIRTUAL:           
                ret = hwc_set_virtual(dev, displays);
                break;
            default:
                ret = -EINVAL;
        }
    }
    return ret;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
                              hwc_procs_t const* procs)
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    HWC_UNREFERENCED_PARAMETER(dev);

    context->procs = (hwc_procs_t *)procs;
}


static int hwc_event_control(struct hwc_composer_device_1* dev,
                             int dpy, int event, int enabled)
{

    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    HWC_UNREFERENCED_PARAMETER(dev);
    HWC_UNREFERENCED_PARAMETER(dpy);
    switch (event)
    {
        case HWC_EVENT_VSYNC:
            {
                int val = !!enabled;
                int err;

                err = ioctl(context->fbFd, RK_FBIOSET_VSYNC_ENABLE, &val);
                if (err < 0)
                {
                    LOGE(" RK_FBIOSET_VSYNC_ENABLE err=%d", err);
                    return -1;
                }
                return 0;
            }
        default:
            return -1;
    }
}

//struct timeval tpend1, tpend2;
void handle_hotplug_event(int mode ,int flag )
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    bool isNeedRemove = true;

#if defined(RK322X_BOX) || defined(RK_DONGLE)
    context->procs->invalidate(context->procs);
#endif

    if (mode == -1 && flag == -1) {
	LOGI("handle_hotplug_event invalidate");
        context->procs->invalidate(context->procs);
        return;
    }

#if defined(RK322X_BOX) || defined(RK_DONGLE)
    if(!context->mIsBootanimExit)
    {
        if(mode)
        {
            if(6 == flag)
            {
                context->mHtg.HtgOn = true;
                context->mHtg.HdmiOn = true;
                context->mHtg.CvbsOn = false;
            }
            else if (1 == flag)
            {
                context->mHtg.HtgOn = true;
                context->mHtg.CvbsOn = true;
                context->mHtg.HdmiOn = false;
            }
            hotplug_get_config(0);
            hotplug_set_config();
        }
        return;
    }
    if(context->mIsFirstCallbackToHotplug)
    {
        isNeedRemove = false;
        context->mIsFirstCallbackToHotplug = false;
    }
#endif

#if (defined(USE_X86) || HOTPLUG_MODE)
    switch(flag){
    case 0:
        if(context->mHtg.HtgOn && isNeedRemove){
#if HOTPLUG_MODE
            int count = 0;
            while(gcontextAnchor[1]->fb_blanked){
                count++;
                usleep(10000);
                if(1000==count){
                    ALOGW("wait for unblank");
                    break;
                }
            }
#endif
            if(context->mHtg.CvbsOn){
                context->mHtg.CvbsOn = false;
            }else{
                context->mHtg.HdmiOn = false;
            }
            context->mHtg.HtgOn = false;
            context->dpyAttr[HWC_DISPLAY_EXTERNAL].connected = false;
#if HOTPLUG_MODE
            context->procs->hotplug(context->procs, HWC_DISPLAY_EXTERNAL, 0);
#endif
            gcontextAnchor[1]->fb_blanked = 1;
            gcontextAnchor[1]->dpyAttr[HWC_DISPLAY_PRIMARY].connected = false;
            //hotplug_set_frame(context,0);
#if HOTPLUG_MODE
            ALOGI("remove hotplug device [%d,%d,%d]",__LINE__,mode,flag);
#else
            ALOGI("remove hdmi device [%d,%d,%d]",__LINE__,mode,flag);
#endif
        }
        if(mode){
            hotplug_get_config(0);
            hotplug_set_config();
            //if(6 == flag)
            //    context->mHdmiSI.HdmiOn = true;
            //else if(1 == flag)
            //    context->mHdmiSI.CvbsOn = true;
            //hotplug_set_frame(context,0);
            context->mHtg.HtgOn = true;
#if HOTPLUG_MODE
            char value[PROPERTY_VALUE_MAX];
            property_set("sys.hwc.htg","hotplug");
            context->procs->hotplug(context->procs, HWC_DISPLAY_EXTERNAL, 1);
            property_get("sys.hwc.htg",value,"hotplug");
            int count = 0;
            while(strcmp(value,"true")){
                count ++;
                if(count%3==0)
                    context->procs->hotplug(context->procs, HWC_DISPLAY_EXTERNAL, 0);
                context->procs->hotplug(context->procs, HWC_DISPLAY_EXTERNAL, 1);
                property_get("sys.hwc.htg",value,"hotplug");
                ALOGI("Trying to hotplug device[%d,%d,%d]",__LINE__,mode,flag);
            }
#if FORCE_REFRESH
	    pthread_mutex_lock(&context->mRefresh.mlk);
	    context->mRefresh.count = 0;
	    ALOGD_IF(is_out_log(),"Htg:mRefresh.count=%d",context->mRefresh.count);
	    pthread_mutex_unlock(&context->mRefresh.mlk);
	    pthread_cond_signal(&context->mRefresh.cond);
#endif
            ALOGI("connet to hotplug device[%d,%d,%d]",__LINE__,mode,flag);
#else
            ALOGI("connet to hdmi [%d,%d,%d]",__LINE__,mode,flag);
#endif   
        }
        break;

    default:
        break;
    }
#endif
    return;
}

static void handle_vsync_event(hwcContext * context)
{
    long usec1 = 0;

    if (!context->procs)
        return;

    int err = lseek(context->vsync_fd, 0, SEEK_SET);
    if (err < 0)
    {
        ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        return;
    }

    char buf[4096];
    err = read(context->vsync_fd, buf, sizeof(buf));
    if (err < 0)
    {
        ALOGE("error reading vsync timestamp: %s", strerror(errno));
        return;
    }
    buf[sizeof(buf) - 1] = '\0';

    //  errno = 0;
    uint64_t timestamp = strtoull(buf, NULL, 0) ;/*+ (uint64_t)(1e9 / context->fb_fps)  ;*/
    err = 0;
#if 0
    uint64_t mNextFakeVSync = timestamp + (uint64_t)(1e9 / context->fb_fps);
    err = 0;
    
    struct timespec spec;
    spec.tv_sec  = mNextFakeVSync / 1000000000;
    spec.tv_nsec = mNextFakeVSync % 1000000000;

    do
    {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    }
    while (err < 0 && errno == EINTR);
#else
    uint64_t mNextFakeVSync = timestamp;
#endif

    if (err == 0)
    {
        if(context->mTimeStamp == mNextFakeVSync) return;

        context->procs->vsync(context->procs, 0, mNextFakeVSync);
       // gettimeofday(&tpend2, NULL);
       // usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
       // LOGD("vysnc interval use time=%ld ms",  usec1); 

       // tpend1 = tpend2;

        //ALOGD(" timestamp=%lld ms,preid=%lld us",mNextFakeVSync/1000000,(uint64_t)(1e6 / context->fb_fps) );
    }
    else
    {
        ALOGE(" clock_nanosleep ERR!!!");
    }

    context->mTimeStamp = mNextFakeVSync;
}

static void *hwc_thread(void *data)
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    HWC_UNREFERENCED_PARAMETER(data);

    prctl(PR_SET_NAME,"HWC_vsync");

    char temp[4096];

    int err = read(context->vsync_fd, temp, sizeof(temp));
    if (err < 0)
    {
        ALOGE("error reading vsync timestamp: %s", strerror(errno));
        return NULL;
    }

    struct pollfd fds[1];
    fds[0].fd = context->vsync_fd;
    fds[0].events = POLLPRI;
    //fds[1].fd = uevent_get_fd();
    //fds[1].events = POLLIN;

    while (true)
    {
        int err = poll(fds, 1, -1);

        if (err > 0)
        {
            if (fds[0].revents & POLLPRI)
            {
                handle_vsync_event(context);
            }

        }
        else if (err == -1)
        {
            if (errno == EINTR)
                break;
            ALOGE("error in vsync thread: %s", strerror(errno));
        }
    }

    return NULL;
}



int
hwc_device_close(
    struct hw_device_t *dev
)
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    int err;
    LOGV("%s(%d):Close hwc device in thread=%d",
         __FUNCTION__, __LINE__, gettid());

    /* Check device. */
    if (context == NULL
            || &context->device.common != (hw_device_t *) dev
       )
    {
        LOGE("%s(%d):Invalid device!", __FUNCTION__, __LINE__);

        return -EINVAL;
    }

    if (--context->reference > 0)
    {
        /* Dereferenced only. */
        return 0;
    }

    if (context->engine_fd)
        close(context->engine_fd);
    /* Clean context. */
    if (context->vsync_fd > 0)
        close(context->vsync_fd);
    if (context->fbFd > 0)
    {
        close(context->fbFd);

    }
    if (context->fbFd1 > 0)
    {
        close(context->fbFd1);
    }

    if (context->ippDev)
    {
        ipp_close(context->ippDev);
    }

    for (int i = 0;i < FB_BUFFERS_NUM;i++)
    {
        if (context->phd_bk[i])
        {
            err = context->mAllocDev->free(context->mAllocDev, context->phd_bk[i]);
            ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
        }

    }

#if 0
    if (context->rk_ion_device)
    {
        context->rk_ion_device->free(context->rk_ion_device, context->pion);
        ion_close(context->rk_ion_device);
    }
#endif
    pthread_mutex_destroy(&context->lock);
    free(context);

    gcontextAnchor[HWC_DISPLAY_PRIMARY] = NULL;

    return 0;
}

static int hwc_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp,
                                 uint32_t* configs, size_t* numConfigs)
{
    int ret = 0;
    hwcContext * pdev = (hwcContext  *)dev;
    //in 1.1 there is no way to choose a config, report as config id # 0
    //This config is passed to getDisplayAttributes. Ignore for now.
    switch (disp)
    {

        case HWC_DISPLAY_PRIMARY:
            if (*numConfigs > 0)
            {
                configs[0] = 0;
                *numConfigs = 1;
            }
            ret = 0; //NO_ERROR
            break;
        case HWC_DISPLAY_EXTERNAL:
            ret = -1; //Not connected
            if (pdev->dpyAttr[HWC_DISPLAY_EXTERNAL].connected)
            {
                ret = 0; //NO_ERROR
                if (*numConfigs > 0)
                {
                    configs[0] = 0;
                    *numConfigs = 1;
                }
            }
            break;
    }
    return 0;
}

static int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
                                    uint32_t config, const uint32_t* attributes, int32_t* values)
{
    HWC_UNREFERENCED_PARAMETER(config);

    hwcContext  *pdev = (hwcContext  *)dev;
    //If hotpluggable displays are inactive return error
    if (disp == HWC_DISPLAY_EXTERNAL && !pdev->dpyAttr[disp].connected)
    {
        return -1;
    }
    static  uint32_t DISPLAY_ATTRIBUTES[] =
    {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };
    //From HWComposer

    const int NUM_DISPLAY_ATTRIBUTES = (sizeof(DISPLAY_ATTRIBUTES) / sizeof(DISPLAY_ATTRIBUTES)[0]);

    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++)
    {
        switch (attributes[i])
        {
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = pdev->dpyAttr[disp].vsync_period;
                break;
            case HWC_DISPLAY_WIDTH:
                values[i] = pdev->dpyAttr[disp].xres;
                ALOGD("%s disp = %d, width = %d", __FUNCTION__, disp,
                      pdev->dpyAttr[disp].xres);
                break;
            case HWC_DISPLAY_HEIGHT:
                values[i] = pdev->dpyAttr[disp].yres;
                ALOGD("%s disp = %d, height = %d", __FUNCTION__, disp,
                      pdev->dpyAttr[disp].yres);
                break;
            case HWC_DISPLAY_DPI_X:
                values[i] = (int32_t)(pdev->dpyAttr[disp].xdpi);
                break;
            case HWC_DISPLAY_DPI_Y:
                values[i] = (int32_t)(pdev->dpyAttr[disp].ydpi);
                break;
            default:
                ALOGE("Unknown display attribute %d",
                      attributes[i]);
                return -EINVAL;
        }
    }

    return 0;
}

int is_surport_wfd_optimize()
{
    char value[PROPERTY_VALUE_MAX];
    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("drm.service.enabled", value, "false");
    if (!strcmp(value, "false"))
    {
        return false;
    }
    else
    {
        return true;
    }
}

int hwc_copybit(struct hwc_composer_device_1 *dev, buffer_handle_t src_handle, buffer_handle_t dst_handle, int flag)
{
    return 0;
}

static void hwc_dump(struct hwc_composer_device_1* dev, char *buff, int buff_len)
{
    HWC_UNREFERENCED_PARAMETER(dev);
    HWC_UNREFERENCED_PARAMETER(buff);
    HWC_UNREFERENCED_PARAMETER(buff_len);
    // return 0;
}
int
hwc_device_open(
    const struct hw_module_t * module,
    const char * name,
    struct hw_device_t ** device
)
{
    int  status    = 0;
    int rel;
    int err;
    hwcContext * context = NULL;
    struct fb_fix_screeninfo fixInfo;
    struct fb_var_screeninfo info;
    int refreshRate = 0;
    float xdpi;
    float ydpi;
    uint32_t vsync_period;
    char pro_value[PROPERTY_VALUE_MAX];
    uint64_t inverseRefreshRate = 0;
#if !ONLY_USE_FB_BUFFERS
    int stride_gr;
#endif


    LOGD("%s(%d):Open hwc device in thread=%d",
         __FUNCTION__, __LINE__, gettid());

    *device = NULL;

    if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0)
    {
        LOGE("%s(%d):Invalid device name!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    /* Get context. */
    context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    /* Return if already initialized. */
    if (context != NULL)
    {
        /* Increament reference count. */
        context->reference++;

        *device = &context->device.common;
        return 0;
    }
   
    /* Allocate memory. */
    context = (hwcContext *) malloc(sizeof(hwcContext));

    if (context == NULL)
    {
        LOGE("%s(%d):malloc Failed!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    memset(context, 0, sizeof(hwcContext));

    context->fbFd = open("/dev/graphics/fb0", O_RDWR, 0);
    if (context->fbFd < 0)
    {
        hwcONERROR(hwcSTATUS_IO_ERR);
    }

    rel = ioctl(context->fbFd, FBIOGET_FSCREENINFO, &fixInfo);
    if (rel != 0)
    {
        hwcONERROR(hwcSTATUS_IO_ERR);
    }



    if (ioctl(context->fbFd, FBIOGET_VSCREENINFO, &info) == -1)
    {
        hwcONERROR(hwcSTATUS_IO_ERR);
    }

    xdpi = 1000 * (info.xres * 25.4f) / info.width;
    ydpi = 1000 * (info.yres * 25.4f) / info.height;


    inverseRefreshRate = uint64_t(info.upper_margin + info.lower_margin + info.yres)
                   * (info.left_margin  + info.right_margin + info.xres)
                   * info.pixclock;

    ALOGD("v[%d,%d,%d],h[%d,%d,%d],c[%d][%d,%dmm]", info.upper_margin, info.lower_margin,
			info.yres, info.left_margin, info.right_margin,info.xres,
			info.pixclock, info.width, info.height);


    if (inverseRefreshRate)
	    refreshRate = 1000000000000LLU / inverseRefreshRate;
    else
	    refreshRate = 0;

    if (refreshRate == 0)
    {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60 * 1000;
    }


    vsync_period  = 1000000000 / refreshRate;

    vsync_period = hwc_get_vsync_period_from_fb_fps(0);

    context->dpyAttr[HWC_DISPLAY_PRIMARY].fd = context->fbFd;
    //xres, yres may not be 32 aligned
    context->dpyAttr[HWC_DISPLAY_PRIMARY].stride = fixInfo.line_length / (info.xres / 8);
    context->dpyAttr[HWC_DISPLAY_PRIMARY].xres = info.xres;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].yres = info.yres;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].xdpi = xdpi;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].ydpi = ydpi;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period = vsync_period;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].connected = true;
    context->info = info;

    /* Initialize variables. */

    context->device.common.tag = HARDWARE_DEVICE_TAG;
    context->device.common.version = HWC_DEVICE_API_VERSION_1_3;

    context->device.common.module  = (hw_module_t *) module;

    /* initialize the procs */
    context->device.common.close   = hwc_device_close;
    context->device.prepare        = hwc_prepare;
    context->device.set            = hwc_set;
    // context->device.common.version = HWC_DEVICE_API_VERSION_1_0;
    context->device.blank          = hwc_blank;
    context->device.query          = hwc_query;
    context->device.eventControl   = hwc_event_control;

    context->device.registerProcs  = hwc_registerProcs;

    context->device.getDisplayConfigs = hwc_getDisplayConfigs;
    context->device.getDisplayAttributes = hwc_getDisplayAttributes;
    context->device.rkCopybit = hwc_copybit;

    context->device.dump = hwc_dump;

    context->membk_index = 0;

    context->engine_fd = open("/dev/rga", O_RDWR, 0);
    if (context->engine_fd < 0)
    {
#ifdef TARGET_BOARD_PLATFORM_RK3036
	    ALOGE("RGA no exit");
#else
	    hwcONERROR(hwcRGA_OPEN_ERR);
#endif
    }

#if ENABLE_WFD_OPTIMIZE
    property_set("sys.enable.wfd.optimize", "1");
#endif

#if defined(RK_DONGLE)
    if (context->engine_fd > 0 ) {
	close(context->engine_fd);
	context->engine_fd = -1;
    }
#endif

    if (context->engine_fd > 0) {
        int ret = 0;
        char value[PROPERTY_VALUE_MAX];
        memset(value, 0, PROPERTY_VALUE_MAX);
        property_get("sys.enable.wfd.optimize", value, "0");
        int type = atoi(value);
        context->wfdOptimize = type;
        ret = init_rga_cfg(context->engine_fd);
        if (type > 0 && !is_surport_wfd_optimize() && !ret)
        {
            property_set("sys.enable.wfd.optimize", "0");
        }
    }
    property_set("sys.yuv.rgb.format", "4");
    /* Initialize pmem and frameubffer stuff. */
    // context->fbFd         = 0;
    // context->fbPhysical   = ~0U;
    // context->fbStride     = 0;


    if (info.pixclock > 0)
    {
        refreshRate = 1000000000000000LLU /
                      (
                          uint64_t(info.vsync_len + info.upper_margin + info.lower_margin + info.yres)
                          * (info.hsync_len + info.left_margin  + info.right_margin + info.xres)
                          * info.pixclock
                      );
    }
    else
    {
        ALOGW("fbdev pixclock is zero");
    }

    if (refreshRate == 0)
    {
        refreshRate = 60 * 1000;  // 60 Hz
    }

    context->fb_fps = refreshRate / 1000.0f;

    context->fbPhysical = fixInfo.smem_start;
    context->fbStride   = fixInfo.line_length;
    context->fbWidth = info.xres;
    context->fbHeight = info.yres;
    context->fbhandle.width = info.xres;
    context->fbhandle.height = info.yres;
    context->fbhandle.format = info.nonstd & 0xff;
    context->fbhandle.stride = (info.xres + 31) & (~31);
    context->pmemPhysical = ~0U;
    context->pmemLength   = 0;
    context->bootCount    = 0;
    context->scaleFd      = -1;
    context->mIsBootanimExit = false;
    context->mIsFirstCallbackToHotplug = false;
    context->mScreenChanged = false;
    property_get("ro.rk.soc", pro_value, "0");
    context->IsRk3188 = !!strstr(pro_value, "rk3188");
    context->IsRk3126 = !!strstr(pro_value, "rk3126");
    context->IsRk3128 = !!strstr(pro_value, "rk3128");
    context->IsRk322x = !!strstr(pro_value, "rk322");
    property_get("ro.target.product", pro_value, "0");
    context->IsRkBox = !strcmp(pro_value, "box");

    initPlatform(context);

    context->fbSize = context->fbStride * info.yres * 3;//info.xres*info.yres*4*3;
    context->lcdSize = context->fbStride * info.yres;//info.xres*info.yres*4;

    if (context->engine_fd > 0)
    {

#if !ONLY_USE_FB_BUFFERS
        hw_module_t const* module_gr;

        err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module_gr);
        ALOGE_IF(err, "FATAL: can't find the %s module", GRALLOC_HARDWARE_MODULE_ID);
        if (err == 0)
        {
            gralloc_open(module_gr, &context->mAllocDev);

            for (int  i = 0;i < FB_BUFFERS_NUM;i++)
            {
                err = context->mAllocDev->alloc(context->mAllocDev,rkmALIGN(info.xres,32), \
                                                info.yres, context->fbhandle.format, \
                                                GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER, \
                                                (buffer_handle_t*)(&context->phd_bk[i]), &stride_gr);
                if (!err)
                {
                    struct private_handle_t*phandle_gr = (struct private_handle_t*)context->phd_bk[i];
                    context->membk_fds[i] = phandle_gr->share_fd;
                    context->membk_base[i] = phandle_gr->base;	    
		    #if !defined(TARGET_BOARD_PLATFORM_RK3328)
                    context->membk_type[i] = phandle_gr->type;
		    #endif
                    ALOGD("@hwc alloc [%dx%d,f=%d],fd=%d", phandle_gr->width, phandle_gr->height, phandle_gr->format, phandle_gr->share_fd);
                   
                }
                else
                { 
                    ALOGE("hwc alloc faild");
                    goto OnError;
                }
            }
        }
        else
        {
            ALOGE(" GRALLOC_HARDWARE_MODULE_ID failed");
        }
#endif

    }

    /* Increment reference count. */
    context->reference++; 
    context->fun_policy[HWC_VOP] = try_hwc_vop_policy;
    context->fun_policy[HWC_RGA] = try_hwc_rga_policy;
    context->fun_policy[HWC_VOP_RGA] = try_hwc_vop_rga_policy;
    context->fun_policy[HWC_RGA_VOP] = try_hwc_rga_vop_policy;
    context->fun_policy[HWC_RGA_TRSM_VOP] = try_hwc_rga_trfm_vop_policy  ;
    context->fun_policy[HWC_RGA_TRSM_GPU_VOP] = try_hwc_rga_trfm_gpu_vop_policy;
    context->fun_policy[HWC_VOP_GPU] = try_hwc_vop_gpu_policy;
    context->fun_policy[HWC_NODRAW_GPU_VOP] = try_hwc_nodraw_gpu_vop_policy;
    context->fun_policy[HWC_GPU_NODRAW_VOP] = try_hwc_gpu_nodraw_vop_policy;
    context->fun_policy[HWC_RGA_GPU_VOP] = try_hwc_rga_gpu_vop_policy;
    context->fun_policy[HWC_GPU_VOP] = try_hwc_gpu_vop_policy;
    context->fun_policy[HWC_CP_FB] = try_hwc_cp_fb_policy;
    context->fun_policy[HWC_GPU] = try_hwc_gpu_policy;
    if(context->fbWidth * context->fbHeight >= 1920*1080 )
        context->vop_mbshake = true;
    context->fb_blanked = 1;    
    gcontextAnchor[HWC_DISPLAY_PRIMARY] = context;
    if (context->fbWidth > context->fbHeight)
    {
        property_set("sys.display.oritation", "0");
    }
    else
    {
        property_set("sys.display.oritation", "2");
    }

    rel = ioctl(context->fbFd, RK_FBIOGET_IOMMU_STA, &context->iommuEn);	    
    if (rel != 0)
    {
         hwcONERROR(hwcSTATUS_IO_ERR);
    }    

#if USE_HW_VSYNC

    context->vsync_fd = open("/sys/class/graphics/fb0/vsync", O_RDONLY, 0);
    //context->vsync_fd = open("/sys/devices/platform/rk30-lcdc.0/vsync", O_RDONLY);
    if (context->vsync_fd < 0)
    {
        hwcONERROR(hwcSTATUS_IO_ERR);
    }


    if (pthread_mutex_init(&context->lock, NULL))
    {
        hwcONERROR(hwcMutex_ERR);
    }

    if (pthread_create(&context->hdmi_thread, NULL, hwc_thread, context))
    {
        hwcONERROR(hwcTHREAD_ERR);
    }
#endif

    /* Return device handle. */
    *device = &context->device.common;

    LOGD("RGA HWComposer verison%s\n"
         "Device:               %p\n"
         "fb_fps=%f",
         "1.0.0",
         context,
         context->fb_fps);

    char acVersion[100];
    memset(acVersion,0,sizeof(acVersion));
    if(sizeof(GHWC_VERSION) > 12)
        strncpy(acVersion,GHWC_VERSION,12);
    else
        strcpy(acVersion,GHWC_VERSION);
#ifndef TARGET_SECVM
    strcat(acVersion,"");
#else
    strcat(acVersion,"_sec");
#endif
#if HOTPLUG_MODE
    strcat(acVersion,"_htg");
#else
    strcat(acVersion,"");
#endif
    strcat(acVersion,"");
    strcat(acVersion,RK_GRAPHICS_VER);
    property_set("sys.ghwc.version", acVersion);

    LOGD(RK_GRAPHICS_VER);

    if (context->engine_fd > 0) {
        char Version[32];

        memset(Version, 0, sizeof(Version));
        if (ioctl(context->engine_fd, RGA_GET_VERSION, Version) == 0)
        {
            property_set("sys.grga.version", Version);
            LOGD(" rga version =%s", Version);

        }
    }
    else
    {
        LOGD(" rga not exit");
    }
    context->ippDev = new ipp_device_t();
    rel = ipp_open(context->ippDev);
    if (rel < 0)
    {
        delete context->ippDev;
        context->ippDev = NULL;
        ALOGE("Open ipp device fail.");
    }
    init_hdmi_mode();
    pthread_t t;
    if (pthread_create(&t, NULL, rk_hwc_hdmi_thread, NULL))
    {
        LOGD("Create readHdmiMode thread error .");
    }
#if (defined(USE_X86) || HOTPLUG_MODE)
#if defined(RK322X_BOX) || defined(RK_DONGLE)
    if(context->IsRk322x || context->IsRk3036)
#else
    if(getHdmiMode())
#endif
    {
        pthread_t t0;
        if (pthread_create(&t0, NULL, hotplug_init_thread, NULL))
        {
            LOGD("Create hotplug_init_thread error .");
        }
    }
#endif
#if FORCE_REFRESH
    pthread_t t1;
    init_thread_pamaters(&context->mRefresh);
    if (pthread_create(&t1, NULL, hotplug_invalidate_refresh, NULL))
    {
        LOGD("Create hotplug_invalidate_refresh error .");
    }
#endif
    init_tv_hdr_info(context);
    context->deviceConected = getHdmiMode();
    hotplug_change_screen_config(HWC_DISPLAY_PRIMARY, 0, 1);
    return 0;

OnError:

    if (context->vsync_fd > 0)
    {
        close(context->vsync_fd);
    }
    if (context->fbFd > 0)
    {
        close(context->fbFd);

    }
    if (context->fbFd1 > 0)
    {
        close(context->fbFd1);
    }


    for (int i = 0;i < FB_BUFFERS_NUM;i++)
    {
        if (context->phd_bk[i])
        {
            err = context->mAllocDev->free(context->mAllocDev, context->phd_bk[i]);
            ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
        }

    }

    pthread_mutex_destroy(&context->lock);

    /* Error roll back. */
    if (context != NULL)
    {
        if (context->engine_fd != 0)
        {
            close(context->engine_fd);
        }
        free(context);

    }

    *device = NULL;
    #if FORCE_REFRESH
    free_thread_pamaters(&context->mRefresh);
    #endif
    LOGE("%s(%d):Failed!", __FUNCTION__, __LINE__);

    return -EINVAL;
}

int  getHdmiMode()
{
#if 0
    char pro_value[16];
    property_get("sys.hdmi.mode", pro_value, 0);
    int mode = atoi(pro_value);
    return mode;
#else
    // LOGD("g_hdmi_mode=%d",g_hdmi_mode);
#endif
    return g_hdmi_mode;
    // return 0;
}

void init_hdmi_mode()
{

    int fd = open("/sys/devices/virtual/switch/hdmi/state", O_RDONLY);
    //int fd = 0;
    if (fd > 0)
    {
        char statebuf[100];
        memset(statebuf, 0, sizeof(statebuf));
        int err = read(fd, statebuf, sizeof(statebuf));

        if (err < 0)
        {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }
        close(fd);
        g_hdmi_mode = atoi(statebuf);
        ALOGD("g_hdmi_mode is %d",g_hdmi_mode);

    }
    else
    {
        LOGE("Open hdmi mode error.");
    }

}

int hotplug_parse_screen(int *outX, int *outY)
{
    char buf[100];
    int width = 0;
    int height = 0;
    int fdExternal = -1;
    fdExternal = open("/sys/class/graphics/fb2/screen_info", O_RDONLY);
    if(fdExternal < 0){
        ALOGE("hotplug_get_config:open fb screen_info error,cvbsfd=%d",fdExternal);
        return -errno;
	}
    if(read(fdExternal,buf,sizeof(buf)) < 0){
        ALOGE("error reading fb screen_info: %s", strerror(errno));
        return -1;
    }
    close(fdExternal);
	sscanf(buf,"xres:%d yres:%d",&width,&height);
    ALOGD("hotplug_get_config:width=%d,height=%d",width,height);
    *outX = width;
    *outY = height;
    return 0;
}

int hotplug_parse_mode(int *outX, int *outY)
{
   int fd = open("/sys/class/display/HDMI/mode", O_RDONLY);
   ALOGD("enter %s", __FUNCTION__);

   if (fd > 0)
   {
        char statebuf[100];
        memset(statebuf, 0, sizeof(statebuf));
        int err = read(fd, statebuf, sizeof(statebuf));
        if (err < 0)
        {
            ALOGE("error reading hdmi mode: %s", strerror(errno));
            return -1;
        }
        //ALOGD("statebuf=%s",statebuf);
        close(fd);
        char xres[10];
        char yres[10];
        int temp = 0;
        memset(xres, 0, sizeof(xres));
        memset(yres, 0, sizeof(yres));
        for (unsigned int i=0; i<strlen(statebuf); i++)
        {
            if (statebuf[i] >= '0' && statebuf[i] <= '9')
            {
                xres[i] = statebuf[i];
            }
            else
            {
                temp = i;
                break;
            }
        }
        int m = 0;
        for (unsigned int j=temp+1; j<strlen(statebuf);j++)
        {
            if (statebuf[j] >= '0' && statebuf[j] <= '9')
            {
                yres[m] = statebuf[j];
                m++;
            }
            else
            {
                break;
            }
        }
        *outX = atoi(xres);
        *outY = atoi(yres);
        close(fd);
        return 0;
    }
    else
    {
        close(fd);
        ALOGE("Get HDMI mode fail");
        return -1;
    }
}

int hotplug_get_config(int flag)
{
    uint64_t inverseRefreshRate = 0;
    int  status    = 0;
    int rel;
    int err;
    hwcContext * context = NULL;
    hwcContext * contextp = NULL;
    struct fb_fix_screeninfo fixInfo;
    struct fb_var_screeninfo info;
    int refreshRate = 0;
    float xdpi;
    float ydpi;
    uint32_t vsync_period;
    char pro_value[PROPERTY_VALUE_MAX];
#if !ONLY_USE_FB_BUFFERS
    int stride_gr;
#endif

    /* Get context. */
    context = gcontextAnchor[HWC_DISPLAY_EXTERNAL];
    contextp = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    if(context == NULL ){
        context = (hwcContext *) malloc(sizeof (hwcContext));
        if(context==NULL){
            ALOGE("hotplug_get_config:Alloc context fail");
            return -1;
        }
        memset(context, 0, sizeof (hwcContext));
    }

    if (context == NULL){
        LOGE("%s(%d):malloc Failed!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

#ifdef USE_X86
    if(gcontextAnchor[HWC_DISPLAY_PRIMARY]->fbFd > 0)
        context->fbFd = gcontextAnchor[HWC_DISPLAY_PRIMARY]->fbFd;
    else{
        context->fbFd = open("/dev/graphics/fb0", O_RDWR, 0);
        if (context->fbFd < 0){
            hwcONERROR(hwcSTATUS_IO_ERR);
        }
    }
#else
    if(context->fbFd > 0)
        context->fbFd = context->fbFd;
    else{
        context->fbFd = open("/dev/graphics/fb2", O_RDWR, 0);
        if (context->fbFd < 0){
            hwcONERROR(hwcSTATUS_IO_ERR);
        }
    }
#endif
    rel = ioctl(context->fbFd, FBIOGET_FSCREENINFO, &fixInfo);
    if (rel != 0){
        hwcONERROR(hwcSTATUS_IO_ERR);
    }

    if (ioctl(context->fbFd, FBIOGET_VSCREENINFO, &info) == -1){
        hwcONERROR(hwcSTATUS_IO_ERR);
    }

    int xres,yres;
    if(contextp->IsRk322x)
        hotplug_parse_screen(&xres, &yres);
    else
        hotplug_parse_mode(&xres, &yres);
    info.xres = xres;
    info.yres = yres;

    xdpi = 1000 * (info.xres * 25.4f) / info.width;
    ydpi = 1000 * (info.yres * 25.4f) / info.height;

    inverseRefreshRate = uint64_t(info.upper_margin + info.lower_margin + info.yres)
	    * (info.left_margin  + info.right_margin + info.xres)
	    * info.pixclock;

    if (inverseRefreshRate)
	    refreshRate = 1000000000000LLU / inverseRefreshRate;
    else
	    refreshRate = 0;

    if (refreshRate == 0){
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60 * 1000;
    }


    vsync_period  = 1000000000 / refreshRate;
    context->fb_blanked = 1;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].fd  = context->fbFd;
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].fd = context->fbFd;
    //xres, yres may not be 32 aligned
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].stride = fixInfo.line_length / (info.xres / 8);
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].xres = info.xres;
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].yres = info.yres;
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].xdpi = xdpi;
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].ydpi = ydpi;
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].vsync_period = vsync_period;
    context->dpyAttr[HWC_DISPLAY_EXTERNAL].connected = true;
    context->dpyAttr[HWC_DISPLAY_PRIMARY].connected  = true;
    context->info = info;

    /* Initialize variables. */

    context->device.common.tag = HARDWARE_DEVICE_TAG;
    context->device.common.version = HWC_DEVICE_API_VERSION_1_3;

    /* initialize the procs */
    context->device.common.close   = hwc_device_close;
    context->device.prepare        = hwc_prepare;
    context->device.set            = hwc_set;
    // context->device.common.version = HWC_DEVICE_API_VERSION_1_0;
    context->device.blank          = hwc_blank;
    context->device.query          = hwc_query;
    context->device.eventControl   = hwc_event_control;

    context->device.registerProcs  = hwc_registerProcs;

    context->device.getDisplayConfigs = hwc_getDisplayConfigs;
    context->device.getDisplayAttributes = hwc_getDisplayAttributes;
    context->device.rkCopybit = hwc_copybit;

    context->device.dump = hwc_dump;

    context->membk_index = 0;

    if(gcontextAnchor[HWC_DISPLAY_PRIMARY]->engine_fd > 0)
        context->engine_fd = gcontextAnchor[HWC_DISPLAY_PRIMARY]->engine_fd;
    else
#if RK_DONGLE
	ALOGD("RGA no exit");
#else
        hwcONERROR(hwcRGA_OPEN_ERR);
#endif


#if ENABLE_WFD_OPTIMIZE
    property_set("sys.enable.wfd.optimize", "1");
#endif
    if (context->engine_fd > 0) {
	int ret = 0;
        char value[PROPERTY_VALUE_MAX];
        memset(value, 0, PROPERTY_VALUE_MAX);
        property_get("sys.enable.wfd.optimize", value, "0");
        int type = atoi(value);
        context->wfdOptimize = type;
        init_rga_cfg(context->engine_fd);
        if (type > 0 && !is_surport_wfd_optimize() && !ret) {
            property_set("sys.enable.wfd.optimize", "0");
        }
    }
    property_set("sys.yuv.rgb.format", "4");
    /* Initialize pmem and frameubffer stuff. */
    // context->fbFd         = 0;
    // context->fbPhysical   = ~0U;
    // context->fbStride     = 0;


    if (info.pixclock > 0){
        refreshRate = 1000000000000000LLU /
                      (
                          uint64_t(info.vsync_len + info.upper_margin + info.lower_margin + info.yres)
                          * (info.hsync_len + info.left_margin  + info.right_margin + info.xres)
                          * info.pixclock
                      );
    }else{
        ALOGW("fbdev pixclock is zero");
    }

    if (refreshRate == 0){
        refreshRate = 60 * 1000;  // 60 Hz
    }

    context->fb_fps = refreshRate / 1000.0f;

    context->fbPhysical = fixInfo.smem_start;
    context->fbStride   = fixInfo.line_length;
    context->fbWidth = info.xres;
    context->fbHeight = info.yres;
    context->fbhandle.width = info.xres;
    context->fbhandle.height = info.yres;
    context->fbhandle.format = info.nonstd & 0xff;
    context->fbhandle.stride = (info.xres + 31) & (~31);
    context->pmemPhysical = ~0U;
    context->pmemLength   = 0;
    property_get("ro.rk.soc", pro_value, "0");
    context->IsRk3188 = !!strstr(pro_value, "rk3188");
    context->IsRk3126 = !!strstr(pro_value, "rk3126");
    context->IsRk3128 = !!strstr(pro_value, "rk3128");
    context->IsRk322x = !!strstr(pro_value, "rk322");
    property_get("ro.target.product", pro_value, "0");
    context->IsRkBox = !strcmp(pro_value, "box");

    initPlatform(context);

    context->fbSize = context->fbStride * info.yres * 3;//info.xres*info.yres*4*3;
    context->lcdSize = context->fbStride * info.yres;//info.xres*info.yres*4;
	context->fb_blanked = 1;
    if (context->engine_fd > 0) {

#if 0//!ONLY_USE_FB_BUFFERS
        hw_module_t const* module_gr;

        err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module_gr);
        ALOGE_IF(err, "FATAL: can't find the %s module", GRALLOC_HARDWARE_MODULE_ID);
        if (err == 0)
        {
            gralloc_open(module_gr, &context->mAllocDev);

            for (int  i = 0;i < FB_BUFFERS_NUM;i++)
            {
                err = context->mAllocDev->alloc(context->mAllocDev,rkmALIGN(info.xres,32), \
                                                info.yres, context->fbhandle.format, \
                                                GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER, \
                                                (buffer_handle_t*)(&context->phd_bk[i]), &stride_gr);
                if (!err)
                {
                    struct private_handle_t*phandle_gr = (struct private_handle_t*)context->phd_bk[i];
                    context->membk_fds[i] = phandle_gr->share_fd;
                    context->membk_base[i] = phandle_gr->base;
                    context->membk_type[i] = phandle_gr->type;
                    ALOGD("@hwc alloc [%dx%d,f=%d],fd=%d", phandle_gr->width, phandle_gr->height,
                        phandle_gr->format, phandle_gr->share_fd);
                }
                else
                {
                    ALOGE("hwc alloc faild");
                    goto OnError;
                }
            }
        }
        else
        {
            ALOGE(" GRALLOC_HARDWARE_MODULE_ID failed");
        }
#endif

    }

    /* Increment reference count. */
    context->reference++;
#ifdef USE_X86
    context->fun_policy[HWC_VOP] = try_hwc_vop_policy;
    context->fun_policy[HWC_RGA] = try_hwc_rga_policy;
    context->fun_policy[HWC_VOP_RGA] = try_hwc_vop_rga_policy;
    context->fun_policy[HWC_RGA_VOP] = try_hwc_rga_vop_policy;
    context->fun_policy[HWC_RGA_TRSM_VOP] = try_hwc_rga_trfm_vop_policy  ;
    context->fun_policy[HWC_RGA_TRSM_GPU_VOP] = try_hwc_rga_trfm_gpu_vop_policy;
    context->fun_policy[HWC_VOP_GPU] = try_hwc_vop_gpu_policy;
    context->fun_policy[HWC_NODRAW_GPU_VOP] = try_hwc_nodraw_gpu_vop_policy;
    context->fun_policy[HWC_GPU_NODRAW_VOP] = try_hwc_skip_policy;
    context->fun_policy[HWC_RGA_GPU_VOP] = try_hwc_rga_gpu_vop_policy;
    context->fun_policy[HWC_GPU_VOP] = try_hwc_gpu_vop_policy;
    context->fun_policy[HWC_CP_FB] = try_hwc_cp_fb_policy;
    context->fun_policy[HWC_GPU] = try_hwc_gpu_policy;
#else
    context->fun_policy[HWC_VOP] = try_hwc_vop_policy;
    context->fun_policy[HWC_RGA] = try_hwc_skip_policy;
    context->fun_policy[HWC_VOP_RGA] = try_hwc_skip_policy;
    context->fun_policy[HWC_RGA_VOP] = try_hwc_skip_policy;
    context->fun_policy[HWC_RGA_TRSM_VOP] = try_hwc_skip_policy  ;
    context->fun_policy[HWC_RGA_TRSM_GPU_VOP] = try_hwc_skip_policy;
    context->fun_policy[HWC_VOP_GPU] = try_hwc_vop_gpu_policy;
    context->fun_policy[HWC_NODRAW_GPU_VOP] = try_hwc_nodraw_gpu_vop_policy;
    context->fun_policy[HWC_GPU_NODRAW_VOP] = try_hwc_skip_policy;
    context->fun_policy[HWC_RGA_GPU_VOP] = try_hwc_skip_policy;
    context->fun_policy[HWC_GPU_VOP] = try_hwc_skip_policy;
    context->fun_policy[HWC_CP_FB] = try_hwc_cp_fb_policy;
    context->fun_policy[HWC_GPU] = try_hwc_gpu_policy;
#endif
    if(context->fbWidth * context->fbHeight >= 1920*1080 )
        context->vop_mbshake = true;
    gcontextAnchor[HWC_DISPLAY_EXTERNAL] = context;

    rel = ioctl(context->fbFd, RK_FBIOGET_IOMMU_STA, &context->iommuEn);
    if (rel != 0){
         hwcONERROR(hwcSTATUS_IO_ERR);
    }

    /*----------- IPP not used by extern device*/
    /* 
    context->ippDev = new ipp_device_t();
    rel = ipp_open(context->ippDev);
    if (rel < 0){
        delete context->ippDev;
        context->ippDev = NULL;
        ALOGE("Open ipp device fail.");
    }
    */
    return 0;

OnError:

    if (context->vsync_fd > 0)
    {
        close(context->vsync_fd);
    }
    if (context->fbFd > 0)
    {
        close(context->fbFd);

    }
    if (context->fbFd1 > 0)
    {
        close(context->fbFd1);
    }


    for (int i = 0;i < FB_BUFFERS_NUM;i++)
    {
        if (context->phd_bk[i])
        {
            err = context->mAllocDev->free(context->mAllocDev, context->phd_bk[i]);
            ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
        }

    }

    pthread_mutex_destroy(&context->lock);

    /* Error roll back. */
    if (context != NULL)
    {
        if (context->engine_fd != 0)
        {
            close(context->engine_fd);
        }
        free(context);

    }

    LOGE("%s(%d):Failed!", __FUNCTION__, __LINE__);

    return -EINVAL;
}

int hotplug_set_config()
{
    int dType = HWC_DISPLAY_EXTERNAL;
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    hwcContext * context1 = gcontextAnchor[HWC_DISPLAY_EXTERNAL];
    if (context1 != NULL) {
        int xres = context1->dpyAttr[dType].xres;
        int yres = context1->dpyAttr[dType].yres;
        context->dpyAttr[dType].fd = context1->dpyAttr[dType].fd;
        context->dpyAttr[dType].stride = context1->dpyAttr[dType].stride;
        context->dpyAttr[dType].xres = context1->dpyAttr[dType].xres;
        context->dpyAttr[dType].yres = context1->dpyAttr[dType].yres;
        context->dpyAttr[dType].xdpi = context1->dpyAttr[dType].xdpi;
        context->dpyAttr[dType].ydpi = context1->dpyAttr[dType].ydpi;
        context->dpyAttr[dType].vsync_period = context1->dpyAttr[dType].vsync_period;
        context->dpyAttr[dType].connected = true;
        if (VIRTUAL_UI_RESOLUTION && (xres * yres > 2073600)) {
            context->dpyAttr[dType].xres = xres * 1080 / yres;
            context->dpyAttr[dType].yres = 1080;
            context1->mIsVirUiResolution = true;
        } else
            context1->mIsVirUiResolution = false;

        return 0;
    } else {
        context->dpyAttr[dType].connected = false;
        return -1;
    }
}

int hotplug_parse_scale(unsigned int *outX,unsigned int *outY)
{
    int fd = -1;
    char buf[100] = {0};
    unsigned int xscale = 0;
    unsigned int yscale = 0;
    hwcContext *ctxp = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    hwcContext *ctxe = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    if(ctxp->scaleFd < 0)
    {
        fd = open("/sys/class/display/HDMI/scale", O_RDONLY);
        ctxp->scaleFd = fd;
    }
    else
    {
        fd = ctxp->scaleFd;
    }

    ctxp->scaleFd = -1;//bug: for we need open the node every time
    if(ctxe && ctxp->scaleFd != ctxe->scaleFd) ctxe->scaleFd = fd;

    if(fd < 0){
        ALOGE("open HDMI scale fail for:%s,fd=%d",strerror(errno),fd);
        return -errno;
    }
    if(read(fd,buf,sizeof(buf)) < 0) {
        close(fd);
        ALOGE("error reading HDMI scale: %s", strerror(errno));
        return -errno;
    }

    close(fd);
    sscanf(buf,"xscale=%d yscale=%d",&xscale,&yscale);

    if(xscale == yscale && yscale == 0)
    {
        xscale = hwc_get_int_property("sys.hwc.xscale", "96");
        yscale = hwc_get_int_property("sys.hwc.yscale", "96");
        if(is_out_log()) ALOGI("Get x y scale from property");
    }
    if(is_out_log())
        ALOGI("[%d]%s,xscale=%d,yscale=%d",sizeof(buf),buf,xscale,yscale);
    *outX = xscale;
    *outY = yscale;
    return 0;
}

/*
@param:fb_info
@param:flag  0:for box,primary pos to externel pos;
             1:for 312x overscan
             2:for 322x overscan
             3:use when ui is the virtual resolution
*/
int hotplug_reset_dstpos(struct rk_fb_win_cfg_data * fb_info,int flag)
{
    float lscale = 0;
    float tscale = 0;
    float rscale = 0;
    float bscale = 0;

    unsigned int xoffset = 0;
    unsigned int yoffset = 0;
    unsigned int xpersent = 0;
    unsigned int ypersent = 0;

    unsigned int lpersent = 0;
    unsigned int tpersent = 0;
    unsigned int rpersent = 0;
    unsigned int bpersent = 0;

    unsigned int w_source = 0;
    unsigned int h_source = 0;
    unsigned int w_dstpos = 0;
    unsigned int h_dstpos = 0;

    char new_valuep[PROPERTY_VALUE_MAX];
    char new_valuee[PROPERTY_VALUE_MAX];

    hwcContext * ctxp = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    hwcContext * ctxe = gcontextAnchor[HWC_DISPLAY_EXTERNAL];

    switch(flag){
    case 0:
        w_source = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        h_source = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
        w_dstpos = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].xres;
        h_dstpos = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].yres;
        break;

    case 1:
        w_source = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].xres;
        h_source = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].yres;
        hotplug_parse_scale(&xpersent,&ypersent);

        if(xpersent < 80) xpersent = 80;
        if(ypersent < 80) ypersent = 80;

        if(xpersent > 100) xpersent = 100;
        if(ypersent > 100) ypersent = 100;

        xpersent = (100 - xpersent) / 2;
        ypersent = (100 - ypersent) / 2;

        xoffset = (unsigned int)(((float)xpersent / 100) * w_source);
        yoffset = (unsigned int)(((float)ypersent / 100) * w_source);
        break;

    case 2:
        w_source = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].xres;
        h_source = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].yres;

        property_get("persist.sys.overscan.main", new_valuep, "false");
        property_get("persist.sys.overscan.aux",  new_valuee, "false");

        sscanf(new_valuep,"overscan %d,%d,%d,%d",&lpersent,&tpersent,&rpersent,&bpersent);

        if(lpersent < 80) lpersent = 80;
        if(tpersent < 80) tpersent = 80;
        if(rpersent < 80) rpersent = 80;
        if(bpersent < 80) bpersent = 80;

        if(lpersent > 100) lpersent = 100;
        if(tpersent > 100) tpersent = 100;
        if(rpersent > 100) rpersent = 100;
        if(bpersent > 100) bpersent = 100;

        lpersent = (100 - lpersent) / 2;
        tpersent = (100 - tpersent) / 2;
        rpersent = (100 - rpersent) / 2;
        bpersent = (100 - bpersent) / 2;

        lscale = ((float)lpersent / 100);
        tscale = ((float)tpersent / 100);
        lscale = ((float)lpersent / 100);
        tscale = ((float)tpersent / 100);
        ALOGD_IF(is_out_log()>2,"%f,%f,%f,%f",lscale,tscale,rscale,bscale);
        break;

    case 3:
        w_source = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].xres;
        h_source = ctxp->dpyAttr[HWC_DISPLAY_EXTERNAL].yres;
        w_dstpos = ctxe->dpyAttr[HWC_DISPLAY_EXTERNAL].xres;
        h_dstpos = ctxe->dpyAttr[HWC_DISPLAY_EXTERNAL].yres;
        break;

    case 4:
        w_source = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        h_source = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
        w_dstpos = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].relxres;
        h_dstpos = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].relyres;
        break;

    case 5:
        w_source = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        h_source = ctxp->dpyAttr[HWC_DISPLAY_PRIMARY].yres;

        property_get("persist.sys.overscan.main", new_valuep, "overscan 100,100,100,100");
        property_get("persist.sys.overscan.aux",  new_valuee, "false");

        sscanf(new_valuep,"overscan %d,%d,%d,%d",&lpersent,&tpersent,&rpersent,&bpersent);

        if(lpersent < 80) lpersent = 80;
        if(tpersent < 80) tpersent = 80;
        if(rpersent < 80) rpersent = 80;
        if(bpersent < 80) bpersent = 80;

        if(lpersent > 100) lpersent = 100;
        if(tpersent > 100) tpersent = 100;
        if(rpersent > 100) rpersent = 100;
        if(bpersent > 100) bpersent = 100;

        lpersent = (100 - lpersent) / 2;
        tpersent = (100 - tpersent) / 2;
        rpersent = (100 - rpersent) / 2;
        bpersent = (100 - bpersent) / 2;

        lscale = ((float)lpersent / 100);
        tscale = ((float)tpersent / 100);
        ALOGD_IF(is_out_log()>2,"%f,%f,%f,%f",lscale,tscale,rscale,bscale);
        break;

    default:
        break;
    }

    float w_scale = (float)w_dstpos / w_source;
    float h_scale = (float)h_dstpos / h_source;

    if(w_source != w_dstpos && (flag == 0 || flag == 3 || flag == 4))
    {
        for(int i = 0;i<4;i++)
        {
            for(int j=0;j<4;j++)
            {
                if(fb_info->win_par[i].area_par[j].ion_fd || fb_info->win_par[i].area_par[j].phy_addr)
                {
                    fb_info->win_par[i].area_par[j].xpos  =
                        (unsigned short)(fb_info->win_par[i].area_par[j].xpos * w_scale);
                    fb_info->win_par[i].area_par[j].ypos  =
                        (unsigned short)(fb_info->win_par[i].area_par[j].ypos * h_scale);
                    fb_info->win_par[i].area_par[j].xsize =
                        (unsigned short)(fb_info->win_par[i].area_par[j].xsize * w_scale);
                    fb_info->win_par[i].area_par[j].ysize =
                        (unsigned short)(fb_info->win_par[i].area_par[j].ysize * h_scale);
                    ALOGD_IF(is_out_log()>2,"Adjust dst to => [%d,%d,%d,%d]",
                        fb_info->win_par[i].area_par[j].xpos,fb_info->win_par[i].area_par[j].ypos,
                        fb_info->win_par[i].area_par[j].xsize,fb_info->win_par[i].area_par[j].ysize);
                }
            }
        }
    }

    if(w_dstpos != 100 && flag == 1)
    {
        for(int i = 0;i<4;i++)
        {
            for(int j=0;j<4;j++)
            {
                if(fb_info->win_par[i].area_par[j].ion_fd || fb_info->win_par[i].area_par[j].phy_addr)
                {
                    fb_info->win_par[i].area_par[j].xpos  += xoffset;
                    fb_info->win_par[i].area_par[j].ypos  += yoffset;
                    fb_info->win_par[i].area_par[j].xsize -= 2 * xoffset;
                    fb_info->win_par[i].area_par[j].ysize -= 2 * yoffset;
                    ALOGD_IF(is_out_log()>2,"Adjust dst to => [%d,%d,%d,%d]",
                        fb_info->win_par[i].area_par[j].xpos,fb_info->win_par[i].area_par[j].ypos,
                        fb_info->win_par[i].area_par[j].xsize,fb_info->win_par[i].area_par[j].ysize);
                }
            }
        }
    }

    if(flag == 2 || flag == 5)
    {
        for(int i = 0;i<4;i++)
        {
            for(int j=0;j<4;j++)
            {
                if(fb_info->win_par[i].area_par[j].ion_fd || fb_info->win_par[i].area_par[j].phy_addr)
                {
                    int xpos = fb_info->win_par[i].area_par[j].xpos;
                    int ypos = fb_info->win_par[i].area_par[j].ypos;
                    int xsize = fb_info->win_par[i].area_par[j].xsize;
                    int ysize = fb_info->win_par[i].area_par[j].ysize;

                    fb_info->win_par[i].area_par[j].xpos = ((int)(xpos * (1.0 - 2 * lscale)) + (int)(w_source * lscale));
                    fb_info->win_par[i].area_par[j].ypos = ((int)(ypos * (1.0 - 2 * tscale)) + (int)(h_source * tscale));
                    fb_info->win_par[i].area_par[j].xsize -= ((int)(xsize * lscale) + (int)(xsize * lscale));
                    fb_info->win_par[i].area_par[j].ysize -= ((int)(ysize * tscale) + (int)(ysize * tscale));
                    ALOGD_IF(is_out_log()>2,"Adjust [%d,%d,%d,%d] => [%d,%d,%d,%d]",xpos,ypos,xsize,ysize,
                        fb_info->win_par[i].area_par[j].xpos,fb_info->win_par[i].area_par[j].ypos,
                        fb_info->win_par[i].area_par[j].xsize,fb_info->win_par[i].area_par[j].ysize);
                }
            }
        }
    }
    return 0;
}

void  *hotplug_init_thread(void *arg)
{
    prctl(PR_SET_NAME,"HWC_htg1");
    HWC_UNREFERENCED_PARAMETER(arg);
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    hotplug_get_config(0);
    hotplug_set_config();
    context->mHtg.HtgOn = true;
    int count = 0;
    while(context->fb_blanked)
    {
        count++;
        usleep(10000);
        if(300==count){
            ALOGW("wait for primary unblank");
            break;
        }
    }
    handle_hotplug_event(1,0);
#if defined(RK322X_BOX) || defined(RK_DONGLE)
    while(!context->mIsBootanimExit){
        int i = 0;
        char value[PROPERTY_VALUE_MAX];
        property_get("service.bootanim.exit",value,"0");
        i = atoi(value);
        if(1==i){
            context->mIsBootanimExit = true;
            context->mIsFirstCallbackToHotplug = true;
        }else{
            usleep(30000);
        }
    }
    handle_hotplug_event(1,0);
#endif
    pthread_exit(NULL);
    return NULL;
}

void hotplug_change_screen_config(int dpy, int fb, int state) {
    hwcContext * context = gcontextAnchor[0];
    if (context) {
        char buf[100];
        int width = 0, height = 0, fd = -1;
        uint32_t vsync_period = 60;

        if (state)
            init_tv_hdr_info(context);

        fd = open("/sys/class/graphics/fb0/screen_info", O_RDONLY);
        if(fd < 0)
        {
            ALOGE("hwc_change_config:open fb0 screen_info error,fd=%d",fd);
            return;
        }
        if(read(fd,buf,sizeof(buf)) < 0)
        {
            ALOGE("error reading fb0 screen_info: %s", strerror(errno));
            return;
        }
        close(fd);
        sscanf(buf,"xres:%d yres:%d",&width,&height);
        ALOGD("hwc_change_config:width=%d,height=%d",width,height);
        vsync_period = hwc_get_vsync_period_from_fb_fps(fb);
        context->dpyAttr[HWC_DISPLAY_PRIMARY].relxres = width;
        context->dpyAttr[HWC_DISPLAY_PRIMARY].relyres = height;
        context->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period = vsync_period;
        context->deviceConected = state;
        if (context->procs && context->procs->hotplug)
            context->procs->hotplug(context->procs, HWC_DISPLAY_PRIMARY, 1);
#if FORCE_REFRESH
        pthread_mutex_lock(&context->mRefresh.mlk);
        context->mRefresh.count = 0;
        ALOGD_IF(is_out_log(),"Htg:mRefresh.count=%d",context->mRefresh.count);
        pthread_mutex_unlock(&context->mRefresh.mlk);
        pthread_cond_signal(&context->mRefresh.cond);
#endif
    }

    return;
}

void  *hotplug_invalidate_refresh(void *arg)
{
#if FORCE_REFRESH
    int count = 0;
    int nMaxCnt = 15;
    unsigned int nSleepTime = 200;
    hwcContext *contextp = gcontextAnchor[HWC_DISPLAY_PRIMARY];
    ALOGD("hotplug_invalidate_refresh creat");
    pthread_cond_wait(&contextp->mRefresh.cond,&contextp->mRefresh.mtx);
    while(true) {
        for(count = 0; count < nMaxCnt; count++) {
            usleep(nSleepTime*1000);
            pthread_mutex_lock(&contextp->mRefresh.mlk);
            count = contextp->mRefresh.count;
            contextp->mRefresh.count ++;
            ALOGD_IF(is_out_log(),"mRefresh.count=%d",contextp->mRefresh.count);
            pthread_mutex_unlock(&contextp->mRefresh.mlk);
            contextp->procs->invalidate(contextp->procs);
        }
        pthread_cond_wait(&contextp->mRefresh.cond,&contextp->mRefresh.mtx);
        count = 0;
    }
    ALOGD("hotplug_invalidate_refresh exit");
#endif
    pthread_exit(NULL);
    return NULL;
}

int hotplug_set_overscan(int flag)
{
    char new_valuep[PROPERTY_VALUE_MAX];
    char new_valuee[PROPERTY_VALUE_MAX];

    switch(flag){
    case 0:
        property_get("persist.sys.overscan.main", new_valuep, "false");
        property_get("persist.sys.overscan.aux",  new_valuee, "false");
        break;

    case 1:
        strcpy(new_valuep,"overscan 100,100,100,100");
        strcpy(new_valuee,"overscan 100,100,100,100");
        break;

    default:
        break;
    }

    int fdp = open("/sys/class/graphics/fb0/scale",O_RDWR);
    if(fdp > 0){
        int ret = write(fdp,new_valuep,sizeof(new_valuep));
        if(ret != sizeof(new_valuep)){
            ALOGE("write /sys/class/graphics/fb0/scale fail");
            close(fdp);
            return -1;
        }
        ALOGV("new_valuep=[%s]",new_valuep);
        close(fdp);
    }
    return 0;
}

int hotpulg_did_hdr_video(hwcContext *ctx,struct rk_fb_win_par *win_par, struct private_handle_t* src_handle) {

   if (src_handle == NULL) {
      return -1;
   }

   if ((src_handle->format != HAL_PIXEL_FORMAT_YCrCb_NV12_10))
        return -1;
   //rk_nv12_10_color_space_t hdrFormat = hwc_get_int_property("sys.hwc.test", "0");  //test
   rk_nv12_10_color_space_t hdrFormat = get_rk_color_space_from_usage(src_handle->usage);  //realy
   ALOGD_IF(is_out_log(), "did_hdr_video: hdrFormat=0x%x, usage=0x%x", hdrFormat, src_handle->usage);
   if (hdrFormat > 0 ) {	
		win_par->area_par[0].data_format =0x22  | 0x80;	  
 
       if (hdrFormat == 2) { 
	        win_par->area_par[0].data_space = 0x01;
			if (ctx->hdrSupportType & HAL_DATASPACE_TRANSFER_ST2084) {
				if (ctx->hdrStatus == 0 && ctx->deviceConected) {
					HdmiSetHDR(4);
					ALOGD("did_hdr_video:HdmiSetHDR"); 
					ctx->hdrStatus = 1;
				}
				ctx->hdrFrameStatus = 1;
				
			} else if ((ctx->hdrSupportType & HAL_DATASPACE_STANDARD_BT2020)) {
				if (ctx->hdrStatus == 0 && ctx->deviceConected) {
				  HdmiSetColorimetry(HAL_DATASPACE_STANDARD_BT2020);
				  ALOGD("did_hdr_video:HAL_DATASPACE_STANDARD_BT2020");
				  ctx->hdrStatus = 1;
				} 
				ctx->hdrFrameStatus = 1;
				
			} else {
				ctx->hdrFrameStatus = 0;
			}
	   } else if (hdrFormat == 1) {
			if (ctx->hdrStatus == 0 && ctx->deviceConected) {
			  HdmiSetColorimetry(HAL_DATASPACE_STANDARD_BT2020);
			  ALOGD("did_hdr_video:HAL_DATASPACE_STANDARD_BT2020");
			  ctx->hdrStatus = 1;
			} 
			ctx->hdrFrameStatus = 1;
			win_par->area_par[0].data_space = 0x00;
	   }
   } else {
	  // ALOGD("hdrFormat=%d",hdrFormat);
   }
   
   return 0;
}
