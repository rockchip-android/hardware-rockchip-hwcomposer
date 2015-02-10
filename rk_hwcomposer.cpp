/*

* rockchip hwcomposer( 2D graphic acceleration unit) .

*

* Copyright (C) 2015 Rockchip Electronics Co., Ltd.

*/




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
#include <ui/PixelFormat.h>

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
                            "HWC_RGA_TRSM_VOP",
                            "HWC_RGA_TRSM_GPU_VOP",
                            "HWC_VOP_GPU",
                            "HWC_NODRAW_GPU_VOP",
                            "HWC_RGA_GPU_VOP",
                            "HWC_CP_FB",
                            "HWC_GPU"
                          };


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
static int DetectValidData(int *data,int w,int h);

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
static int is_out_log( void )
{
    return hwc_get_int_property("sys.hwc.log","0");
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
    for (int r = 0; r < (unsigned int) Region->numRects ; r++)
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
        ALOGW("zone is small ,LCDC can not support");
        return -1;
    }
    return 0;
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
static PFNEGLGETRENDERBUFFERANDROIDPROC _eglGetRenderBufferANDROID;
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
static PFNEGLRENDERBUFFERMODIFYEDANDROIDPROC _eglRenderBufferModifiedANDROID;
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
    for (int r = 0; r < (unsigned int) Region->numRects ; r++)
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

    for (unsigned int i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        if(layer)
            layer->dospecialflag = 0;
    }    

    if((list->numHwLayers - 1) <= 0)  // vop not support
    {
        return -1;
    }
    hwc_en = hwc_get_int_property("sys.hwc.enable","0");
    if(!hwc_en)
    {
        return -1;
    }
    
    return 0;
}

