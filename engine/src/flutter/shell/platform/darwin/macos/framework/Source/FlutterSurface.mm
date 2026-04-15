// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "flutter/shell/platform/darwin/macos/framework/Source/FlutterSurface.h"

#import <CoreMedia/CoreMedia.h>
#import <Metal/Metal.h>

#import "flutter/fml/platform/darwin/cf_utils.h"

@interface FlutterSurface () {
  CGSize _size;
  fml::CFRef<IOSurfaceRef> _ioSurface;
  id<MTLTexture> _texture;
  // Used for testing.
  BOOL _isInUseOverride;
  // Whether this surface was created with wide gamut enabled.
  BOOL _isWideGamut;
}
@end

@implementation FlutterSurface

- (IOSurfaceRef)ioSurface {
  return _ioSurface;
}

- (CGSize)size {
  return _size;
}

- (int64_t)textureId {
  return reinterpret_cast<int64_t>(_texture);
}

- (BOOL)isInUse {
  return _isInUseOverride || IOSurfaceIsInUse(_ioSurface);
}

- (BOOL)isWideGamut {
  return _isWideGamut;
}

- (BOOL)isInUseOverride {
  return _isInUseOverride;
}

- (void)setIsInUseOverride:(BOOL)isInUseOverride {
  _isInUseOverride = isInUseOverride;
}

- (instancetype)initWithSize:(CGSize)size
                      device:(id<MTLDevice>)device
             enableWideGamut:(BOOL)enableWideGamut {
  if (self = [super init]) {
    self->_size = size;
    self->_isWideGamut = enableWideGamut;
    self->_ioSurface.Reset([FlutterSurface createIOSurfaceWithSize:size
                                                   enableWideGamut:enableWideGamut]);
    // The wide-gamut surface is half-float so EDR values above 1.0 survive
    // composition.
    MTLPixelFormat pixelFormat =
        enableWideGamut ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
    self->_texture = [FlutterSurface createTextureForIOSurface:_ioSurface
                                                          size:size
                                                        device:device
                                                   pixelFormat:pixelFormat];
  }
  return self;
}

- (FlutterMetalTexture)asFlutterMetalTexture {
  return FlutterMetalTexture{
      .struct_size = sizeof(FlutterMetalTexture),
      .texture_id = self.textureId,
      .texture = (__bridge void*)_texture,
      // Retain for use in [FlutterSurface fromFlutterMetalTexture]. Released in
      // destruction_callback.
      .user_data = (__bridge_retained void*)self,
      .destruction_callback =
          [](void* user_data) {
            // Balancing release for the retain when setting user_data above.
            FlutterSurface* surface = (__bridge_transfer FlutterSurface*)user_data;
            surface = nil;
          },
  };
}

+ (FlutterSurface*)fromFlutterMetalTexture:(const FlutterMetalTexture*)texture {
  return (__bridge FlutterSurface*)texture->user_data;
}

+ (IOSurfaceRef)createIOSurfaceWithSize:(CGSize)size enableWideGamut:(BOOL)enableWideGamut {
  unsigned pixelFormat;
  unsigned bytesPerElement;
  if (enableWideGamut) {
    // F16 compositing surface so HDR values above 1.0 reach Core Animation.
    // Paired with kCGColorSpaceExtendedSRGB (gamma-encoded, not linear) so
    // alpha blending matches the 8-bit surface; linear blending dims
    // semi-transparent UI at midtones.
    pixelFormat = kCVPixelFormatType_64RGBAHalf;
    bytesPerElement = 8;
  } else {
    pixelFormat = kCVPixelFormatType_32BGRA;
    bytesPerElement = 4;
  }

  size_t bytesPerRow = IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, size.width * bytesPerElement);
  size_t totalBytes = IOSurfaceAlignProperty(kIOSurfaceAllocSize, size.height * bytesPerRow);
  NSDictionary* options = @{
    (id)kIOSurfaceWidth : @(size.width),
    (id)kIOSurfaceHeight : @(size.height),
    (id)kIOSurfacePixelFormat : @(pixelFormat),
    (id)kIOSurfaceBytesPerElement : @(bytesPerElement),
    (id)kIOSurfaceBytesPerRow : @(bytesPerRow),
    (id)kIOSurfaceAllocSize : @(totalBytes),
  };

  IOSurfaceRef res = IOSurfaceCreate((CFDictionaryRef)options);
  if (enableWideGamut) {
    // Extended sRGB: sRGB transfer with values above 1.0 shown as EDR.
    IOSurfaceSetValue(res, kIOSurfaceColorSpace, kCGColorSpaceExtendedSRGB);
  } else {
    IOSurfaceSetValue(res, kIOSurfaceColorSpace, kCGColorSpaceSRGB);
  }
  return res;
}

+ (id<MTLTexture>)createTextureForIOSurface:(IOSurfaceRef)surface
                                       size:(CGSize)size
                                     device:(id<MTLDevice>)device
                                pixelFormat:(MTLPixelFormat)pixelFormat {
  MTLTextureDescriptor* textureDescriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                         width:size.width
                                                        height:size.height
                                                     mipmapped:NO];
  textureDescriptor.usage =
      MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;
  return [device newTextureWithDescriptor:textureDescriptor iosurface:surface plane:0];
}

@end
