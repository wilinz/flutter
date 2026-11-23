// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_subsurface.h"

#include <wayland-client.h>

#include <unistd.h>

#include <cmath>

#include "flutter/shell/platform/linux/protocols/color-management-v1-client-protocol.h"

// Luminances anchoring linear content: 1.0 is the reference white (80 cd/m²,
// the sRGB default). The minimum is the sRGB default of 0.2 cd/m², in the
// protocol's 1/10000 units.
static constexpr uint32_t kReferenceLuminance = 80;
static constexpr uint32_t kMinLuminance = 2000;

struct _FlSubsurface {
  GObject parent_instance;

  // Surface backing the subsurface and the subsurface itself.
  struct wl_surface* surface;
  struct wl_subsurface* subsurface;

  // Color management, all NULL when the surface can't be tagged. The objects
  // live on [queue] so they can be waited on from any thread without
  // dispatching GTK's events. [mutex] serializes their use.
  GMutex mutex;
  struct wl_display* display;
  struct wl_event_queue* queue;
  struct wp_color_manager_v1* manager_wrapper;
  struct wp_color_management_surface_v1* color_surface;
  struct wp_color_management_surface_feedback_v1* feedback;
  gboolean can_set_luminances;
  uint32_t render_intent;

  // The image description set on the surface, NULL when untagged, and the
  // headroom it was created for.
  struct wp_image_description_v1* description;
  double description_headroom;
};

// State of an image description being waited for.
typedef struct {
  gboolean done;
  gboolean ready;
} DescriptionWait;

static void description_failed(void* data,
                               struct wp_image_description_v1* description,
                               uint32_t cause,
                               const char* msg) {
  DescriptionWait* wait = static_cast<DescriptionWait*>(data);
  g_warning("Image description failed: %s", msg);
  wait->done = TRUE;
}

static void description_ready(void* data,
                              struct wp_image_description_v1* description,
                              uint32_t identity) {
  DescriptionWait* wait = static_cast<DescriptionWait*>(data);
  wait->ready = TRUE;
  wait->done = TRUE;
}

static const struct wp_image_description_v1_listener kDescriptionListener = {
    .failed = description_failed,
    .ready = description_ready,
};

// Waits for @description to become ready. Returns FALSE (and destroys it) if
// it failed. Must be called with the mutex held.
static gboolean wait_for_description(
    FlSubsurface* self,
    struct wp_image_description_v1* description) {
  DescriptionWait wait = {};
  wp_image_description_v1_add_listener(description, &kDescriptionListener,
                                       &wait);
  while (!wait.done) {
    if (wl_display_dispatch_queue(self->display, self->queue) < 0) {
      break;
    }
  }
  if (!wait.ready) {
    wp_image_description_v1_destroy(description);
    return FALSE;
  }
  return TRUE;
}

// Luminances reported by an image description's information.
typedef struct {
  gboolean done;
  uint32_t reference;
  uint32_t max;
  uint32_t target_max;
} DescriptionInfo;

static void info_done(void* data, struct wp_image_description_info_v1* info) {
  static_cast<DescriptionInfo*>(data)->done = TRUE;
}

static void info_icc_file(void* data,
                          struct wp_image_description_info_v1* info,
                          int32_t icc,
                          uint32_t icc_size) {
  close(icc);
}

static void info_primaries(void* data,
                           struct wp_image_description_info_v1* info,
                           int32_t r_x,
                           int32_t r_y,
                           int32_t g_x,
                           int32_t g_y,
                           int32_t b_x,
                           int32_t b_y,
                           int32_t w_x,
                           int32_t w_y) {}

static void info_primaries_named(void* data,
                                 struct wp_image_description_info_v1* info,
                                 uint32_t primaries) {}

static void info_tf_power(void* data,
                          struct wp_image_description_info_v1* info,
                          uint32_t eexp) {}

static void info_tf_named(void* data,
                          struct wp_image_description_info_v1* info,
                          uint32_t tf) {}

static void info_luminances(void* data,
                            struct wp_image_description_info_v1* info,
                            uint32_t min_lum,
                            uint32_t max_lum,
                            uint32_t reference_lum) {
  DescriptionInfo* result = static_cast<DescriptionInfo*>(data);
  result->max = max_lum;
  result->reference = reference_lum;
}

