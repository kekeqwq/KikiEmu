// A host-only WGL check. This does not start QEMU or the Windows hypervisor.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>

#ifdef KIKI_PROBE_VIRGL
#include <virgl/virglrenderer.h>

static HDC probe_dc;
static HGLRC probe_base_context;
static int probe_cookie;

static void *probe_get_egl_display(void *cookie) {
    (void)cookie;
    return NULL;
}

static void probe_write_fence(void *cookie, uint32_t fence) {
    (void)cookie;
    (void)fence;
}

static void probe_write_context_fence(void *cookie, uint32_t context,
                                      uint32_t ring, uint64_t fence) {
    (void)cookie;
    (void)context;
    (void)ring;
    (void)fence;
}

static virgl_renderer_gl_context probe_create_context(
        void *cookie, int scanout, struct virgl_renderer_gl_ctx_param *params) {
    (void)cookie;
    (void)scanout;
    HGLRC context = wglCreateContext(probe_dc);
    if (context && params->shared &&
        !wglShareLists(probe_base_context, context)) {
        wglDeleteContext(context);
        return NULL;
    }
    return context;
}

static void probe_destroy_context(void *cookie, virgl_renderer_gl_context ctx) {
    (void)cookie;
    wglDeleteContext((HGLRC)ctx);
}

static int probe_make_current(void *cookie, int scanout,
                              virgl_renderer_gl_context ctx) {
    (void)cookie;
    (void)scanout;
    return wglMakeCurrent(ctx ? probe_dc : NULL, (HGLRC)ctx) ? 0 : -1;
}
#endif

int main(void) {
    HWND window = CreateWindowExA(0, "STATIC", "Kiki WGL probe", 0,
                                 0, 0, 1, 1, NULL, NULL,
                                 GetModuleHandleA(NULL), NULL);
    if (!window) {
        fprintf(stderr, "CreateWindowEx failed: %lu\n", GetLastError());
        return 1;
    }

    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format = {0};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.iLayerType = PFD_MAIN_PLANE;

    int pixel_format = ChoosePixelFormat(dc, &format);
    if (!pixel_format || !SetPixelFormat(dc, pixel_format, &format)) {
        fprintf(stderr, "WGL pixel format failed: %lu\n", GetLastError());
        ReleaseDC(window, dc);
        DestroyWindow(window);
        return 2;
    }

    HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) {
        fprintf(stderr, "WGL context failed: %lu\n", GetLastError());
        if (context) wglDeleteContext(context);
        ReleaseDC(window, dc);
        DestroyWindow(window);
        return 3;
    }

    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
           vendor ? (const char *)vendor : "<null>",
           renderer ? (const char *)renderer : "<null>",
           version ? (const char *)version : "<null>");

#ifdef KIKI_PROBE_VIRGL
    probe_dc = dc;
    probe_base_context = context;
    struct virgl_renderer_callbacks callbacks = {0};
    callbacks.version = VIRGL_RENDERER_CALLBACKS_VERSION;
    callbacks.write_fence = probe_write_fence;
    callbacks.write_context_fence = probe_write_context_fence;
    callbacks.create_gl_context = probe_create_context;
    callbacks.destroy_gl_context = probe_destroy_context;
    callbacks.make_current = probe_make_current;
#ifdef KIKI_PROBE_ASYNC_VIRGL
    // Exercise virglrenderer async/thread-sync initialization on a WGL context
    // with no EGL display before changing QEMU's production init path.
    callbacks.get_egl_display = probe_get_egl_display;
    int virgl_flags = VIRGL_RENDERER_ASYNC_FENCE_CB | VIRGL_RENDERER_THREAD_SYNC;
#elif defined(KIKI_PROBE_THREAD_SYNC_VIRGL)
    // Check whether WGL can activate thread sync; poll_fd == -1 means it was
    // ignored, as virgl_renderer_get_poll_fd documents for this feature.
    int virgl_flags = VIRGL_RENDERER_THREAD_SYNC;
#else
    int virgl_flags = 0;
#endif
    // virglrenderer rejects a NULL cookie even when callbacks do not use it.
    int virgl_result = virgl_renderer_init(&probe_cookie, virgl_flags, &callbacks);
    printf("VIRGL_INIT=%d\n", virgl_result);
    if (virgl_result == 0) {
        printf("VIRGL_FLAGS=0x%X POLL_FD=%d\n", virgl_flags,
               virgl_renderer_get_poll_fd());
        uint32_t max_version = 0, max_size = 0;
        virgl_renderer_get_cap_set(1, &max_version, &max_size);
        printf("VIRGL_CAPSET_1_VERSION=%u SIZE=%u\n", max_version, max_size);
        virgl_renderer_cleanup(&probe_cookie);
    }
#endif

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
#ifdef KIKI_PROBE_VIRGL
    return vendor && renderer && version && virgl_result == 0 ? 0 : 4;
#else
    return vendor && renderer && version ? 0 : 4;
#endif
}
