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

#define LOG_TAG "hwc-drm-display-composition"

#include "drmdisplaycomposition.h"
#include "drmcrtc.h"
#include "drmplane.h"
#include "drmresources.h"

#include <stdlib.h>

#include <algorithm>
#include <unordered_set>

#include <cutils/log.h>
#include <sw_sync.h>
#include <sync/sync.h>
#include <xf86drmMode.h>

namespace android {

const size_t DrmCompositionPlane::kSourceNone;
const size_t DrmCompositionPlane::kSourcePreComp;
const size_t DrmCompositionPlane::kSourceSquash;
const size_t DrmCompositionPlane::kSourceLayerMax;

DrmDisplayComposition::~DrmDisplayComposition() {
  if (timeline_fd_ >= 0) {
    SignalCompositionDone();
    close(timeline_fd_);
  }
}

int DrmDisplayComposition::Init(DrmResources *drm, DrmCrtc *crtc,
                                Importer *importer, uint64_t frame_no) {
  drm_ = drm;
  crtc_ = crtc;  // Can be NULL if we haven't modeset yet
  importer_ = importer;
  frame_no_ = frame_no;

  int ret = sw_sync_timeline_create();
  if (ret < 0) {
    ALOGE("Failed to create sw sync timeline %d", ret);
    return ret;
  }
  timeline_fd_ = ret;
  return 0;
}

bool DrmDisplayComposition::validate_composition_type(DrmCompositionType des) {
  return type_ == DRM_COMPOSITION_TYPE_EMPTY || type_ == des;
}

int DrmDisplayComposition::CreateNextTimelineFence() {
  ++timeline_;
  return sw_sync_fence_create(timeline_fd_, "hwc drm display composition fence",
                              timeline_);
}

int DrmDisplayComposition::IncreaseTimelineToPoint(int point) {
  int timeline_increase = point - timeline_current_;
  if (timeline_increase <= 0)
    return 0;

  int ret = sw_sync_timeline_inc(timeline_fd_, timeline_increase);
  if (ret)
    ALOGE("Failed to increment sync timeline %d", ret);
  else
    timeline_current_ = point;

  return ret;
}

int DrmDisplayComposition::SetLayers(DrmHwcLayer *layers, size_t num_layers,
                                     bool geometry_changed) {
  if (!validate_composition_type(DRM_COMPOSITION_TYPE_FRAME))
    return -EINVAL;

  geometry_changed_ = geometry_changed;

  for (size_t layer_index = 0; layer_index < num_layers; layer_index++) {
    layers_.emplace_back(std::move(layers[layer_index]));
  }

  type_ = DRM_COMPOSITION_TYPE_FRAME;
  return 0;
}

int DrmDisplayComposition::SetDpmsMode(uint32_t dpms_mode) {
  if (!validate_composition_type(DRM_COMPOSITION_TYPE_DPMS))
    return -EINVAL;
  dpms_mode_ = dpms_mode;
  type_ = DRM_COMPOSITION_TYPE_DPMS;
  return 0;
}

int DrmDisplayComposition::SetDisplayMode(const DrmMode &display_mode) {
  if (!validate_composition_type(DRM_COMPOSITION_TYPE_MODESET))
    return -EINVAL;
  display_mode_ = display_mode;
  dpms_mode_ = DRM_MODE_DPMS_ON;
  type_ = DRM_COMPOSITION_TYPE_MODESET;
  return 0;
}

int DrmDisplayComposition::AddPlaneDisable(DrmPlane *plane) {
  composition_planes_.emplace_back(
      DrmCompositionPlane{plane, crtc_, DrmCompositionPlane::kSourceNone, DrmCompositionPlane::kSourceNone});
  return 0;
}

static size_t CountUsablePlanes(DrmCrtc *crtc,
                                std::vector<DrmPlane *> *primary_planes,
                                std::vector<DrmPlane *> *overlay_planes) {
  return std::count_if(
             primary_planes->begin(), primary_planes->end(),
             [=](DrmPlane *plane) { return plane->GetCrtcSupported(*crtc); }) +
         std::count_if(
             overlay_planes->begin(), overlay_planes->end(),
             [=](DrmPlane *plane) { return plane->GetCrtcSupported(*crtc); });
}

#if RK_DRM_HWC
bool EraseFromPlanes(uint32_t id,std::vector<DrmPlane *> *planes)
{
    for (auto iter = planes->begin(); iter != planes->end(); ++iter) {
        if((*iter)->id()== id) {
           planes->erase(iter);
           return true;
        }
    }
    return false;
}

bool ErasePlane(uint32_t id,std::vector<DrmPlane *> *primary_planes,std::vector<DrmPlane *> *overlay_planes)
{

    if(!EraseFromPlanes(id,primary_planes))
        return EraseFromPlanes(id,overlay_planes);

    return true;
}

static DrmPlane *TakePlane(DrmCrtc *crtc,
                                DrmResources *drm,
                                std::vector<size_t>& layers_remaining,
                                std::vector<DrmPlane *> *primary_planes,
                                std::vector<DrmPlane *> *overlay_planes,
                                DrmHwcLayer* layer) {
    bool b_yuv;
    std::vector<PlaneGroup *>& plane_groups=drm->GetPlaneGroups();

    //loop plane groups.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
        iter != plane_groups.end(); ++iter) {
        //find the useful plane group
        if(!(*iter)->bUse) {
             //loop plane
            for(std::vector<DrmPlane*> ::const_iterator iter_plane=(*iter)->planes.begin();
                !(*iter)->planes.empty() && iter_plane != (*iter)->planes.end(); ++iter_plane) {
                    if(!(*iter_plane)->is_use() && (*iter_plane)->GetCrtcSupported(*crtc))
                    {
                        //if layer is valid,then judge whether it suit for the plane.
                        //otherwise,only get a plane from the plane group.
                        if(layer)
                        {
                            b_yuv  = (*iter_plane)->get_yuv();
                            if(layer->is_yuv && !b_yuv)
                                continue;

                            ALOGD_IF(log_level(DBG_DEBUG),"TakePlane: match layer=%s,plane=%d,layer->index=%d",
                                    layer->name.c_str(),(*iter_plane)->id(),layer->index);

                            //remove the assigned layer from layers_remaining index.
                            for(std::vector<size_t>::iterator iter_index = layers_remaining.begin();
                                !layers_remaining.empty() && iter_index != layers_remaining.end();++iter_index)
                            {
                                if(*iter_index == layer->index)
                                    layers_remaining.erase(iter_index);
                            }
                        }

                        (*iter_plane)->set_use(true);
                        (*iter)->bUse = true;
                        //erase the plane from primary_planes or overlay_planes
                        ErasePlane((*iter_plane)->id(),primary_planes,overlay_planes);
                        return *iter_plane;
                    }
                }
        }
    }

