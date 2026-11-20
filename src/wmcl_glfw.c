/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wmcl_glfw.h"

typedef struct {
    int width, height, focused, cursor_mode, mouse_down;
    double cursor_x, cursor_y;
    void *user;
    GLFWwindowposfun position;
    GLFWwindowsizefun size;
    GLFWwindowclosefun close;
    GLFWwindowfocusfun focus;
    GLFWframebuffersizefun framebuffer;
    GLFWcursorposfun cursor;
    GLFWcursorenterfun cursor_enter;
    GLFWmousebuttonfun mouse;
    GLFWkeyfun key;
    GLFWcharfun character;
    GLFWcharmodsfun char_mods;
    GLFWscrollfun scroll;
    GLFWdropfun drop;
    GLFWwindowrefreshfun refresh;
    GLFWwindowiconifyfun iconify;
    GLFWwindowmaximizefun maximize;
    GLFWwindowcontentscalefun scale;
} Window;

typedef struct { int width, height, action; double x, y; } Event;
#define EVENT_CAPACITY 64
static Event events[EVENT_CAPACITY];
static atomic_uint read_pos, write_pos;
static atomic_bool accepting, close_requested;
static bool attached, initialized, window_exists, current;
static WmclGlfwHost host;
static Window window;
static GLFWmonitor *monitor;
static char monitor_id;
static GLFWvidmode video_mode;
static GLFWerrorfun error_cb;
static GLFWmonitorfun monitor_cb;
static GLFWjoystickfun joystick_cb;
static void *monitor_user;
static int pending_error;
static const char *pending_description = "";
static double clock_offset;
static uint64_t clock_origin;
static char *clipboard;

static uint64_t clock_now(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void fail(int code, const char *what)
{
    pending_error = code;
    pending_description = what;
    if (error_cb) error_cb(code, what);
}

static bool valid(GLFWwindow *handle)
{
    return window_exists && handle == (GLFWwindow *)&window;
}

static bool monitor_valid(GLFWmonitor *value)
{
    return attached && value == monitor;
}

int wmcl_glfw_attach(const WmclGlfwHost *value)
{
    if (!value || attached || value->width < 1 || value->height < 1 || !value->make_current ||
        !value->swap_buffers || !value->get_proc_address) return 0;
    host = *value;
    monitor = (GLFWmonitor *)&monitor_id;
    video_mode = (GLFWvidmode){ value->width, value->height, 8, 8, 8, 60 };
    atomic_store(&read_pos, 0);
    atomic_store(&write_pos, 0);
    atomic_store(&close_requested, false);
    atomic_store(&accepting, true);
    attached = true;
    return 1;
}

void wmcl_glfw_request_close(void) { atomic_store(&close_requested, true); }

/* Single Android UI producer, single GLFW consumer. Close stays out-of-band when full. */
static int enqueue(Event value)
{
    if (!atomic_load_explicit(&accepting, memory_order_acquire)) return 0;
    unsigned tail = atomic_load_explicit(&write_pos, memory_order_relaxed);
    if (tail - atomic_load_explicit(&read_pos, memory_order_acquire) >= EVENT_CAPACITY) return 0;
    events[tail % EVENT_CAPACITY] = value;
    atomic_store_explicit(&write_pos, tail + 1, memory_order_release);
    return 1;
}

int wmcl_glfw_post_touch(double x, double y, int action)
{
    if (action != GLFW_RELEASE && action != GLFW_PRESS && action != GLFW_REPEAT) return 0;
    return enqueue((Event){ .x=x, .y=y, .action=action });
}

int wmcl_glfw_post_resize(int width, int height)
{
    if (width < 1 || height < 1) return 0;
    return enqueue((Event){ .width=width, .height=height });
}

void wmcl_glfw_detach(void)
{
    atomic_store(&accepting, false);
    // Release is only needed if the Android client attached its context through this ABI.
    if (current) host.make_current(0);
    current = window_exists = initialized = attached = false;
    error_cb = NULL;
    monitor_cb = NULL;
    joystick_cb = NULL;
    monitor_user = NULL;
    pending_error = 0;
    pending_description = "";
    free(clipboard);
    clipboard = NULL;
    memset(&window, 0, sizeof window);
}

int glfwInit(void)
{
    if (!attached) { fail(GLFW_PLATFORM_UNAVAILABLE, "WMCL Android Surface is not attached"); return GLFW_FALSE; }
    if (!initialized) { initialized = true; clock_origin = clock_now(); clock_offset = 0; }
    return GLFW_TRUE;
}

void glfwTerminate(void)
{
    if (current) host.make_current(0);
    current = window_exists = initialized = false;
    monitor_cb = NULL;
    joystick_cb = NULL;
    monitor_user = NULL;
    free(clipboard);
    clipboard = NULL;
    memset(&window, 0, sizeof window);
}

void glfwGetVersion(int *a, int *b, int *c) { if (a) *a=3; if (b) *b=4; if (c) *c=0; }
const char *glfwGetVersionString(void) { return "3.4.0 WMCL Android single-surface shim"; }
int glfwGetPlatform(void) { return GLFW_PLATFORM_NULL; }
int glfwPlatformSupported(int platform) { return platform == GLFW_PLATFORM_NULL; }
int glfwGetError(const char **text)
{
    if (text) *text = pending_error ? pending_description : NULL;
    int code=pending_error;
    pending_error=0;
    return code;
}
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun next) { GLFWerrorfun old=error_cb; error_cb=next; return old; }

