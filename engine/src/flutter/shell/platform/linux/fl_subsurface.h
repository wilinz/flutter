// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_SUBSURFACE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_SUBSURFACE_H_

#include <gtk/gtk.h>

struct wl_compositor;
struct wl_display;
struct wl_subcompositor;
struct wl_surface;
struct wp_color_manager_v1;

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(FlSubsurface, fl_subsurface, FL, SUBSURFACE, GObject)

/**
 * FlSubsurfaceColorManagement:
 * @display: the Wayland display, used to wait for image descriptions.
 * @manager: the color manager, or %NULL to leave the surface untagged.
 * @can_set_luminances: %TRUE if the compositor accepts set_luminances.
 * @render_intent: the wp_color_manager_v1 render intent to use.
 *
 * Color management used to present extended linear sRGB content.
 */
typedef struct {
  struct wl_display* display;
  struct wp_color_manager_v1* manager;
  gboolean can_set_luminances;
  guint32 render_intent;
} FlSubsurfaceColorManagement;

/**
 * FlSubsurface:
 *
 * #FlSubsurface manages a Wayland subsurface attached to another Wayland
 * surface. It is used to render Flutter content directly to a Wayland surface,
 * independently of the GTK widget hierarchy.
 */

/**
 * fl_subsurface_new:
 * @compositor: the Wayland compositor to create the surface with.
 * @subcompositor: the Wayland subcompositor to create the subsurface with.
 * @parent_surface: the Wayland surface to attach the subsurface to.
 * @color_management: (nullable): color management for extended range
 * content, or %NULL.
 *
 * Creates a new Wayland subsurface on @parent_surface.
 *
 * Returns: a new #FlSubsurface.
 */
FlSubsurface* fl_subsurface_new(
    struct wl_compositor* compositor,
    struct wl_subcompositor* subcompositor,
    struct wl_surface* parent_surface,
    const FlSubsurfaceColorManagement* color_management);

/**
 * fl_subsurface_supports_extended_range:
 * @subsurface: an #FlSubsurface.
 *
 * Returns: %TRUE if the subsurface can be tagged as extended linear sRGB.
 */
gboolean fl_subsurface_supports_extended_range(FlSubsurface* subsurface);

/**
 * fl_subsurface_set_extended_range:
 * @subsurface: an #FlSubsurface.
 * @linear: %TRUE if the next buffer holds linear sRGB values, %FALSE if it
 * holds gamma-encoded sRGB values.
 * @headroom: the brightest value shown for linear content, relative to SDR
 * white (1.0 clips at SDR white).
 *
 * Tags the subsurface for the next commit. Linear content is described with
 * the ext_linear transfer function, 1.0 anchored to the compositor's reference
 * white. May be called from any thread; blocks until the compositor has
 * accepted a new image description.
 *
 * Returns: %FALSE if linear content was requested but cannot be tagged.
 */
gboolean fl_subsurface_set_extended_range(FlSubsurface* subsurface,
                                          gboolean linear,
                                          double headroom);

/**
 * fl_subsurface_get_headroom:
 * @subsurface: an #FlSubsurface.
 *
 * Queries the compositor's preferred image description for the subsurface and
 * returns its target peak luminance over its reference white. May be called
 * from any thread.
 *
 * Returns: the headroom, 1.0 if unknown or the surface can't show it.
 */
double fl_subsurface_get_headroom(FlSubsurface* subsurface);

/**
 * fl_subsurface_get_surface:
 * @subsurface: an #FlSubsurface.
 *
 * Gets the Wayland surface backing this subsurface.
 *
 * Returns: a `struct wl_surface`.
 */
struct wl_surface* fl_subsurface_get_surface(FlSubsurface* subsurface);

/**
 * fl_subsurface_set_position:
 * @subsurface: an #FlSubsurface.
 * @x: x coordinate in the parent surface.
 * @y: y coordinate in the parent surface.
 *
 * Moves the subsurface to the given position relative to the parent surface.
 */
void fl_subsurface_set_position(FlSubsurface* subsurface, gint x, gint y);

/**
 * fl_subsurface_set_sync:
 * @subsurface: an #FlSubsurface.
 *
 * Synchronizes the subsurface with its parent surface, so frames committed to
 * it are held back until the parent surface is next committed.
 */
void fl_subsurface_set_sync(FlSubsurface* subsurface);

/**
 * fl_subsurface_set_desync:
 * @subsurface: an #FlSubsurface.
 *
 * Desynchronizes the subsurface from its parent surface, so frames committed
 * to it are shown as soon as they are committed. A frame that was held back
 * while synchronized is shown now.
 */
void fl_subsurface_set_desync(FlSubsurface* subsurface);

G_END_DECLS

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_SUBSURFACE_H_