    if(layer)
        ALOGD_IF(log_level(DBG_DEBUG),"TakePlane: cann't match layer=%s,layer->index=%d",
            layer->name.c_str(),layer->index);

    return NULL;
}



//According to zpos and combine layer count,find the suitable plane.
bool DrmDisplayComposition::MatchPlane(std::vector<DrmHwcLayer*>& layer_vector,
                               std::vector<size_t>& layers_remaining,
                               uint64_t* zpos,
                               std::vector<DrmPlane *> *primary_planes,
                                std::vector<DrmPlane *> *overlay_planes)
{
    uint32_t combine_layer_count = layer_vector.size();
    bool b_yuv;
    std::vector<PlaneGroup *> ::const_iterator iter;
    std::vector<PlaneGroup *>& plane_groups=drm_->GetPlaneGroups();

    //loop plane groups.
    for (iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
       ALOGD_IF(log_level(DBG_DEBUG),"line=%d,last zpos=%d,plane group zpos=%d,plane group bUse=%d",__LINE__,*zpos,(*iter)->zpos,(*iter)->bUse);
        //find the match zpos plane group
        if(!(*iter)->bUse && (*iter)->zpos >= *zpos)
        {
            ALOGD_IF(log_level(DBG_DEBUG),"line=%d,combine_layer_count=%d,planes size=%d",__LINE__,combine_layer_count,(*iter)->planes.size());

            //find the match combine layer count with plane size.
            if(combine_layer_count <= (*iter)->planes.size())
            {
                //loop layer
                for(std::vector<DrmHwcLayer*>::const_iterator iter_layer= layer_vector.begin();
                    !layer_vector.empty() && iter_layer != layer_vector.end();++iter_layer)
                {
                    if((*iter_layer)->is_match)
                        continue;

                    //loop plane
                    for(std::vector<DrmPlane*> ::const_iterator iter_plane=(*iter)->planes.begin();
                        !(*iter)->planes.empty() && iter_plane != (*iter)->planes.end(); ++iter_plane)
                    {
                        ALOGD_IF(log_level(DBG_DEBUG),"line=%d,plane is_use=%d",__LINE__,(*iter_plane)->is_use());
                        if(!(*iter_plane)->is_use() && (*iter_plane)->GetCrtcSupported(*crtc_))
                        {
                            b_yuv  = (*iter_plane)->get_yuv();
                            if((*iter_layer)->is_yuv && !b_yuv)
                                continue;

                            ALOGD_IF(log_level(DBG_DEBUG),"MatchPlane: match layer=%s,plane=%d,(*iter_layer)->index=%d",(*iter_layer)->name.c_str(),
                                (*iter_plane)->id(),(*iter_layer)->index);
                            //Find the match plane for layer,it will be commit.
                            composition_planes_.emplace_back(
                                DrmCompositionPlane{(*iter_plane),crtc_, (*iter_layer)->index, (*iter_layer)->index});
                            //remove the assigned layer from layers_remaining index.
                            for(std::vector<size_t>::iterator iter_index = layers_remaining.begin();
                                !layers_remaining.empty() && iter_index != layers_remaining.end();++iter_index)
                            {
                                if(*iter_index == (*iter_layer)->index)
                                    layers_remaining.erase(iter_index);
                            }

                            (*iter_layer)->is_match = true;
                            (*iter_plane)->set_use(true);

                            //erase the plane from primary_planes or overlay_planes
                            ErasePlane((*iter_plane)->id(),primary_planes,overlay_planes);
                            break;

                        }
                    }
                }
                if(layer_vector.size()==0)
                {
                    ALOGD_IF(log_level(DBG_DEBUG),"line=%d all match",__LINE__);
                    //update zpos for the next time.
                    *zpos=(*iter)->zpos+1;
                    (*iter)->bUse = true;
                    return true;
                }
            }
            /*else
            {
                //1. cut out combine_layer_count to (*iter)->planes.size().
                //2. combine_layer_count layer assign planes.
                //3. extern layers assign planes.
                return false;
            }*/
        }

    }

    return false;
}


