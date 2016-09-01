/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "hwc-platform"

#include "drmresources.h"
#include "platform.h"

#include <cutils/log.h>

namespace android {

std::vector<DrmPlane *> Planner::GetUsablePlanes(
    DrmCrtc *crtc, std::vector<DrmPlane *> *primary_planes,
    std::vector<DrmPlane *> *overlay_planes) {
  std::vector<DrmPlane *> usable_planes;
  std::copy_if(primary_planes->begin(), primary_planes->end(),
               std::back_inserter(usable_planes),
               [=](DrmPlane *plane) { return plane->GetCrtcSupported(*crtc); });
  std::copy_if(overlay_planes->begin(), overlay_planes->end(),
               std::back_inserter(usable_planes),
               [=](DrmPlane *plane) { return plane->GetCrtcSupported(*crtc); });
  return usable_planes;
}

#if RK_DRM_HWC
static void rkSetPlaneFlag(DrmCrtc *crtc, DrmPlane* plane) {
    DrmResources* drm = crtc->getDrmReoources();
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();

    //loop plane groups.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
        //loop plane
        for(std::vector<DrmPlane*> ::const_iterator iter_plane=(*iter)->planes.begin();
            !(*iter)->planes.empty() && iter_plane != (*iter)->planes.end(); ++iter_plane)
        {
            //only set the special crtc's plane
            if((*iter_plane)->GetCrtcSupported(*crtc) && plane == (*iter_plane))
            {
                (*iter)->bUse = true;
                (*iter_plane)->set_use(true);
            }
        }
    }
}

std::vector<DrmPlane *> Planner::rkGetUsablePlanes(DrmCrtc *crtc) {
    DrmResources* drm = crtc->getDrmReoources();
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    std::vector<DrmPlane *> usable_planes;
    //loop plane groups.
    for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
            if(!(*iter)->bUse)
                //only count the first plane in plane group.
                std::copy_if((*iter)->planes.begin(), (*iter)->planes.begin()+1,
                       std::back_inserter(usable_planes),
                       [=](DrmPlane *plane) { return !plane->is_use() && plane->GetCrtcSupported(*crtc); });
  }
  return usable_planes;
}
#endif

