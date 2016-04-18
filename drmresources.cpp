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

namespace android {

DrmResources::DrmResources() : fd_(-1), mode_id_(0), compositor_(this) {
}

DrmResources::~DrmResources() {
  for (std::vector<DrmConnector *>::const_iterator iter = connectors_.begin();
       iter != connectors_.end(); ++iter)
    delete *iter;
  connectors_.clear();

  for (std::vector<DrmEncoder *>::const_iterator iter = encoders_.begin();
       iter != encoders_.end(); ++iter)
    delete *iter;
  encoders_.clear();

  for (std::vector<DrmCrtc *>::const_iterator iter = crtcs_.begin();
       iter != crtcs_.end(); ++iter)
    delete *iter;
  crtcs_.clear();

  for (std::vector<DrmPlane *>::const_iterator iter = planes_.begin();
       iter != planes_.end(); ++iter)
    delete *iter;
  planes_.clear();

  if (fd_ >= 0)
    close(fd_);
}

int DrmResources::Init() {
  char path[PROPERTY_VALUE_MAX];
  property_get("hwc.drm.device", path, "/dev/dri/card0");

  /* TODO: Use drmOpenControl here instead */
  fd_ = open(path, O_RDWR);
  if (fd_ < 0) {
    ALOGE("Failed to open dri- %s", strerror(-errno));
    return -ENODEV;
  }

  int ret = drmSetClientCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret) {
    ALOGE("Failed to set universal plane cap %d", ret);
    return ret;
  }

  ret = drmSetClientCap(fd_, DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    ALOGE("Failed to set atomic cap %d", ret);
    return ret;
  }

  drmModeResPtr res = drmModeGetResources(fd_);
  if (!res) {
    ALOGE("Failed to get DrmResources resources");
    return -ENODEV;
  }

  bool found_primary = false;
  int display_num = 1;

  for (int i = 0; !ret && i < res->count_crtcs; ++i) {
    drmModeCrtcPtr c = drmModeGetCrtc(fd_, res->crtcs[i]);
    if (!c) {
      ALOGE("Failed to get crtc %d", res->crtcs[i]);
      ret = -ENODEV;
      break;
    }

    DrmCrtc *crtc = new DrmCrtc(this, c, i);

#if RK_DRM_HWC_DEBUG
    std::ostringstream out;
    out << "CRTCs:\n";
	out << "id\tfb\tpos\tsize\n";
    crtc->dump_crtc(&out);
    out << "\n";
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
#endif

    drmModeFreeCrtc(c);

    if (!crtc) {
      ALOGE("Failed to allocate crtc %d", res->crtcs[i]);
      ret = -ENOMEM;
      break;
    }

    ret = crtc->Init();
    if (ret) {
      ALOGE("Failed to initialize crtc %d", res->crtcs[i]);
      delete crtc;
      break;
    }
    crtcs_.push_back(crtc);
  }

  for (int i = 0; !ret && i < res->count_encoders; ++i) {
    drmModeEncoderPtr e = drmModeGetEncoder(fd_, res->encoders[i]);
    if (!e) {
      ALOGE("Failed to get encoder %d", res->encoders[i]);
      ret = -ENODEV;
      break;
    }

    std::vector<DrmCrtc *> possible_crtcs;
    DrmCrtc *current_crtc = NULL;
    for (std::vector<DrmCrtc *>::const_iterator iter = crtcs_.begin();
         iter != crtcs_.end(); ++iter) {
      if ((1 << (*iter)->pipe()) & e->possible_crtcs)
        possible_crtcs.push_back(*iter);

      if ((*iter)->id() == e->crtc_id)
        current_crtc = (*iter);
    }

    DrmEncoder *enc = new DrmEncoder(this, e, current_crtc, possible_crtcs);
#if RK_DRM_HWC_DEBUG
    std::ostringstream out;
	out << "Encoders:\n";
	out << "id\tcrtc\ttype\tpossible crtcs\tpossible clones\t\n";
    enc->dump_encoder(&out);
    out << "\n";
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
#endif
    drmModeFreeEncoder(e);

    if (!enc) {
      ALOGE("Failed to allocate enc %d", res->encoders[i]);
      ret = -ENOMEM;
      break;
    }
    encoders_.push_back(enc);
  }

  for (int i = 0; !ret && i < res->count_connectors; ++i) {
    drmModeConnectorPtr c = drmModeGetConnector(fd_, res->connectors[i]);
    if (!c) {
      ALOGE("Failed to get connector %d", res->connectors[i]);
      ret = -ENODEV;
      break;
    }

    std::vector<DrmEncoder *> possible_encoders;
    DrmEncoder *current_encoder = NULL;
    for (int j = 0; j < c->count_encoders; ++j) {
      for (std::vector<DrmEncoder *>::const_iterator iter = encoders_.begin();
           iter != encoders_.end(); ++iter) {
        if ((*iter)->id() == c->encoders[j])
          possible_encoders.push_back((*iter));
        if ((*iter)->id() == c->encoder_id)
          current_encoder = *iter;
      }
    }

    DrmConnector *conn =
        new DrmConnector(this, c, current_encoder, possible_encoders);

#if RK_DRM_HWC_DEBUG
    std::ostringstream out;
	out << "Connectors:\n";
	out << "id\tencoder\tstatus\t\ttype\tsize (mm)\tmodes\tencoders\n";

    conn->dump_connector(&out);
    out << "\n";
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
#endif

    drmModeFreeConnector(c);

    if (!conn) {
      ALOGE("Failed to allocate conn %d", res->connectors[i]);
      ret = -ENOMEM;
      break;
    }

    ret = conn->Init();
    if (ret) {
      ALOGE("Init connector %d failed", res->connectors[i]);
      delete conn;
      break;
    }
    connectors_.push_back(conn);

    if (conn->built_in() && !found_primary) {
      conn->set_display(0);
      found_primary = true;
    } else {
      conn->set_display(display_num);
      ++display_num;
    }
  }
  if (res)
    drmModeFreeResources(res);

