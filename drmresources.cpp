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

#define LOG_TAG "hwc-drm-resources"

#include "drmconnector.h"
#include "drmcrtc.h"
#include "drmencoder.h"
#include "drmplane.h"
#include "drmresources.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <drm_fourcc.h>

namespace android {

DrmResources::DrmResources() : compositor_(this) {
}

#if RK_DRM_HWC

bool PlaneSortByZpos(const DrmPlane* plane1,const DrmPlane* plane2)
{
    uint64_t zpos1,zpos2;
    plane1->zpos_property().value(&zpos1);
    plane2->zpos_property().value(&zpos2);
    return zpos1 < zpos2;
}

bool SortByZpos(const PlaneGroup* planeGroup1,const PlaneGroup* planeGroup2)
{
    return planeGroup1->zpos < planeGroup2->zpos;
}
#endif

int DrmResources::Init() {
  char path[PROPERTY_VALUE_MAX];
  property_get("hwc.drm.device", path, "/dev/dri/card0");

  /* TODO: Use drmOpenControl here instead */
  fd_.Set(open(path, O_RDWR));
  if (fd() < 0) {
    ALOGE("Failed to open dri- %s", strerror(-errno));
    return -ENODEV;
  }

  int ret = drmSetClientCap(fd(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret) {
    ALOGE("Failed to set universal plane cap %d", ret);
    return ret;
  }

  ret = drmSetClientCap(fd(), DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    ALOGE("Failed to set atomic cap %d", ret);
    return ret;
  }

  //Open Multi-area support.
  ret = drmSetClientCap(fd(), DRM_CLIENT_CAP_SHARE_PLANES, 1);
  if (ret) {
    ALOGE("Failed to set share planes %d", ret);
    return ret;
  }

  drmModeResPtr res = drmModeGetResources(fd());
  if (!res) {
    ALOGE("Failed to get DrmResources resources");
    return -ENODEV;
  }

  bool found_primary = false;
  int display_num = 1;

#if RK_DRM_HWC_DEBUG
    std::ostringstream out;
    out << "Frame buffers:\n";
    out << "id\tsize\tpitch\n";
    for (int i = 0; !ret && i < res->count_fbs; ++i) {
        drmModeFBPtr fb = drmModeGetFB(fd(), res->fbs[i]);
        if (!fb) {
          ALOGE("Failed to get FB %d", res->fbs[i]);
          ret = -ENODEV;
          break;
        }

        out << fb->fb_id << "\t("
            << fb->width << "x"
            << fb->height << ")\t"
            << fb->pitch << "\n";

        drmModeFreeFB(fb);
    }

  ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
  out.str("");
#endif

#if RK_DRM_HWC_DEBUG
  out << "CRTCs:\n";
  out << "id\tfb\tpos\tsize\n";
#endif
  for (int i = 0; !ret && i < res->count_crtcs; ++i) {
    drmModeCrtcPtr c = drmModeGetCrtc(fd(), res->crtcs[i]);
    if (!c) {
      ALOGE("Failed to get crtc %d", res->crtcs[i]);
      ret = -ENODEV;
      break;
    }

    std::unique_ptr<DrmCrtc> crtc(new DrmCrtc(this, c, i));

#if RK_DRM_HWC_DEBUG
    crtc->dump_crtc(&out);
    out << "\n";
#endif

    drmModeFreeCrtc(c);

    ret = crtc->Init();
    if (ret) {
      ALOGE("Failed to initialize crtc %d", res->crtcs[i]);
      break;
    }
    crtcs_.emplace_back(std::move(crtc));
  }

#if RK_DRM_HWC_DEBUG
  ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
  out.str("");
#endif

#if RK_DRM_HWC_DEBUG
  out << "Encoders:\n";
  out << "id\tcrtc\ttype\tpossible crtcs\tpossible clones\t\n";
#endif
  for (int i = 0; !ret && i < res->count_encoders; ++i) {
    drmModeEncoderPtr e = drmModeGetEncoder(fd(), res->encoders[i]);
    if (!e) {
      ALOGE("Failed to get encoder %d", res->encoders[i]);
      ret = -ENODEV;
      break;
    }

    std::vector<DrmCrtc *> possible_crtcs;
    DrmCrtc *current_crtc = NULL;
    for (auto &crtc : crtcs_) {
      if ((1 << crtc->pipe()) & e->possible_crtcs)
        possible_crtcs.push_back(crtc.get());

      if (crtc->id() == e->crtc_id)
        current_crtc = crtc.get();
    }

    std::unique_ptr<DrmEncoder> enc(
        new DrmEncoder(this, e, current_crtc, possible_crtcs));

#if RK_DRM_HWC_DEBUG
    enc->dump_encoder(&out);
    out << "\n";
#endif

    drmModeFreeEncoder(e);

    encoders_.emplace_back(std::move(enc));
  }
#if RK_DRM_HWC_DEBUG
  ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
  out.str("");
#endif


#if RK_DRM_HWC_DEBUG
  out << "Connectors:\n";
  out << "id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\tencoders\n";
#endif
  for (int i = 0; !ret && i < res->count_connectors; ++i) {
    drmModeConnectorPtr c = drmModeGetConnector(fd(), res->connectors[i]);
    if (!c) {
      ALOGE("Failed to get connector %d", res->connectors[i]);
      ret = -ENODEV;
      break;
    }

    std::vector<DrmEncoder *> possible_encoders;
    DrmEncoder *current_encoder = NULL;
    for (int j = 0; j < c->count_encoders; ++j) {
      for (auto &encoder : encoders_) {
        if (encoder->id() == c->encoders[j])
          possible_encoders.push_back(encoder.get());
        if (encoder->id() == c->encoder_id)
          current_encoder = encoder.get();
      }
    }

    std::unique_ptr<DrmConnector> conn(
        new DrmConnector(this, c, current_encoder, possible_encoders));

#if RK_DRM_HWC_DEBUG
    conn->dump_connector(&out);
    out << "\n";
#endif

    drmModeFreeConnector(c);

    ret = conn->Init();
    if (ret) {
      ALOGE("Init connector %d failed", res->connectors[i]);
      break;
    }

    if (conn->built_in() && !found_primary) {
      conn->set_display(0);
      found_primary = true;
    } else {
      conn->set_display(display_num);
      ++display_num;
    }

    connectors_.emplace_back(std::move(conn));
  }

#if RK_DRM_HWC_DEBUG
  ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
  out.str("");
#endif

  if (res)
    drmModeFreeResources(res);

  // Catch-all for the above loops
  if (ret)
    return ret;

  drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd());
  if (!plane_res) {
    ALOGE("Failed to get plane resources");
    return -ENOENT;
  }

#if RK_DRM_HWC_DEBUG
  out << "Planes:\n";
  out << "id\tcrtc\tfb\tCRTC x,y\tx,y\tgamma size\tpossible crtcs\n";
#endif

  for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
    drmModePlanePtr p = drmModeGetPlane(fd(), plane_res->planes[i]);
    if (!p) {
      ALOGE("Failed to get plane %d", plane_res->planes[i]);
      ret = -ENODEV;
      break;
    }

    std::unique_ptr<DrmPlane> plane(new DrmPlane(this, p));

#if RK_DRM_HWC_DEBUG
    plane->dump_plane(&out);
    out << "\n";
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
    out.str("");
#endif

    ret = plane->Init();
    if (ret) {
      ALOGE("Init plane %d failed", plane_res->planes[i]);
      break;
    }
#if RK_DRM_HWC
    uint64_t share_id,zpos;
    plane->share_id_property().value(&share_id);
    plane->zpos_property().value(&zpos);
    std::vector<PlaneGroup *> ::const_iterator iter;
    for (iter = plane_groups_.begin();
       iter != plane_groups_.end(); ++iter)
    {
        if((*iter)->share_id == share_id /*&& (*iter)->zpos == zpos*/)
        {
            (*iter)->planes.push_back(plane.get());
            break;
        }
    }
    if(iter == plane_groups_.end())
    {
        PlaneGroup* plane_group = new PlaneGroup();
        plane_group->bUse= false;
        plane_group->zpos = zpos;
        plane_group->share_id = share_id;
        plane_group->planes.push_back(plane.get());
        plane_groups_.push_back(plane_group);
    }

       for (uint32_t j = 0; j < p->count_formats; j++) {
               if (p->formats[j] == DRM_FORMAT_NV12 ||
                   p->formats[j] == DRM_FORMAT_NV21) {
                       plane->set_yuv(true);
               }
    }
    sort_planes_.emplace_back(plane.get());
#endif
    drmModeFreePlane(p);

    planes_.emplace_back(std::move(plane));
  }

#if RK_DRM_HWC
  std::sort(sort_planes_.begin(),sort_planes_.end(),PlaneSortByZpos);
#endif
#if RK_DRM_HWC & RK_DRM_HWC_DEBUG
    for (std::vector<DrmPlane*>::const_iterator iter= sort_planes_.begin();
       iter != sort_planes_.end(); ++iter) {
       uint64_t share_id,zpos;
       (*iter)->share_id_property().value(&share_id);
       (*iter)->zpos_property().value(&zpos);
       ALOGD_IF(log_level(DBG_VERBOSE),"sort_planes_ share_id=%d,zpos=%d",share_id,zpos);
    }

    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups_.begin();
           iter != plane_groups_.end(); ++iter)
    {
        ALOGD_IF(log_level(DBG_VERBOSE),"Plane groups: zpos=%d,share_id=%d,plane size=%d",
            (*iter)->zpos,(*iter)->share_id,(*iter)->planes.size());
        for(std::vector<DrmPlane*> ::const_iterator iter_plane = (*iter)->planes.begin();
           iter_plane != (*iter)->planes.end(); ++iter_plane)
        {
            ALOGD_IF(log_level(DBG_VERBOSE),"\tPlane id=%d",(*iter_plane)->id());
        }
    }
    ALOGD_IF(log_level(DBG_VERBOSE),"--------------------sort plane--------------------");
