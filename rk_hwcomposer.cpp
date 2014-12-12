/****************************************************************************
*
*    Copyright (c) 2005 - 2011 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/13/2011. Do not edit!!!
*
*****************************************************************************/




#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "rk_hwcomposer.h"

#include <hardware/hardware.h>


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

//#define ENABLE_HDMI_APP_LANDSCAP_TO_PORTRAIT
static int SkipFrameCount = 0;
//static bool LastUseGpuCompose = false;
static hwcContext * _contextAnchor = NULL;
static hwbkupmanage bkupmanage;
static int skip_count = 0;
static int last_video_addr[2];

#ifdef ENABLE_HDMI_APP_LANDSCAP_TO_PORTRAIT
static int bootanimFinish = 0;
#endif


#ifdef USE_LCDC_COMPOSER
static int skip_hdmi_count = 0;
#endif


struct rk_fb_win_config_data
{
    int                rel_fence_fd[4];
    int            acq_fence_fd[16];
    int            wait_fs;
    unsigned char  fence_begin;
    int            ret_fence_fd;
    //struct s3c_fb_win_config config[S3C_FB_MAX_WIN];
};
struct rk_fb_win_config_data g_sync;

static int
hwc_blank(
    struct hwc_composer_device_1 *dev,
    int dpy,
    int blank);
static int
hwc_query(
    struct hwc_composer_device_1* dev,
    int what,
    int* value);

static int hwc_event_control(
    struct hwc_composer_device_1* dev,
    int dpy,
    int event,
    int enabled);

static int
hwc_prepare(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
);


static int
hwc_set(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t  ** displays
);

static int
hwc_device_close(
    struct hw_device_t * dev
);

static int
hwc_device_open(
    const struct hw_module_t * module,
    const char * name,
    struct hw_device_t ** device
);

int getFbInfo(hwc_display_t dpy, hwc_surface_t surf, hwc_display_contents_1_t *list);

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
author:        "Vivante Corporation"
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

//return property value of pcProperty
static int hwc_get_int_property(const char* pcProperty, const char* default_value)
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
static int is_out_log( void )
{
    return hwc_get_int_property("sys.hwc.log","0");
}

static int LayerZoneCheck(hwc_layer_1_t * Layer)
{
    hwc_region_t * Region = &(Layer->visibleRegionScreen);
    hwc_rect_t const * rects = Region->rects;
    int i;
    for (i = 0; i < (unsigned int) Region->numRects ;i++)
    {
        LOGV("checkzone=%s,[%d,%d,%d,%d]", \
             Layer->LayerName, rects[i].left, rects[i].top, rects[i].right, rects[i].bottom);
        if (rects[i].left < 0 || rects[i].top < 0
                || rects[i].right > _contextAnchor->fbWidth
                || rects[i].bottom > _contextAnchor->fbHeight)
        {
            return -1;
        }
    }

    return 0;
}

void hwc_sync(hwc_display_contents_1_t  *list)
{
    if (list == NULL)
    {
        return ;
    }

    for (int i = 0; i < list->numHwLayers; i++)
    {
        if (list->hwLayers[i].acquireFenceFd > 0)
        {
            sync_wait(list->hwLayers[i].acquireFenceFd, 500);
            ALOGV("fenceFd=%d,name=%s", list->hwLayers[i].acquireFenceFd, list->hwLayers[i].LayerName);
        }

    }
}

void hwc_sync_release(hwc_display_contents_1_t  *list)
{
    for (int i = 0; i < list->numHwLayers; i++)
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
#if 1
//static int layer_seq = 0;

int rga_video_copybit(struct private_handle_t *src_handle,
                      struct private_handle_t *dst_handle, int videoIndex)
{
    struct tVPU_FRAME *pFrame  = NULL;
    struct rga_req  rga_cfg;
    int   rga_fd = _contextAnchor->engine_fd;
    if (!rga_fd)
    {
        return -1;
    }
    if (!src_handle || !dst_handle)
    {
        return -1;
    }
    struct private_handle_t *handle = src_handle;
    pFrame = (tVPU_FRAME *)GPU_BASE;

    //backup video
    memset(&_contextAnchor->video_frame[videoIndex], 0, sizeof(vpu_frame_t));
    _contextAnchor->video_frame[videoIndex].vpu_handle = (void*)GPU_BASE;
    memcpy(&_contextAnchor->video_frame[videoIndex].vpu_frame, (void*)GPU_BASE, sizeof(tVPU_FRAME));
    if (pFrame->FrameWidth > 2048 || pFrame->FrameWidth == 0)
    {
        return -1;
    }
    handle->video_disp_width = pFrame->DisplayWidth;
    handle->video_disp_height = pFrame->DisplayHeight;
    memset(&rga_cfg, 0x0, sizeof(rga_req));
    ALOGV("videopFrame addr=%x,FrameWidth=%d,FrameHeight=%d", pFrame->FrameBusAddr[0], pFrame->FrameWidth, pFrame->FrameHeight);
    rga_cfg.src.yrgb_addr =  0;
    rga_cfg.src.uv_addr  = (int)pFrame->FrameBusAddr[0];
    rga_cfg.src.v_addr   =  rga_cfg.src.uv_addr;
#if 0
    rga_cfg.src.vir_w = ((pFrame->FrameWidth + 15) & ~15);
    rga_cfg.src.vir_h = ((pFrame->FrameHeight + 15) & ~15);
#else
    rga_cfg.src.vir_w =  pFrame->FrameWidth;
    rga_cfg.src.vir_h = pFrame->FrameHeight;
#endif
    rga_cfg.src.format = RK_FORMAT_YCbCr_420_SP;

#if 0
    rga_cfg.src.act_w = pFrame->FrameWidth;
    rga_cfg.src.act_h = pFrame->FrameHeight;
#else
    rga_cfg.src.act_w = handle->video_disp_width;
    rga_cfg.src.act_h = handle->video_disp_height;
#endif
    rga_cfg.src.x_offset = 0;
    rga_cfg.src.y_offset = 0;

    ALOGD_IF(0, "copybit src info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
             "vir_w=%d,vir_h=%d,format=%d,"
             "act_x_y_w_h [%d,%d,%d,%d] ",
             rga_cfg.src.yrgb_addr, rga_cfg.src.uv_addr , rga_cfg.src.v_addr,
             rga_cfg.src.vir_w , rga_cfg.src.vir_h , rga_cfg.src.format ,
             rga_cfg.src.x_offset ,
             rga_cfg.src.y_offset,
             rga_cfg.src.act_w ,
             rga_cfg.src.act_h
            );


#ifdef TARGET_BOARD_PLATFORM_RK30XXB
    rga_cfg.dst.yrgb_addr = dst_handle->share_fd; //dsthandle->base;//(int)(fixInfo.smem_start + dsthandle->offset);
    rga_cfg.dst.vir_w = (GPU_WIDTH + 31) & ~31;  //((srcandle->iWidth*2 + (8-1)) & ~(8-1))/2 ;  /* 2:RK_FORMAT_RGB_565 ,8:????*///srcandle->width;
#else
    rga_cfg.dst.yrgb_addr = dst_handle->share_fd; //dsthandle->base;//(int)(fixInfo.smem_start + dsthandle->offset);
#if 0
    rga_cfg.dst.vir_w = ((GPU_WIDTH * 2 + (8 - 1)) & ~(8 - 1)) / 2;
#else
    rga_cfg.dst.vir_w =   GPU_WIDTH;
#endif
#endif
    rga_cfg.dst.vir_h =  GPU_HEIGHT;
#if 0
    rga_cfg.dst.act_w = GPU_WIDTH;//Rga_Request.dst.vir_w;
    rga_cfg.dst.act_h = GPU_HEIGHT;//Rga_Request.dst.vir_h;
#else
    rga_cfg.dst.act_w = handle->video_disp_width;
    rga_cfg.dst.act_h = handle->video_disp_height;
#endif
    rga_cfg.dst.uv_addr  = 0;
    rga_cfg.dst.v_addr   = 0;
    //Rga_Request.dst.format = RK_FORMAT_RGB_565;
    rga_cfg.clip.xmin = 0;
    rga_cfg.clip.xmax = rga_cfg.dst.vir_w - 1;
    rga_cfg.clip.ymin = 0;
    rga_cfg.clip.ymax = rga_cfg.dst.vir_h - 1;
    rga_cfg.dst.x_offset = 0;
    rga_cfg.dst.y_offset = 0;

    rga_cfg.sina = 0;
    rga_cfg.cosa = 0x10000;

    char property[PROPERTY_VALUE_MAX];
    int gpuformat = HAL_PIXEL_FORMAT_RGB_565;
    if (property_get("sys.yuv.rgb.format", property, NULL) > 0)
    {
        gpuformat = atoi(property);
    }
    if (gpuformat == 1)
    {
        rga_cfg.dst.format = RK_FORMAT_RGBA_8888;//RK_FORMAT_RGB_565;
    }
    else if (gpuformat == 2)
    {
        rga_cfg.dst.format = RK_FORMAT_RGBX_8888;
    }
    else if (gpuformat == 3)
    {
        rga_cfg.dst.format = RK_FORMAT_RGB_565;
    }
    else if (gpuformat == 4)
    {
        rga_cfg.dst.format = RK_FORMAT_YCbCr_420_SP;
    }
    else
    {
        rga_cfg.dst.format = RK_FORMAT_RGB_565;
    }

    if (dst_handle->type == 1)
    {
        rga_cfg.dst.uv_addr = dst_handle->base;
        RGA_set_mmu_info(&rga_cfg, 1, 0, 0, 0, 0, 2);
        rga_cfg.mmu_info.mmu_flag |= (1 << 31) | (1 << 10);
    }

    ALOGD_IF(0, "copybit dst info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
             "vir_w=%d,vir_h=%d,format=%d,"
             "clip[%d,%d,%d,%d], "
             "act_x_y_w_h [%d,%d,%d,%d] ",
             rga_cfg.dst.yrgb_addr, rga_cfg.dst.uv_addr , rga_cfg.dst.v_addr,
             rga_cfg.dst.vir_w , rga_cfg.dst.vir_h , rga_cfg.dst.format,
             rga_cfg.clip.xmin,
             rga_cfg.clip.xmax,
             rga_cfg.clip.ymin,
             rga_cfg.clip.ymax,
             rga_cfg.dst.x_offset ,
             rga_cfg.dst.y_offset,
             rga_cfg.dst.act_w ,
             rga_cfg.dst.act_h

            );

    ALOGV("src info: x_offset=%d,y_offset=%d,act_w=%d,act_h=%d,vir_w=%d,vir_h=%d", rga_cfg.src.x_offset, rga_cfg.src.y_offset, \
          rga_cfg.src.act_w, rga_cfg.src.act_h, \
          rga_cfg.src.vir_w, rga_cfg.src.vir_h);
    ALOGV("dst info: x_offset=%d,y_offset=%d,act_w=%d,act_h=%d,vir_w=%d,vir_h=%d", rga_cfg.dst.x_offset, rga_cfg.dst.y_offset, \
          rga_cfg.dst.act_w, rga_cfg.dst.act_h, \
          rga_cfg.dst.vir_w, rga_cfg.dst.vir_h);

    //Rga_Request.render_mode = pre_scaling_mode;
    rga_cfg.alpha_rop_flag |= (1 << 5);
    rga_cfg.render_mode = pre_scaling_mode;

    //gettimeofday(&tpend1,NULL);
    if (ioctl(rga_fd, RGA_BLIT_SYNC, &rga_cfg) != 0)
    {

        ALOGE("ERROR:src info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
              "vir_w=%d,vir_h=%d,format=%d,"
              "act_x_y_w_h [%d,%d,%d,%d] ",
              rga_cfg.src.yrgb_addr, rga_cfg.src.uv_addr , rga_cfg.src.v_addr,
              rga_cfg.src.vir_w , rga_cfg.src.vir_h , rga_cfg.src.format ,
              rga_cfg.src.x_offset ,
              rga_cfg.src.y_offset,
              rga_cfg.src.act_w ,
              rga_cfg.src.act_h

             );

        ALOGE("ERROR dst info: yrgb_addr=%x, uv_addr=%x,v_addr=%x,"
              "vir_w=%d,vir_h=%d,format=%d,"
              "clip[%d,%d,%d,%d], "
              "act_x_y_w_h [%d,%d,%d,%d] ",
              rga_cfg.dst.yrgb_addr, rga_cfg.dst.uv_addr , rga_cfg.dst.v_addr,
              rga_cfg.dst.vir_w , rga_cfg.dst.vir_h , rga_cfg.dst.format,
              rga_cfg.clip.xmin,
              rga_cfg.clip.xmax,
              rga_cfg.clip.ymin,
              rga_cfg.clip.ymax,
              rga_cfg.dst.x_offset ,
              rga_cfg.dst.y_offset,
              rga_cfg.dst.act_w ,
              rga_cfg.dst.act_h
             );
        return -1;
    }

#if 0
    FILE * pfile = NULL;
    int srcStride = android::bytesPerPixel(src_handle->format);
    char layername[100];
    memset(layername, 0, sizeof(layername));
    system("mkdir /data/dumplayer/ && chmod /data/dumplayer/ 777 ");
    sprintf(layername, "/data/dumplayer/dmlayer%d_%d_%d_%d.bin", \
            layer_seq, src_handle->stride, src_handle->height, srcStride);

    pfile = fopen(layername, "wb");
    if (pfile)
    {
        fwrite((const void *)src_handle->base, (size_t)(2 * src_handle->stride*src_handle->height), 1, pfile);
        fclose(pfile);
    }
    layer_seq++;
#endif
    return 0;
}


int rga_video_reset()
{
    for (int i = 0; i < 2; i++)
    {
        if (_contextAnchor->video_frame[i].vpu_handle)
        {
            ALOGV(" rga_video_reset,%x", _contextAnchor->video_frame[i].vpu_handle);
            memcpy((void*)_contextAnchor->video_frame[i].vpu_handle,
                   (void*)&_contextAnchor->video_frame[i].vpu_frame, sizeof(tVPU_FRAME));
            //tVPU_FRAME* p = (tVPU_FRAME*)_contextAnchor->video_frame[i].vpu_handle;
            // ALOGD("vpu,w=%d,h=%d",p->FrameWidth,p->FrameHeight);
            _contextAnchor->video_frame[i].vpu_handle = 0;
        }
    }
    return 0;
}
#endif
static PFNEGLGETRENDERBUFFERANDROIDPROC _eglGetRenderBufferANDROID;
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
static PFNEGLRENDERBUFFERMODIFYEDANDROIDPROC _eglRenderBufferModifiedANDROID;
#endif
int  IsInputMethod()
{
    char pro_value[PROPERTY_VALUE_MAX];
    property_get("sys.gui.special", pro_value, 0);
    if (!strcmp(pro_value, "true"))
        return 1;
    else
        return 0;
}

static uint32_t
_CheckLayer(
    hwcContext * Context,
    uint32_t Count,
    uint32_t Index,
    hwc_layer_1_t * Layer,
    hwc_display_contents_1_t * list,
    int videoflag
)
{
    struct private_handle_t * handle =
                    (struct private_handle_t *) Layer->handle;

    float hfactor = 1;
    float vfactor = 1;
    char pro_value[PROPERTY_VALUE_MAX];

    (void) Context;
    (void) Count;
    (void) Index;

    if (!videoflag)
{

        hfactor = (float)(Layer->sourceCrop.right - Layer->sourceCrop.left)
                  / (Layer->displayFrame.right - Layer->displayFrame.left);

        vfactor = (float)(Layer->sourceCrop.bottom - Layer->sourceCrop.top)
                  / (Layer->displayFrame.bottom - Layer->displayFrame.top);

        /* ----for 3066b support only in video mode in hwc module ----*/
        /* --------------- this code is  for a short time----*/
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
        // Layer->compositionType = HWC_FRAMEBUFFER;
        // return HWC_FRAMEBUFFER;
#endif
        /* ----------------------end--------------------------------*/

    }
    /* Check whether this layer is forced skipped. */

    if ((Layer->flags & HWC_SKIP_LAYER)
            //||(Layer->transform == (HAL_TRANSFORM_FLIP_V  | HAL_TRANSFORM_ROT_90))
            //||(Layer->transform == (HAL_TRANSFORM_FLIP_H  | HAL_TRANSFORM_ROT_90))
#ifndef USE_LCDC_COMPOSER
            || (hfactor > 1.0f)  // because rga scale down too slowly,so return to opengl  ,huangds modify
            || (vfactor > 1.0f)  // because rga scale down too slowly,so return to opengl ,huangds modify
            || ((hfactor < 1.0f || vfactor < 1.0f) && (handle && GPU_FORMAT == HAL_PIXEL_FORMAT_RGBA_8888)) // because rga scale up RGBA foramt not support
#endif
#ifndef USE_LCDC_COMPOSER
            || ((Layer->transform != 0) && (!videoflag))
            || ((Context->IsRk3188 || Context->IsRk3126) && !(videoflag && Count <= 2))
#else
            || ((Layer->transform != 0) && (!videoflag))
#ifdef USE_LAUNCHER2
            || !strcmp(Layer->LayerName, "Keyguard")
#endif
#endif
            || skip_count < 5
            //  || handle->type == 1
       )
    {
        /* We are forbidden to handle this layer. */
        if(is_out_log() )
       // if(1)
        {
            LOGD("%s(%d):Will not handle layer %s: SKIP_LAYER,Layer->transform=%d,Layer->flags=%d",
                __FUNCTION__, __LINE__, Layer->LayerName,Layer->transform,Layer->flags);
            if(handle)   
            {
                LOGD("Will not handle format=%x,handle_type=%d",GPU_FORMAT,handle->type);  
            }    
        }        
        if (skip_count < 5)
        {
            skip_count++;
        }
        return HWC_FRAMEBUFFER;
    }

    /* Check whether this layer can be handled by Vivante 2D. */
    do
    {
        RgaSURF_FORMAT format = RK_FORMAT_UNKNOWN;
        /* Check for dim layer. */
        if ((Layer->blending & 0xFFFF) == HWC_BLENDING_DIM)
        {
            Layer->compositionType = HWC_DIM;
            Layer->flags           = 0;
            break;
        }

        if (Layer->handle == NULL)
        {
            LOGE("%s(%d):No layer surface at %d.",
                 __FUNCTION__,
                 __LINE__,
                 Index);
            /* TODO: I BELIEVE YOU CAN HANDLE SUCH LAYER!. */
            if (SkipFrameCount == 0)
            {
                Layer->compositionType = HWC_FRAMEBUFFER;
                SkipFrameCount = 1;
            }
            else
            {
                Layer->compositionType = HWC_CLEAR_HOLE;
                Layer->flags           = 0;
            }

            break;
        }

        /* At least surfaceflinger can handle this layer. */
        Layer->compositionType = HWC_FRAMEBUFFER;

        /* Get format. */
        if (hwcGetFormat(handle, &format) != hwcSTATUS_OK
                || (LayerZoneCheck(Layer) != 0))
        {

            return HWC_FRAMEBUFFER;
        }

        LOGV("name[%d]=%s,phy_addr=%x", Index, list->hwLayers[Index].LayerName, handle->phy_addr);

#ifdef USE_LCDC_COMPOSER
        int win0 = 0;
        int win1 = 1;
        int other = 2;
        int max_cont = MAX_DO_SPECIAL_COUNT;
        if (IsInputMethod())
        {
            win1 = 2;
            other = 3;
            max_cont = MAX_DO_SPECIAL_COUNT + 1;
        }
        property_get("sys.SD2HD", pro_value, 0);
        if ((Layer->visibleRegionScreen.numRects == 1)
                && (Count <= max_cont)
                //&& (getHdmiMode()==0)
                && strcmp(pro_value, "true")
                && handle->phy_addr != 0
           )    // layer <=3,do special processing

        {

            int SrcHeight = Layer->sourceCrop.bottom - Layer->sourceCrop.top;
            int SrcWidth = Layer->sourceCrop.right - Layer->sourceCrop.left;
            bool isLandScape = ((0 == Layer->realtransform) \
                                || (HWC_TRANSFORM_ROT_180 == Layer->realtransform));
            bool isSmallRect = (isLandScape && (SrcHeight < Context->fbHeight / 4))  \
                               || (!isLandScape && (SrcWidth < Context->fbWidth / 4)) ;
            int AlignLh = (android::bytesPerPixel(handle->format)) * 32;

            if (getHdmiMode() > 0)
            {
                if (!videoflag && skip_hdmi_count > 10)
                {
                    Layer->compositionType = HWC_FRAMEBUFFER;
                    return HWC_FRAMEBUFFER;
                }
                else
                {
                    skip_hdmi_count++;
                }
            }
            if (Index == win0)
            {
                if (Layer->sourceCrop.right - Layer->sourceCrop.left < 16)
                {
                    Layer->compositionType = HWC_FRAMEBUFFER;
                    return HWC_FRAMEBUFFER;
                }
                Layer->compositionType = HWC_TOWIN0;
                Layer->flags           = 0;
                break;
            }
            else if (Index == win1)
            {
                if (hfactor != 1.0f || vfactor != 1.0f
                        || (!isSmallRect && (handle->stride % AlignLh != 0) && _contextAnchor->IsRk3188) // modify win1 no suppost scale
                        || (Layer->sourceCrop.right - Layer->sourceCrop.left < 16)) //zxl:lcdc not support xres < 16
                {
                    Layer->compositionType = HWC_FRAMEBUFFER;
                    return HWC_FRAMEBUFFER;
                }
                else
                {
                    if (videoflag > 0 && Count <= 2)
                    {
                        Layer->compositionType = HWC_BLITTER;
                        Layer->flags           = 0;
                        return Layer->compositionType;
                    }
                    else
                    {
                        Layer->compositionType = HWC_TOWIN1;
                        Layer->flags           = 0;
                    }
                }
                break;
            }

            if (Index >= other)
            {
                bool IsBottom = !strcmp(BOTTOM_LAYER_NAME, list->hwLayers[Index].LayerName);
                bool IsTop = !strcmp(TOP_LAYER_NAME, list->hwLayers[Index].LayerName);
                bool IsFps = !strcmp(FPS_NAME, list->hwLayers[Index].LayerName);
                bool IsInputPop = strstr(list->hwLayers[Index].LayerName, PopWin)
                                  && IsInputMethod();
                if ((!(IsBottom | IsTop | IsFps | IsInputPop)) ||
                        (videoflag && Count >= 3) || !isSmallRect)
                {
                    if (Context->fbFd1 > 0)
                    {
                        Context->fb1_cflag = true;
                    }
                    Layer->compositionType = HWC_FRAMEBUFFER;
                    return HWC_FRAMEBUFFER;
                }
            }

            if (Context->fbFd1 > 0 && Count == 1)
            {
                Context->fb1_cflag = true;

            }
        }
        else
        {
            if (Context->fbFd1 > 0)
            {
                Context->fb1_cflag = true;
            }

            /* return GPU for temp*/
            //if(IsRk3188)
            {
                Layer->compositionType = HWC_FRAMEBUFFER;
                return HWC_FRAMEBUFFER;
            }
            /*    ----end  ----*/
        }
#else
        if (((GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO || GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12) && Count <= 2 /*&& getHdmiMode()==0*/ && Context->wfdOptimize == 0)
                || ((GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO || GPU_FORMAT ==  HAL_PIXEL_FORMAT_YCrCb_NV12) && Count == 3 /*&& getHdmiMode()==0*/ \
                    && strstr(list->hwLayers[Count - 2].LayerName, "SystemBar") && Context->wfdOptimize == 0)
           )
        {
            /*if (strstr(list->hwLayers[Count - 1].LayerName, "android.rk.RockVideoPlayer")
                 ||strstr(list->hwLayers[Count - 1].LayerName, "SystemBar")  // for Gallery
                 ||strstr(list->hwLayers[Count - 1].LayerName,"com.android.gallery3d")  // for Gallery
                 ||strstr(list->hwLayers[Count - 1].LayerName,"com.asus.ephoto.app.MovieActivity")
            ||strstr(list->hwLayers[Count - 1].LayerName,"com.mxtech.videoplayer.ad"))
            {*/
            if (Layer->transform == 0 || (Context->ippDev != NULL && Layer->transform != 0 && Context->ippDev->ipp_is_enable() > 0))
            {
                int video_state = hwc_get_int_property("sys.video.fullscreen", "0");

                if (video_state != VIDEO_UI)
                {
                    Layer->compositionType = HWC_TOWIN0;
                    Layer->flags = 0;
                    Context->flag = 1;
                    break;
                }
            }
            //}
        }
#endif

        if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO || GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12)
            //if( handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO )
        {
            Layer->compositionType = HWC_FRAMEBUFFER;
            return HWC_FRAMEBUFFER;

        }


        //if( videoflag)
        ///{

        // Layer->compositionType = HWC_FRAMEBUFFER;
        // return HWC_FRAMEBUFFER;
        //}


        //if((!strcmp(Layer->LayerName,"Starting com.android.camera"))
        //||(!strcmp(Layer->LayerName,"com.android.camera/com.android.camera.Camera"))
        //)
        /*
        if(strstr(Layer->LayerName,"com.android.camera"))
        {
            Layer->compositionType = HWC_FRAMEBUFFER;
            return HWC_FRAMEBUFFER;

        }
        */
#ifndef USE_LCDC_COMPOSER
        /* Normal 2D blit can be use. */
        Layer->compositionType = HWC_BLITTER;

        property_get("sys_graphic.wfdstatus", pro_value, "false");
        if (!strcmp(pro_value, "true"))
        {
            if (!videoflag)
            {
                Layer->compositionType = HWC_FRAMEBUFFER;//HWC_BLITTER;
            }
            else
            {
                Layer->compositionType = HWC_BLITTER;
            }
#ifdef TARGET_BOARD_PLATFORM_RK29XX
            Layer->compositionType = HWC_FRAMEBUFFER;
#endif
        }
#else
        Layer->compositionType = HWC_BLITTER;
#endif
        /* Stupid, disable alpha blending for the first layer. */
        if (Index == 0)
        {
            Layer->blending = HWC_BLENDING_NONE;
        }
    }
    while (0);

    /* Return last composition type. */
    return Layer->compositionType;
}

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
                FILE * pfile = NULL;
                char layername[100] ;


                if (handle_pre == NULL)
                    continue;

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
                    fwrite((const void *)handle_pre->base, (size_t)(SrcStride * handle_pre->stride*handle_pre->height), 1, pfile);