GLFWmonitor **glfwGetMonitors(int *count) { if (count) *count=attached ? 1 : 0; return attached ? &monitor : NULL; }
GLFWmonitor *glfwGetPrimaryMonitor(void) { return attached ? monitor : NULL; }
void glfwGetMonitorPos(GLFWmonitor *m, int *x, int *y) { (void)m; if (x) *x=0; if (y) *y=0; }
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *m, int *count)
{
    if (count) *count=monitor_valid(m) ? 1 : 0;
    return monitor_valid(m) ? &video_mode : NULL;
}
const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *m) { return glfwGetVideoModes(m, NULL); }
GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun next) { GLFWmonitorfun old=monitor_cb; monitor_cb=next; return old; }

void glfwDefaultWindowHints(void) { /* The supplied Android EGL configuration owns the context. */ }
void glfwWindowHint(int key, int value) { (void)key; (void)value; }
GLFWwindow *glfwCreateWindow(int w, int h, const char *title, GLFWmonitor *display, GLFWwindow *share)
{
    (void)title;
    if (!initialized || window_exists || share || w <= 0 || h <= 0 || (display && !monitor_valid(display))) {
        fail(GLFW_PLATFORM_ERROR, "WMCL supports one Android surface and no shared contexts"); return NULL;
    }
    window = (Window){ .width=video_mode.width, .height=video_mode.height, .focused=GLFW_TRUE, .cursor_mode=GLFW_CURSOR_NORMAL };
    window_exists = true;
    return (GLFWwindow *)&window;
}
void glfwDestroyWindow(GLFWwindow *w)
{
    if (!valid(w)) return;
    if (current) host.make_current(0);
    window_exists = current = false;
    memset(&window, 0, sizeof window);
}
void glfwMakeContextCurrent(GLFWwindow *w)
{
    if (!initialized || (w && !valid(w))) { fail(GLFW_NO_WINDOW_CONTEXT, "No WMCL EGL window"); return; }
    if (!host.make_current(w != NULL)) { fail(GLFW_PLATFORM_ERROR, "WMCL EGL context bind failed"); return; }
    current = w != NULL;
}
GLFWwindow *glfwGetCurrentContext(void) { return current ? (GLFWwindow *)&window : NULL; }
void glfwSwapBuffers(GLFWwindow *w)
{
    if (!valid(w) || !current || !host.swap_buffers()) fail(GLFW_PLATFORM_ERROR, "WMCL EGL swap failed");
}
void glfwSwapInterval(int interval) { if (current && host.swap_interval) host.swap_interval(interval); }
GLFWglproc glfwGetProcAddress(const char *name) { return current && name ? host.get_proc_address(name) : NULL; }
void glfwGetFramebufferSize(GLFWwindow *w, int *x, int *y)
{
    if (x) *x=valid(w) ? window.width : 0;
    if (y) *y=valid(w) ? window.height : 0;
}
void glfwGetWindowSize(GLFWwindow *w, int *x, int *y) { glfwGetFramebufferSize(w, x, y); }
void glfwGetWindowPos(GLFWwindow *w, int *x, int *y) { (void)w; if (x) *x=0; if (y) *y=0; }
GLFWmonitor *glfwGetWindowMonitor(GLFWwindow *w) { (void)w; return NULL; }
void glfwSetWindowMonitor(GLFWwindow *w, GLFWmonitor *m, int x, int y, int width, int height, int refresh)
{
    (void)w; (void)m; (void)x; (void)y; (void)width; (void)height; (void)refresh;
    /* Android, not the virtual monitor, decides the actual surface dimensions. */
}
int glfwWindowShouldClose(GLFWwindow *w) { return !valid(w) || atomic_load(&close_requested); }
void glfwSetWindowShouldClose(GLFWwindow *w, int value) { if (valid(w)) atomic_store(&close_requested, value != 0); }
void glfwSetWindowTitle(GLFWwindow *w, const char *text) { (void)w; (void)text; }
void glfwSetWindowIcon(GLFWwindow *w, int n, const GLFWimage *icons) { (void)w; (void)n; (void)icons; }
void glfwSetWindowSizeLimits(GLFWwindow *w, int a, int b, int c, int d) { (void)w; (void)a; (void)b; (void)c; (void)d; }