#if 0
static bool is_not_hor_intersect(DrmHwcRect<int>* rec1,DrmHwcRect<int>* rec2)
{

    if (rec1->bottom > rec2->top && rec1->bottom < rec2->bottom) {
        return false;
    }

    if (rec1->top > rec2->top && rec1->top < rec2->bottom) {
        return false;
    }

    return true;
}
#else
static bool is_not_intersect(DrmHwcRect<int>* rec1,DrmHwcRect<int>* rec2)
{
    ALOGD_IF(log_level(DBG_DEBUG),"is_not_intersect: rec1[%d,%d,%d,%d],rec2[%d,%d,%d,%d]",rec1->left,rec1->top,
        rec1->right,rec1->bottom,rec2->left,rec2->top,rec2->right,rec2->bottom);
    if (rec1->left >= rec2->left && rec1->left <= rec2->right) {
        if (rec1->top >= rec2->top && rec1->top <= rec2->bottom) {
            return false;
        }
        if (rec1->bottom >= rec2->top && rec1->bottom <= rec2->bottom) {
            return false;
        }
    }
    if (rec1->right >= rec2->left && rec1->right <= rec2->right) {
        if (rec1->top >= rec2->top && rec1->top <= rec2->bottom) {
            return false;
        }
        if (rec1->bottom >= rec2->top && rec1->bottom <= rec2->bottom) {
            return false;
        }
    }

    return true;
}
#endif

static bool is_layer_combine(DrmHwcLayer * layer_one,DrmHwcLayer * layer_two)
{
    //Don't care format.
    if(/*layer_one->format != layer_two->format
        ||*/ layer_one->alpha!= layer_two->alpha
        || layer_one->is_scale || layer_two->is_scale
        || !is_not_intersect(&layer_one->display_frame,&layer_two->display_frame))
    {
        ALOGD_IF(log_level(DBG_DEBUG),"is_layer_combine layer one alpha=%d,is_scale=%d",layer_one->alpha,layer_one->is_scale);
        ALOGD_IF(log_level(DBG_DEBUG),"is_layer_combine layer two alpha=%d,is_scale=%d",layer_two->alpha,layer_two->is_scale);
        return false;
    }

    return true;
}


static void ReservedPlane(DrmCrtc *crtc, std::vector<DrmPlane *> *planes,unsigned int id) {

  for (auto iter = planes->begin(); iter != planes->end(); ++iter) {
    if ((*iter)->GetCrtcSupported(*crtc)) {
        DrmPlane *plane = *iter;
        if(plane->id() == id)
            plane->set_reserved(true);
    }
  }
}

static void ReservedPlane(DrmCrtc *crtc,
                           std::vector<DrmPlane *> *primary_planes,
                           std::vector<DrmPlane *> *overlay_planes,
                           unsigned int id) {
    ReservedPlane(crtc, primary_planes, id);
    ReservedPlane(crtc, overlay_planes, id);
}

void DrmDisplayComposition::EmplaceCompositionPlane(
    size_t source_layer,
    std::vector<size_t>& layers_remaining,
    std::vector<DrmPlane *> *primary_planes,
    std::vector<DrmPlane *> *overlay_planes) {

  DrmPlane *plane;
  DrmHwcLayer* layer;
  if(source_layer == DrmCompositionPlane::kSourceNone ||
     source_layer == DrmCompositionPlane::kSourceSquash ||
     source_layer == DrmCompositionPlane::kSourcePreComp)
     layer = NULL;
  else
     layer = &layers_[source_layer];

  plane = TakePlane(crtc_, drm_, layers_remaining, primary_planes, overlay_planes, layer);

  if (plane == NULL) {
    ALOGE(
        "Failed to add composition plane because there are no planes "
        "remaining");
    return;
  }
  composition_planes_.emplace_back(
      DrmCompositionPlane{plane, crtc_, source_layer, source_layer});
}

#else

static DrmPlane *TakePlane(DrmCrtc *crtc, std::vector<DrmPlane *> *planes) {
   for (auto iter = planes->begin(); iter != planes->end(); ++iter) {
     if ((*iter)->GetCrtcSupported(*crtc)) {
       DrmPlane *plane = *iter;
       planes->erase(iter);
       return plane;
    }
  }
  return NULL;
}

 static DrmPlane *TakePlane(DrmCrtc *crtc,
                            std::vector<DrmPlane *> *primary_planes,
                            std::vector<DrmPlane *> *overlay_planes) {
    DrmPlane *plane = TakePlane(crtc, primary_planes);
    if (plane)
        return plane;
    return TakePlane(crtc, overlay_planes);
}

