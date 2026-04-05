#pragma once
#include <string>
#include <vtkSmartPointer.h>
#include <vtkOpenGLRenderWindow.h>
#include "gl_context_utils.h"


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
    ~OpenGLManager() = default;

    /*
	* @brief 初始化OpenGL上下文，获取运行时信息
    * @param offscreen 是否使用离屏渲染
     * @return 初始化是否成功
    */
    bool initialize(bool offscreen = false)
    {   
		//创建持久化的OpenGL上下文，offscreen参数决定是否使用离屏渲染
        m_window = createPersistentGLContext(offscreen);
        if (!m_window) {
            m_ready = false;
            return false;
        }
		//确保当前线程有一个有效的OpenGL上下文，以便后续的OpenGL调用能够正确执行
        m_window->MakeCurrent();
        
		const char* ven = reinterpret_cast<const char*>(glGetString(GL_VENDOR));//获取GPU供应商信息
		const char* ren = reinterpret_cast<const char*>(glGetString(GL_RENDERER));//获取GPU渲染器信息
		const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));//获取OpenGL版本信息
		const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));//获取GLSL版本信息

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