  // Catch-all for the above loops
  if (ret)
    return ret;

  drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd_);
  if (!plane_res) {
    ALOGE("Failed to get plane resources");
    return -ENOENT;
  }

  for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
    drmModePlanePtr p = drmModeGetPlane(fd_, plane_res->planes[i]);
    if (!p) {
      ALOGE("Failed to get plane %d", plane_res->planes[i]);
      ret = -ENODEV;
      break;
    }

    DrmPlane *plane = new DrmPlane(this, p);

#if RK_DRM_HWC_DEBUG
    std::ostringstream out;
	out << "Planes:\n";
	out << "id\tcrtc\tfb\tCRTC x,y\tx,y\tgamma size\tpossible crtcs\n";

    plane->dump_plane(&out);
    out << "\n";
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
#endif

    drmModeFreePlane(p);

    if (!plane) {
      ALOGE("Allocate plane %d failed", plane_res->planes[i]);
      ret = -ENOMEM;
      break;
    }

    ret = plane->Init();
    if (ret) {
      ALOGE("Init plane %d failed", plane_res->planes[i]);
      delete plane;
      break;
    }

    planes_.push_back(plane);
  }
  drmModeFreePlaneResources(plane_res);
  if (ret)
    return ret;

  ret = compositor_.Init();
  if (ret)
    return ret;

  for (auto i = begin_connectors(); i != end_connectors(); ++i) {
    ret = CreateDisplayPipe(*i);
    if (ret) {
      ALOGE("Failed CreateDisplayPipe %d with %d", (*i)->id(), ret);
      return ret;
    }
  }
  return 0;
}

int DrmResources::fd() const {
  return fd_;
}

DrmResources::ConnectorIter DrmResources::begin_connectors() const {
  return connectors_.begin();
}

DrmResources::ConnectorIter DrmResources::end_connectors() const {
  return connectors_.end();
}

DrmConnector *DrmResources::GetConnectorForDisplay(int display) const {
  for (ConnectorIter iter = connectors_.begin(); iter != connectors_.end();
       ++iter) {
    if ((*iter)->display() == display)
      return *iter;
  }
  return NULL;
}

DrmCrtc *DrmResources::GetCrtcForDisplay(int display) const {
  for (std::vector<DrmCrtc *>::const_iterator iter = crtcs_.begin();
       iter != crtcs_.end(); ++iter) {
    if ((*iter)->display() == display)
      return *iter;
  }
  return NULL;
}

DrmResources::PlaneIter DrmResources::begin_planes() const {
  return planes_.begin();
}

DrmResources::PlaneIter DrmResources::end_planes() const {
  return planes_.end();
}

DrmPlane *DrmResources::GetPlane(uint32_t id) const {
  for (std::vector<DrmPlane *>::const_iterator iter = planes_.begin();
       iter != planes_.end(); ++iter) {
    if ((*iter)->id() == id)
      return *iter;
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
  for (DrmEncoder::CrtcIter iter = enc->begin_possible_crtcs();
       iter != enc->end_possible_crtcs(); ++iter) {
    /* We've already tried this earlier */
    if (*iter == enc->crtc())
      continue;

    if ((*iter)->can_bind(display)) {
      enc->set_crtc(*iter);
      (*iter)->set_display(display);
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

  for (DrmConnector::EncoderIter iter = connector->begin_possible_encoders();
       iter != connector->end_possible_encoders(); ++iter) {
    int ret = TryEncoderForDisplay(display, *iter);
    if (!ret) {
      connector->set_encoder(*iter);
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

  int ret = drmIoctl(fd_, DRM_IOCTL_MODE_CREATEPROPBLOB, &create_blob);
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
  int ret = drmIoctl(fd_, DRM_IOCTL_MODE_DESTROYPROPBLOB, &destroy_blob);
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

  props = drmModeObjectGetProperties(fd_, obj_id, obj_type);
  if (!props) {
    ALOGE("Failed to get properties for %d/%x", obj_id, obj_type);
    return -ENODEV;
  }

  bool found = false;
  for (int i = 0; !found && (size_t)i < props->count_props; ++i) {
    drmModePropertyPtr p = drmModeGetProperty(fd_, props->props[i]);
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

	blob = drmModeGetPropertyBlob(fd_, blob_id);
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

	printf(" %s:\n", prop->name);
	*out << prop->name << ":\n";

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
}

int DrmResources::DumpProperty(uint32_t obj_id, uint32_t obj_type, std::ostringstream *out) {
  drmModePropertyPtr* prop_info;
  drmModeObjectPropertiesPtr props;

  props = drmModeObjectGetProperties(fd_, obj_id, obj_type);
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
    prop_info[i] = drmModeGetProperty(fd_, props->props[i]);

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
}
