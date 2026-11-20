import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWCursorPosCallback;
import org.lwjgl.glfw.GLFWFramebufferSizeCallback;
import org.lwjgl.glfw.GLFWMouseButtonCallback;
import org.lwjgl.glfw.GLFWVidMode;
import org.lwjgl.system.JNI;
import org.lwjgl.system.linux.DynamicLinkLoader;

/** Uses the unmodified LWJGL Java bindings and real libffi callbacks, not an actual Android EGL context. */
public final class WmclGlfwContractProbe {
    private static int points, buttons, sizes;
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static long symbol(long lib, String name) {
        long address = DynamicLinkLoader.dlsym(lib, name);
        check(address != 0, "missing " + name);
        return address;
    }
    public static void main(String[] args) {
        long shim = DynamicLinkLoader.dlopen(args[0], DynamicLinkLoader.RTLD_NOW);
        check(shim != 0, "cannot load shim");
        check(JNI.callI(symbol(shim, "wmcl_test_attach")) == 1, "host attach failed");
        check(GLFW.glfwInit(), "glfwInit failed");
        long monitor = GLFW.glfwGetPrimaryMonitor();
        check(monitor != 0 && GLFW.glfwGetMonitors().remaining() == 1, "virtual monitor missing");
        GLFWVidMode mode = GLFW.glfwGetVideoMode(monitor);
        check(mode != null && mode.width() == 1280 && mode.height() == 720, "virtual video mode missing");
        long window = GLFW.glfwCreateWindow(640, 480, "WMCL", 0, 0);
        check(window != 0, "window missing");
        GLFW.glfwMakeContextCurrent(window);
        check(GLFW.glfwGetCurrentContext() == window, "context not attached");
        GLFW.glfwSwapBuffers(window);
        check(GLFW.glfwGetProcAddress("wmcl_test_proc") != 0, "procedure not found");
        GLFWCursorPosCallback cursor = GLFWCursorPosCallback.create((w, x, y) -> {
            if (w == window && x == 20 && y == 30) points++;
        });
        GLFWMouseButtonCallback mouse = GLFWMouseButtonCallback.create((w, button, action, modifiers) -> {
            if (w == window && button == GLFW.GLFW_MOUSE_BUTTON_LEFT) buttons++;
        });
        GLFWFramebufferSizeCallback size = GLFWFramebufferSizeCallback.create((w, width, height) -> {
            if (w == window && width == 1920 && height == 1080) sizes++;
        });
        try {
            check(GLFW.glfwSetCursorPosCallback(window, cursor) == null, "first cursor registration");
            check(GLFW.glfwSetMouseButtonCallback(window, mouse) == null, "first button registration");
            check(GLFW.glfwSetFramebufferSizeCallback(window, size) == null, "first resize registration");
            check(JNI.callI(symbol(shim, "wmcl_test_post_events")) == 1, "queue failed");
            GLFW.glfwPollEvents();
            check(points == 2 && buttons == 2 && sizes == 1, "callback trace mismatch");
            int[] width = {0}, height = {0};
            GLFW.glfwGetFramebufferSize(window, width, height);
            check(width[0] == 1920 && height[0] == 1080, "size update missing");
            check(GLFW.glfwSetCursorPosCallback(window, null).address() == cursor.address(), "previous cursor callback");
            check(GLFW.glfwSetMouseButtonCallback(window, null).address() == mouse.address(), "previous mouse callback");
            check(GLFW.glfwSetFramebufferSizeCallback(window, null).address() == size.address(), "previous resize callback");
        } finally {
            GLFW.glfwSetCursorPosCallback(window, null);
            GLFW.glfwSetMouseButtonCallback(window, null);
            GLFW.glfwSetFramebufferSizeCallback(window, null);
            cursor.free(); mouse.free(); size.free();
        }
        JNI.callV(symbol(shim, "wmcl_glfw_request_close"));
        check(GLFW.glfwWindowShouldClose(window), "close flag missing");
        GLFW.glfwDestroyWindow(window);
        check(GLFW.glfwGetCurrentContext() == 0, "current context leaked");
        GLFW.glfwTerminate();
        JNI.callV(symbol(shim, "wmcl_glfw_detach"));
        System.out.println("WMCL_GLFW_CONTRACT_OK: window, monitor, context, callbacks, close");
    }
}
