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




#include "rk_hwcomposer.h"
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#include <hardware/hal_public.h>
#else
#include "gralloc_priv.h"
#endif
#include <linux/fb.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
//#include <linux/android_pmem.h>
#include <ui/PixelFormat.h>
#include <fcntl.h>




#ifndef PMEM_CACHE_FLUSH
#define PMEM_CACHE_FLUSH _IOW(PMEM_IOCTL_MAGIC, 8, unsigned int)
#endif




hwcSTATUS
hwcLockBuffer(
    IN  hwcContext  * Context,
    IN  struct private_handle_t * Handle,
    OUT void * * Logical,
    OUT unsigned int  * Physical,
    OUT unsigned int  * Width,
    OUT unsigned int  * Height,
    OUT unsigned int  * Stride,
    OUT void * * Info
)
{
    hwcSTATUS status = hwcSTATUS_OK;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    struct private_handle_t * handle = Handle;
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
    stride = Handle->stride;
#else
    stride = gcmALIGN(GPU_WIDTH, 32);
#endif
    width  = GPU_WIDTH ;
    height = GPU_HEIGHT;

    //LOGD(" Handle->width=%d,width=%d",Handle->width,width);


    //LOGD("hwcLockBuffer width=%d,Handle->format=%x,bytesPerPixel=%d,stride=%d",width,Handle->format,android::bytesPerPixel(Handle->format),stride);
    {
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
        if (Handle->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)
#else
        if (Handle->usage & GRALLOC_USAGE_HW_FB)
#endif
        {
            /* Framebuffer. */
            if (Context->fbFd == 0)
            {
                struct fb_fix_screeninfo fixInfo;
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                int rel = ioctl(Handle->fd, FBIOGET_FSCREENINFO, &fixInfo);

#else
                int iFbFd;
                iFbFd = open("/dev/graphics/fb0", O_RDWR, 0);

                if (!iFbFd)
                {
                    LOGE("open(dev/graphics/fb0) failed in %s", __func__);
                    return hwcSTATUS_IO_ERR;
                }
                int rel = ioctl(iFbFd, FBIOGET_FSCREENINFO, &fixInfo);
#endif


                if (rel != 0)
                {
                    LOGE("ioctl(fd, FBIOGET_FSCREENINFO) failed"
                         " in %s, fd=%d", __func__, Handle->fd);

                    return hwcSTATUS_IO_ERR;
                }
#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                Context->fbFd       = Handle->fd;
#else
                Context->fbFd       = iFbFd;
#endif



                Context->fbPhysical = fixInfo.smem_start;
                Context->fbStride   = fixInfo.line_length;
            }

#ifndef TARGET_BOARD_PLATFORM_RK30XXB
            *Logical       = (void *) GPU_BASE;
#else
            *Logical       = (void *) GPU_BASE;
#endif

#ifndef USE_LCDC_COMPOSER
#ifndef ONLY_USE_FB_BUFFERS
            *Physical      = Context->membk_fds[Context->membk_index];
#else
            *Physical      = Context->mFbFd;
#endif
#else
            *Physical      = Context->mFbFd;/*(unsigned int) (Context->fbPhysical + Handle->offset)
                                     - Context->baseAddress;*/
#endif
            *Width         = Context->fbWidth;// // gcmALIGN(Context->fbWidth, 32);
            *Height        =  Context->fbHeight;
            *Stride        = Context->fbStride;
            ALOGV("hwcLockBuffer:phy_addr=%p,w=%d,h=%d,stride=%d", *Physical, *Width, *Height, *Stride);
            *Info          = NULL;
        }
        else //if (Handle->flags & private_handle_t::PRIV_FLAGS_USES_UMP)
        {
            /* PMEM type. */
#if 0
            struct pmem_region region;

            //if (Context->pmemPhysical == ~0U)
            {
                /* ASSUME: PMEM physical address is constant.
                 * PMEM physical address must be constant for PMEM pool,
                 * unless multiple PMEM devices are used. */
                if (ioctl(Handle->fd, PMEM_GET_PHYS, &region) != 0)
                {
                    LOGE("Get PMEM physical address failed: fd=%d", Handle->fd);
                    return hwcSTATUS_IO_ERR;
                }

                Context->pmemPhysical = region.offset;
                Context->pmemLength   = region.len;
            }
#endif

            /* Try lock handle once. */


#ifndef TARGET_BOARD_PLATFORM_RK30XXB
            *Logical       = (void *) GPU_BASE;
#else
            const gralloc_module_t * module;
            void * vaddr = NULL;

            if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                              (const hw_module_t **) &module) != 0)
            {
                return hwcSTATUS_IO_ERR;
            }

            module->lock(module,
                         (buffer_handle_t)Handle,
                         GRALLOC_USAGE_SW_READ_OFTEN,
                         0, 0, width, height,
                         &vaddr);

            module->unlock(module, (buffer_handle_t)Handle);
            *Logical       = (void *) GPU_BASE;
#endif
#ifdef USE_LCDC_COMPOSER
            *Physical      =  Handle->phy_addr;//Context->fbPhysical;//Handle->phy_addr; ; // debug
#else
            *Physical      = NULL;
#endif

            *Width         = width;
            *Height        = height;
            *Stride        = stride;
            *Info          = NULL;

            /* Flush cache. */

            if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
            {

#ifndef TARGET_BOARD_PLATFORM_RK30XXB
                tVPU_FRAME *pFrame = (tVPU_FRAME *)Handle->base;
                stride = 1 * (gcmALIGN(width, 16));//((width + 15) & ~15);
#else
                tVPU_FRAME *pFrame = (tVPU_FRAME *)Handle->iBase;
#if 0
                stride = gcmALIGN(pFrame->FrameWidth, 16)  ;
#else
                stride = pFrame->FrameWidth;
#endif
#endif
#if 0
                *Height = gcmALIGN(pFrame->FrameHeight, 16);
                *Width = gcmALIGN(pFrame->FrameWidth, 16);
#else
                *Height = pFrame->FrameHeight;
                *Width = pFrame->FrameWidth;
#endif
                *Physical = pFrame->FrameBusAddr[0];
                *Info          = NULL;
                *Stride        = stride;
                ALOGV("hwcLockBuffer:video info phy_addr=%p,w=%d,h=%d", pFrame->FrameBusAddr[0], *Width, *Height);
            }

        }

    }

    return status;
}



