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
#include <string.h>


int         g_hdmi_mode;
int         mUsedVopNum;
int rk_parse_uevent_buf(const char *buf,int* type,int* flag,int* fbx, int* vopId, int len)
{
    int ret = -1;
	const char *str = buf;
    //ALOGD("[%s]",buf);

	while(*str) {
	    if(!strcmp(str,"switch screen")) ret = 1;
		sscanf(str,"SCREEN=%d,ENABLE=%d",type,flag);
		sscanf(str,"FBDEV=%d",fbx);
		if(ret==1) ALOGI("SCREEN=%d ENABLE=%d,vopId=%d",*type,*flag,*vopId);
        str += strlen(str) + 1;
        if (str - buf >= len){
            break;
        }
	    if(ret==1) ALOGI("line %d,buf[%s]",__LINE__,str);
    }
    return ret;
}

int rk_check_hdmi_state()
{
    int ret = 0;
    int fd = open("/sys/devices/virtual/display/HDMI/connect", O_RDONLY);
	if (fd > 0) {
		char statebuf[100];
		memset(statebuf, 0, sizeof(statebuf));
		int err = read(fd, statebuf, sizeof(statebuf));
		if (err < 0) {
		    close(fd);
		    ALOGE("error reading hdmi state: %s", strerror(errno));
		    return -errno;
		}
		close(fd);
		ret = atoi(statebuf);
		ALOGI("Read HDMI connect state = %d");
	} else {
		ALOGD("err=%s",strerror(errno));
	}
	return ret;
}

void rk_check_hdmi_uevents(const char *buf,int len)
{
    int ret = 0;
    int vopId = -1;
    int fbIndex = 0;
    int screenType = 0;
    int statusFlag = 0;
    int hdmiStatus = -1;
    ret = rk_parse_uevent_buf(buf,&screenType,&statusFlag,&fbIndex,&vopId,len);
    if(ret != 1) return;
    g_hdmi_mode = rk_check_hdmi_state();

    if (fbIndex == 0)
        hotplug_change_screen_config(0, fbIndex, statusFlag);
    else
        handle_hotplug_event(statusFlag, screenType == 6 ? 6 : 1);

    ALOGI("uevent receive!type=%d,status=%d,hdmi=%d,vop=%d,fb=%d,line=%d",
                screenType, statusFlag, g_hdmi_mode, vopId, fbIndex, __LINE__);

    return;
}

void rk_handle_uevents(const char *buff,int len)
{
	// uint64_t timestamp = 0;
    rk_check_hdmi_uevents(buff,len);
}

void  *rk_hwc_hdmi_thread(void *arg)
{
    prctl(PR_SET_NAME,"HWC_Uevent");
    static char uevent_desc[4096];
    struct pollfd fds[1];
    int timeout;
    int err;
    uevent_init();
    fds[0].fd = uevent_get_fd();
    fds[0].events = POLLIN;
    timeout = 200;//ms
    memset(uevent_desc, 0, sizeof(uevent_desc));
    do {
        err = poll(fds, 1, timeout);
        if (err == -1) {
            if (errno != EINTR)
                ALOGE("event error: %m");
            continue;
        }

        if (fds[0].revents & POLLIN) {
            int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
            rk_handle_uevents(uevent_desc,len);
        }
    } while (1);

    pthread_exit(NULL);

    return NULL;
}

