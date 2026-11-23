// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_hdr_handler.h"

#include <cstring>

#include "flutter/shell/platform/linux/public/flutter_linux/fl_method_channel.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_standard_method_codec.h"

static constexpr char kChannelName[] = "flutter/hdr";
static constexpr char kSetEnabledMethod[] = "setEnabled";
static constexpr char kGetCapabilitiesMethod[] = "getCapabilities";

struct _FlHdrHandler {
  GObject parent_instance;

  FlMethodChannel* channel;
};

G_DEFINE_TYPE(FlHdrHandler, fl_hdr_handler, G_TYPE_OBJECT)

// SDR-only capabilities: no extended range surface and no headroom.
static FlValue* sdr_capabilities() {
  FlValue* caps = fl_value_new_map();
  fl_value_set_string_take(caps, "surface", fl_value_new_bool(FALSE));
  fl_value_set_string_take(caps, "enabled", fl_value_new_bool(FALSE));
  fl_value_set_string_take(caps, "currentHeadroom", fl_value_new_float(1.0));
  fl_value_set_string_take(caps, "potentialHeadroom", fl_value_new_float(1.0));
  fl_value_set_string_take(caps, "referenceHeadroom", fl_value_new_float(0.0));
  return caps;
}

// Called when a method call is received from Flutter.
static void method_call_cb(FlMethodChannel* channel,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  const gchar* method = fl_method_call_get_name(method_call);

  g_autoptr(FlMethodResponse) response = nullptr;
  if (strcmp(method, kSetEnabledMethod) == 0 ||
      strcmp(method, kGetCapabilitiesMethod) == 0) {
    g_autoptr(FlValue) caps = sdr_capabilities();
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(caps));
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

FlHdrHandler* fl_hdr_handler_new(FlBinaryMessenger* messenger) {
  g_return_val_if_fail(FL_IS_BINARY_MESSENGER(messenger), nullptr);

  FlHdrHandler* self =
      FL_HDR_HANDLER(g_object_new(fl_hdr_handler_get_type(), nullptr));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  self->channel =
      fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(self->channel, method_call_cb, self,
                                            nullptr);

  return self;
}