/*******************************************************************************
**
**  YUV pixel formats of android hal.
**
**  Different android versions have different definitaions.
**  These are collected from hardware/libhardware/include/hardware/hardware.h
*/


hwcSTATUS
hwcGetFormat(
    IN  struct private_handle_t * Handle,
    OUT RgaSURF_FORMAT * Format

)
{
    struct private_handle_t *handle = Handle;
    if (Format != NULL)
    {

        switch (GPU_FORMAT)
        {
            case HAL_PIXEL_FORMAT_RGB_565:
                *Format = RK_FORMAT_RGB_565;
                break;

            case HAL_PIXEL_FORMAT_RGBA_8888:
                *Format = RK_FORMAT_RGBA_8888;
                break;

            case HAL_PIXEL_FORMAT_RGBX_8888:
                *Format = RK_FORMAT_RGBX_8888;
                break;


            case HAL_PIXEL_FORMAT_BGRA_8888:
                *Format = RK_FORMAT_BGRA_8888;
                break;

            case HAL_PIXEL_FORMAT_YCrCb_NV12:
                /* YUV 420 semi planner: NV12 */
                *Format = RK_FORMAT_YCbCr_420_SP;
                break;
            case HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO:
                *Format = RK_FORMAT_YCbCr_420_SP;
                break;
            default:
                return hwcSTATUS_INVALID_ARGUMENT;
        }
    }


    return hwcSTATUS_OK;
}

#if ENABLE_HWC_WORMHOLE
/*
 * Area spliting feature depends on the following 3 functions:
 * '_AllocateArea', '_FreeArea' and '_SplitArea'.
 */
#define POOL_SIZE 512

hwcArea *
_AllocateArea(
    IN hwcContext * Context,
    IN hwcArea * Slibing,
    IN hwcRECT * Rect,
    IN int Owner
)
{
    hwcArea * area;
    hwcAreaPool * pool  = &Context->areaPool;

    for (;;)
    {
        if (pool->areas == NULL)
        {

            /* No areas allocated, allocate now. */
            pool->areas = (hwcArea *) malloc(sizeof(hwcArea) * POOL_SIZE);

            /* Get area. */
            area = pool->areas;

            /* Update freeNodes. */
            pool->freeNodes = area + 1;

            break;
        }

        else if (pool->freeNodes - pool->areas >= POOL_SIZE)
        {
            /* This pool is full. */
            if (pool->next == NULL)
            {
                /* No more pools, allocate one. */
                pool->next = (hwcAreaPool *) malloc(sizeof(hwcAreaPool));

                /* Point to the new pool. */
                pool = pool->next;

                /* Clear fields. */
                pool->areas     = NULL;
                pool->freeNodes = NULL;
                pool->next      = NULL;
            }

            else
            {

                /* Advance to next pool. */
                pool = pool->next;
            }
        }

        else
        {
            /* Get area and update freeNodes. */
            area = pool->freeNodes++;

            break;
        }
    }

    /* Update area fields. */
    area->rect   = *Rect;
    area->owners = Owner;

    //LOGD("area->rect.left=%d,top=%d,right=%d,bottom=%d,area->owners=%d",area->rect.left,area->rect.top,area->rect.right,area->rect.bottom,area->owners);
    if (Slibing == NULL)
    {
        area->next = NULL;
    }

    else if (Slibing->next == NULL)
    {
        area->next = NULL;
        Slibing->next = area;
    }

    else
    {
        area->next = Slibing->next;
        Slibing->next = area;
    }

    return area;
}


