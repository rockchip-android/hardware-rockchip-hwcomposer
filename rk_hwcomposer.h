/*

* rockchip hwcomposer( 2D graphic acceleration unit) .

*

* Copyright (C) 2015 Rockchip Electronics Co., Ltd.

*/




#ifndef __rk_hwcomposer_h_
#define __rk_hwcomposer_h_

/* Set 0 to enable LOGV message. See cutils/log.h */
#include <cutils/log.h>

#include <hardware/hwcomposer.h>
//#include <ui/android_native_buffer.h>

#include <hardware/rga.h>
#include <utils/Thread.h>
#include <linux/fb.h>
#include <hardware/rk_fh.h>
#include "hwc_ipp.h"
#include "../librkvpu/vpu_global.h"


#define hwcDEBUG                0
#define hwcUseTime              1
#define hwcBlitUseTime          0
#define hwcDumpSurface          0
#define  ENABLE_HWC_WORMHOLE    1
#define  DUMP_SPLIT_AREA        0
#define FB1_IOCTL_SET_YUV_ADDR 0x5002
#define RK_FBIOSET_VSYNC_ENABLE 0x4629
#define RK_FBIOGET_IOMMU_STA        0x4632

//#define USE_LCDC_COMPOSER
#define USE_HW_VSYNC            1
#define FBIOSET_OVERLAY_STATE   0x5018
#define bakupbufsize            4
#define FB_BUFFERS_NUM          (4)
#define RGA_REL_FENCE_NUM       10
#define RGA_ALLOW_MAX_ERR       10
#define EN_VIDEO_UI_MIX         0
#define FENCE_TIME_USE          (1)
#define ONLY_USE_FB_BUFFERS     (0)  //zxl:If close this macro,you need remove hasBlitComposition condition in DisplayDevice::swapBuffers
#define MaxIForVop              (3840)

#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#define GPU_BASE           handle->iBase
#define GPU_WIDTH          handle->iWidth
#define GPU_HEIGHT         handle->iHeight
#define GPU_FORMAT         handle->iFormat
#define GPU_DST_FORMAT     DstHandle->iFormat
#define private_handle_t   IMG_native_handle_t
#else
#define GPU_BASE           handle->base
#define GPU_WIDTH          handle->width
#define GPU_HEIGHT         handle->height
#define GPU_FORMAT         handle->format
#define GPU_DST_FORMAT     DstHandle->format
#endif
#define RGA_POLICY_MAX_SIZE  (5*1024*1024/2 ) //(2*1024*1024)//
#define VIDEO_UI            (1)
#define VIDEO_FULLSCREEN    (2)
#define GPUDRAWCNT          (20)

#ifdef USE_X86
#define VOP_WIN_NUM       2
#define RGA_USE_FENCE     1
#define VIDEO_USE_PPROT   1
#define HOTPLUG_MODE      0
#define ONLY_USE_ONE_VOP  1
#define VIDEO_WIN1_UI_DISABLE     1
#define VIRTUAL_UI_RESOLUTION     0
#elif defined(TARGET_BOARD_PLATFORM_RK312X)
#define VOP_WIN_NUM       2
#define RGA_USE_FENCE     0
#define VIDEO_USE_PPROT   0
#ifdef RK312X_BOX
    #define HOTPLUG_MODE      0
#else
    #define HOTPLUG_MODE      1
#endif
#define ONLY_USE_ONE_VOP  1
#define FORCE_REFRESH     1
#define VIDEO_WIN1_UI_DISABLE     1
#define VIRTUAL_UI_RESOLUTION     0
#elif defined(TARGET_BOARD_PLATFORM_RK322X)
#define VOP_WIN_NUM       2
#define RGA_USE_FENCE     0
#define VIDEO_USE_PPROT   0
#define HOTPLUG_MODE      1
#define ONLY_USE_ONE_VOP  1
#define FORCE_REFRESH     1
#define VIDEO_WIN1_UI_DISABLE     0
#define VIRTUAL_UI_RESOLUTION     1
#elif defined(TARGET_BOARD_PLATFORM_RK3188)
#define VOP_WIN_NUM       2
#define RGA_USE_FENCE     1
#define VIDEO_USE_PPROT   0
#define HOTPLUG_MODE      1
#define ONLY_USE_ONE_VOP  0
#define FORCE_REFRESH     1
#define VIDEO_WIN1_UI_DISABLE     1
#define VIRTUAL_UI_RESOLUTION     0
#elif defined(TARGET_BOARD_PLATFORM_RK3036)
#define VOP_WIN_NUM       2
#define RGA_USE_FENCE     0
#define VIDEO_USE_PPROT   0
#define HOTPLUG_MODE      1
#define ONLY_USE_ONE_VOP  1
#define VIDEO_WIN1_UI_DISABLE     1
#define VIRTUAL_UI_RESOLUTION     0
#elif defined(TARGET_BOARD_PLATFORM_RK3328)
#define VOP_WIN_NUM       3
#define RGA_USE_FENCE     0
#define VIDEO_USE_PPROT   0
#define HOTPLUG_MODE      0
#define ONLY_USE_ONE_VOP  1
#define FORCE_REFRESH     1
#define VIDEO_WIN1_UI_DISABLE     0
#define VIRTUAL_UI_RESOLUTION     0
#endif