#endif
#if RK_DRM_HWC
    std::sort(plane_groups_.begin(),plane_groups_.end(),SortByZpos);
#endif
#if RK_DRM_HWC & RK_DRM_HWC_DEBUG
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups_.begin();
           iter != plane_groups_.end(); ++iter)
    {
        ALOGD_IF(log_level(DBG_VERBOSE),"Plane groups: zpos=%d,share_id=%d,plane size=%d",
            (*iter)->zpos,(*iter)->share_id,(*iter)->planes.size());
        for(std::vector<DrmPlane*> ::const_iterator iter_plane = (*iter)->planes.begin();
           iter_plane != (*iter)->planes.end(); ++iter_plane)
        {
            ALOGD_IF(log_level(DBG_VERBOSE),"\tPlane id=%d",(*iter_plane)->id());
        }
    }
#endif

  drmModeFreePlaneResources(plane_res);
  if (ret)
    return ret;

  ret = compositor_.Init();
  if (ret)
    return ret;

  for (auto &conn : connectors_) {
    ret = CreateDisplayPipe(conn.get());
    if (ret) {
      ALOGE("Failed CreateDisplayPipe %d with %d", conn->id(), ret);
      return ret;
    }
  }
  return 0;
}

DrmConnector *DrmResources::GetConnectorForDisplay(int display) const {
  for (auto &conn : connectors_) {
    if (conn->display() == display)
      return conn.get();
  }
  return NULL;
}

