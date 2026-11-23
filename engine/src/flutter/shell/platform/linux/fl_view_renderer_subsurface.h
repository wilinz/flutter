// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_VIEW_RENDERER_SUBSURFACE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_VIEW_RENDERER_SUBSURFACE_H_

#include "flutter/shell/platform/linux/fl_view_renderer.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_engine.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(FlViewRendererSubsurface,
                     fl_view_renderer_subsurface,
                     FL,
                     VIEW_RENDERER_SUBSURFACE,
                     FlViewRenderer)

/**
 * FlViewRendererSubsurface:
 *
 * #FlViewRendererSubsurface is an #FlViewRenderer that renders Flutter frames
 * using OpenGL into a Wayland subsurface.
 */

/**
 * fl_view_renderer_subsurface_new:
 * @engine: the #FlEngine to render.
 * @sized_to_content: %TRUE if the view size is controlled by Flutter.
 *
 * Creates a new widget that renders Flutter frames using OpenGL into a Wayland
 * subsurface.
 *
 * Returns: a new #FlViewRendererSubsurface.
 */
FlViewRendererSubsurface* fl_view_renderer_subsurface_new(
    FlEngine* engine,
    gboolean sized_to_content);

/**
 * fl_view_renderer_subsurface_get_hdr_capabilities:
 * @renderer: an #FlViewRendererSubsurface.
 * @surface: (out): %TRUE if frames are presented as extended linear sRGB.
 * @enabled: (out): %TRUE if extended range presentation is enabled.
 * @headroom: (out): the display headroom over SDR white, 1.0 if none.
 *
 * Gets the extended range capabilities of the subsurface, querying the
 * compositor for the current headroom.
 */
void fl_view_renderer_subsurface_get_hdr_capabilities(
    FlViewRendererSubsurface* renderer,
    gboolean* surface,
    gboolean* enabled,
    double* headroom);

/**
 * fl_view_renderer_subsurface_set_hdr_enabled:
 * @renderer: an #FlViewRendererSubsurface.
 * @enabled: %TRUE to show values above SDR white up to the headroom.
 *
 * Enables or disables extended range presentation. Disabled, linear content
 * is clipped at SDR white.
 */
void fl_view_renderer_subsurface_set_hdr_enabled(
    FlViewRendererSubsurface* renderer,
    gboolean enabled);

G_END_DECLS

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_VIEW_RENDERER_SUBSURFACE_H_