#define IQIY_SPECIAL_PROCESS 0
#define rkmALIGN(n, align) \
( \
    ((n) + ((align) - 1)) & ~((align) - 1) \
)

#define GHWC_VERSION  "2.79"

//HWC version Tag
//Get commit info:  git log --format="Author: %an%nTime:%cd%nCommit:%h%n%n%s%n%n"
//Get version: busybox strings /system/lib/hw/hwcomposer.rk30board.so | busybox grep HWC_VERSION
//HWC_VERSION Author:zxl Time:Tue Aug 12 17:27:36 2014 +0800 Version:1.17 Branch&Previous-Commit:rk/rk312x/mid/4.4_r1/develop-9533348.
/*#define HWC_VERSION "HWC_VERSION  \
Author:huangds \
Previous-Time: Mon Jan 19 11:34:09 2015 +0800 \
Version:2.54 \
Branch&Previous-Commit:rk/rk312x/mid/4.4_r1/develop-a45e577."*/

/* Set it to 1 to enable swap rectangle optimization;
 * Set it to 0 to disable. */
/* Set it to 1 to enable pmem cache flush.
 * For linux kernel 3.0 later, you may not be able to flush PMEM cache in a
 * different process (surfaceflinger). Please add PMEM cache flush in gralloc
 * 'unlock' function, which will be called in the same process SW buffers are
 * written/read by software (Skia) */