DrmCrtc *DrmResources::GetCrtcForDisplay(int display) const {
  for (auto &crtc : crtcs_) {
    if (crtc->display() == display)
      return crtc.get();
  }
  return NULL;
}

DrmPlane *DrmResources::GetPlane(uint32_t id) const {
  for (auto &plane : planes_) {
    if (plane->id() == id)
      return plane.get();
  }
  return NULL;
}

uint32_t DrmResources::next_mode_id() {
  return ++mode_id_;
}

int DrmResources::TryEncoderForDisplay(int display, DrmEncoder *enc) {
  /* First try to use the currently-bound crtc */
  DrmCrtc *crtc = enc->crtc();
  if (crtc && crtc->can_bind(display)) {
    crtc->set_display(display);
    return 0;
  }

  /* Try to find a possible crtc which will work */
  for (DrmCrtc *crtc : enc->possible_crtcs()) {
    /* We've already tried this earlier */
    if (crtc == enc->crtc())
      continue;

    if (crtc->can_bind(display)) {
      enc->set_crtc(crtc);
      crtc->set_display(display);
      return 0;
    }
  }

  /* We can't use the encoder, but nothing went wrong, try another one */
  return -EAGAIN;
}

int DrmResources::CreateDisplayPipe(DrmConnector *connector) {
  int display = connector->display();
  /* Try to use current setup first */
  if (connector->encoder()) {
    int ret = TryEncoderForDisplay(display, connector->encoder());
    if (!ret) {
      return 0;
    } else if (ret != -EAGAIN) {
      ALOGE("Could not set mode %d/%d", display, ret);
      return ret;
    }
  }

  for (DrmEncoder *enc : connector->possible_encoders()) {
    int ret = TryEncoderForDisplay(display, enc);
    if (!ret) {
      connector->set_encoder(enc);
      return 0;
    } else if (ret != -EAGAIN) {
      ALOGE("Could not set mode %d/%d", display, ret);
      return ret;
    }
  }
  ALOGE("Could not find a suitable encoder/crtc for display %d",
        connector->display());
  return -ENODEV;
}

