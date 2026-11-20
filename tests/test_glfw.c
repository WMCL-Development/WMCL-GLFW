/* SPDX-License-Identifier: Apache-2.0 */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)
#include "wmcl_glfw.h"

static int current, frames, interval, motions, clicks, resize_count;
static int bind(int attach) { current = attach; return 1; }
static int swap(void) { frames++; return current; }
static void set_interval(int n) { interval = n; }
static void procedure(void) { }
static void (*lookup(const char *name))(void) { return name ? procedure : 0; }
static void motion(GLFWwindow *w, double x, double y) { CHECK(w && x == 20 && y == 30); motions++; }
static void mouse(GLFWwindow *w, int key, int action, int mods)
{
    CHECK(w && key == GLFW_MOUSE_BUTTON_LEFT && mods == 0);
    CHECK(action == (clicks == 0 ? GLFW_PRESS : GLFW_RELEASE));
    clicks++;
}
static void resize(GLFWwindow *w, int x, int y) { CHECK(w && x == 1920 && y == 1080); resize_count++; }

int smoke_main(int argc, char **argv)
{
    (void)argc; (void)argv;
    CHECK(!glfwInit());
    WmclGlfwHost host = { 1280, 720, bind, swap, set_interval, lookup };
    CHECK(wmcl_glfw_attach(&host) && !wmcl_glfw_attach(&host) && glfwInit());
    int count;
    GLFWmonitor **m = glfwGetMonitors(&count);
    CHECK(count == 1 && m[0] == glfwGetPrimaryMonitor() && glfwGetVideoMode(m[0])->width == 1280);
    GLFWwindow *w = glfwCreateWindow(640, 480, "WMCL", 0, 0);
    CHECK(w && !glfwCreateWindow(640, 480, "second", 0, 0));
    glfwMakeContextCurrent(w);
    CHECK(current && glfwGetCurrentContext() == w && glfwGetProcAddress("glClear") == procedure);
    glfwSwapInterval(1);
    glfwSwapBuffers(w);
    CHECK(interval == 1 && frames == 1);
    CHECK(glfwSetCursorPosCallback(w, motion) == 0);
    CHECK(glfwSetMouseButtonCallback(w, mouse) == 0);
    CHECK(glfwSetFramebufferSizeCallback(w, resize) == 0);
    CHECK(wmcl_glfw_post_touch(20, 30, GLFW_PRESS));
    CHECK(wmcl_glfw_post_touch(20, 30, GLFW_RELEASE));
    CHECK(wmcl_glfw_post_resize(1920, 1080));
    glfwPollEvents();
    CHECK(motions == 2 && clicks == 2 && resize_count == 1);
    int width, height;
    glfwGetFramebufferSize(w, &width, &height);
    CHECK(width == 1920 && height == 1080);
    CHECK(glfwSetCursorPosCallback(w, 0) == motion);
    CHECK(glfwSetMouseButtonCallback(w, 0) == mouse);
    CHECK(glfwSetFramebufferSizeCallback(w, 0) == resize);
    count = 0;
    while (count < 128 && wmcl_glfw_post_resize(1920, 1080)) count++;
    CHECK(count == 64);
    wmcl_glfw_request_close();
    CHECK(glfwWindowShouldClose(w));
    glfwDestroyWindow(w);
    CHECK(!current && !glfwGetCurrentContext());
    glfwTerminate();
    wmcl_glfw_detach();
    CHECK(!wmcl_glfw_post_resize(640, 480));
    CHECK(wmcl_glfw_attach(&host) && glfwInit());
    w = glfwCreateWindow(640, 480, "restart", 0, 0);
    CHECK(w && !glfwWindowShouldClose(w));
    wmcl_glfw_detach();
    if (failures) return 1;
    puts("WMCL_GLFW_C_CONTRACT_OK");
    return 0;
}