static void info_target_luminance(void* data,
                                  struct wp_image_description_info_v1* info,
                                  uint32_t min_lum,
                                  uint32_t max_lum) {
  static_cast<DescriptionInfo*>(data)->target_max = max_lum;
}

static void info_target_max_cll(void* data,
                                struct wp_image_description_info_v1* info,
                                uint32_t max_cll) {}

static void info_target_max_fall(void* data,
                                 struct wp_image_description_info_v1* info,
                                 uint32_t max_fall) {}

static const struct wp_image_description_info_v1_listener kInfoListener = {
    .done = info_done,
    .icc_file = info_icc_file,
    .primaries = info_primaries,
    .primaries_named = info_primaries_named,
    .tf_power = info_tf_power,
    .tf_named = info_tf_named,
    .luminances = info_luminances,
    .target_primaries = info_primaries,
    .target_luminance = info_target_luminance,
    .target_max_cll = info_target_max_cll,
    .target_max_fall = info_target_max_fall,
};

G_DEFINE_TYPE(FlSubsurface, fl_subsurface, G_TYPE_OBJECT)

static void fl_subsurface_dispose(GObject* object) {
  FlSubsurface* self = FL_SUBSURFACE(object);

  g_clear_pointer(&self->description, wp_image_description_v1_destroy);
  g_clear_pointer(&self->feedback,
                  wp_color_management_surface_feedback_v1_destroy);
  g_clear_pointer(&self->color_surface, wp_color_management_surface_v1_destroy);
  if (self->manager_wrapper != nullptr) {
    wl_proxy_wrapper_destroy(self->manager_wrapper);
    self->manager_wrapper = nullptr;
  }
  g_clear_pointer(&self->queue, wl_event_queue_destroy);
  g_clear_pointer(&self->subsurface, wl_subsurface_destroy);
  g_clear_pointer(&self->surface, wl_surface_destroy);

  G_OBJECT_CLASS(fl_subsurface_parent_class)->dispose(object);
}

static void fl_subsurface_finalize(GObject* object) {
  FlSubsurface* self = FL_SUBSURFACE(object);

  g_mutex_clear(&self->mutex);

  G_OBJECT_CLASS(fl_subsurface_parent_class)->finalize(object);
}

static void fl_subsurface_class_init(FlSubsurfaceClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_subsurface_dispose;
  G_OBJECT_CLASS(klass)->finalize = fl_subsurface_finalize;
}

static void fl_subsurface_init(FlSubsurface* self) {
  g_mutex_init(&self->mutex);
}

FlSubsurface* fl_subsurface_new(
    struct wl_compositor* compositor,
    struct wl_subcompositor* subcompositor,
    struct wl_surface* parent_surface,
    const FlSubsurfaceColorManagement* color_management) {
  FlSubsurface* self =
      FL_SUBSURFACE(g_object_new(fl_subsurface_get_type(), nullptr));

  self->surface = wl_compositor_create_surface(compositor);
  self->subsurface = wl_subcompositor_get_subsurface(
      subcompositor, self->surface, parent_surface);

  // Give the subsurface an empty input region so pointer, touch and keyboard
  // events pass through to the parent (GTK) surface, which handles all input
  // for the view.
  struct wl_region* input_region = wl_compositor_create_region(compositor);
  wl_surface_set_input_region(self->surface, input_region);
  wl_region_destroy(input_region);

  if (color_management != nullptr && color_management->manager != nullptr) {
    self->display = color_management->display;
    self->can_set_luminances = color_management->can_set_luminances;
    self->render_intent = color_management->render_intent;
    self->queue = wl_display_create_queue(self->display);
    self->manager_wrapper = static_cast<struct wp_color_manager_v1*>(
        wl_proxy_create_wrapper(color_management->manager));
    wl_proxy_set_queue(
        reinterpret_cast<struct wl_proxy*>(self->manager_wrapper), self->queue);
    self->color_surface =
        wp_color_manager_v1_get_surface(self->manager_wrapper, self->surface);
    self->feedback = wp_color_manager_v1_get_surface_feedback(
        self->manager_wrapper, self->surface);
  }

  return self;
}

