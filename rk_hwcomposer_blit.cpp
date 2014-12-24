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

#include <fcntl.h>
#ifdef TARGET_BOARD_PLATFORM_RK30XXB
#include <hardware/hal_public.h>
#include <linux/fb.h>
#else
#include "gralloc_priv.h"
#endif
#include <ui/PixelFormat.h>

//#include "struct.h"

#undef LOGV
#define LOGV(...)

#undef LOGI
#define LOGI(...)

int
_HasAlpha(RgaSURF_FORMAT Format)
{
    return (Format == RK_FORMAT_RGB_565) ? false
           : (
               (Format == RK_FORMAT_RGBA_8888) ||
               (Format == RK_FORMAT_BGRA_8888)
           );
}
static int _DumpFbInfo(struct fb_var_screeninfo *info, int win)
{
    LOGD("dump win%d: vir[%d,%d] [%d,%d,%d,%d] => [%d,%d,%d,%d]", win,
         info->xres_virtual, info->yres_virtual,
         info->xoffset,
         info->yoffset,
         info->xoffset + info->xres,
         info->yoffset + info->yres,
         (info->nonstd >> 8)&0xfff,
         (info->nonstd >> 20)&0xfff,
         ((info->grayscale >> 8)&0xfff) + ((info->nonstd >> 8)&0xfff),
         ((info->grayscale >> 20)&0xfff) + ((info->nonstd >> 20)&0xfff));

    return 0;
}
hwcSTATUS
_ComputeUVOffset(
    IN  RgaSURF_FORMAT Format,
    IN  unsigned int Logical ,
    IN  unsigned int Height,
    IN  unsigned int Stride,
    OUT unsigned int * ULogical,
    OUT unsigned int *  UStride,
    OUT unsigned int *  VLogical,
    OUT unsigned int *  VStride
)
{
    unsigned int uLogical;
    unsigned int vLogical;
    unsigned int uStride;
    unsigned int vStride;

    switch (Format)
    {
        case RK_FORMAT_YCbCr_420_SP:
            uStride   = vStride   = Stride;
            uLogical = vLogical = Logical + Stride * Height; //((Height + 15) & ~15) ;
            break;

        default:
            return hwcSTATUS_INVALID_ARGUMENT;
    }

    /* Return results. */
    if (ULogical != NULL)
    {
        *ULogical = uLogical;
    }

    if (VLogical != NULL)
    {
        *VLogical = vLogical;
    }

    if (UStride != NULL)
    {
        *UStride = uStride;
    }

    if (VStride != NULL)
    {
        *VStride = vStride;
    }

    return hwcSTATUS_OK;
}

#include <cutils/properties.h>


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