#else
                    fwrite((const void *)handle_pre->iBase, (size_t)(SrcStride * handle_pre->width*handle_pre->height), 1, pfile);

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

            if (area->owners < (1U << i))
            {
                break;
            }
        }

        LOGD("%s", buf);

        /* Advance to next area. */
        area = area->next;
    }
}
#include <ui/PixelFormat.h>


//extern "C" void *blend(uint8_t *dst, uint8_t *src, int dst_w, int src_w, int src_h);

#ifdef USE_LCDC_COMPOSER


static int backupbuffer(hwbkupinfo *pbkupinfo)
{
    struct rga_req  Rga_Request;
    RECT clip;
    /*    int i,j;
        char *src_adr_s1;
        char *src_adr_s2;
        char *src_adr_s3;
        char *dst_adr_s1;
        char *dst_adr_s2;
        char *dst_adr_s3;
        int ret;*/
    int bpp;

    ALOGV("backupbuffer addr=[%x,%x],bkmem=[%x,%x],w-h[%d,%d][%d,%d,%d,%d][f=%d]",
          pbkupinfo->buf_fd, pbkupinfo->buf_addr_log, pbkupinfo->membk_fd, pbkupinfo->pmem_bk_log, pbkupinfo->w_vir,
          pbkupinfo->h_vir, pbkupinfo->xoffset, pbkupinfo->yoffset, pbkupinfo->w_act, pbkupinfo->h_act, pbkupinfo->format);

    bpp = pbkupinfo->format == RK_FORMAT_RGB_565 ? 2 : 4;

#if 0
    src_adr_s1 = (char *)pbkupinfo->buf_addr_log + \
                 (pbkupinfo->xoffset + pbkupinfo->yoffset * pbkupinfo->w_vir) * bpp;
    src_adr_s2 = src_adr_s1  + (pbkupinfo->w_vir * pbkupinfo->h_act / 2) * bpp;
    src_adr_s3 = src_adr_s1 + (pbkupinfo->w_vir * (pbkupinfo->h_act - 1)) * bpp;
    dst_adr_s1 = (char *)pbkupinfo->pmem_bk_log ;
    dst_adr_s2 = dst_adr_s1 + (pbkupinfo->w_act * pbkupinfo->h_act / 2) * bpp;
    dst_adr_s3 = dst_adr_s1 + (pbkupinfo->w_act * (pbkupinfo->h_act - 1)) * bpp;

    ALOGV("src[%x,%x,%x],dst[%x,%x,%x]", src_adr_s1, src_adr_s2, src_adr_s3, dst_adr_s1, dst_adr_s2, dst_adr_s3);
    ret = memcmp((void*)src_adr_s1, (void*)dst_adr_s1, pbkupinfo->w_act * 1 * bpp);
    if (!ret)
    {
        ret = memcmp((void*)src_adr_s2, (void*)dst_adr_s2, pbkupinfo->w_act * 1 * bpp);
        if (!ret)
        {
            ret = memcmp((void*)src_adr_s3, (void*)dst_adr_s3, pbkupinfo->w_act * 1 * bpp);
            if (!ret)
            {
                return 0; //the smae do not backup
            }
        }
    }
#endif
    clip.xmin = 0;
    clip.xmax = pbkupinfo->w_act - 1;
    clip.ymin = 0;
    clip.ymax = pbkupinfo->h_act - 1;

    memset(&Rga_Request, 0x0, sizeof(Rga_Request));


    RGA_set_src_vir_info(&Rga_Request, pbkupinfo->buf_fd, 0, 0, pbkupinfo->w_vir, pbkupinfo->h_vir, pbkupinfo->format, 0);
    RGA_set_dst_vir_info(&Rga_Request, pbkupinfo->membk_fd, 0, 0, pbkupinfo->w_act, pbkupinfo->h_act, &clip, pbkupinfo->format, 0);
    //RGA_set_src_vir_info(&Rga_Request, (int)pbkupinfo->buf_addr_log, 0, 0,pbkupinfo->w_vir, pbkupinfo->h_vir, pbkupinfo->format, 0);
    //RGA_set_dst_vir_info(&Rga_Request, (int)pbkupinfo->pmem_bk_log, 0, 0,pbkupinfo->w_act, pbkupinfo->h_act, &clip, pbkupinfo->format, 0);
    //RGA_set_mmu_info(&Rga_Request, 1, 0, 0, 0, 0, 2);

    RGA_set_bitblt_mode(&Rga_Request, 0, 0, 0, 0, 0, 0);
    RGA_set_src_act_info(&Rga_Request, pbkupinfo->w_act,  pbkupinfo->h_act,  pbkupinfo->xoffset, pbkupinfo->yoffset);
    RGA_set_dst_act_info(&Rga_Request, pbkupinfo->w_act,  pbkupinfo->h_act, 0, 0);

    // uint32_t RgaFlag = (i==(RgaCnt-1)) ? RGA_BLIT_SYNC : RGA_BLIT_ASYNC;
    if (ioctl(_contextAnchor->engine_fd, RGA_BLIT_ASYNC, &Rga_Request) != 0)
    {
        LOGE(" %s(%d) RGA_BLIT fail", __FUNCTION__, __LINE__);
    }
// #endif
    return 0;
}
static int restorebuffer(hwbkupinfo *pbkupinfo, int direct_fd)
{
    struct rga_req  Rga_Request;
    RECT clip;
    memset(&Rga_Request, 0x0, sizeof(Rga_Request));

    clip.xmin = 0;
    clip.xmax = pbkupinfo->w_vir - 1;
    clip.ymin = 0;
    clip.ymax = pbkupinfo->h_vir - 1;


    ALOGV("restorebuffer addr=[%x,%x],bkmem=[%x,%x],direct_addr=%x,w-h[%d,%d][%d,%d,%d,%d][f=%d]",
          pbkupinfo->buf_fd, pbkupinfo->buf_addr_log, pbkupinfo->membk_fd, pbkupinfo->pmem_bk_log, direct_fd, pbkupinfo->w_vir,
          pbkupinfo->h_vir, pbkupinfo->xoffset, pbkupinfo->yoffset, pbkupinfo->w_act, pbkupinfo->h_act, pbkupinfo->format);

    //RGA_set_src_vir_info(&Rga_Request, (int)pbkupinfo->pmem_bk_log, 0, 0,pbkupinfo->w_act, pbkupinfo->h_act, pbkupinfo->format, 0);
    // RGA_set_dst_vir_info(&Rga_Request, (int)pbkupinfo->buf_addr_log, 0, 0,pbkupinfo->w_vir, pbkupinfo->h_vir, &clip, pbkupinfo->format, 0);
    // RGA_set_mmu_info(&Rga_Request, 1, 0, 0, 0, 0, 2);

    RGA_set_src_vir_info(&Rga_Request,  pbkupinfo->membk_fd, 0, 0, pbkupinfo->w_act, pbkupinfo->h_act, pbkupinfo->format, 0);
    if (direct_fd)
        RGA_set_dst_vir_info(&Rga_Request, direct_fd, 0, 0, pbkupinfo->w_vir, pbkupinfo->h_vir, &clip, pbkupinfo->format, 0);
    else
        RGA_set_dst_vir_info(&Rga_Request, pbkupinfo->buf_fd, 0, 0, pbkupinfo->w_vir, pbkupinfo->h_vir, &clip, pbkupinfo->format, 0);
    RGA_set_bitblt_mode(&Rga_Request, 0, 0, 0, 0, 0, 0);
    RGA_set_src_act_info(&Rga_Request, pbkupinfo->w_act,  pbkupinfo->h_act, 0, 0);
    RGA_set_dst_act_info(&Rga_Request, pbkupinfo->w_act,  pbkupinfo->h_act,  pbkupinfo->xoffset, pbkupinfo->yoffset);
    if (ioctl(_contextAnchor->engine_fd, RGA_BLIT_ASYNC, &Rga_Request) != 0)
    {
        LOGE(" %s(%d) RGA_BLIT fail", __FUNCTION__, __LINE__);
    }
    return 0;
}
static int  CopyBuffByRGA(hwbkupinfo *pcpyinfo)
{
    struct rga_req  Rga_Request;
    RECT clip;
    memset(&Rga_Request, 0x0, sizeof(Rga_Request));
    clip.xmin = 0;
    clip.xmax = pcpyinfo->w_vir - 1;
    clip.ymin = 0;
    clip.ymax = pcpyinfo->h_vir - 1;
    /*
    ALOGV("CopyBuffByRGA addr=[%x,%x],bkmem=[%x,%x],w-h[%d,%d][%d,%d,%d,%d][f=%d]",
        pcpyinfo->buf_addr, pcpyinfo->buf_addr_log, pcpyinfo->pmem_bk, pcpyinfo->pmem_bk_log,pcpyinfo->w_vir,
        pcpyinfo->h_vir,pcpyinfo->xoffset,pcpyinfo->yoffset,pcpyinfo->w_act,pcpyinfo->h_act,pcpyinfo->format);*/
    ALOGV("CopyBuffByRGA addr=[%x,%x],bkmem=[%x,%x],w-h[%d,%d][%d,%d,%d,%d][f=%d]",
          pcpyinfo->buf_fd, pcpyinfo->buf_addr_log, pcpyinfo->membk_fd, pcpyinfo->pmem_bk_log, pcpyinfo->w_vir,
          pcpyinfo->h_vir, pcpyinfo->xoffset, pcpyinfo->yoffset, pcpyinfo->w_act, pcpyinfo->h_act, pcpyinfo->format);
    RGA_set_src_vir_info(&Rga_Request,  pcpyinfo->membk_fd, 0, 0, pcpyinfo->w_vir, pcpyinfo->h_vir, pcpyinfo->format, 0);
    RGA_set_dst_vir_info(&Rga_Request, pcpyinfo->buf_fd, 0, 0, pcpyinfo->w_vir, pcpyinfo->h_vir, &clip, pcpyinfo->format, 0);
    RGA_set_bitblt_mode(&Rga_Request, 0, 0, 0, 0, 0, 0);
    RGA_set_src_act_info(&Rga_Request, pcpyinfo->w_act,  pcpyinfo->h_act, pcpyinfo->xoffset, pcpyinfo->yoffset);
    RGA_set_dst_act_info(&Rga_Request, pcpyinfo->w_act,  pcpyinfo->h_act,  pcpyinfo->xoffset, pcpyinfo->yoffset);

    // uint32_t RgaFlag = (i==(RgaCnt-1)) ? RGA_BLIT_SYNC : RGA_BLIT_ASYNC;
    if (ioctl(_contextAnchor->engine_fd, RGA_BLIT_ASYNC, &Rga_Request) != 0)
    {
        LOGE(" %s(%d) RGA_BLIT fail", __FUNCTION__, __LINE__);
    }

    return 0;
}
static int Is_lcdc_using(int fd)
{
    // ALOGD("enqiu6 enter Is_lcdc_using.");
    int i;
    int dsp_fd[2];
    hwcContext * context = _contextAnchor;
    // ioctl
    // ioctl
    int sync = 0;
    int count = 0;
    while (!sync)
    {
        count++;
        usleep(1000);
        ioctl(context->fbFd, RK_FBIOGET_LIST_STAT, &sync);
    }
    // struct timeval time_begin,time_end;
    //long fence_sync_time = 0;
    ioctl(context->fbFd, RK_FBIOGET_DSP_FD, dsp_fd);
    for (i = 0;i < 2;i++)
    {
        if (fd == dsp_fd[i])
            return 1;
    }
    return 0;
}
static int
hwc_buff_recover(
    hwc_display_contents_1_t* list,
    int gpuflag
)
{
    int LcdCont;
    hwbkupinfo cpyinfo;
    int i;
    hwcContext * context = _contextAnchor;
    //unsigned int  videodata[2];
    //struct fb_var_screeninfo info;
    //int sync = 1;
    bool IsDispDirect = Is_lcdc_using(bkupmanage.direct_fd);//bkupmanage.crrent_dis_addr == bkupmanage.direct_addr;//
    int fbFd = bkupmanage.dstwinNo ? context->fbFd1 : context->fbFd;
    int needrev = 0;
    ALOGV("fd=%d,bkupmanage.dstwinNo=%d", fbFd, bkupmanage.dstwinNo);
    if (context == NULL)
    {
        LOGE("%s(%d):Invalid device!", __FUNCTION__, __LINE__);
        return HWC_EGL_ERROR;
    }
    if (!gpuflag)
    {
        if ((list->numHwLayers - 1) <= 2
                || !fbFd)
        {
            return 0;
        }
        LcdCont = 0;
        for (i = 0; i < (list->numHwLayers - 1) ; i++)
        {
            if ((list->hwLayers[i].compositionType == HWC_TOWIN0) |
                    (list->hwLayers[i].compositionType == HWC_TOWIN1)
               )
            {
                LcdCont ++;
            }
        }
        if (LcdCont != 2)
        {
            return 0;   // dont need recover
        }
    }
    for (i = 0; i < (list->numHwLayers - 1) && i < 2; i++)
    {
        struct private_handle_t * handle = (struct private_handle_t *) list->hwLayers[i].handle;
        if ((list->hwLayers[i].flags & HWC_SKIP_LAYER)
                || (handle == NULL)
           )
            continue;
        if (handle == bkupmanage.handle_bk && \
                handle->phy_addr == bkupmanage.bkupinfo[0].buf_fd)
        {
            ALOGV(" handle->phy_addr==bkupmanage ,name=%s", list->hwLayers[i].LayerName);
            needrev = 1;
            break;
        }
    }
    if (!needrev)
        return 0;
    if (!IsDispDirect)
    {
        cpyinfo.membk_fd = bkupmanage.bkupinfo[bkupmanage.count -1].buf_fd;
        cpyinfo.buf_fd = bkupmanage.direct_fd;
        cpyinfo.xoffset = 0;
        cpyinfo.yoffset = 0;
        cpyinfo.w_vir = bkupmanage.bkupinfo[0].w_vir;
        cpyinfo.h_vir = bkupmanage.bkupinfo[0].h_vir;
        cpyinfo.w_act = bkupmanage.bkupinfo[0].w_vir;
        cpyinfo.h_act = bkupmanage.bkupinfo[0].h_vir;
        cpyinfo.format = bkupmanage.bkupinfo[0].format;
        CopyBuffByRGA(&cpyinfo);
        if (ioctl(context->engine_fd, RGA_FLUSH, NULL) != 0)
        {
            LOGE("%s(%d):RGA_FLUSH Failed!", __FUNCTION__, __LINE__);
        }
#if 0
        videodata[0] = bkupmanage.direct_addr;
        if (ioctl(fbFd, FBIOGET_VSCREENINFO, &info) == -1)
        {
            LOGE("%s(%d):  fd[%d] Failed", __FUNCTION__, __LINE__, context->fbFd1);
            return -1;
        }
        if (ioctl(fbFd, FB1_IOCTL_SET_YUV_ADDR, videodata) == -1)
        {
            LOGE("%s(%d):  fd[%d] Failed,DataAddr=%x", __FUNCTION__, __LINE__, context->fbFd1, videodata[0]);
            return -1;
        }
        if (ioctl(fbFd, FBIOPUT_VSCREENINFO, &info) == -1)
        {
            LOGE("%s(%d):  fd[%d] Failed", __FUNCTION__, __LINE__, context->fbFd1);
            return -1;
        }
        //ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &sync);
        struct rk_fb_win_config_data sync;
        memset((void*)&sync, 0, sizeof(rk_fb_win_config_data));
        sync.fence_begin = 0;
        sync.wait_fs = 1;
        ioctl(context->dpyAttr[0].fd, RK_FBIOSET_CONFIG_DONE, &sync);
#endif
        struct rk_fb_win_cfg_data fb_info ;
        memset(&fb_info, 0, sizeof(fb_info));
        fb_info.win_par[0].data_format = bkupmanage.bkupinfo[0].format;
        fb_info.win_par[0].win_id = 0;
        fb_info.win_par[0].z_order = 0;
        fb_info.win_par[0].area_par[0].ion_fd = bkupmanage.direct_fd;
        fb_info.win_par[0].area_par[0].acq_fence_fd = -1;
        fb_info.win_par[0].area_par[0].x_offset = 0;
        fb_info.win_par[0].area_par[0].y_offset = 0;
        fb_info.win_par[0].area_par[0].xpos = 0;
        fb_info.win_par[0].area_par[0].ypos = 0;
        fb_info.win_par[0].area_par[0].xsize = bkupmanage.bkupinfo[0].w_vir;
        fb_info.win_par[0].area_par[0].ysize = bkupmanage.bkupinfo[0].h_vir;
        fb_info.win_par[0].area_par[0].xact = bkupmanage.bkupinfo[0].w_vir;
        fb_info.win_par[0].area_par[0].yact = bkupmanage.bkupinfo[0].h_vir;
        fb_info.win_par[0].area_par[0].xvir = bkupmanage.bkupinfo[0].w_vir;
        fb_info.win_par[0].area_par[0].yvir = bkupmanage.bkupinfo[0].h_vir;
#if USE_HWC_FENCE
        fb_info.wait_fs = 1;
#endif
        ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &fb_info);

#if USE_HWC_FENCE
        for (int k = 0;k < RK_MAX_BUF_NUM;k++)
        {
            if (fb_info.rel_fence_fd[k] != -1)
                close(fb_info.rel_fence_fd[k]);
        }

        if (fb_info.ret_fence_fd != -1)
            close(fb_info.ret_fence_fd);
#endif
        bkupmanage.crrent_dis_fd =  bkupmanage.direct_fd;
    }
    for (i = 0; i < bkupmanage.count;i++)
    {
        restorebuffer(&bkupmanage.bkupinfo[i], 0);
    }
    if (ioctl(context->engine_fd, RGA_FLUSH, NULL) != 0)
    {
        LOGE("%s(%d):RGA_FLUSH Failed!", __FUNCTION__, __LINE__);
    }
    return 0;
}
int
hwc_layer_recover(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
)
{
//    int LcdCont;
//   hwbkupinfo cpyinfo;
    //  int i;
    HWC_UNREFERENCED_PARAMETER(dev);
    HWC_UNREFERENCED_PARAMETER(numDisplays);

    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    hwc_buff_recover(list, 0);
    return 0;
}
static int
hwc_LcdcToGpu(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
)
{
    HWC_UNREFERENCED_PARAMETER(dev);
    HWC_UNREFERENCED_PARAMETER(numDisplays);

    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    if (!bkupmanage.needrev)
        return 0;
    hwc_buff_recover(list, 1);
    bkupmanage.needrev = 0;
    return 0;
}

