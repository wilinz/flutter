// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_wayland_display.h"

#include <gdk/gdkwayland.h>
#include <wayland-client.h>

#include "flutter/shell/platform/linux/protocols/color-management-v1-client-protocol.h"

// Key used to attach the #FlWaylandDisplay to the #GdkDisplay it was created
// for, so the Wayland globals are only bound once per display.
static const char* kWaylandDisplayKey = "fl-wayland-display";

struct _FlWaylandDisplay {
  GObject parent_instance;

  // The Wayland display the globals were bound on.
  struct wl_display* wl_display;

  // Wayland globals used to create subsurfaces.
  struct wl_compositor* compositor;
  struct wl_subcompositor* subcompositor;

  // Optional color manager, NULL if the compositor doesn't provide one.
  struct wp_color_manager_v1* color_manager;

  // Color manager capabilities needed to present extended linear sRGB.
  gboolean has_parametric;
  gboolean has_set_luminances;
  gboolean has_ext_linear;
  gboolean has_srgb_primaries;
  gboolean has_perceptual;
  gboolean has_relative;
};

G_DEFINE_TYPE(FlWaylandDisplay, fl_wayland_display, G_TYPE_OBJECT)

// Color manager capability events, sent after binding.
static void color_manager_supported_intent(void* data,
                                           struct wp_color_manager_v1* manager,
                                           uint32_t render_intent) {
  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(data);
  if (render_intent == WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL) {
    self->has_perceptual = TRUE;
  } else if (render_intent == WP_COLOR_MANAGER_V1_RENDER_INTENT_RELATIVE) {
    self->has_relative = TRUE;
  }
}

static void color_manager_supported_feature(void* data,
                                            struct wp_color_manager_v1* manager,
                                            uint32_t feature) {
  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(data);
  if (feature == WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC) {
    self->has_parametric = TRUE;
  } else if (feature == WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES) {
    self->has_set_luminances = TRUE;
  }
}

static void color_manager_supported_tf_named(
    void* data,
    struct wp_color_manager_v1* manager,
    uint32_t tf) {
  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(data);
  if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR) {
    self->has_ext_linear = TRUE;
  }
}

static void color_manager_supported_primaries_named(
    void* data,
    struct wp_color_manager_v1* manager,
    uint32_t primaries) {
  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(data);
  if (primaries == WP_COLOR_MANAGER_V1_PRIMARIES_SRGB) {
    self->has_srgb_primaries = TRUE;
  }
}

static void color_manager_done(void* data,
                               struct wp_color_manager_v1* manager) {}

static const struct wp_color_manager_v1_listener kColorManagerListener = {
    .supported_intent = color_manager_supported_intent,
    .supported_feature = color_manager_supported_feature,
    .supported_tf_named = color_manager_supported_tf_named,
    .supported_primaries_named = color_manager_supported_primaries_named,
    .done = color_manager_done,
};

// Wayland registry handling.
static void registry_global(void* data,
                            struct wl_registry* registry,
                            uint32_t name,
                            const char* interface,
                            uint32_t version) {
  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(data);
  if (g_strcmp0(interface, wl_compositor_interface.name) == 0) {
    self->compositor = static_cast<struct wl_compositor*>(wl_registry_bind(
        registry, name, &wl_compositor_interface, MIN(version, 4)));
  } else if (g_strcmp0(interface, wl_subcompositor_interface.name) == 0) {
    self->subcompositor = static_cast<struct wl_subcompositor*>(
        wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
  } else if (g_strcmp0(interface, wp_color_manager_v1_interface.name) == 0) {
    self->color_manager = static_cast<struct wp_color_manager_v1*>(
        wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1));
    wp_color_manager_v1_add_listener(self->color_manager,
                                     &kColorManagerListener, self);
  }
}

static void registry_global_remove(void* data,
                                   struct wl_registry* registry,
                                   uint32_t name) {}

static const struct wl_registry_listener kRegistryListener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// Binds the Wayland globals required by Flutter. Returns FALSE if any of them
// are not available.
static gboolean bind_globals(FlWaylandDisplay* self,
                             struct wl_display* display) {
  struct wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &kRegistryListener, self);
  wl_display_roundtrip(display);
  wl_registry_destroy(registry);

  // The color manager sends its capabilities once bound.
  if (self->color_manager != nullptr) {
    wl_display_roundtrip(display);
  }

  return self->compositor != nullptr && self->subcompositor != nullptr;
}

static void fl_wayland_display_dispose(GObject* object) {
  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(object);

  g_clear_pointer(&self->color_manager, wp_color_manager_v1_destroy);
  g_clear_pointer(&self->subcompositor, wl_subcompositor_destroy);
  g_clear_pointer(&self->compositor, wl_compositor_destroy);

  G_OBJECT_CLASS(fl_wayland_display_parent_class)->dispose(object);
}

static void fl_wayland_display_class_init(FlWaylandDisplayClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_wayland_display_dispose;
}

static void fl_wayland_display_init(FlWaylandDisplay* self) {}

FlWaylandDisplay* fl_wayland_display_open(struct wl_display* wl_display) {
  FlWaylandDisplay* self =
      FL_WAYLAND_DISPLAY(g_object_new(fl_wayland_display_get_type(), nullptr));
  self->wl_display = wl_display;

  if (!bind_globals(self, wl_display)) {
    g_warning("Required Wayland globals not available");
    g_object_unref(self);
    return nullptr;
  }

  return self;
}

FlWaylandDisplay* fl_wayland_display_get_for_display(GdkDisplay* gdk_display) {
  g_return_val_if_fail(GDK_IS_WAYLAND_DISPLAY(gdk_display), nullptr);

  FlWaylandDisplay* self = FL_WAYLAND_DISPLAY(
      g_object_get_data(G_OBJECT(gdk_display), kWaylandDisplayKey));
  if (self != nullptr) {
    return self;
  }

  self =
      fl_wayland_display_open(gdk_wayland_display_get_wl_display(gdk_display));
  if (self == nullptr) {
    return nullptr;
  }

  // Owned by the #GdkDisplay, so the globals are shared by everything using
  // that display and released with it.
  g_object_set_data_full(G_OBJECT(gdk_display), kWaylandDisplayKey, self,
                         g_object_unref);

  return self;
}

FlSubsurface* fl_wayland_display_create_subsurface(
    FlWaylandDisplay* self,
    struct wl_surface* parent_surface) {
  g_return_val_if_fail(FL_IS_WAYLAND_DISPLAY(self), nullptr);

  FlSubsurfaceColorManagement color_management = {};
  if (fl_wayland_display_supports_extended_range(self)) {
    color_management.display = self->wl_display;
    color_management.manager = self->color_manager;
    color_management.can_set_luminances = self->has_set_luminances;
    // Relative colorimetric keeps the reference white anchored without tone
    // mapping; perceptual is the fallback every compositor supports.
    color_management.render_intent =
        self->has_relative ? WP_COLOR_MANAGER_V1_RENDER_INTENT_RELATIVE
                           : WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL;
  }
  return fl_subsurface_new(self->compositor, self->subcompositor,
                           parent_surface, &color_management);
}

gboolean fl_wayland_display_supports_extended_range(FlWaylandDisplay* self) {
  g_return_val_if_fail(FL_IS_WAYLAND_DISPLAY(self), FALSE);
  return self->color_manager != nullptr && self->has_parametric &&
         self->has_ext_linear && self->has_srgb_primaries &&
         (self->has_perceptual || self->has_relative);
}
