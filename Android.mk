##############################################################################
#
#    Copyright (c) 2005 - 2011 by Vivante Corp.  All rights reserved.
#
#    The material in this file is confidential and contains trade secrets
#    of Vivante Corporation. This is proprietary information owned by
#    Vivante Corporation. No part of this work may be disclosed,
#    reproduced, copied, transmitted, or used in any way for any purpose,
#    without the express written permission of Vivante Corporation.
#
##############################################################################
#
#    Auto-generated file on 12/13/2011. Do not edit!!!
#
##############################################################################
LOCAL_PATH := $(call my-dir)
#include $(LOCAL_PATH)/../../Android.mk.def

#
# hwcomposer.default.so
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	rk_hwcomposer.cpp \
	rk_hwcomposer_blit.cpp \
	rk_hwcomposer_buffer.cpp \
	rga_api.cpp \
	rk_hwcomposer_hdmi.cpp \
	hwc_ipp.cpp \
	hwc_rga.cpp 

LOCAL_CFLAGS := \
	$(CFLAGS) \
	-Wall \
	-Wextra \
	-DLOG_TAG=\"hwcomposer\"

LOCAL_C_INCLUDES := \
	$(AQROOT)/sdk/inc \
	$(AQROOT)/hal/inc

LOCAL_C_INCLUDES += hardware/rockchip/libgralloc_ump
LOCAL_C_INCLUDES += hardware/rockchip/libgralloc
LOCAL_C_INCLUDES += hardware/intel/libgralloc
LOCAL_LDFLAGS := \
	-Wl,-z,defs \
	-Wl,--version-script=$(LOCAL_PATH)/hwcomposer.map

LOCAL_SHARED_LIBRARIES := \
	libhardware \
	liblog \
	libui \
	libEGL \
	libcutils \
	libion \
	libhardware_legacy \
	libsync



#LOCAL_C_INCLUDES := \
#	$(LOCAL_PATH)/inc

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk30xxb)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK30XXB
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk2928)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK29XX
endif
#LOCAL_CFLAGS += -DUSE_LCDC_COMPOSER

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk312x)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK312X
endif

ifeq ($(strip $(BOARD_USE_LCDC_COMPOSER)),true)
LOCAL_CFLAGS += -DUSE_LCDC_COMPOSER
ifeq ($(strip $(BOARD_LCDC_COMPOSER_LANDSCAPE_ONLY)),false)
LOCAL_CFLAGS += -DLCDC_COMPOSER_FULL_ANGLE
endif
endif

#ifeq ($(strip $(GRAPHIC_MEMORY_PROVIDER)),dma_buf)
 LOCAL_CFLAGS += -DUSE_DMA_BUF
#endif

ifeq ($(strip $(BOARD_USE_LAUNCHER2)),true)
LOCAL_CFLAGS += -DUSE_LAUNCHER2
endif

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_HARDWARE)
LOCAL_MODULE_TAGS    := optional
LOCAL_MODULE_PATH    := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)

