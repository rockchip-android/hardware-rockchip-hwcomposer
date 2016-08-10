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
#include "platform.h"

#include <stdlib.h>

#include <algorithm>
#include <unordered_set>

#include <cutils/log.h>
#include <sw_sync.h>
#include <sync/sync.h>
#include <xf86drmMode.h>

namespace android {

DrmDisplayComposition::~DrmDisplayComposition() {
  if (timeline_fd_ >= 0) {
    SignalCompositionDone();
    close(timeline_fd_);
  }
}

int DrmDisplayComposition::Init(DrmResources *drm, DrmCrtc *crtc,
                                Importer *importer, Planner *planner,
                                uint64_t frame_no) {
  drm_ = drm;
  crtc_ = crtc;  // Can be NULL if we haven't modeset yet
  importer_ = importer;
  planner_ = planner;
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

int DrmDisplayComposition::CreateNextTimelineFence(const char* fence_name) {
  ++timeline_;
  return sw_sync_fence_create(timeline_fd_, fence_name,
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
  composition_planes_.emplace_back(DrmCompositionPlane::Type::kDisable, plane,
                                   crtc_);
  return 0;
}

static std::vector<size_t> SetBitsToVector(
    uint64_t in, const std::vector<size_t> &index_map) {
  std::vector<size_t> out;
  size_t msb = sizeof(in) * 8 - 1;
  uint64_t mask = (uint64_t)1 << msb;
  for (size_t i = msb; mask != (uint64_t)0; i--, mask >>= 1)
    if (in & mask)
      out.push_back(index_map[i]);
  return out;
}

int DrmDisplayComposition::AddPlaneComposition(DrmCompositionPlane plane) {
  composition_planes_.emplace_back(std::move(plane));
  return 0;
}

void DrmDisplayComposition::SeparateLayers(DrmHwcRect<int> *exclude_rects,
                                           size_t num_exclude_rects) {
  DrmCompositionPlane *comp = NULL;
  std::vector<size_t> dedicated_layers;
  // Go through the composition and find the precomp layer as well as any
  // layers that have a dedicated plane located below the precomp layer.
  for (auto &i : composition_planes_) {
    if (i.type() == DrmCompositionPlane::Type::kLayer) {
      dedicated_layers.insert(dedicated_layers.end(), i.source_layers().begin(),
                              i.source_layers().end());
    } else if (i.type() == DrmCompositionPlane::Type::kPrecomp) {
      comp = &i;
      break;
    }
  }
  if (!comp)
    return;

  const std::vector<size_t> &comp_layers = comp->source_layers();
  if (comp_layers.size() > 64) {
    ALOGE("Failed to separate layers because there are more than 64");
    return;
  }

  // Index at which the actual layers begin
  size_t layer_offset = num_exclude_rects + dedicated_layers.size();
  if (comp_layers.size() + layer_offset > 64) {
    ALOGW(
        "Exclusion rectangles are being truncated to make the rectangle count "
        "fit into 64");
    num_exclude_rects = 64 - comp_layers.size() - dedicated_layers.size();
  }

  // We inject all the exclude rects into the rects list. Any resulting rect
  // that includes ANY of the first num_exclude_rects is rejected. After the
  // exclude rects, we add the lower layers. The rects that intersect with
  // these layers will be inspected and only those which are to be composited
  // above the layer will be included in the composition regions.
  std::vector<DrmHwcRect<int>> layer_rects(comp_layers.size() + layer_offset);
  std::copy(exclude_rects, exclude_rects + num_exclude_rects,
            layer_rects.begin());
  std::transform(
      dedicated_layers.begin(), dedicated_layers.end(),
      layer_rects.begin() + num_exclude_rects,
      [=](size_t layer_index) { return layers_[layer_index].display_frame; });
  std::transform(comp_layers.begin(), comp_layers.end(),
                 layer_rects.begin() + layer_offset, [=](size_t layer_index) {
    return layers_[layer_index].display_frame;
  });

  std::vector<separate_rects::RectSet<uint64_t, int>> separate_regions;
  separate_rects::separate_rects_64(layer_rects, &separate_regions);
  uint64_t exclude_mask = ((uint64_t)1 << num_exclude_rects) - 1;
  uint64_t dedicated_mask = (((uint64_t)1 << dedicated_layers.size()) - 1)
                            << num_exclude_rects;

  for (separate_rects::RectSet<uint64_t, int> &region : separate_regions) {
    if (region.id_set.getBits() & exclude_mask)
      continue;

    // If a rect intersects one of the dedicated layers, we need to remove the
    // layers from the composition region which appear *below* the dedicated
    // layer. This effectively punches a hole through the composition layer such
    // that the dedicated layer can be placed below the composition and not
    // be occluded.
    uint64_t dedicated_intersect = region.id_set.getBits() & dedicated_mask;
    for (size_t i = 0; dedicated_intersect && i < dedicated_layers.size();
         ++i) {
      // Only exclude layers if they intersect this particular dedicated layer
      if (!(dedicated_intersect & (1 << (i + num_exclude_rects))))
        continue;

      for (size_t j = 0; j < comp_layers.size(); ++j) {
        if (comp_layers[j] < dedicated_layers[i])
          region.id_set.subtract(j + layer_offset);
      }
    }
    if (!(region.id_set.getBits() >> layer_offset))
      continue;

    pre_comp_regions_.emplace_back(DrmCompositionRegion{
        region.rect,
        SetBitsToVector(region.id_set.getBits() >> layer_offset, comp_layers)});
  }
}

#if RK_DRM_HWC_DEBUG
void DrmCompositionPlane::dump_drm_com_plane(int index, std::ostringstream *out) const {
    *out << "DrmCompositionPlane[" << index << "]"
         << " plane=" << (plane_ ? plane_->id() : -1)
         << " type=";
    switch (type_) {
      case DrmCompositionPlane::Type::kDisable:
        *out << "DISABLE";
        break;
      case DrmCompositionPlane::Type::kLayer:
        *out << "LAYER";
        break;
      case DrmCompositionPlane::Type::kPrecomp:
        *out << "PRECOMP";
        break;
      case DrmCompositionPlane::Type::kSquash:
        *out << "SQUASH";
        break;
      default:
        *out << "<invalid>";
        break;
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
    if (plane.type() == DrmCompositionPlane::Type::kLayer) {
      for (auto i : plane.source_layers()) {
        DrmHwcLayer *source_layer = &layers_[i];
        comp_layers.emplace(source_layer);
        pre_comp_layers.erase(source_layer);
      }
    }
  }

  for (DrmHwcLayer *layer : squash_layers) {
    if (!layer->release_fence)
      continue;
    int ret = layer->release_fence.Set(CreateNextTimelineFence("squash_layers"));
    if (ret < 0)
      return ret;
  }
  timeline_squash_done_ = timeline_;

  for (DrmHwcLayer *layer : pre_comp_layers) {
    if (!layer->release_fence)
      continue;
    int ret = layer->release_fence.Set(CreateNextTimelineFence("pre_comp_layers"));
    if (ret < 0)
      return ret;
  }
  timeline_pre_comp_done_ = timeline_;

  char acBuf[50];
  for (DrmHwcLayer *layer : comp_layers) {
    if (!layer->release_fence)
      continue;
#if RK_VR
    if(layer->release_fence.get() > -1 && (layer->gralloc_buffer_usage & 0x08000000))
    {
        ALOGD_IF(log_level(DBG_DEBUG),">>>close releaseFenceFd:%d,layername=%s",
                    layer->release_fence.get(),layer->name.c_str());
        close(layer->release_fence.get());
        layer->release_fence.Set(-1);
    }
    else
#endif
    {
        sprintf(acBuf,"frame-%d",layer->frame_no);
        int ret = layer->release_fence.Set(CreateNextTimelineFence(acBuf));
        if (ret < 0)
          return ret;
    }
  }

  return 0;
}

static bool is_rec1_intersect_rec2(DrmHwcRect<int>* rec1,DrmHwcRect<int>* rec2)
{
    ALOGD_IF(log_level(DBG_DEBUG),"is_not_intersect: rec1[%d,%d,%d,%d],rec2[%d,%d,%d,%d]",rec1->left,rec1->top,
        rec1->right,rec1->bottom,rec2->left,rec2->top,rec2->right,rec2->bottom);
    if (rec1->left >= rec2->left && rec1->left <= rec2->right) {
        if (rec1->top >= rec2->top && rec1->top <= rec2->bottom) {
            return true;
        }
        if (rec1->bottom >= rec2->top && rec1->bottom <= rec2->bottom) {
            return true;
        }
    }
    if (rec1->right >= rec2->left && rec1->right <= rec2->right) {
        if (rec1->top >= rec2->top && rec1->top <= rec2->bottom) {
            return true;
        }
        if (rec1->bottom >= rec2->top && rec1->bottom <= rec2->bottom) {
            return true;
        }
    }

    return false;
}

static bool is_layer_combine(DrmHwcLayer * layer_one,DrmHwcLayer * layer_two)
{
    //Don't care format.
    if(/*layer_one->format != layer_two->format
        ||*/ layer_one->alpha!= layer_two->alpha
        || layer_one->is_scale || layer_two->is_scale
        || is_rec1_intersect_rec2(&layer_one->display_frame,&layer_two->display_frame)
        || is_rec1_intersect_rec2(&layer_two->display_frame,&layer_one->display_frame))
    {
        ALOGD_IF(log_level(DBG_DEBUG),"is_layer_combine layer one alpha=%d,is_scale=%d",layer_one->alpha,layer_one->is_scale);
        ALOGD_IF(log_level(DBG_DEBUG),"is_layer_combine layer two alpha=%d,is_scale=%d",layer_two->alpha,layer_two->is_scale);
        return false;
    }

    return true;
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

int DrmDisplayComposition::combine_layer()
{
    /*Group layer*/
    int zpos = 0;
    size_t i,j;
    uint32_t sort_cnt=0;
    bool is_combine = false;
    size_t min_size = (MOST_WIN_ZONES<layers_.size())?MOST_WIN_ZONES:layers_.size();

    layer_map_.clear();

    for (i = 0; i < layers_.size(); ) {
        sort_cnt=0;
        if(i == 0)
        {
            layer_map_[zpos].push_back(&layers_[0]);
        }

        if(i == min_size)
        {
            //We can use pre-comp to optimise.
            ALOGD_IF(log_level(DBG_DEBUG),"combine_layer fail: it remain layer i=%d, min_size=%d",i,min_size);
            return -1;
        }

        for(j = i+1; j < min_size; j++) {
            DrmHwcLayer &layer_one = layers_[j];
            layer_one.index = j;
            is_combine = false;
            for(size_t k = 0; k <= sort_cnt; k++ ) {
                DrmHwcLayer &layer_two = layers_[j-1-k];
                layer_two.index = j-1-k;
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
  for (LayerMap::iterator iter = layer_map_.begin();
       iter != layer_map_.end(); ++iter) {
        if(iter->second.size() > 1) {
            for(uint32_t i=0;i < iter->second.size()-1;i++) {
                for(uint32_t j=i+1;j < iter->second.size();j++) {
                     if(iter->second[i]->display_frame.left > iter->second[j]->display_frame.left) {
                        ALOGD_IF(log_level(DBG_DEBUG),"swap %s and %s",iter->second[i]->name.c_str(),iter->second[j]->name.c_str());
                        std::swap(iter->second[i],iter->second[j]);
                        break;
                     }
                 }
            }
        }
  }

#if RK_DRM_HWC_DEBUG
  for (LayerMap::iterator iter = layer_map_.begin();
       iter != layer_map_.end(); ++iter) {
        ALOGD_IF(log_level(DBG_DEBUG),"layer map id=%d,size=%d",iter->first,iter->second.size());
        for(std::vector<DrmHwcLayer*>::const_iterator iter_layer = iter->second.begin();
            iter_layer != iter->second.end();++iter_layer)
        {
             ALOGD_IF(log_level(DBG_DEBUG),"\tlayer name=%s",(*iter_layer)->name.c_str());
        }
  }
#endif

    return 0;
}

int DrmDisplayComposition::Plan(SquashState *squash,
                                std::vector<DrmPlane *> *primary_planes,
                                std::vector<DrmPlane *> *overlay_planes) {
  if (type_ != DRM_COMPOSITION_TYPE_FRAME)
    return 0;

  // Used to track which layers should be sent to the planner. We exclude layers
  // that are entirely squashed so the planner can provision a precomposition
  // layer as appropriate (ex: if 5 layers are squashed and 1 is not, we don't
  // want to plan a precomposition layer that will be comprised of the already
  // squashed layers).
  std::map<size_t, DrmHwcLayer *> to_composite;
  bool use_squash_framebuffer = false;
#if RK_DRM_HWC
  std::vector<PlaneGroup *>& plane_groups = drm_->GetPlaneGroups();

    //set use flag to false.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
        (*iter)->bUse=false;
        for(std::vector<DrmPlane *> ::const_iterator iter_plane=(*iter)->planes.begin();
            iter_plane != (*iter)->planes.end(); ++iter_plane) {
            if((*iter_plane)->GetCrtcSupported(*crtc_))  //only init the special crtc's plane
                (*iter_plane)->set_use(false);
        }
    }
#endif

#if USE_SQUASH
  // Used to determine which layers were entirely squashed
  std::vector<int> layer_squash_area(layers_.size(), 0);
  // Used to avoid rerendering regions that were squashed
  std::vector<DrmHwcRect<int>> exclude_rects;
  if (squash != NULL) {
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

    for (size_t i = 0; i < layers_.size(); ++i) {
      if (layer_squash_area[i] < layers_[i].display_frame.area())
      {
        to_composite.emplace(std::make_pair(i, &layers_[i]));
      }
      else
      {
        layers_[i].is_match = true;
        ALOGD_IF(log_level(DBG_DEBUG),"add Squash layer[%d] %s",i,layers_[i].name.c_str());
      }
    }
  } else {
    for (size_t i = 0; i < layers_.size(); ++i)
      to_composite.emplace(std::make_pair(i, &layers_[i]));
  }
#else
    for (size_t i = 0; i < layers_.size(); ++i)
      to_composite.emplace(std::make_pair(i, &layers_[i]));
#endif

  int ret=0;
#if USE_MULTI_AREAS
  ret = combine_layer();

  if(0==ret)
  {
      std::tie(ret, composition_planes_) = planner_->MatchPlanes(layer_map_,crtc_,drm_);
      if (ret) {
        ALOGD_IF(log_level(DBG_VERBOSE),"Planner failed MatchPlanes planes ret=%d", ret);
      }
  }

#endif

  std::vector<DrmCompositionPlane> plan;
  if(ret)
  {
      std::tie(ret, composition_planes_) =
          planner_->ProvisionPlanes(to_composite, use_squash_framebuffer, crtc_,
                                    primary_planes, overlay_planes);
      if (ret) {
        ALOGE("Planner failed provisioning planes ret=%d", ret);
        return ret;
      }
  }

  // Remove the planes we used from the pool before returning. This ensures they
  // won't be reused by another display in the composition.
  for (auto &i : composition_planes_) {
    if (!i.plane())
      continue;

    std::vector<DrmPlane *> *container;
    if (i.plane()->type() == DRM_PLANE_TYPE_PRIMARY)
      container = primary_planes;
    else
      container = overlay_planes;
    for (auto j = container->begin(); j != container->end(); ++j) {
      if (*j == i.plane()) {
        container->erase(j);
        break;
      }
    }
  }

#if RK_DRM_HWC_DEBUG
  size_t j=0;
  for (const DrmCompositionPlane &plane : composition_planes_) {
    std::ostringstream out;
    plane.dump_drm_com_plane(j,&out);
    ALOGD_IF(log_level(DBG_VERBOSE),"%s",out.str().c_str());
    j++;
  }
#endif

#if USE_SQUASH
  return FinalizeComposition(exclude_rects.data(), exclude_rects.size());
#else
  return FinalizeComposition();
#endif
}

int DrmDisplayComposition::FinalizeComposition() {
  return FinalizeComposition(NULL, 0);
}

int DrmDisplayComposition::FinalizeComposition(DrmHwcRect<int> *exclude_rects,
                                               size_t num_exclude_rects) {
  SeparateLayers(exclude_rects, num_exclude_rects);
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
         << " plane=" << (comp_plane.plane() ? comp_plane.plane()->id() : -1)
         << " type=";
    switch (comp_plane.type()) {
      case DrmCompositionPlane::Type::kDisable:
        *out << "DISABLE";
        break;
      case DrmCompositionPlane::Type::kLayer:
        *out << "LAYER";
        break;
      case DrmCompositionPlane::Type::kPrecomp:
        *out << "PRECOMP";
        break;
      case DrmCompositionPlane::Type::kSquash:
        *out << "SQUASH";
        break;
      default:
        *out << "<invalid>";
        break;
    }

    *out << " source_layer=";
    for (auto i : comp_plane.source_layers()) {
      *out << i << " ";
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
