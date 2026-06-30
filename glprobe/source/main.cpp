#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

static int s_nxlinkSock = -1;
extern "C" void userAppInit() {
  if (R_SUCCEEDED(socketInitializeDefault())) {
    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock < 0)
      socketExit();
  }
}
extern "C" void userAppExit() {
  if (s_nxlinkSock >= 0) {
    close(s_nxlinkSock);
    socketExit();
    s_nxlinkSock = -1;
  }
}

static FILE *s_report = nullptr;
static void R(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fputs(buf, stdout);
  if (s_report)
    fputs(buf, s_report);
}

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

static bool tryContext(EGLDisplay dpy, EGLConfig cfg, EGLint profileBit,
                       int major, int minor) {
  const EGLint attrs[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                          profileBit,
                          EGL_CONTEXT_MAJOR_VERSION_KHR,
                          major,
                          EGL_CONTEXT_MINOR_VERSION_KHR,
                          minor,
                          EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attrs);
  if (ctx) {
    eglDestroyContext(dpy, ctx);
    return true;
  }
  return false;
}

static bool initEgl(NWindow *win) {
  s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!s_display)
    return false;
  eglInitialize(s_display, nullptr, nullptr);
  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
    return false;

  const EGLint cfgAttrs[] = {EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_DEPTH_SIZE,
                             24,
                             EGL_STENCIL_SIZE,
                             8,
                             EGL_NONE};
  EGLConfig config;
  EGLint numConfigs;
  eglChooseConfig(s_display, cfgAttrs, &config, 1, &numConfigs);
  if (numConfigs == 0)
    return false;

  s_surface = eglCreateWindowSurface(s_display, config, win, nullptr);
  if (!s_surface)
    return false;

  bool core43 = tryContext(s_display, config,
                           EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, 4, 3);
  bool core33 = tryContext(s_display, config,
                           EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, 3, 3);
  bool compat21 =
      tryContext(s_display, config,
                 EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR, 2, 1);
  bool compat32 =
      tryContext(s_display, config,
                 EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR, 3, 2);

  const EGLint ctxAttrs[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                             EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                             EGL_CONTEXT_MAJOR_VERSION_KHR,
                             4,
                             EGL_CONTEXT_MINOR_VERSION_KHR,
                             3,
                             EGL_NONE};
  s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, ctxAttrs);
  if (!s_context)
    return false;
  eglMakeCurrent(s_display, s_surface, s_surface, s_context);

  R("== EGL context profile availability ==\n");
  R("  Core 4.3            : %s\n", core43 ? "YES" : "no");
  R("  Core 3.3            : %s\n", core33 ? "YES" : "no");
  R("  Compatibility 2.1   : %s  <- 0 A.D. legacy GL path\n",
    compat21 ? "YES" : "no");
  R("  Compatibility 3.2   : %s\n", compat32 ? "YES" : "no");
  R("\n");
  return true;
}

static bool s_hasExt(const char *name, int n) {
  for (int i = 0; i < n; ++i) {
    const char *e = (const char *)glGetStringi(GL_EXTENSIONS, i);
    if (e && strcmp(e, name) == 0)
      return true;
  }
  return false;
}

static void geti(const char *label, GLenum e) {
  GLint v = 0;
  glGetIntegerv(e, &v);
  R("  %-38s : %d\n", label, v);
}

static void dumpCaps() {
  R("== Driver strings ==\n");
  R("  GL_VENDOR    : %s\n", glGetString(GL_VENDOR));
  R("  GL_RENDERER  : %s\n", glGetString(GL_RENDERER));
  R("  GL_VERSION   : %s\n", glGetString(GL_VERSION));
  R("  GL_SLVERSION : %s\n\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

  R("== Limits relevant to 0 A.D. ==\n");
  geti("GL_MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE);
  geti("GL_MAX_3D_TEXTURE_SIZE", GL_MAX_3D_TEXTURE_SIZE);
  geti("GL_MAX_CUBE_MAP_TEXTURE_SIZE", GL_MAX_CUBE_MAP_TEXTURE_SIZE);
  geti("GL_MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS);
  geti("GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS",
       GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
  geti("GL_MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS);
  geti("GL_MAX_VERTEX_UNIFORM_COMPONENTS", GL_MAX_VERTEX_UNIFORM_COMPONENTS);
  geti("GL_MAX_FRAGMENT_UNIFORM_COMPONENTS",
       GL_MAX_FRAGMENT_UNIFORM_COMPONENTS);
  geti("GL_MAX_DRAW_BUFFERS", GL_MAX_DRAW_BUFFERS);
  geti("GL_MAX_RENDERBUFFER_SIZE", GL_MAX_RENDERBUFFER_SIZE);
  geti("GL_MAX_SAMPLES", GL_MAX_SAMPLES);
  R("\n");

  GLint n = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  R("== Extensions 0 A.D. cares about (%d total reported) ==\n", n);
  struct {
    const char *ext;
    const char *why;
  } wanted[] = {
      {"GL_EXT_texture_compression_s3tc",
       "DXT1/3/5 textures (0 A.D. ships these!)"},
      {"GL_ARB_texture_compression_rgtc", "normal-map compression"},
      {"GL_EXT_texture_filter_anisotropic", "anisotropic filtering"},
      {"GL_ARB_framebuffer_object",
       "FBOs / render-to-texture (shadows, postproc)"},
      {"GL_ARB_instanced_arrays", "instanced rendering"},
      {"GL_ARB_draw_instanced", "instanced rendering"},
      {"GL_ARB_uniform_buffer_object", "UBOs"},
      {"GL_ARB_vertex_array_object", "VAOs"},
      {"GL_ARB_map_buffer_range", "streaming dynamic VBOs"},
      {"GL_ARB_texture_float", "HDR / float render targets"},
      {"GL_ARB_depth_texture", "shadow maps"},
      {"GL_ARB_occlusion_query", "occlusion culling"},
      {"GL_ARB_timer_query", "GPU profiling"},
      {"GL_KHR_debug", "debug output"},
      {"GL_ARB_sync", "fences"},
  };
  for (auto &w : wanted)
    R("  [%s] %-36s  %s\n", s_hasExt(w.ext, n) ? "x" : " ", w.ext, w.why);
  R("\n");

  R("== Full extension list ==\n");
  for (int i = 0; i < n; ++i)
    R("  %s\n", (const char *)glGetStringi(GL_EXTENSIONS, i));
}

int main(int argc, char *argv[]) {
  s_report = fopen("sdmc:/glprobe_report.txt", "w");

  bool ok = initEgl(nwindowGetDefault());
  if (ok) {
    gladLoadGL();
    dumpCaps();
    R("\n[done] Report saved to sdmc:/glprobe_report.txt\n");
  } else {
    R("[FAIL] Could not create an OpenGL context via EGL.\n");
  }

  if (s_report) {
    fclose(s_report);
    s_report = nullptr;
  }

  float r = ok ? 0.15f : 0.6f, g = ok ? 0.6f : 0.1f, b = 0.2f;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
      break;
    if (s_display && s_surface) {
      glClearColor(r, g, b, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      eglSwapBuffers(s_display, s_surface);
    }
  }

  if (s_display) {
    eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s_context)
      eglDestroyContext(s_display, s_context);
    if (s_surface)
      eglDestroySurface(s_display, s_surface);
    eglTerminate(s_display);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
