/*

* rockchip hwcomposer( 2D graphic acceleration unit) .

*

* Copyright (C) 2015 Rockchip Electronics Co., Ltd.

*/





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








hwcSTATUS
hwcGetBufferInfo(
      hwcContext  * Context,
      struct private_handle_t * Handle,
     void * * Logical,
     unsigned int  * Physical,
     unsigned int  * Width,
     unsigned int  * Height,
     unsigned int  * Stride,
     void * * Info
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
    stride = rkmALIGN(GPU_WIDTH,32);
#endif	   
   	width  = GPU_WIDTH ;
    height = GPU_HEIGHT;
    
	//LOGD(" Handle->width=%d,width=%d",Handle->width,width);
    

	//LOGD("hwcGetBufferInfo width=%d,Handle->format=%x,bytesPerPixel=%d,stride=%d",width,Handle->format,android::bytesPerPixel(Handle->format),stride);
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
            *Width         =  rkmALIGN(Context->fbWidth,32);
            *Height        =  Context->fbHeight;
            *Stride        = Context->fbStride;
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
            *Physical      = 0;
            #endif

            *Width         = width;
            *Height        = height;
            *Stride        = stride;
            *Info          = NULL;

            /* Flush cache. */

			if(GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
			{

#ifndef TARGET_BOARD_PLATFORM_RK30XXB    						
				tVPU_FRAME *pFrame = (tVPU_FRAME *)Handle->base;
		    	stride = 1 * (rkmALIGN(width,16));//((width + 15) & ~15);
#else
				tVPU_FRAME *pFrame = (tVPU_FRAME *)Handle->iBase;
				#if 0
				stride = rkmALIGN(pFrame->FrameWidth,16)  ;
				#else
				stride = pFrame->FrameWidth;
				#endif
#endif
                #if 0
                *Height = rkmALIGN(pFrame->FrameHeight,16);
                *Width = rkmALIGN(pFrame->FrameWidth,16);
                #else
                *Height = pFrame->FrameHeight;
                *Width = pFrame->FrameWidth;
                #endif
				*Physical = pFrame->FrameBusAddr[0];
		        *Info          = NULL;
		        *Stride        = stride;
                ALOGV("hwcGetBufferInfo:video info phy_addr=%p,w=%d,h=%d",pFrame->FrameBusAddr[0],*Width,*Height);
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
hwcGetBufFormat(
      struct private_handle_t * Handle,
     RgaSURF_FORMAT * Format
    
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
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:  // NV21
            *Format = RK_FORMAT_YCrCb_420_SP;
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
#define MEM_SIZE 512

hwcArea *
zone_alloc(
     hwcContext * Context,
     hwcArea * Slibing,
     hwcRECT * Rect,
     int Owner
    )
{
    hwcArea * area;
    hwcAreaPool * pool  = &Context->areaMem;

    while(true)
    {
        if (pool->areas == NULL)
        {

            /* No areas allocated, allocate now. */
            pool->areas = (hwcArea *) malloc(sizeof (hwcArea) * MEM_SIZE);

            /* Get area. */
            area = pool->areas;

            /* Update freeNodes. */
            pool->freeNodes = area + 1;

            break;
        }

        else if (pool->freeNodes - pool->areas >= MEM_SIZE)
        {
            /* This pool is full. */
            if (pool->next == NULL)
            {
                pool->next = (hwcAreaPool *) malloc(sizeof (hwcAreaPool));

                pool = pool->next;

                pool->areas     = NULL;
                pool->freeNodes = NULL;
                pool->next      = NULL;
            }

            else
            {
            
                pool = pool->next;
            }
        }

        else
        {
            area = pool->freeNodes++;

            break;
        }
    }

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
ZoneFree(
     hwcContext * Context,
     hwcArea* Head
    )
{
    hwcAreaPool * pool  = &Context->areaMem;

    while (pool != NULL)
    {
        if (Head >= pool->areas && Head < pool->areas + MEM_SIZE)
        {
            if (Head < pool->freeNodes)
            {
                pool->freeNodes = Head;

                while (pool->next != NULL)
                {
                    pool = pool->next;

                    pool->freeNodes = pool->areas;
                }
            }

            break;
        }

        else if (pool->freeNodes < pool->areas + MEM_SIZE)
        {
            break;
        }

        else
        {
            pool = pool->next;
        }
    }
}


void
DivArea(
     hwcContext * Context,
     hwcArea * Area,
     hwcRECT * Rect,
     int Owner
    )
{
    hwcRECT r0[4];
    hwcRECT r1[4];
    int i = 0;
    int j = 0;

    hwcRECT * rect;

    while (true)
    {
        rect = &Area->rect;

        if ((Rect->left   < rect->right)
        &&  (Rect->top    < rect->bottom)
        &&  (Rect->right  > rect->left)
        &&  (Rect->bottom > rect->top)
        )
        {
            /* Overlapped. */
            break;
        }

        if (Area->next == NULL)
        {
            /* This rectangle is not overlapped with any area. */
            zone_alloc(Context, Area, Rect, Owner);
            return;
        }

        Area = Area->next;
    }

    if ((Rect->left <= rect->left)
    &&  (Rect->right >= rect->right)
    )
    {

        if (Rect->left < rect->left)
        {
            r1[j].left   = Rect->left;
            r1[j].top    = Rect->top;
            r1[j].right  = rect->left;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        if (Rect->top < rect->top)
        {
            r1[j].left   = rect->left;
            r1[j].top    = Rect->top;
            r1[j].right  = rect->right;
            r1[j].bottom = rect->top;

            j++;
        }

        else if (rect->top < Rect->top)
        {
            r0[i].left   = rect->left;
            r0[i].top    = rect->top;
            r0[i].right  = rect->right;
            r0[i].bottom = Rect->top;

            i++;
        }

        if (Rect->right > rect->right)
        {
            r1[j].left   = rect->right;
            r1[j].top    = Rect->top;
            r1[j].right  = Rect->right;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        if (Rect->bottom > rect->bottom)
        {
            r1[j].left   = rect->left;
            r1[j].top    = rect->bottom;
            r1[j].right  = rect->right;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[i].left   = rect->left;
            r0[i].top    = Rect->bottom;
            r0[i].right  = rect->right;
            r0[i].bottom = rect->bottom;

            i++;
        }
    }

    else if (Rect->left <= rect->left)
    {

        if (Rect->left < rect->left)
        {
            r1[j].left   = Rect->left;
            r1[j].top    = Rect->top;
            r1[j].right  = rect->left;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        if (Rect->top < rect->top)
        {
            r1[j].left   = rect->left;
            r1[j].top    = Rect->top;
            r1[j].right  = Rect->right;
            r1[j].bottom = rect->top;

            j++;
        }

        else if (rect->top < Rect->top)
        {
            r0[i].left   = rect->left;
            r0[i].top    = rect->top;
            r0[i].right  = Rect->right;
            r0[i].bottom = Rect->top;

            i++;
        }

        r0[i].left   = Rect->right;
        r0[i].top    = rect->top;
        r0[i].right  = rect->right;
        r0[i].bottom = rect->bottom;

        i++;

        if (Rect->bottom > rect->bottom)
        {
            r1[j].left   = rect->left;
            r1[j].top    = rect->bottom;
            r1[j].right  = Rect->right;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[i].left   = rect->left;
            r0[i].top    = Rect->bottom;
            r0[i].right  = Rect->right;
            r0[i].bottom = rect->bottom;

            i++;
        }
    }

    else if (Rect->right >= rect->right)
    {

        r0[i].left   = rect->left;
        r0[i].top    = rect->top;
        r0[i].right  = Rect->left;
        r0[i].bottom = rect->bottom;

        i++;

        if (Rect->top < rect->top)
        {
            r1[j].left   = Rect->left;
            r1[j].top    = Rect->top;
            r1[j].right  = rect->right;
            r1[j].bottom = rect->top;

            j++;
        }

        else if (rect->top < Rect->top)
        {
            r0[i].left   = Rect->left;
            r0[i].top    = rect->top;
            r0[i].right  = rect->right;
            r0[i].bottom = Rect->top;

            i++;
        }

        if (Rect->right > rect->right)
        {
            r1[j].left   = rect->right;
            r1[j].top    = Rect->top;
            r1[j].right  = Rect->right;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        if (Rect->bottom > rect->bottom)
        {
            r1[j].left   = Rect->left;
            r1[j].top    = rect->bottom;
            r1[j].right  = rect->right;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[i].left   = Rect->left;
            r0[i].top    = Rect->bottom;
            r0[i].right  = rect->right;
            r0[i].bottom = rect->bottom;

            i++;
        }
    }

    else
    {

        r0[i].left   = rect->left;
        r0[i].top    = rect->top;
        r0[i].right  = Rect->left;
        r0[i].bottom = rect->bottom;

        i++;

        if (Rect->top < rect->top)
        {
            r1[j].left   = Rect->left;
            r1[j].top    = Rect->top;
            r1[j].right  = Rect->right;
            r1[j].bottom = rect->top;

            j++;
        }

        else if (rect->top < Rect->top)
        {
            r0[i].left   = Rect->left;
            r0[i].top    = rect->top;
            r0[i].right  = Rect->right;
            r0[i].bottom = Rect->top;

            i++;
        }

        r0[i].left   = Rect->right;
        r0[i].top    = rect->top;
        r0[i].right  = rect->right;
        r0[i].bottom = rect->bottom;

        i++;

        if (Rect->bottom > rect->bottom)
        {
            r1[j].left   = Rect->left;
            r1[j].top    = rect->bottom;
            r1[j].right  = Rect->right;
            r1[j].bottom = Rect->bottom;

            j++;
        }

        else if (rect->bottom > Rect->bottom)
        {
            r0[i].left   = Rect->left;
            r0[i].top    = Rect->bottom;
            r0[i].right  = Rect->right;
            r0[i].bottom = rect->bottom;

            i++;
        }
    }

    if (j > 0)
    {
        if (Area->next == NULL)
        {
            for (int k = 0; k < j; k++)
            {
                zone_alloc(Context, Area, &r1[k], Owner);
            }
        }

        else
        {
            for (int k = 0; k < j; k++)
            {
                DivArea(Context, Area, &r1[k], Owner);
            }
        }
    }

    if (i > 0)
    {
        for (int k = 0; k < i; k++)
        {
            zone_alloc(Context, Area, &r0[k], Area->owners);
        }

        if (rect->left   < Rect->left)   { rect->left   = Rect->left;   }
        if (rect->top    < Rect->top)    { rect->top    = Rect->top;    }
        if (rect->right  > Rect->right)  { rect->right  = Rect->right;  }
        if (rect->bottom > Rect->bottom) { rect->bottom = Rect->bottom; }
    }

    Area->owners |= Owner;
}
#endif

