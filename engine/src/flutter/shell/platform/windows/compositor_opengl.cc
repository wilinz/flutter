// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/compositor_opengl.h"

#include "GLES3/gl3.h"
#include "flutter/fml/logging.h"
#include "flutter/shell/platform/windows/flutter_windows_engine.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"

namespace flutter {

namespace {

constexpr uint32_t kWindowFrameBufferId = 0;

// The metadata for an OpenGL framebuffer backing store.
struct FramebufferBackingStore {
  uint32_t framebuffer_id = 0;
  uint32_t texture_id = 0;
  uint32_t color_renderbuffer_id = 0;
  uint32_t depth_stencil_id = 0;
};

typedef const impeller::GLProc<decltype(glBlitFramebuffer)> BlitFramebufferProc;

const BlitFramebufferProc& GetBlitFramebufferProc(
    const impeller::ProcTableGLES& gl) {
  if (gl.BlitFramebuffer.IsAvailable()) {
    return gl.BlitFramebuffer;
  } else if (gl.BlitFramebufferANGLE.IsAvailable()) {
    return gl.BlitFramebufferANGLE;
  }

  // CompositorOpenGL::Initialize verifies that a blit procedure is available.
  FML_UNREACHABLE();
}

}  // namespace

CompositorOpenGL::CompositorOpenGL(FlutterWindowsEngine* engine,
                                   impeller::ProcTableGLES::Resolver resolver,
                                   bool enable_impeller)
    : engine_(engine), resolver_(resolver), enable_impeller_(enable_impeller) {}

bool CompositorOpenGL::CreateBackingStore(
    const FlutterBackingStoreConfig& config,
    FlutterBackingStore* result) {
  if (!is_initialized_ && !Initialize()) {
    return false;
  }

  auto store = std::make_unique<FramebufferBackingStore>();

  gl_->GenFramebuffers(1, &store->framebuffer_id);
  gl_->BindFramebuffer(GL_FRAMEBUFFER, store->framebuffer_id);

  if (enable_impeller_) {
    if (supports_implicit_msaa_) {
      gl_->GenTextures(1, &store->texture_id);
      gl_->BindTexture(GL_TEXTURE_2D, store->texture_id);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl_->TexImage2D(GL_TEXTURE_2D, 0, format_.internal_format,
                      config.size.width, config.size.height, 0,
                      format_.general_format, format_.pixel_type, nullptr);
      gl_->BindTexture(GL_TEXTURE_2D, 0);

      // MSAA color attachment
      gl_->FramebufferTexture2DMultisampleEXT(
          GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
          store->texture_id, 0, 4);

      // Impeller always requires depth/stencil attachment.
      gl_->GenRenderbuffers(1, &store->depth_stencil_id);
      gl_->BindRenderbuffer(GL_RENDERBUFFER, store->depth_stencil_id);
      gl_->RenderbufferStorageMultisampleEXT(
          GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, config.size.width,
          config.size.height);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, store->depth_stencil_id);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                   GL_RENDERBUFFER, store->depth_stencil_id);
    } else if (supports_offscreen_msaa_) {
      // MSAA color renderbuffer attachment
      gl_->GenRenderbuffers(1, &store->color_renderbuffer_id);
      gl_->BindRenderbuffer(GL_RENDERBUFFER, store->color_renderbuffer_id);
      gl_->RenderbufferStorageMultisample(
          GL_RENDERBUFFER, 4, format_.sized_format, config.size.width,
          config.size.height);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_RENDERBUFFER,
                                   store->color_renderbuffer_id);