void DrmDisplayComposition::EmplaceCompositionPlane(
    size_t source_layer,
    std::vector<DrmPlane *> *primary_planes,
    std::vector<DrmPlane *> *overlay_planes) {


  DrmPlane *plane = TakePlane(crtc_, primary_planes, overlay_planes);

  if (plane == NULL) {
    ALOGE(
        "EmplaceCompositionPlane: Failed to add composition plane because there are no planes "
        "remaining");
    return;
  }
  composition_planes_.emplace_back(
      DrmCompositionPlane{plane, crtc_, source_layer, source_layer});
}
#endif

static std::vector<size_t> SetBitsToVector(uint64_t in, size_t *index_map) {
  std::vector<size_t> out;
  size_t msb = sizeof(in) * 8 - 1;
  uint64_t mask = (uint64_t)1 << msb;
  for (size_t i = msb; mask != (uint64_t)0; i--, mask >>= 1)
    if (in & mask)
      out.push_back(index_map[i]);
  return out;
}

static void SeparateLayers(DrmHwcLayer *layers, size_t *used_layers,
                           size_t num_used_layers,
                           DrmHwcRect<int> *exclude_rects,
                           size_t num_exclude_rects,
                           std::vector<DrmCompositionRegion> &regions) {
  if (num_used_layers > 64) {
    ALOGE("Failed to separate layers because there are more than 64");
    return;
  }

  if (num_used_layers + num_exclude_rects > 64) {
    ALOGW(
        "Exclusion rectangles are being truncated to make the rectangle count "
        "fit into 64");
    num_exclude_rects = 64 - num_used_layers;
  }

  // We inject all the exclude rects into the rects list. Any resulting rect
  // that includes ANY of the first num_exclude_rects is rejected.
  std::vector<DrmHwcRect<int>> layer_rects(num_used_layers + num_exclude_rects);
  std::copy(exclude_rects, exclude_rects + num_exclude_rects,
            layer_rects.begin());
  std::transform(
      used_layers, used_layers + num_used_layers,
      layer_rects.begin() + num_exclude_rects,
      [=](size_t layer_index) { return layers[layer_index].display_frame; });

  std::vector<separate_rects::RectSet<uint64_t, int>> separate_regions;
  separate_rects::separate_rects_64(layer_rects, &separate_regions);
  uint64_t exclude_mask = ((uint64_t)1 << num_exclude_rects) - 1;

  for (separate_rects::RectSet<uint64_t, int> &region : separate_regions) {
    if (region.id_set.getBits() & exclude_mask)
      continue;
    regions.emplace_back(DrmCompositionRegion{
        region.rect,
        SetBitsToVector(region.id_set.getBits() >> num_exclude_rects,
                        used_layers)});
  }
}

#if RK_DRM_HWC_DEBUG
void DrmCompositionPlane::dump_drm_com_plane(int index, std::ostringstream *out) const {
    *out << "DrmCompositionPlane[" << index << "]"
         << " plane=" << (plane ? plane->id() : -1)
         << " source_layer=";
    if (source_layer <= DrmCompositionPlane::kSourceLayerMax) {
      *out << source_layer;
    } else {
      switch (source_layer) {
        case DrmCompositionPlane::kSourceNone:
          *out << "NONE";
          break;
        case DrmCompositionPlane::kSourcePreComp:
          *out << "PRECOMP";
          break;
        case DrmCompositionPlane::kSourceSquash:
          *out << "SQUASH";
          break;
        default:
          *out << "<invalid>";
          break;
      }
    }

    *out << "\n";
}
#endif

int DrmDisplayComposition::CreateAndAssignReleaseFences() {
  std::unordered_set<DrmHwcLayer *> squash_layers;
  std::unordered_set<DrmHwcLayer *> pre_comp_layers;
  std::unordered_set<DrmHwcLayer *> comp_layers;


  for (const DrmCompositionRegion &region : squash_regions_) {
    for (size_t source_layer_index : region.source_layers) {
      DrmHwcLayer *source_layer = &layers_[source_layer_index];
      squash_layers.emplace(source_layer);
    }
  }

  for (const DrmCompositionRegion &region : pre_comp_regions_) {
    for (size_t source_layer_index : region.source_layers) {
      DrmHwcLayer *source_layer = &layers_[source_layer_index];
      pre_comp_layers.emplace(source_layer);
      squash_layers.erase(source_layer);
    }
  }

  for (const DrmCompositionPlane &plane : composition_planes_) {
    if (plane.source_layer <= DrmCompositionPlane::kSourceLayerMax) {
      DrmHwcLayer *source_layer = &layers_[plane.source_layer];
      comp_layers.emplace(source_layer);
      pre_comp_layers.erase(source_layer);
    }
  }

  for (DrmHwcLayer *layer : squash_layers) {
    if (!layer->release_fence)
      continue;
    int ret = layer->release_fence.Set(CreateNextTimelineFence());
    if (ret < 0)
      return ret;
  }
  timeline_squash_done_ = timeline_;

  for (DrmHwcLayer *layer : pre_comp_layers) {
    if (!layer->release_fence)
      continue;
    int ret = layer->release_fence.Set(CreateNextTimelineFence());
    if (ret < 0)
      return ret;
  }
  timeline_pre_comp_done_ = timeline_;

  for (DrmHwcLayer *layer : comp_layers) {
    if (!layer->release_fence)
      continue;
    int ret = layer->release_fence.Set(CreateNextTimelineFence());
    if (ret < 0)
      return ret;
  }

  return 0;
}