//static int blitcount = 0;
hwcSTATUS
hwcBlit(
    IN hwcContext * Context,
    IN hwc_layer_1_t * Src,
    IN struct private_handle_t * DstHandle,
    IN hwc_rect_t * SrcRect,
    IN hwc_rect_t * DstRect,
    IN hwc_region_t * Region
)
{
    hwcSTATUS status = hwcSTATUS_OK;

    hwcRECT             srcRect;
    hwcRECT             dstRect;

    void *              srcLogical  = NULL;
    unsigned int        srcPhysical = ~0;
    void *              srcInfo     = NULL;
    unsigned int        srcStride;
    unsigned int        srcWidth;
    unsigned int        srcHeight;
    RgaSURF_FORMAT      srcFormat;

    void *              dstLogical  = NULL;
    unsigned int        dstPhysical = ~0;
    int                 dstFd, dstBase;
    void *              dstInfo     = NULL;
    unsigned int        dstStride;
    unsigned int        dstWidth;
    unsigned int        dstHeight;
    RgaSURF_FORMAT      dstFormat;

    int                 perpixelAlpha;
    unsigned char       planeAlpha;

    struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;

    unsigned int n = 0;
    int Rotation = 0;

    float hfactor;
    float vfactor;
    int  stretch;
    int  yuvFormat;
    struct rga_req  Rga_Request;
    RECT clip;
    unsigned char RotateMode = 0;
    unsigned int      Xoffset;
    unsigned int      Yoffset;
    unsigned int      WidthAct;
    unsigned int      HeightAct;
    unsigned char       mmu_en;
    unsigned char   scale_mode = 1;
    unsigned char   dither_en = 0;

    struct private_handle_t* handle = srchnd;
    dither_en = android::bytesPerPixel(GPU_FORMAT) == android::bytesPerPixel(GPU_DST_FORMAT) ? 0 : 1;
    LOGV(" hwcBlit start--->");

    LOGV("layer src w-h[%d,%d]", srchnd->width, srchnd->height);
    memset(&Rga_Request, 0x0, sizeof(Rga_Request));
    /* >>> Begin surface information. */
    hwcONERROR(
        hwcLockBuffer(Context,
                      srchnd,
                      &srcLogical,
                      &srcPhysical,
                      &srcWidth,
                      &srcHeight,
                      &srcStride,
                      &srcInfo));

    hwcONERROR(
        hwcLockBuffer(Context,
                      DstHandle,
                      &dstLogical,
                      &dstPhysical,
                      &dstWidth,
                      &dstHeight,
                      &dstStride,
                      &dstInfo));

    hwcONERROR(
        hwcGetFormat(srchnd,
                     &srcFormat
                    ));

    //fix bug of color change when running screenrecord
    if (DstHandle->format != Context->fbhandle.format)
    {
        ALOGV("Force dst format to fb format: %x => %x", DstHandle->format, Context->fbhandle.format);
        DstHandle->format = Context->fbhandle.format;
    }

    hwcONERROR(
        hwcGetFormat(DstHandle,
                     &dstFormat
                    ));


    /* <<< End surface information. */

    /* Setup transform */
    //Src->transform = HWC_TRANSFORM_FLIP_H;

    {
        srcRect.left    = SrcRect->left;
        srcRect.top     = SrcRect->top;
        srcRect.right   = SrcRect->right;
        srcRect.bottom  = SrcRect->bottom;

        dstRect.left   = DstRect->left;
        dstRect.top    = DstRect->top;
        dstRect.right  = DstRect->right;
        dstRect.bottom = DstRect->bottom;
    }


    clip.xmin = 0;
    clip.xmax = dstWidth - 1;
    clip.ymin = 0;
    clip.ymax = dstHeight - 1;


    if (GPU_FORMAT ==  HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
    {
        srcLogical = (void*)(srcPhysical + 0x60000000);
        dstLogical = (void*)(dstPhysical + 0x60000000);
    }

#if !ONLY_USE_FB_BUFFERS
    if (srchnd->type == 1)
        dstFd = 0;
    else
        dstFd = (unsigned int)(Context->membk_fds[Context->membk_index]);
    dstBase = (unsigned int)(Context->membk_base[Context->membk_index]);
#else
    if (srchnd->type == 1)
        dstFd = 0;      //fix rga switch buffer bug
    else
        dstFd = (unsigned int)(DstHandle->share_fd);
    dstBase = (unsigned int)(DstHandle->base);
#endif

   // ALOGD("mem_type=%d", srchnd->type);
    if (srchnd->type == 1)
    {
        RGA_set_src_vir_info(&Rga_Request, srchnd->base, 0, 0, srcStride, srcHeight, srcFormat, 0);
        RGA_set_dst_vir_info(&Rga_Request,  DstHandle->base, 0, 0, DstHandle->stride, dstHeight, &clip, dstFormat, 0);
        rga_set_fds_offsets(&Rga_Request, srchnd->share_fd, dstFd, 0, 0);
    }
    else
    {
        RGA_set_src_vir_info(&Rga_Request, 0, 0,  0, srcStride, srcHeight, srcFormat, 0);
        RGA_set_dst_vir_info(&Rga_Request, 0, 0,  0, DstHandle->stride, dstHeight, &clip, dstFormat, 0);
        rga_set_fds_offsets(&Rga_Request, srchnd->share_fd, dstFd, 0, 0);
    }
    LOGV("RGA src:fd=%d,base=%p,src_vir_w = %d, src_vir_h = %d,srcLogical=%x,srcFormat=%d", srchnd->share_fd, srchnd->base, \
         srcStride, srcHeight, srcLogical, srcFormat);
    LOGV("RGA dst:fd=%d,offset=%d,base=%p,dst_vir_w = %d, dst_vir_h = %d,dstLogical=%x,dstPhysical=%x,dstFormat=%d", DstHandle->share_fd, DstHandle->offset, DstHandle->base, \
         dstWidth, dstHeight, dstLogical, dstPhysical, dstFormat);

    mmu_en = 0;

    LOGV("%s(%d):  "
         "source=%p srcPhysical=%x,[%dx%d](stride=%d,format=%d) "
         "dest=%p [%dx%d](stride=%d,format=%d) "
         "rects=%d",
         __FUNCTION__,
         __LINE__,
         srchnd,
         srcPhysical,
         srcWidth,
         srcHeight,
         srcStride,
         srcFormat,
         DstHandle,
         dstWidth,
         dstHeight,
         dstStride,
         dstFormat,
         Region->numRects);

    /* Get plane alpha. */
    planeAlpha = Src->blending >> 16;

    LOGI(" planeAlpha=%x,Src->blending=%x", planeAlpha, Src->blending);


#if 1
    /* Setup blending. */
    switch ((Src->blending & 0xFFFF))
    {
        case HWC_BLENDING_PREMULT:
            perpixelAlpha = _HasAlpha(srcFormat);

            /* Setup alpha blending. */
            if (perpixelAlpha && planeAlpha < 255)
            {

                RGA_set_alpha_en_info(&Rga_Request, 1, 2, planeAlpha , 1, 9, 0);
            }
            else if (perpixelAlpha)
            {
                /* Perpixel alpha only. */
                RGA_set_alpha_en_info(&Rga_Request, 1, 1, 0, 1, 3, 0);

            }
            else /* if (planeAlpha < 255) */
            {
                /* Plane alpha only. */
                RGA_set_alpha_en_info(&Rga_Request, 1, 0, planeAlpha , 0, 0, 0);

            }
            break;

        case HWC_BLENDING_COVERAGE:
            /* SRC_ALPHA / ONE_MINUS_SRC_ALPHA. */
            /* Cs' = Cs * As
             * As' = As
             * C = Cs' + Cd * (1 - As)
             * A = As' + Ad * (1 - As) */
            perpixelAlpha = _HasAlpha(srcFormat);
            LOGI("perpixelAlpha=%d,planeAlpha=%d,line=%d ", perpixelAlpha, planeAlpha, __LINE__);
            /* Setup alpha blending. */
            if (perpixelAlpha && planeAlpha < 255)
            {

                RGA_set_alpha_en_info(&Rga_Request, 1, 2, planeAlpha , 0, 0, 0);
            }
            else if (perpixelAlpha)
            {
                /* Perpixel alpha only. */
                RGA_set_alpha_en_info(&Rga_Request, 1, 1, 0, 0, 0, 0);

            }
            else /* if (planeAlpha < 255) */
            {
                /* Plane alpha only. */
                RGA_set_alpha_en_info(&Rga_Request, 1, 0, planeAlpha , 0, 0, 0);

            }
            break;

        case HWC_BLENDING_NONE:
        default:
            /* Tips: BLENDING_NONE is non-zero value, handle zero value as
             * BLENDING_NONE. */
            /* C = Cs
             * A = As */
            break;
    }
#endif
    /* Check yuv format. */
    yuvFormat = (srcFormat >= RK_FORMAT_YCbCr_422_SP && srcFormat <= RK_FORMAT_YCbCr_420_P);

    /* Check stretching. */
    if ((Src->transform == HWC_TRANSFORM_ROT_90) || (Src->transform == HWC_TRANSFORM_ROT_270))
    {
        hfactor = (float)(srcRect.bottom - srcRect.top)
                  / (dstRect.right - dstRect.left);

        vfactor = (float)(srcRect.right - srcRect.left)
                  / (dstRect.bottom - dstRect.top);

    }
    else
    {
        hfactor = (float)(srcRect.right - srcRect.left)
                  / (dstRect.right - dstRect.left);

        vfactor = (float)(srcRect.bottom - srcRect.top)
                  / (dstRect.bottom - dstRect.top);

    }

    if (hfactor < 1 || vfactor < 1)  // scale up use bicubic
        scale_mode = 2;

    stretch = (hfactor != 1.0f) || (vfactor != 1.0f);

    if (stretch && (srcFormat == RK_FORMAT_RGBA_8888 || srcFormat == RK_FORMAT_BGRA_8888))
    {
        scale_mode = 0;     //  force change scale_mode to 0 ,for rga not support
    }


    LOGV("%s(%d):,SRCname=%s,VirSrcRect=[%d,%d,%d,%d] => VirDstRect=[%d,%d,%d,%d] "
         "stretch=%d hfactor=%f vfactor=%f",
         __FUNCTION__,
         __LINE__,
         Src->LayerName,
         srcRect.left,
         srcRect.top,
         srcRect.right,
         srcRect.bottom,
         dstRect.left,
         dstRect.top,
         dstRect.right,
         dstRect.bottom,
         stretch,
         hfactor,
         vfactor);


    /* Go through all visible regions (clip rectangles?). */
    do
    {
        /* 16 rectangles a batch. */
        unsigned int m;
        hwcRECT srcRects[16];
        hwcRECT dstRects[16];
        hwc_rect_t const * rects = Region->rects;

        for (m = 0; n < (unsigned int) Region->numRects && m < 16; n++)
        {
            /* Hardware will mirror in dest rect and blit area in clipped rect.
             * But we need mirror area in clippred rect.
             * NOTE: Now we always set dstRect to clip area. */

            /* Intersect clip with dest. */
            dstRects[m].left   = hwcMAX(dstRect.left,   rects[n].left);
            dstRects[m].top    = hwcMAX(dstRect.top,    rects[n].top);
            dstRects[m].right  = hwcMIN(dstRect.right,  rects[n].right);
            dstRects[m].right  = hwcMIN(dstRects[m].right, dstWidth);
            dstRects[m].bottom = hwcMIN(dstRect.bottom, rects[n].bottom);
            if (dstRects[m].top < 0) // @ buyudaren grame dstRects[m].top < 0,bottom is height ,so do this
            {
                dstRects[m].top = dstRects[m].top + dstHeight;
                dstRects[m].bottom = dstRects[m].bottom + dstRects[m].top;
            }

            /* Check dest area. */
            if ((dstRects[m].right <= dstRects[m].left)
                    || (dstRects[m].bottom <= dstRects[m].top)
               )
            {
                /* Skip this empty rectangle. */
                LOGI("%s(%d):  skip empty rectangle [%d,%d,%d,%d]",
                     __FUNCTION__,
                     __LINE__,
                     dstRects[m].left,
                     dstRects[m].top,
                     dstRects[m].right,
                     dstRects[m].bottom);

                continue;
            }
            LOGI("%s(%d): Region rect[%d]:  [%d,%d,%d,%d]",
                 __FUNCTION__,
                 __LINE__,
                 m,
                 rects[n].left,
                 rects[n].top,
                 rects[n].right,
                 rects[n].bottom);

            /* Advance to next rectangle. */
            m++;
        }

        /* Try next group if no rects. */
        if (m == 0)
        {
            hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
        }

        if (yuvFormat)
        {
            /* Video filter blit
             * Only FilterBlit support yuv format covertion. */
            unsigned int uLogical;
            unsigned int vLogical;
            unsigned int uStride;
            unsigned int vStride;

            hwcONERROR(
                _ComputeUVOffset(srcFormat,
                                 (unsigned int)srcLogical,
                                 srcHeight,
                                 srcStride,
                                 &uLogical,
                                 &uStride,
                                 &vLogical,
                                 &vStride));

            // Rga_Request.src.uv_addr  = uLogical;
            // Rga_Request.src.v_addr   = vLogical;
            //scale_mode = 2;
            LOGI("yuvformat y_addr=%x,uv_addr=%x,v_addr=%x", srcLogical, uLogical, vLogical);
        }
        for (unsigned int i = 0; i < m; i++)
        {
            switch (Src->transform)
            {

                case 0:
                    RotateMode = stretch;
                    Xoffset = dstRects[i].left;
                    Yoffset = dstRects[i].top;
                    WidthAct = dstRects[i].right - dstRects[i].left ;
                    HeightAct = dstRects[i].bottom - dstRects[i].top ;
                    /* calculate srcRects,dstRect is virtual rect,dstRects[i] is actual rect,
                       hfactor and vfactor are scale factor.
                    */
                    srcRects[i].left   = srcRect.left
                                         - (int)((dstRect.left   - dstRects[i].left)   * hfactor);

                    srcRects[i].top    = srcRect.top
                                         - (int)((dstRect.top    - dstRects[i].top)    * vfactor);

                    srcRects[i].right  = srcRect.right
                                         - (int)((dstRect.right  - dstRects[i].right)  * hfactor);

                    srcRects[i].bottom = srcRect.bottom
                                         - (int)((dstRect.bottom - dstRects[i].bottom) * vfactor);

                    if (yuvFormat && vfactor != 1)
                    {
                        srcRects[i].left  =  srcRects[i].left  +   srcRects[i].left % 2;
                        srcRects[i].top  =  srcRects[i].top + srcRects[i].top % 2;
                        srcRects[i].right  =  srcRects[i].right  - srcRects[i].right % 2;
                        srcRects[i].bottom  =  srcRects[i].bottom  - srcRects[i].bottom % 2;
                    }

                    if (yuvFormat && vfactor == 1)
                    {
                        srcRects[i].left  =  srcRects[i].left & (~1);
                        srcRects[i].top  =  srcRects[i].top & (~1);
                        srcRects[i].right  =  srcRects[i].right & (~1);
                        srcRects[i].bottom  =  srcRects[i].bottom & (~1);
                    }
                    break;
                case HWC_TRANSFORM_FLIP_H:
                    RotateMode      = 2;
                    Xoffset = dstRects[i].left;
                    Yoffset = dstRects[i].top;
                    WidthAct = dstRects[i].right - dstRects[i].left ;
                    HeightAct = dstRects[i].bottom - dstRects[i].top ;
                    srcRects[i].left   = srcRect.left
                                         - (int)((dstRect.left   - dstRects[i].left)   * hfactor);

                    srcRects[i].top    = srcRect.top
                                         - (int)((dstRect.top    - dstRects[i].top)    * vfactor);

                    srcRects[i].right  = srcRect.right
                                         - (int)((dstRect.right  - dstRects[i].right)  * hfactor);

                    srcRects[i].bottom = srcRect.bottom
                                         - (int)((dstRect.bottom - dstRects[i].bottom) * vfactor);

                    break;
                case HWC_TRANSFORM_FLIP_V:
                    RotateMode      = 3;
                    Xoffset = dstRects[i].left;
                    Yoffset = dstRects[i].top;
                    WidthAct = dstRects[i].right - dstRects[i].left ;
                    HeightAct = dstRects[i].bottom - dstRects[i].top ;
                    srcRects[i].left   = srcRect.left
                                         - (int)((dstRect.left   - dstRects[i].left)   * hfactor);

                    srcRects[i].top    = srcRect.top
                                         - (int)((dstRect.top    - dstRects[i].top)    * vfactor);

                    srcRects[i].right  = srcRect.right
                                         - (int)((dstRect.right  - dstRects[i].right)  * hfactor);

                    srcRects[i].bottom = srcRect.bottom
                                         - (int)((dstRect.bottom - dstRects[i].bottom) * vfactor);

                    break;

                case HWC_TRANSFORM_ROT_90:
                    RotateMode      = 1;
                    Rotation    = 90;
                    Xoffset = dstRects[i].right - 1;
                    Yoffset = dstRects[i].top ;
                    WidthAct = dstRects[i].bottom - dstRects[i].top ;
                    HeightAct = dstRects[i].right - dstRects[i].left ;

                    srcRects[i].left   = srcRect.top
                                         - (int)((dstRect.top    - dstRects[i].top)    * vfactor);

                    srcRects[i].top    =  srcRect.left
                                          - (int)((dstRect.left   - dstRects[i].left)   * hfactor);

                    srcRects[i].right  = srcRects[i].left
                                         + (int)((dstRects[i].bottom - dstRects[i].top) * vfactor);

                    srcRects[i].bottom = srcRects[i].top
                                         + (int)((dstRects[i].right  - dstRects[i].left) * hfactor);

                    break;

                case HWC_TRANSFORM_ROT_180:
                    RotateMode      = 1;
                    Rotation    = 180;
                    Xoffset = dstRects[i].right - 1;
                    Yoffset = dstRects[i].bottom - 1;
                    WidthAct = dstRects[i].right - dstRects[i].left;
                    HeightAct = dstRects[i].bottom - dstRects[i].top;
                    //srcRects[i].left   = srcRect.left +  (srcRect.right - srcRect.left)
                    //- ((dstRects[i].right - dstRects[i].left) * hfactor)
                    //+ ((dstRect.left   - dstRects[i].left)   * hfactor);

                    srcRects[i].left   = srcRect.left + (srcRect.right - srcRect.left)
                                         - ((dstRects[i].right - dstRect.left)   * hfactor);

                    srcRects[i].top    = srcRect.top
                                         - (int)((dstRect.top    - dstRects[i].top)    * vfactor);

                    srcRects[i].right  = srcRects[i].left
                                         + (int)((dstRects[i].right  - dstRects[i].left) * hfactor);

                    srcRects[i].bottom = srcRects[i].top
                                         + (int)((dstRects[i].bottom - dstRects[i].top) * vfactor);

                    break;

                case HWC_TRANSFORM_ROT_270:
                    RotateMode      = 1;
                    Rotation        = 270;
                    Xoffset = dstRects[i].left;
                    Yoffset = dstRects[i].bottom - 1;
                    WidthAct = dstRects[i].bottom - dstRects[i].top ;
                    HeightAct = dstRects[i].right - dstRects[i].left ;

                    //srcRects[i].left   = srcRect.top +  (srcRect.right - srcRect.left)
                    //- ((dstRects[i].bottom - dstRects[i].top) * vfactor)
                    //+ ((dstRect.top    - dstRects[i].top)    * vfactor);

                    srcRects[i].left   = srcRect.top + (srcRect.right - srcRect.left)
                                         - ((dstRects[i].bottom - dstRect.top)    * vfactor);

                    srcRects[i].top    =  srcRect.left
                                          - (int)((dstRect.left   - dstRects[i].left)   * hfactor);

                    srcRects[i].right  = srcRects[i].left
                                         + (int)((dstRects[i].bottom - dstRects[i].top) * vfactor);

                    srcRects[i].bottom = srcRects[i].top
                                         + (int)((dstRects[i].right  - dstRects[i].left) * hfactor);

                    break;
                default:
                    hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                    break;
            }
            LOGV("%s(%d): Adjust ActSrcRect[%d]=[%d,%d,%d,%d] => ActDstRect=[%d,%d,%d,%d]",
                 __FUNCTION__,
                 __LINE__,
                 i,
                 srcRects[i].left,
                 srcRects[i].top,
                 srcRects[i].right,
                 srcRects[i].bottom,
                 dstRects[i].left,
                 dstRects[i].top,
                 dstRects[i].right,
                 dstRects[i].bottom
                );

            LOGV("RGA src[%d] Xoffset=%d,Yoffset=%d,WidthAct=%d,HeightAct= %d",
                 i,
                 srcRects[i].left,
                 srcRects[i].top,
                 srcRects[i].right -  srcRects[i].left,
                 srcRects[i].bottom - srcRects[i].top);
            LOGV("RGA dst[%d] Xoffset=%d,Yoffset=%d,WidthAct=%d,HeightAct=%d,transform =%d,RotateMode=%d,Rotation=%d",
                 i,
                 Xoffset,
                 Yoffset,
                 WidthAct,
                 HeightAct,
                 Src->transform,
                 RotateMode,
                 Rotation);
            RGA_set_bitblt_mode(&Rga_Request, scale_mode, RotateMode, Rotation, dither_en, 0, 0);
            RGA_set_src_act_info(&Rga_Request, srcRects[i].right -  srcRects[i].left, srcRects[i].bottom - srcRects[i].top,  srcRects[i].left, srcRects[i].top);
            RGA_set_dst_act_info(&Rga_Request, WidthAct, HeightAct, Xoffset, Yoffset);

            if (srchnd->type == 1)
            {
                RGA_set_mmu_info(&Rga_Request, 1, 0, 0, 0, 0, 2);
                Rga_Request.mmu_info.mmu_flag |= (1 << 31) | (1 << 10) | (1 << 8);
            }

            if (ioctl(Context->engine_fd, RGA_BLIT_ASYNC, &Rga_Request) != 0)
            {
                LOGE("%s(%d)[i=%d]:  RGA_BLIT_ASYNC Failed Region->numRects=%d ,n=%d,m=%d", __FUNCTION__, __LINE__, i, Region->numRects, n, m);
            }


        }
    }
    while (n < (unsigned int) Region->numRects);
    LOGI(" hwcBlit end---<");

    //debug: clear fb to 0xff
#if 0
    if (1 == hwc_get_int_property("sys.setfb", "0"))
    {
        memset((void*)dstBase, 0xff, dstWidth*dstWidth);
    }
#endif

    return hwcSTATUS_OK;

OnError:
    LOGE("%s(%d):  Failed", __FUNCTION__, __LINE__);
#ifdef HWC_Layer_DEBUG
    LOGE("composer err in layer=%s,rect.left=%d,rect.top=%d,rect.right=%d,rect.bottom=%d",
         Src->LayerName, Src->sourceCrop.left, Src->sourceCrop.top, Src->sourceCrop.right, Src->sourceCrop.bottom);
#endif
    /* Error roll back. */

    /* Unlock buffer. */

    return status;
}

hwcSTATUS
hwcDim(
    IN hwcContext * Context,
    IN hwc_layer_1_t * Src,
    IN struct private_handle_t * DstHandle,
    IN hwc_rect_t * DstRect,
    IN hwc_region_t * Region
)
{
    hwcSTATUS status = hwcSTATUS_OK;

#if 1
    void *     dstLogical;
    unsigned int      dstFd = ~0, dstBase = ~0;
    void *     dstInfo;
    unsigned int      dstStride;
    unsigned int      dstWidth;
    unsigned int      dstHeight;
    RgaSURF_FORMAT dstFormat;

    unsigned int        planeAlpha;
    unsigned int        n = 0;
    struct rga_req  Rga_Request;
    RECT clip;
    unsigned int      Xoffset;
    unsigned int      Yoffset;
    unsigned int      WidthAct;
    unsigned int      HeightAct;
    struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;
    memset(&Rga_Request, 0x0, sizeof(Rga_Request));

    /* >>> Begin dest surface information. */
    LOGV("hwcDim start--->");
    hwcONERROR(
        hwcLockBuffer(Context,
                      DstHandle,
                      &dstLogical,
                      &dstFd,
                      &dstWidth,
                      &dstHeight,
                      &dstStride,
                      &dstInfo));
    hwcONERROR(
        hwcGetFormat(DstHandle,
                     &dstFormat
                    ));
    /* <<< End surface information. */

    /* Get planeAlpha. */
    planeAlpha = Src->blending >> 16;

    LOGI("%s(%d):  planeAlpha=%d", __FUNCTION__, __LINE__, planeAlpha);

    clip.xmin = 0;
    clip.xmax = dstWidth - 1;
    clip.ymin = 0;
    clip.ymax = dstHeight - 1;

#if !ONLY_USE_FB_BUFFERS
    if (srchnd->type == 1)
        dstFd = 0;
    else
        dstFd = (unsigned int)(Context->membk_fds[Context->membk_index]);
    dstBase = (unsigned int)(Context->membk_base[Context->membk_index]);
#else
    if (srchnd->type == 1)
        dstFd = 0;
    else
        dstFd = (unsigned int)(Context->mFbFd);
    dstBase = (unsigned int)(Context->mFbBase);
#endif

    /* Setup target. */
    LOGI("RGA dst_vir_w = %d, dst_vir_h = %d", dstWidth, dstHeight);

    if (srchnd->type == 1)
    {
        RGA_set_dst_vir_info(&Rga_Request, dstFd, dstBase, 0, dstWidth, dstHeight, &clip, dstFormat, 0);
    }
    else
    {
        RGA_set_dst_vir_info(&Rga_Request, dstFd, 0, 0, dstWidth, dstHeight, &clip, dstFormat, 0);
    }

    /* Go through all visible regions (clip rectangles?). */
    do
    {
        /* 16 rectangles a batch. */
        unsigned int m;
        hwcRECT dstRects[16];
        COLOR_FILL FillColor ;
        memset(&FillColor , 0x0, sizeof(COLOR_FILL));
        hwc_rect_t const * rects = Region->rects;

        for (m = 0; n < (unsigned int) Region->numRects && m < 16; n++)
        {
            /* Intersect clip with dest. */
            dstRects[m].left   = hwcMAX(DstRect->left,   rects[n].left);
            dstRects[m].top    = hwcMAX(DstRect->top,    rects[n].top);
            dstRects[m].right  = hwcMIN(DstRect->right,  rects[n].right);
            dstRects[m].bottom = hwcMIN(DstRect->bottom, rects[n].bottom);


            /* Check dest area. */
            if ((dstRects[m].right <= dstRects[m].left)
                    || (dstRects[m].bottom <= dstRects[m].top)
               )
            {
                /* Skip this empty rectangle. */
                LOGV("%s(%d):  skip empty rectangle [%d,%d,%d,%d]",
                     __FUNCTION__,
                     __LINE__,
                     dstRects[m].left,
                     dstRects[m].top,
                     dstRects[m].right,
                     dstRects[m].bottom);

                continue;
            }

            LOGV("%s(%d):  rect[%d]: [%d,%d,%d,%d] by [%d,%d,%d,%d]",
                 __FUNCTION__,
                 __LINE__,
                 m,
                 dstRects[m].left,
                 dstRects[m].top,
                 dstRects[m].right,
                 dstRects[m].bottom,
                 rects[n].left,
                 rects[n].top,
                 rects[n].right,
                 rects[n].bottom);

            /* Advance to next rectangle. */
            m++;
        }

        /* Try next group if no rects. */
        if (m == 0)
        {
            hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
        }

        if (planeAlpha == 255)
        {

            /* Perform a Clear. */
            RGA_set_color_fill_mode(&Rga_Request, &FillColor, 0, 0, 0x00, 0, 0, 0, 0, 0);

        }
        else
        {

            RGA_set_alpha_en_info(&Rga_Request, 1, 0, planeAlpha , 0, 0, 0);
            RGA_set_color_fill_mode(&Rga_Request, &FillColor, 0, 0, 0x00, 0, 0, 0, 0, 0);


        }
        for (unsigned int i = 0; i < n; i++)
        {
            Xoffset = dstRects[i].left;
            Yoffset = dstRects[i].top;
            WidthAct = dstRects[i].right - dstRects[i].left ;
            HeightAct = dstRects[i].bottom - dstRects[i].top ;

            LOGI("RGA dst[%d] Xoffset=%d,Yoffset=%d,WidthAct=%d,HeightAct=%d",
                 i,
                 Xoffset,
                 Yoffset,
                 WidthAct,
                 HeightAct
                );
            RGA_set_dst_act_info(&Rga_Request, WidthAct, HeightAct, Xoffset, Yoffset);

            RGA_set_mmu_info(&Rga_Request, 1, 0, 0, 0, 0, 2);

            if (srchnd->type == 1)
            {
                Rga_Request.mmu_info.mmu_flag |= (1 << 31) | (1 << 10) | (1 << 8);
            }

            if (ioctl(Context->engine_fd, RGA_BLIT_ASYNC, &Rga_Request) != 0)
            {
                LOGE("%s(%d)[i=%d]:  RGA_BLIT_ASYNC Failed", __FUNCTION__, __LINE__, i);
            }
        }
    }
    while (n < (unsigned int) Region->numRects);

    /* Unlock buffer. */

    LOGI("hwcDim end--->");

    return hwcSTATUS_OK;

OnError:
    LOGE("%s(%d):  Failed", __FUNCTION__, __LINE__);
    /* Error roll back. */

#endif
    return status;
}

hwcSTATUS
hwcClear(
    IN hwcContext * Context,
    IN unsigned int Color,
    IN hwc_layer_1_t * Src,
    IN struct private_handle_t * DstHandle,
    IN hwc_rect_t * DstRect,
    IN hwc_region_t * Region
)
{
    hwcSTATUS status = hwcSTATUS_OK;

#if 1
    void *     dstLogical;
    unsigned int      dstFd = ~0, dstBase = ~0;
    void *     dstInfo;
    unsigned int      dstStride;
    unsigned int      dstWidth;
    unsigned int      dstHeight;
    RgaSURF_FORMAT dstFormat;
    unsigned int        n = 0;

    struct rga_req  Rga_Request;
    RECT clip;
    unsigned int      Xoffset;
    unsigned int      Yoffset;
    unsigned int      WidthAct;
    unsigned int      HeightAct;
    COLOR_FILL FillColor ;
    struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;

    memset(&FillColor , 0x0, sizeof(COLOR_FILL));

    LOGV("%s(%d):  color=0x%.8x", __FUNCTION__, __LINE__, Color);

    memset(&Rga_Request, 0x0, sizeof(Rga_Request));

    /* >>> Begin dest surface information. */

    hwcONERROR(
        hwcLockBuffer(Context,
                      DstHandle,
                      &dstLogical,
                      &dstFd,
                      &dstWidth,
                      &dstHeight,
                      &dstStride,
                      &dstInfo));
    hwcONERROR(
        hwcGetFormat(DstHandle,
                     &dstFormat
                    ));
    /* <<< End surface information. */

    /* Get planeAlpha. */


    clip.xmin = 0;
    clip.xmax = dstWidth - 1;
    clip.ymin = 0;
    clip.ymax = dstHeight - 1;

#if !ONLY_USE_FB_BUFFERS
    if (srchnd->type == 1)
        dstFd = 0;
    else
        dstFd = (unsigned int)(Context->membk_fds[Context->membk_index]);
    dstBase = (unsigned int)(Context->membk_base[Context->membk_index]);
#else
    if (srchnd->type == 1)
        dstFd = 0;
    else
        dstFd = (unsigned int)(Context->mFbFd);
    dstBase = (unsigned int)(Context->mFbBase);
#endif

    /* Setup target. */
    LOGI("RGA dst_vir_w = %d, dst_vir_h = %d", dstWidth, dstHeight);

    if (srchnd->type == 1)
    {
        RGA_set_dst_vir_info(&Rga_Request, dstFd, dstBase, 0, dstWidth, dstHeight, &clip, dstFormat, 0);
    }
    else
    {
        RGA_set_dst_vir_info(&Rga_Request, dstFd, 0, 0, dstWidth, dstHeight, &clip, dstFormat, 0);
    }

    /* Go through all visible regions (clip rectangles?). */
    do
    {
        /* 16 rectangles a batch. */
        unsigned int m;
        hwcRECT dstRects[16];

        hwc_rect_t const * rects = Region->rects;

        for (m = 0; n < (unsigned int) Region->numRects && m < 16; n++)
        {
            /* Intersect clip with dest. */
            dstRects[m].left   = hwcMAX(DstRect->left,   rects[n].left);
            dstRects[m].top    = hwcMAX(DstRect->top,    rects[n].top);
            dstRects[m].right  = hwcMIN(DstRect->right,  rects[n].right);
            dstRects[m].bottom = hwcMIN(DstRect->bottom, rects[n].bottom);


            /* Check dest area. */
            if ((dstRects[m].right <= dstRects[m].left)
                    || (dstRects[m].bottom <= dstRects[m].top)
               )
            {
                /* Skip this empty rectangle. */
                LOGV("%s(%d):  skip empty rectangle [%d,%d,%d,%d]",
                     __FUNCTION__,
                     __LINE__,
                     dstRects[m].left,
                     dstRects[m].top,
                     dstRects[m].right,
                     dstRects[m].bottom);

                continue;
            }

            LOGV("%s(%d):  rect[%d]: [%d,%d,%d,%d] by [%d,%d,%d,%d]",
                 __FUNCTION__,
                 __LINE__,
                 m,
                 dstRects[m].left,
                 dstRects[m].top,
                 dstRects[m].right,
                 dstRects[m].bottom,
                 rects[n].left,
                 rects[n].top,
                 rects[n].right,
                 rects[n].bottom);

            /* Advance to next rectangle. */
            m++;
        }

        /* Try next group if no rects. */
        if (m == 0)
        {
            hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
        }
        RGA_set_color_fill_mode(&Rga_Request, &FillColor, 0, 0, Color, 0, 0, 0, 0, 0);

        for (unsigned int i = 0; i < n; i++)
        {
            Xoffset = dstRects[i].left;
            Yoffset = dstRects[i].top;
            WidthAct = dstRects[i].right - dstRects[i].left ;
            HeightAct = dstRects[i].bottom - dstRects[i].top ;

            LOGI("RGA dst[%d] Xoffset=%d,Yoffset=%d,WidthAct=%d,HeightAct=%d",
                 i,
                 Xoffset,
                 Yoffset,
                 WidthAct,
                 HeightAct
                );
            RGA_set_mmu_info(&Rga_Request, 1, 0, 0, 0, 0, 2);

            if (srchnd->type == 1)
            {
                Rga_Request.mmu_info.mmu_flag |= (1 << 31) | (1 << 10) | (1 << 8);
            }

            RGA_set_dst_act_info(&Rga_Request, WidthAct, HeightAct, Xoffset, Yoffset);
            if (ioctl(Context->engine_fd, RGA_BLIT_ASYNC, &Rga_Request) != 0)
            {
                LOGE("%s(%d)[i=%d]:  RGA_BLIT_ASYNC Failed", __FUNCTION__, __LINE__, i);
            }
        }

    }
    while (n < (unsigned int) Region->numRects);

    /* Unlock buffer. */

    return hwcSTATUS_OK;

OnError:
    LOGE("%s(%d):  Failed", __FUNCTION__, __LINE__);
    /* Error roll back. */

#endif
    return status;
}



hwcSTATUS
hwcLayerToWin(
    IN hwcContext * Context,
    IN hwc_layer_1_t * Src,
    IN struct private_handle_t * DstHandle,
    IN hwc_rect_t * SrcRect,
    IN hwc_rect_t * DstRect,
    IN hwc_region_t * Region,
    IN int Index,
    IN int Win,
    OUT struct rk_fb_win_cfg_data* pFbInfo
)
{
    hwcSTATUS status = hwcSTATUS_OK;

    void *              srcLogical  = NULL;
    unsigned int        srcPhysical = ~0;
    void *              srcInfo     = NULL;
    unsigned int        srcStride;
    unsigned int        srcWidth;
    unsigned int        srcHeight;
    float hfactor;
    float vfactor;
    int  stretch;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo info;
    unsigned int  videodata[2];
    unsigned int m;
    hwcRECT srcRects[16];
    hwcRECT dstRects[16];
    int video_width = 0;
    int video_height = 0;
    unsigned int dst_x;
    unsigned int dst_y;
    hwc_rect_t const * rects = Region->rects;
    int iFormat;
    int layer_fd = -1;

    int fbFd = Win ? Context->fbFd1 : Context->fbFd;

    struct private_handle_t* srchnd = (struct private_handle_t *) Src->handle;
    struct private_handle_t* handle = srchnd;
    LOGV(" hwcLayerToWin start--->");

    LOGV("hwcLayerToWin%d layer src w-h[%d,%d], f[%d]", Win, GPU_WIDTH, GPU_HEIGHT, GPU_FORMAT);

    HWC_UNREFERENCED_PARAMETER(DstHandle);

    if (pFbInfo == NULL)
        hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);

    /* >>> Begin surface information. */
    hwcONERROR(
        hwcLockBuffer(Context,
                      srchnd,
                      &srcLogical,
                      &srcPhysical,
                      &srcWidth,
                      &srcHeight,
                      &srcStride,
                      &srcInfo));

    if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO && Src->transform != 0 && Context->ippDev != NULL)
    {
        Context->ippDev->ipp_rotate_and_scale(srchnd, Src->transform, videodata, &video_width, &video_height);
        // videodata[1]= videodata[0] + (srcHeight+SrcRect->top) * srcStride + SrcRect->left;
        videodata[1] = videodata[0]  + srcHeight * srcStride ;
    }
    else if (Src->direct_fd)
    {
        layer_fd = Src->direct_fd;
    }
    else
    {
        layer_fd = srchnd->share_fd;
    }

    LOGV(" Src->transform=%d,SrcRect[%d,%d,%d,%d],DstRect[%d,%d,%d,%d]", Src->transform ,
         SrcRect->left, SrcRect->top, SrcRect->right, SrcRect->bottom ,
         DstRect->left, DstRect->top, DstRect->right, DstRect->bottom);

    if ((Src->transform == HWC_TRANSFORM_ROT_90) || (Src->transform == HWC_TRANSFORM_ROT_270))
    {
        hfactor = (float)(SrcRect->bottom - SrcRect->top)
                  / (DstRect->right - DstRect->left);

        vfactor = (float)(SrcRect->right - SrcRect->left)
                  / (DstRect->bottom - DstRect->top);
    }
    else
    {
        hfactor = (float)(SrcRect->right - SrcRect->left)
                  / (DstRect->right - DstRect->left);

        vfactor = (float)(SrcRect->bottom - SrcRect->top)
                  / (DstRect->bottom - DstRect->top);
    }

    stretch = (hfactor != 1.0f) || (vfactor != 1.0f);


    LOGV("%s(%d):  "
         "name=%s handle=%p,srcPhysical=%x,videodata[0]=%x,[%dx%d](stride=%d,format=%d),hfactor=%f,vfactor=%f",
         __FUNCTION__,
         __LINE__,
         Src->LayerName, srchnd,
         srcPhysical, videodata[0],
         srcWidth,
         srcHeight,
         srcStride,
         srchnd->format,
         hfactor,
         vfactor
        );

    for (m = 0; m < (unsigned int) Region->numRects ; m++)
    {
        /* Hardware will mirror in dest rect and blit area in clipped rect.
         * But we need mirror area in clippred rect.
         * NOTE: Now we always set dstRect to clip area. */

        /* Intersect clip with dest. */
        dstRects[m].left   = hwcMAX(DstRect->left,   rects[m].left);
        dstRects[m].top    = hwcMAX(DstRect->top,    rects[m].top);
        dstRects[m].right  = hwcMIN(DstRect->right,  rects[m].right);
        dstRects[m].bottom = hwcMIN(DstRect->bottom, rects[m].bottom);


        /* Check dest area. */
        if ((dstRects[m].right <= dstRects[m].left)
                || (dstRects[m].bottom <= dstRects[m].top)
           )
        {
            /* Skip this empty rectangle. */
            LOGI("%s(%d):  skip empty rectangle [%d,%d,%d,%d]",
                 __FUNCTION__,
                 __LINE__,
                 dstRects[m].left,
                 dstRects[m].top,
                 dstRects[m].right,
                 dstRects[m].bottom);
            continue;
        }
        LOGI("%s(%d): Region rect[%d]:  [%d,%d,%d,%d]",
             __FUNCTION__,
             __LINE__,
             m,
             rects[m].left,
             rects[m].top,
             rects[m].right,
             rects[m].bottom);
    }
    for (unsigned int i = 0; i < 1; i++)
    {
        switch (Src->transform)
        {
            case 0:
                srcRects[i].left   = SrcRect->left
                                     - (int)((DstRect->left   - dstRects[i].left)   * hfactor);
                srcRects[i].top    = SrcRect->top
                                     - (int)((DstRect->top    - dstRects[i].top)    * vfactor);
                srcRects[i].right  = SrcRect->right
                                     - (int)((DstRect->right  - dstRects[i].right)  * hfactor);
                srcRects[i].bottom = SrcRect->bottom
                                     - (int)((DstRect->bottom - dstRects[i].bottom) * vfactor);
                break;
            case HWC_TRANSFORM_ROT_270:
                srcRects[i].left   = SrcRect->top + (SrcRect->right - SrcRect->left)
                                     - ((dstRects[i].bottom - DstRect->top)    * vfactor);

                srcRects[i].top    =  SrcRect->left
                                      - (int)((DstRect->left   - dstRects[i].left)   * hfactor);

                srcRects[i].right  = srcRects[i].left
                                     + (int)((dstRects[i].bottom - dstRects[i].top) * vfactor);

                srcRects[i].bottom = srcRects[i].top
                                     + (int)((dstRects[i].right  - dstRects[i].left) * hfactor);
                break;

            case HWC_TRANSFORM_ROT_90:
                srcRects[i].left   = SrcRect->top
                                     - (int)((DstRect->top    - dstRects[i].top)    * vfactor);

                srcRects[i].top    =  SrcRect->left
                                      - (int)((DstRect->left   - dstRects[i].left)   * hfactor);

                srcRects[i].right  = srcRects[i].left
                                     + (int)((dstRects[i].bottom - dstRects[i].top) * vfactor);

                srcRects[i].bottom = srcRects[i].top
                                     + (int)((dstRects[i].right  - dstRects[i].left) * hfactor);
                break;

            case HWC_TRANSFORM_ROT_180:
                srcRects[i].left   = SrcRect->left + (SrcRect->right - SrcRect->left)
                                     - ((dstRects[i].right - DstRect->left)   * hfactor);

                srcRects[i].top    = SrcRect->top
                                     - (int)((DstRect->top    - dstRects[i].top)    * vfactor);

                srcRects[i].right  = srcRects[i].left
                                     + (int)((dstRects[i].right  - dstRects[i].left) * hfactor);

                srcRects[i].bottom = srcRects[i].top
                                     + (int)((dstRects[i].bottom - dstRects[i].top) * vfactor);
                break;

            default:
                hwcONERROR(hwcSTATUS_INVALID_ARGUMENT);
                break;
        }
        LOGV("%s(%d): Adjust ActSrcRect[%d]=[%d,%d,%d,%d] => ActDstRect=[%d,%d,%d,%d]",
             __FUNCTION__,
             __LINE__,
             i,
             srcRects[i].left,
             srcRects[i].top,
             srcRects[i].right,
             srcRects[i].bottom,
             dstRects[i].left,
             dstRects[i].top,
             dstRects[i].right,
             dstRects[i].bottom
            );
        if (ioctl(fbFd, FBIOGET_FSCREENINFO, &finfo) == -1)
        {
            LOGE("%s(%d):  fd[%d] Failed", __FUNCTION__, __LINE__, fbFd);
            return hwcSTATUS_IO_ERR;
        }

        if (ioctl(fbFd, FBIOGET_VSCREENINFO, &info) == -1)
        {
            LOGE("%s(%d):  fd[%d] Failed", __FUNCTION__, __LINE__, fbFd);
            return hwcSTATUS_IO_ERR;
        }

        info.activate = FB_ACTIVATE_NOW;
        info.nonstd &= 0x00;
        if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
        {
            info.nonstd |= HAL_PIXEL_FORMAT_YCrCb_NV12;
            iFormat = HAL_PIXEL_FORMAT_YCrCb_NV12;
        }
        else if (Index == 0 && GPU_FORMAT == HAL_PIXEL_FORMAT_RGBA_8888)
        {
            info.nonstd |= HAL_PIXEL_FORMAT_RGBX_8888;
            iFormat = HAL_PIXEL_FORMAT_RGBX_8888;
        }
        else
        {
            info.nonstd |= GPU_FORMAT;//HAL_PIXEL_FORMAT_YCrCb_NV12;//HAL_PIXEL_FORMAT_RGB_565
            iFormat = GPU_FORMAT;
        }
        info.grayscale &= 0xff;

        LOGV("hwcLayerToWin%d: src[%d,%d,%d,%d], dst[%d,%d,%d,%d], src_ex[%d,%d,%d,%d] %d", Win,
             srcRects[i].left, srcRects[i].top, srcRects[i].right, srcRects[i].bottom,
             dstRects[i].left, dstRects[i].top, dstRects[i].right, dstRects[i].bottom,
             Src->exLeft, Src->exTop, Src->exRight, Src->exBottom, Src->exAddrOffset
            );

        info.xoffset = hwcMAX(srcRects[i].left - Src->exLeft, 0);
        info.yoffset = hwcMAX(srcRects[i].top - Src->exTop, 0);
        info.xoffset = info.xoffset - info.xoffset % 2;
        info.yoffset = info.yoffset - info.yoffset % 2;
        info.xres = (srcRects[i].right - srcRects[i].left) + (Src->exLeft + Src->exRight);
        info.yres = (srcRects[i].bottom - srcRects[i].top) + (Src->exTop + Src->exBottom);
        info.xres_virtual = srcStride;
        info.yres_virtual = srcHeight + hwcMAX(Src->exBottom, Src->exTop);

        if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
        {
            if (Src->transform == HWC_TRANSFORM_ROT_90 || Src->transform == HWC_TRANSFORM_ROT_270)
            {
                info.yres = (srcRects[i].right - srcRects[i].left) + (Src->exLeft + Src->exRight);
                info.xres = (srcRects[i].bottom - srcRects[i].top) + (Src->exTop + Src->exBottom);
                info.xres_virtual = video_width;//info.xres ;//srcStride;
                info.yres_virtual = srcHeight * srcStride;//info.yres;//srcHeight + hwcMAX(Src->exBottom,Src->exTop);
                if (Src->transform == HWC_TRANSFORM_ROT_90)
                {
                    info.xoffset = info.xres_virtual - info.xres;
                    info.xoffset = info.xoffset - info.xoffset % 2;
                }
            }
            else if (Src->transform == HWC_TRANSFORM_ROT_180)
            {
                info.yoffset = info.yres_virtual - info.yres;
                info.yoffset = info.yoffset - info.yoffset % 2;
            }
            else
            {

            }
        }

        dst_x = (hwcMAX(dstRects[i].left - Src->exLeft, 0));
        dst_x = dst_x - dst_x % 2;
        info.nonstd |=  dst_x << 8;
        dst_y = (hwcMAX(dstRects[i].top - Src->exTop, 0));
        dst_y = dst_y - dst_y % 2;
        info.nonstd |= dst_y << 20;
        info.grayscale |= ((dstRects[i].right - dstRects[i].left) + (Src->exLeft + Src->exRight)) << 8;
        info.grayscale |= ((dstRects[i].bottom - dstRects[i].top) + (Src->exTop + Src->exBottom)) << 20;

        videodata[0] += Src->exAddrOffset;

        info.activate |= FB_ACTIVATE_FORCE;
        LOGV(" lcdc info : info.nonstd=%x, info.grayscale=%x", info.nonstd, info.grayscale);

        pFbInfo->win_par[Win].area_par[0].data_format = iFormat;
        pFbInfo->win_par[Win].win_id = Win;
        //pFbInfo->win_par[Win].alpha_mode = AB_SRC_OVER;
        //pFbInfo->win_par[Win].g_alpha_val =
        pFbInfo->win_par[Win].z_order = 0;
        if (GPU_FORMAT == HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO)
        {
            pFbInfo->win_par[Win].area_par[0].ion_fd = 0;
            pFbInfo->win_par[Win].area_par[0].phy_addr = srcPhysical;
        }
        else
        {
            pFbInfo->win_par[Win].area_par[0].ion_fd = layer_fd;
        }
        pFbInfo->win_par[Win].area_par[0].acq_fence_fd = -1;//fbLayer->acquireFenceFd;
        pFbInfo->win_par[Win].area_par[0].x_offset = info.xoffset;
        pFbInfo->win_par[Win].area_par[0].y_offset = info.yoffset;
        pFbInfo->win_par[Win].area_par[0].xpos = (info.nonstd >> 8) & 0xfff;
        pFbInfo->win_par[Win].area_par[0].ypos = (info.nonstd >> 20) & 0xfff;
        pFbInfo->win_par[Win].area_par[0].xsize = (info.grayscale >> 8) & 0xfff;
        pFbInfo->win_par[Win].area_par[0].ysize = (info.grayscale >> 20) & 0xfff;
        pFbInfo->win_par[Win].area_par[0].xact = info.xres;
        pFbInfo->win_par[Win].area_par[0].yact = info.yres;
        pFbInfo->win_par[Win].area_par[0].xvir = info.xres_virtual;
        pFbInfo->win_par[Win].area_par[0].yvir = info.yres_virtual;
        pFbInfo->wait_fs = 0;

        //_DumpFbInfo(&info, Win);

        //debug info
        ALOGV("hwcLayerToWin:z_win_galp[%d,%d,%x],[%d,%d,%d,%d]=>[%d,%d,%d,%d],w_h_f[%d,%d,%d],acq_fence_fd=%d,fd=%d,addr=%x",
              pFbInfo->win_par[Win].z_order,
              pFbInfo->win_par[Win].win_id,
              pFbInfo->win_par[Win].g_alpha_val,
              pFbInfo->win_par[Win].area_par[0].x_offset,
              pFbInfo->win_par[Win].area_par[0].y_offset,
              pFbInfo->win_par[Win].area_par[0].xact,
              pFbInfo->win_par[Win].area_par[0].yact,
              pFbInfo->win_par[Win].area_par[0].xpos,
              pFbInfo->win_par[Win].area_par[0].ypos,
              pFbInfo->win_par[Win].area_par[0].xsize,
              pFbInfo->win_par[Win].area_par[0].ysize,
              pFbInfo->win_par[Win].area_par[0].xvir,
              pFbInfo->win_par[Win].area_par[0].yvir,
              pFbInfo->win_par[Win].area_par[0].data_format,
              pFbInfo->win_par[Win].area_par[0].acq_fence_fd,
              pFbInfo->win_par[Win].area_par[0].ion_fd,
              pFbInfo->win_par[Win].area_par[0].phy_addr);
    }

    return status;
OnError:
    LOGE("%s(%d):  Failed", __FUNCTION__, __LINE__);
    /* Error roll back. */

    return status;
}

