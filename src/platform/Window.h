// Window.h -- GLFW window with an OpenGL 4.5 core context.
#pragma once

#include <string>

#include <glm/glm.hpp>

#include "platform/Input.h"

struct GLFWwindow;

namespace mc {

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Minecraft";
    bool vsync = true;
    bool debugContext = false;
    // Force the X11 backend even when a Wayland compositor is present (needed under xvfb-run,
    // which inherits WAYLAND_DISPLAY from the desktop session).
    bool forceX11 = false;
};

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the window, makes its context current and loads the GL entry points.
    bool create(const WindowConfig& config);
    void destroy();

    bool shouldClose() const;
    void setShouldClose(bool close);
    void pollEvents();
    void swapBuffers();

    glm::ivec2 framebufferSize() const { return framebufferSize_; }
    glm::ivec2 windowSize() const { return windowSize_; }
    bool focused() const { return focused_; }
    bool resizedThisFrame() const { return resized_; }

    void setVsync(bool on);
    void toggleFullscreen();
    bool fullscreen() const { return fullscreen_; }
    void setCursorCaptured(bool captured);
    bool cursorCaptured() const { return cursorCaptured_; }
    void setTitle(const std::string& title);

    Input& input() { return input_; }
    const Input& input() const { return input_; }
    GLFWwindow* handle() const { return window_; }
    static double time();

private:
    static void onFramebufferSize(GLFWwindow* w, int width, int height);
    static void onWindowSize(GLFWwindow* w, int width, int height);
    static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void onChar(GLFWwindow* w, unsigned codepoint);
    static void onMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void onCursor(GLFWwindow* w, double x, double y);
    static void onScroll(GLFWwindow* w, double dx, double dy);
    static void onFocus(GLFWwindow* w, int focused);

    GLFWwindow* window_ = nullptr;
    Input input_;
    glm::ivec2 framebufferSize_{0};
    glm::ivec2 windowSize_{0};
    glm::ivec2 windowedPos_{0};
    glm::ivec2 windowedSize_{0};
    bool fullscreen_ = false;
    bool focused_ = true;
    bool resized_ = false;
    bool cursorCaptured_ = false;
    bool glfwInitialized_ = false;
};

} // namespace mc
