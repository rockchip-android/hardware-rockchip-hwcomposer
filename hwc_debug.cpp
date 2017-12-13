#define LOG_TAG "hwc_debug"

#include "hwc_debug.h"
#include "hwc_rockchip.h"
#include <sstream>

namespace android {

unsigned int g_log_level;
unsigned int g_frame;

void inc_frame()
{
    g_frame++;
}

void dec_frame()
{
    g_frame--;
}

int get_frame()
{
    return g_frame;
}

int init_log_level()
{
    char value[PROPERTY_VALUE_MAX];
    int iValue;
    property_get("sys.hwc.log", value, "0");
    g_log_level = atoi(value);
    return 0;
}

bool log_level(LOG_LEVEL log_level)
{
    return g_log_level & log_level;
}

void init_rk_debug()
{
  g_log_level = 0;
  g_frame = 0;
  init_log_level();
}

/**
 * @brief Dump Layer data.
 *
 * @param layer_index   layer index
 * @param layer 		layer data
 * @return 				Errno no
 */
#define DUMP_LAYER_CNT (10)
static int DumpSurfaceCount = 0;
int DumpLayer(const char* layer_name,buffer_handle_t handle)
{
    char pro_value[PROPERTY_VALUE_MAX];

    property_get("sys.dump",pro_value,0);

    if(handle && !strcmp(pro_value,"true"))
    {
        FILE * pfile = NULL;
        char data_name[100] ;
        const gralloc_module_t *gralloc;
        void* cpu_addr;
        int i;
        int width,height,stride,byte_stride,format,size;

        int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc);
        if (ret) {
            ALOGE("Failed to open gralloc module");
            return ret;
        }
#if RK_DRM_GRALLOC
        width = hwc_get_handle_attibute(gralloc,handle,ATT_WIDTH);
        height = hwc_get_handle_attibute(gralloc,handle,ATT_HEIGHT);
        stride = hwc_get_handle_attibute(gralloc,handle,ATT_STRIDE);
        byte_stride = hwc_get_handle_attibute(gralloc,handle,ATT_BYTE_STRIDE);
        format = hwc_get_handle_attibute(gralloc,handle,ATT_FORMAT);
        size = hwc_get_handle_attibute(gralloc,handle,ATT_SIZE);
#else
        width = hwc_get_handle_width(gralloc,handle);
        height = hwc_get_handle_height(gralloc,handle);
        stride = hwc_get_handle_stride(gralloc,handle);
        byte_stride = hwc_get_handle_byte_stride(gralloc,handle);
        format = hwc_get_handle_format(gralloc,handle);
        size = hwc_get_handle_size(gralloc,handle);
#endif
        system("mkdir /data/dump/ && chmod /data/dump/ 777 ");
        DumpSurfaceCount++;
        sprintf(data_name,"/data/dump/dmlayer%d_%d_%d.bin", DumpSurfaceCount,
                stride,height);
        gralloc->lock(gralloc, handle, GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK, //gr_handle->usage,
                        0, 0, width, height, (void **)&cpu_addr);
        pfile = fopen(data_name,"wb");
        if(pfile)
        {
            fwrite((const void *)cpu_addr,(size_t)(size),1,pfile);
            fflush(pfile);
            fclose(pfile);
            ALOGD(" dump surface layer_name: %s,data_name %s,w:%d,h:%d,stride :%d,size=%d,cpu_addr=%p",
                layer_name,data_name,width,height,byte_stride,size,cpu_addr);
        }
        else
        {
            ALOGE("Open %s fail", data_name);
            ALOGD(" dump surface layer_name: %s,data_name %s,w:%d,h:%d,stride :%d,size=%d,cpu_addr=%p",
                layer_name,data_name,width,height,byte_stride,size,cpu_addr);
        }
        gralloc->unlock(gralloc, handle);
        //only dump once time.
        if(DumpSurfaceCount > DUMP_LAYER_CNT)
        {
            DumpSurfaceCount = 0;
            property_set("sys.dump","0");
        }
    }
    return 0;
}