int DrmResources::CreatePropertyBlob(void *data, size_t length,
                                     uint32_t *blob_id) {
  struct drm_mode_create_blob create_blob;
  memset(&create_blob, 0, sizeof(create_blob));
  create_blob.length = length;
  create_blob.data = (__u64)data;

  int ret = drmIoctl(fd(), DRM_IOCTL_MODE_CREATEPROPBLOB, &create_blob);
  if (ret) {
    ALOGE("Failed to create mode property blob %d", ret);
    return ret;
  }
  *blob_id = create_blob.blob_id;
  return 0;
}

int DrmResources::DestroyPropertyBlob(uint32_t blob_id) {
  if (!blob_id)
    return 0;

  struct drm_mode_destroy_blob destroy_blob;
  memset(&destroy_blob, 0, sizeof(destroy_blob));
  destroy_blob.blob_id = (__u32)blob_id;
  int ret = drmIoctl(fd(), DRM_IOCTL_MODE_DESTROYPROPBLOB, &destroy_blob);
  if (ret) {
    ALOGE("Failed to destroy mode property blob %ld/%d", blob_id, ret);
    return ret;
  }
  return 0;
}

int DrmResources::SetDisplayActiveMode(int display, const DrmMode &mode) {
  std::unique_ptr<DrmComposition> comp(compositor_.CreateComposition(NULL));
  if (!comp) {
    ALOGE("Failed to create composition for dpms on %d", display);
    return -ENOMEM;
  }
  int ret = comp->SetDisplayMode(display, mode);
  if (ret) {
    ALOGE("Failed to add mode to composition on %d %d", display, ret);
    return ret;
  }
  ret = compositor_.QueueComposition(std::move(comp));
  if (ret) {
    ALOGE("Failed to queue dpms composition on %d %d", display, ret);
    return ret;
  }
  return 0;
}

int DrmResources::SetDpmsMode(int display, uint64_t mode) {
  if (mode != DRM_MODE_DPMS_ON && mode != DRM_MODE_DPMS_OFF) {
    ALOGE("Invalid dpms mode %d", mode);
    return -EINVAL;
  }

  std::unique_ptr<DrmComposition> comp(compositor_.CreateComposition(NULL));
  if (!comp) {
    ALOGE("Failed to create composition for dpms on %d", display);
    return -ENOMEM;
  }
  int ret = comp->SetDpmsMode(display, mode);
  if (ret) {
    ALOGE("Failed to add dpms %ld to composition on %d %d", mode, display, ret);
    return ret;
  }
  ret = compositor_.QueueComposition(std::move(comp));
  if (ret) {
    ALOGE("Failed to queue dpms composition on %d %d", display, ret);
    return ret;
  }
  return 0;
}

DrmCompositor *DrmResources::compositor() {
  return &compositor_;
}

int DrmResources::GetProperty(uint32_t obj_id, uint32_t obj_type,
                              const char *prop_name, DrmProperty *property) {
  drmModeObjectPropertiesPtr props;

  props = drmModeObjectGetProperties(fd(), obj_id, obj_type);
  if (!props) {
    ALOGE("Failed to get properties for %d/%x", obj_id, obj_type);
    return -ENODEV;
  }

  bool found = false;
  for (int i = 0; !found && (size_t)i < props->count_props; ++i) {
    drmModePropertyPtr p = drmModeGetProperty(fd(), props->props[i]);
    if (!strcmp(p->name, prop_name)) {
      property->Init(p, props->prop_values[i]);
      found = true;
    }
    drmModeFreeProperty(p);
  }

  drmModeFreeObjectProperties(props);
  return found ? 0 : -ENOENT;
}

#if RK_DRM_HWC_DEBUG
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
static inline int64_t U642I64(uint64_t val)
{
	return (int64_t)*((int64_t *)&val);
}

struct type_name {
	int type;
	const char *name;
};

#define type_name_fn(res) \
const char * DrmResources::res##_str(int type) {			\
	unsigned int i;					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) { \
		if (res##_names[i].type == type)	\
			return res##_names[i].name;	\
	}						\
	return "(invalid)";				\
}