      // MSAA depth/stencil attachment
      gl_->GenRenderbuffers(1, &store->depth_stencil_id);
      gl_->BindRenderbuffer(GL_RENDERBUFFER, store->depth_stencil_id);
      gl_->RenderbufferStorageMultisample(
          GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, config.size.width,
          config.size.height);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, store->depth_stencil_id);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                   GL_RENDERBUFFER, store->depth_stencil_id);
    } else {
      gl_->GenTextures(1, &store->texture_id);
      gl_->BindTexture(GL_TEXTURE_2D, store->texture_id);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl_->TexImage2D(GL_TEXTURE_2D, 0, format_.internal_format,
                      config.size.width, config.size.height, 0,
                      format_.general_format, format_.pixel_type, nullptr);
      gl_->BindTexture(GL_TEXTURE_2D, 0);

      gl_->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, store->texture_id, 0);

      gl_->GenRenderbuffers(1, &store->depth_stencil_id);
      gl_->BindRenderbuffer(GL_RENDERBUFFER, store->depth_stencil_id);
      gl_->RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                               config.size.width, config.size.height);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, store->depth_stencil_id);
      gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                   GL_RENDERBUFFER, store->depth_stencil_id);
    }
  } else {
    gl_->GenTextures(1, &store->texture_id);
    gl_->BindTexture(GL_TEXTURE_2D, store->texture_id);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->TexImage2D(GL_TEXTURE_2D, 0, format_.internal_format,
                    config.size.width, config.size.height, 0,
                    format_.general_format, format_.pixel_type, nullptr);
    gl_->BindTexture(GL_TEXTURE_2D, 0);

    gl_->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, store->texture_id, 0);
  }

  result->type = kFlutterBackingStoreTypeOpenGL;
  result->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
  result->open_gl.framebuffer.name = store->framebuffer_id;
  result->open_gl.framebuffer.target = format_.sized_format;
  result->open_gl.framebuffer.user_data = store.release();
  result->open_gl.framebuffer.destruction_callback = [](void* user_data) {
    // Backing store destroyed in `CompositorOpenGL::CollectBackingStore`, set
    // on FlutterCompositor.collect_backing_store_callback during engine start.
  };
  return true;
}

bool CompositorOpenGL::CollectBackingStore(const FlutterBackingStore* store) {
  FML_DCHECK(is_initialized_);
  FML_DCHECK(store->type == kFlutterBackingStoreTypeOpenGL);
  FML_DCHECK(store->open_gl.type == kFlutterOpenGLTargetTypeFramebuffer);

  auto user_data = static_cast<FramebufferBackingStore*>(
      store->open_gl.framebuffer.user_data);

  gl_->DeleteFramebuffers(1, &user_data->framebuffer_id);
  if (user_data->texture_id != 0) {
    gl_->DeleteTextures(1, &user_data->texture_id);
  }
  if (user_data->color_renderbuffer_id != 0) {
    gl_->DeleteRenderbuffers(1, &user_data->color_renderbuffer_id);
  }
  if (user_data->depth_stencil_id != 0) {
    gl_->DeleteRenderbuffers(1, &user_data->depth_stencil_id);
  }

  delete user_data;
  return true;
}

// Draws a texture to the whole viewport, optionally decoding extended sRGB
// (gamma encoded, mirrored for negative values) to linear, then scaling.
constexpr char kPresentVertexShader[] =
    "attribute vec2 position;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "  uv = position * 0.5 + 0.5;\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";

constexpr char kPresentFragmentShader[] =
    "precision highp float;\n"
    "uniform sampler2D frame;\n"
    "uniform float scale;\n"
    "uniform float decode;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "  vec4 color = texture2D(frame, uv);\n"
    "  if (decode > 0.5) {\n"
    "    vec3 a = abs(color.rgb);\n"
    "    vec3 decoded = mix(a / 12.92, pow((a + 0.055) / 1.055, vec3(2.4)),\n"
    "                       step(0.04045, a));\n"
    "    color.rgb = sign(color.rgb) * decoded;\n"
    "  }\n"
    "  gl_FragColor = vec4(color.rgb * scale, color.a);\n"
    "}\n";

