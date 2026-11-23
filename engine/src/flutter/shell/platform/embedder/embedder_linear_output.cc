// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_linear_output.h"

#include <atomic>

namespace flutter {

namespace {
// Written on the platform thread, read on the raster thread.
std::atomic<float> g_linear_output_scale{1.0f};
}  // namespace

void SetLinearOutputScale(float scale) {
  g_linear_output_scale.store(scale > 0.0f ? scale : 1.0f,
                              std::memory_order_relaxed);
}

float GetLinearOutputScale() {
  return g_linear_output_scale.load(std::memory_order_relaxed);
}

}  // namespace flutter