struct type_name encoder_type_names[] = {
	{ DRM_MODE_ENCODER_NONE, "none" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TVDAC" },
};

type_name_fn(encoder_type)

struct type_name connector_status_names[] = {
	{ DRM_MODE_CONNECTED, "connected" },
	{ DRM_MODE_DISCONNECTED, "disconnected" },
	{ DRM_MODE_UNKNOWNCONNECTION, "unknown" },
};

type_name_fn(connector_status)

struct type_name connector_type_names[] = {
	{ DRM_MODE_CONNECTOR_Unknown, "unknown" },
	{ DRM_MODE_CONNECTOR_VGA, "VGA" },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I" },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D" },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A" },
	{ DRM_MODE_CONNECTOR_Composite, "composite" },
	{ DRM_MODE_CONNECTOR_SVIDEO, "s-video" },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS" },
	{ DRM_MODE_CONNECTOR_Component, "component" },
	{ DRM_MODE_CONNECTOR_9PinDIN, "9-pin DIN" },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DP" },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A" },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B" },
	{ DRM_MODE_CONNECTOR_TV, "TV" },
	{ DRM_MODE_CONNECTOR_eDP, "eDP" },
};

type_name_fn(connector_type)

#define bit_name_fn(res)					\
const char * res##_str(int type, std::ostringstream *out) {				\
	unsigned int i;						\
	const char *sep = "";					\
	for (i = 0; i < ARRAY_SIZE(res##_names); i++) {		\
		if (type & (1 << i)) {				\
			*out << sep << res##_names[i];	\
			sep = ", ";				\
		}						\
	}							\
	return NULL;						\
}

static const char *mode_type_names[] = {
	"builtin",
	"clock_c",
	"crtc_c",
	"preferred",
	"default",
	"userdef",
	"driver",
};

static bit_name_fn(mode_type)

static const char *mode_flag_names[] = {
	"phsync",
	"nhsync",
	"pvsync",
	"nvsync",
	"interlace",
	"dblscan",
	"csync",
	"pcsync",
	"ncsync",
	"hskew",
	"bcast",
	"pixmux",
	"dblclk",
	"clkdiv2"
};
static bit_name_fn(mode_flag)

void DrmResources::dump_mode(drmModeModeInfo *mode, std::ostringstream *out) {
	*out << mode->name << " " << mode->vrefresh << " "
	     << mode->hdisplay << " " << mode->hsync_start << " "
	     << mode->hsync_end << " " << mode->htotal << " "
	     << mode->vdisplay << " " << mode->vsync_start << " "
	     << mode->vsync_end << " " << mode->vtotal;

	*out << " flags: ";
	mode_flag_str(mode->flags, out);
	*out << " types: " << mode->type << "\n";
    mode_type_str(mode->type, out);
}

void DrmResources::dump_blob(uint32_t blob_id, std::ostringstream *out) {
	uint32_t i;
	unsigned char *blob_data;
	drmModePropertyBlobPtr blob;

	blob = drmModeGetPropertyBlob(fd(), blob_id);
	if (!blob) {
		*out << "\n";
		return;
	}

	blob_data = (unsigned char*)blob->data;

	for (i = 0; i < blob->length; i++) {
		if (i % 16 == 0)
			*out << "\n\t\t\t";
		*out << std::hex << blob_data[i];
	}
	*out << "\n";

	drmModeFreePropertyBlob(blob);
}

