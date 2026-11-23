// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_LINEAR_OUTPUT_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_LINEAR_OUTPUT_H_

namespace flutter {

// Scale applied to linear values written into F16 (linear scRGB) render
// targets, so that SDR white (1.0) lands on the display's SDR white level.
// scRGB defines 1.0 as 80 nits; an HDR desktop shows SDR white brighter.
// Defaults to 1.0. Affects render targets created after the call.
void SetLinearOutputScale(float scale);

float GetLinearOutputScale();

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_LINEAR_OUTPUT_H_
