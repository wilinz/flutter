// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_EGL_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_EGL_MANAGER_H_

// OpenGL ES and EGL includes
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Windows platform specific includes
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>
#include <memory>
#include <optional>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/windows/egl/context.h"
#include "flutter/shell/platform/windows/egl/surface.h"
#include "flutter/shell/platform/windows/egl/window_surface.h"

namespace flutter {
namespace egl {

enum class GpuPreference {
  NoPreference,
  LowPowerPreference,
  HighPerformancePreference,
};

// A Direct3D feature level, such as 10_0 or 11_1.
struct D3DFeatureLevel {
  int major;
  int minor;
};

// A manager for initializing ANGLE correctly and using it to create and
// destroy surfaces
class Manager {
 public:
  // Creates a manager.
  //
  // If |allow_inverted_surface| is true and ANGLE supports
  // EGL_ANGLE_surface_orientation, window surfaces are created with an
  // inverted Y axis so that the default framebuffer shares Impeller's
  // top-left origin. See |surface_origin_is_top_left|.
  //
  // If |max_feature_level| is set, ANGLE's D3D11 renderer is limited to at
  // most that feature level, even if the hardware supports a higher one. This
  // is intended for testing: capping to 10_0 makes ANGLE expose only an
  // OpenGL ES 2.0 context, emulating older GPUs on any machine.
  static std::unique_ptr<Manager> Create(
      GpuPreference gpu_preference,
      bool allow_inverted_surface,
      std::optional<D3DFeatureLevel> max_feature_level = std::nullopt);

  virtual ~Manager();

  // Whether the manager is currently valid.
  bool IsValid() const;

  // Creates an EGL surface that can be used to render a Flutter view into a
  // win32 HWND.
  //
  // After the surface is created, |WindowSurface::SetVSyncEnabled| should be
  // called on a thread that can make the surface current.
  //
  // HWND is the window backing the surface. Width and height are the surface's
  // physical pixel dimensions.
  //
  // Returns nullptr on failure.
  virtual std::unique_ptr<WindowSurface> CreateWindowSurface(HWND hwnd,
                                                             size_t width,
                                                             size_t height);

  // Check if the current thread has a context bound.
  bool HasContextCurrent();

  // Creates a |EGLSurface| from the provided handle.
  //
  // |is_rgba16float| selects the EGL config used for the pbuffer:
  //   * true  → the RGBA16F config (matches HDR/FP16 shared handles)
  //   * false → the RGBA8 config (matches 8-bit BGRA shared handles,
  //             including media_kit's video output)
  //
  // Without this distinction ANGLE rejects pbuffer creation with
  // EGL_BAD_PARAMETER when the buffer format doesn't match the config,
  // which broke media_kit video after the FP16 config landed.
  // Virtual so unit tests can mock the handle-import path without linking
  // ANGLE — exercised by ExternalTextureD3dTest.
  virtual EGLSurface CreateSurfaceFromHandle(EGLenum handle_type,
                                             EGLClientBuffer handle,
                                             const EGLint* attributes,
                                             bool is_rgba16float = false) const;

  // Gets the |EGLDisplay|.
  virtual EGLDisplay egl_display() const { return display_; };

  // Gets the |EGLConfig|.
  EGLConfig egl_config() const { return config_; };

  // Whether the origin of the default framebuffer (framebuffer 0) is the
  // top-left of the window rather than OpenGL's usual bottom-left.
  //
  // This is true when window surfaces were created with
  // EGL_SURFACE_ORIENTATION_INVERT_Y_ANGLE. Content blitted or drawn into the
  // default framebuffer must be stored top-down in that case.
  virtual bool surface_origin_is_top_left() const;

  // Gets the |ID3D11Device| chosen by ANGLE. Virtual for the same reason
  // as CreateSurfaceFromHandle above.
  virtual bool GetDevice(ID3D11Device** device);

  // Get the EGL context used to render Flutter views.
  virtual Context* render_context() const;

  // Get the EGL context used for async texture uploads.
  virtual Context* resource_context() const;

  // True when ANGLE supplied an RGBA16 float-component config; composed
  // Flutter surfaces should render into F16 backing stores so HDR highlights
  // survive compositing. Callers must mirror this into the GL format they
  // request for backing-store textures.
  virtual bool is_rgba16float() const { return is_rgba16float_; }

  static std::optional<LUID> GetLowPowerGpuLuid();

  static std::optional<LUID> GetHighPerformanceGpuLuid();

 protected:
  // Creates a new surface manager retaining reference to the passed-in target
  // for the lifetime of the manager.
  Manager(GpuPreference gpu_preference,
          bool allow_inverted_surface,
          std::optional<D3DFeatureLevel> max_feature_level = std::nullopt);

 private:
  // Number of active instances of Manager
  static int instance_count_;

  // Helper function to get GPU LUID by preference.
  static std::optional<LUID> GetGpuLuidByPreference(
      DXGI_GPU_PREFERENCE preference);

  // Initialize the EGL display.
  bool InitializeDisplay(GpuPreference gpu_preference,
                         bool allow_inverted_surface,
                         std::optional<D3DFeatureLevel> max_feature_level);

  // Initialize the EGL configs.
  bool InitializeConfig();

  // Initialize the EGL render and resource contexts.
  bool InitializeContexts();

  // Initialize the D3D11 device.
  bool InitializeDevice();

  // Enable Direct3D's multithread protection on ANGLE's D3D11 device so that
  // the render and resource contexts' threads cannot use its immediate
  // context at the same time.
  //
  // See: https://github.com/flutter/flutter/issues/196618
  void EnableMultithreadProtection();

  void CleanUp();

  // Whether the manager was initialized successfully.
  bool is_valid_ = false;

  // The EGL_SURFACE_ORIENTATION_ANGLE value to create window surfaces with.
  //
  // Either 0 or EGL_SURFACE_ORIENTATION_INVERT_Y_ANGLE. Non-zero only when the
  // caller opted in and ANGLE advertised EGL_ANGLE_surface_orientation.
  EGLint surface_orientation_ = 0;

  // EGL representation of native display.
  EGLDisplay display_ = EGL_NO_DISPLAY;

  // EGL framebuffer configuration used for the main Flutter render
  // context (F16 when available, else RGBA8 — same config as
  // |config_rgba8_| in that case).
  EGLConfig config_ = nullptr;

  // RGBA8 EGL framebuffer configuration used for 8-bit shared-handle
  // pbuffer creation (e.g., media_kit's BGRA8 video output textures).
  // Always populated on success, independent of whether F16 is
  // available. ANGLE's |eglCreatePbufferFromClientBuffer| rejects the
  // call when the buffer format doesn't match the config, so 8-bit
  // external textures must bind against an 8-bit config.
  EGLConfig config_rgba8_ = nullptr;

  // True when |config_| is an RGBA16 float-component config.
  bool is_rgba16float_ = false;

  // The EGL context used to render Flutter views.
  std::unique_ptr<Context> render_context_;

  // The EGL context used for async texture uploads.
  std::unique_ptr<Context> resource_context_;

  // The current D3D device.
  Microsoft::WRL::ComPtr<ID3D11Device> resolved_device_ = nullptr;

  FML_DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace egl
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_EGL_MANAGER_H_
