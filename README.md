# wmcl-glfw

A single-window GLFW implementation for Android, built for WMCL. It exposes the
complete `glfw*` symbol table that the official LWJGL 3.3.3 bindings resolve
during class initialisation, backed by one Android `Surface` and one EGL
context.

## Why this exists

LWJGL's `GLFW$Functions` class initialiser resolves **all 117** `glfw*` entry
points eagerly. Supplying only the 47 functions that Minecraft 1.21.1 calls
directly fails with:

```
NullPointerException: A required function is missing: glfwInitHint
```

Desktop GLFW cannot be used either: it wants X11 or Wayland and creates its own
window, while Android has neither and the window belongs to the app.

So this library implements the ABI for a case GLFW does not cover: exactly one
window, created by the host, on a surface the host owns.

## Host contract

The host (see `src/wmcl_glfw.h`) attaches before `glfwInit`:

```c
typedef struct WmclGlfwHost {
    int width, height;
    int (*make_current)(int attach);
    int (*swap_buffers)(void);
    void (*swap_interval)(int interval);
    void (*(*get_proc_address)(const char *name))(void);
} WmclGlfwHost;

int  wmcl_glfw_attach(const WmclGlfwHost *host);
void wmcl_glfw_detach(void);
int  wmcl_glfw_post_touch(double x, double y, int action);
int  wmcl_glfw_post_resize(int width, int height);
void wmcl_glfw_request_close(void);
```

Events are queued by the Android thread and drained by `glfwPollEvents` on the
GL thread. The queue holds 64 events and drops new ones when full, except for
the close flag, which is out of band and never lost.

## What is real and what is not

Real: window creation/destruction, context current, buffer swap, framebuffer and
window size, video mode and monitor enumeration, cursor position, mouse button,
key/char/scroll/drop callback registration, clipboard, monotonic clock, close
flag.

Not real on Android: monitors are virtual (one entry with the surface size),
fullscreen/monitor switching is a no-op, and cursor shapes, gamma, joysticks and
gamepads report `GLFW_FEATURE_UNAVAILABLE` instead of pretending to work.

## Building

```bash
# With the Android NDK
scripts/build-android.sh --out /tmp/wmcl-glfw \
  --glfw-headers /path/to/glfw-3.4/include --ndk /path/to/android-ndk-r27

# With an existing Android arm64 sysroot (links against a real Bionic libc)
scripts/build-android.sh --out /tmp/wmcl-glfw \
  --glfw-headers /path/to/glfw-3.4/include \
  --sysroot /path/to/sysroot --bionic-libc /path/to/sysroot/lib/libc.so
```

Both modes produce a library that depends only on `libc.so`, is 16 KiB page
aligned, and carries `soname=libwmcl_glfw.so`. The script refuses a
`glfw3.h` whose SHA-256 is not the pinned GLFW 3.4 header.

## Tests

`tests/test_glfw.c` drives the ABI natively: single window, re-open after
detach, callback replacement, queue saturation, close flag, EGL release.
`tests/WmclGlfwContractProbe.java` runs the same path through the unmodified
LWJGL Java bindings, including real libffi callbacks.

The platform-specific harness used to link these against Bionic lives in the
WMCL tree; the sources here are self-contained and take a host-provided
`WmclGlfwHost`.

## License

Apache-2.0. The ABI itself follows GLFW 3.4 (`GLFW/glfw3.h`), whose header is
not vendored here.