double glfwGetTime(void) { return initialized ? (double)(clock_now() - clock_origin) / 1000000000.0 + clock_offset : 0; }
void glfwSetTime(double t) { if (initialized && t >= 0 && t < 18446744073.0) { clock_origin=clock_now(); clock_offset=t; } }
uint64_t glfwGetTimerValue(void) { return clock_now(); }
uint64_t glfwGetTimerFrequency(void) { return UINT64_C(1000000000); }

#define WINDOW_CALLBACK(Function, Type, Slot) \
    Type glfwSet##Function##Callback(GLFWwindow *w, Type next) { \
        if (!valid(w)) return NULL; \
        Type old=window.Slot; window.Slot=next; return old; \
    }
WINDOW_CALLBACK(WindowPos, GLFWwindowposfun, position)
WINDOW_CALLBACK(WindowSize, GLFWwindowsizefun, size)
WINDOW_CALLBACK(WindowClose, GLFWwindowclosefun, close)
WINDOW_CALLBACK(WindowFocus, GLFWwindowfocusfun, focus)
WINDOW_CALLBACK(FramebufferSize, GLFWframebuffersizefun, framebuffer)
WINDOW_CALLBACK(CursorPos, GLFWcursorposfun, cursor)
WINDOW_CALLBACK(CursorEnter, GLFWcursorenterfun, cursor_enter)
WINDOW_CALLBACK(MouseButton, GLFWmousebuttonfun, mouse)
WINDOW_CALLBACK(Key, GLFWkeyfun, key)
WINDOW_CALLBACK(Char, GLFWcharfun, character)
WINDOW_CALLBACK(CharMods, GLFWcharmodsfun, char_mods)
WINDOW_CALLBACK(Scroll, GLFWscrollfun, scroll)
WINDOW_CALLBACK(Drop, GLFWdropfun, drop)
WINDOW_CALLBACK(WindowRefresh, GLFWwindowrefreshfun, refresh)
WINDOW_CALLBACK(WindowIconify, GLFWwindowiconifyfun, iconify)
WINDOW_CALLBACK(WindowMaximize, GLFWwindowmaximizefun, maximize)
WINDOW_CALLBACK(WindowContentScale, GLFWwindowcontentscalefun, scale)