int try_hwc_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{
    int scale_cnt = 0;
    hwcContext * context = (hwcContext *)ctx;
    if((list->numHwLayers - 1) > VOP_WIN_NUM)  // vop not support
    {
        return -1;
    }
    
    for (unsigned int i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        float hfactor = 1.0;
        float vfactor = 1.0;
        if (layer->flags & HWC_SKIP_LAYER
            || layer->transform != 0
            || layer->handle == NULL
          )
        {
            return -1;
        }      
        if(LayerZoneCheck(layer,context) != 0)
            return -1;
        hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
            / (layer->displayFrame.right - layer->displayFrame.left);

        vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
            / (layer->displayFrame.bottom - layer->displayFrame.top);
        if(hfactor != 1.0 || vfactor != 1.0)
        {
            scale_cnt ++;
            if(i>0)
            {
                context->win_swap = 1;
                ALOGV("i=%d,swap",i);
            }
        }
        if(scale_cnt > 1 || (context->vop_mbshake && vfactor > 1.0)) // vop has only one win support scale,or vop sacle donwe need more BW lead to vop shake
        {
            context->win_swap = 0;
            return -1;
        }   
        if(i == 0)
            layer->compositionType = HWC_TOWIN0;
        else
            layer->compositionType = HWC_TOWIN1;
        
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
    
   // RGA_POLICY_MAX_SIZE
    for (  i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
        {
            ALOGV("rga policy skip");
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
        hfactor = (float)(layer->sourceCrop.right - layer->sourceCrop.left)
                  / (layer->displayFrame.right - layer->displayFrame.left);

        vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                  / (layer->displayFrame.bottom - layer->displayFrame.top);
        if(hfactor != 1.0f 
           || vfactor != 1.0f
           || layer->transform != 0
          )   // because rga scale & transform  too slowly,so return to opengl        
        {
            ALOGV("RGA_policy not support [%f,%f,%d]",hfactor,vfactor,layer->transform );
            return -1;
        }
        pixelSize += ((layer->sourceCrop.bottom - layer->sourceCrop.top) * \
                        (layer->sourceCrop.right - layer->sourceCrop.left));
        if(pixelSize > RGA_POLICY_MAX_SIZE )  // pixel too large,RGA done use more time
        {
            ALOGV("pielsize=%d,max_size=%d",pixelSize ,RGA_POLICY_MAX_SIZE);
            return -1;
        }    

        layer->compositionType = HWC_BLITTER;
        
    }
    context->composer_mode = HWC_RGA;
    ALOGV("hwc-prepare use HWC_RGA policy");

    return 0;
}

// video two layers ,one need transform, 0->rga_trfm->buffer->win0, 1->win1
int try_hwc_rga_trfm_vop_policy(void * ctx,hwc_display_contents_1_t *list)
{

#if 1
    float hfactor = 1;
    float vfactor = 1;
    bool isYuvModtrfm = false;
    int  pixelSize  = 0;
    unsigned int i ;
    hwcContext * context = (hwcContext *)ctx;
    
   // RGA_POLICY_MAX_SIZE


    if((list->numHwLayers - 1) > VOP_WIN_NUM)  // vop not support
    {
        return -1;
    }

    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
        {
            return -1;  
        }
        if(handle->format == HAL_PIXEL_FORMAT_YCrCb_NV12 
            &&(context->vop_mbshake || layer->transform != 0))  // video use other policy
        {
            isYuvModtrfm = true;
        }    
    }    
    ALOGD("isYuvModtrfm=%d",isYuvModtrfm);
    if(!isYuvModtrfm)
        return -1;
        
    for ( i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
      
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
                return -1;
            }
            #if VIDEO_WIN1_UI_DISABLE
            if(context->vop_mbshake)
            {
                int ret = DetectValidData((int *)handle->base,handle->width,handle->height); 
                if(ret) // ui need display
                {
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
   // RGA_POLICY_MAX_SIZE
    hwcContext * context = (hwcContext *)ctx;

    hwc_layer_1_t * layer = &list->hwLayers[0];
    struct private_handle_t * handle = (struct private_handle_t *)layer->handle;

    #if VIDEO_WIN1_UI_DISABLE
    if(context->vop_mbshake)
    {
        return -1;  
    }
    #endif
    if ((layer->flags & HWC_SKIP_LAYER) || (handle == NULL))
    {
        return -1;  
    }
    if(handle->format != HAL_PIXEL_FORMAT_YCrCb_NV12 || layer->transform == 0)  // video use other policy
    {
        return -1;
    }    
    layer->compositionType = HWC_BLITTER;
    
    for ( i = 1; i < (list->numHwLayers - 1); i++)
    {
        layer->compositionType = HWC_FRAMEBUFFER;
    }
    context->composer_mode = HWC_RGA_TRSM_GPU_VOP;
    ALOGV("hwc-prepare use HHWC_RGA_TRSM_GPU_VOP policy");

    return 0;
#else
    return -1;
#endif
}


// > 2 layers, 0->win0 ,1\2\3->FB->win1
int try_hwc_vop_gpu_policy(void * ctx,hwc_display_contents_1_t *list)
{
    float hfactor = 1;
    float vfactor = 1;
    bool isYuvMod = false;
    unsigned int i ;
   // RGA_POLICY_MAX_SIZE
    hwcContext * context = (hwcContext *)ctx;

    
    hwc_layer_1_t * layer = &list->hwLayers[0];
    struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
    if ((layer->flags & HWC_SKIP_LAYER) 
        || handle == NULL
        || layer->transform != 0
        ||(list->numHwLayers - 1)>4)
    {
        return -1;  
    }

    for (i = 0; i < (list->numHwLayers - 1); i++)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        struct private_handle_t * handle = (struct private_handle_t *)layer->handle;
        if(i == 0)
        {
            if(context->vop_mbshake)
            {
                float vfactor = 1.0;
                vfactor = (float)(layer->sourceCrop.bottom - layer->sourceCrop.top)
                  / (layer->displayFrame.bottom - layer->displayFrame.top);
                if(vfactor > 1.0f  )   //   vop sacle donwe need more BW lead to vop shake      
                {
                    return -1;
                }
            }    
            if(LayerZoneCheck(layer,context) != 0)
                return -1;
            else
                layer->compositionType = HWC_TOWIN0;
        }    
        else
            layer->compositionType = HWC_FRAMEBUFFER;
        
    }
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


int try_hwc_cp_fb_policy(void * ctx,hwc_display_contents_1_t *list)
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

    /* Check layer list. */
    if ((list == NULL)
            || (list->numHwLayers == 0)
            //||  !(list->flags & HWC_GEOMETRY_CHANGED)
       )
    {
        return 0; 
    }
    
    LOGV("%s(%d):>>> prepare_primary %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);
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
    ALOGV("cmp_mode=%s",compositionModeName[context->composer_mode]);
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
        layer->bufferCount = 2;
    }

    //if(context->composer_mode == HWC_NODRAW_GPU_VOP)
    //hwc_sync(list);

    return 0;
}
static int hwc_prepare_external(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list) 
{
    return 0;
}

int hwc_prepare_virtual(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list) 
{
    HWC_UNREFERENCED_PARAMETER(dev);

    if (list == NULL)
    {
        return 0;
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
                ret = hwc_prepare_virtual(dev, list);
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
            value[0] = 1e9 / context->fb_fps;
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
    for(i = 0;i<4;i++) // compare 4 lins
    {
        for(j= 0;j<w;j++)  
        {
            if(*da != 0xff000000 && *da != 0x0)
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
    for(i = 0;i<4;i++) // compare 4 lins
    {
        data = da + i;
        for(j= 0;j<h;j++)  
        {
            if(*data != 0xff000000 && *data != 0x0 )
            {
                ALOGV("vers [%d,%d]=%x",i,j,*da);

                return 1;
            }    
            data +=w;    
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
   
    return 0; 
    
}

int hwc_vop_config(hwcContext * context,hwc_display_contents_1_t *list)
{
    int scale_cnt = 0;
    int start = 0,end = 0;
    int i = 0,step = 1;
    int j;
    int winIndex = 0;
    bool fb_flag = false;
    cmpType mode = context->composer_mode;
    int dump = 0;
    struct timeval tpend1, tpend2;
    long usec1 = 0;
    int bk_index = 0;
    struct rk_fb_win_cfg_data fb_info;
    struct fb_var_screeninfo info;
    if ((!context->dpyAttr[0].connected) 
            || (context->dpyAttr[0].fd <= 0))
    {
        return -1;
    }
    
    ALOGV("hwc_vop_config mode=%s",compositionModeName[mode]);    
    info = context->info;    
    memset(&fb_info, 0, sizeof(fb_info));
    switch (mode)
    {
        case HWC_VOP:
            start = 0;
            end = list->numHwLayers - 1;        
            break;
        case HWC_RGA:
        case HWC_RGA_TRSM_VOP:   
        case HWC_RGA_TRSM_GPU_VOP:   
        case HWC_NODRAW_GPU_VOP:
        case HWC_RGA_GPU_VOP:
            if(mode == HWC_RGA)
            {
                start = 0;
                end = 0; 
                winIndex = 0;
            }
            else if(mode == HWC_RGA_TRSM_VOP)
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
                    start = 1;
                    end = list->numHwLayers - 1;   
                    winIndex = 1;
                }    
               
            }
            else if(mode == HWC_RGA_TRSM_GPU_VOP
                    || mode == HWC_NODRAW_GPU_VOP 
                    || mode == HWC_RGA_GPU_VOP)
            {
                start = list->numHwLayers - 1;
                end = list->numHwLayers;   
                winIndex = 1;
            }
            
            fb_info.win_par[0].area_par[0].data_format = context->fbhandle.format;
            fb_info.win_par[0].win_id = 0;
            fb_info.win_par[0].z_order = 0;
            if(mode == HWC_NODRAW_GPU_VOP)
            {
                fb_info.win_par[0].area_par[0].ion_fd = context->membk_fds[context->NoDrMger.membk_index_pre];                        
            }
            else
            {
                fb_info.win_par[0].area_par[0].ion_fd = context->membk_fds[context->membk_index];            
            }
            fb_info.win_par[0].area_par[0].acq_fence_fd = -1;
            fb_info.win_par[0].area_par[0].x_offset = 0;//info.xoffset;
            fb_info.win_par[0].area_par[0].y_offset = 0;//info.yoffset;
            fb_info.win_par[0].area_par[0].xpos = (info.nonstd >> 8) & 0xfff;
            fb_info.win_par[0].area_par[0].ypos = (info.nonstd >> 20) & 0xfff;
            fb_info.win_par[0].area_par[0].xsize = (info.grayscale >> 8) & 0xfff;
            fb_info.win_par[0].area_par[0].ysize = (info.grayscale >> 20) & 0xfff;
            fb_info.win_par[0].area_par[0].xact = info.xres;
            fb_info.win_par[0].area_par[0].yact = info.yres;
            fb_info.win_par[0].area_par[0].xvir = info.xres_virtual;
            fb_info.win_par[0].area_par[0].yvir = info.yres_virtual;
            fb_info.wait_fs = 0;    
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
        case HWC_CP_FB:
            break;
        case HWC_GPU:
            start = list->numHwLayers - 1;
            end = list->numHwLayers;   
            winIndex = 0;        
            break;
        default:
            // unsupported query
            ALOGE("no policy set !!!");
            return -EINVAL;
    }
    ALOGV("[%d->%d],win_index=%d,step=%d",start,end,winIndex,step);
    for (i = start; i < end;)
    {
        hwc_layer_1_t * layer = &list->hwLayers[i];
        float hfactor = 1.0;
        float vfactor = 1.0;
        hwc_region_t * Region = &layer->visibleRegionScreen;
        hwc_rect_t * SrcRect = &layer->sourceCrop;
        hwc_rect_t * DstRect = &layer->displayFrame;
        hwc_rect_t const * rects = Region->rects;
        hwc_rect_t  rect_merge;
        int left_min =0; 
        int top_min =0;
        int right_max=0 ;
        int bottom_max=0;
        hwcRECT dstRects;
        hwcRECT srcRects;

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
        for (int r = 0; r < (unsigned int) Region->numRects ; r++)
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

        if( i == (list->numHwLayers -1))
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
        fb_info.win_par[winIndex].z_order = winIndex;
        fb_info.win_par[winIndex].area_par[0].ion_fd = handle->share_fd;
        fb_info.win_par[winIndex].area_par[0].data_format = handle->format;
        if(fb_info.win_par[winIndex].area_par[0].data_format == HAL_PIXEL_FORMAT_YCrCb_NV12)
            fb_info.win_par[winIndex].area_par[0].data_format = 0x20;
        fb_info.win_par[winIndex].area_par[0].acq_fence_fd = layer->acquireFenceFd;
        fb_info.win_par[winIndex].area_par[0].x_offset =  hwcMAX(srcRects.left, 0);
        if( i == (list->numHwLayers -1))
        {
            fb_info.win_par[winIndex].area_par[0].y_offset = handle->offset / context->fbStride;    
            fb_info.win_par[winIndex].area_par[0].yvir = handle->height*NUM_FB_BUFFERS;
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
        winIndex ++;
        i += step;
        
    }
    //fb_info.wait_fs = 1;
    //gettimeofday(&tpend1, NULL);

    #if 1 // detect UI invalid ,so close win1 ,reduce  bandwidth.
    if(
        fb_info.win_par[0].area_par[0].data_format == 0x20
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
                    ret = DetectValidData((int *)uiHnd->base,uiHnd->width,uiHnd->height);
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

    if(!context->fb_blanked)
    {
        ioctl(context->fbFd, RK_FBIOSET_CONFIG_DONE, &fb_info);
    
    
        ///gettimeofday(&tpend2, NULL);
        //usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
        //LOGD("config use time=%ld ms",  usec1); 

        
        
        //debug info
        for(i = 0;i<4;i++)
        {
            for(j=0;j<4;j++)
            {
                if(fb_info.win_par[i].area_par[j].ion_fd || fb_info.win_par[i].area_par[j].phy_addr)
                {
                   
                    ALOGV("par[%d],area[%d],z_win_galp[%d,%d,%x],[%d,%d,%d,%d]=>[%d,%d,%d,%d],w_h_f[%d,%d,%d],acq_fence_fd=%d,fd=%d,addr=%x",
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

#if 0
        for (int k = 0;k < RK_MAX_BUF_NUM;k++)
        {
            if (fb_info.rel_fence_fd[k] > 0)
            {
                close(fb_info.rel_fence_fd[k]);
            }                 
        }
        if(fb_info.ret_fence_fd > 0)
            close(fb_info.ret_fence_fd);

#else
        switch (mode)
        {
            case HWC_VOP:       
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > 0)
                    {
                        if( k < list->numHwLayers - 1)
                            list->hwLayers[k].releaseFenceFd = fb_info.rel_fence_fd[k];
                        else 
                            close(fb_info.rel_fence_fd[k]);
                    }                 
                }
                if(fb_info.ret_fence_fd > 0)
                    list->retireFenceFd = fb_info.ret_fence_fd;
                break;
            case HWC_RGA:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > 0)
                    {
                        context->membk_fence_fd[bk_index] = fb_info.rel_fence_fd[k];
                    }                 
                }
                if(fb_info.ret_fence_fd > 0)
                    close(fb_info.ret_fence_fd);
                break;
            case HWC_RGA_TRSM_VOP:           
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > 0)
                    {
                        close(fb_info.rel_fence_fd[k]);
                    }                 
                }
                if(fb_info.ret_fence_fd > 0)
                    close(fb_info.ret_fence_fd);
                break;
            case HWC_RGA_TRSM_GPU_VOP:   
            case HWC_NODRAW_GPU_VOP:
            case HWC_RGA_GPU_VOP:
            case HWC_GPU:   
            //case HWC_VOP_GPU:

                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > 0)
                    {
                        if( k < list->numHwLayers && !fb_flag )
                        {
                            fb_flag = true;
                            list->hwLayers[list->numHwLayers - 1].releaseFenceFd = fb_info.rel_fence_fd[k];
                        }    
                        else 
                            close(fb_info.rel_fence_fd[k]);
                    }                 
                }
                if(fb_info.ret_fence_fd > 0)
                    list->retireFenceFd = fb_info.ret_fence_fd;        
                break;
                
            case HWC_VOP_GPU:
                for (int k = 0;k < RK_MAX_BUF_NUM;k++)
                {
                    if (fb_info.rel_fence_fd[k] > 0 )
                    {
                        if( k == 0 )
                        {
                            list->hwLayers[k].releaseFenceFd = fb_info.rel_fence_fd[k];
                            //list->hwLayers[k].releaseFenceFd = -1;
                            //close(fb_info.rel_fence_fd[k]);
                        }                        
                        else if(k == 1)
                        {
                            list->hwLayers[list->numHwLayers - 1].releaseFenceFd = fb_info.rel_fence_fd[k];
                            //list->hwLayers[list->numHwLayers - 1].releaseFenceFd = -1;
                            //close(fb_info.rel_fence_fd[k]);
                        }
                        else 
                            close(fb_info.rel_fence_fd[k]);
                    }           
                }
                if(fb_info.ret_fence_fd > 0)
                {
                    list->retireFenceFd = fb_info.ret_fence_fd;
                    //list->retireFenceFd = -1;
                   // close(fb_info.ret_fence_fd);
                }
                break;
            case HWC_CP_FB:
                break;
            default:
                return -EINVAL;
        }
#endif  
    }
    return 0;
}


int hwc_rga_blit( hwcContext * context ,hwc_display_contents_1_t *list)
{
    hwcSTATUS status = hwcSTATUS_OK;
    unsigned int i;

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

#if hwcUseTime
    gettimeofday(&tpend1, NULL);
#endif

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
            if (context->membk_fence_fd[context->membk_index] > 0)
            {
                sync_wait(context->membk_fence_fd[context->membk_index], 500);
                ALOGV("fenceFd=%d,name=%s", context->membk_fence_fd[context->membk_index],list->hwLayers[i].LayerName);
                close(context->membk_fence_fd[context->membk_index]);
                context->membk_fence_fd[context->membk_index] = -1;
            }
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
                for (int j = 0; j < region->numRects; j++)
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

    for (i = 0; i < list->numHwLayers; i++)
    {
        switch (list->hwLayers[i].compositionType)
        {
            case HWC_BLITTER:
                LOGV("%s(%d):Layer %d ,name=%s,is BLIITER", __FUNCTION__, __LINE__, i,list->hwLayers[i].LayerName);
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
            }
                
    }


    if (bNeedFlush)
    {        
        if (ioctl(context->engine_fd, RGA_FLUSH, NULL) != 0)
        {
            LOGE("%s(%d):RGA_FLUSH Failed!", __FUNCTION__, __LINE__);
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
        case HWC_GPU:        
        case HWC_NODRAW_GPU_VOP:
            hwc_vop_config(context,list);        
            break;
        case HWC_RGA:
        case HWC_RGA_TRSM_VOP:           
        case HWC_RGA_TRSM_GPU_VOP:   
        case HWC_RGA_GPU_VOP:
            hwc_rga_blit(context,list);
            hwc_vop_config(context,list);        
            break;
        case HWC_CP_FB:
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

static int hwc_set_primary(hwc_composer_device_1 *dev, hwc_display_contents_1_t *list) 
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

#if hwcUseTime
    struct timeval tpend1, tpend2;
    long usec1 = 0;
#endif


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
    //hwc_sync(list);      
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
            hwc_sync(list);        
            break;
        case HWC_CP_FB:
            break;
        default:
            return -EINVAL;
    }


    //gettimeofday(&tpend2, NULL);
    //usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    //LOGD("hwc_syncs_set use time=%ld ms",  usec1); 
    
    LOGV("%s(%d):>>> Set start %d layers <<<",
         __FUNCTION__,
         __LINE__,
         list->numHwLayers);

    hwc_policy_set(context,list);
#if hwcUseTime
    gettimeofday(&tpend2, NULL);
    usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
    LOGV("hwcBlit compositer %d layers use time=%ld ms", list->numHwLayers -1, usec1); 
#endif
    hwc_sync_release(list);
    return 0; //? 0 : HWC_EGL_ERROR;
}

static int hwc_set_external(hwc_composer_device_1_t *dev, hwc_display_contents_1_t* list)
{
    return 0;
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
}

static void *hwc_thread(void *data)
{
    hwcContext * context = gcontextAnchor[HWC_DISPLAY_PRIMARY];

    HWC_UNREFERENCED_PARAMETER(data);




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

    context->device.dump = hwc_dump;

    context->membk_index = 0;

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
                    context->membk_type[i] = phandle_gr->type;
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
    context->fun_policy[HWC_RGA_TRSM_VOP] = try_hwc_rga_trfm_vop_policy  ;
    context->fun_policy[HWC_RGA_TRSM_GPU_VOP] = try_hwc_rga_trfm_gpu_vop_policy;
    context->fun_policy[HWC_VOP_GPU] = try_hwc_vop_gpu_policy;
    context->fun_policy[HWC_NODRAW_GPU_VOP] = try_hwc_nodraw_gpu_vop_policy;
    context->fun_policy[HWC_RGA_GPU_VOP] = try_hwc_rga_gpu_vop_policy;
    context->fun_policy[HWC_CP_FB] = try_hwc_cp_fb_policy;
    context->fun_policy[HWC_GPU] = try_hwc_gpu_policy;
    if(context->fbWidth * context->fbHeight >= 1920*1080 )
        context->vop_mbshake = true;
    gcontextAnchor[HWC_DISPLAY_PRIMARY] = context;
    if (context->fbWidth > context->fbHeight)
    {
        property_set("sys.display.oritation", "0");
    }
    else
    {
        property_set("sys.display.oritation", "2");
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

    property_set("sys.ghwc.version", GHWC_VERSION);

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

    }
    else
    {
        LOGE("Open hdmi mode error.");
    }

}