#ifdef __cplusplus
extern "C"
{
#endif



    #define  hwc_layer_list_t   hwc_display_contents_1_t
    enum
    {
        /* NOTE: These enums are unknown to Android.
         * Android only checks against HWC_FRAMEBUFFER.
         * This layer is to be drawn into the framebuffer by hwc blitter */
        //HWC_TOWIN0 = 0x10,
        //HWC_TOWIN1,
        HWC_BLITTER = 100,
        HWC_DIM,
        HWC_CLEAR_HOLE

    };
    typedef enum _cmpType
    {
        HWC_VOP = 0,
        HWC_RGA,   
        HWC_VOP_RGA,
        HWC_RGA_VOP,
        HWC_RGA_TRSM_VOP,
        HWC_RGA_TRSM_GPU_VOP,
        HWC_VOP_GPU,
        HWC_NODRAW_GPU_VOP,
        HWC_GPU_NODRAW_VOP,
        HWC_RGA_GPU_VOP,
        HWC_GPU_VOP,
        HWC_CP_FB,
        HWC_GPU,
        HWC_POLICY_NUM
    }cmpType;


    typedef enum _hwcSTATUS
    {
        hwcSTATUS_OK     =   0,
        hwcSTATUS_INVALID_ARGUMENT      =   -1,
        hwcSTATUS_IO_ERR                =   -2,
        hwcRGA_OPEN_ERR                 =   -3,
        hwcTHREAD_ERR                   =   -4,
        hwcMutex_ERR                    =   -5,
        hwcPamet_ER                   =   -6,

    }
    hwcSTATUS;

    typedef struct _hwcRECT
    {
        int                    left;
        int                    top;
        int                    right;
        int                    bottom;
    }
    hwcRECT;


#define MaxMixUICnt 2
typedef struct _NodrawManager
{
    cmpType composer_mode_pre;
    int membk_index_pre;
    int uicnt;
    int addr[MaxMixUICnt];
    int alpha[MaxMixUICnt];
}
NoDrawManager;

typedef struct _FenceMangrRga
{
    bool is_last;
    int  rel_fd;  
    bool use_fence;
}
FenceMangrRga;

#define IN
#define OUT

    /* Area struct. */
    struct hwcArea
    {
        /* Area potisition. */
        hwcRECT                          rect;

        /* Bit field, layers who own this Area. */
        int                        owners;

        /* Point to next area. */
        struct hwcArea *                 next;
    };


    /* Area pool struct. */
    struct hwcAreaPool
    {
        /* Pre-allocated areas. */
        hwcArea *                        areas;

        /* Point to free area. */
        hwcArea *                        freeNodes;

        /* Next area pool. */
        hwcAreaPool *                    next;
    };

    struct DisplayAttributes
    {
        uint32_t vsync_period; //nanos
        uint32_t xres;
        uint32_t yres;
        uint32_t relxres;
        uint32_t relyres;
        uint32_t stride;
        float xdpi;
        float ydpi;
        int fd;
        int fd1;
        int fd2;
        int fd3;
        bool connected; //Applies only to pluggable disp.
        //Connected does not mean it ready to use.
        //It should be active also. (UNBLANKED)
        bool isActive;
        // In pause state, composition is bypassed
        // used for WFD displays only
        bool isPause;
    };

    typedef struct tVPU_FRAME_v2
    {
        uint32_t          FrameBusAddr[2];    // 0: Y address; 1: UV address;
        uint32_t         FrameWidth;         // 16 aligned frame width
        uint32_t         FrameHeight;        // 16 aligned frame height
    }tVPU_FRAME_v2_t;

    typedef struct
    {
        tVPU_FRAME vpu_frame;
        void*      vpu_handle;
    } vpu_frame_t;

    typedef struct
    {
        //ion_buffer_t *pion;
        //ion_device_t *ion_device;
        unsigned int  offset;
        unsigned int  last_offset;
    } hwc_ion_t;

    typedef struct
    {
        uint32_t xres;
        uint32_t yres;
        bool HtgOn;
        bool CvbsOn;
        bool HdmiOn;
    } htg_info_t;

    typedef struct _threadPamaters
    {
        int count;
        pthread_mutex_t mlk;
        pthread_mutex_t mtx;
        pthread_cond_t cond;
    }threadPamaters;

    typedef struct _lastStatus
    {
        int numLayers;
        int ovleryLayer;
        int fd[GPUDRAWCNT];
        int alpha[GPUDRAWCNT];
        hwc_rect_t drect[GPUDRAWCNT];
    }lastStatus;

    typedef struct _hwcContext
    {
        hwc_composer_device_1_t device;

        /* Reference count. Normally: 1. */
        unsigned int reference;


        /* Raster engine */
        int     engine_fd;
        int     engine_err_cnt;
        /* Feature: 2D PE 2.0. */
        /* Base address. */
        unsigned int baseAddress;
        int (*fun_policy[HWC_POLICY_NUM])(void * ,hwc_display_contents_1_t*);
        /* Framebuffer stuff. */
        int       fbFd;
        int       fbFd1;
        int       vsync_fd;
        int       fbWidth;
        int       fbHeight;
        bool      fb1_cflag;
        bool      mScreenChanged;
        char      cupcore_string[16];
        DisplayAttributes              dpyAttr[HWC_NUM_DISPLAY_TYPES];
        struct                         fb_var_screeninfo info;
        hwc_procs_t *procs;
        ipp_device_t *ippDev;
        pthread_t hdmi_thread;
        pthread_mutex_t lock;
        uint64_t        mTimeStamp;
        nsecs_t         mNextFakeVSync;
        float           fb_fps;
        NoDrawManager   NoDrMger;
        cmpType     composer_mode;
        cmpType     last_composer_mode;
        unsigned int fbPhysical;
        unsigned int fbStride;
        int          fb_disp_ofset;
        int          wfdOptimize;
        int          wfddev;
        int         win_swap;  
        /* PMEM stuff. */
        unsigned int pmemPhysical;
        unsigned int pmemLength;
        vpu_frame_t  video_frame[2];
        unsigned int fbSize;
        unsigned int lcdSize;
        char *pbakupbuf[bakupbufsize];
        /*hotplug info*/
        htg_info_t mHtg;
        bool       mIsVirUiResolution;
        bool       mIsBootanimExit;
        bool       mIsFirstCallbackToHotplug;
#if FORCE_REFRESH
        threadPamaters mRefresh;
#endif
#if ENABLE_HWC_WORMHOLE
        /* Splited composition area queue. */
        hwcArea *                        compositionArea;

        /* Pre-allocated area pool. */
        hwcAreaPool                      areaMem;
#endif
        int     flag;
        int     fb_blanked;
        bool    IsRk3188;
        bool    IsRk312x;
        bool    IsRk3126;
        bool    IsRk3128;
        bool    IsRk322x;
        bool    IsRk3036;
        bool	IsRk3328;
        bool    IsRkBox;
        bool    isBox;
        bool    isVr;
        bool    isMid;
        bool    isPhone;
        bool    isDongle;
        int     IsInput;
        int     mFbFd;
        void    *mFbBase;
        int     vui_fd;
        int     vui_hide;
        int     videoCnt;
        int     bootCount;
        int     scaleFd;
        bool     vop_mbshake;
        bool     Is_video;
        bool     Is_Lvideo;        
        bool     Is_bypp;
        bool     Is_Secure;
        bool     Is_noi;
        bool    special_app;
        int      isStereo;
        int      Is_debug;
    	int           iommuEn;
    	bool     Is_OverscanEn;
        alloc_device_t  *mAllocDev;
        int     *video_ui;
        int rga_fence_relfd[RGA_REL_FENCE_NUM];
        int membk_fds[FB_BUFFERS_NUM];
        int membk_type[FB_BUFFERS_NUM];
        int membk_fence_acqfd[FB_BUFFERS_NUM];	  // RGA do ,output fence	
        int membk_fence_fd[FB_BUFFERS_NUM];		
        void *membk_base[FB_BUFFERS_NUM];
        buffer_handle_t phd_bk[FB_BUFFERS_NUM];		
        int membk_index;
        void *phy_addr;
        struct private_handle_t fbhandle ;
        lastStatus mLastStatus;
        int hdrStatus;
        int hdrFrameStatus;
        int hdrSupportType;
        int deviceConected;
	bool hasPlaneAlpha;
    }
    hwcContext;

#define hwcMIN(x, y)   (((x) <= (y)) ?  (x) :  (y))
#define hwcMAX(x, y)   (((x) >= (y)) ?  (x) :  (y))

#define hwcIS_ERROR(status)   (status < 0)


#define _hwcONERROR(prefix, func) \
    do \
    { \
        status = func; \
        if (hwcIS_ERROR(status)) \
        { \
            LOGD( "ONERROR: status=%d @ %s(%d) in ", \
                status, __FUNCTION__, __LINE__); \
            goto OnError; \
        } \
    } \
    while (false)
#define hwcONERROR(func)            _hwcONERROR(hwc, func)

#ifdef  ALOGD
#define LOGV        ALOGV
#define LOGE        ALOGE
#define LOGD        ALOGD
#define LOGI        ALOGI
#endif
    /******************************************************************************\
     ********************************* Blitters ***********************************
    \******************************************************************************/

    /* 2D blit. */
    hwcSTATUS
    hwcBlit(
        IN hwcContext * Context,
        IN hwc_layer_1_t * Src,
        IN struct private_handle_t * DstHandle,
        IN hwc_rect_t * SrcRect,
        IN hwc_rect_t * DstRect,
        IN hwc_region_t * Region,
        IN FenceMangrRga *FceMrga,
        IN int index
    );


    hwcSTATUS
    hwcDim(
        IN hwcContext * Context,
        IN hwc_layer_1_t * Src,
        IN struct private_handle_t * DstHandle,
        IN hwc_rect_t * DstRect,
        IN hwc_region_t * Region
    );

    hwcSTATUS
    hwcClear(
        IN hwcContext * Context,
        IN unsigned int Color,
        IN hwc_layer_1_t * Src,
        IN struct private_handle_t * DstHandle,
        IN hwc_rect_t * DstRect,
        IN hwc_region_t * Region
    );

    int hwc_get_int_property (const char* pcProperty, const char* default_value);

    /******************************************************************************\
     ************************** Native buffer handling ****************************
    \******************************************************************************/

    hwcSTATUS
    hwcGetBufFormat(
        IN  struct private_handle_t * Handle,
        OUT RgaSURF_FORMAT * Format
    );

    hwcSTATUS
    hwcGetBufferInfo(
        IN  hwcContext *  Context,
        IN  struct private_handle_t * Handle,
        OUT void * *  Logical,
        OUT unsigned int* Physical,
        OUT unsigned int* Width,
        OUT unsigned int* Height,
        OUT unsigned int* Stride,
        OUT void * *  Info
    );


    hwcSTATUS
    hwcUnlockBuffer(
        IN hwcContext * Context,
        IN struct private_handle_t * Handle,
        IN void * Logical,
        IN void * Info,
        IN unsigned int  Physical
    );

    int
    _HasAlpha(RgaSURF_FORMAT Format);

    int closeFb(int fd);
    int  getHdmiMode();
    void  init_hdmi_mode();
    /******************************************************************************\
     ****************************** Rectangle split *******************************
    \******************************************************************************/
    /* Split rectangles. */
    bool
    hwcSplit(
        IN  hwcRECT * Source,
        IN  hwcRECT * Dest,
        OUT hwcRECT * Intersection,
        OUT hwcRECT * Rects,
        OUT int  * Count
    );

    hwcSTATUS
    WormHole(
        IN hwcContext * Context,
        IN hwcRECT * Rect
    );

    void
    ZoneFree(
        IN hwcContext * Context,
        IN hwcArea* Head
    );

    void
    DivArea(
        IN hwcContext * Context,
        IN hwcArea * Area,
        IN hwcRECT * Rect,
        IN int Owner
    );

    hwcArea *
    zone_alloc(
        IN hwcContext * Context,
        IN hwcArea * Slibing,
        IN hwcRECT * Rect,
        IN int Owner
    );



    extern "C" int clock_nanosleep(clockid_t clock_id, int flags,
                                       const struct timespec *request,
                                       struct timespec *remain);

#ifdef __cplusplus
}
#endif

#endif 