static bool has_layer(std::vector<DrmHwcLayer*>& layer_vector,DrmHwcLayer &layer)
{
        for (std::vector<DrmHwcLayer*>::const_iterator iter = layer_vector.begin();
               iter != layer_vector.end(); ++iter) {
            if((*iter)->sf_handle==layer.sf_handle)
                return true;
          }

          return false;
}
#define MOST_WIN_ZONES                  4

static void add_layer_by_xpos(std::vector<DrmHwcLayer*>& layers,DrmHwcLayer& layer)
{
    std::vector<DrmHwcLayer*>::iterator iter_layer;
    for(iter_layer = layers.begin();iter_layer != layers.end();++iter_layer) {
        if((*iter_layer)->display_frame.left > layer.display_frame.left) {
            layers.insert(iter_layer,&layer);
            break;
        }
    }

    if(iter_layer == layers.end()) {
        layers.emplace_back(&layer);
    }
}

int DrmDisplayComposition::Plan(SquashState *squash,
                                std::vector<DrmPlane *> *primary_planes,
                                std::vector<DrmPlane *> *overlay_planes) {
  if (type_ != DRM_COMPOSITION_TYPE_FRAME)
    return 0;

#if RK_DRM_HWC
  std::vector<size_t> layers_remaining_bk;
  std::vector<PlaneGroup *>& plane_groups=drm_->GetPlaneGroups();
#endif

#if USE_MULTI_AREAS
  size_t planes_can_use = plane_groups.size();
#else
  size_t planes_can_use =
      CountUsablePlanes(crtc_, primary_planes, overlay_planes);
#endif

  if (planes_can_use == 0) {
    ALOGE("Display %d has no usable planes", crtc_->display());
    return -ENODEV;
  }

  // All protected layers get first usage of planes
  std::vector<size_t> layers_remaining;
  for (size_t layer_index = 0; layer_index < layers_.size(); layer_index++) {
    if (!layers_[layer_index].protected_usage() || planes_can_use == 0) {
      layers_[layer_index].index = layer_index;
      layers_remaining.push_back(layer_index);
      continue;
    }

 #if RK_DRM_HWC
    EmplaceCompositionPlane(layer_index, layers_remaining, primary_planes, overlay_planes);
 #else
    EmplaceCompositionPlane(layer_index, primary_planes, overlay_planes);
 #endif
    planes_can_use--;
  }
#if 0
    for (int i = 0; i < layers_remaining.size()-1;i++)
    {
        for(int j=i+1;j < layers_remaining.size();j++)
        {
            if(layers_[i].display_frame.left > layers_[j].display_frame.left)
            {
                ALOGD("swap %s and %s",layers_[i].name.c_str(),layers_[j].name.c_str());
                std::swap(layers_[i],layers_[j]);
            }
        }
    }
#endif
  if (planes_can_use == 0 && layers_remaining.size() > 0) {
    ALOGE("Protected layers consumed all hardware planes");
    return CreateAndAssignReleaseFences();
  }

#if USE_MULTI_AREAS
    /*Group layer*/
    int zpos = 0;
    size_t i;
    uint32_t sort_cnt=0;
    bool is_combine = false;
    layer_map_.clear();

    ALOGD_IF(log_level(DBG_DEBUG),"layers_remaining.size()=%d",layers_remaining.size());
    for (i = 0; i < layers_remaining.size();) {
        sort_cnt=0;
        if(i == 0)
            layer_map_[zpos].push_back(&layers_[layers_remaining[0]]);
        for(size_t j = i+1;j<MOST_WIN_ZONES && j < layers_remaining.size(); j++) {
            DrmHwcLayer &layer_one = layers_[layers_remaining[j]];
            layer_one.index = layers_remaining[j];
            is_combine = false;
            for(size_t k = 0; k <= sort_cnt; k++ ) {
                DrmHwcLayer &layer_two = layers_[layers_remaining[j-1-k]];
                layer_two.index = layers_remaining[j-1-k];
                //juage the layer is contained in layer_vector
                bool bHasLayerOne = has_layer(layer_map_[zpos],layer_one);
                bool bHasLayerTwo = has_layer(layer_map_[zpos],layer_two);
                if(is_layer_combine(&layer_one,&layer_two)) {
                    //append layer into layer_vector of layer_map_.
                    if(!bHasLayerOne && !bHasLayerTwo)
                    {
#if 1
                        layer_map_[zpos].emplace_back(&layer_one);
                        layer_map_[zpos].emplace_back(&layer_two);
#else
                        add_layer_by_xpos(layer_map_[zpos],layer_one);
                        add_layer_by_xpos(layer_map_[zpos],layer_two);
#endif
                    }
                    else if(!bHasLayerTwo)
                    {
#if 1
                        layer_map_[zpos].emplace_back(&layer_two);
#else
                        add_layer_by_xpos(layer_map_[zpos],layer_two);
#endif
                    }
                    else if(!bHasLayerOne)
                    {
#if 1
                        layer_map_[zpos].emplace_back(&layer_one);
#else
                        add_layer_by_xpos(layer_map_[zpos],layer_one);
#endif
                    }
                    is_combine = true;
                }
                else
                {
                    //if it cann't combine two layer,it need start a new group.
                    if(!bHasLayerOne)
                    {
                        zpos++;
#if 1
                        layer_map_[zpos].emplace_back(&layer_one);
#else
                        add_layer_by_xpos(layer_map_[zpos],layer_one);
#endif
                    }
                    is_combine = false;
                    break;
                }
             }
             sort_cnt++; //update sort layer count
             if(!is_combine)
             {
                break;
             }
        }
        if(is_combine)  //all remain layer or limit MOST_WIN_ZONES layer is combine well,it need start a new group.
            zpos++;
        if(sort_cnt)
            i+=sort_cnt;    //jump the sort compare layers.
        else
            i++;
    }

  //sort layer by xpos
  for (DrmDisplayComposition::LayerMap::iterator iter = layer_map_.begin();
       iter != layer_map_.end(); ++iter) {
        if(iter->second.size() > 1) {
            for(int i=0;i < iter->second.size()-1;i++) {
                for(int j=i+1;j < iter->second.size();j++) {
                     if(iter->second[i]->display_frame.left > iter->second[j]->display_frame.left) {
                        ALOGV("swap %s and %s",iter->second[i]->name.c_str(),iter->second[j]->name.c_str());
                        std::swap(iter->second[i],iter->second[j]);
                        break;
                     }
                 }
            }
        }
  }

#if RK_DRM_HWC_DEBUG
  for (DrmDisplayComposition::LayerMap::iterator iter = layer_map_.begin();
       iter != layer_map_.end(); ++iter) {
        ALOGD_IF(log_level(DBG_DEBUG),"layer map id=%d,size=%d",iter->first,iter->second.size());
        for(std::vector<DrmHwcLayer*>::const_iterator iter_layer = iter->second.begin();
            iter_layer != iter->second.end();++iter_layer)
        {
             ALOGD_IF(log_level(DBG_DEBUG),"\tlayer name=%s",(*iter_layer)->name.c_str());
        }
  }
#endif

  uint64_t last_zpos=0;
  std::vector<DrmPlane *> primary_planes_bk = *primary_planes;
  std::vector<DrmPlane *> overlay_planes_bk = *overlay_planes;
  size_t planes_can_use_bk = planes_can_use;
  std::vector<DrmCompositionPlane> composition_planes_bk = composition_planes_;
  bool bMatch = false;
  layers_remaining_bk = layers_remaining;

  for (DrmDisplayComposition::LayerMap::iterator iter = layer_map_.begin();
       iter != layer_map_.end(); ++iter) {
        bMatch = MatchPlane(iter->second,layers_remaining,&last_zpos,primary_planes,overlay_planes);
        if(!bMatch)
        {
            ALOGV("Cann't find the match plane for layer group %d",iter->first);
            break;
        }
        else
            planes_can_use--;
  }

  //If it cann't match any layer after area assign process or
  //all planes is used up but still has layer remaining,we need rollback the area assign process.
  if(!bMatch || (planes_can_use==0 && layers_remaining.size() > 0))
  {
        //restore layers_remaining
        layers_remaining = layers_remaining_bk;
        *primary_planes = primary_planes_bk;
        *overlay_planes = overlay_planes_bk;
        planes_can_use = planes_can_use_bk;
        composition_planes_ = composition_planes_bk;

        ALOGD_IF(log_level(DBG_DEBUG),"restore layer size=%d,primary size=%d,overlay size=%d",
                layers_remaining.size(),primary_planes->size(),overlay_planes->size());

        //set use flag to false.
        for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
           iter != plane_groups.end(); ++iter) {
            (*iter)->bUse=false;

            for(std::vector<DrmPlane *> ::const_iterator iter_plane=(*iter)->planes.begin();
                iter_plane != (*iter)->planes.end(); ++iter_plane) {
                (*iter_plane)->set_use(false);
            }
        }
  }
