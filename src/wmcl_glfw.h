/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WMCL_GLFW_H
#define WMCL_GLFW_H

#ifdef __cplusplus
extern "C" {
#endif

/* Supplied by the Android EGL owner before glfwInit. The callbacks run on the client thread. */
typedef struct WmclGlfwHost {
    int width;
    int height;
    int (*make_current)(int attach);
    int (*swap_buffers)(void);
    void (*swap_interval)(int interval);
    void (*(*get_proc_address)(const char *name))(void);
} WmclGlfwHost;

/* A single attached Android surface is supported. Attach/detach on the client thread. */
int wmcl_glfw_attach(const WmclGlfwHost *host);
void wmcl_glfw_detach(void);

/* Android producer calls, consumed by GLFW on the client thread. Zero means not queued. */
int wmcl_glfw_post_touch(double x, double y, int action);
int wmcl_glfw_post_resize(int width, int height);
void wmcl_glfw_request_close(void);

#ifdef __cplusplus
}
#endif
#endif
