#

# rockchip hwcomposer( 2D graphic acceleration unit) .

#

#Copyright (C) 2015 Rockchip Electronics Co., Ltd.

#

LOCAL_PATH := $(call my-dir)
#include $(LOCAL_PATH)/../../Android.mk.def

#
# hwcomposer.default.so
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	rk_hwcomposer.cpp \
	rk_hwcomposer_blit.cpp \
	rk_hwc_com.cpp \
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
LOCAL_C_INCLUDES += hardware/rockchip/librkvpu
LOCAL_LDFLAGS := \
	-Wl,-z,defs 

LOCAL_SHARED_LIBRARIES := \
	libhardware \
	liblog \
	libui \
	libEGL \
	libcutils \
	libion \
	libhardware_legacy \
	libsync \
	libvpu



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

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),sofia3gr)
LOCAL_CFLAGS += -DUSE_X86
endif
ifeq ($(strip $(BOARD_USE_LAUNCHER2)),true)
LOCAL_CFLAGS += -DUSE_LAUNCHER2
endif

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_HARDWARE)
LOCAL_MODULE_TAGS    := optional
LOCAL_MODULE_PATH    := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)