bool CompositorOpenGL::Present(FlutterWindowsView* view,
                               const FlutterLayer** layers,
                               size_t layers_count) {
  FML_DCHECK(view != nullptr);

  // Clear the view if there are no layers to present.
  if (layers_count == 0) {
    // Normally the compositor is initialized when the first backing store is
    // created. However, on an empty frame no backing stores are created and
    // the present needs to initialize the compositor.
    if (!is_initialized_ && !Initialize()) {
      return false;
    }

    return Clear(view);
  }

  // TODO: Support compositing layers and platform views.
  // See: https://github.com/flutter/flutter/issues/31713
  FML_DCHECK(is_initialized_);
  FML_DCHECK(layers_count == 1);
  FML_DCHECK(layers[0]->offset.x == 0 && layers[0]->offset.y == 0);
  FML_DCHECK(layers[0]->type == kFlutterLayerContentTypeBackingStore);
  FML_DCHECK(layers[0]->backing_store->type == kFlutterBackingStoreTypeOpenGL);
  FML_DCHECK(layers[0]->backing_store->open_gl.type ==
             kFlutterOpenGLTargetTypeFramebuffer);

  auto width = layers[0]->size.width;
  auto height = layers[0]->size.height;

  // Check if this frame can be presented. This resizes the surface if a resize
  // is pending and |width| and |height| match the target size.
  if (!view->OnFrameGenerated(width, height)) {
    return false;
  }

  // |OnFrameGenerated| should return false if the surface isn't valid.
  FML_DCHECK(view->surface() != nullptr);
  FML_DCHECK(view->surface()->IsValid());

  egl::WindowSurface* surface = view->surface();
  if (!surface->MakeCurrent()) {
    return false;
  }

  auto source_id = layers[0]->backing_store->open_gl.framebuffer.name;

  // Disable the scissor test as it can affect blit operations.
  // Prevents regressions like: https://github.com/flutter/flutter/issues/140828
  // See OpenGL specification version 4.6, section 18.3.1.
  gl_->Disable(GL_SCISSOR_TEST);

  if (format_.pixel_type == GL_HALF_FLOAT) {
    if (!PresentExtendedRange(source_id, width, height,
                              view->hdr_output_scale())) {
      return false;
    }
    if (!surface->SwapBuffers()) {
      return false;
    }
    view->OnFramePresented();
    return true;
  }

  gl_->BindFramebuffer(GL_READ_FRAMEBUFFER, source_id);
  gl_->BindFramebuffer(GL_DRAW_FRAMEBUFFER, kWindowFrameBufferId);

  auto blitFramebuffer = GetBlitFramebufferProc(*gl_);
  blitFramebuffer(0,                    // srcX0
                  0,                    // srcY0
                  width,                // srcX1
                  height,               // srcY1
                  0,                    // dstX0
                  0,                    // dstY0
                  width,                // dstX1
                  height,               // dstY1
                  GL_COLOR_BUFFER_BIT,  // mask
                  GL_NEAREST            // filter
  );

  if (!surface->SwapBuffers()) {
    return false;
  }

  view->OnFramePresented();
  return true;
}

bool CompositorOpenGL::CreatePresentProgram() {
  auto compile = [this](GLenum type, const char* source) -> GLuint {
    GLuint shader = gl_->CreateShader(type);
    gl_->ShaderSource(shader, 1, &source, nullptr);
    gl_->CompileShader(shader);
    GLint status = GL_FALSE;
    gl_->GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
      FML_LOG(ERROR) << "Failed to compile the HDR present shader.";
      gl_->DeleteShader(shader);
      return 0;
    }
    return shader;
  };

  GLuint vertex_shader = compile(GL_VERTEX_SHADER, kPresentVertexShader);
  GLuint fragment_shader = compile(GL_FRAGMENT_SHADER, kPresentFragmentShader);
  if (vertex_shader == 0 || fragment_shader == 0) {
    return false;
  }

  GLuint program = gl_->CreateProgram();
  gl_->AttachShader(program, vertex_shader);
  gl_->AttachShader(program, fragment_shader);
  gl_->BindAttribLocation(program, 0, "position");
  gl_->LinkProgram(program);
  gl_->DeleteShader(vertex_shader);
  gl_->DeleteShader(fragment_shader);
  GLint status = GL_FALSE;
  gl_->GetProgramiv(program, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) {
    FML_LOG(ERROR) << "Failed to link the HDR present program.";
    gl_->DeleteProgram(program);
    return false;
  }

  present_program_ = program;
  present_scale_location_ = gl_->GetUniformLocation(program, "scale");
  present_decode_location_ = gl_->GetUniformLocation(program, "decode");

  // A triangle strip covering the viewport.
  const GLfloat vertices[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  gl_->GenBuffers(1, &present_vertex_buffer_);
  gl_->BindBuffer(GL_ARRAY_BUFFER, present_vertex_buffer_);
  gl_->BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  return true;
}