std::tuple<int, std::vector<DrmCompositionPlane>> Planner::ProvisionPlanes(
    std::map<size_t, DrmHwcLayer *> &layers, bool use_squash_fb, DrmCrtc *crtc,
    std::vector<DrmPlane *> *primary_planes,
    std::vector<DrmPlane *> *overlay_planes) {
  std::vector<DrmCompositionPlane> composition;
#if RK_DRM_HWC
  std::vector<DrmPlane *> planes = rkGetUsablePlanes(crtc);
  UN_USED(primary_planes);
  UN_USED(overlay_planes);
#else
  std::vector<DrmPlane *> planes =
      GetUsablePlanes(crtc, primary_planes, overlay_planes);
#endif
  if (planes.empty()) {
    ALOGE("Display %d has no usable planes", crtc->display());
    return std::make_tuple(-ENODEV, std::vector<DrmCompositionPlane>());
  }

#if USE_SQUASH
  // If needed, reserve the squash plane at the highest z-order
  DrmPlane *squash_plane = NULL;
  if (use_squash_fb) {
    if (!planes.empty()) {
      squash_plane = planes.back();
#if RK_DRM_HWC
      rkSetPlaneFlag(crtc,squash_plane);
#endif
      planes.pop_back();
    } else {
      ALOGI("Not enough planes to reserve for squash fb");
    }
  }
#endif

#if USE_PRE_COMP
  // If needed, reserve the precomp plane at the next highest z-order
  DrmPlane *precomp_plane = NULL;
  if (layers.size() > planes.size()) {
    if (!planes.empty()) {
      precomp_plane = planes.back();
#if RK_DRM_HWC
      rkSetPlaneFlag(crtc,precomp_plane);
#endif
      planes.pop_back();
      composition.emplace_back(DrmCompositionPlane::Type::kPrecomp,
                               precomp_plane, crtc);
    } else {
      ALOGE("Not enough planes to reserve for precomp fb");
    }
  }
#endif

  // Go through the provisioning stages and provision planes
  for (auto &i : stages_) {
    int ret = i->ProvisionPlanes(&composition, layers, crtc, &planes);
    if (ret) {
#if USE_PRE_COMP & RK_DRM_HWC
        if (!planes.empty()) {
              precomp_plane = planes.back();
              if(!precomp_plane->is_use()) {
                  rkSetPlaneFlag(crtc,precomp_plane);
                  planes.pop_back();
                  composition.emplace_back(DrmCompositionPlane::Type::kPrecomp,
                                           precomp_plane, crtc);
                  // Put the rest of the layers in the precomp plane
                  DrmCompositionPlane *precomp = i->GetPrecomp(&composition);
                  if (precomp) {
                        for (auto j = layers.begin(); j != layers.end(); j++) {
                            if(!j->second->is_take)
                            {
                                ALOGD_IF(log_level(DBG_DEBUG),"line=%d,using precomp for layer=%s",__LINE__,
                                    j->second->name.c_str());
                                precomp->source_layers().emplace_back(j->first);
                            }
                        }
                        //Fix squash layer show nothing when enter precomp case
                        break;
                   } else {
                        ALOGE("line=%d precomp is null",__LINE__);
                   }
              } else {
                ALOGE("line=%d the last plane is using",__LINE__);
              }
          } else {
            ALOGE("line=%d planes is empty",__LINE__);
          }
#endif
      ALOGE("Failed provision stage with ret %d", ret);
      return std::make_tuple(ret, std::vector<DrmCompositionPlane>());
    }
  }


#if USE_SQUASH
  if (squash_plane)
    composition.emplace_back(DrmCompositionPlane::Type::kSquash, squash_plane,
                             crtc);
#endif

  return std::make_tuple(0, std::move(composition));
}