#endif

#if USE_SQUASH
  bool use_squash_framebuffer = false;
  // Used to determine which layers were entirely squashed
  std::vector<int> layer_squash_area(layers_.size(), 0);
  // Used to avoid rerendering regions that were squashed
  std::vector<DrmHwcRect<int>> exclude_rects;
  if (squash != NULL && planes_can_use >= 3) {
    if (geometry_changed_) {
      squash->Init(layers_.data(), layers_.size());
    } else {
      std::vector<bool> changed_regions;
      squash->GenerateHistory(layers_.data(), layers_.size(), changed_regions);

      std::vector<bool> stable_regions;
      squash->StableRegionsWithMarginalHistory(changed_regions, stable_regions);

      // Only if SOME region is stable
      use_squash_framebuffer =
          std::find(stable_regions.begin(), stable_regions.end(), true) !=
          stable_regions.end();

      squash->RecordHistory(layers_.data(), layers_.size(), changed_regions);

      // Changes in which regions are squashed triggers a rerender via
      // squash_regions.
      bool render_squash = squash->RecordAndCompareSquashed(stable_regions);

      for (size_t region_index = 0; region_index < stable_regions.size();
           region_index++) {
        const SquashState::Region &region = squash->regions()[region_index];
        if (!stable_regions[region_index])
          continue;

        exclude_rects.emplace_back(region.rect);

        if (render_squash) {
          squash_regions_.emplace_back();
          squash_regions_.back().frame = region.rect;
        }

        int frame_area = region.rect.area();
        // Source layers are sorted front to back i.e. top layer has lowest
        // index.
        for (size_t layer_index = layers_.size();
             layer_index-- > 0;  // Yes, I double checked this
             /* See condition */) {
          if (!region.layer_refs[layer_index])
            continue;
          layer_squash_area[layer_index] += frame_area;
          if (render_squash)
            squash_regions_.back().source_layers.push_back(layer_index);
        }
      }
    }
  }

  std::vector<size_t> layers_remaining_if_squash;
  for (size_t layer_index : layers_remaining) {
    if (layer_squash_area[layer_index] <
        layers_[layer_index].display_frame.area())
      layers_remaining_if_squash.push_back(layer_index);
  }

  if (use_squash_framebuffer) {
    if (planes_can_use > 1 || layers_remaining_if_squash.size() == 0) {
      layers_remaining = std::move(layers_remaining_if_squash);
      planes_can_use--;  // Reserve plane for squashing
    } else {
      use_squash_framebuffer = false;  // The squash buffer is still rendered
    }
  }