bool CompositorOpenGL::PresentExtendedRange(GLuint source_framebuffer,
                                            GLint width,
                                            GLint height,
                                            GLfloat scale) {
  if (present_program_ == 0 && !CreatePresentProgram()) {
    return false;
  }

  // Resolve the frame into a single-sample texture that can be sampled.
  if (present_texture_ == 0 || present_width_ != width ||
      present_height_ != height) {
    if (present_texture_ == 0) {
      gl_->GenTextures(1, &present_texture_);
      gl_->GenFramebuffers(1, &present_framebuffer_);
    }
    gl_->BindTexture(GL_TEXTURE_2D, present_texture_);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA,
                    GL_HALF_FLOAT, nullptr);
    gl_->BindFramebuffer(GL_FRAMEBUFFER, present_framebuffer_);
    gl_->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, present_texture_, 0);
    present_width_ = width;
    present_height_ = height;
  }

  gl_->BindFramebuffer(GL_READ_FRAMEBUFFER, source_framebuffer);
  gl_->BindFramebuffer(GL_DRAW_FRAMEBUFFER, present_framebuffer_);
  GetBlitFramebufferProc (*gl_)(0, 0, width, height, 0, 0, width, height,
                                GL_COLOR_BUFFER_BIT, GL_NEAREST);

  // Draw it to the window with the same pixel mapping as a blit.
  gl_->BindFramebuffer(GL_FRAMEBUFFER, kWindowFrameBufferId);
  gl_->Viewport(0, 0, width, height);
  gl_->Disable(GL_BLEND);
  gl_->UseProgram(present_program_);
  const GLfloat decode = enable_impeller_ ? 1.0f : 0.0f;
  gl_->Uniform1fv(present_scale_location_, 1, &scale);
  gl_->Uniform1fv(present_decode_location_, 1, &decode);
  gl_->ActiveTexture(GL_TEXTURE0);
  gl_->BindTexture(GL_TEXTURE_2D, present_texture_);
  gl_->BindBuffer(GL_ARRAY_BUFFER, present_vertex_buffer_);
  gl_->EnableVertexAttribArray(0);
  gl_->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  gl_->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  gl_->BindTexture(GL_TEXTURE_2D, 0);
  gl_->UseProgram(0);
  return true;
}

bool CompositorOpenGL::Initialize() {
  FML_DCHECK(!is_initialized_);

  egl::Manager* manager = engine_->egl_manager();
  if (!manager) {
    return false;
  }

  if (!manager->render_context()->MakeCurrent()) {
    return false;
  }

  gl_ = std::make_unique<impeller::ProcTableGLES>(resolver_);
  if (!gl_->IsValid()) {
    gl_.reset();
    return false;
  }

  // Match the backing-store format to the EGL config: F16 when ANGLE supplied
  // a RGBA16 float-component config, otherwise 8-bit. Keeping the backing
  // store format in sync with the window surface ensures the blit at
  // presentation time doesn't round-trip HDR values through an 8-bit target.
  if (manager->is_rgba16float()) {
    format_.sized_format = GL_RGBA16F;
    format_.general_format = GL_RGBA;
    format_.pixel_type = GL_HALF_FLOAT;
  } else if (gl_->GetDescription()->HasExtension(
                 "GL_EXT_texture_format_BGRA8888")) {
    format_.sized_format = GL_BGRA8_EXT;
    format_.general_format = GL_BGRA_EXT;
    format_.pixel_type = GL_UNSIGNED_BYTE;
  } else {
    format_.sized_format = GL_RGBA8;
    format_.general_format = GL_RGBA;
    format_.pixel_type = GL_UNSIGNED_BYTE;
  }

  const impeller::DescriptionGLES* description = gl_->GetDescription();
  bool is_gles2 =
      description->IsES() && description->GetGlVersion().major_version < 3;
  format_.internal_format =
      is_gles2 ? format_.general_format : format_.sized_format;

  if (!gl_->BlitFramebuffer.IsAvailable() &&
      !gl_->BlitFramebufferANGLE.IsAvailable()) {
    FML_LOG(ERROR) << "Unable to find OpenGL blit framebuffer procedure.";
    return false;
  }

  supports_implicit_msaa_ =
      gl_->GetCapabilities()->SupportsImplicitResolvingMSAA();
  supports_offscreen_msaa_ = gl_->GetCapabilities()->SupportsOffscreenMSAA();

  if (enable_impeller_ && !supports_implicit_msaa_ &&
      !supports_offscreen_msaa_) {
    // TODO(190362): I suspect this branch is never taken since ANGLE will
    // support offscreen MSAA. We should investigate removing this branch and
    // the implicit MSAA branch in the rendering functions.
    FML_LOG(WARNING) << "Rendering without MSAA.";
  }

  is_initialized_ = true;
  return true;
}

bool CompositorOpenGL::Clear(FlutterWindowsView* view) {
  FML_DCHECK(is_initialized_);

  // Check if this frame can be presented. This resizes the surface if needed.
  if (!view->OnEmptyFrameGenerated()) {
    return false;
  }

  // |OnEmptyFrameGenerated| should return false if the surface isn't valid.
  FML_DCHECK(view->surface() != nullptr);
  FML_DCHECK(view->surface()->IsValid());

  egl::WindowSurface* surface = view->surface();
  if (!surface->MakeCurrent()) {
    return false;
  }

  gl_->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  gl_->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  if (!surface->SwapBuffers()) {
    return false;
  }

  view->OnFramePresented();
  return true;
}

}  // namespace flutter