//According to zpos and combine layer count,find the suitable plane.
bool Planner::MatchPlane(std::vector<DrmHwcLayer*>& layer_vector,
                               uint64_t* zpos,
                               DrmCrtc *crtc,
                               DrmResources *drm,
                               std::vector<DrmCompositionPlane>* composition_plane)
{
    uint32_t combine_layer_count = 0;
    uint32_t layer_size = layer_vector.size();
    bool b_yuv,b_scale;
    std::vector<PlaneGroup *> ::const_iterator iter;
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    uint64_t rotation = 0;
    uint64_t alpha = 0xFF;

    //loop plane groups.
    for (iter = plane_groups.begin();
       iter != plane_groups.end(); ++iter) {
       ALOGD_IF(log_level(DBG_DEBUG),"line=%d,last zpos=%d,plane group zpos=%d,plane group bUse=%d,crtc=0x%x",__LINE__,*zpos,(*iter)->zpos,(*iter)->bUse,(1<<crtc->pipe()));
        //find the match zpos plane group
        if(!(*iter)->bUse && (*iter)->zpos >= *zpos)
        {
            ALOGD_IF(log_level(DBG_DEBUG),"line=%d,layer_size=%d,planes size=%d",__LINE__,layer_size,(*iter)->planes.size());

            //find the match combine layer count with plane size.
            if(layer_size <= (*iter)->planes.size())
            {
                //loop layer
                for(std::vector<DrmHwcLayer*>::const_iterator iter_layer= layer_vector.begin();
                    iter_layer != layer_vector.end();++iter_layer)
                {
                    if((*iter_layer)->is_match)
                        continue;

                    //loop plane
                    for(std::vector<DrmPlane*> ::const_iterator iter_plane=(*iter)->planes.begin();
                        !(*iter)->planes.empty() && iter_plane != (*iter)->planes.end(); ++iter_plane)
                    {
                        ALOGD_IF(log_level(DBG_DEBUG),"line=%d,crtc=0x%x,plane(%d) is_use=%d,possible_crtc_mask=0x%x",__LINE__,(1<<crtc->pipe()),
                                (*iter_plane)->id(),(*iter_plane)->is_use(),(*iter_plane)->get_possible_crtc_mask());
                        if(!(*iter_plane)->is_use() && (*iter_plane)->GetCrtcSupported(*crtc))
                        {
#if 1
                            b_yuv  = (*iter_plane)->get_yuv();
                            if((*iter_layer)->is_yuv && !b_yuv)
                            {
                                ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) cann't support yuv",(*iter_plane)->id());
                                continue;
                            }
#endif
                            b_scale = (*iter_plane)->get_scale();
                            if((*iter_layer)->is_scale && !b_scale)
                            {
                                ALOGD_IF(log_level(DBG_DEBUG),"Plane(%d) cann't support scale",(*iter_plane)->id());
                                continue;
                            }

                            if ((*iter_layer)->blending == DrmHwcBlending::kPreMult)
                                alpha = (*iter_layer)->alpha;
                            if(alpha != 0xFF && (*iter_plane)->alpha_property().id() == 0)
                            {
                                ALOGV("layer name=%s,plane id=%d",(*iter_layer)->name.c_str(),(*iter_plane)->id());
                                ALOGV("layer alpha=0x%x,alpha id=%d",(*iter_layer)->alpha,(*iter_plane)->alpha_property().id());
                                continue;
                            }
#if RK_RGA
                                    if(NULL == drm->GetRgaDevice()
#if USE_AFBC_LAYER
                                    || isAfbcInternalFormat((*iter_layer)->internal_format)
#endif
                                    )
#endif
                            {
                                rotation = 0;
                                if ((*iter_layer)->transform & DrmHwcTransform::kFlipH)
                                    rotation |= 1 << DRM_REFLECT_X;
                                if ((*iter_layer)->transform & DrmHwcTransform::kFlipV)
                                    rotation |= 1 << DRM_REFLECT_Y;
                                if ((*iter_layer)->transform & DrmHwcTransform::kRotate90)
                                    rotation |= 1 << DRM_ROTATE_90;
                                else if ((*iter_layer)->transform & DrmHwcTransform::kRotate180)
                                    rotation |= 1 << DRM_ROTATE_180;
                                else if ((*iter_layer)->transform & DrmHwcTransform::kRotate270)
                                    rotation |= 1 << DRM_ROTATE_270;
                                if(rotation && !(rotation & (*iter_plane)->get_rotate()))
                                    continue;
                            }

                            ALOGD_IF(log_level(DBG_DEBUG),"MatchPlane: match layer=%s,plane=%d,(*iter_layer)->index=%d",(*iter_layer)->name.c_str(),
                                (*iter_plane)->id(),(*iter_layer)->index);
                            //Find the match plane for layer,it will be commit.
                            composition_plane->emplace_back(DrmCompositionPlane::Type::kLayer, (*iter_plane), crtc, (*iter_layer)->index);
                            (*iter_layer)->is_match = true;
                            (*iter_plane)->set_use(true);

                            combine_layer_count++;
                            break;

                        }
                    }
                }
                if(combine_layer_count == layer_size)
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


std::tuple<int, std::vector<DrmCompositionPlane>> Planner::MatchPlanes(
  std::map<int, std::vector<DrmHwcLayer*>> &layer_map,
  DrmCrtc *crtc,
  DrmResources *drm)
{
    std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
    uint64_t last_zpos=0;
    bool bMatch = false;
    uint32_t planes_can_use=0;
    std::vector<DrmCompositionPlane> composition_plane;
    for (LayerMap::iterator iter = layer_map.begin();
        iter != layer_map.end(); ++iter) {
        bMatch = MatchPlane(iter->second, &last_zpos, crtc, drm, &composition_plane);
        if(!bMatch)
        {
            ALOGV("Cann't find the match plane for layer group %d",iter->first);
            break;
        }
    }

    //If it cann't match any layer after area assign process,we need rollback the area assign process.
    if(!bMatch)
    {
#if RK_DRM_HWC
        //set use flag to false.
        for (std::vector<PlaneGroup *> ::const_iterator iter = plane_groups.begin();
           iter != plane_groups.end(); ++iter) {
            (*iter)->bUse=false;

            for(std::vector<DrmPlane *> ::const_iterator iter_plane=(*iter)->planes.begin();
                iter_plane != (*iter)->planes.end(); ++iter_plane) {
                if((*iter_plane)->GetCrtcSupported(*crtc))
                    (*iter_plane)->set_use(false);
            }
        }
#endif
        composition_plane.clear();
        return std::make_tuple(-1, std::vector<DrmCompositionPlane>());
    }

    return std::make_tuple(0, std::move(composition_plane));
}

int PlanStageProtected::ProvisionPlanes(
    std::vector<DrmCompositionPlane> *composition,
    std::map<size_t, DrmHwcLayer *> &layers, DrmCrtc *crtc,
    std::vector<DrmPlane *> *planes) {
  int ret;
  int protected_zorder = -1;
  for (auto i = layers.begin(); i != layers.end();) {
    if (!i->second->protected_usage()) {
      ++i;
      continue;
    }

    ret = Emplace(composition, planes, DrmCompositionPlane::Type::kLayer, crtc,
                  i->first);
    if (ret)
      ALOGE("Failed to dedicate protected layer! Dropping it.");

    protected_zorder = i->first;
    i = layers.erase(i);
  }

  if (protected_zorder == -1)
    return 0;

  // Add any layers below the protected content to the precomposition since we
  // need to punch a hole through them.
  for (auto i = layers.begin(); i != layers.end();) {
    // Skip layers above the z-order of the protected content
    if (i->first > static_cast<size_t>(protected_zorder)) {
      ++i;
      continue;
    }

    // If there's no precomp layer already queued, queue one now.
    DrmCompositionPlane *precomp = GetPrecomp(composition);
    if (precomp) {
      precomp->source_layers().emplace_back(i->first);
    } else {
      if (!planes->empty()) {
        DrmPlane *precomp_plane = planes->back();
        planes->pop_back();
        composition->emplace_back(DrmCompositionPlane::Type::kPrecomp,
                                  precomp_plane, crtc, i->first);
      } else {
        ALOGE("Not enough planes to reserve for precomp fb");
      }
    }
    i = layers.erase(i);
  }
  return 0;
}

#if RK_DRM_HWC
    int Planner::PlanStage::TakePlane(std::vector<DrmCompositionPlane> *composition,
                       std::vector<DrmPlane *> *planes,
                       DrmCompositionPlane::Type type, DrmCrtc *crtc,
                       size_t source_layer,
                       DrmHwcLayer* layer)
    {
        bool b_yuv,b_scale;
        DrmResources* drm = crtc->getDrmReoources();
        std::vector<PlaneGroup *>& plane_groups = drm->GetPlaneGroups();
        uint64_t rotation = 0;
        uint64_t alpha = 0xFF;

        UN_USED(planes);

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
#if 1
                                    b_yuv  = (*iter_plane)->get_yuv();
                                    if(layer->is_yuv && !b_yuv)
                                        continue;
#endif
                                    b_scale = (*iter_plane)->get_scale();
                                    if(layer->is_scale && !b_scale)
                                        continue;

                                   if (layer->blending == DrmHwcBlending::kPreMult)
                                        alpha = layer->alpha;
                                    if(alpha != 0xFF && (*iter_plane)->alpha_property().id() == 0)
                                    {
                                        ALOGV("layer name=%s,plane id=%d",layer->name.c_str(),(*iter_plane)->id());
                                        ALOGV("layer alpha=0x%x,alpha id=%d",layer->alpha,(*iter_plane)->alpha_property().id());
                                        continue;
                                    }
#if RK_RGA
                                    if(NULL == drm->GetRgaDevice()
#if USE_AFBC_LAYER
                                    || isAfbcInternalFormat(layer->internal_format)
#endif
                                    )
#endif
                                    {
                                        rotation = 0;
                                        if (layer->transform & DrmHwcTransform::kFlipH)
                                            rotation |= 1 << DRM_REFLECT_X;
                                        if (layer->transform & DrmHwcTransform::kFlipV)
                                            rotation |= 1 << DRM_REFLECT_Y;
                                        if (layer->transform & DrmHwcTransform::kRotate90)
                                            rotation |= 1 << DRM_ROTATE_90;
                                        else if (layer->transform & DrmHwcTransform::kRotate180)
                                            rotation |= 1 << DRM_ROTATE_180;
                                        else if (layer->transform & DrmHwcTransform::kRotate270)
                                            rotation |= 1 << DRM_ROTATE_270;
                                        if(rotation && !(rotation & (*iter_plane)->get_rotate()))
                                            continue;
                                    }

                                    layer->is_take = true;  //mark layer which take a plane.
                                    ALOGD_IF(log_level(DBG_DEBUG),"TakePlane: match layer=%s,plane=%d,layer->index=%d",
                                            layer->name.c_str(),(*iter_plane)->id(),layer->index);
                                }

                                (*iter_plane)->set_use(true);
                                (*iter)->bUse = true;
#if USE_PRE_COMP
                                auto precomp = GetPrecompIter(composition);
                                composition->emplace(precomp, type, (*iter_plane), crtc, source_layer);
#else
                                composition->emplace_back(type, (*iter_plane), crtc, source_layer);
#endif
                                return 0;
                            }
                        }
                }
        }

        if(layer)
                ALOGD_IF(log_level(DBG_DEBUG),"TakePlane: cann't match layer=%s,layer->index=%d",
                        layer->name.c_str(),layer->index);

