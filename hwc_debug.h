#ifndef _HWC_DEBUG_H_
#define _HWC_DEBUG_H_

#include <stdlib.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/hwcomposer.h>
#if RK_DRM_GRALLOC
#include "gralloc_drm_handle.h"
#endif

namespace android {

enum LOG_LEVEL
{
    //Log level flag
    /*1*/
    DBG_VERBOSE = 1 << 0,
    /*2*/
    DBG_DEBUG = 1 << 1,
    /*4*/
    DBG_INFO = 1 << 2,
    /*8*/
    DBG_WARN = 1 << 3,
    /*16*/
    DBG_ERROR = 1 << 4,
    /*32*/
    DBG_FETAL = 1 << 5,
    /*64*/
    DBG_SILENT = 1 << 6,
};

bool log_level(LOG_LEVEL log_level);

/* interval ms of print fps.*/
#define HWC_DEBUG_FPS_INTERVAL_MS 1

/* print time macros. */
#define PRINT_TIME_START        \
    struct timeval tpend1, tpend2;\
    long usec1 = 0;\
    gettimeofday(&tpend1,NULL);\

#define PRINT_TIME_END(tag)        \
    gettimeofday(&tpend2,NULL);\
    usec1 = 1000*(tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec- tpend1.tv_usec)/1000;\
    if (property_get_bool("sys.hwc.time", 0)) \
    ALOGD_IF(1,"%s use time=%ld ms",tag,usec1);


void inc_frame();
void dec_frame();
int get_frame();
int init_log_level();
bool log_level(LOG_LEVEL log_level);
void init_rk_debug();
int DumpLayer(const char* layer_name,buffer_handle_t handle);
void hwc_dump_fps(void);
void dump_layer(const gralloc_module_t *gralloc, bool bDump, hwc_layer_1_t *layer, int index);
}

#endif