#ifdef USE_LAUNCHER2
int hwc_do_special_composer(hwc_display_contents_1_t  * list)
{
    int                 srcFd;
    void *              srcLogical  = NULL;
    void *              srcInfo     = NULL;
    unsigned int        srcPhysical = ~0;
    unsigned int        srcWidth;
    unsigned int        srcHeight;
    RgaSURF_FORMAT      srcFormat;
    unsigned int        srcStride;

    int                 dstFd ;
    void *              dstLogical = NULL;
    void *              dstInfo     = NULL;
    unsigned int        dstPhysical = ~0;
    unsigned int        dstStride;
    unsigned int        dstWidth;
    unsigned int        dstHeight ;
    RgaSURF_FORMAT      dstFormat;
    int                 dstBpp;
    int                 x_off;
    int                 y_off;
    unsigned int        act_dstwidth;
    unsigned int        act_dstheight;

    RECT clip;
    int DstBuferIndex, ComposerIndex;
    int LcdCont;
    unsigned char       planeAlpha;
    int                 perpixelAlpha;
    int                 currentDstAddr = 0;
    unsigned int        curphyaddr = ~0;

    struct rga_req  Rga_Request[MAX_DO_SPECIAL_COUNT];
    int             RgaCnt = 0;
    int     dst_indexfid = 0;
    struct private_handle_t *handle_cur;
    static int backcout = 0;
    bool IsDiff = 0;
    unsigned int dst_bk_ddr = 0;

    for (int i = 0; i < 2 && i < list->numHwLayers ; i++)
    {
        list->hwLayers[i].exLeft = 0;
        list->hwLayers[i].exTop = 0;
        list->hwLayers[i].exRight = 0;
        list->hwLayers[i].exBottom = 0;
        list->hwLayers[i].exAddrOffset = 0;
        list->hwLayers[i].direct_fd = 0;
    }

    if ((list->numHwLayers - 1) <= 2)
    {
        return 0;
    }
    LcdCont = 0;
    for (int i = 0; i < (list->numHwLayers - 1) ; i++)
    {
        if (list->hwLayers[i].compositionType == HWC_TOWIN0 |
                list->hwLayers[i].compositionType == HWC_TOWIN1
           )
        {
            LcdCont ++;
        }
    }
    if (LcdCont != 2)
    {
        return 0;
    }

    memset(&Rga_Request, 0x0, sizeof(Rga_Request));

    for (ComposerIndex = 2 ;ComposerIndex < (list->numHwLayers - 1); ComposerIndex++)
    {
        bool IsBottom = !strcmp(BOTTOM_LAYER_NAME, list->hwLayers[ComposerIndex].LayerName);
        IsBottom |= (!strcmp(BOTTOM_LAYER_NAME1, list->hwLayers[ComposerIndex].LayerName));
        bool IsTop = !strcmp(TOP_LAYER_NAME, list->hwLayers[ComposerIndex].LayerName);
        bool IsFps = !strcmp(FPS_NAME, list->hwLayers[ComposerIndex].LayerName);
        bool NeedBlit = true;
        struct private_handle_t *srcHnd = (struct private_handle_t *) list->hwLayers[ComposerIndex].handle;

        srcFd = srcHnd->share_fd;
        hwcLockBuffer(_contextAnchor,
                      (struct private_handle_t *) list->hwLayers[ComposerIndex].handle,
                      &srcLogical,
                      &srcPhysical,
                      &srcWidth,
                      &srcHeight,
                      &srcStride,
                      &srcInfo);

        hwcGetFormat((struct private_handle_t *)list->hwLayers[ComposerIndex].handle,
                     &srcFormat
                    );

        //if( IsFps && srcFormat == RK_FORMAT_RGBA_8888 )
        //{
        //srcFormat = RK_FORMAT_RGBX_8888;
        // }

        for (DstBuferIndex = 1; DstBuferIndex >= 0; DstBuferIndex--)
        {
            int bar = 0;
            bool IsWp = strstr(list->hwLayers[DstBuferIndex].LayerName, WALLPAPER);

            hwc_layer_1_t *dstLayer = &(list->hwLayers[DstBuferIndex]);
            hwc_layer_1_t *srcLayer = &(list->hwLayers[ComposerIndex]);
            struct private_handle_t * dstHnd = (struct private_handle_t *)dstLayer->handle;

            dstFd = dstHnd->layer_fd;
            if (IsWp)
            {
                DstBuferIndex = -1;
                break;
            }

            hwcLockBuffer(_contextAnchor,
                          (struct private_handle_t *) dstLayer->handle,
                          &dstLogical,
                          &dstPhysical,
                          &dstWidth,
                          &dstHeight,
                          &dstStride,
                          &dstInfo);
            hwcGetFormat((struct private_handle_t *)dstLayer->handle,
                         &dstFormat
                        );
            if (dstHeight > 2048)
            {
                LOGV("  %d->%d: dstHeight=%d > 2048", ComposerIndex, DstBuferIndex, dstHeight);
                continue;   // RGA donot support destination vir_h > 2048
            }
            if (IsBottom) // the Navigation
            {
                int dstBpp = android::bytesPerPixel(((struct private_handle_t *)dstLayer->handle)->format);

                bool isLandscape = (dstLayer->realtransform != HAL_TRANSFORM_ROT_90) &&
                                   (dstLayer->realtransform != HAL_TRANSFORM_ROT_270);
                bool isReverse   = (dstLayer->realtransform == HAL_TRANSFORM_ROT_180) ||
                                   (dstLayer->realtransform == HAL_TRANSFORM_ROT_270);

                // Calculate the ex* value of dstLayer.
                if (isLandscape)
                {
                    bar = dstHeight - (dstLayer->displayFrame.bottom - dstLayer->displayFrame.top);
                    if (bar > 0)
                    {
                        if (!isReverse)
                            dstLayer->exTop = bar;
                        else
                            dstLayer->exBottom = bar;
                    }
                    bar = _contextAnchor->fbHeight - dstHeight;
                    if ((dstWidth == _contextAnchor->fbWidth) && (bar > 0) && (bar < 100))
                    {
                        if (!isReverse)
                        {
                            dstLayer->exBottom = bar;
                        }
                        else
                        {
                            dstLayer->exTop = bar;
                            dstLayer->exAddrOffset = -(dstBpp * dstStride * bar);
                        }
                        dstHeight += bar;
                    }
                }
                else
                {
                    bar = dstWidth - (dstLayer->displayFrame.right - dstLayer->displayFrame.left);
                    if (bar > 0)
                    {
                        if (!isReverse)
                            dstLayer->exRight = bar;
                        else
                            dstLayer->exLeft = bar;
                    }
                    bar = _contextAnchor->fbWidth - dstWidth;
                    if ((dstHeight == _contextAnchor->fbHeight) && (bar > 0) && (bar < 100))
                    {
                        if (!isReverse)
                        {
                            dstLayer->exLeft = bar;
                            dstLayer->exAddrOffset = -(dstBpp * bar);
                        }
                        else
                        {
                            dstLayer->exRight = bar;
                        }
                    }
                }
            }
            hwc_rect_t const * srcVR = srcLayer->visibleRegionScreen.rects;
            hwc_rect_t const * dstVR = dstLayer->visibleRegionScreen.rects;

            LOGV("  %d->%d:  src= rot[%d] fmt[%d] wh[%d(%d),%d] dis[%d,%d,%d,%d] vis[%d,%d,%d,%d]",
                 ComposerIndex, DstBuferIndex,
                 srcLayer->realtransform, srcFormat, srcWidth, srcStride, srcHeight,
                 srcLayer->displayFrame.left, srcLayer->displayFrame.top,
                 srcLayer->displayFrame.right, srcLayer->displayFrame.bottom,
                 srcVR->left, srcVR->top, srcVR->right, srcVR->bottom
                );
            LOGV("         dst= rot[%d] fmt[%d] wh[%d(%d),%d] dis[%d,%d,%d,%d] vis[%d,%d,%d,%d] ex[%d,%d,%d,%d-%d]",
                 dstLayer->realtransform, dstFormat, dstWidth, dstStride, dstHeight,
                 dstLayer->displayFrame.left, dstLayer->displayFrame.top,
                 dstLayer->displayFrame.right, dstLayer->displayFrame.bottom,
                 dstVR->left, dstVR->top, dstVR->right, dstVR->bottom,
                 dstLayer->exLeft, dstLayer->exTop, dstLayer->exRight, dstLayer->exBottom, dstLayer->exAddrOffset
                );

            // lcdc need address aligned to 128 byte when win1 area is too large.
            // (win0 area consider is large)
#if 0
            if ((DstBuferIndex == 1) &&
                    ((dstWidth * dstHeight * 4) >= (_contextAnchor->fbWidth * _contextAnchor->fbHeight)))
            {
                win1IsLarge = 1;
            }
            if ((dstLayer->exAddrOffset % 128) && win1IsLarge)
            {
                LOGV("  dstLayer->exAddrOffset = %d, not 128 aligned && win1 is too large!", dstLayer->exAddrOffset);
                DstBuferIndex = -1;
                break;
            }
#endif
            // display width must smaller than dst stride.
            if (dstStride < (dstVR->right - dstVR->left + dstLayer->exLeft + dstLayer->exRight))
            {
                LOGE("  dstStride[%d] < [%d + %d + %d]", dstStride, dstVR->right - dstVR->left, dstLayer->exLeft, dstLayer->exRight);
                DstBuferIndex = -1;
                break;
            }

            // incoming param error, need to debug!
            if (dstVR->right > 2048)
            {
                LOGE("  dstLayer's VR right (%d) is too big!!!", dstVR->right);
                DstBuferIndex = -1;
                break;
            }

            act_dstwidth = srcWidth;
            act_dstheight = srcHeight;
            x_off = list->hwLayers[ComposerIndex].displayFrame.left;
            y_off = list->hwLayers[ComposerIndex].displayFrame.top;


            // [1998,0,50,1536],[1952,1536]
            /* if((x_off + act_dstwidth) > dstWidth
                 || (y_off + act_dstheight ) > dstHeight ) // overflow zone
             {
                // DstBuferIndex = -1;
                 ALOGD("[%d,%d,%d,%d],[%d,%d]",x_off,y_off,act_dstwidth,act_dstheight,dstWidth,dstHeight);
                 list->hwLayers[DstBuferIndex].exLeft= 0;
                 list->hwLayers[DstBuferIndex].exTop = 0;
                 list->hwLayers[DstBuferIndex].exRight = 0;
                 list->hwLayers[DstBuferIndex].exBottom = 0;
                 list->hwLayers[DstBuferIndex].exAddrOffset = 0;
                 list->hwLayers[DstBuferIndex].direct_addr = 0;
                 continue;

             }*/

            ALOGV("(%d>%d)(%d>%d)(%d<%d)(%d<%d)", \
                  srcLayer->displayFrame.left, dstVR->left - dstLayer->exLeft, \
                  srcLayer->displayFrame.top, dstVR->top - dstLayer->exTop, \
                  srcLayer->displayFrame.right, dstVR->right + dstLayer->exRight, \
                  srcLayer->displayFrame.bottom, dstVR->bottom + dstLayer->exBottom
                 );
            // if the srcLayer inside the dstLayer, then get DstBuferIndex and break.
            if ((srcLayer->displayFrame.left >= (dstVR->left - dstLayer->exLeft))
                    && (srcLayer->displayFrame.top >= (dstVR->top - dstLayer->exTop))
                    && (srcLayer->displayFrame.right <= (dstVR->right + dstLayer->exRight))
                    && (srcLayer->displayFrame.bottom <= (dstVR->bottom + dstLayer->exBottom))
               )
            {
                handle_cur = (struct private_handle_t *)dstLayer->handle;
                break;
            }
            list->hwLayers[DstBuferIndex].exLeft = 0;
            list->hwLayers[DstBuferIndex].exTop = 0;
            list->hwLayers[DstBuferIndex].exRight = 0;
            list->hwLayers[DstBuferIndex].exBottom = 0;
            list->hwLayers[DstBuferIndex].exAddrOffset = 0;
            list->hwLayers[DstBuferIndex].direct_fd = 0;

        }

        /*       if(ComposerIndex == 2) // first find ,store
                   dst_indexfid = DstBuferIndex;
               else if( DstBuferIndex != dst_indexfid )
                   DstBuferIndex = -1;*/
        // there isn't suitable dstLayer to copy, use gpu compose.
        if (DstBuferIndex < 0)
        {
            goto BackToGPU;
        }

        if (srcFormat == RK_FORMAT_YCbCr_420_SP)
            goto BackToGPU;



        if (NeedBlit)
        {
            bool IsSblend = srcFormat == RK_FORMAT_RGBA_8888 || srcFormat == RK_FORMAT_BGRA_8888;
            bool IsDblend = dstFormat == RK_FORMAT_RGBA_8888 || dstFormat == RK_FORMAT_BGRA_8888;
            curphyaddr = dstPhysical += list->hwLayers[DstBuferIndex].exAddrOffset;
            clip.xmin = 0;
            clip.xmax = dstStride - 1;
            clip.ymin = 0;
            clip.ymax = dstHeight - 1;
            //x_off  = x_off < 0 ? 0:x_off;


            LOGV("    src[%d]=%s,  dst[%d]=%s", ComposerIndex, list->hwLayers[ComposerIndex].LayerName, DstBuferIndex, list->hwLayers[DstBuferIndex].LayerName);
            LOGV("    src info f[%d] w_h[%d(%d),%d]", srcFormat, srcWidth, srcStride, srcHeight);
            LOGV("    dst info f[%d] w_h[%d(%d),%d] rect[%d,%d,%d,%d]", dstFormat, dstWidth, dstStride, dstHeight, x_off, y_off, act_dstwidth, act_dstheight);
            RGA_set_src_vir_info(&Rga_Request[RgaCnt], srcFd, (int)0, 0, srcStride, srcHeight, srcFormat, 0);
            RGA_set_dst_vir_info(&Rga_Request[RgaCnt], dstFd, (int)0, 0, dstStride, dstHeight, &clip, dstFormat, 0);
            /* Get plane alpha. */
            planeAlpha = list->hwLayers[ComposerIndex].blending >> 16;
            /* Setup blending. */

            if (list->hwLayers[DstBuferIndex].exAddrOffset == 0 &&
                    (list->hwLayers[ComposerIndex].blending & 0xFFFF) == HWC_BLENDING_PREMULT
               )
            {

                perpixelAlpha = _HasAlpha(srcFormat);
                LOGV("perpixelAlpha=%d,planeAlpha=%d,line=%d ", perpixelAlpha, planeAlpha, __LINE__);
                /* Setup alpha blending. */
                if (perpixelAlpha && planeAlpha < 255 && planeAlpha != 0)
                {

                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 2, planeAlpha , 1, 9, 0);
                }
                else if (perpixelAlpha)
                {
                    /* Perpixel alpha only. */
                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 1, 0, 1, 3, 0);

                }
                else /* if (planeAlpha < 255) */
                {
                    /* Plane alpha only. */
                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 0, planeAlpha , 0, 0, 0);

                }

                /* SRC_ALPHA / ONE_MINUS_SRC_ALPHA. */
                /* Cs' = Cs * As
                 * As' = As
                 * C = Cs' + Cd * (1 - As)
                 * A = As' + Ad * (1 - As) */
                /* Setup alpha blending. */

            }
            /* Perpixel alpha only. */

            /* Plane alpha only. */


            /* Tips: BLENDING_NONE is non-zero value, handle zero value as
             * BLENDING_NONE. */
            /* C = Cs
             * A = As */

            RGA_set_bitblt_mode(&Rga_Request[RgaCnt], 0, 0, 0, 0, 0, 0);
            RGA_set_src_act_info(&Rga_Request[RgaCnt], srcWidth, srcHeight,  0, 0);
            RGA_set_dst_act_info(&Rga_Request[RgaCnt], act_dstwidth, act_dstheight, x_off, y_off);

            RgaCnt ++;
        }
    }

    // Check Aligned
    if (_contextAnchor->IsRk3188)
    {
        int TotalSize = 0;
        int32_t bpp ;
        bool  IsLarge = false;
        int DstLayerIndex;
        for (int i = 0; i < 2; i++)
        {
            hwc_layer_1_t *dstLayer = &(list->hwLayers[i]);
            hwc_region_t * Region = &(dstLayer->visibleRegionScreen);
            hwc_rect_t const * rects = Region->rects;
            struct private_handle_t * handle_pre = (struct private_handle_t *) dstLayer->handle;
            bpp = android::bytesPerPixel(handle_pre->format);

            TotalSize += (rects[0].right - rects[0].left) \
                         * (rects[0].bottom - rects[0].top) * 4;
        }
        // fb regard as RGBX , datasize is width * height * 4, so 0.75 multiple is width * height * 4 * 3/4
        if (TotalSize >= (_contextAnchor->fbWidth * _contextAnchor->fbHeight * 3))
        {
            IsLarge = true;
        }
        for (DstLayerIndex = 1; DstLayerIndex >= 0; DstLayerIndex--)
        {
            hwc_layer_1_t *dstLayer = &(list->hwLayers[DstLayerIndex]);

            hwc_rect_t * DstRect = &(dstLayer->displayFrame);
            hwc_rect_t * SrcRect = &(dstLayer->sourceCrop);
            hwc_region_t * Region = &(dstLayer->visibleRegionScreen);
            hwc_rect_t const * rects = Region->rects;
            struct private_handle_t * handle_pre = (struct private_handle_t *) dstLayer->handle;
            hwcRECT dstRects;
            hwcRECT srcRects;
            int xoffset;
            bpp = android::bytesPerPixel(handle_pre->format);

            hwcLockBuffer(_contextAnchor,
                          (struct private_handle_t *) dstLayer->handle,
                          &dstLogical,
                          &dstPhysical,
                          &dstWidth,
                          &dstHeight,
                          &dstStride,
                          &dstInfo);


            dstRects.left   = hwcMAX(DstRect->left,   rects[0].left);

            srcRects.left   = SrcRect->left
                              - (int)(DstRect->left   - dstRects.left);

            xoffset = hwcMAX(srcRects.left - dstLayer->exLeft, 0);


            LOGV("[%d]=%s,IsLarge=%d,dstStride=%d,xoffset=%d,exAddrOffset=%d,bpp=%d,dstPhysical=%x",
                 DstLayerIndex, list->hwLayers[DstLayerIndex].LayerName,
                 IsLarge, dstStride, xoffset, dstLayer->exAddrOffset, bpp, dstPhysical);
            if (IsLarge &&
                    ((dstStride * bpp) % 128 || (xoffset * bpp + dstLayer->exAddrOffset) % 128)
               )
            {
                LOGD("  Not 128 aligned && win is too large!") ;
                break;
            }


        }
        if (DstLayerIndex >= 0)     goto BackToGPU;
    }
    // there isn't suitable dstLayer to copy, use gpu compose.

    for (int i = 0; i < RgaCnt; i++)
    {

        uint32_t RgaFlag = (i == (RgaCnt - 1)) ? RGA_BLIT_SYNC : RGA_BLIT_ASYNC;
        if (ioctl(_contextAnchor->engine_fd, RgaFlag, &Rga_Request[i]) != 0)
        {
            LOGE(" %s(%d) RGA_BLIT fail", __FUNCTION__, __LINE__);
        }
        // bkupmanage.dstwinNo = DstBuferIndex;

    }
#if 0 // for debug ,Dont remove
    if (1)
    {
        char pro_value[PROPERTY_VALUE_MAX];
        property_get("sys.dumprga", pro_value, 0);
        static int dumcout = 0;
        if (!strcmp(pro_value, "true"))
        {
            // usleep(50000);
#if 0
            {
                int *mem = NULL;
                int *cl;
                int ii, j;
                mem = (int *)((char *)(Rga_Request[0].dst.uv_addr) + 1902 * 4);
                for (ii = 0;ii < 800;ii++)
                {
                    mem +=  2048;
                    cl = mem;
                    for (j = 0;j < 50;j++)
                    {
                        *cl = 0xffff0000;
                        cl ++;
                    }
                }
            }
#endif
#if 0
            char layername[100] ;
            void * pmem;
            FILE * pfile = NULL;
            if (bkupmanage.crrent_dis_addr != bkupmanage.direct_addr)
                pmem = (void*)Rga_Request[bkupmanage.count -1].dst.uv_addr;
            else
                pmem = (void*)bkupmanage.direct_addr_log;
            memset(layername, 0, sizeof(layername));
            system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
            sprintf(layername, "/data/dump/dmlayer%d.bin", dumcout);
            dumcout ++;
            pfile = fopen(layername, "wb");
            if (pfile)
            {
                fwrite(pmem, (size_t)(2048*300*4), 1, pfile);
                fclose(pfile);
                LOGI(" dump surface layername %s,crrent_dis_addr=%x DstBuferIndex=%d", layername, bkupmanage.crrent_dis_addr, DstBuferIndex);
            }
#endif
        }
        else
        {
            dumcout = 0;
        }
    }
