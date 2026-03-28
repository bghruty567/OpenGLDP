#include <vtkDataSetReader.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkGradientFilter.h>
#include <vtkRenderWindow.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkStaticCellLinks.h>
#include <vtkIdList.h>
#include <glad/glad.h>
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include "GLGradientEngine.h"

static bool nearlyEqual(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps * (1.0f + std::max(std::fabs(a), std::fabs(b))); }

// 加载 uGrid，返回独立持有的对象
static vtkSmartPointer<vtkUnstructuredGrid> loadUG(const std::string& path) {
    vtkNew<vtkDataSetReader> r;
    r->SetFileName(path.c_str());
    r->Update();
    vtkUnstructuredGrid* out = vtkUnstructuredGrid::SafeDownCast(r->GetOutput());
    if (!out) {
        std::cerr << "file is not an UnstructuredGrid\n";
        return nullptr;
    }
    vtkSmartPointer<vtkUnstructuredGrid> ug = vtkSmartPointer<vtkUnstructuredGrid>::New();
    ug->ShallowCopy(out);
    return ug;
}

// 选择用于梯度的点标量名（更稳健）
static std::string pickScalarName(vtkUnstructuredGrid* ug) {
    if (!ug) { std::cerr << "UG null\n"; return ""; }
    auto* pd = ug->GetPointData();
    if (!pd) { std::cerr << "No PointData\n"; return ""; }

    if (auto* s = pd->GetScalars()) {
        if (auto* nm = s->GetName()) return nm;
    }
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        auto* a = pd->GetArray(i);
        if (!a) continue;
        if (a->GetNumberOfComponents() == 1) {
            if (auto* nm = a->GetName()) return nm;
        }
    }

    std::cerr << "No 1-component point array found. Available arrays:\n";
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        auto* a = pd->GetArray(i);
        if (!a) continue;
        std::cerr << "  [" << i << "] name=" << (a->GetName() ? a->GetName() : "<null>")
            << " comps=" << a->GetNumberOfComponents() << "\n";
    }
    return "";
}

static void copyScalar(vtkUnstructuredGrid* ug, const std::string& name, std::vector<float>& out) {
    auto* a = ug->GetPointData()->GetArray(name.c_str());
    vtkIdType n = a->GetNumberOfTuples();
    out.resize(static_cast<size_t>(n));
    for (vtkIdType i = 0; i < n; ++i) { out[i] = static_cast<float>(a->GetComponent(i, 0)); }
}

static void copyPoints(vtkUnstructuredGrid* ug, std::vector<float>& pos) {
    auto* pts = ug->GetPoints();
    vtkIdType n = pts->GetNumberOfPoints();
    pos.resize(static_cast<size_t>(n) * 3);
    for (vtkIdType i = 0; i < n; ++i) { double p[3]; pts->GetPoint(i, p); pos[i * 3 + 0] = static_cast<float>(p[0]); pos[i * 3 + 1] = static_cast<float>(p[1]); pos[i * 3 + 2] = static_cast<float>(p[2]); }
}

static void buildTopoPointNeighbors(vtkUnstructuredGrid* ug, std::vector<int>& nbr, std::vector<int>& off) {
    vtkIdType np = ug->GetNumberOfPoints();
    off.clear(); off.reserve(static_cast<size_t>(np) + 1); off.push_back(0);
    vtkNew<vtkIdList> pc;
    for (vtkIdType p = 0; p < np; ++p) {
        std::vector<vtkIdType> acc;
        ug->GetPointCells(p, pc);
        for (vtkIdType i = 0; i < pc->GetNumberOfIds(); ++i) {
            auto* cell = ug->GetCell(pc->GetId(i));
            auto* ids = cell->GetPointIds();
            for (vtkIdType k = 0; k < ids->GetNumberOfIds(); ++k) {
                vtkIdType q = ids->GetId(k);
                if (q != p) acc.push_back(q);
            }
        }
        std::sort(acc.begin(), acc.end());
        acc.erase(std::unique(acc.begin(), acc.end()), acc.end());
        for (auto q : acc) nbr.push_back(static_cast<int>(q));
        off.push_back(static_cast<int>(nbr.size()));
    }
}

static void vtkGradient(vtkUnstructuredGrid* ug, const std::string& scalarName, std::vector<float>& grad) {
    vtkNew<vtkGradientFilter> gf;
    gf->SetResultArrayName("grad_vtk");
    gf->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, scalarName.c_str());
    gf->SetInputData(ug);
    gf->Update();
    auto* out = vtkUnstructuredGrid::SafeDownCast(gf->GetOutput());
    auto* ga = out->GetPointData()->GetArray("grad_vtk");
    vtkIdType n = out->GetNumberOfPoints();
    grad.resize(static_cast<size_t>(n) * 3);
    for (vtkIdType i = 0; i < n; ++i) {
        grad[i * 3 + 0] = static_cast<float>(ga->GetComponent(i, 0));
        grad[i * 3 + 1] = static_cast<float>(ga->GetComponent(i, 1));
        grad[i * 3 + 2] = static_cast<float>(ga->GetComponent(i, 2));
    }
}