void
_FreeArea(
    IN hwcContext * Context,
    IN hwcArea* Head
)
{
    /* Free the first node is enough. */
    hwcAreaPool * pool  = &Context->areaPool;

    while (pool != NULL)
    {
        if (Head >= pool->areas && Head < pool->areas + POOL_SIZE)
        {
            /* Belongs to this pool. */
            if (Head < pool->freeNodes)
            {
                /* Update freeNodes if the 'Head' is older. */
                pool->freeNodes = Head;

                /* Reset all later pools. */
                while (pool->next != NULL)
                {
                    /* Advance to next pool. */
                    pool = pool->next;

                    /* Reset freeNodes. */
                    pool->freeNodes = pool->areas;
                }
            }

            /* Done. */
            break;
        }

        else if (pool->freeNodes < pool->areas + POOL_SIZE)
        {
            /* Already tagged as freed. */
            break;
        }

        else
        {
            /* Advance to next pool. */
            pool = pool->next;
        }
    }
}


void
_SplitArea(
    IN hwcContext * Context,
    IN hwcArea * Area,
    IN hwcRECT * Rect,
    IN int Owner
)
{
    hwcRECT r0[4];
    hwcRECT r1[4];
    int c0 = 0;
    int c1 = 0;

    hwcRECT * rect;

    for (;;)
    {
        rect = &Area->rect;

        if ((Rect->left   < rect->right)
                && (Rect->top    < rect->bottom)
                && (Rect->right  > rect->left)
                && (Rect->bottom > rect->top)
           )
        {
            /* Overlapped. */
            break;
        }

        if (Area->next == NULL)
        {
            /* This rectangle is not overlapped with any area. */
            _AllocateArea(Context, Area, Rect, Owner);
            return;
        }

        Area = Area->next;
    }

    /* OK, the rectangle is overlapped with 'rect' area. */
    if ((Rect->left <= rect->left)
            && (Rect->right >= rect->right)
       )
    {
        /* |-><-| */
        /* +---+---+---+
         * | X | X | X |
         * +---+---+---+
         * | X | X | X |
         * +---+---+---+
         * | X | X | X |
         * +---+---+---+
         */

        if (Rect->left < rect->left)
        {
            r1[c1].left   = Rect->left;
            r1[c1].top    = Rect->top;
            r1[c1].right  = rect->left;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        if (Rect->top < rect->top)
        {
            r1[c1].left   = rect->left;
            r1[c1].top    = Rect->top;
            r1[c1].right  = rect->right;
            r1[c1].bottom = rect->top;

            c1++;
        }

        else if (rect->top < Rect->top)
        {
            r0[c0].left   = rect->left;
            r0[c0].top    = rect->top;
            r0[c0].right  = rect->right;
            r0[c0].bottom = Rect->top;

            c0++;
        }

        if (Rect->right > rect->right)
        {
            r1[c1].left   = rect->right;
            r1[c1].top    = Rect->top;
            r1[c1].right  = Rect->right;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        if (Rect->bottom > rect->bottom)
        {
            r1[c1].left   = rect->left;
            r1[c1].top    = rect->bottom;
            r1[c1].right  = rect->right;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[c0].left   = rect->left;
            r0[c0].top    = Rect->bottom;
            r0[c0].right  = rect->right;
            r0[c0].bottom = rect->bottom;

            c0++;
        }
    }

    else if (Rect->left <= rect->left)
    {
        /* |-> */
        /* +---+---+---+
         * | X | X |   |
         * +---+---+---+
         * | X | X |   |
         * +---+---+---+
         * | X | X |   |
         * +---+---+---+
         */

        if (Rect->left < rect->left)
        {
            r1[c1].left   = Rect->left;
            r1[c1].top    = Rect->top;
            r1[c1].right  = rect->left;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        if (Rect->top < rect->top)
        {
            r1[c1].left   = rect->left;
            r1[c1].top    = Rect->top;
            r1[c1].right  = Rect->right;
            r1[c1].bottom = rect->top;

            c1++;
        }

        else if (rect->top < Rect->top)
        {
            r0[c0].left   = rect->left;
            r0[c0].top    = rect->top;
            r0[c0].right  = Rect->right;
            r0[c0].bottom = Rect->top;

            c0++;
        }

        /* if (rect->right > Rect->right) */
        {
            r0[c0].left   = Rect->right;
            r0[c0].top    = rect->top;
            r0[c0].right  = rect->right;
            r0[c0].bottom = rect->bottom;

            c0++;
        }

        if (Rect->bottom > rect->bottom)
        {
            r1[c1].left   = rect->left;
            r1[c1].top    = rect->bottom;
            r1[c1].right  = Rect->right;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[c0].left   = rect->left;
            r0[c0].top    = Rect->bottom;
            r0[c0].right  = Rect->right;
            r0[c0].bottom = rect->bottom;

            c0++;
        }
    }

    else if (Rect->right >= rect->right)
    {
        /*    <-| */
        /* +---+---+---+
         * |   | X | X |
         * +---+---+---+
         * |   | X | X |
         * +---+---+---+
         * |   | X | X |
         * +---+---+---+
         */

        /* if (rect->left < Rect->left) */
        {
            r0[c0].left   = rect->left;
            r0[c0].top    = rect->top;
            r0[c0].right  = Rect->left;
            r0[c0].bottom = rect->bottom;

            c0++;
        }

        if (Rect->top < rect->top)
        {
            r1[c1].left   = Rect->left;
            r1[c1].top    = Rect->top;
            r1[c1].right  = rect->right;
            r1[c1].bottom = rect->top;

            c1++;
        }

        else if (rect->top < Rect->top)
        {
            r0[c0].left   = Rect->left;
            r0[c0].top    = rect->top;
            r0[c0].right  = rect->right;
            r0[c0].bottom = Rect->top;

            c0++;
        }

        if (Rect->right > rect->right)
        {
            r1[c1].left   = rect->right;
            r1[c1].top    = Rect->top;
            r1[c1].right  = Rect->right;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        if (Rect->bottom > rect->bottom)
        {
            r1[c1].left   = Rect->left;
            r1[c1].top    = rect->bottom;
            r1[c1].right  = rect->right;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[c0].left   = Rect->left;
            r0[c0].top    = Rect->bottom;
            r0[c0].right  = rect->right;
            r0[c0].bottom = rect->bottom;

            c0++;
        }
    }

    else
    {
        /* | */
        /* +---+---+---+
         * |   | X |   |
         * +---+---+---+
         * |   | X |   |
         * +---+---+---+
         * |   | X |   |
         * +---+---+---+
         */

        /* if (rect->left < Rect->left) */
        {
            r0[c0].left   = rect->left;
            r0[c0].top    = rect->top;
            r0[c0].right  = Rect->left;
            r0[c0].bottom = rect->bottom;

            c0++;
        }

        if (Rect->top < rect->top)
        {
            r1[c1].left   = Rect->left;
            r1[c1].top    = Rect->top;
            r1[c1].right  = Rect->right;
            r1[c1].bottom = rect->top;

            c1++;
        }

        else if (rect->top < Rect->top)
        {
            r0[c0].left   = Rect->left;
            r0[c0].top    = rect->top;
            r0[c0].right  = Rect->right;
            r0[c0].bottom = Rect->top;

            c0++;
        }

        /* if (rect->right > Rect->right) */
        {
            r0[c0].left   = Rect->right;
            r0[c0].top    = rect->top;
            r0[c0].right  = rect->right;
            r0[c0].bottom = rect->bottom;

            c0++;
        }

        if (Rect->bottom > rect->bottom)
        {
            r1[c1].left   = Rect->left;
            r1[c1].top    = rect->bottom;
            r1[c1].right  = Rect->right;
            r1[c1].bottom = Rect->bottom;

            c1++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[c0].left   = Rect->left;
            r0[c0].top    = Rect->bottom;
            r0[c0].right  = Rect->right;
            r0[c0].bottom = rect->bottom;

            c0++;
        }
    }

    if (c1 > 0)
    {
        /* Process rects outside area. */
        if (Area->next == NULL)
        {
            /* Save rects outside area. */
            for (int i = 0; i < c1; i++)
            {
                _AllocateArea(Context, Area, &r1[i], Owner);
            }
        }

        else
        {
            /* Rects outside area. */
            for (int i = 0; i < c1; i++)
            {
                _SplitArea(Context, Area, &r1[i], Owner);
            }
        }
    }

    if (c0 > 0)
    {
        /* Save rects inside area but not overlapped. */
        for (int i = 0; i < c0; i++)
        {
            _AllocateArea(Context, Area, &r0[i], Area->owners);
        }

        /* Update overlapped area. */
        if (rect->left   < Rect->left)
        {
            rect->left   = Rect->left;
        }
        if (rect->top    < Rect->top)
        {
            rect->top    = Rect->top;
        }
        if (rect->right  > Rect->right)
        {
            rect->right  = Rect->right;
        }
        if (rect->bottom > Rect->bottom)
        {
            rect->bottom = Rect->bottom;
        }
    }

    /* The area is owned by the new owner as well. */
    Area->owners |= Owner;
}
#endif

