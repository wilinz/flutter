// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_HDR_HANDLER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_HDR_HANDLER_H_

#include "flutter/shell/platform/linux/public/flutter_linux/fl_engine.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(FlHdrHandler, fl_hdr_handler, FL, HDR_HANDLER, GObject);

/**
 * FlHdrHandler:
 *
 * #FlHdrHandler answers the flutter/hdr channel for the view addressed by the
 * optional "viewId" argument, or the implicit view.
 * Extended range presentation needs the Wayland subsurface renderer and a
 * compositor with wp_color_manager_v1; otherwise it reports SDR only.
 */

/**
 * fl_hdr_handler_new:
 * @engine: the #FlEngine the handler belongs to. Not referenced, the engine
 * owns the handler.
 *
 * Creates a new handler for the flutter/hdr channel.
 *
 * Returns: a new #FlHdrHandler.
 */
FlHdrHandler* fl_hdr_handler_new(FlEngine* engine);

G_END_DECLS

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_HDR_HANDLER_H_
