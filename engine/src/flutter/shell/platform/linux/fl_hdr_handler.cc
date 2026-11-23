// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_hdr_handler.h"

#include <cstring>

#include "flutter/common/constants.h"
#include "flutter/shell/platform/linux/fl_engine_private.h"
#include "flutter/shell/platform/linux/fl_view_private.h"
#include "flutter/shell/platform/linux/fl_view_renderer_subsurface.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_method_channel.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_standard_method_codec.h"

static constexpr char kChannelName[] = "flutter/hdr";
static constexpr char kSetEnabledMethod[] = "setEnabled";
static constexpr char kGetCapabilitiesMethod[] = "getCapabilities";
static constexpr char kEnabledKey[] = "enabled";
static constexpr char kViewIdKey[] = "viewId";

struct _FlHdrHandler {
  GObject parent_instance;

  // Engine the handler belongs to, not referenced.
  FlEngine* engine;

  FlMethodChannel* channel;
};

G_DEFINE_TYPE(FlHdrHandler, fl_hdr_handler, G_TYPE_OBJECT)

// Gets the view addressed by the optional "viewId" argument, or the implicit
// view.
static FlutterViewId get_view_id(FlValue* args) {
  if (args != nullptr && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
    FlValue* value = fl_value_lookup_string(args, kViewIdKey);
    if (value != nullptr && fl_value_get_type(value) == FL_VALUE_TYPE_INT) {
      return fl_value_get_int(value);
    }
  }
  return flutter::kFlutterImplicitViewId;
}

static FlValue* capabilities(FlViewRenderer* renderer) {
  gboolean surface = FALSE;
  gboolean enabled = FALSE;
  double headroom = 1.0;
  if (FL_IS_VIEW_RENDERER_SUBSURFACE(renderer)) {
    fl_view_renderer_subsurface_get_hdr_capabilities(
        FL_VIEW_RENDERER_SUBSURFACE(renderer), &surface, &enabled, &headroom);
  }

  FlValue* caps = fl_value_new_map();
  fl_value_set_string_take(caps, "surface", fl_value_new_bool(surface));
  fl_value_set_string_take(caps, "enabled", fl_value_new_bool(enabled));
  fl_value_set_string_take(caps, "currentHeadroom",
                           fl_value_new_float(headroom));
  fl_value_set_string_take(caps, "potentialHeadroom",
                           fl_value_new_float(headroom));
  fl_value_set_string_take(caps, "referenceHeadroom", fl_value_new_float(0.0));
  return caps;
}

static FlMethodResponse* handle(FlHdrHandler* self,
                                const gchar* method,
                                FlValue* args) {
  g_autoptr(FlRenderable) renderable =
      fl_engine_get_renderable(self->engine, get_view_id(args));
  FlViewRenderer* renderer = nullptr;
  if (renderable != nullptr && FL_IS_VIEW(renderable)) {
    renderer = fl_view_get_renderer(FL_VIEW(renderable));
  }
  if (renderer == nullptr) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "no_view", "No view with this ID.", nullptr));
  }

  if (strcmp(method, kSetEnabledMethod) == 0 &&
      FL_IS_VIEW_RENDERER_SUBSURFACE(renderer)) {
    FlValue* value =
        args != nullptr && fl_value_get_type(args) == FL_VALUE_TYPE_MAP
            ? fl_value_lookup_string(args, kEnabledKey)
            : nullptr;
    gboolean enabled = value != nullptr &&
                       fl_value_get_type(value) == FL_VALUE_TYPE_BOOL &&
                       fl_value_get_bool(value);
    fl_view_renderer_subsurface_set_hdr_enabled(
        FL_VIEW_RENDERER_SUBSURFACE(renderer), enabled);
  }

  g_autoptr(FlValue) caps = capabilities(renderer);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(caps));
}

// Called when a method call is received from Flutter.
static void method_call_cb(FlMethodChannel* channel,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  FlHdrHandler* self = FL_HDR_HANDLER(user_data);
  const gchar* method = fl_method_call_get_name(method_call);

  g_autoptr(FlMethodResponse) response = nullptr;
  if (strcmp(method, kSetEnabledMethod) == 0 ||
      strcmp(method, kGetCapabilitiesMethod) == 0) {
    response = handle(self, method, fl_method_call_get_args(method_call));
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  g_autoptr(GError) error = nullptr;
  if (!fl_method_call_respond(method_call, response, &error)) {
    g_warning("Failed to send method call response: %s", error->message);
  }
}

static void fl_hdr_handler_dispose(GObject* object) {
  FlHdrHandler* self = FL_HDR_HANDLER(object);

  g_clear_object(&self->channel);

  G_OBJECT_CLASS(fl_hdr_handler_parent_class)->dispose(object);
}

static void fl_hdr_handler_class_init(FlHdrHandlerClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_hdr_handler_dispose;
}

static void fl_hdr_handler_init(FlHdrHandler* self) {}

FlHdrHandler* fl_hdr_handler_new(FlEngine* engine) {
  g_return_val_if_fail(FL_IS_ENGINE(engine), nullptr);

  FlHdrHandler* self =
      FL_HDR_HANDLER(g_object_new(fl_hdr_handler_get_type(), nullptr));
  self->engine = engine;

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  self->channel = fl_method_channel_new(fl_engine_get_binary_messenger(engine),
                                        kChannelName, FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(self->channel, method_call_cb, self,
                                            nullptr);

  return self;
}
