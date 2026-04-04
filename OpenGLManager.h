#pragma once
#include <string>
#include <vtkSmartPointer.h>
#include <vtkOpenGLRenderWindow.h>
#include "gl_context_utils.h"

struct OpenGLRuntimeInfo
{
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string glsl;
    int major = 0;
    int minor = 0;
};

class OpenGLManager
{
public:
    OpenGLManager() = default;
    ~OpenGLManager() = default;

    bool initialize(bool offscreen = false)
    {
        m_window = createPersistentGLContext(offscreen);
        if (!m_window) {
            m_ready = false;
            return false;
        }

        m_window->MakeCurrent();

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
    }

    bool isReady() const
    {
        return m_ready && m_window != nullptr;
    }

    void makeCurrent()
    {
        if (m_window) m_window->MakeCurrent();
    }

    const OpenGLRuntimeInfo& info() const
    {
        return m_info;
    }

    vtkSmartPointer<vtkOpenGLRenderWindow> window() const
    {
        return m_window;
    }

private:
    vtkSmartPointer<vtkOpenGLRenderWindow> m_window;
    OpenGLRuntimeInfo m_info;
    bool m_ready = false;
};