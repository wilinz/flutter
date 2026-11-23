// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.embedding.android;

import static io.flutter.Build.API_LEVELS;

import android.app.Activity;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.view.Display;
import android.view.View;
import android.view.Window;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import io.flutter.embedding.engine.FlutterEngine;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import java.util.HashMap;
import java.util.Map;
import java.util.function.Consumer;

/**
 * Handles the {@code flutter/hdr} channel for a {@link FlutterView}.
 *
 * <p>Extended range presentation is off by default. {@code setEnabled} puts the window in HDR color
 * mode and declares the display's current HDR/SDR ratio for the F16 surface; {@code
 * getCapabilities} reports the surface format and the display headroom.
 */
final class HdrPresentation implements MethodChannel.MethodCallHandler {
  private static final String CHANNEL = "flutter/hdr";

  @NonNull private final View view;
  @NonNull private final FlutterEngine engine;
  @NonNull private final MethodChannel channel;
  private boolean enabled = false;
  @Nullable private Display listenedDisplay;
  @Nullable private Consumer<Display> ratioListener;

  HdrPresentation(@NonNull View view, @NonNull FlutterEngine engine) {
    this.view = view;
    this.engine = engine;
    this.channel = new MethodChannel(engine.getDartExecutor(), CHANNEL);
    channel.setMethodCallHandler(this);
  }

  /** Turns presentation back to SDR and stops handling the channel. */
  void dispose() {
    setEnabled(false);
    channel.setMethodCallHandler(null);
  }

  @Override
  public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
    switch (call.method) {
      case "setEnabled":
        Boolean value = call.argument("enabled");
        setEnabled(value != null && value);
        result.success(capabilities());
        break;
      case "getCapabilities":
        result.success(capabilities());
        break;
      default:
        result.notImplemented();
    }
  }

  private void setEnabled(boolean value) {
    if (enabled == value) {
      return;
    }
    enabled = value;
    Window window = findWindow(view.getContext());
    if (window != null && Build.VERSION.SDK_INT >= API_LEVELS.API_26) {
      window.setColorMode(value ? ActivityInfo.COLOR_MODE_HDR : ActivityInfo.COLOR_MODE_DEFAULT);
    }
    if (value) {
      listenForRatio();
    } else {
      stopListening();
    }
    applyRatio();
  }

  private void applyRatio() {
    engine.getRenderer().setHdrHeadroom(enabled ? currentHeadroom() : 1.0);
  }

  private void listenForRatio() {
    if (Build.VERSION.SDK_INT < API_LEVELS.API_34) {
      return;
    }
    Display display = view.getDisplay();
    if (display == null || !display.isHdrSdrRatioAvailable()) {
      return;
    }
    listenedDisplay = display;
    ratioListener = d -> applyRatio();
    display.registerHdrSdrRatioChangedListener(view.getContext().getMainExecutor(), ratioListener);
  }

  private void stopListening() {
    if (Build.VERSION.SDK_INT >= API_LEVELS.API_34
        && listenedDisplay != null
        && ratioListener != null) {
      listenedDisplay.unregisterHdrSdrRatioChangedListener(ratioListener);
    }
    listenedDisplay = null;
    ratioListener = null;
  }

  private double currentHeadroom() {
    if (Build.VERSION.SDK_INT >= API_LEVELS.API_34) {
      Display display = view.getDisplay();
      if (display != null && display.isHdrSdrRatioAvailable()) {
        return Math.max(1.0, display.getHdrSdrRatio());
      }
    }
    return 1.0;
  }

  private double potentialHeadroom() {
    if (Build.VERSION.SDK_INT >= API_LEVELS.API_36) {
      Display display = view.getDisplay();
      if (display != null) {
        return Math.max(1.0, highestRatio(display));
      }
    }
    return currentHeadroom();
  }

  @RequiresApi(API_LEVELS.API_36)
  private static float highestRatio(@NonNull Display display) {
    return display.getHighestHdrSdrRatio();
  }

  @NonNull
  private Map<String, Object> capabilities() {
    Map<String, Object> caps = new HashMap<>();
    caps.put("surface", engine.getRenderer().isSurfaceExtendedRange());
    caps.put("enabled", enabled);
    caps.put("currentHeadroom", currentHeadroom());
    caps.put("potentialHeadroom", potentialHeadroom());
    caps.put("referenceHeadroom", 0.0);
    return caps;
  }

  @Nullable
  private static Window findWindow(@Nullable Context context) {
    while (context instanceof ContextWrapper) {
      if (context instanceof Activity) {
        return ((Activity) context).getWindow();
      }
      context = ((ContextWrapper) context).getBaseContext();
    }
    return null;
  }
}
