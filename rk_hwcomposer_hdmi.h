#define MODE_CHANGE_HDMI 1
#define MODE_ADD_HDMI 2
#define MODE_REMOVE_HDMI 3

void      rk_check_hdmi_uevents(const char *buf);
void      rk_handle_uevents(const char *buff);
void     *rk_hwc_hdmi_thread(void *arg);
extern  int         g_hdmi_mode;


