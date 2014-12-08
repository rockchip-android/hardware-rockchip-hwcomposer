/****************************************************************
* FILE: hwc_rga.h
* FUNCTION:
*
*  The graphics buffer is to be transformed or scaled by the rga device.
*  It is a public rga api (scale or transform).
* AUTHOR:  qiuen     2014/1/6
 ***************************************************************/
#ifndef __rk_hwc_rga
#define __rk_hwc_rga
#include <hardware/hwcomposer.h>
#include <hardware/hardware.h>
#include <hardware/rga.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <cutils/log.h>
#include "rga_angle.h"
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#include  <hardware/hal_public.h>
#else
#include "gralloc_priv.h"
#endif

#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#define SRC_HANDLE_BASE    src_handle->iBase
#define SRC_HANDLE_WIDTH   src_handle->iWidth
#define SRC_HANDLE_HEIGHT  src_handle->iHeight
#define SRC_HANDLE_FORMAT  src_handle->iFormat

#define DST_HANDLE_BASE    dst_handle->iBase
#define DST_HANDLE_WIDTH   dst_handle->iWidth
#define DST_HANDLE_HEIGHT  dst_handle->iHeight
#define DST_HANDLE_FORMAT  dst_handle->iFormat
#define private_handle_t IMG_native_handle_t
#else
#define SRC_HANDLE_BASE    src_handle->base
#define SRC_HANDLE_WIDTH   src_handle->width
#define SRC_HANDLE_HEIGHT  src_handle->height
#define SRC_HANDLE_FORMAT  src_handle->format

#define DST_HANDLE_BASE    dst_handle->base
#define DST_HANDLE_WIDTH   dst_handle->width
#define DST_HANDLE_HEIGHT  dst_handle->height
#define DST_HANDLE_FORMAT  dst_handle->format

#endif

//#define  ENABLE_WFD_OPTIMIZE 1
#define  DEBUG_LOG  1

enum
{
    PROPORTIONS,
    NOT_TO_SCALE
};

typedef struct hwc_cfg
{
    int transform;
    int src_format;
    int dst_format;
    hwc_rect_t src_rect;
    hwc_rect_t dst_rect;
    unsigned int rga_fbFd;
    struct private_handle_t *src_handle;
    struct private_handle_t *dst_handle;
} hwc_cfg_t;

int init_rga_cfg(int rga_fd);

int set_rga_cfg(hwc_cfg_t  *hwc_cfg);

int do_rga_transform_and_scale();



#endif