#endif
//    bkupmanage.handle_bk = handle_cur;
//   bkupmanage.count = backcout;
    return 0;

BackToGPU:
    ALOGV(" go brack to GPU");
    for (size_t j = 0; j < (list->numHwLayers - 1); j++)
    {
        list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;
    }
    if (_contextAnchor->fbFd1 > 0)
    {
        _contextAnchor->fb1_cflag = true;
    }
    return 0;
}
#else

int hwc_do_special_composer(hwc_display_contents_1_t  * list)
{
    int                 srcFd;
    void *              srcLogical  = NULL;
    void *              srcInfo     = NULL;
    unsigned int        srcPhysical = ~0;
    unsigned int        srcWidth;
    unsigned int        srcHeight;
    RgaSURF_FORMAT      srcFormat;
    unsigned int        srcStride;

    int                 dstFd ;
    void *              dstLogical = NULL;
    void *              dstInfo     = NULL;
    unsigned int        dstPhysical = ~0;
    unsigned int        dstStride;
    unsigned int        dstWidth;
    unsigned int        dstHeight ;
    RgaSURF_FORMAT      dstFormat;
//    int                 dstBpp;
    int                 x_off = 0;
    int                 y_off = 0;
    unsigned int        act_dstwidth = 0;
    unsigned int        act_dstheight = 0;

    RECT clip;
    int DstBuferIndex = -1, ComposerIndex, findDstIndex;
    int LcdCont;
    unsigned char       planeAlpha;
    int                 perpixelAlpha;
//   int                 currentDstAddr = 0;
    unsigned int        curphyaddr = ~0;
    int                 curFd = 0;

    struct rga_req  Rga_Request[MAX_DO_SPECIAL_COUNT];
    int             RgaCnt = 0;
    int     dst_indexfid = 0;
    struct private_handle_t *handle_cur = NULL;
    static int backcout = 0;
    bool IsDiff = 0;
    int dst_bk_fd = 0;

    for (int i = 0; i < 2 && i < list->numHwLayers ; i++)
    {
        list->hwLayers[i].exLeft = 0;
        list->hwLayers[i].exTop = 0;
        list->hwLayers[i].exRight = 0;
        list->hwLayers[i].exBottom = 0;
        list->hwLayers[i].exAddrOffset = 0;
        list->hwLayers[i].direct_fd = 0;
    }

    if ((list->numHwLayers - 1) <= 2)
    {
        return 0;
    }
    LcdCont = 0;
    for (int i = 0; i < (list->numHwLayers - 1) ; i++)
    {
        if (list->hwLayers[i].compositionType == HWC_TOWIN0 |
                list->hwLayers[i].compositionType == HWC_TOWIN1
           )
        {
            LcdCont ++;
        }
    }
    if (LcdCont != 2)
    {
        return 0;
    }
    findDstIndex = -1;
    if (bkupmanage.ckpstcnt <= 4) // for phy mem not enough,switch to logic memery ,so one phy,three log addr ,accur err
        goto BackToGPU;

    memset(&Rga_Request, 0x0, sizeof(Rga_Request));
    for (ComposerIndex = 2 ;ComposerIndex < (list->numHwLayers - 1); ComposerIndex++)
    {
        bool IsBottom = !strcmp(BOTTOM_LAYER_NAME, list->hwLayers[ComposerIndex].LayerName);
        //   bool IsTop = !strcmp(TOP_LAYER_NAME,list->hwLayers[ComposerIndex].LayerName);
        //   bool IsFps = !strcmp(FPS_NAME,list->hwLayers[ComposerIndex].LayerName);
        bool NeedBlit = true;
        struct private_handle_t * srcHnd = (struct private_handle_t *)list->hwLayers[ComposerIndex].handle;

        srcFd = srcHnd->share_fd;
        hwcLockBuffer(_contextAnchor,
                      (struct private_handle_t *) list->hwLayers[ComposerIndex].handle,
                      &srcLogical,
                      &srcPhysical,
                      &srcWidth,
                      &srcHeight,
                      &srcStride,
                      &srcInfo);

        hwcGetFormat((struct private_handle_t *)list->hwLayers[ComposerIndex].handle,
                     &srcFormat
                    );

        //if( IsFps && srcFormat == RK_FORMAT_RGBA_8888 )
        //{
        //srcFormat = RK_FORMAT_RGBX_8888;
        // }

        for (DstBuferIndex = 1; DstBuferIndex >= 0; DstBuferIndex--)
        {
            int bar = 0;
            bool IsWp = strstr(list->hwLayers[DstBuferIndex].LayerName, WALLPAPER);

            hwc_layer_1_t *dstLayer = &(list->hwLayers[DstBuferIndex]);
            hwc_layer_1_t *srcLayer = &(list->hwLayers[ComposerIndex]);
            struct private_handle_t * dstHnd = (struct private_handle_t *)dstLayer->handle;
            dstFd = dstHnd->share_fd;

            if (IsWp)
            {
                DstBuferIndex = -1;
                break;
            }

            hwcLockBuffer(_contextAnchor,
                          (struct private_handle_t *) dstLayer->handle,
                          &dstLogical,
                          &dstPhysical,
                          &dstWidth,
                          &dstHeight,
                          &dstStride,
                          &dstInfo);
            hwcGetFormat((struct private_handle_t *)dstLayer->handle,
                         &dstFormat
                        );
            if (dstHeight > 2048)
            {
                LOGV("  %d->%d: dstHeight=%d > 2048", ComposerIndex, DstBuferIndex, dstHeight);
                continue;   // RGA donot support destination vir_h > 2048
            }

            if (IsBottom) // the Navigation
            {
                int dstBpp = android::bytesPerPixel(((struct private_handle_t *)dstLayer->handle)->format);

                bool isLandscape = (dstLayer->realtransform != HAL_TRANSFORM_ROT_90) &&
                                   (dstLayer->realtransform != HAL_TRANSFORM_ROT_270);
                bool isReverse   = (dstLayer->realtransform == HAL_TRANSFORM_ROT_180) ||
                                   (dstLayer->realtransform == HAL_TRANSFORM_ROT_270);

                // Calculate the ex* value of dstLayer.
                if (isLandscape)
                {


                    bar = _contextAnchor->fbHeight - dstHeight;
                    if ((dstWidth == _contextAnchor->fbWidth) && (bar > 0) && (bar < 100))
                    {
                        if (!isReverse)
                        {
                            dstLayer->exBottom = bar;
                        }
                        else
                        {
                            dstLayer->exTop = bar;
                            dstLayer->exAddrOffset = -(dstBpp * dstStride * bar);
                        }
                        dstHeight += bar;
                    }
                }
                else
                {
                    bar = _contextAnchor->fbWidth - dstWidth;
                    if ((dstHeight == _contextAnchor->fbHeight) && (bar > 0) && (bar < 100))
                    {
                        if (!isReverse)
                        {
                            dstLayer->exLeft = bar;
                            dstLayer->exAddrOffset = -(dstBpp * bar);
                        }
                        else
                        {
                            dstLayer->exRight = bar;
                        }
                    }
                }
            }
            hwc_rect_t const * srcVR = srcLayer->visibleRegionScreen.rects;
            hwc_rect_t const * dstVR = dstLayer->visibleRegionScreen.rects;

            LOGV("  %d->%d:  src= rot[%d] fmt[%d] wh[%d(%d),%d] dis[%d,%d,%d,%d] vis[%d,%d,%d,%d]",
                 ComposerIndex, DstBuferIndex,
                 srcLayer->realtransform, srcFormat, srcWidth, srcStride, srcHeight,
                 srcLayer->displayFrame.left, srcLayer->displayFrame.top,
                 srcLayer->displayFrame.right, srcLayer->displayFrame.bottom,
                 srcVR->left, srcVR->top, srcVR->right, srcVR->bottom
                );
            LOGV("         dst= rot[%d] fmt[%d] wh[%d(%d),%d] dis[%d,%d,%d,%d] vis[%d,%d,%d,%d] ex[%d,%d,%d,%d-%d]",
                 dstLayer->realtransform, dstFormat, dstWidth, dstStride, dstHeight,
                 dstLayer->displayFrame.left, dstLayer->displayFrame.top,
                 dstLayer->displayFrame.right, dstLayer->displayFrame.bottom,
                 dstVR->left, dstVR->top, dstVR->right, dstVR->bottom,
                 dstLayer->exLeft, dstLayer->exTop, dstLayer->exRight, dstLayer->exBottom, dstLayer->exAddrOffset
                );

            // lcdc need address aligned to 128 byte when win1 area is too large.
            // (win0 area consider is large)
#if 0
            if ((DstBuferIndex == 1) &&
                    ((dstWidth * dstHeight * 4) >= (_contextAnchor->fbWidth * _contextAnchor->fbHeight)))
            {
                win1IsLarge = 1;
            }
            if ((dstLayer->exAddrOffset % 128) && win1IsLarge)
            {
                LOGV("  dstLayer->exAddrOffset = %d, not 128 aligned && win1 is too large!", dstLayer->exAddrOffset);
                DstBuferIndex = -1;
                break;
            }
#endif
            // display width must smaller than dst stride.
            if (dstStride < (dstVR->right - dstVR->left + dstLayer->exLeft + dstLayer->exRight))
            {
                LOGE("  dstStride[%d] < [%d + %d + %d]", dstStride, dstVR->right - dstVR->left, dstLayer->exLeft, dstLayer->exRight);
                DstBuferIndex = -1;
                break;
            }

            // incoming param error, need to debug!
            if (dstVR->right > 2048)
            {
                LOGE("  dstLayer's VR right (%d) is too big!!!", dstVR->right);
                DstBuferIndex = -1;
                break;
            }

            act_dstwidth = srcWidth;
            act_dstheight = srcHeight;
            x_off = list->hwLayers[ComposerIndex].displayFrame.left;
            y_off = list->hwLayers[ComposerIndex].displayFrame.top;



            if ((x_off + act_dstwidth) > dstWidth
                    || (y_off + act_dstheight) > dstHeight)   // overflow zone
            {
                // DstBuferIndex = -1;
                ALOGV("[%d,%d,%d,%d],[%d,%d]", x_off, y_off, act_dstwidth, act_dstheight, dstWidth, dstHeight);
                list->hwLayers[DstBuferIndex].exLeft = 0;
                list->hwLayers[DstBuferIndex].exTop = 0;
                list->hwLayers[DstBuferIndex].exRight = 0;
                list->hwLayers[DstBuferIndex].exBottom = 0;
                list->hwLayers[DstBuferIndex].exAddrOffset = 0;
                list->hwLayers[DstBuferIndex].direct_fd = 0;
                continue;

            }


            // if the srcLayer inside the dstLayer, then get DstBuferIndex and break.
            if ((srcLayer->displayFrame.left >= (dstVR->left - dstLayer->exLeft))
                    && (srcLayer->displayFrame.top >= (dstVR->top - dstLayer->exTop))
                    && (srcLayer->displayFrame.right <= (dstVR->right + dstLayer->exRight))
                    && (srcLayer->displayFrame.bottom <= (dstVR->bottom + dstLayer->exBottom))
               )
            {
                //only for fps test item of cts
                char value[PROPERTY_VALUE_MAX];
                hwc_get_string_property("sys.cts_gts.status", "0", value);
                if (!strcmp(value, "true"))
                    findDstIndex = DstBuferIndex;

                handle_cur = (struct private_handle_t *)dstLayer->handle;
                break;
            }
            list->hwLayers[DstBuferIndex].exLeft = 0;
            list->hwLayers[DstBuferIndex].exTop = 0;
            list->hwLayers[DstBuferIndex].exRight = 0;
            list->hwLayers[DstBuferIndex].exBottom = 0;
            list->hwLayers[DstBuferIndex].exAddrOffset = 0;
            list->hwLayers[DstBuferIndex].direct_fd = 0;
        }

        if (ComposerIndex == 2) // first find ,store
            dst_indexfid = DstBuferIndex;
        else if (DstBuferIndex != dst_indexfid)
            DstBuferIndex = -1;
        // there isn't suitable dstLayer to copy, use gpu compose.
        if (DstBuferIndex < 0)
        {
            if (findDstIndex >= 0)
            {
                DstBuferIndex = findDstIndex;
            }
            else
            {
                goto BackToGPU;
            }
        }

        // Remove the duplicate copies of bottom bar.
        if (!(bkupmanage.dstwinNo == 0xff || bkupmanage.dstwinNo == DstBuferIndex))
        {
            ALOGW(" last and current frame is not the win,[%d - %d]", bkupmanage.dstwinNo, DstBuferIndex);
            goto BackToGPU;
        }
        if (srcFormat == RK_FORMAT_YCbCr_420_SP)
            goto BackToGPU;



        if (NeedBlit)
        {
            // bool IsSblend = srcFormat == RK_FORMAT_RGBA_8888 || srcFormat == RK_FORMAT_BGRA_8888;
            // bool IsDblend = dstFormat == RK_FORMAT_RGBA_8888 ||dstFormat == RK_FORMAT_BGRA_8888;
            //curphyaddr = dstPhysical += list->hwLayers[DstBuferIndex].exAddrOffset;
            curFd = dstFd ;
            clip.xmin = 0;
            clip.xmax = dstStride - 1;
            clip.ymin = 0;
            clip.ymax = dstHeight - 1;
            //x_off  = x_off < 0 ? 0:x_off;


            LOGV("    src[%d]=%s,  dst[%d]=%s", ComposerIndex, list->hwLayers[ComposerIndex].LayerName, DstBuferIndex, list->hwLayers[DstBuferIndex].LayerName);
            LOGV("    src info f[%d] w_h[%d(%d),%d]", srcFormat, srcWidth, srcStride, srcHeight);
            LOGV("    dst info f[%d] w_h[%d(%d),%d] rect[%d,%d,%d,%d]", dstFormat, dstWidth, dstStride, dstHeight, x_off, y_off, act_dstwidth, act_dstheight);
            RGA_set_src_vir_info(&Rga_Request[RgaCnt], srcFd, (int)0, 0, srcStride, srcHeight, srcFormat, 0);
            RGA_set_dst_vir_info(&Rga_Request[RgaCnt], dstFd, (int)0, 0, dstStride, dstHeight, &clip, dstFormat, 0);
            /* Get plane alpha. */
            planeAlpha = list->hwLayers[ComposerIndex].blending >> 16;
            /* Setup blending. */

            if (list->hwLayers[DstBuferIndex].exAddrOffset == 0 &&
                    (list->hwLayers[ComposerIndex].blending & 0xFFFF) == HWC_BLENDING_PREMULT
               )
            {

                perpixelAlpha = _HasAlpha(srcFormat);
                LOGV("perpixelAlpha=%d,planeAlpha=%d,line=%d ", perpixelAlpha, planeAlpha, __LINE__);
                /* Setup alpha blending. */
                if (perpixelAlpha && planeAlpha < 255 && planeAlpha != 0)
                {

                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 2, planeAlpha , 1, 9, 0);
                }
                else if (perpixelAlpha)
                {
                    /* Perpixel alpha only. */
                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 1, 0, 1, 3, 0);

                }
                else /* if (planeAlpha < 255) */
                {
                    /* Plane alpha only. */
                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 0, planeAlpha , 0, 0, 0);

                }

                /* SRC_ALPHA / ONE_MINUS_SRC_ALPHA. */
                /* Cs' = Cs * As
                 * As' = As
                 * C = Cs' + Cd * (1 - As)
                 * A = As' + Ad * (1 - As) */
                /* Setup alpha blending. */

            }
            /* Perpixel alpha only. */

            /* Plane alpha only. */


            /* Tips: BLENDING_NONE is non-zero value, handle zero value as
             * BLENDING_NONE. */
            /* C = Cs
             * A = As */

            RGA_set_bitblt_mode(&Rga_Request[RgaCnt], 0, 0, 0, 0, 0, 0);
            RGA_set_src_act_info(&Rga_Request[RgaCnt], srcWidth, srcHeight,  0, 0);
            RGA_set_dst_act_info(&Rga_Request[RgaCnt], act_dstwidth, act_dstheight, x_off, y_off);

            RgaCnt ++;
        }
    }

#if 1
    // Check Aligned
    if (_contextAnchor->IsRk3188)
    {
        int TotalSize = 0;
        int32_t bpp ;
        bool  IsLarge = false;
        int DstLayerIndex;
        for (int i = 0; i < 2; i++)
        {
            hwc_layer_1_t *dstLayer = &(list->hwLayers[i]);
            hwc_region_t * Region = &(dstLayer->visibleRegionScreen);
            hwc_rect_t const * rects = Region->rects;
            struct private_handle_t * handle_pre = (struct private_handle_t *) dstLayer->handle;
            bpp = android::bytesPerPixel(handle_pre->format);

            TotalSize += (rects[0].right - rects[0].left) \
                         * (rects[0].bottom - rects[0].top) * 4;
        }
        // fb regard as RGBX , datasize is width * height * 4, so 0.75 multiple is width * height * 4 * 3/4
        if (TotalSize >= (_contextAnchor->fbWidth * _contextAnchor->fbHeight * 3))
        {
            IsLarge = true;
        }
        for (DstLayerIndex = 1; DstLayerIndex >= 0; DstLayerIndex--)
        {
            hwc_layer_1_t *dstLayer = &(list->hwLayers[DstLayerIndex]);

            hwc_rect_t * DstRect = &(dstLayer->displayFrame);
            hwc_rect_t * SrcRect = &(dstLayer->sourceCrop);
            hwc_region_t * Region = &(dstLayer->visibleRegionScreen);
            hwc_rect_t const * rects = Region->rects;
            struct private_handle_t * handle_pre = (struct private_handle_t *) dstLayer->handle;
            hwcRECT dstRects;
            hwcRECT srcRects;
            int xoffset;
            bpp = android::bytesPerPixel(handle_pre->format);

            hwcLockBuffer(_contextAnchor,
                          (struct private_handle_t *) dstLayer->handle,
                          &dstLogical,
                          &dstPhysical,
                          &dstWidth,
                          &dstHeight,
                          &dstStride,
                          &dstInfo);


            dstRects.left   = hwcMAX(DstRect->left,   rects[0].left);

            srcRects.left   = SrcRect->left
                              - (int)(DstRect->left   - dstRects.left);

            xoffset = hwcMAX(srcRects.left - dstLayer->exLeft, 0);


            LOGV("[%d]=%s,IsLarge=%d,dstStride=%d,xoffset=%d,exAddrOffset=%d,bpp=%d,dstPhysical=%x",
                 DstLayerIndex, list->hwLayers[DstLayerIndex].LayerName,
                 IsLarge, dstStride, xoffset, dstLayer->exAddrOffset, bpp, dstPhysical);
            if (IsLarge &&
                    ((dstStride * bpp) % 128 || (xoffset * bpp + dstLayer->exAddrOffset) % 128)
               )
            {
                LOGV("  Not 128 aligned && win is too large!") ;
                break;
            }


        }
        if (DstLayerIndex >= 0)     goto BackToGPU;
    }
    // there isn't suitable dstLayer to copy, use gpu compose.
#endif
    /*
    if(!strcmp("Keyguard",list->hwLayers[DstBuferIndex].LayerName))
    {
         bkupmanage.skipcnt = 10;

    }
    else if( bkupmanage.skipcnt > 0)
    {
        bkupmanage.skipcnt --;
        if(bkupmanage.skipcnt > 0)
          goto BackToGPU;
    }
    */
    if (strstr(list->hwLayers[DstBuferIndex].LayerName, "Starting@#"))
    {
        goto BackToGPU;
    }

#if 0
    if (strcmp(bkupmanage.LayerName, list->hwLayers[DstBuferIndex].LayerName))
    {
        ALOGD("[%s],[%s]", bkupmanage.LayerName, list->hwLayers[DstBuferIndex].LayerName);
        strcpy(bkupmanage.LayerName, list->hwLayers[DstBuferIndex].LayerName);
        goto BackToGPU;
    }
#endif
    // Realy Blit
    // ALOGD("RgaCnt=%d",RgaCnt);

#if 0
    IsDiff = handle_cur != bkupmanage.handle_bk \
             || (handle_cur->phy_addr != bkupmanage.bkupinfo[0].buf_addr &&
                 handle_cur->phy_addr != bkupmanage.bkupinfo[bkupmanage.count -1].buf_addr);
#endif
    IsDiff = handle_cur != bkupmanage.handle_bk \
             || (curphyaddr != bkupmanage.bkupinfo[0].buf_fd &&
                 curphyaddr != bkupmanage.bkupinfo[bkupmanage.count -1].buf_fd);


//    ALOGD("enqiu6 handle_cur=%x,handle_bk=%x",handle_cur,bkupmanage.handle_bk);
    //  ALOGD("enqiu6 phy_addr=%x,buf_addr=%x",curphyaddr,bkupmanage.bkupinfo[0].buf_addr);