void glfwPollEvents(void)
{
    GLFWwindow *w = (GLFWwindow *)&window;
    unsigned head=atomic_load_explicit(&read_pos, memory_order_relaxed);
    unsigned tail=atomic_load_explicit(&write_pos, memory_order_acquire);
    while (head != tail) {
        Event ev=events[head++ % EVENT_CAPACITY];
        atomic_store_explicit(&read_pos, head, memory_order_release);
        if (!window_exists) continue;
        if (ev.width) {
            video_mode.width=window.width=ev.width;
            video_mode.height=window.height=ev.height;
            if (window.size) window.size(w, ev.width, ev.height);
            if (window.framebuffer) window.framebuffer(w, ev.width, ev.height);
            continue;
        }
        window.cursor_x=ev.x; window.cursor_y=ev.y;
        if (window.cursor) window.cursor(w, ev.x, ev.y);
        if (ev.action != GLFW_REPEAT && window.mouse_down != ev.action) {
            window.mouse_down=ev.action;
            if (window.mouse) window.mouse(w, GLFW_MOUSE_BUTTON_LEFT, ev.action, 0);
        }
    }
}
void glfwWaitEventsTimeout(double timeout)
{
    if (timeout > 0 && !atomic_load(&close_requested) && atomic_load(&read_pos) == atomic_load(&write_pos)) {
        // Short waits retain responsiveness without introducing a condition-variable dependency in the shim.
        if (timeout > .016) timeout=.016;
        struct timespec sleep = { .tv_sec=0, .tv_nsec=(long)(timeout*1000000000.0) };
        nanosleep(&sleep, NULL);
    }
    glfwPollEvents();
}
void glfwWaitEvents(void) { glfwWaitEventsTimeout(.016); }
void glfwPostEmptyEvent(void) { }
void glfwGetCursorPos(GLFWwindow *w, double *x, double *y)
{
    if (x) *x=valid(w) ? window.cursor_x : 0;
    if (y) *y=valid(w) ? window.cursor_y : 0;
}
void glfwSetCursorPos(GLFWwindow *w, double x, double y) { if (valid(w)) { window.cursor_x=x; window.cursor_y=y; } }
int glfwGetMouseButton(GLFWwindow *w, int button) { return valid(w) && button == GLFW_MOUSE_BUTTON_LEFT ? window.mouse_down : GLFW_RELEASE; }
int glfwGetKey(GLFWwindow *w, int key) { (void)w; (void)key; return GLFW_RELEASE; }
const char *glfwGetKeyName(int key, int scancode) { (void)key; (void)scancode; return NULL; }
int glfwGetKeyScancode(int key) { return key; }
int glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }
void glfwSetInputMode(GLFWwindow *w, int key, int value) { if (valid(w) && key == GLFW_CURSOR) window.cursor_mode=value; }
int glfwGetInputMode(GLFWwindow *w, int key) { return valid(w) && key == GLFW_CURSOR ? window.cursor_mode : 0; }
void glfwSetClipboardString(GLFWwindow *w, const char *value)
{
    (void)w;
    if (!value) return;
    char *copy=strdup(value);
    if (copy) { free(clipboard); clipboard=copy; }
}
const char *glfwGetClipboardString(GLFWwindow *w) { (void)w; return clipboard; }

/* Official LWJGL resolves the complete GLFW table on first use. Unsupported Android
 * desktop operations expose their typed GLFW entry and report unavailable where appropriate. */
