#pragma once
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>


struct OpenGLRuntimeInfo
{
	std::string vendor;// GPU供应商信息
	std::string renderer;// GPU渲染器信息
	std::string version;// OpenGL版本信息
	std::string glsl;// GLSL版本信息
	int major = 0;// OpenGL主版本号
	int minor = 0;// OpenGL次版本号
};

/*
* @brief OpenGLManager类负责管理OpenGL上下文和相关信息
*/
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

    // offscreen 参数先保留，将来你可以根据需要控制窗口是否可见
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