#else
    UN_USED(squash);
#endif

#if USE_PRE_COMP
  if (layers_remaining.size() > planes_can_use)
    planes_can_use--;  // Reserve one for pre-compositing
#endif

  // Whatever planes that are not reserved get assigned a layer
  size_t last_composition_layer = 0;
#if RK_DRM_HWC
  layers_remaining_bk = layers_remaining;
  for (last_composition_layer = 0;
       last_composition_layer < layers_remaining_bk.size() && planes_can_use > 0;
       last_composition_layer++, planes_can_use--) {
    EmplaceCompositionPlane(layers_remaining_bk[last_composition_layer],layers_remaining,
                            primary_planes, overlay_planes);

  }
#else
  for (last_composition_layer = 0;
       last_composition_layer < layers_remaining.size() && planes_can_use > 0;
       last_composition_layer++, planes_can_use--) {
    EmplaceCompositionPlane(layers_remaining[last_composition_layer],
                            primary_planes, overlay_planes);
  }
  layers_remaining.erase(layers_remaining.begin(),
                         layers_remaining.begin() + last_composition_layer);
#endif

#if USE_PRE_COMP
  if (layers_remaining.size() > 0) {
#if RK_DRM_HWC
    EmplaceCompositionPlane(DrmCompositionPlane::kSourcePreComp, layers_remaining, primary_planes,
                            overlay_planes);
#else
    EmplaceCompositionPlane(DrmCompositionPlane::kSourcePreComp, primary_planes,
                            overlay_planes);
#endif
    SeparateLayers(layers_.data(), layers_remaining.data(),
                   layers_remaining.size(), exclude_rects.data(),
                   exclude_rects.size(), pre_comp_regions_);
  }
#endif

#if USE_SQUASH
  if (use_squash_framebuffer) {
#if RK_DRM_HWC
    EmplaceCompositionPlane(DrmCompositionPlane::kSourceSquash, layers_remaining, primary_planes,
                            overlay_planes);
#else
    EmplaceCompositionPlane(DrmCompositionPlane::kSourceSquash, primary_planes,
                            overlay_planes);
#endif
  }

#endif

#if RK_DRM_HWC
    //set use flag to false.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
        (*iter)->bUse=false;

        for(std::vector<DrmPlane *> ::const_iterator iter_plane=(*iter)->planes.begin();
            iter_plane != (*iter)->planes.end(); ++iter_plane) {
            (*iter_plane)->set_use(false);
        }
    }
#endif

#if RK_DRM_HWC_DEBUG
  size_t j=0;
  for (const DrmCompositionPlane &plane : composition_planes_) {
    std::ostringstream out;
    plane.dump_drm_com_plane(j,&out);
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
    j++;
  }
#endif

  return CreateAndAssignReleaseFences();
}

static const char *DrmCompositionTypeToString(DrmCompositionType type) {
  switch (type) {
    case DRM_COMPOSITION_TYPE_EMPTY:
      return "EMPTY";
    case DRM_COMPOSITION_TYPE_FRAME:
      return "FRAME";
    case DRM_COMPOSITION_TYPE_DPMS:
      return "DPMS";
    case DRM_COMPOSITION_TYPE_MODESET:
      return "MODESET";
    default:
      return "<invalid>";
  }
}

static const char *DPMSModeToString(int dpms_mode) {
  switch (dpms_mode) {
    case DRM_MODE_DPMS_ON:
      return "ON";
    case DRM_MODE_DPMS_OFF:
      return "OFF";
    default:
      return "<invalid>";
  }
}

#if !RK_DRM_HWC_DEBUG
static void DumpBuffer(const DrmHwcBuffer &buffer, std::ostringstream *out) {
  if (!buffer) {
    *out << "buffer=<invalid>";
    return;
  }

  *out << "buffer[w/h/format]=";
  *out << buffer->width << "/" << buffer->height << "/" << buffer->format;
}

