// c:\Users\lenovo\Desktop\bishe\tests\gl_context_utils.h
#pragma once
#include <vtkRenderWindow.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <glad/glad.h>
#include <iostream>

inline vtkSmartPointer<vtkOpenGLRenderWindow> createPersistentGLContext(bool offscreen = true) {
    vtkNew<vtkRenderer> ren;
    vtkNew<vtkRenderWindow> win;
    win->SetOffScreenRendering(offscreen ? 1 : 0);
    win->AddRenderer(ren);
    win->SetSize(1, 1);
    win->Render();

    auto glw = vtkOpenGLRenderWindow::SafeDownCast(win.GetPointer());
    if (!glw) {
        std::cerr << "[GL] vtkOpenGLRenderWindow cast failed, retry onscreen\n";
        win->SetOffScreenRendering(0);
        win->Render();
        glw = vtkOpenGLRenderWindow::SafeDownCast(win.GetPointer());
        if (!glw) {
            std::cerr << "[GL] onscreen fallback failed\n";
            return nullptr;
        }
    }
    glw->MakeCurrent();
    if (!gladLoadGL()) {
        std::cerr << "[GL] gladLoadGL failed (no current context or loader missing)\n";
        return nullptr;
    }
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* ven = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renStr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::cout << "GL_VENDOR=" << (ven ? ven : "?") << "\nGL_RENDERER=" << (renStr ? renStr : "?")
        << "\nGL_VERSION=" << (ver ? ver : "?") << "  GLSL=" << (glsl ? glsl : "?")
        << "\nGL " << major << "." << minor << "\n";

    if (major < 4 || (major == 4 && minor < 3)) {
        std::cerr << "[GL] Compute Shader requires >=4.3. Current " << major << "." << minor << "\n";
        return nullptr;
    }
    return glw;
}
