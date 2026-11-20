/* SPDX-License-Identifier: Apache-2.0 */
#include "wmcl_glfw.h"
#include <string.h>

static int current;
static int bind(int attach) { current = attach; return 1; }
static int swap(void) { return current; }
static void stub(void) { }
static void (*lookup(const char *name))(void)
{
    return strcmp(name, "wmcl_test_proc") == 0 ? stub : 0;
}
int wmcl_test_attach(void)
{
    WmclGlfwHost host = {1280, 720, bind, swap, 0, lookup};
    return wmcl_glfw_attach(&host);
}
int wmcl_test_post_events(void)
{
    return wmcl_glfw_post_touch(20, 30, 1) && wmcl_glfw_post_touch(20, 30, 0) && wmcl_glfw_post_resize(1920, 1080);
}