static void DumpTransform(uint32_t transform, std::ostringstream *out) {
  *out << "[";

  if (transform == 0)
    *out << "IDENTITY";

  bool separator = false;
  if (transform & DrmHwcTransform::kFlipH) {
    *out << "FLIPH";
    separator = true;
  }
  if (transform & DrmHwcTransform::kFlipV) {
    if (separator)
      *out << "|";
    *out << "FLIPV";
    separator = true;
  }
  if (transform & DrmHwcTransform::kRotate90) {
    if (separator)
      *out << "|";
    *out << "ROTATE90";
    separator = true;
  }
  if (transform & DrmHwcTransform::kRotate180) {
    if (separator)
      *out << "|";
    *out << "ROTATE180";
    separator = true;
  }
  if (transform & DrmHwcTransform::kRotate270) {
    if (separator)
      *out << "|";
    *out << "ROTATE270";
    separator = true;
  }

  uint32_t valid_bits = DrmHwcTransform::kFlipH | DrmHwcTransform::kFlipH |
                        DrmHwcTransform::kRotate90 |
                        DrmHwcTransform::kRotate180 |
                        DrmHwcTransform::kRotate270;
  if (transform & ~valid_bits) {
    if (separator)
      *out << "|";
    *out << "INVALID";
  }
  *out << "]";
}

static const char *BlendingToString(DrmHwcBlending blending) {
  switch (blending) {
    case DrmHwcBlending::kNone:
      return "NONE";
    case DrmHwcBlending::kPreMult:
      return "PREMULT";
    case DrmHwcBlending::kCoverage:
      return "COVERAGE";
    default:
      return "<invalid>";
  }
}
#endif

static void DumpRegion(const DrmCompositionRegion &region,
                       std::ostringstream *out) {
  *out << "frame";
  region.frame.Dump(out);
  *out << " source_layers=(";

  const std::vector<size_t> &source_layers = region.source_layers;
  for (size_t i = 0; i < source_layers.size(); i++) {
    *out << source_layers[i];
    if (i < source_layers.size() - 1) {
      *out << " ";
    }
  }

  *out << ")";
}

void DrmDisplayComposition::Dump(std::ostringstream *out) const {
  *out << "----DrmDisplayComposition"
       << " crtc=" << (crtc_ ? crtc_->id() : -1)
       << " type=" << DrmCompositionTypeToString(type_);

  switch (type_) {
    case DRM_COMPOSITION_TYPE_DPMS:
      *out << " dpms_mode=" << DPMSModeToString(dpms_mode_);
      break;
    case DRM_COMPOSITION_TYPE_MODESET:
      *out << " display_mode=" << display_mode_.h_display() << "x"
           << display_mode_.v_display();
      break;
    default:
      break;
  }

  *out << " timeline[current/squash/pre-comp/done]=" << timeline_current_ << "/"
       << timeline_squash_done_ << "/" << timeline_pre_comp_done_ << "/"
       << timeline_ << "\n";

  *out << "    Layers: count=" << layers_.size() << "\n";
  for (size_t i = 0; i < layers_.size(); i++) {
    const DrmHwcLayer &layer = layers_[i];
#if RK_DRM_HWC_DEBUG
    layer.dump_drm_layer(i,out);
#else
    *out << "      [" << i << "] ";

    DumpBuffer(layer.buffer, out);

    if (layer.protected_usage())
      *out << " protected";

    *out << " transform=";
    DumpTransform(layer.transform, out);
    *out << " blending[a=" << (int)layer.alpha
         << "]=" << BlendingToString(layer.blending) << " source_crop";
    layer.source_crop.Dump(out);
    *out << " display_frame";
    layer.display_frame.Dump(out);

    *out << "\n";
#endif
  }

  *out << "    Planes: count=" << composition_planes_.size() << "\n";
  for (size_t i = 0; i < composition_planes_.size(); i++) {
    const DrmCompositionPlane &comp_plane = composition_planes_[i];
#if RK_DRM_HWC_DEBUG
    comp_plane.dump_drm_com_plane(i,out);
#else
    *out << "      [" << i << "]"
         << " plane=" << (comp_plane.plane ? comp_plane.plane->id() : -1)
         << " source_layer=";
    if (comp_plane.source_layer <= DrmCompositionPlane::kSourceLayerMax) {
      *out << comp_plane.source_layer;
    } else {
      switch (comp_plane.source_layer) {
        case DrmCompositionPlane::kSourceNone:
          *out << "NONE";
          break;
        case DrmCompositionPlane::kSourcePreComp:
          *out << "PRECOMP";
          break;
        case DrmCompositionPlane::kSourceSquash:
          *out << "SQUASH";
          break;
        default:
          *out << "<invalid>";
          break;
      }
    }

    *out << "\n";
#endif
  }

  *out << "    Squash Regions: count=" << squash_regions_.size() << "\n";
  for (size_t i = 0; i < squash_regions_.size(); i++) {
    *out << "      [" << i << "] ";
    DumpRegion(squash_regions_[i], out);
    *out << "\n";
  }

  *out << "    Pre-Comp Regions: count=" << pre_comp_regions_.size() << "\n";
  for (size_t i = 0; i < pre_comp_regions_.size(); i++) {
    *out << "      [" << i << "] ";
    DumpRegion(pre_comp_regions_[i], out);
    *out << "\n";
  }
}

}