//    ALOGD("enqiu6 isDiff=%d",IsDiff);
    if (!IsDiff)  // restore from current display buffer
    {
        // if(bkupmanage.crrent_dis_addr != bkupmanage.direct_addr)
        if (!Is_lcdc_using(bkupmanage.direct_fd))
        {
            hwbkupinfo cpyinfo;
            ALOGV("bkupmanage.invalid=%d", bkupmanage.invalid);
            if (bkupmanage.invalid)
            {
                cpyinfo.pmem_bk = bkupmanage.bkupinfo[bkupmanage.count -1].buf_fd;
                cpyinfo.buf_addr = bkupmanage.direct_fd;
                cpyinfo.xoffset = 0;
                cpyinfo.yoffset = 0;
                cpyinfo.w_vir = bkupmanage.bkupinfo[0].w_vir;
                cpyinfo.h_vir = bkupmanage.bkupinfo[0].h_vir;
                cpyinfo.w_act = bkupmanage.bkupinfo[0].w_vir;
                cpyinfo.h_act = bkupmanage.bkupinfo[0].h_vir;
                cpyinfo.format = bkupmanage.bkupinfo[0].format;
                CopyBuffByRGA(&cpyinfo);
                bkupmanage.invalid = 0;
            }
            list->hwLayers[DstBuferIndex].direct_fd = bkupmanage.direct_fd;//bkupmanage.direct_addr - list->hwLayers[DstBuferIndex].exAddrOffset;
            dst_bk_fd = bkupmanage.crrent_dis_fd = bkupmanage.direct_fd;
            for (int i = 0; i < RgaCnt; i++)
            {
                Rga_Request[i].dst.yrgb_addr = bkupmanage.direct_fd;
            }
        }
        for (int i = 0; i < bkupmanage.count; i++)
        {
            restorebuffer(&bkupmanage.bkupinfo[i], dst_bk_fd);
        }
        if (!dst_bk_fd)
            bkupmanage.crrent_dis_fd =  bkupmanage.bkupinfo[bkupmanage.count -1].buf_fd;
    }
    for (int i = 0; i < RgaCnt; i++)
    {

        if (IsDiff) // backup the dstbuff
        {
            bkupmanage.bkupinfo[i].format = Rga_Request[i].dst.format;
            bkupmanage.bkupinfo[i].buf_fd = Rga_Request[i].dst.yrgb_addr;
            bkupmanage.bkupinfo[i].buf_addr_log = (void*)Rga_Request[i].dst.uv_addr;
            bkupmanage.bkupinfo[i].xoffset = Rga_Request[i].dst.x_offset;
            bkupmanage.bkupinfo[i].yoffset = Rga_Request[i].dst.y_offset;
            bkupmanage.bkupinfo[i].w_vir = Rga_Request[i].dst.vir_w;
            bkupmanage.bkupinfo[i].h_vir = Rga_Request[i].dst.vir_h;
            bkupmanage.bkupinfo[i].w_act = Rga_Request[i].dst.act_w;
            bkupmanage.bkupinfo[i].h_act = Rga_Request[i].dst.act_h;
            if (!i)
            {
                backcout = 0;
                bkupmanage.invalid = 1;
            }
            bkupmanage.crrent_dis_fd =  bkupmanage.bkupinfo[i].buf_fd;
            if (Rga_Request[i].src.format == RK_FORMAT_RGBA_8888)
                bkupmanage.needrev = 1;
            else
                bkupmanage.needrev = 0;
            backcout ++;
            backupbuffer(&bkupmanage.bkupinfo[i]);

        }
#if 0
        else if (i < bkupmanage.count) // restore the dstbuff
        {
            restorebuffer(&bkupmanage.bkupinfo[i], dst_bk_ddr);
            if (!dst_bk_ddr && !i)
                bkupmanage.crrent_dis_addr =  bkupmanage.bkupinfo[i].buf_addr;
        }
#endif
        uint32_t RgaFlag = (i == (RgaCnt - 1)) ? RGA_BLIT_SYNC : RGA_BLIT_ASYNC;
        if (ioctl(_contextAnchor->engine_fd, RgaFlag, &Rga_Request[i]) != 0)
        {
            LOGE(" %s(%d) RGA_BLIT fail", __FUNCTION__, __LINE__);
        }
        bkupmanage.dstwinNo = DstBuferIndex;

    }
#if 0 // for debug ,Dont remove
    if (1)
    {
        char pro_value[PROPERTY_VALUE_MAX];
        property_get("sys.dumprga", pro_value, 0);
        static int dumcout = 0;
        if (!strcmp(pro_value, "true"))
        {
            // usleep(50000);
#if 0
            {
                int *mem = NULL;
                int *cl;
                int ii, j;
                mem = (int *)((char *)(Rga_Request[0].dst.uv_addr) + 1902 * 4);
                for (ii = 0;ii < 800;ii++)
                {
                    mem +=  2048;
                    cl = mem;
                    for (j = 0;j < 50;j++)
                    {
                        *cl = 0xffff0000;
                        cl ++;
                    }
                }
            }
#endif
#if 0
            char layername[100] ;
            void * pmem;
            FILE * pfile = NULL;
            if (bkupmanage.crrent_dis_addr != bkupmanage.direct_addr)
                pmem = (void*)Rga_Request[bkupmanage.count -1].dst.uv_addr;
            else
                pmem = (void*)bkupmanage.direct_addr_log;
            memset(layername, 0, sizeof(layername));
            system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
            sprintf(layername, "/data/dump/dmlayer%d.bin", dumcout);
            dumcout ++;
            pfile = fopen(layername, "wb");
            if (pfile)
            {
                fwrite(pmem, (size_t)(2048*300*4), 1, pfile);
                fclose(pfile);
                LOGI(" dump surface layername %s,crrent_dis_addr=%x DstBuferIndex=%d", layername, bkupmanage.crrent_dis_addr, DstBuferIndex);
            }
#endif
        }
        else
        {
            dumcout = 0;
        }
    }
#endif
    bkupmanage.handle_bk = handle_cur;
    bkupmanage.count = backcout;
    return 0;

BackToGPU:
    ALOGD(" go brack to GPU");
    for (size_t j = 0; j < (list->numHwLayers - 1); j++)
    {
        list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;
    }
    if (_contextAnchor->fbFd1 > 0)
    {
        _contextAnchor->fb1_cflag = true;
    }
    return 0;
}
#endif

struct rga_req  gRga_Request_Pri[MAX_DO_SPECIAL_COUNT];
int hwc_do_Input_composer(hwc_display_contents_1_t  * list)
{
    void *              srcLogical  = NULL;
    void *              srcInfo     = NULL;
    unsigned int        srcPhysical = ~0;
    unsigned int        srcWidth;
    unsigned int        srcHeight;
    RgaSURF_FORMAT      srcFormat;
    unsigned int        srcStride;

    void *              dstLogical = NULL;
    void *              dstInfo     = NULL;
    unsigned int        dstPhysical = ~0;
    unsigned int        dstStride;
    unsigned int        dstWidth;
    unsigned int        dstHeight ;
    RgaSURF_FORMAT      dstFormat;
//    int                 dstBpp;
    int                 x_off = 0;
    int                 y_off = 0;
    unsigned int        act_dstwidth = 0;
    unsigned int        act_dstheight = 0;

    RECT clip;
    int DstBuferIndex, ComposerIndex;
    int LcdCont;
    unsigned char       planeAlpha;
    int                 perpixelAlpha;
    //  int                 currentDstAddr = 0;

    struct rga_req  Rga_Request[MAX_DO_SPECIAL_COUNT];
    int             RgaCnt = 0;
    //  int     dst_indexfid = 0;
    struct private_handle_t *handle_cur;
    //  static int backcout = 0;
    // bool IsDiff = 0;
    // unsigned int dst_bk_ddr = 0;
    static unsigned int gDst_pri;
    static unsigned int gSrcpop_pri;

    for (int i = 0; i < (list->numHwLayers - 1); i++)
    {
        list->hwLayers[i].exLeft = 0;
        list->hwLayers[i].exTop = 0;
        list->hwLayers[i].exRight = 0;
        list->hwLayers[i].exBottom = 0;
        list->hwLayers[i].exAddrOffset = 0;
        list->hwLayers[i].direct_fd = 0;
    }

    if ((list->numHwLayers - 1) <= 2)
    {
        return 0;
    }
    LcdCont = 0;
    for (int i = 0; i < (list->numHwLayers - 1) ; i++)
    {
        if (list->hwLayers[i].compositionType == HWC_TOWIN0 |
                list->hwLayers[i].compositionType == HWC_TOWIN1
           )
        {
            LcdCont ++;
        }
    }
    if (LcdCont != 2)
    {
        return 0;
    }
    bkupmanage.inputspcnt ++;
    if (bkupmanage.inputspcnt <= 20) // skip the first 4 frmaes,accur err
    {
        ALOGV(" input skip go gpu");
        goto BackToGPU;
    }

    if ((list->numHwLayers - 1) == 6) // skip input dont in writting,but acrrue tow pop
    {
        if (list->hwLayers[1].displayFrame.left <= list->hwLayers[3].displayFrame.left
                && list->hwLayers[1].displayFrame.top <= list->hwLayers[3].displayFrame.top
                && list->hwLayers[1].displayFrame.right >= list->hwLayers[3].displayFrame.right
                && list->hwLayers[1].displayFrame.bottom >= list->hwLayers[3].displayFrame.bottom
           )
            // layer3 in layer1 zone
        {
            goto BackToGPU;
        }
    }
    memset(&Rga_Request, 0x0, sizeof(Rga_Request));
    for (ComposerIndex = 1 ;ComposerIndex < (list->numHwLayers - 1); ComposerIndex++)
    {
        bool IsBottom = !strcmp(BOTTOM_LAYER_NAME, list->hwLayers[ComposerIndex].LayerName);
        bool IsTop = !strcmp(TOP_LAYER_NAME, list->hwLayers[ComposerIndex].LayerName);
        bool IsInput = !strcmp(INPUT, list->hwLayers[ComposerIndex].LayerName);

        bool NeedBlit = true;
        bool IsVer = 0;
        hwcLockBuffer(_contextAnchor,
                      (struct private_handle_t *) list->hwLayers[ComposerIndex].handle,
                      &srcLogical,
                      &srcPhysical,
                      &srcWidth,
                      &srcHeight,
                      &srcStride,
                      &srcInfo);

        hwcGetFormat((struct private_handle_t *)list->hwLayers[ComposerIndex].handle,
                     &srcFormat
                    );


        //if(strstr(list->hwLayers[ComposerIndex].LayerName,"PopupWindow"))
        // {
        // memset((void*)srcLogical, 0x55,srcWidth * srcHeight * 2 );
        //ALOGD("froce set 0x55");
        // }
        for (DstBuferIndex = 0; DstBuferIndex <= 2; DstBuferIndex += 2)
        {
            int bar = 0;
//            bool IsWp = strstr(list->hwLayers[DstBuferIndex].LayerName,WALLPAPER);

            hwc_layer_1_t *dstLayer = &(list->hwLayers[DstBuferIndex]);
            hwc_layer_1_t *srcLayer = &(list->hwLayers[ComposerIndex]);


            hwcLockBuffer(_contextAnchor,
                          (struct private_handle_t *) dstLayer->handle,
                          &dstLogical,
                          &dstPhysical,
                          &dstWidth,
                          &dstHeight,
                          &dstStride,
                          &dstInfo);
            hwcGetFormat((struct private_handle_t *)dstLayer->handle,
                         &dstFormat
                        );
            if (dstHeight > 2048)
            {
                LOGV(" [@input] %d->%d: dstHeight=%d > 2048", ComposerIndex, DstBuferIndex, dstHeight);
                continue;   // RGA donot support destination vir_h > 2048
            }

            act_dstwidth = srcWidth;
            act_dstheight = srcHeight;
            x_off = list->hwLayers[ComposerIndex].displayFrame.left;
            y_off = list->hwLayers[ComposerIndex].displayFrame.top;

            if (IsBottom || IsInput) // the Navigation
            {
                int dstBpp = android::bytesPerPixel(((struct private_handle_t *)dstLayer->handle)->format);

                bool isLandscape = (dstLayer->realtransform != HAL_TRANSFORM_ROT_90) &&
                                   (dstLayer->realtransform != HAL_TRANSFORM_ROT_270);
                bool isReverse   = (dstLayer->realtransform == HAL_TRANSFORM_ROT_180) ||
                                   (dstLayer->realtransform == HAL_TRANSFORM_ROT_270);

                // Calculate the ex* value of dstLayer.
                if (isLandscape)
                {

                    if (dstWidth != _contextAnchor->fbWidth)
                    {
                        ALOGV("dstWidth[%d]!=fbWidth[%d]", dstWidth, _contextAnchor->fbWidth);
                        DstBuferIndex = -1;
                        break;
                    }

                    bar = _contextAnchor->fbHeight - dstHeight;
                    if ((bar > 0) && (bar < 100))
                    {
                        if (!isReverse)
                        {
                            if (dstLayer->exBottom == 0)
                                dstLayer->exBottom = bar;

                        }
                        else
                        {
                            if (dstLayer->exTop == 0)
                            {
                                dstLayer->exTop = bar;
                                dstLayer->exAddrOffset = -(dstBpp * dstStride * bar);
                            }
                        }
                        dstHeight += bar;
                    }
                }
                else
                {
                    if (dstHeight != _contextAnchor->fbHeight)
                    {
                        ALOGV("dstHeight[%d]!=fbHeight[%d]", dstHeight, _contextAnchor->fbHeight);
                        DstBuferIndex = -1;
                        break;
                    }
                    bar = _contextAnchor->fbWidth - dstWidth;
                    if ((bar > 0) && (bar < 100))
                    {
                        if (!isReverse)
                        {
                            if (dstLayer->exLeft == 0)
                            {
                                dstLayer->exLeft = bar;
                                dstLayer->exAddrOffset = -(dstBpp * bar);
                            }
                        }
                        else
                        {
                            if (dstLayer->exRight == 0)
                                dstLayer->exRight = bar;
                        }
                    }
                }
            }
            else if (IsTop)
            {
                int dstBpp = android::bytesPerPixel(((struct private_handle_t *)dstLayer->handle)->format);

                // Calculate the ex* value of dstLayer.

                //ALOGD("dstLayer->realtransform=%d",dstLayer->realtransform);
                switch (dstLayer->realtransform)
                {
                    case 0:
                        {
                            bar = dstLayer->displayFrame.top;
                            if (bar > 0)
                            {
                                if (dstLayer->sourceCrop.top == bar)
                                {
                                    if (dstLayer->exTop == 0)
                                        dstLayer->exTop = bar;
                                }
                                else
                                {
                                    if (dstLayer->exTop == 0)
                                    {
                                        dstLayer->exTop = bar;
                                        dstLayer->exAddrOffset = -(dstBpp * dstStride * bar);
                                    }
                                    dstHeight += bar;
                                }
                            }
                            break;
                        }
                    case HAL_TRANSFORM_ROT_270:

                        {
                            ALOGD("HAL_TRANSFORM_ROT_270 [%d->%d]", dstLayer->sourceCrop.right, dstLayer->displayFrame.right);
                            bar = dstLayer->displayFrame.left;
                            if (bar > 0)
                            {
                                if (dstLayer->sourceCrop.left == bar)
                                {
                                    if (dstLayer->exLeft == 0)
                                        dstLayer->exLeft = bar;
                                }
                                else
                                {
                                    if (dstLayer->exLeft == 0)
                                    {
                                        dstLayer->exLeft = bar;
                                        dstLayer->exAddrOffset = -(dstBpp * dstStride * bar);
                                    }
                                    dstWidth += bar;
                                }
                            }
                            break;


                        }
                    case  HAL_TRANSFORM_ROT_180:
                        {
                            bar = _contextAnchor->fbHeight - dstLayer->displayFrame.bottom;
                            ALOGV("[%d->%d]", dstLayer->sourceCrop.bottom, dstLayer->displayFrame.bottom);
                            if (bar > 0)
                            {
                                if ((dstHeight - dstLayer->sourceCrop.bottom) == bar)
                                {
                                    if (dstLayer->exBottom == 0)
                                        dstLayer->exBottom = bar;
                                }
                                else
                                {
                                    if (dstLayer->exBottom == 0)
                                    {
                                        dstLayer->exBottom = bar;
                                    }
                                    dstHeight += bar;
                                }
                            }
                            dstHeight += dstLayer->exTop;
                            y_off = dstLayer->sourceCrop.bottom + dstLayer->exTop;
                            // ALOGD("[%d,%d]->[%d,%d]",dstLayer->sourceCrop.top,dstLayer->sourceCrop.bottom,
                            // dstLayer->displayFrame.top,dstLayer->displayFrame.bottom);
                            break;
                        }
                    case HAL_TRANSFORM_ROT_90:
                        {
                            IsVer = true;
                            bar = _contextAnchor->fbWidth - dstLayer->displayFrame.right;
                            ALOGV("HAL_TRANSFORM_ROT_90 [%d->%d]", dstLayer->sourceCrop.right, dstLayer->displayFrame.right);
                            if (bar > 0)
                            {
                                if ((_contextAnchor->fbWidth - dstLayer->sourceCrop.right) == bar)
                                {
                                    if (dstLayer->exRight == 0)
                                        dstLayer->exRight = bar;
                                }
                                else
                                {
                                    if (dstLayer->exRight == 0)
                                    {
                                        dstLayer->exRight = bar;
                                    }
                                    dstWidth += bar;
                                }
                            }
                            break;
                        }
                    default:
                        break;
                }


            }
            hwc_rect_t const * srcVR = srcLayer->visibleRegionScreen.rects;
            hwc_rect_t const * dstVR = dstLayer->visibleRegionScreen.rects;

            LOGV(" [@input] %d->%d:  src= rot[%d] fmt[%d] wh[%d(%d),%d] dis[%d,%d,%d,%d] vis[%d,%d,%d,%d]",
                 ComposerIndex, DstBuferIndex,
                 srcLayer->realtransform, srcFormat, srcWidth, srcStride, srcHeight,
                 srcLayer->displayFrame.left, srcLayer->displayFrame.top,
                 srcLayer->displayFrame.right, srcLayer->displayFrame.bottom,
                 srcVR->left, srcVR->top, srcVR->right, srcVR->bottom
                );
            LOGV(" [@input] dst= rot[%d] fmt[%d] wh[%d(%d),%d] dis[%d,%d,%d,%d] vis[%d,%d,%d,%d] ex[%d,%d,%d,%d-%d]",
                 dstLayer->realtransform, dstFormat, dstWidth, dstStride, dstHeight,
                 dstLayer->displayFrame.left, dstLayer->displayFrame.top,
                 dstLayer->displayFrame.right, dstLayer->displayFrame.bottom,
                 dstVR->left, dstVR->top, dstVR->right, dstVR->bottom,
                 dstLayer->exLeft, dstLayer->exTop, dstLayer->exRight, dstLayer->exBottom, dstLayer->exAddrOffset
                );

            // lcdc need address aligned to 128 byte when win1 area is too large.
            // (win0 area consider is large)
#if 0
            if ((DstBuferIndex == 1) &&
                    ((dstWidth * dstHeight * 4) >= (_contextAnchor->fbWidth * _contextAnchor->fbHeight)))
            {
                win1IsLarge = 1;
            }
            if ((dstLayer->exAddrOffset % 128) && win1IsLarge)
            {
                LOGV("  dstLayer->exAddrOffset = %d, not 128 aligned && win1 is too large!", dstLayer->exAddrOffset);
                DstBuferIndex = -1;
                break;
            }
#endif
            // display width must smaller than dst stride.
            if (dstStride < (dstVR->right - dstVR->left + dstLayer->exLeft + dstLayer->exRight))
            {
                LOGE(" [@input] dstStride[%d] < [%d + %d + %d]", dstStride, dstVR->right - dstVR->left, dstLayer->exLeft, dstLayer->exRight);
                DstBuferIndex = -1;
                break;
            }

            // incoming param error, need to debug!
            if (dstVR->right > 2048)
            {
                LOGE(" [@input] dstLayer's VR right (%d) is too big!!!", dstVR->right);
                DstBuferIndex = -1;
                break;
            }




            // if the srcLayer inside the dstLayer, then get DstBuferIndex and break.
            if ((srcLayer->displayFrame.left >= (dstVR->left - dstLayer->exLeft))
                    && (srcLayer->displayFrame.top >= (dstVR->top - dstLayer->exTop))
                    && (srcLayer->displayFrame.right <= (dstVR->right + dstLayer->exRight))
                    && (srcLayer->displayFrame.bottom <= (dstVR->bottom + dstLayer->exBottom))
               )
            {
                handle_cur = (struct private_handle_t *)dstLayer->handle;
                break;
            }
            list->hwLayers[DstBuferIndex].exLeft = 0;
            list->hwLayers[DstBuferIndex].exTop = 0;
            list->hwLayers[DstBuferIndex].exRight = 0;
            list->hwLayers[DstBuferIndex].exBottom = 0;
            list->hwLayers[DstBuferIndex].exAddrOffset = 0;
            list->hwLayers[DstBuferIndex].direct_fd = 0;
        }


        // there isn't suitable dstLayer to copy, use gpu compose.
        if (DstBuferIndex < 0 || DstBuferIndex > 2)      goto BackToGPU;


        if (NeedBlit)
        {
            //bool IsSblend = srcFormat == RK_FORMAT_RGBA_8888 || srcFormat == RK_FORMAT_BGRA_8888;
            //bool IsDblend = dstFormat == RK_FORMAT_RGBA_8888 ||dstFormat == RK_FORMAT_BGRA_8888;
            bool dither_en = srcFormat != dstFormat;
            hwc_layer_1_t *NaviLayer = &(list->hwLayers[list->numHwLayers -2]);
            hwc_layer_1_t *InputLayer = &(list->hwLayers[ComposerIndex]);
            bool IsBottomNavi = !strcmp(BOTTOM_LAYER_NAME, NaviLayer->LayerName);
            int x_cut = 0;
            int y_cut = 0;
            int w_cut = 0;
            int h_cut = 0;

            dstPhysical += list->hwLayers[DstBuferIndex].exAddrOffset;

            clip.xmin = 0;
            clip.xmax = dstStride - 1;
            clip.ymin = 0;
            clip.ymax = dstHeight - 1;
            //x_off  = x_off < 0 ? 0:x_off;


            LOGV(" [@input] src[%d]=%s,addr=%x,  dst[%d]=%s,addr=%x", ComposerIndex, list->hwLayers[ComposerIndex].LayerName, srcPhysical, DstBuferIndex, list->hwLayers[DstBuferIndex].LayerName, dstPhysical);
            LOGV(" [@input] src info f[%d] w_h[%d(%d),%d]", srcFormat, srcWidth, srcStride, srcHeight);
            LOGV(" [@input] dst info f[%d] w_h[%d(%d),%d] rect[%d,%d,%d,%d]", dstFormat, dstWidth, dstStride, dstHeight, x_off, y_off, act_dstwidth, act_dstheight);
            RGA_set_src_vir_info(&Rga_Request[RgaCnt], 0, (int)srcPhysical, 0, srcStride, srcHeight, srcFormat, 0);
            RGA_set_dst_vir_info(&Rga_Request[RgaCnt], 0, (int)dstPhysical, 0, dstStride, dstHeight, &clip, dstFormat, 0);
            /* Get plane alpha. */
            planeAlpha = list->hwLayers[ComposerIndex].blending >> 16;
            /* Setup blending. */

            if ((list->hwLayers[ComposerIndex].blending & 0xFFFF) == HWC_BLENDING_PREMULT)
            {

                perpixelAlpha = _HasAlpha(srcFormat);
                LOGV("[@input] perpixelAlpha=%d,planeAlpha=%d,line=%d ", perpixelAlpha, planeAlpha, __LINE__);
                /* Setup alpha blending. */
                if (perpixelAlpha && planeAlpha < 255 && planeAlpha != 0)
                {

                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 2, planeAlpha , 1, 9, 0);
                }
                else if (perpixelAlpha)
                {
                    /* Perpixel alpha only. */
                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 1, 0, 1, 3, 0);

                }
                else /* if (planeAlpha < 255) */
                {
                    /* Plane alpha only. */
                    RGA_set_alpha_en_info(&Rga_Request[RgaCnt], 1, 0, planeAlpha , 0, 0, 0);

                }

            }

            RGA_set_bitblt_mode(&Rga_Request[RgaCnt], 0, 0, 0, dither_en, 0, 0);
            //if(IsInput && IsVer)
            if (IsInput && IsBottomNavi)
            {

                switch (NaviLayer->realtransform)
                {
                    case 0:
                        {
                            h_cut =  InputLayer->displayFrame.bottom - NaviLayer->displayFrame.top;
                            if (h_cut < 0)
                            {
                                h_cut = 0;
                            }
                            break;
                        }
                    case HAL_TRANSFORM_ROT_270:

                        {
                            w_cut = InputLayer->displayFrame.right - NaviLayer->displayFrame.left;
                            if (w_cut < 0)
                            {
                                w_cut = 0;
                            }
                            break;
                        }
                    case  HAL_TRANSFORM_ROT_180:
                        {
                            h_cut =  NaviLayer->displayFrame.bottom - InputLayer->displayFrame.top;

                            if (h_cut >= 0)
                            {
                                y_cut = h_cut;
                            }
                            else
                            {
                                h_cut = 0;
                            }
                            break;
                        }
                    case HAL_TRANSFORM_ROT_90:
                        {
                            w_cut = NaviLayer->displayFrame.right - InputLayer->displayFrame.left;
                            if (w_cut >= 0)
                            {
                                x_cut = w_cut;
                            }
                            else
                            {
                                w_cut = 0;
                            }
                            break;
                        }
                    default:
                        break;
                }
                //int nvai_w = NaviLayer->displayFrame.right - NaviLayer->displayFrame.right;

            }
            RGA_set_src_act_info(&Rga_Request[RgaCnt], srcWidth - w_cut, srcHeight - h_cut,  x_cut, y_cut);
            RGA_set_dst_act_info(&Rga_Request[RgaCnt], act_dstwidth - w_cut, act_dstheight - h_cut, x_off + x_cut, y_off + y_cut);

            //  else
            //{
            // RGA_set_src_act_info(&Rga_Request[RgaCnt],srcWidth, srcHeight,  0, 0);
            // RGA_set_dst_act_info(&Rga_Request[RgaCnt], act_dstwidth, act_dstheight, x_off, y_off);
            //}
            RgaCnt ++;
        }
        if (ComposerIndex == 1)
            ComposerIndex ++;
    }

