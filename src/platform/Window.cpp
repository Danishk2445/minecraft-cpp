#include "platform/Window.h"

#include <cstdlib>

#include <GLFW/glfw3.h>

#include "core/Log.h"
#include "gfx/GL.h"

namespace mc {
namespace {

void glfwErrorCallback(int code, const char* description) { log::error("GLFW error {}: {}", code, description); }

Window* self(GLFWwindow* w) { return static_cast<Window*>(glfwGetWindowUserPointer(w)); }

} // namespace

Window::~Window() { destroy(); }

bool Window::create(const WindowConfig& config) {
    glfwSetErrorCallback(glfwErrorCallback);
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if (config.forceX11 || !wayland || !*wayland) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) {
        log::error("glfwInit failed");
        return false;
    }
    glfwInitialized_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, config.debugContext ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

    window_ = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!window_) {
        log::error("Could not create a {}x{} window with an OpenGL 4.5 core context", config.width,
                   config.height);
        return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwMakeContextCurrent(window_);

    if (!gl::load(reinterpret_cast<gl::LoaderFunc>(glfwGetProcAddress))) return false;
    if (config.debugContext) gl::enableDebugOutput();

    log::info("OpenGL {} on {} ({}), GLFW platform {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
              glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? "wayland" : "x11");

    glfwSetFramebufferSizeCallback(window_, onFramebufferSize);
    glfwSetWindowSizeCallback(window_, onWindowSize);
    glfwSetKeyCallback(window_, onKey);
    glfwSetCharCallback(window_, onChar);
    glfwSetMouseButtonCallback(window_, onMouseButton);
    glfwSetCursorPosCallback(window_, onCursor);
    glfwSetScrollCallback(window_, onScroll);
    glfwSetWindowFocusCallback(window_, onFocus);

    glfwGetFramebufferSize(window_, &framebufferSize_.x, &framebufferSize_.y);
    glfwGetWindowSize(window_, &windowSize_.x, &windowSize_.y);
    setVsync(config.vsync);
    return true;
}

void Window::destroy() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfwInitialized_) {
        glfwTerminate();
        glfwInitialized_ = false;
    }
}

bool Window::shouldClose() const { return window_ && glfwWindowShouldClose(window_); }
void Window::setShouldClose(bool close) { glfwSetWindowShouldClose(window_, close ? GLFW_TRUE : GLFW_FALSE); }

void Window::pollEvents() {
    resized_ = false;
    input_.beginFrame();
    glfwPollEvents();
}

void Window::swapBuffers() { glfwSwapBuffers(window_); }

void Window::setVsync(bool on) { glfwSwapInterval(on ? 1 : 0); }

void Window::toggleFullscreen() {
    if (!fullscreen_) {
        glfwGetWindowPos(window_, &windowedPos_.x, &windowedPos_.y);
        glfwGetWindowSize(window_, &windowedSize_.x, &windowedSize_.y);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) return;
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        fullscreen_ = true;
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowedPos_.x, windowedPos_.y, windowedSize_.x, windowedSize_.y,
                             GLFW_DONT_CARE);
        fullscreen_ = false;
    }
}

void Window::setCursorCaptured(bool captured) {
    if (captured == cursorCaptured_) return;
    cursorCaptured_ = captured;
    glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    input_.resetMouseTracking();
}

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(window_, title.c_str()); }

double Window::time() { return glfwGetTime(); }

void Window::onFramebufferSize(GLFWwindow* w, int width, int height) {
    Window* win = self(w);
    win->framebufferSize_ = {width, height};
    win->resized_ = true;
}

void Window::onWindowSize(GLFWwindow* w, int width, int height) { self(w)->windowSize_ = {width, height}; }

void Window::onKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
    self(w)->input_.onKey(key, scancode, action, mods);
}

void Window::onChar(GLFWwindow* w, unsigned codepoint) { self(w)->input_.onChar(codepoint); }

void Window::onMouseButton(GLFWwindow* w, int button, int action, int mods) {
    self(w)->input_.onMouseButton(button, action, mods);
}

void Window::onCursor(GLFWwindow* w, double x, double y) { self(w)->input_.onCursor(x, y); }

void Window::onScroll(GLFWwindow* w, double dx, double dy) { self(w)->input_.onScroll(dx, dy); }

void Window::onFocus(GLFWwindow* w, int focused) {
    Window* win = self(w);
    win->focused_ = focused != 0;
    if (!focused) win->input_.onFocusLost();
}

} // namespace mc