static bool makeGLContext() {
    vtkNew<vtkRenderer> ren;
    vtkNew<vtkRenderWindow> win;

    win->SetOffScreenRendering(1);
    win->AddRenderer(ren);
    win->Render();

    auto* glw = vtkOpenGLRenderWindow::SafeDownCast(win.GetPointer());
    if (!glw) {
        std::cerr << "[GL] vtkOpenGLRenderWindow cast failed (check VTK OpenGL2 autoinit/linking)" << std::endl;
        // 回退到隐藏窗口创建
        win->SetOffScreenRendering(0);
        win->SetSize(1, 1);
        win->Render();
        glw = vtkOpenGLRenderWindow::SafeDownCast(win.GetPointer());
        if (!glw) {
            std::cerr << "[GL] Fallback onscreen (hidden) also failed" << std::endl;
            return false;
        }
    }

    glw->MakeCurrent();
    if (!gladLoadGL()) {
        std::cerr << "[GL] gladLoadGL failed (no current context or loader not linked)" << std::endl;
        return false;
    }
    const GLubyte* ver = glGetString(GL_VERSION);
    std::cout << "GL_VERSION: " << (ver ? reinterpret_cast<const char*>(ver) : "unknown") << std::endl;

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        std::cerr << "[GL] OpenGL version < 4.3 (" << major << "." << minor << "), compute shader not supported" << std::endl;
        return false;
    }
    return true;
}
#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkRenderingFreeType);
#include "gl_context_utils.h"

int main(int argc, char** argv) {
    std::string path = "uGridEx.vtk";
    if (argc >= 2) path = argv[1];
    auto ug = loadUG(path);               // vtkSmartPointer
    if (!ug) { return 1; }
    auto scalarName = pickScalarName(ug); // 传 raw 指针或 ug.GetPointer()
    if (scalarName.empty()) { return 2; }

    std::vector<float> pos, phi;
    copyPoints(ug, pos);
    copyScalar(ug, scalarName, phi);
    std::vector<int> off, nbr;
    buildTopoPointNeighbors(ug, nbr, off);

    std::vector<float> grad_vtk;
    vtkGradient(ug, scalarName, grad_vtk);

    if (!makeGLContext()) { std::cerr << "gl init failed" << std::endl; return 3; }

    auto glw = createPersistentGLContext(/*offscreen=*/false);
    if(!glw){ std::cerr << "gl init failed\n"; return 3; }

    // 编译前再次确保当前上下文
    glw->MakeCurrent();

    GLGradientEngine eng;
    eng.setShaderDir("C:\\Users\\lenovo\\Desktop\\bishe\\myProj\\OpenGLDP");
    if (!eng.init()) { std::cerr << "shader init failed" << std::endl; return 4; }
    GLGradientEngine::WLSParams wp; wp.wExponent = 2.0f; wp.lambda = 1e-6f;
    std::vector<float> grad_gl;
    if (!eng.computeUnstructuredWLS(pos, off, nbr, phi, wp, grad_gl)) { std::cerr << "compute failed" << std::endl; return 5; }

    size_t n = grad_gl.size() / 3;
    if (grad_vtk.size() != grad_gl.size()) { std::cerr << "size mismatch" << std::endl; return 6; }
    double mae = 0.0, rmse = 0.0, maxe = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = grad_gl[i * 3 + 0] - grad_vtk[i * 3 + 0];
        double dy = grad_gl[i * 3 + 1] - grad_vtk[i * 3 + 1];
        double dz = grad_gl[i * 3 + 2] - grad_vtk[i * 3 + 2];
        double e = std::sqrt(dx * dx + dy * dy + dz * dz);
        mae += e;
        rmse += e * e;
        if (e > maxe) maxe = e;
    }
    mae /= double(n);
    rmse = std::sqrt(rmse / double(n));

    std::cout << "N=" << n << std::endl;
    std::cout << "MAE=" << mae << " RMSE=" << rmse << " MAXE=" << maxe << std::endl;

    size_t show = std::min<size_t>(n, 5);
    for (size_t i = 0; i < show; ++i) {
        std::cout << "i=" << i << " GL=[" << grad_gl[i * 3 + 0] << "," << grad_gl[i * 3 + 1] << "," << grad_gl[i * 3 + 2] << "] VTK=[" << grad_vtk[i * 3 + 0] << "," << grad_vtk[i * 3 + 1] << "," << grad_vtk[i * 3 + 2] << "]" << std::endl;
    }
    std::cout << (rmse < 1e-3 ? "OK" : "CHECK") << std::endl;
    return 0;
}