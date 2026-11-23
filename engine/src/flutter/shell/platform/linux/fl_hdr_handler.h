// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_HDR_HANDLER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_HDR_HANDLER_H_

#include "flutter/shell/platform/linux/public/flutter_linux/fl_binary_messenger.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(FlHdrHandler, fl_hdr_handler, FL, HDR_HANDLER, GObject);

/**
 * FlHdrHandler:
 *
 * #FlHdrHandler answers the flutter/hdr channel. The GTK embedder presents
 * through an 8-bit SDR surface, so it reports no extended range capability
 * and keeps presentation in SDR.
 */

/**
 * fl_hdr_handler_new:
 * @messenger: an #FlBinaryMessenger.
 *
 * Creates a new handler for the flutter/hdr channel.
 *
 * Returns: a new #FlHdrHandler.
 */
FlHdrHandler* fl_hdr_handler_new(FlBinaryMessenger* messenger);

G_END_DECLS

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_HDR_HANDLER_H_