static unsigned int HWC_Clockms(void)
{
	struct timespec t = { .tv_sec = 0, .tv_nsec = 0 };
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

void hwc_dump_fps(void)
{
	static unsigned int n_frames = 0;
	static unsigned int lastTime = 0;

	++n_frames;

	if (property_get_bool("sys.hwc.fps", 0))
	{
		unsigned int time = HWC_Clockms();
		unsigned int intv = time - lastTime;
		if (intv >= HWC_DEBUG_FPS_INTERVAL_MS)
		{
			unsigned int fps = n_frames * 1000 / intv;
			ALOGD_IF(log_level(DBG_DEBUG),"fps %u", fps);

			n_frames = 0;
			lastTime = time;
		}
	}
}

void dump_layer(const gralloc_module_t *gralloc, bool bDump, hwc_layer_1_t *layer, int index) {
    size_t i;
  std::ostringstream out;
  int format;
  char layername[100];

  if (!log_level(DBG_VERBOSE))
    return;

    if(layer->flags & HWC_SKIP_LAYER)
    {
        ALOGD_IF(log_level(DBG_VERBOSE),"layer %p skipped", layer);
    }
    else
    {
        if(layer->handle)
        {
#if RK_DRM_GRALLOC
            format = hwc_get_handle_attibute(gralloc, layer->handle, ATT_FORMAT);
#else
            format = hwc_get_handle_format(gralloc, layer->handle);
#endif

#ifdef USE_HWC2
                hwc_get_handle_layername(gralloc, layer->handle, layername, 100);
                out << "layer[" << index << "]=" << layername
#else
                out << "layer[" << index << "]=" << layer->LayerName
#endif
                << "\n\tlayer=" << layer
                << ",type=" << layer->compositionType
                << ",hints=" << layer->compositionType
                << ",flags=" << layer->flags
                << ",handle=" << layer->handle
                << ",format=0x" << std::hex << format
                << ",fd =" << std::dec << hwc_get_handle_primefd(gralloc, layer->handle)
                << ",transform=0x" <<  std::hex << layer->transform
                << ",blend=0x" << layer->blending
                << ",sourceCropf{" << std::dec
                    << layer->sourceCropf.left << "," << layer->sourceCropf.top << ","
                    << layer->sourceCropf.right << "," << layer->sourceCropf.bottom
                << "},sourceCrop{"
                    << layer->sourceCrop.left << ","
                    << layer->sourceCrop.top << ","
                    << layer->sourceCrop.right << ","
                    << layer->sourceCrop.bottom
                << "},displayFrame{"
                    << layer->displayFrame.left << ","
                    << layer->displayFrame.top << ","
                    << layer->displayFrame.right << ","
                    << layer->displayFrame.bottom << "},";
        }
        else
        {
            out << "layer[" << index << "]=" << layer->LayerName
                << "\n\tlayer=" << layer
                << ",type=" << layer->compositionType
                << ",hints=" << layer->compositionType
                << ",flags=" << layer->flags
                << ",handle=" << layer->handle
                << ",transform=0x" <<  std::hex << layer->transform
                << ",blend=0x" << layer->blending
                << ",sourceCropf{" << std::dec
                    << layer->sourceCropf.left << "," << layer->sourceCropf.top << ","
                    << layer->sourceCropf.right << "," << layer->sourceCropf.bottom
                << "},sourceCrop{"
                    << layer->sourceCrop.left << ","
                    << layer->sourceCrop.top << ","
                    << layer->sourceCrop.right << ","
                    << layer->sourceCrop.bottom
                << "},displayFrame{"
                    << layer->displayFrame.left << ","
                    << layer->displayFrame.top << ","
                    << layer->displayFrame.right << ","
                    << layer->displayFrame.bottom << "},";
        }
        for (i = 0; i < layer->visibleRegionScreen.numRects; i++)
        {
            out << "rect[" << i << "]={"
                << layer->visibleRegionScreen.rects[i].left << ","
                << layer->visibleRegionScreen.rects[i].top << ","
                << layer->visibleRegionScreen.rects[i].right << ","
                << layer->visibleRegionScreen.rects[i].bottom << "},";
        }
        out << "\n";
        ALOGD_IF(log_level(DBG_VERBOSE) || bDump,"%s",out.str().c_str());
    }
}

}
