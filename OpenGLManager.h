#pragma once
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>


struct OpenGLRuntimeInfo
{
	std::string vendor;   // GPU 供应商信息
	std::string renderer; // 驱动报告的具体渲染器名称
	std::string version;  // OpenGL 版本字符串
	std::string glsl;     // GLSL 版本字符串
	int major = 0;        // OpenGL 主版本号
	int minor = 0;        // OpenGL 次版本号
};

/// 轻量级 OpenGL 运行时管理器。
///
/// 本项目需要一套“独立于 Qt/VTK 显示窗口”的 OpenGL 上下文，
/// 用来安全地执行 compute shader、SSBO 上传和 GPU 计时。
/// `OpenGLManager` 只负责这件事，不负责具体算法。
class OpenGLManager
{
public:
    OpenGLManager() = default;

    ~OpenGLManager()
    {
#ifdef _WIN32
        destroyContext();
#endif
    }

    /// 创建并初始化 OpenGL 上下文。
    ///
    /// `offscreen=true` 时会优先创建一个不可见的 dummy window，
    /// 让 compute shader 拥有稳定的 WGL 上下文。
    bool initialize(bool offscreen = false)
    {
#ifdef _WIN32
        if (m_ready) return true;
        if (!createContext(offscreen)) {
            m_ready = false;
            return false;
        }

        const char* ven = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* ren = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

        glGetIntegerv(GL_MAJOR_VERSION, &m_info.major);
        glGetIntegerv(GL_MINOR_VERSION, &m_info.minor);

        m_info.vendor = ven ? ven : "";
        m_info.renderer = ren ? ren : "";
        m_info.version = ver ? ver : "";
        m_info.glsl = glsl ? glsl : "";

        m_ready = true;
        return true;
#else
        m_ready = false;
        return false;
#endif
    }

    bool isReady() const
    {
#ifdef _WIN32
        return m_ready && m_hglrc != nullptr;
#else
        return false;
#endif
    }

    void makeCurrent()
    {
#ifdef _WIN32
        // 每次真正发起 GPU 计算前，门面层都会切回这套上下文。
        if (m_hdc && m_hglrc) {
            wglMakeCurrent(m_hdc, m_hglrc);
        }
#endif
    }

    const OpenGLRuntimeInfo& info() const
    {
        return m_info;
    }

private:
#ifdef _WIN32
    static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    /// 在 Windows 下创建最小可用的 OpenGL 上下文。
    ///
    /// 实现顺序是：
    /// 1. 注册一个 dummy window 类；
    /// 2. 创建 1x1 小窗口；
    /// 3. 选择像素格式并建立 WGL 上下文；
    /// 4. 调用 glad 装载 OpenGL 函数指针。
    bool createContext(bool offscreen)
    {
        HINSTANCE hInst = GetModuleHandleA(nullptr);

        WNDCLASSA wc{};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = "OpenGLDP_DummyWindow";

        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        DWORD style = offscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;

        m_hwnd = CreateWindowExA(
            0,
            wc.lpszClassName,
            wc.lpszClassName,
            style,
            0, 0,
            1, 1,
            nullptr,
            nullptr,
            hInst,
            nullptr);
        if (!m_hwnd) {
            return false;
        }

        m_hdc = GetDC(m_hwnd);
        if (!m_hdc) {
            destroyContext();
            return false;
        }

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;

        int pf = ChoosePixelFormat(m_hdc, &pfd);
        if (pf == 0) {
            destroyContext();
            return false;
        }
        if (!SetPixelFormat(m_hdc, pf, &pfd)) {
            destroyContext();
            return false;
        }

        m_hglrc = wglCreateContext(m_hdc);
        if (!m_hglrc) {
            destroyContext();
            return false;
        }
        if (!wglMakeCurrent(m_hdc, m_hglrc)) {
            destroyContext();
            return false;
        }

        if (!gladLoadGL()) {
            destroyContext();
            return false;
        }

        return true;
    }

    /// 释放 OpenGL 上下文及其依附的窗口/设备资源。
    void destroyContext()
    {
        if (m_hglrc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_hglrc);
            m_hglrc = nullptr;
        }
        if (m_hwnd && m_hdc) {
            ReleaseDC(m_hwnd, m_hdc);
        }
        m_hdc = nullptr;
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    HWND  m_hwnd = nullptr;
    HDC   m_hdc = nullptr;
    HGLRC m_hglrc = nullptr;
#endif

    OpenGLRuntimeInfo m_info;
    bool m_ready = false;
};
