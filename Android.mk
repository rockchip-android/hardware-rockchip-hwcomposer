#

# rockchip hwcomposer( 2D graphic acceleration unit) .

#

#Copyright (C) 2015 Rockchip Electronics Co., Ltd.

#

LOCAL_PATH := $(call my-dir)
#include $(LOCAL_PATH)/../../Android.mk.def
BUILD_SECVM_LIB := false

ifeq ($(BUILD_SECVM_LIB),true)
include $(CLEAR_VARS)
LOCAL_PREBUILT_LIBS := libcodec_intel_sec.so
LOCAL_MODULE_TAGS := optional
include $(BUILD_MULTI_PREBUILT)
endif

#
# hwcomposer.default.so
#
include $(CLEAR_VARS)

$(info $(shell $(LOCAL_PATH)/version.sh))

LOCAL_SRC_FILES := \
	rk_hwcomposer.cpp \
	rk_hwcomposer_blit.cpp \
	rk_hwc_com.cpp \
	rga_api.cpp \
	rk_hwcomposer_htg.cpp \
	hwc_ipp.cpp \
	hwc_rga.cpp \
	TVInfo.cpp

LOCAL_CFLAGS := \
	$(CFLAGS) \
	-Wall \
	-Wextra \
	-DLOG_TAG=\"hwcomposer\"

ifeq ($(BUILD_SECVM_LIB),true)
LOCAL_CFLAGS += -DTARGET_SECVM
endif

LOCAL_C_INCLUDES := \
	$(AQROOT)/sdk/inc \
	$(AQROOT)/hal/inc

LOCAL_C_INCLUDES += hardware/rockchip/libgralloc_ump
LOCAL_C_INCLUDES += hardware/rockchip/libgralloc
LOCAL_C_INCLUDES += hardware/intel/libgralloc
LOCAL_C_INCLUDES += hardware/rockchip/librkvpu
LOCAL_C_INCLUDES += system/core/include/system
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
	libsync

#LOCAL_SHARED_LIBRARIES += \
	libvpu

ifeq ($(BUILD_SECVM_LIB),true)
LOCAL_SHARED_LIBRARIES += libcodec_intel_sec
endif

#LOCAL_C_INCLUDES := \
#	$(LOCAL_PATH)/inc

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk30xxb)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK30XXB
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk2928)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK29XX
endif
#LOCAL_CFLAGS += -DUSE_LCDC_COMPOSER

ifneq ($(filter rk312x rk3126c rk3128, $(TARGET_BOARD_PLATFORM)), )
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK312X
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),tablet)
	LOCAL_CFLAGS += -DRK312X_MID
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),box)
	LOCAL_CFLAGS += -DRK312X_BOX
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),phone)
	LOCAL_CFLAGS += -DRK312X_PHONE
endif
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk322x)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK322X
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),tablet)
	LOCAL_CFLAGS += -DRK322X_MID
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),box)
	LOCAL_CFLAGS += -DRK322X_BOX
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),phone)
	LOCAL_CFLAGS += -DRK322X_PHONE
endif
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk3188)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK3188
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),tablet)
	LOCAL_CFLAGS += -DRK3188_MID
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),box)
	LOCAL_CFLAGS += -DRK3188_BOX
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),phone)
	LOCAL_CFLAGS += -DRK3188_PHONE
endif
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk3036)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK3036
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),tablet)
        LOCAL_CFLAGS += -DRK3036_MID
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),box)
        LOCAL_CFLAGS += -DRK3036_BOX
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),phone)
        LOCAL_CFLAGS += -DRK3036_PHONE
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),dongle)
        LOCAL_CFLAGS += -DRK3036_DONGLE
endif
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM)),rk3328)
LOCAL_CFLAGS += -DTARGET_BOARD_PLATFORM_RK3328
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),tablet)
        LOCAL_CFLAGS += -DRK3328_MID
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),box)
        LOCAL_CFLAGS += -DRK3328_BOX
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),phone)
        LOCAL_CFLAGS += -DRK3328_PHONE
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),dongle)
        LOCAL_CFLAGS += -DRK3328_DONGLE
endif
endif

ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),tablet)
        LOCAL_CFLAGS += -DRK_MID
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),box)
        LOCAL_CFLAGS += -DRK_BOX
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),phone)
        LOCAL_CFLAGS += -DRK_PHONE
endif
ifeq ($(strip $(TARGET_BOARD_PLATFORM_PRODUCT)),dongle)
        LOCAL_CFLAGS += -DRK_DONGLE
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
LOCAL_CFLAGS += -DROCKCHIP_GPU_LIB_ENABLE
endif
ifeq ($(strip $(BOARD_USE_LAUNCHER2)),true)
LOCAL_CFLAGS += -DUSE_LAUNCHER2
endif

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_HARDWARE)
LOCAL_MODULE_TAGS    := optional

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 21 && echo OK),OK)
        LOCAL_MODULE_RELATIVE_PATH := hw
else
        LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
endif

LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)