#if 1
    // Check Aligned
    if (_contextAnchor->IsRk3188)
    {
        int TotalSize = 0;
        int32_t bpp ;
        bool  IsLarge = false;
        int DstLayerIndex;
        for (int i = 0; i <= 2; i += 2)
        {
            hwc_layer_1_t *dstLayer = &(list->hwLayers[i]);
            hwc_region_t * Region = &(dstLayer->visibleRegionScreen);
            hwc_rect_t const * rects = Region->rects;
            struct private_handle_t * handle_pre = (struct private_handle_t *) dstLayer->handle;
            bpp = android::bytesPerPixel(handle_pre->format);

            TotalSize += (rects[0].right - rects[0].left) \
                         * (rects[0].bottom - rects[0].top) * 4;
        }
        // fb regard as RGBX , datasize is width * height * 4, so 1.25 multiple is width * height * 4 * 5/4
        if (TotalSize >= (_contextAnchor->fbWidth * _contextAnchor->fbHeight * 5))
        {
            IsLarge = true;
        }
        for (DstLayerIndex = 2; DstLayerIndex >= 0; DstLayerIndex -= 2)
        {
            hwc_layer_1_t *dstLayer = &(list->hwLayers[DstLayerIndex]);

            hwc_rect_t * DstRect = &(dstLayer->displayFrame);
            hwc_rect_t * SrcRect = &(dstLayer->sourceCrop);
            hwc_region_t * Region = &(dstLayer->visibleRegionScreen);
            hwc_rect_t const * rects = Region->rects;
            struct private_handle_t * handle_pre = (struct private_handle_t *) dstLayer->handle;
            hwcRECT dstRects;
            hwcRECT srcRects;
            int xoffset;
            bpp = android::bytesPerPixel(handle_pre->format);

            hwcLockBuffer(_contextAnchor,
                          (struct private_handle_t *) dstLayer->handle,
                          &dstLogical,
                          &dstPhysical,
                          &dstWidth,
                          &dstHeight,
                          &dstStride,
                          &dstInfo);


            dstRects.left   = hwcMAX(DstRect->left,   rects[0].left);

            srcRects.left   = SrcRect->left
                              - (int)(DstRect->left   - dstRects.left);

            xoffset = hwcMAX(srcRects.left - dstLayer->exLeft, 0);


            LOGV("[@input] [%d]=%s,IsLarge=%d,dstStride=%d,xoffset=%d,exAddrOffset=%d,bpp=%d,dstPhysical=%x",
                 DstLayerIndex, list->hwLayers[DstLayerIndex].LayerName,
                 IsLarge, dstStride, xoffset, dstLayer->exAddrOffset, bpp, dstPhysical);
            if (IsLarge &&
                    ((dstStride * bpp) % 128 || (xoffset * bpp + dstLayer->exAddrOffset) % 128)
               )
            {
                LOGV("[@input] Not 128 aligned && win is too large!") ;
                break;
            }


        }
        if (DstLayerIndex >= 0)     goto BackToGPU;
    }
    // there isn't suitable dstLayer to copy, use gpu compose.
#endif

    /*
       if(!strcmp("Keyguard",list->hwLayers[DstBuferIndex].LayerName))
       {
            bkupmanage.skipcnt = 10;
       }
       else if( bkupmanage.skipcnt > 0)
       {
           bkupmanage.skipcnt --;
           if(bkupmanage.skipcnt > 0)
             goto BackToGPU;
       }
    */
#if 0
    if (strcmp(bkupmanage.LayerName, list->hwLayers[DstBuferIndex].LayerName))
    {
        ALOGD("[%s],[%s]", bkupmanage.LayerName, list->hwLayers[DstBuferIndex].LayerName);
        strcpy(bkupmanage.LayerName, list->hwLayers[DstBuferIndex].LayerName);
        goto BackToGPU;
    }
#endif
    // Realy Blit
    // ALOGD("RgaCnt=%d",RgaCnt);
    // if(memcmp(gRga_Request_Pri,Rga_Request,sizeof(Rga_Request)))
    if (gDst_pri != Rga_Request[0].dst.yrgb_addr
            || gSrcpop_pri != Rga_Request[1].src.yrgb_addr)
    {
        //ALOGD("RgaCnt=%d",RgaCnt);

        for (int i = 0; i < RgaCnt; i++)
        {

            uint32_t RgaFlag = (i == (RgaCnt - 1)) ? RGA_BLIT_SYNC : RGA_BLIT_ASYNC;
            if (ioctl(_contextAnchor->engine_fd, RgaFlag, &Rga_Request[i]) != 0)
            {
                LOGE(" %s(%d) RGA_BLIT fail", __FUNCTION__, __LINE__);
            }

        }
        gDst_pri = Rga_Request[0].dst.yrgb_addr;
        gSrcpop_pri = Rga_Request[1].src.yrgb_addr;
    }
#if 0 // for debug ,Dont remove
    if (1)
    {
        char pro_value[PROPERTY_VALUE_MAX];
        property_get("sys.dumprga", pro_value, 0);
        static int dumcout = 0;
        if (!strcmp(pro_value, "true"))
        {
#if 0
            {
                int *mem = NULL;
                int *cl;
                int ii, j;
                mem = (int *)((char *)(Rga_Request[0].dst.uv_addr) + 1902 * 4);
                for (ii = 0;ii < 800;ii++)
                {
                    mem +=  2048;
                    cl = mem;
                    for (j = 0;j < 50;j++)
                    {
                        *cl = 0xffff0000;
                        cl ++;
                    }
                }
            }
#endif
#if 1
            char layername[100] ;
            void * pmem;
            FILE * pfile = NULL;
            if (bkupmanage.crrent_dis_addr != bkupmanage.direct_addr)
                pmem = (void*)Rga_Request[bkupmanage.count -1].dst.uv_addr;
            else
                pmem = (void*)bkupmanage.direct_addr_log;
            memset(layername, 0, sizeof(layername));
            system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
            sprintf(layername, "/data/dump/dmlayer%d.bin", dumcout);
            dumcout ++;
            pfile = fopen(layername, "wb");
            if (pfile)
            {
                fwrite(pmem, (size_t)(2048*300*4), 1, pfile);
                fclose(pfile);
                LOGI(" dump surface layername %s,crrent_dis_addr=%x DstBuferIndex=%d", layername, bkupmanage.crrent_dis_addr, DstBuferIndex);
            }
#endif
        }
        else
        {
            dumcout = 0;
        }
    }
#endif
    return 0;

BackToGPU:
    //ALOGD(" [@input] go brack to GPU");
    for (size_t j = 0; j < (list->numHwLayers - 1); j++)
    {
        list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;
    }
    if (_contextAnchor->fbFd1 > 0)
    {
        _contextAnchor->fb1_cflag = true;
    }
    return 0;
}

#endif

int hwc_prepare_virtual(hwc_composer_device_1_t * dev, hwc_display_contents_1_t  *contents)
{
    HWC_UNREFERENCED_PARAMETER(dev);

    if (contents == NULL)
    {
        return -1;
    }
    int i = 0;
// hwcContext * context = _contextAnchor;
    for (int j = 0; j < (contents->numHwLayers - 1); j++)
    {
        struct private_handle_t * handle = (struct private_handle_t *)contents->hwLayers[j].handle;
        if (handle && GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
        {
            ALOGV("rga_video_copybit,%x,w=%d,h=%d", \
                  GPU_BASE, GPU_WIDTH, GPU_HEIGHT);
            if (!_contextAnchor->video_frame[i].vpu_handle)
            {
                rga_video_copybit(handle, handle, i);
                i++;
            }
        }
    }

    return 0;
}

static int videoIndex;
videomix gvmix;
int
hwc_prepare(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t** displays
)
{
    size_t i;
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;
    int videoflag = 0;
#if EN_VIDEO_UI_MIX
    int videomixmode = 0;
#endif
    int iVideoCount = 0;
    hwcContext * context = _contextAnchor;

    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first

#ifndef USE_LCDC_COMPOSER
    HWC_UNREFERENCED_PARAMETER(numDisplays);
#endif

    /* Check device handle. */
    if (context == NULL
            || &context->device.common != (hw_device_t *) dev
       )
    {
        LOGE("%s(%d):Invalid device!", __FUNCTION__, __LINE__);
        return HWC_EGL_ERROR;
    }

#if hwcDumpSurface
    _DumpSurface(list);
#endif

    /* Check layer list. */
    if ((list == NULL)
            || (list->numHwLayers == 0)
            //||  !(list->flags & HWC_GEOMETRY_CHANGED)
       )
    {
        return 0;
    }
    hwc_sync(list);
    gvmix.mixflag  = 0;
    LOGV("%s(%d):>>> Preparing %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);

    property_get("sys.hwc.compose_policy", value, "0");
    new_value = atoi(value);
    new_value = 6;
    /* Roll back to FRAMEBUFFER if any layer can not be handled. */
    if (new_value <= 0)
    {
#ifdef USE_LCDC_COMPOSER
        if (context->fbFd1 > 0)
        {
            if (closeFb(context->fbFd1) == 0)
            {
                context->fbFd1 = 0;
                context->fb1_cflag = false;
            }
        }
#endif
        for (i = 0; i < (list->numHwLayers - 1); i++)
        {
            list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
        }
        return 0;
    }
#if hwcDEBUG
    LOGD("%s(%d):Layers to prepare:", __FUNCTION__, __LINE__);
    _Dump(list);
#endif
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        struct private_handle_t * handle = (struct private_handle_t *) list->hwLayers[i].handle;

        if ((list->hwLayers[i].flags & HWC_SKIP_LAYER)
                || (handle == NULL)
           )
            break;

        iVideoCount++; //Count video sources
        if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO || GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12)
        {

            videoflag = 1;

#if EN_VIDEO_UI_MIX
            if ((i == 0  //Disable Mix Mode in small window case
                    && getHdmiMode() == 0
                    && (list->numHwLayers - 1) > 2
                )
        {
            videomixmode = 1;
        }

        //Disable Mix Mode in MultiVideos case
        if (iVideoCount > 1)
            videomixmode = 0;
#endif
            break;
        }

    }
    /* Check all layers: tag with different compositionType. */
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];

        uint32_t compositionType =
            _CheckLayer(context, list->numHwLayers - 1, i, layer, list, videoflag);

        /* TODO: Viviante limitation:
         * If any layer can not be handled by hwcomposer, fail back to
         * 3D composer for all layers. We then need to tag all layers
         * with FRAMEBUFFER in that case. */
        if (compositionType == HWC_FRAMEBUFFER)
        {
            //ALOGD("line=%d back to gpu", __LINE__);
            break;
        }
    }

#ifdef USE_LCDC_COMPOSER
    if (i == (list->numHwLayers - 1))
        bkupmanage.ckpstcnt ++;
    else
    {
        bkupmanage.ckpstcnt = 0;
        bkupmanage.inputspcnt = 0;
    }
#endif
    /* Roll back to FRAMEBUFFER if any layer can not be handled. */
    if (i != (list->numHwLayers - 1))
    {
        size_t j;
        //if(new_value == 1)  // print log
        //LOGD("%s(%d):Fail back to 3D composition path,i=%x,list->numHwLayers=%d", __FUNCTION__, __LINE__,i,list->numHwLayers);

        for (j = 0; j < (list->numHwLayers - 1); j++)
        {
            list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;

            /*  // move to exit
            struct private_handle_t * handle = (struct private_handle_t *)list->hwLayers[j].handle;
            if (handle && GPU_FORMAT==HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
            {
               ALOGV("rga_video_copybit,%x,w=%d,h=%d",\
                      GPU_BASE,GPU_WIDTH,GPU_HEIGHT);
               if (!_contextAnchor->video_frame.vpu_handle)
               {
                  rga_video_copybit(handle,handle);
               }
            }
            */
        }

        if (context->fbFd1 > 0)
        {
            context->fb1_cflag = true;
        }


    }
#ifdef USE_LCDC_COMPOSER
    else if ((list->numHwLayers - 1) <= MAX_DO_SPECIAL_COUNT
             && getHdmiMode() == 0
             && !IsInputMethod())
    {
        //struct timeval tpend1, tpend2;
        //long usec1 = 0;
        // gettimeofday(&tpend1,NULL);
        hwc_do_special_composer(list);
        // gettimeofday(&tpend2,NULL);
        // usec1 = 1000*(tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec- tpend1.tv_usec)/1000;
        //  if((int)usec1 > 5)
        //   ALOGD(" hwc_do_special_composer  time=%ld ms",usec1);
        bkupmanage.inputspcnt = 0;

    }
    else if ((list->numHwLayers - 1) <= (MAX_DO_SPECIAL_COUNT + 1)
             && getHdmiMode() == 0
             && IsInputMethod())
    {
        hwc_do_Input_composer(list);
    }


    /*------------Roll back to HWC_BLITTER if any layer can not be handled by lcdc -----------*/
    if ((list->numHwLayers - 1) >= 2)
    {
        size_t LcdCont = 0;
        for (i = 0; i < (list->numHwLayers - 1) ; i++)
        {
            if ((list->hwLayers[i].compositionType == HWC_TOWIN0) |
                    (list->hwLayers[i].compositionType == HWC_TOWIN1)
               )
            {
                LcdCont ++;
            }
        }
        if (LcdCont == 1 && videoflag == 0)
        {
            for (i = 0; i < 2 ; i++)
            {
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                if (context->fbFd1 > 0)
                {
                    context->fb1_cflag = true;

                }
            }
        }
    }
    if (videomixmode)
    {
        int redraw = 0;
        int j;
        struct private_handle_t * handle ;
        list->hwLayers[0].compositionType = HWC_TOWIN0;
        if ((list->numHwLayers - 2) != gvmix.uicnt)
        {
            redraw = 1;
        }
        for (i = 1, j = 0; i < (list->numHwLayers - 1) && j < MaxMixUICnt; i++)
        {

            handle = (struct private_handle_t *) list->hwLayers[i].handle;
            if (handle &&
                    (gvmix.addr[j] != GPU_BASE ||  gvmix.alpha[j] != list->hwLayers[i].blending))
            {
                redraw = 1;
                break;
            }
            j++;
        }
        if (redraw)
        {
            for (i = 1, j = 0; i < (list->numHwLayers - 1) ; i++, j++)
            {
                handle = (struct private_handle_t *) list->hwLayers[i].handle;
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER ;
                if (handle)
                    gvmix.addr[j] = GPU_BASE;
                else
                    gvmix.addr[j] = 0;
                gvmix.alpha[j] =  list->hwLayers[i].blending;

            }
            gvmix.uicnt = list->numHwLayers - 2;

        }
        else
        {
            for (i = 1; i < (list->numHwLayers - 1) ; i++)
            {
                list->hwLayers[i].compositionType = HWC_NODRAW ;
            }

        }
        gvmix.mixflag = 1;
        //ALOGD(" video mixed redarw=%d",redraw);
    }

    /*--------------------end----------------------------*/
#endif
    if (list->numHwLayers > 1 &&
            list->hwLayers[0].compositionType == HWC_FRAMEBUFFER)  // GPU handle it ,so recover
    {
        size_t j;
#ifdef USE_LCDC_COMPOSER
        hwc_LcdcToGpu(dev, numDisplays, displays);       //Dont remove
        bkupmanage.dstwinNo = 0xff;  // GPU handle
        bkupmanage.invalid = 1;
        bkupmanage.handle_bk = NULL;
#endif
        for (j = 0; j < (list->numHwLayers - 1); j++)
        {
            struct private_handle_t * handle = (struct private_handle_t *)list->hwLayers[j].handle;

            if (handle && GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
            {
                ALOGV("rga_video_copybit,%x,w=%d,h=%d", \
                      GPU_BASE, GPU_WIDTH, GPU_HEIGHT);
                if (!_contextAnchor->video_frame[videoIndex].vpu_handle)
                {
                    rga_video_copybit(handle, handle, videoIndex);
                    videoIndex++;
                }
            }
        }
        videoIndex = 0;
    }

    hwc_display_contents_1_t* list_wfd = displays[HWC_DISPLAY_VIRTUAL];
    if (list_wfd)
    {
        hwc_prepare_virtual(dev, list_wfd);
    }
#ifdef USE_LCDC_COMPOSER
    if (context->fb1_cflag == true && context->fbFd1 > 0 && !videomixmode)
    {
        if (closeFb(context->fbFd1) == 0)
        {
            context->fbFd1 = 0;
            context->fb1_cflag = false;
        }
    }
#endif
    return 0;
}