#if RK_EARLY_PRECOMP
  // reserved plane for precomp when find failed plane earlyer.
  DrmCompositionPlane *precomp = GetPrecomp(composition);
  if (!precomp) {
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
                                DrmPlane *precomp_plane = (*iter_plane);
                                rkSetPlaneFlag(crtc,precomp_plane);
                                composition->emplace_back(DrmCompositionPlane::Type::kPrecomp,
                                                        precomp_plane, crtc);
                                return -1;
                             }
                   }
                }
         }
  }
#endif

        return -1;
    }
#endif

int PlanStageGreedy::ProvisionPlanes(
    std::vector<DrmCompositionPlane> *composition,
    std::map<size_t, DrmHwcLayer *> &layers, DrmCrtc *crtc,
    std::vector<DrmPlane *> *planes) {
  // Fill up the remaining planes
#if RK_DRM_HWC
  int iEmplaceFail=0;
  for (auto i = layers.begin(); i != layers.end(); ++i) {
    int ret = TakePlane(composition, planes, DrmCompositionPlane::Type::kLayer,
                      crtc, i->first,i->second);
#else
  for (auto i = layers.begin(); i != layers.end(); i = layers.erase(i)) {
    int ret = Emplace(composition, planes, DrmCompositionPlane::Type::kLayer,
                      crtc, i->first);
#endif
    // We don't have any planes left
    if (ret == -ENOENT)
      break;
    else if (ret)
    {
        ALOGD_IF(log_level(DBG_DEBUG),"Failed to emplace layer %zu, dropping it", i->first);
        iEmplaceFail++;
    }
  }

#if USE_PRE_COMP
  // Put the rest of the layers in the precomp plane
  DrmCompositionPlane *precomp = GetPrecomp(composition);
  if (precomp) {
#if RK_DRM_HWC
    for (auto i = layers.begin(); i != layers.end(); i++) {
#else
    for (auto i = layers.begin(); i != layers.end(); i = layers.erase(i)) {
#endif

#if RK_DRM_HWC
        if(!i->second->is_take)
        {
            iEmplaceFail--;
            precomp->source_layers().emplace_back(i->first);
            ALOGD_IF(log_level(DBG_DEBUG),"line=%d,using precomp for layer=%s",__LINE__,
                i->second->name.c_str());
        }
#else
        precomp->source_layers().emplace_back(i->first);
#endif
    }
  }
#endif

#if RK_DRM_HWC
  //If it still contain layers which are not take.
  if(iEmplaceFail)
    return -1;
#endif

  return 0;
}
}
