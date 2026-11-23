// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_HDR_HANDLER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_HDR_HANDLER_H_

#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/binary_messenger.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/encodable_value.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/method_channel.h"

namespace flutter {

class FlutterWindowsEngine;

// Answers the flutter/hdr channel.
//
// Reports whether the implicit view presents through an F16 linear scRGB
// swap chain and the headroom of its monitor (peak luminance over the SDR
// white level when the desktop is in HDR mode, else 1.0). Also keeps the
// engine's HDR output scale in step with the SDR white level so SDR white
// renders at the level the desktop uses for SDR content.
//
// The swap chain stays scRGB for the lifetime of the surface, so
// setEnabled only records the requested state; it does not clip content
// above SDR white.
class HdrHandler {
 public:
  HdrHandler(BinaryMessenger* messenger, FlutterWindowsEngine* engine);

  // Re-reads the implicit view's monitor and updates the output scale.
  void Refresh();

 private:
  void HandleMethodCall(const MethodCall<EncodableValue>& method_call,
                        std::unique_ptr<MethodResult<EncodableValue>> result);

  // Re-reads the monitor state, updates the output scale and returns the
  // capabilities map.
  EncodableValue Capabilities();

  std::unique_ptr<MethodChannel<EncodableValue>> channel_;

  FlutterWindowsEngine* engine_;

  bool enabled_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(HdrHandler);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_HDR_HANDLER_H_
