/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "hwc-vsync-worker"

#include "drmresources.h"
#include "hotplugworker.h"
#include "worker.h"

#include <map>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/log.h>
#include <hardware/hardware.h>

namespace android {

HotplugWorker::HotplugWorker()
    : Worker("hotplug", HAL_PRIORITY_URGENT_DISPLAY),
      drm_(NULL),
      procs_(NULL),
      display_(-1) {
}

HotplugWorker::~HotplugWorker() {
}

int HotplugWorker::Init(DrmResources *drm, int display) {

    uevent_init();
    fds[0].fd = uevent_get_fd();
    fds[0].events = POLLIN;

    drm_ = drm;
    display_ = display;

    return InitWorker();
}

int HotplugWorker::SetProcs(hwc_procs_t const *procs) {
  int ret = Lock();
  if (ret) {
    ALOGE("Failed to lock vsync worker lock %d\n", ret);
    return ret;
  }

  procs_ = procs;

  ret = Unlock();
  if (ret) {
    ALOGE("Failed to unlock vsync worker lock %d\n", ret);
    return ret;
  }
  return 0;
}

void HotplugWorker::Routine() {
  ALOGE("----------------------------HotplugWorker Routine start----------------------------");
    static char uevent_desc[4096];
  hwc_procs_t const *procs = procs_;
    int err;

    memset(uevent_desc, 0, sizeof(uevent_desc));
    err = poll(fds, 1, -1);
    if (err == -1) {
	    if (errno != EINTR)
		    ALOGE("event error: %m");
	    return;
    }

    if (fds[0].revents & POLLIN) {
	    const char *str = uevent_desc;
	    int hotplug = 0;
	    int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);
//	    ALOGE("[%s]",uevent_desc);

	    if (strcmp(uevent_desc, "change@/devices/platform/display-subsystem/drm/card0"))
		    return;
	    while(*str){
		    //ALOGI("SCREEN=%d ENABLE=%d,",*type,*flag);
	    ALOGE("[%s]",str);
		    str += strlen(str) + 1;
		    if (str - uevent_desc >= len){
			    break;
		    }

		    if (!strncmp(str, "HOTPLUG=", strlen("HOTPLUG="))) {
    			    int fd = open("/sys/class/drm/card0-HDMI-A-1/status", O_RDONLY);
			    if (fd > 0){
				    char statebuf[20];
				    memset(statebuf, 0, sizeof(statebuf));
				    int err = read(fd, statebuf, sizeof(statebuf));
				    if (err < 0)
				    {
					    ALOGE("error reading hdmi state: %s", strerror(errno));
					    return;
				    }
				    close(fd);
				    if (!strncmp(statebuf, "connected", sizeof(connected)))
					    hotplug = 1; 
				    else
					    hotplug = 0;
				    ALOGE("--->statebuf=%s cmp=%d\n", statebuf, strncmp(statebuf, "connected", sizeof(connected)));
			    }else{
				    ALOGD("err=%s",strerror(errno));
			    }
#if 1
			    ALOGE("-->yzq hotplug=%d\n", hotplug);
			    if (procs && procs->hotplug)
				    procs->hotplug(procs, 1, hotplug);
#endif
		    }
	    }
    }

  ALOGE("----------------------------HotplugWorker Routine end----------------------------");
}
}
