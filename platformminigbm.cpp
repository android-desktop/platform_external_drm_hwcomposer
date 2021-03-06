/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "hwc-platform-drm-minigbm"

#include "drmresources.h"
#include "platform.h"
#include "platformminigbm.h"

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <log/log.h>
#include <hardware/gralloc.h>

#include "cros_gralloc_handle.h"

namespace android {

Importer *Importer::CreateInstance(DrmResources *drm) {
  DrmMinigbmImporter *importer = new DrmMinigbmImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the minigbm importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}

DrmMinigbmImporter::DrmMinigbmImporter(DrmResources *drm) : DrmGenericImporter(drm), drm_(drm) {
}

DrmMinigbmImporter::~DrmMinigbmImporter() {
}

int DrmMinigbmImporter::Init() {
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc_);
  if (ret) {
    ALOGE("Failed to open gralloc module %d", ret);
    return ret;
  }

  if (strcasecmp(gralloc_->common.author, "Chrome OS"))
    ALOGW("Using non-minigbm gralloc module: %s/%s\n", gralloc_->common.name,
          gralloc_->common.author);

  return 0;
}

EGLImageKHR DrmMinigbmImporter::ImportImage(EGLDisplay egl_display, buffer_handle_t handle) {
  cros_gralloc_handle *gr_handle = (cros_gralloc_handle *)handle;
  if (!gr_handle)
    return NULL;
  EGLint attr[] = {
    EGL_WIDTH, (EGLint)gr_handle->width,
    EGL_HEIGHT, (EGLint)gr_handle->height,
    EGL_LINUX_DRM_FOURCC_EXT, (EGLint)gr_handle->format,
    EGL_DMA_BUF_PLANE0_FD_EXT, gr_handle->fds[0],
    EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)gr_handle->strides[0],
    EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)gr_handle->offsets[0],
    EGL_NONE,
  };
  return eglCreateImageKHR(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attr);
}

int DrmMinigbmImporter::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  cros_gralloc_handle *gr_handle = (cros_gralloc_handle *)handle;
  if (!gr_handle)
    return -EINVAL;

  uint32_t gem_handle;
  int ret = drmPrimeFDToHandle(drm_->fd(), gr_handle->fds[0], &gem_handle);
  if (ret) {
    ALOGE("failed to import prime fd %d ret=%d", gr_handle->fds[0], ret);
    return ret;
  }

  memset(bo, 0, sizeof(hwc_drm_bo_t));
  bo->width = gr_handle->width;
  bo->height = gr_handle->height;
  bo->format = gr_handle->format;
  bo->usage = gr_handle->usage;
  bo->pitches[0] = gr_handle->strides[0];
  bo->offsets[0] = gr_handle->offsets[0];
  bo->gem_handles[0] = gem_handle;

  ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                      bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id, 0);
  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    return ret;
  }

  return ret;
}

std::unique_ptr<Planner> Planner::CreateInstance(DrmResources *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}

}
