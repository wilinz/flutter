// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/hdr_handler.h"

#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <vector>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/embedder/embedder_linear_output.h"
#include "flutter/shell/platform/windows/egl/manager.h"
#include "flutter/shell/platform/windows/flutter_windows_engine.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"

namespace flutter {

namespace {

constexpr char kChannelName[] = "flutter/hdr";
constexpr char kSetEnabledMethod[] = "setEnabled";
constexpr char kGetCapabilitiesMethod[] = "getCapabilities";
constexpr char kEnabledKey[] = "enabled";

// scRGB defines 1.0 as 80 nits.
constexpr double kScrgbReferenceNits = 80.0;

struct MonitorHdr {
  bool hdr = false;
  double peak_nits = 0.0;
  double sdr_white_nits = kScrgbReferenceNits;
};

// SDR white level of the display attached to the GDI device |device_name|,
// in nits. Returns |kScrgbReferenceNits| when unavailable.
double SdrWhiteNits(const wchar_t* device_name) {
  UINT32 path_count = 0;
  UINT32 mode_count = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count,
                                  &mode_count) != ERROR_SUCCESS) {
    return kScrgbReferenceNits;
  }
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                         &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) {
    return kScrgbReferenceNits;
  }
  for (UINT32 i = 0; i < path_count; i++) {
    const DISPLAYCONFIG_PATH_INFO& path = paths[i];
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
        wcscmp(source.viewGdiDeviceName, device_name) != 0) {
      continue;
    }
    DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
    white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
    white.header.size = sizeof(white);
    white.header.adapterId = path.targetInfo.adapterId;
    white.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS) {
      return kScrgbReferenceNits;
    }
    // SDRWhiteLevel is in units of 1/1000 of the 80 nit reference.
    return white.SDRWhiteLevel / 1000.0 * kScrgbReferenceNits;
  }
  return kScrgbReferenceNits;
}

MonitorHdr QueryMonitor(HMONITOR monitor) {
  MonitorHdr result;
  if (monitor == nullptr) {
    return result;
  }
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return result;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND;
       a++) {
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND;
         o++) {
      Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
      DXGI_OUTPUT_DESC1 desc = {};
      if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&desc)) ||
          desc.Monitor != monitor) {
        continue;
      }
      result.hdr =
          desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
      result.peak_nits = desc.MaxLuminance;
      if (result.hdr) {
        result.sdr_white_nits = SdrWhiteNits(desc.DeviceName);
      }
      return result;
    }
  }
  return result;
}

}  // namespace

HdrHandler::HdrHandler(BinaryMessenger* messenger, FlutterWindowsEngine* engine)
    : channel_(std::make_unique<MethodChannel<EncodableValue>>(
          messenger,
          kChannelName,
          &StandardMethodCodec::GetInstance())),
      engine_(engine) {
  channel_->SetMethodCallHandler(
      [this](const MethodCall<EncodableValue>& call,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
        HandleMethodCall(call, std::move(result));
      });
}

void HdrHandler::HandleMethodCall(
    const MethodCall<EncodableValue>& method_call,
    std::unique_ptr<MethodResult<EncodableValue>> result) {
  const std::string& method = method_call.method_name();
  if (method == kSetEnabledMethod) {
    const auto* arguments = std::get_if<EncodableMap>(method_call.arguments());
    bool enabled = false;
    if (arguments != nullptr) {
      auto it = arguments->find(EncodableValue(std::string(kEnabledKey)));
      if (it != arguments->end()) {
        const bool* value = std::get_if<bool>(&it->second);
        enabled = value != nullptr && *value;
      }
    }
    enabled_ = enabled;
    result->Success(Capabilities());
  } else if (method == kGetCapabilitiesMethod) {
    result->Success(Capabilities());
  } else {
    result->NotImplemented();
  }
}

void HdrHandler::Refresh() {
  Capabilities();
}

EncodableValue HdrHandler::Capabilities() {
  const egl::Manager* manager = engine_->egl_manager();
  const bool surface = manager != nullptr && manager->is_rgba16float();

  HMONITOR monitor = nullptr;
  FlutterWindowsView* view = engine_->view(kImplicitViewId);
  if (view != nullptr && view->GetWindowHandle() != nullptr) {
    monitor =
        MonitorFromWindow(view->GetWindowHandle(), MONITOR_DEFAULTTONEAREST);
  }
  const MonitorHdr state = QueryMonitor(monitor);

  double headroom = 1.0;
  if (surface && state.hdr && state.sdr_white_nits > 0.0) {
    headroom = std::max(1.0, state.peak_nits / state.sdr_white_nits);
    SetLinearOutputScale(
        static_cast<float>(state.sdr_white_nits / kScrgbReferenceNits));
  } else {
    SetLinearOutputScale(1.0f);
  }

  return EncodableValue(EncodableMap{
      {EncodableValue("surface"), EncodableValue(surface)},
      {EncodableValue("enabled"), EncodableValue(enabled_)},
      {EncodableValue("currentHeadroom"), EncodableValue(headroom)},
      {EncodableValue("potentialHeadroom"), EncodableValue(headroom)},
      {EncodableValue("referenceHeadroom"), EncodableValue(0.0)},
  });
}

}  // namespace flutter