static void unavailable(void) { fail(GLFW_FEATURE_UNAVAILABLE, "Operation is unavailable on WMCL's Android Surface"); }
void glfwInitHint(int hint, int value) { (void)hint; (void)value; }
void glfwInitAllocator(const GLFWallocator *value) { if (value) unavailable(); }
const char *glfwGetMonitorName(GLFWmonitor *m) { return monitor_valid(m) ? "Android Surface" : NULL; }
void glfwGetMonitorWorkarea(GLFWmonitor *m, int *x, int *y, int *w, int *h)
{
    glfwGetMonitorPos(m,x,y);
    if(w) *w=monitor_valid(m) ? video_mode.width : 0;
    if(h) *h=monitor_valid(m) ? video_mode.height : 0;
}
void glfwGetMonitorPhysicalSize(GLFWmonitor *m, int *x, int *y) { glfwGetMonitorPos(m,x,y); }
void glfwGetMonitorContentScale(GLFWmonitor *m, float *x, float *y) { (void)m; if(x)*x=1; if(y)*y=1; }
void glfwSetMonitorUserPointer(GLFWmonitor *m, void *p) { if(monitor_valid(m)) monitor_user=p; }
void *glfwGetMonitorUserPointer(GLFWmonitor *m) { return monitor_valid(m) ? monitor_user : NULL; }
void glfwSetGamma(GLFWmonitor *m, float gamma) { (void)m; (void)gamma; unavailable(); }
const GLFWgammaramp *glfwGetGammaRamp(GLFWmonitor *m) { (void)m; unavailable(); return NULL; }
void glfwSetGammaRamp(GLFWmonitor *m, const GLFWgammaramp *ramp) { (void)m; (void)ramp; unavailable(); }
void glfwWindowHintString(int hint, const char *value) { (void)hint; (void)value; }
void glfwSetWindowPos(GLFWwindow *w, int x, int y) { (void)w; (void)x; (void)y; }
void glfwSetWindowSize(GLFWwindow *w, int width, int height) { (void)w; (void)width; (void)height; }
void glfwSetWindowAspectRatio(GLFWwindow *w, int x, int y) { (void)w; (void)x; (void)y; }
void glfwGetWindowFrameSize(GLFWwindow *w, int *l, int *t, int *r, int *b)
{ (void)w; if(l)*l=0; if(t)*t=0; if(r)*r=0; if(b)*b=0; }
void glfwGetWindowContentScale(GLFWwindow *w, float *x, float *y) { (void)w; if(x)*x=1; if(y)*y=1; }
float glfwGetWindowOpacity(GLFWwindow *w) { return valid(w) ? 1 : 0; }
void glfwSetWindowOpacity(GLFWwindow *w, float opacity) { (void)w; if (opacity != 1) unavailable(); }
int glfwGetWindowAttrib(GLFWwindow *w, int attr)
{
    if(!valid(w)) return 0;
    switch(attr) {
    case GLFW_FOCUSED: return window.focused;
    case GLFW_VISIBLE: case GLFW_RESIZABLE: return GLFW_TRUE;
    case GLFW_CLIENT_API: return GLFW_OPENGL_ES_API;
    case GLFW_CONTEXT_CREATION_API: return GLFW_EGL_CONTEXT_API;
    default: return 0;
    }
}
void glfwSetWindowAttrib(GLFWwindow *w, int attr, int value) { (void)w; (void)attr; (void)value; }
void glfwSetWindowUserPointer(GLFWwindow *w, void *p) { if(valid(w)) window.user=p; }
void *glfwGetWindowUserPointer(GLFWwindow *w) { return valid(w) ? window.user : NULL; }
void glfwIconifyWindow(GLFWwindow *w) { (void)w; }
void glfwRestoreWindow(GLFWwindow *w) { (void)w; }
void glfwMaximizeWindow(GLFWwindow *w) { (void)w; }
void glfwShowWindow(GLFWwindow *w) { (void)w; }
void glfwHideWindow(GLFWwindow *w) { (void)w; }
void glfwFocusWindow(GLFWwindow *w) { (void)w; }
void glfwRequestWindowAttention(GLFWwindow *w) { (void)w; }
GLFWcursor *glfwCreateCursor(const GLFWimage *image, int x, int y)
{ (void)image; (void)x; (void)y; unavailable(); return NULL; }
GLFWcursor *glfwCreateStandardCursor(int shape) { (void)shape; unavailable(); return NULL; }
void glfwDestroyCursor(GLFWcursor *cursor) { (void)cursor; }
void glfwSetCursor(GLFWwindow *w, GLFWcursor *cursor) { (void)w; (void)cursor; }
int glfwJoystickPresent(int id) { (void)id; return GLFW_FALSE; }
const float *glfwGetJoystickAxes(int id, int *count) { (void)id; if(count)*count=0; return NULL; }
const unsigned char *glfwGetJoystickButtons(int id, int *count) { (void)id; if(count)*count=0; return NULL; }
const unsigned char *glfwGetJoystickHats(int id, int *count) { (void)id; if(count)*count=0; return NULL; }
const char *glfwGetJoystickName(int id) { (void)id; return NULL; }
const char *glfwGetJoystickGUID(int id) { (void)id; return NULL; }
void glfwSetJoystickUserPointer(int id, void *p) { (void)id; (void)p; }
void *glfwGetJoystickUserPointer(int id) { (void)id; return NULL; }
int glfwJoystickIsGamepad(int id) { (void)id; return GLFW_FALSE; }
GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun next) { GLFWjoystickfun old=joystick_cb; joystick_cb=next; return old; }
int glfwUpdateGamepadMappings(const char *value) { (void)value; return GLFW_FALSE; }
const char *glfwGetGamepadName(int id) { (void)id; return NULL; }
int glfwGetGamepadState(int id, GLFWgamepadstate *state) { (void)id; (void)state; return GLFW_FALSE; }
int glfwExtensionSupported(const char *name) { (void)name; return GLFW_FALSE; }