void DrmResources::dump_prop(drmModePropertyPtr prop,
		      uint32_t prop_id, uint64_t value, std::ostringstream *out) {
	int i;

	*out << "\t" << prop_id;
	if (!prop) {
		*out << "\n";
		return;
	}

	*out << " " << prop->name << ":\n";

	*out << "\t\tflags:";
	if (prop->flags & DRM_MODE_PROP_PENDING)
		*out << " pending";
	if (prop->flags & DRM_MODE_PROP_IMMUTABLE)
		*out << " immutable";
	if (drm_property_type_is(prop, DRM_MODE_PROP_SIGNED_RANGE))
		*out << " signed range";
	if (drm_property_type_is(prop, DRM_MODE_PROP_RANGE))
		*out << " range";
	if (drm_property_type_is(prop, DRM_MODE_PROP_ENUM))
		*out << " enum";
	if (drm_property_type_is(prop, DRM_MODE_PROP_BITMASK))
		*out << " bitmask";
	if (drm_property_type_is(prop, DRM_MODE_PROP_BLOB))
		*out << " blob";
	if (drm_property_type_is(prop, DRM_MODE_PROP_OBJECT))
		*out << " object";
	*out << "\n";

	if (drm_property_type_is(prop, DRM_MODE_PROP_SIGNED_RANGE)) {
		*out << "\t\tvalues:";
		for (i = 0; i < prop->count_values; i++)
			*out << U642I64(prop->values[i]);
		*out << "\n";
	}

	if (drm_property_type_is(prop, DRM_MODE_PROP_RANGE)) {
		*out << "\t\tvalues:";
		for (i = 0; i < prop->count_values; i++)
			*out << prop->values[i];
		*out << "\n";
	}

	if (drm_property_type_is(prop, DRM_MODE_PROP_ENUM)) {
		*out << "\t\tenums:";
		for (i = 0; i < prop->count_enums; i++)
			*out << prop->enums[i].name << "=" << prop->enums[i].value;
		*out << "\n";
	} else if (drm_property_type_is(prop, DRM_MODE_PROP_BITMASK)) {
		*out << "\t\tvalues:";
		for (i = 0; i < prop->count_enums; i++)
			*out << prop->enums[i].name << "=" << std::hex << (1LL << prop->enums[i].value);
		*out << "\n";
	} else {
		assert(prop->count_enums == 0);
	}

	if (drm_property_type_is(prop, DRM_MODE_PROP_BLOB)) {
		*out << "\t\tblobs:\n";
		for (i = 0; i < prop->count_blobs; i++)
			dump_blob(prop->blob_ids[i], out);
		*out << "\n";
	} else {
		assert(prop->count_blobs == 0);
	}

	*out << "\t\tvalue:";
	if (drm_property_type_is(prop, DRM_MODE_PROP_BLOB))
		dump_blob(value, out);
	else
		*out << value;

    *out << "\n";
}

int DrmResources::DumpProperty(uint32_t obj_id, uint32_t obj_type, std::ostringstream *out) {
  drmModePropertyPtr* prop_info;
  drmModeObjectPropertiesPtr props;

  props = drmModeObjectGetProperties(fd(), obj_id, obj_type);
  if (!props) {
    ALOGE("Failed to get properties for %d/%x", obj_id, obj_type);
    return -ENODEV;
  }
  prop_info = (drmModePropertyPtr*)malloc(props->count_props * sizeof *prop_info);
  if (!prop_info) {
    ALOGE("Malloc drmModePropertyPtr array failed");
    return -ENOMEM;
  }

  *out << "  props:\n";
  for (int i = 0;(size_t)i < props->count_props; ++i) {
    prop_info[i] = drmModeGetProperty(fd(), props->props[i]);

    dump_prop(prop_info[i],props->props[i],props->prop_values[i],out);

    drmModeFreeProperty(prop_info[i]);
  }

  drmModeFreeObjectProperties(props);
  free(prop_info);
  return 0;
}

int DrmResources::DumpPlaneProperty(const DrmPlane &plane, std::ostringstream *out) {
  return DumpProperty(plane.id(), DRM_MODE_OBJECT_PLANE, out);
}

int DrmResources::DumpCrtcProperty(const DrmCrtc &crtc, std::ostringstream *out) {
  return DumpProperty(crtc.id(), DRM_MODE_OBJECT_CRTC, out);
}

int DrmResources::DumpConnectorProperty(const DrmConnector &connector, std::ostringstream *out) {
   return DumpProperty(connector.id(), DRM_MODE_OBJECT_CONNECTOR, out);
}
#endif

int DrmResources::GetPlaneProperty(const DrmPlane &plane, const char *prop_name,
                                   DrmProperty *property) {
  return GetProperty(plane.id(), DRM_MODE_OBJECT_PLANE, prop_name, property);
}

int DrmResources::GetCrtcProperty(const DrmCrtc &crtc, const char *prop_name,
                                  DrmProperty *property) {
  return GetProperty(crtc.id(), DRM_MODE_OBJECT_CRTC, prop_name, property);
}

int DrmResources::GetConnectorProperty(const DrmConnector &connector,
                                       const char *prop_name,
                                       DrmProperty *property) {
  return GetProperty(connector.id(), DRM_MODE_OBJECT_CONNECTOR, prop_name,
                     property);
}

#if RK_DRM_HWC
std::vector<PlaneGroup *>& DrmResources::GetPlaneGroups() {
    return plane_groups_;
}
#endif
}