int hwc_blank(struct hwc_composer_device_1 *dev, int dpy, int blank)
{
    // We're using an older method of screen blanking based on
    // early_suspend in the kernel.  No need to do anything here.
    hwcContext * context = _contextAnchor;

    HWC_UNREFERENCED_PARAMETER(dev);

#ifdef TARGET_BOARD_PLATFORM_RK29XX
    return 0;
#endif
    switch (dpy)
    {
        case HWC_DISPLAY_PRIMARY:
            {
                int fb_blank = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
                int err = ioctl(context->fbFd, FBIOBLANK, fb_blank);
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

    hwcContext * context = _contextAnchor;

    switch (what)
    {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            // we support the background layer
            value[0] = 1;
            break;
        case HWC_VSYNC_PERIOD:
            // vsync period in nanosecond
            value[0] = 1e9 / context->fb_fps;
            break;
        default:
            // unsupported query
            return -EINVAL;
    }
    return 0;
}



#if 0
static int hwc_fbPost(hwc_composer_device_1_t * dev, size_t numDisplays, hwc_display_contents_1_t** displays)
{
    hwcContext * context = _contextAnchor;
    //unsigned int videodata[2];
    //int ret = 0;

    HWC_UNREFERENCED_PARAMETER(dev);

    for (size_t i = 0; i < numDisplays - 1; i++)
    {
        if (context->dpyAttr[i].connected && context->dpyAttr[i].fd > 0)
        {
            hwc_display_contents_1_t *list = displays[i];
            if (list == NULL)
            {
                return -1;
            }

            last_video_addr[0] = 0;
            last_video_addr[1] = 0;

            if (context->fbFd > 0 && !context->fb_blanked)
            {
                struct fb_var_screeninfo info;
                struct rk_fb_win_cfg_data fb_info;
                memset(&fb_info, 0, sizeof(fb_info));
                int numLayers = list->numHwLayers;
                hwc_layer_1_t *fbLayer = &list->hwLayers[numLayers - 1];

                if (!fbLayer)
                {
                    ALOGE("fbLayer=NULL");
                    return -1;
                }

                info = context->info;
                struct private_handle_t*  handle;

                handle = (struct private_handle_t*)fbLayer->handle;

                if (!handle)
                {
                    ALOGE("hanndle=NULL");
                    return -1;
                }

                ALOGD("hwc_fbPost num=%d,fd=%d,base=%p", numLayers, handle->share_fd, handle->base);

                unsigned int offset = handle->offset;
                fb_info.win_par[0].data_format = context->fbhandle.format;
                fb_info.win_par[0].win_id = 0;
                fb_info.win_par[0].z_order = 0;
                fb_info.win_par[0].area_par[0].ion_fd = handle->share_fd;
#if USE_HWC_FENCE
                fb_info.win_par[0].area_par[0].acq_fence_fd = -1;//fbLayer->acquireFenceFd;
#else
                fb_info.win_par[0].area_par[0].acq_fence_fd = -1;
#endif
                fb_info.win_par[0].area_par[0].x_offset = 0;
                fb_info.win_par[0].area_par[0].y_offset = offset / context->fbStride;
                fb_info.win_par[0].area_par[0].xpos = 0;
                fb_info.win_par[0].area_par[0].ypos = 0;
                fb_info.win_par[0].area_par[0].xsize = handle->width;
                fb_info.win_par[0].area_par[0].ysize = handle->height;
                fb_info.win_par[0].area_par[0].xact = handle->width;
                fb_info.win_par[0].area_par[0].yact = handle->height;
                fb_info.win_par[0].area_par[0].xvir = handle->stride;
                fb_info.win_par[0].area_par[0].yvir = handle->height;
#if USE_HWC_FENCE
                fb_info.wait_fs = 0;
#endif
                ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &fb_info);

#if USE_HWC_FENCE

                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] != -1)
                        fbLayer->releaseFenceFd = fb_info.rel_fence_fd[k];
                }

                list->retireFenceFd = fb_info.ret_fence_fd;
                ALOGV("hwc_fbPost: releaseFd=%d,retireFd=%d", fbLayer->releaseFenceFd, list->retireFenceFd);
#endif

                for (int i = 0;i < 4;i++)
                {
                    for (int j = 0;j < 4;j++)
                    {
                        if (fb_info.win_par[i].area_par[j].ion_fd || fb_info.win_par[i].area_par[j].phy_addr)
                            ALOGV("win[%d],area[%d],z_win[%d,%d],[%d,%d,%d,%d]=>[%d,%d,%d,%d],w_h_f[%d,%d,%d],fd=%d,addr=%x",
                                  i, j,
                                  fb_info.win_par[i].z_order,
                                  fb_info.win_par[i].win_id,
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
                                  fb_info.win_par[i].data_format,
                                  fb_info.win_par[i].area_par[j].ion_fd,
                                  fb_info.win_par[i].area_par[j].phy_addr);
                    }

                }
            }
        }
    }

    return 0;
}

#else

struct rk_fb_win_config_data_g {
	int ret_fence_fd;
	int rel_fence_fd[8];
	int acq_fence_fd[8];
	bool wait_fs;
	unsigned char fence_begin;
};
static int hwc_fbPost(hwc_composer_device_1_t * dev, size_t numDisplays, hwc_display_contents_1_t** displays)
{
    hwcContext * context = _contextAnchor;
    unsigned int videodata[2];
    int ret = 0;


#if 1
    //ALOGD("numDisplays=%d",numDisplays);

    if (context->fbFd > 0 && !context->fb_blanked)
    {
        struct fb_var_screeninfo info;
        int sync = 0;
        hwc_display_contents_1_t *list = displays[0];
        int numLayers = list->numHwLayers;
        hwc_layer_1_t *fbLayer = &list->hwLayers[numLayers - 1];
        struct rk_fb_win_config_data_g fb_sync;
        
        if (!fbLayer)
        {
            ALOGE("fbLayer=NULL");
            return -1;
        }
        info = context->info;
     
        struct private_handle_t*  handle;
        handle = (struct private_handle_t*)fbLayer->handle;
        
        if (!handle)
        {
            ALOGE("hanndle=NULL,fblayer=%p",fbLayer);
            return -1;
        }
        info.yoffset = handle->offset/context->fbStride;
        ALOGV("hwc_fbPost2 num=%d,fd=%d,base=%p,offset=%d", numLayers, handle->share_fd, handle->base,info.yoffset);

        ioctl(context->dpyAttr[0].fd, FBIOPUT_VSCREENINFO, &info);
        //ioctl(context->dpyAttr[0].fd, RK_FBIOSET_CONFIG_DONE, &sync);
        memset((void*)&fb_sync,0,sizeof(rk_fb_win_config_data_g));
        fb_sync.fence_begin = 1;
        for (int j=0; j<8; j++)
        {
            fb_sync.acq_fence_fd[j] = -1;
        }
        fb_sync.wait_fs = 0;
       // ALOGD("fb config done enter");
        ioctl(context->dpyAttr[0].fd, RK_FBIOSET_CONFIG_DONE, &fb_sync);
        for (int j=0; j<8; j++)
        {
            if(fb_sync.rel_fence_fd[j] > 0)
                close(fb_sync.rel_fence_fd[j]);
        }
        /*
        if(fb_sync.rel_fence_fd[0])
            close(fb_sync.rel_fence_fd[0]);
        if(fb_sync.rel_fence_fd[1])    
            close(fb_sync.rel_fence_fd[1]);*/
        if(fb_sync.ret_fence_fd > 0)    
            close( fb_sync.ret_fence_fd);

        

    }
#else
    ALOGD("hwc_fbPost test");
#endif
    return 0;
}
static int hwc_fbPost_Rga(hwc_composer_device_1_t * dev, size_t numDisplays, hwc_display_contents_1_t** displays)