gboolean fl_subsurface_supports_extended_range(FlSubsurface* self) {
  g_return_val_if_fail(FL_IS_SUBSURFACE(self), FALSE);
  return self->color_surface != nullptr;
}

gboolean fl_subsurface_set_extended_range(FlSubsurface* self,
                                          gboolean linear,
                                          double headroom) {
  g_return_val_if_fail(FL_IS_SUBSURFACE(self), FALSE);

  if (self->color_surface == nullptr) {
    return !linear;
  }

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  if (!linear) {
    if (self->description != nullptr) {
      wp_color_management_surface_v1_unset_image_description(
          self->color_surface);
      g_clear_pointer(&self->description, wp_image_description_v1_destroy);
    }
    return TRUE;
  }

  // Without set_luminances the primary volume ends at reference white.
  if (!self->can_set_luminances || headroom < 1.0) {
    headroom = 1.0;
  }
  if (self->description != nullptr &&
      std::fabs(self->description_headroom - headroom) < 0.01) {
    return TRUE;
  }

  struct wp_image_description_creator_params_v1* creator =
      wp_color_manager_v1_create_parametric_creator(self->manager_wrapper);
  wp_image_description_creator_params_v1_set_tf_named(
      creator, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR);
  wp_image_description_creator_params_v1_set_primaries_named(
      creator, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
  if (self->can_set_luminances) {
    wp_image_description_creator_params_v1_set_luminances(
        creator, kMinLuminance,
        static_cast<uint32_t>(std::lround(kReferenceLuminance * headroom)),
        kReferenceLuminance);
  }
  struct wp_image_description_v1* description =
      wp_image_description_creator_params_v1_create(creator);
  if (!wait_for_description(self, description)) {
    return FALSE;
  }

  wp_color_management_surface_v1_set_image_description(
      self->color_surface, description, self->render_intent);
  g_clear_pointer(&self->description, wp_image_description_v1_destroy);
  self->description = description;
  self->description_headroom = headroom;
  return TRUE;
}

double fl_subsurface_get_headroom(FlSubsurface* self) {
  g_return_val_if_fail(FL_IS_SUBSURFACE(self), 1.0);

  if (self->feedback == nullptr || !self->can_set_luminances) {
    return 1.0;
  }

  g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);

  struct wp_image_description_v1* preferred =
      wp_color_management_surface_feedback_v1_get_preferred(self->feedback);
  if (!wait_for_description(self, preferred)) {
    return 1.0;
  }

  DescriptionInfo info = {};
  struct wp_image_description_info_v1* info_object =
      wp_image_description_v1_get_information(preferred);
  wp_image_description_info_v1_add_listener(info_object, &kInfoListener, &info);
  while (!info.done) {
    if (wl_display_dispatch_queue(self->display, self->queue) < 0) {
      break;
    }
  }
  // done is a destructor event; the proxy is released here.
  wp_image_description_info_v1_destroy(info_object);
  wp_image_description_v1_destroy(preferred);

  uint32_t peak = info.target_max != 0 ? info.target_max : info.max;
  if (info.reference == 0 || peak <= info.reference) {
    return 1.0;
  }
  return static_cast<double>(peak) / info.reference;
}

struct wl_surface* fl_subsurface_get_surface(FlSubsurface* self) {
  g_return_val_if_fail(FL_IS_SUBSURFACE(self), nullptr);
  return self->surface;
}

void fl_subsurface_set_position(FlSubsurface* self, gint x, gint y) {
  g_return_if_fail(FL_IS_SUBSURFACE(self));
  wl_subsurface_set_position(self->subsurface, x, y);
}

void fl_subsurface_set_sync(FlSubsurface* self) {
  g_return_if_fail(FL_IS_SUBSURFACE(self));
  wl_subsurface_set_sync(self->subsurface);
}

void fl_subsurface_set_desync(FlSubsurface* self) {
  g_return_if_fail(FL_IS_SUBSURFACE(self));
  wl_subsurface_set_desync(self->subsurface);
}
