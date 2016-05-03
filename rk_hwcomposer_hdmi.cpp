
/*

* rockchip hwcomposer( 2D graphic acceleration unit) .

*

* Copyright (C) 2015 Rockchip Electronics Co., Ltd.

*/
#include <sys/prctl.h>
#include "rk_hwcomposer_hdmi.h"
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware_legacy/uevent.h>
#include <hardware/hwcomposer.h>

int         g_hdmi_mode;
void rk_parse_uevent_buf(const char *buf,int* type,int* flag,int* fbx, int len)
{
	const char *str = buf;
	while(*str){
        sscanf(str,"SCREEN=%d,ENABLE=%d",type,flag);
        sscanf(str,"FBDEV=%d",fbx);
        str += strlen(str) + 1;
        if (str - buf >= len){
            break;
        }
        //ALOGI("line %d,buf[%s]",__LINE__,str);
    }
    ALOGI("SCREEN=%d ENABLE=%d,",*type,*flag);

}

void rk_check_hdmi_state()
{
    //int fd = open("/sys/devices/virtual/switch/hdmi/state", O_RDONLY);
    int fd = open("/sys/devices/virtual/display/HDMI/connect", O_RDONLY);
    if (fd > 0)
    {
        char statebuf[100];
        memset(statebuf, 0, sizeof(statebuf));
        int err = read(fd, statebuf, sizeof(statebuf));

        if (err < 0)
        {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }
        close(fd);
        g_hdmi_mode = atoi(statebuf);
        /* if (g_hdmi_mode==0)
        {
            property_set("sys.hdmi.mode", "0");
        }
        else
        {
            property_set("sys.hdmi.mode", "1");
        }*/
    }
    else
    {
        ALOGD("err=%s", strerror(errno));
    }
}

//0,1,2
void rk_check_hdmi_uevents(const char *buf,int len)
{
    //ALOGI("fun[%s],line[%d],buf is [%s]",__FUNCTION__,__LINE__,buf);
#ifdef USE_X86
    if(!strcmp(buf, "change@/devices/soc0/e0000000.noc/ef010000.l2_noc/e1000000.ahb_per/vop0"))
#else
#ifdef RK322X_BOX
	if (strstr(buf, "change@/devices/vop"))
#else
    if (!strcmp(buf, "change@/devices/virtual/display/HDMI"))
#endif
#endif
    {
        int type,flag,fbx;
        type = flag = fbx = -1;
        rk_check_hdmi_state();
        rk_parse_uevent_buf(buf,&type,&flag,&fbx,len);
#ifdef USE_X86
        if(type == -1 && flag == -1)
            handle_hotplug_event(g_hdmi_mode,1);
        else
#endif
#ifdef RK322X_BOX
            handle_hotplug_event(flag,0);
#else
            handle_hotplug_event(g_hdmi_mode,0);
#endif
    }
}

void rk_handle_uevents(const char *buff,int len)
{
    // uint64_t timestamp = 0;
    rk_check_hdmi_uevents(buff,len);
}

void  *rk_hwc_hdmi_thread(void *arg)
{
    static char uevent_desc[4096];
    struct pollfd fds[1];
    int timeout;
    int err;
    prctl(PR_SET_NAME,"HWC_htg");
    HWC_UNREFERENCED_PARAMETER(arg);

    uevent_init();
    fds[0].fd = uevent_get_fd();
    fds[0].events = POLLIN;
    memset(uevent_desc, 0, sizeof(uevent_desc));
    do
    {
        err = poll(fds, 1, -1);
        if (err == -1)
        {
            if (errno != EINTR)
                ALOGE("event error: %m");
            continue;
        }

        if (fds[0].revents & POLLIN)
        {
            int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
            rk_handle_uevents(uevent_desc,len);
        }
    }
    while (1);

    pthread_exit(NULL);

    return NULL;
}