{
    hwcContext * context = _contextAnchor;
    unsigned int videodata[2];
    int ret = 0;


#if 1
    //ALOGD("numDisplays=%d",numDisplays);

    if (context->fbFd > 0 && !context->fb_blanked)
    {
        struct fb_var_screeninfo info;
        int sync = 0;
        hwc_display_contents_1_t *list = displays[0];
        int numLayers = list->numHwLayers;
        hwc_layer_1_t *fbLayer = &list->hwLayers[numLayers - 1];
        struct rk_fb_win_config_data_g fb_sync;
        
        if (!fbLayer)
        {
            ALOGE("fbLayer=NULL");
            return -1;
        }
        info = context->info;
     
        struct private_handle_t*  handle;
        handle = (struct private_handle_t*)fbLayer->handle;
        
        if (!handle)
        {
            ALOGE("hanndle=NULL,fblayer=%p",fbLayer);
            return -1;
        }
       // info.yoffset = handle->offset/context->fbStride;
        info.yoffset = context->fb_disp_ofset;
        context->fb_disp_ofset += info.yres;
        if(context->fb_disp_ofset > (info.yres*2))
            context->fb_disp_ofset = 0;
        ALOGV("hwc_fbPost2 num=%d,fd=%d,base=%p,offset=%d", numLayers, handle->share_fd, handle->base,info.yoffset);

        ioctl(context->dpyAttr[0].fd, FBIOPUT_VSCREENINFO, &info);
        //ioctl(context->dpyAttr[0].fd, RK_FBIOSET_CONFIG_DONE, &sync);
        memset((void*)&fb_sync,0,sizeof(rk_fb_win_config_data_g));
        fb_sync.fence_begin = 1;
        for (int j=0; j<8; j++)
        {
            fb_sync.acq_fence_fd[j] = -1;
        }
        fb_sync.wait_fs = 0;
       // ALOGD("fb config done enter");
        ioctl(context->dpyAttr[0].fd, RK_FBIOSET_CONFIG_DONE, &fb_sync);
        for (int j=0; j<8; j++)
        {
            if(fb_sync.rel_fence_fd[j] > 0)
                close(fb_sync.rel_fence_fd[j]);
        }
        /*
        if(fb_sync.rel_fence_fd[0])
            close(fb_sync.rel_fence_fd[0]);
        if(fb_sync.rel_fence_fd[1])    
            close(fb_sync.rel_fence_fd[1]);*/
        if(fb_sync.ret_fence_fd > 0)    
            close( fb_sync.ret_fence_fd);

        

    }
#else
    ALOGD("hwc_fbPost test");
#endif
    return 0;
}
#endif
int hwc_set_virtual(hwc_composer_device_1_t * dev, hwc_display_contents_1_t  **contents, unsigned int rga_fb_fd)
{
    hwc_display_contents_1_t* list_pri = contents[0];
    hwc_display_contents_1_t* list_wfd = contents[2];
    hwc_layer_1_t *  fbLayer = &list_pri->hwLayers[list_pri->numHwLayers - 1];
    hwc_layer_1_t *  wfdLayer = &list_wfd->hwLayers[list_wfd->numHwLayers - 1];
    hwcContext * context = _contextAnchor;
    struct timeval tpend1, tpend2;
    long usec1 = 0;

    HWC_UNREFERENCED_PARAMETER(dev);

    gettimeofday(&tpend1, NULL);
    if (list_wfd)
    {
        hwc_sync(list_wfd);
    }
    if (fbLayer == NULL || wfdLayer == NULL)
    {
        return -1;
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

        cfg.rga_fbFd = rga_fb_fd;
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
    hwcContext * context = _contextAnchor;

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

int
hwc_set(
    hwc_composer_device_1_t * dev,
    size_t numDisplays,
    hwc_display_contents_1_t  ** displays
)
{
    hwcContext * context = _contextAnchor;
    hwcSTATUS status = hwcSTATUS_OK;
    unsigned int i;

//   struct private_handle_t * handle     = NULL;

    int needSwap = false;
    EGLBoolean success = EGL_FALSE;
#if hwcUseTime
    struct timeval tpend1, tpend2;
    long usec1 = 0;
#endif
#if hwcBlitUseTime
    struct timeval tpendblit1, tpendblit2;
    long usec2 = 0;
#endif
    // int sync = 1;

    hwc_display_t dpy = NULL;
    hwc_surface_t surf = NULL;
    hwc_layer_1_t *fbLayer = NULL;
    struct private_handle_t * fbhandle = NULL;
    bool bNeedFlush = false;
    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    hwc_display_contents_1_t* list_wfd = displays[HWC_DISPLAY_VIRTUAL];
    struct rk_fb_win_cfg_data fb_info;

    memset(&fb_info, 0, sizeof(fb_info));

    rga_video_reset();
    if (list != NULL)
    {
        //dpy = list->dpy;
        //surf = list->sur;
        dpy = eglGetCurrentDisplay();
        surf = eglGetCurrentSurface(EGL_DRAW);

    }

    /* Check device handle. */
    if (context == NULL
            || &context->device.common != (hw_device_t *) dev
       )
    {
        LOGE("%s(%d): Invalid device!", __FUNCTION__, __LINE__);
        return hwcRGA_OPEN_ERR;
    }

    /* Check layer list. */
    if (list == NULL
            || list->numHwLayers == 0)
    {
        /* Reset swap rectangles. */
        return 0;
    }

    if (list->skipflag)
    {
        hwc_sync_release(list);
        ALOGW("hwc skipflag!!!");
        return 0;
    }
#if hwcUseTime
    gettimeofday(&tpend1, NULL);
#endif

    hwc_sync(list);
    LOGV("%s(%d):>>> Set start %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);

#if hwcDEBUG
    LOGD("%s(%d):Layers to set:", __FUNCTION__, __LINE__);
    _Dump(list);
#endif


    if (0 != getFbInfo(dpy, surf, list))
    {
        ALOGE("Can get fb info to context");
        return -1;
    }

    /* Prepare. */
    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        /* Check whether this composition can be handled by hwcomposer. */
        if (list->hwLayers[i].compositionType >= HWC_BLITTER)
        {
#if ENABLE_HWC_WORMHOLE
            hwcRECT FbRect;
            hwcArea * area;
            hwc_region_t holeregion;
#endif

            bNeedFlush = true;
            fbLayer = &list->hwLayers[list->numHwLayers - 1];
            if (fbLayer == NULL)
            {
                ALOGE("fbLayer is null");
                hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
            }
            fbhandle = (struct private_handle_t*)fbLayer->handle;
            struct private_handle_t *handle = fbhandle;

#if ENABLE_HWC_WORMHOLE
            /* Reset allocated areas. */
            if (context->compositionArea != NULL)
            {
                _FreeArea(context, context->compositionArea);

                context->compositionArea = NULL;
            }

            FbRect.left = 0;
            FbRect.top = 0;
            FbRect.right = GPU_WIDTH;
            FbRect.bottom = GPU_HEIGHT;

            /* Generate new areas. */
            /* Put a no-owner area with screen size, this is for worm hole,
             * and is needed for clipping. */
            context->compositionArea = _AllocateArea(context,
                                       NULL,
                                       &FbRect,
                                       0U);

            /* Split areas: go through all regions. */
            for (int i = 0; i < list->numHwLayers - 1; i++)
            {
                int owner = 1U << i;
                hwc_layer_1_t *  hwLayer = &list->hwLayers[i];
                hwc_region_t * region  = &hwLayer->visibleRegionScreen;

                //zxl:ignore PointerLocation
                if (!strcmp(hwLayer->LayerName, "PointerLocation"))
                {
                    ALOGV("ignore PointerLocation,or it will overlay the whole area");
                    continue;
                }

                /* Now go through all rectangles to split areas. */
                for (int j = 0; j < region->numRects; j++)
                {
                    /* Assume the region will never go out of dest surface. */
                    _SplitArea(context,
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
            needSwap = true;
            break;
        }
        else if (list->hwLayers[i].compositionType == HWC_FRAMEBUFFER)
        {
            /* Previous swap rectangle is gone. */
            needSwap = true;
            break;

        }
    }

    /* Go through the layer list one-by-one blitting each onto the FB */

    for (i = 0; i < list->numHwLayers; i++)
    {
        switch (list->hwLayers[i].compositionType)
        {
            case HWC_BLITTER:
                LOGV("%s(%d):Layer %d is BLIITER", __FUNCTION__, __LINE__, i);
                /* Do the blit. */
#ifdef USE_LCDC_COMPOSER
                if (IsInputMethod()) break;
#endif
#if hwcBlitUseTime
                gettimeofday(&tpendblit1, NULL);
#endif
                hwcONERROR(
                    hwcBlit(context,
                            &list->hwLayers[i],
                            fbhandle,
                            &list->hwLayers[i].sourceCrop,
                            &list->hwLayers[i].displayFrame,
                            &list->hwLayers[i].visibleRegionScreen));
#if hwcBlitUseTime
                gettimeofday(&tpendblit2, NULL);
                usec2 = 1000 * (tpendblit2.tv_sec - tpendblit1.tv_sec) + (tpendblit2.tv_usec - tpendblit1.tv_usec) / 1000;
                LOGD("hwcBlit compositer %d layers=%s use time=%ld ms", i, list->hwLayers[i].LayerName, usec2);
#endif

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

            case HWC_TOWIN0:
                {
                    LOGV("%s(%d):Layer %d is HWC_TOWIN0", __FUNCTION__, __LINE__, i);

                    struct private_handle_t * currentHandle = (struct private_handle_t *)list->hwLayers[i].handle;;
                    fbLayer = &list->hwLayers[list->numHwLayers - 1];
                    if (fbLayer == NULL)
                    {
                        ALOGE("fbLayer is null");
                        hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                    }
                    fbhandle = (struct private_handle_t*)fbLayer->handle;

                    unsigned int nowVideoAddr = 0;
                    // memcpy((void*)&nowVideoAddr, (void*)handle->base,4);
                    nowVideoAddr = currentHandle->base;
                    if ((list->numHwLayers - 1) == 1)
                    {
                        if (nowVideoAddr == last_video_addr[0])
                        {
#ifdef    USE_HWC_FENCE
                            hwc_sync_release(list);
#endif
                            if (list_wfd)
                            {
                                hwc_sync_release(list_wfd);
                            }
                            return 0;
                        }
                    }
                    last_video_addr[0] = nowVideoAddr;
                    hwcONERROR(
                        hwcLayerToWin(context,
                                      &list->hwLayers[i],
                                      fbhandle,
                                      &list->hwLayers[i].sourceCrop,
                                      &list->hwLayers[i].displayFrame,
                                      &list->hwLayers[i].visibleRegionScreen,
                                      i,
                                      0,
                                      &fb_info
                                     ));

                    if ((list->numHwLayers - 1) == 1 || i == 1
                            // #ifndef USE_LCDC_COMPOSER
                            || ((currentHandle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO || currentHandle->format == HAL_PIXEL_FORMAT_YCrCb_NV12) && !gvmix.mixflag)
                            //#endif
                       )
                    {
                        if (!context->fb_blanked)
                        {
                            if (ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &fb_info) == -1)
                            {
                                ALOGE("RK_FBIOSET_CONFIG_DONE err line=%d !", __LINE__);
                            }
                            if (context->fbFd1 > 0)
                            {
                                LOGD(" close fb1 in video");
                                //close(Context->fbFd1);
                                if (closeFb(context->fbFd1) == 0)
                                {
                                    context->fbFd1 = 0;
                                    context->fb1_cflag = false;
                                }
                            }

                            for (int j = 0;j < RK_MAX_BUF_NUM;j++)
                            {
                                if (fb_info.rel_fence_fd[j] != -1)
                                {
                                    if (j < (list->numHwLayers - 1))
                                    {
                                        //  if(mix_flag)  // mix
                                        //    list->hwLayers[i+1].releaseFenceFd = fb_info.rel_fence_fd[i];
                                        //else
                                        list->hwLayers[j].releaseFenceFd = fb_info.rel_fence_fd[j];
                                    }
                                    else
                                        close(fb_info.rel_fence_fd[j]);
                                }
                            }
#if 0
                            close(fb_info.ret_fence_fd);
                            list->retireFenceFd = -1;
#else
                            list->retireFenceFd = fb_info.ret_fence_fd;
#endif
                        }
                        else
                        {
                            for (i = 0;i < (list->numHwLayers - 1);i++)
                            {
                                list->hwLayers[i].releaseFenceFd = -1;
                            }
                            list->retireFenceFd = -1;
                        }
#ifdef    USE_HWC_FENCE
                        hwc_sync_release(list);
#endif
                        if (list_wfd)
                        {
                            hwc_sync_release(list_wfd);
                        }
                        return hwcSTATUS_OK;
                    }

                }

                break;

            case HWC_TOWIN1:
                {
                    LOGV("%s(%d):Layer %d is HWC_TOWIN1", __FUNCTION__, __LINE__, i);

                    struct private_handle_t * currentHandle = NULL;
                    fbLayer = &list->hwLayers[list->numHwLayers - 1];
                    if (fbLayer == NULL)
                    {
                        ALOGE("fbLayer is null");
                        hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                    }
                    fbhandle = (struct private_handle_t*)fbLayer->handle;


                    if (context->fbFd1 == 0)
                    {
                        context->fbFd1 = open("/dev/graphics/fb1", O_RDWR, 0);
                        if (context->fbFd1 <= 0)
                        {
                            LOGE(" fb1 open fail,return to opengl composer");
                            hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                        }
                        else
                        {
                            ALOGD("fb1 open!");
                        }

                    }
                    int addr_flag = 0;
                    //gettimeofday(&tpendblit1,NULL);
                    for (int g = 0; g < 2; g++)
                    {
                        //  hwc_layer_1_t * layer = &list->hwLayers[g];
                        currentHandle = (struct private_handle_t *)list->hwLayers[g].handle;
                        if (last_video_addr[g] != currentHandle->base)
                        {
                            addr_flag = 1;
                        }
                    }
                    //last_video_addr[1] = GPU_BASE;
                    if (addr_flag == 0)
                    {
#ifdef    USE_HWC_FENCE
                        hwc_sync_release(list);
#endif
                        if (list_wfd)
                        {
                            hwc_sync_release(list_wfd);
                        }
                        return 0;
                    }
                    hwcONERROR(
                        hwcLayerToWin(context,
                                      &list->hwLayers[i],
                                      fbhandle,
                                      &list->hwLayers[i].sourceCrop,
                                      &list->hwLayers[i].displayFrame,
                                      &list->hwLayers[i].visibleRegionScreen,
                                      i,
                                      1,
                                      &fb_info
                                     ));
                    //gettimeofday(&tpendblit2,NULL);
                    //usec2 = 1000*(tpendblit2.tv_sec - tpendblit1.tv_sec) + (tpendblit2.tv_usec- tpendblit1.tv_usec)/1000;
                    //LOGD("hwcBlit hwcVideo2LCDC %d layers=%s use time=%ld ms",i,list->hwLayers[i].LayerName,usec2);
                }
                if ((list->numHwLayers == 1) || i == 1 || IsInputMethod())
                {
                    if (!context->fb_blanked)
                    {
                        // sync = 1;
                        // ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &sync);
                        if (ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &fb_info) == -1)
                        {
                            ALOGE("RK_FBIOSET_CONFIG_DONE err line=%d !", __LINE__);
                        }

                        for (int j = 0;j < RK_MAX_BUF_NUM;j++)
                        {
                            if (fb_info.rel_fence_fd[j] != -1)
                            {
                                if (j < (list->numHwLayers - 1))
                                {
                                    //  if(mix_flag)  // mix
                                    //    list->hwLayers[i+1].releaseFenceFd = fb_info.rel_fence_fd[i];
                                    //else
                                    list->hwLayers[j].releaseFenceFd = fb_info.rel_fence_fd[j];
                                }
                                else
                                    close(fb_info.rel_fence_fd[j]);
                            }
                        }
#if 0
                        close(fb_info.ret_fence_fd);
                        list->retireFenceFd = -1;
#else
                        list->retireFenceFd = fb_info.ret_fence_fd;
#endif

                    }
                    else
                    {
                        for (i = 0;i < (list->numHwLayers - 1);i++)
                        {
                            list->hwLayers[i].releaseFenceFd = -1;
                        }
                        list->retireFenceFd = -1;
                    }
#ifdef    USE_HWC_FENCE
                    hwc_sync_release(list);
#endif
                    if (list_wfd)
                    {
                        hwc_sync_release(list_wfd);
                    }
                    return hwcSTATUS_OK;
                }
            case HWC_FRAMEBUFFER:
            case HWC_NODRAW:
                LOGV("%s(%d):Layer %d is FRAMEBUFFER", __FUNCTION__, __LINE__, i);

                if (gvmix.mixflag)
                {
                    struct fb_var_screeninfo info;
                    //  unsigned int videodata[2];

                    int numLayers = list->numHwLayers;
                    hwc_layer_1_t *fbLayer = &list->hwLayers[numLayers - 1];

                    if (fbLayer == NULL)
                    {
                        return -1;
                    }
                    info = context->info;
                    struct private_handle_t*  handle = (struct private_handle_t*)fbLayer->handle;
                    if (!handle)
                    {
                        return -1;
                    }
                    // void * vaddr = NULL;
                    // videodata[0] = videodata[1] = context->fbPhysical;

                    if (context->fbFd1 == 0)
                    {
                        context->fbFd1 = open("/dev/graphics/fb1", O_RDWR, 0);
                        if (context->fbFd1 <= 0)
                        {
                            LOGE(" fb1 open fail,return to opengl composer");
                            hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                        }
                        else
                        {
                            ALOGD("fb1 open!");
                        }

                    }
                    struct rk_fb_win_cfg_data mix_fb_info;
                    memset(&mix_fb_info, 0, sizeof(fb_info));
                    unsigned int offset = handle->offset;
                    mix_fb_info.win_par[0].win_id = 0;
                    mix_fb_info.win_par[0].z_order = 0;
                    mix_fb_info.win_par[0].area_par[0].ion_fd = handle->share_fd;
                    mix_fb_info.win_par[0].area_par[0].data_format = context->fbhandle.format;
#if USE_HWC_FENCE
                    mix_fb_info.win_par[0].area_par[0].acq_fence_fd = -1;//fbLayer->acquireFenceFd;
#else
                    mix_fb_info.win_par[0].area_par[0].acq_fence_fd = -1;
#endif
                    mix_fb_info.win_par[0].area_par[0].x_offset = 0;
                    mix_fb_info.win_par[0].area_par[0].y_offset = offset / context->fbStride;
                    mix_fb_info.win_par[0].area_par[0].xpos = 0;
                    mix_fb_info.win_par[0].area_par[0].ypos = 0;
                    mix_fb_info.win_par[0].area_par[0].xsize = handle->width;
                    mix_fb_info.win_par[0].area_par[0].ysize = handle->height;
                    mix_fb_info.win_par[0].area_par[0].xact = handle->width;
                    mix_fb_info.win_par[0].area_par[0].yact = handle->height;
                    mix_fb_info.win_par[0].area_par[0].xvir = handle->stride;
                    mix_fb_info.win_par[0].area_par[0].yvir = handle->height;
#if USE_HWC_FENCE
                    mix_fb_info.wait_fs = 0;
#endif

                    ioctl(context->fbFd1, RK_FBIOSET_CONFIG_DONE, &mix_fb_info);
#if USE_HWC_FENCE

                    for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                    {
                        if (fb_info.rel_fence_fd[k] != -1)
                            close(fb_info.rel_fence_fd[k]);
                    }
                    if (fb_info.ret_fence_fd != -1)
                        close(fb_info.ret_fence_fd);

#endif
#if 0
                    if (ioctl(context->fbFd1, FB1_IOCTL_SET_YUV_ADDR, videodata) == -1)
                    {
                        //ALOGE("%s(%d):  fd[%d] Failed,DataAddr=%x", __FUNCTION__, __LINE__,pdev->dpyAttr[i].fd,videodata[0]);
                        return -1;
                    }

                    if (handle != NULL)
                    {

                        unsigned int offset = handle->offset;
                        ALOGD("mix fb offset =%d", offset);
                        info.yoffset = offset / context->fbStride;
                        if (ioctl(context->fbFd1, FBIOPUT_VSCREENINFO, &info) != -1)
                        {
                            if (!context->fb_blanked)
                            {
                                //int sync = 0;
                                //ioctl(context->dpyAttr[i].fd, RK_FBIOSET_CONFIG_DONE, &sync);
#if 0
                                for (int m = 0; m < list->numHwLayers; m++)
                                {
                                    if (g_sync.acq_fence_fd[m] > 0)
                                    {
                                        close(g_sync.acq_fence_fd[m]);
                                    }
                                }
#endif
                                memset((void*)&g_sync, 0, sizeof(rk_fb_win_config_data));
                                g_sync.fence_begin = 1;
                                for (int j = 0; j < 16; j++)
                                {
                                    g_sync.acq_fence_fd[j] = -1;
                                }



                                g_sync.wait_fs = 0;
                                ALOGD(" video mix confingdone");
                                ioctl(context->fbFd1, RK_FBIOSET_CONFIG_DONE, &g_sync);
                                //fbLayer->releaseFenceFd = g_sync.rel_fence_fd[0];
                                list->hwLayers[0].releaseFenceFd =  g_sync.rel_fence_fd[0];
                                //close(g_sync.rel_fence_fd[0]);
                                close(g_sync.rel_fence_fd[1]);

                                list->retireFenceFd = g_sync.ret_fence_fd;
                            }

                        }
                    }
#endif

                    hwc_sync_release(list);
                    if (list_wfd)
                    {
                        hwc_sync_release(list_wfd);
                    }

                    return hwcSTATUS_OK;

                }
                break;
            default:
                LOGV("%s(%d):Layer %d is FRAMEBUFFER TARGET", __FUNCTION__, __LINE__, i);
                break;
        }
    }


    if (bNeedFlush)
    {
        
        if (ioctl(context->engine_fd, RGA_FLUSH, NULL) != 0)
        {
            LOGE("%s(%d):RGA_FLUSH Failed!", __FUNCTION__, __LINE__);
        }
        else
        {
            success =  EGL_TRUE ;
        }
#if hwcUseTime
        gettimeofday(&tpend2, NULL);
        usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
        LOGV("hwcBlit compositer %d layers use time=%ld ms", list->numHwLayers -1, usec1);
#endif

        //gettimeofday(&tpend1, NULL);

        hwc_fbPost_Rga(dev, numDisplays, displays);
        //gettimeofday(&tpend2, NULL);
       // usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
        //LOGD("hwcBlit hwc_fbPost use time=%ld ms", usec1);
        
    }
    else
    {
        hwc_fbPost(dev, numDisplays, displays);
    }



#ifdef TARGET_BOARD_PLATFORM_RK30XXB
    if (context->flag > 0)
    {
        int videodata[2];
        videodata[1] = videodata[0] = context->fbPhysical;
        ioctl(context->fbFd, 0x5002, videodata);
        context->flag = 0;
    }
#endif


    //close(Context->fbFd1);
#ifdef ENABLE_HDMI_APP_LANDSCAP_TO_PORTRAIT
    if (list != NULL && getHdmiMode() > 0)
    {
        if (bootanimFinish == 0)
        {
            char pro_value[16];
            property_get("service.bootanim.exit", pro_value, 0);
            bootanimFinish = atoi(pro_value);
            if (bootanimFinish > 0)
            {
                usleep(1000000);
            }
        }
        else
        {
            if (strstr(list->hwLayers[list->numHwLayers-1].LayerName, "FreezeSurface") <= 0)
            {

                if (list->hwLayers[0].transform == HAL_TRANSFORM_ROT_90 || list->hwLayers[0].transform == HAL_TRANSFORM_ROT_270)
                {
                    int rotation = list->hwLayers[0].transform;
                    if (ioctl(_contextAnchor->fbFd, RK_FBIOSET_ROTATE, &rotation) != 0)
                    {
                        LOGE("%s(%d):RK_FBIOSET_ROTATE error!", __FUNCTION__, __LINE__);
                    }
                }
                else
                {
                    int rotation = 0;
                    if (ioctl(_contextAnchor->fbFd, RK_FBIOSET_ROTATE, &rotation) != 0)
                    {
                        LOGE("%s(%d):RK_FBIOSET_ROTATE error!", __FUNCTION__, __LINE__);
                    }
                }
            }
        }
    }
#endif

#ifdef    USE_HWC_FENCE
    hwc_sync_release(list);
#endif
    {
        if (list_wfd)
        {
            //  int mode = -1;
            hwc_set_virtual(dev, displays, 0);
        }
    }
    return 0; //? 0 : HWC_EGL_ERROR;
OnError:

    LOGE("%s(%d):Failed!", __FUNCTION__, __LINE__);
    rga_video_reset();
    return HWC_EGL_ERROR;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
                              hwc_procs_t const* procs)
{
    hwcContext * context = _contextAnchor;

    HWC_UNREFERENCED_PARAMETER(dev);

    context->procs = (hwc_procs_t *)procs;
}


static int hwc_event_control(struct hwc_composer_device_1* dev,
                             int dpy, int event, int enabled)
{

    hwcContext * context = _contextAnchor;

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

static void handle_vsync_event(hwcContext * context)
{

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

    uint64_t mNextFakeVSync = timestamp + (uint64_t)(1e9 / context->fb_fps);


    struct timespec spec;
    spec.tv_sec  = mNextFakeVSync / 1000000000;
    spec.tv_nsec = mNextFakeVSync % 1000000000;

    do
    {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    }
    while (err < 0 && errno == EINTR);


    if (err == 0)
    {
        context->procs->vsync(context->procs, 0, mNextFakeVSync);
        //ALOGD(" timestamp=%lld ms,preid=%lld us",mNextFakeVSync/1000000,(uint64_t)(1e6 / context->fb_fps) );
    }
    else
    {
        ALOGE(" clock_nanosleep ERR!!!");
    }
}

static void *hwc_thread(void *data)
{
    hwcContext * context = _contextAnchor;

    HWC_UNREFERENCED_PARAMETER(data);


#if 0
    uint64_t timestamp = 0;
    nsecs_t now = 0;
    nsecs_t next_vsync = 0;
    nsecs_t sleep;
    const nsecs_t period = nsecs_t(1e9 / 50.0);
    struct timespec spec;
    // int err;
    do
    {

        now = systemTime(CLOCK_MONOTONIC);
        next_vsync = context->mNextFakeVSync;

        sleep = next_vsync - now;
        if (sleep < 0)
        {
            // we missed, find where the next vsync should be
            sleep = (period - ((now - next_vsync) % period));
            next_vsync = now + sleep;
        }
        context->mNextFakeVSync = next_vsync + period;

        spec.tv_sec  = next_vsync / 1000000000;
        spec.tv_nsec = next_vsync % 1000000000;

        do
        {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        }
        while (err < 0 && errno == EINTR);

        if (err == 0)
        {
            if (context->procs && context->procs->vsync)
            {
                context->procs->vsync(context->procs, 0, next_vsync);

                ALOGD(" hwc_thread next_vsync=%lld ", next_vsync);
            }

        }

    }
    while (1);
#endif

    //    char uevent_desc[4096];
    // memset(uevent_desc, 0, sizeof(uevent_desc));



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
    hwcContext * context = _contextAnchor;
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

    for (int i = 0;i < bakupbufsize;i++)
    {
        if (bkupmanage.bkupinfo[i].phd_bk)
        {
            err = context->mAllocDev->free(context->mAllocDev, bkupmanage.bkupinfo[i].phd_bk);
            ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
        }

    }

    if (bkupmanage.phd_drt)
    {
        err = context->mAllocDev->free(context->mAllocDev, bkupmanage.phd_drt);
        ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
    }

    if (context->ippDev)
    {
        ipp_close(context->ippDev);
    }

    if (context->phd_bk)
    {
        err = context->mAllocDev->free(context->mAllocDev, context->phd_bk);
        ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
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

    _contextAnchor = NULL;

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
                values[i] = (int32_t)(pdev->dpyAttr[disp].xdpi * 1000.0);
                break;
            case HWC_DISPLAY_DPI_Y:
                values[i] = (int32_t)(pdev->dpyAttr[disp].ydpi * 1000.0);
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

static int copybit_video_index = 0;
int hwc_copybit(struct hwc_composer_device_1 *dev, buffer_handle_t src_handle, buffer_handle_t dst_handle, int flag)
{
    HWC_UNREFERENCED_PARAMETER(dev);

    if (src_handle == 0 || dst_handle == 0)
    {
        return -1;
    }
    struct private_handle_t *srcHandle = (struct private_handle_t *)src_handle;
    struct private_handle_t *dstHandle = (struct private_handle_t *)dst_handle;
#ifndef  TARGET_BOARD_PLATFORM_RK30XXB
    if (srcHandle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO \
            && srcHandle->format == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
#else
    if (srcHandle->iFormat == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO \
            && srcHandle->iFormat == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
#endif
    {
        if (flag > 0)
        {
            rga_video_copybit(srcHandle, dstHandle, copybit_video_index);
            copybit_video_index++;
        }
        if (flag == 0)
        {
            rga_video_reset();
            copybit_video_index = 0;
        }

    }
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

    LOGD("%s(%d):Open hwc device in thread=%d",
         __FUNCTION__, __LINE__, gettid());

    *device = NULL;

    if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0)
    {
        LOGE("%s(%d):Invalid device name!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    /* Get context. */
    context = _contextAnchor;

    /* Return if already initialized. */
    if (context != NULL)
    {
        /* Increament reference count. */
        context->reference++;

        *device = &context->device.common;
        return 0;
    }

    /* TODO: Find a better way instead of EGL_ANDROID_get_render_buffer. */
    _eglGetRenderBufferANDROID = (PFNEGLGETRENDERBUFFERANDROIDPROC)
                                 eglGetProcAddress("eglGetRenderBufferANDROID");

#ifndef TARGET_BOARD_PLATFORM_RK30XXB

    _eglRenderBufferModifiedANDROID = (PFNEGLRENDERBUFFERMODIFYEDANDROIDPROC)
                                      eglGetProcAddress("eglRenderBufferModifiedANDROID");
#endif

    if (_eglGetRenderBufferANDROID == NULL)
    {
        LOGE("EGL_ANDROID_get_render_buffer extension "
             "Not Found for hwcomposer");

        return HWC_EGL_ERROR;
    }
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
    if (_eglRenderBufferModifiedANDROID == NULL)
    {
        LOGE("EGL_ANDROID_buffer_modifyed extension "
             "Not Found for hwcomposer");

        return HWC_EGL_ERROR;
    }
#endif
    /* Allocate memory. */
    context = (hwcContext *) malloc(sizeof(hwcContext));

    if (context == NULL)
    {
        LOGE("%s(%d):malloc Failed!", __FUNCTION__, __LINE__);
        return -EINVAL;
    }
    memset(&gvmix, 0, sizeof(videomix));

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

    refreshRate = 1000000000000LLU /
                  (
                      uint64_t(info.upper_margin + info.lower_margin + info.yres)
                      * (info.left_margin  + info.right_margin + info.xres)
                      * info.pixclock
                  );

    if (refreshRate == 0)
    {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60 * 1000;
    }


    vsync_period  = 1000000000 / refreshRate;

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

    context->device.fbPost2 = hwc_fbPost;
    context->device.dump = hwc_dump;
#ifdef USE_LCDC_COMPOSER
    context->device.layer_recover   = hwc_layer_recover;
#endif

    context->membk_index = 0;

    /* Get gco2D object pointer. */
    context->engine_fd = open("/dev/rga", O_RDWR, 0);
    if (context->engine_fd < 0)
    {
        hwcONERROR(hwcRGA_OPEN_ERR);

    }

#if ENABLE_WFD_OPTIMIZE
    property_set("sys.enable.wfd.optimize", "1");
#endif
    {
        char value[PROPERTY_VALUE_MAX];
        memset(value, 0, PROPERTY_VALUE_MAX);
        property_get("sys.enable.wfd.optimize", value, "0");
        int type = atoi(value);
        context->wfdOptimize = type;
        init_rga_cfg(context->engine_fd);
        if (type > 0 && !is_surport_wfd_optimize())
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
    property_get("ro.rk.soc", pro_value, "0");
    context->IsRk3188 = !strcmp(pro_value, "rk3188");
    context->IsRk3126 = !strcmp(pro_value, "rk3126");
    context->fbSize = context->fbStride * info.yres * 3;//info.xres*info.yres*4*3;
    context->lcdSize = context->fbStride * info.yres;//info.xres*info.yres*4;


    /* Increment reference count. */
    context->reference++;
    _contextAnchor = context;
    if (context->fbWidth > context->fbHeight)
    {
        property_set("sys.display.oritation", "0");
    }
    else
    {
        property_set("sys.display.oritation", "2");
    }
#ifdef USE_LCDC_COMPOSER
    memset(&bkupmanage, 0, sizeof(hwbkupmanage));
    bkupmanage.dstwinNo = 0xff;
#if 0
    ion_open(fixInfo.line_length * context->fbHeight * 2, ION_MODULE_UI, &context->rk_ion_device);
    rel = context->rk_ion_device->alloc(context->rk_ion_device, fixInfo.line_length * context->fbHeight * 2 , _ION_HEAP_RESERVE, &context->pion);
    if (!rel)
    {
        int i;
        for (i = 0;i < bakupbufsize;i++)
        {
            bkupmanage.bkupinfo[i].pmem_bk = context->pion->phys + (fixInfo.line_length * context->fbHeight / 4) * i;
            bkupmanage.bkupinfo[i].pmem_bk_log = (void*)((int)context->pion->virt + (fixInfo.line_length * context->fbHeight / 4) * i);
        }
        bkupmanage.direct_addr = context->pion->phys + (fixInfo.line_length * context->fbHeight / 4) * i;
        bkupmanage.direct_addr_log = (void*)((int)context->pion->virt + (fixInfo.line_length * context->fbHeight / 4) * i);
    }
    else
    {
        ALOGE(" ion_device->alloc failed");
    }
#else
    //alloc Physically contiguous address for "Backup and restore mechanism"
    for (int  i = 0;i < bakupbufsize;i++)
    {
        err = context->mAllocDev->alloc(context->mAllocDev, context->fbhandle.width, \
                                        context->fbhandle.height / bakupbufsize, context->fbhandle.format, \
                                        GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER, \
                                        (buffer_handle_t*)(&bkupmanage.bkupinfo[i].phd_bk), &stride_gr);
        if (!err)
        {
            struct private_handle_t*phandle_gr = (struct private_handle_t*)bkupmanage.bkupinfo[i].phd_bk;
            bkupmanage.bkupinfo[i].membk_fd = phandle_gr->share_fd;
            ALOGD("@hwc alloc membk_fd [%d] [%dx%d,f=%d],fd=%d", i, phandle_gr->width, phandle_gr->height, phandle_gr->format, phandle_gr->share_fd);
        }
        else
        {
            ALOGE("@hwc alloc membk_fd [%d] faild", i);
            goto OnError;
        }
    }

    err = context->mAllocDev->alloc(context->mAllocDev, context->fbhandle.width, \
                                    context->fbhandle.height, context->fbhandle.format, \
                                    GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER, \
                                    (buffer_handle_t*)(&bkupmanage.phd_drt), &stride_gr);

    if (!err)
    {
        struct private_handle_t*phandle_gr = (struct private_handle_t*)bkupmanage.phd_drt;
        bkupmanage.direct_fd = phandle_gr->share_fd;
        ALOGD("@hwc alloc direct_fd [%dx%d,f=%d],fd=%d ", phandle_gr->width, phandle_gr->height, phandle_gr->format, phandle_gr->share_fd);
    }
    else
    {
        ALOGE("hwc alloc direct_fd faild");
        goto OnError;
    }
#endif
#endif

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

#ifdef TARGET_BOARD_PLATFORM_RK30XXB
    property_set("sys.ghwc.version", GHWC_VERSION);
#else
#ifdef USE_LCDC_COMPOSER
    char acVersion[100];
    memset(acVersion, 0, 100);
    strcpy(acVersion, GHWC_VERSION);
    strcat(acVersion, "_LCP");
    property_set("sys.ghwc.version", acVersion);
#else
    property_set("sys.ghwc.version", GHWC_VERSION);
#endif
#endif

    LOGD(HWC_VERSION);

    char Version[32];

    memset(Version, 0, sizeof(Version));
    if (ioctl(context->engine_fd, RGA_GET_VERSION, Version) == 0)
    {
        property_set("sys.grga.version", Version);
        LOGD(" rga version =%s", Version);

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

    for (int i = 0;i < bakupbufsize;i++)
    {
        if (bkupmanage.bkupinfo[i].phd_bk)
        {
            err = context->mAllocDev->free(context->mAllocDev, bkupmanage.bkupinfo[i].phd_bk);
            ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
        }

    }

    if (bkupmanage.phd_drt)
    {
        err = context->mAllocDev->free(context->mAllocDev, bkupmanage.phd_drt);
        ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
    }

    if (context->phd_bk)
    {
        err = context->mAllocDev->free(context->mAllocDev, context->phd_bk);
        ALOGW_IF(err, "free(...) failed %d (%s)", err, strerror(-err));
    }

#if 0
    if (context->rk_ion_device)
    {
        context->rk_ion_device->free(context->rk_ion_device, context->pion);
        ion_close(context->rk_ion_device);
    }
#endif
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
        /* if (g_hdmi_mode==0)
         {
             property_set("sys.hdmi.mode", "0");
         }
         else
         {
             property_set("sys.hdmi.mode", "1");
         }*/

    }
    else
    {
        LOGE("Open hdmi mode error.");
    }

}
int closeFb(int fd)
{
    if (fd > 0)
    {
        int disable = 0;

        if (ioctl(fd, 0x5019, &disable) == -1)
        {
            LOGE("close fb[%d] fail.", fd);
            return -1;
        }
        ALOGD("fb1 realy close!");
        return (close(fd));
    }
    return -1;
}
