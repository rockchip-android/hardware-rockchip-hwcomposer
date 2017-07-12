#include "hwc_util.h"
#include <cutils/log.h>
#include <cutils/properties.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static void sysfs_write(const char *path,const char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    ALOGV("%s: [%s: %s]", __FUNCTION__, path, s);
    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("%s: [%s: %s]", __FUNCTION__, path, s);
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("%s: [%s: %s]", __FUNCTION__, path, s);
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

/*
 * Control cpu performance mode.
 * Parameters:
 *  on:
 *      1: open performance
 *      0: close performance
 *  type:
 *      1: big cpu
 *      0: little cpu
 */
void ctl_cpu_performance(int on, int type)
{
    if((on != 0 && on != 1) ||
        (type != 0 && type != 1))
    {
        ALOGE("%s: invalid parameters,on=%d,type=%d", __FUNCTION__, on, type);
        return;
    }

    if(type)
    {
        sysfs_write(CPU_CLUST1_GOV_PATH, on ? "performance" : "interactive");
    }
    else
    {
        sysfs_write(CPU_CLUST0_GOV_PATH, on ? "performance" : "interactive");
    }
}

/*
 * Control little cpu.
 * Parameters:
 *  on:
 *      1: Enable little cpu
 *      0: Disable little cpu
 */
void ctl_little_cpu(int on)
{
    if(on != 0 && on != 1)
    {
        ALOGE("%s: invalid parameters,on=%d", __FUNCTION__, on);
        return;
    }

    sysfs_write("/sys/devices/system/cpu/cpu0/online", on ? "1" : "0");
    sysfs_write("/sys/devices/system/cpu/cpu1/online", on ? "1" : "0");
    sysfs_write("/sys/devices/system/cpu/cpu2/online", on ? "1" : "0");
    sysfs_write("/sys/devices/system/cpu/cpu3/online", on ? "1" : "0");
}

int hwc_get_int_property(const char* pcProperty,const char* default_value)
{
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;

    if(pcProperty == NULL || default_value == NULL)
    {
        ALOGE("hwc_get_int_property: invalid param");
        return -1;
    }

    property_get(pcProperty, value, default_value);
    new_value = atoi(value);

    return new_value;
}

int hwc_get_string_property(const char* pcProperty,const char* default_value,char* retult)
{
    if(pcProperty == NULL || default_value == NULL || retult == NULL)
    {
        ALOGE("hwc_get_string_property: invalid param");
        return -1;
    }

    property_get(pcProperty, retult, default_value);

    return 0;
}

static int CompareLines(int *da,int w)
{
    int i,j;
    for(i = 0;i<1;i++) // compare 4 lins
    {
        for(j= 0;j<w;j+=8)
        {
            if((unsigned int)*da != 0xff000000 && (unsigned int)*da != 0x0)
            {
                return 1;
            }
            da +=8;

        }
    }
    return 0;
}

int DetectValidData(int *data,int w,int h)
{
    int i,j;
    int *da;
    int ret;
    /*  detect model
    -------------------------
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    |   |   |    |    |      |
    |------------------------|
    |   |   |    |    |      |
    --------------------------
    */
    if(data == NULL)
        return 1;
    for(i = 2; i<h; i+= 8)
    {
        da = data +  i *w;
        if(CompareLines(da,w))
            return 1;
    }

    return 0;
}


