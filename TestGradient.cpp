#include <vtkDataSetReader.h>
#include <vtkDataSet.h>
#include <vtkStructuredGrid.h>
#include <vtkRectilinearGrid.h>
#include <vtkImageData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkGradientFilter.h>

#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkRenderingFreeType);

#include "gl_context_utils.h"
#include <vtkOpenGLRenderWindow.h>

#include <glad/glad.h>
#include <windows.h>

#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <chrono>

#include "GLGradientEngine.h"
#include "VTKDataConverter.h"
#include "DataObject.h"

int main(int argc, char** argv)
{
    std::string path = "Data\\hexa.vtk";
    if (argc >= 2) path = argv[1];

    vtkNew<vtkDataSetReader> r;
    r->SetFileName(path.c_str());
    r->Update();
    vtkDataSet* ds = vtkDataSet::SafeDownCast(r->GetOutput());
    if (!ds) { std::cerr << "read failed\n"; return 1; }

    auto pickName = [&](int& comps) -> std::string {
        auto* pd = ds->GetPointData();
        if (!pd) { comps = 0; return std::string(); }
        if (auto* s = pd->GetScalars()) { if (s->GetName()) { comps = s->GetNumberOfComponents(); return s->GetName(); } }
        for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
            auto* a = pd->GetArray(i);
            if (!a) continue;
            if (a->GetName()) { comps = a->GetNumberOfComponents(); return a->GetName(); }
        }
        comps = 0; return std::string();
        };

    int comps = 0;
    std::string arrName = pickName(comps);
    if (arrName.empty() || comps <= 0) { std::cerr << "no point array\n"; return 2; }

    std::vector<float> grad_vtk;
    int vtkOutComps = 0;
    {
        vtkNew<vtkGradientFilter> gf;
        gf->SetResultArrayName("grad_vtk");
        gf->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS, arrName.c_str());
        gf->SetInputData(ds);

        const int warmup = 1;
        const int reps = 5;

        for (int w = 0; w < warmup; ++w) {
            gf->Modified();
            gf->Update();
        }

        double sum_ms = 0.0;
        double min_ms = std::numeric_limits<double>::max();

        for (int r = 0; r < reps; ++r) {
            gf->Modified();
            auto t0 = std::chrono::high_resolution_clock::now();
            gf->Update();
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            sum_ms += ms;
            if (ms < min_ms) min_ms = ms;
        }

        vtkDataSet* out = vtkDataSet::SafeDownCast(gf->GetOutput());
        auto* ga = out->GetPointData()->GetArray("grad_vtk");
        vtkIdType n = out->GetNumberOfPoints();
        vtkOutComps = ga ? ga->GetNumberOfComponents() : 0;
        grad_vtk.resize(size_t(n) * size_t(vtkOutComps));
        for (vtkIdType i = 0; i < n; ++i) {
            for (int c = 0; c < vtkOutComps; ++c) {
                grad_vtk[size_t(i) * size_t(vtkOutComps) + c] = float(ga->GetComponent(i, c));
            }
        }

        std::cout << "VTK_time_ms_avg=" << (sum_ms / reps)
            << " VTK_time_ms_min=" << min_ms << std::endl;
    }

    auto glw = createPersistentGLContext(false);
    if (!glw) { std::cerr << "gl init failed\n"; return 3; }
    glw->MakeCurrent();

    GLGradientEngine eng;
    eng.setShaderDir("Shaders");
    if (!eng.init()) { std::cerr << "shader init failed\n"; return 4; }

    DataObject obj;
    VTKDataConverter conv;
    conv.bindVTKDataAndInternalData(ds, &obj);
    if (!conv.convertVTKToInternal()) { std::cerr << "convert failed\n"; return 5; }

    const DataArray* arrPtr = nullptr;
    for (const auto& a : obj.dataArrays) { if (a.dataType == POINT_DATA && a.name == arrName) { arrPtr = &a; break; } }
    if (!arrPtr) { std::cerr << "array not found\n"; return 6; }

    size_t N = obj.points.size() / 3;
    if (N == 0) { std::cerr << "no points\n"; return 7; }
    if (arrPtr->data.size() % N != 0) { std::cerr << "array size mismatch\n"; return 8; }
    int compsIn = int(arrPtr->data.size() / N);

    std::vector<float> grad_gl;
    auto t0 = std::chrono::high_resolution_clock::now();
    bool ok = false;

    if (obj.gridType == DATA_OBJECT_TYPE_RegularGrid) {
        GLGradientEngine::RegularParams p;
        p.dims[0] = obj.dimensions[0]; p.dims[1] = obj.dimensions[1]; p.dims[2] = obj.dimensions[2];
        float sp[3] = { 1,1,1 };
        if (vtkImageData::SafeDownCast(ds)) {
            double s[3]; vtkImageData::SafeDownCast(ds)->GetSpacing(s);
            sp[0] = float(s[0]); sp[1] = float(s[1]); sp[2] = float(s[2]);
        }
        else {
            int nx = p.dims[0], ny = p.dims[1], nz = p.dims[2];
            auto len3 = [&](int a, int b) {
                float dx = obj.points[b * 3 + 0] - obj.points[a * 3 + 0];
                float dy = obj.points[b * 3 + 1] - obj.points[a * 3 + 1];
                float dz = obj.points[b * 3 + 2] - obj.points[a * 3 + 2];
                return std::sqrt(dx * dx + dy * dy + dz * dz);
                };
            if (nx > 1) sp[0] = len3(0, 1);
            if (ny > 1) sp[1] = len3(0, nx);
            if (nz > 1) sp[2] = len3(0, nx * ny);
        }
        p.spacing[0] = sp[0]; p.spacing[1] = sp[1]; p.spacing[2] = sp[2];
        p.origin[0] = 0; p.origin[1] = 0; p.origin[2] = 0;
        //ok = eng.computeRegularFD(obj.points, arrPtr->data, p, grad_gl);
        eng.setEnableGpuTiming(true);

        const int warmup = 1;
        const int reps = 5;

        std::vector<float> grad_tmp;
        bool ok = false;

        // 预热
        for (int w = 0; w < warmup; ++w) {
            ok = eng.computeRegularFD(obj.points, arrPtr->data, p, grad_tmp);
            if (!ok) { std::cerr << "compute failed (warmup)\n"; return 9; }
        }

        // 多次测量（端到端 & GPU）
        double wall_sum = 0.0, wall_min = std::numeric_limits<double>::max();
        double gpu_sum = 0.0, gpu_min = std::numeric_limits<double>::max();

        for (int r = 0; r < reps; ++r) {
            auto t0 = std::chrono::high_resolution_clock::now();
            ok = eng.computeRegularFD(obj.points, arrPtr->data, p, grad_tmp);
            auto t1 = std::chrono::high_resolution_clock::now();
            if (!ok) { std::cerr << "compute failed\n"; return 9; }

            double wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            wall_sum += wall_ms; wall_min = std::min(wall_min, wall_ms);

            double gpu_ms = eng.getLastGpuTimeMs();
            gpu_sum += gpu_ms; gpu_min = std::min(gpu_min, gpu_ms);
        }

        grad_gl.swap(grad_tmp);

        std::cout << "GL_wall_ms_avg=" << (wall_sum / reps)
            << " GL_wall_ms_min=" << wall_min << std::endl;
        std::cout << "GL_gpu_ms_avg=" << (gpu_sum / reps)
            << " GL_gpu_ms_min=" << gpu_min << std::endl;
    }
    else {
        GLGradientEngine::WLSParams wp;
        wp.wExponent = 1.0f;
        wp.lambda = 1e-2f;
        //ok = eng.computeUnstructuredWLS(obj.points, obj.pointNeighborOffsets, obj.pointNeighbors, arrPtr->data, wp, grad_gl);
        eng.setEnableGpuTiming(true);

        const int warmup = 1;
        const int reps = 5;

        std::vector<float> grad_tmp;
        bool ok = false;

        // 预热
        for (int w = 0; w < warmup; ++w) {
            ok = eng.computeUnstructuredWLS(obj.points, obj.pointNeighborOffsets, obj.pointNeighbors, arrPtr->data, wp, grad_tmp);
            if (!ok) { std::cerr << "compute failed (warmup)\n"; return 9; }
        }

        // 多次测量（端到端 & GPU）
        double wall_sum = 0.0, wall_min = std::numeric_limits<double>::max();
        double gpu_sum = 0.0, gpu_min = std::numeric_limits<double>::max();

        for (int r = 0; r < reps; ++r) {
            auto t0 = std::chrono::high_resolution_clock::now();
            ok = eng.computeUnstructuredWLS(obj.points, obj.pointNeighborOffsets, obj.pointNeighbors, arrPtr->data, wp, grad_tmp);
            auto t1 = std::chrono::high_resolution_clock::now();
            if (!ok) { std::cerr << "compute failed\n"; return 9; }

            double wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            wall_sum += wall_ms; wall_min = std::min(wall_min, wall_ms);

            double gpu_ms = eng.getLastGpuTimeMs();
            gpu_sum += gpu_ms; gpu_min = std::min(gpu_min, gpu_ms);
        }

        grad_gl.swap(grad_tmp);

        std::cout << "GL_wall_ms_avg=" << (wall_sum / reps)
            << " GL_wall_ms_min=" << wall_min << std::endl;
        std::cout << "GL_gpu_ms_avg=" << (gpu_sum / reps)
            << " GL_gpu_ms_min=" << gpu_min << std::endl;
    }

   // auto t1 = std::chrono::high_resolution_clock::now();
    //if (!ok) { std::cerr << "compute failed\n"; return 9; }
    //auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    //std::cout << "GL_time_ms=" << ms << std::endl;

    if (grad_gl.size() != N * size_t(3 * compsIn)) { std::cerr << "gl size mismatch\n"; return 10; }
    if (vtkOutComps != 3 * compsIn) { std::cerr << "vtk comps mismatch\n"; return 11; }

    double mae = 0.0, rmse = 0.0, maxe = 0.0;
    for (size_t i = 0; i < N; ++i) {
        for (int c = 0; c < compsIn; ++c) {
            size_t gi = i * size_t(3 * compsIn) + c * 3;
            size_t vi = i * size_t(vtkOutComps) + c * 3;
            double dx = grad_gl[gi + 0] - grad_vtk[vi + 0];
            double dy = grad_gl[gi + 1] - grad_vtk[vi + 1];
            double dz = grad_gl[gi + 2] - grad_vtk[vi + 2];
            double e = std::sqrt(dx * dx + dy * dy + dz * dz);
            mae += e; rmse += e * e; if (e > maxe) maxe = e;
        }
    }
    double denom = double(N) * double(compsIn);
    mae /= denom; rmse = std::sqrt(rmse / denom);

    std::cout << "N=" << N << " comps=" << compsIn << std::endl;
    std::cout << "MAE=" << mae << " RMSE=" << rmse << " MAXE=" << maxe << std::endl;

    size_t show = std::min<size_t>(N, 100);
    for (size_t i = 0; i < show; ++i) {
        for (int c = 0; c < compsIn; ++c) {
            size_t gi = i * size_t(3 * compsIn) + c * 3;
            size_t vi = i * size_t(vtkOutComps) + c * 3;
            std::cout << "i=" << i << " c=" << c
                << " GL=[" << grad_gl[gi + 0] << "," << grad_gl[gi + 1] << "," << grad_gl[gi + 2] << "] "
                << "VTK=[" << grad_vtk[vi + 0] << "," << grad_vtk[vi + 1] << "," << grad_vtk[vi + 2] << "]\n";
        }
    }
    std::cout << (rmse < 1e-3 ? "OK" : "CHECK") << std::endl;
    return 0;
}