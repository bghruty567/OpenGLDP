#include <vtkDataSetReader.h>
#include <vtkDataSet.h>
#include <vtkStructuredGrid.h>
#include <vtkRectilinearGrid.h>
#include <vtkImageData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkCellData.h>
#include <vtkGradientFilter.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>

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
#include <limits>

#include "CAEProcessingFacade.h"

int main(int argc, char** argv)
{
    std::string path = "Data\\uGridEx.vtk";
    if (argc >= 2) path = argv[1];

    {
        std::string assocArg = "point";
        std::string arrayName;
        int reps = 5;
        if (argc >= 3) assocArg = argv[2];
        if (argc >= 4) arrayName = argv[3];
        if (argc >= 5) reps = std::max(1, atoi(argv[4]));

        CAEFieldAssociation assoc = (assocArg == "cell" || assocArg == "CELL")
            ? CAEFieldAssociation::Cell
            : CAEFieldAssociation::Point;

        CAEProcessingFacade facade;
        if (!facade.initialize("Shaders")) {
            std::cerr << "facade init failed\n";
            return 1;
        }

        std::string dsId = facade.loadDatasetFromVTKFile(path);
        if (dsId.empty()) {
            std::cerr << "load dataset failed\n";
            return 2;
        }

        CAEDatasetSummary s;
        if (!facade.getDatasetSummary(dsId, s)) {
            std::cerr << "no dataset summary\n";
            return 3;
        }
        std::cout << "Dataset=" << s.displayName << " points=" << s.pointCount << " cells=" << s.cellCount << std::endl;

        std::vector<CAEFieldInfo> fields;
        if (!facade.listFields(dsId, assoc, fields) || fields.empty()) {
            std::cerr << "no arrays for association\n";
            return 4;
        }

        std::cout << "Available arrays (" << (assoc == CAEFieldAssociation::Point ? "POINT" : "CELL") << "):\n";
        for (const auto& f : fields) {
            std::cout << "  - " << f.name << " comps=" << f.numComponents << " tuples=" << f.tupleCount << "\n";
        }

        if (arrayName.empty()) {
            arrayName = fields.front().name;
            std::cout << "Use default array: " << arrayName << "\n";
        }

        CAEGradientRequest req;
        req.datasetId = dsId;
        req.inputArrayName = arrayName;
        req.association = assoc;
        req.method = CAEGradientMethod::Auto;

        CAEGradientResultMeta meta;
        double glWallSum = 0.0, glWallMin = std::numeric_limits<double>::max();
        double glGpuSum = 0.0, glGpuMin = std::numeric_limits<double>::max();
        for (int i = 0; i < reps; ++i) {
            if (!facade.computeGradient(req, meta)) {
                std::cerr << "computeGradient failed\n";
                return 5;
            }
            double w = facade.getLastComputeWallMs();
            double g = facade.getLastComputeGpuMs();
            glWallSum += w;
            glGpuSum += g;
            glWallMin = std::min(glWallMin, w);
            glGpuMin = std::min(glGpuMin, g);
        }

        std::vector<float> grad_gl;
        int glComps = 0;
        if (!facade.getArrayData(dsId, meta.resultArrayName, assoc, grad_gl, glComps)) {
            std::cerr << "get OpenGL result failed\n";
            return 6;
        }

        vtkNew<vtkDataSetReader> rr;
        rr->SetFileName(path.c_str());
        rr->Update();
        vtkDataSet* ds = vtkDataSet::SafeDownCast(rr->GetOutput());
        if (!ds) {
            std::cerr << "vtk read failed\n";
            return 7;
        }

        vtkNew<vtkGradientFilter> gf;
        gf->SetResultArrayName("grad_vtk");
        int vtkAssoc = (assoc == CAEFieldAssociation::Point)
            ? vtkDataObject::FIELD_ASSOCIATION_POINTS
            : vtkDataObject::FIELD_ASSOCIATION_CELLS;
        gf->SetInputArrayToProcess(0, 0, 0, vtkAssoc, arrayName.c_str());
        gf->SetInputData(ds);

        double vtkSum = 0.0, vtkMin = std::numeric_limits<double>::max();
        for (int i = 0; i < reps; ++i) {
            gf->Modified();
            auto t0 = std::chrono::high_resolution_clock::now();
            gf->Update();
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            vtkSum += ms;
            vtkMin = std::min(vtkMin, ms);
        }

        vtkDataSet* out = vtkDataSet::SafeDownCast(gf->GetOutput());
        vtkDataArray* ga = (assoc == CAEFieldAssociation::Point)
            ? out->GetPointData()->GetArray("grad_vtk")
            : out->GetCellData()->GetArray("grad_vtk");
        if (!ga) {
            std::cerr << "vtk gradient array missing\n";
            return 8;
        }

        int vtkComps = ga->GetNumberOfComponents();
        size_t vtkTuples = static_cast<size_t>(ga->GetNumberOfTuples());
        std::vector<float> grad_vtk(vtkTuples * static_cast<size_t>(vtkComps));
        for (size_t i = 0; i < vtkTuples; ++i) {
            for (int c = 0; c < vtkComps; ++c) {
                grad_vtk[i * static_cast<size_t>(vtkComps) + c] = static_cast<float>(ga->GetComponent(static_cast<vtkIdType>(i), c));
            }
        }

        size_t glTuples = glComps > 0 ? grad_gl.size() / static_cast<size_t>(glComps) : 0;
        size_t nTuple = std::min(glTuples, vtkTuples);
        int nComp = std::min(glComps, vtkComps);
        double mae = 0.0, rmse = 0.0, maxe = 0.0;
        for (size_t i = 0; i < nTuple; ++i) {
            for (int c = 0; c < nComp; ++c) {
                double d = static_cast<double>(grad_gl[i * static_cast<size_t>(glComps) + c])
                    - static_cast<double>(grad_vtk[i * static_cast<size_t>(vtkComps) + c]);
                double a = std::abs(d);
                mae += a;
                rmse += d * d;
                if (a > maxe) maxe = a;
            }
        }
        double denom = std::max<size_t>(1, nTuple * static_cast<size_t>(nComp));
        mae /= denom;
        rmse = std::sqrt(rmse / denom);

        std::cout << "Array=" << arrayName << " Assoc=" << (assoc == CAEFieldAssociation::Point ? "POINT" : "CELL") << "\n";
        std::cout << "VTK_time_ms_avg=" << (vtkSum / reps) << " VTK_time_ms_min=" << vtkMin << "\n";
        std::cout << "GL_wall_ms_avg=" << (glWallSum / reps) << " GL_wall_ms_min=" << glWallMin << "\n";
        std::cout << "GL_gpu_ms_avg=" << (glGpuSum / reps) << " GL_gpu_ms_min=" << glGpuMin << "\n";
        std::cout << "Compare tuples=" << nTuple << " comps=" << nComp << " MAE=" << mae << " RMSE=" << rmse << " MAXE=" << maxe << "\n";

        size_t show = std::min<size_t>(nTuple, 10);
        for (size_t i = 0; i < show; ++i) {
            std::cout << "i=" << i << " GL=[";
            for (int c = 0; c < nComp; ++c) {
                std::cout << grad_gl[i * static_cast<size_t>(glComps) + c] << (c + 1 < nComp ? "," : "");
            }
            std::cout << "] VTK=[";
            for (int c = 0; c < nComp; ++c) {
                std::cout << grad_vtk[i * static_cast<size_t>(vtkComps) + c] << (c + 1 < nComp ? "," : "");
            }
            std::cout << "]\n";
        }

        vtkSmartPointer<vtkDataSet> showDs;
        if (facade.exportDatasetToVTK(dsId, showDs)) {
            //vtkDataSet* showDs = conv.vtkData;

            vtkNew<vtkDataSetMapper> m1;
            m1->SetInputData(showDs);
            m1->SetScalarModeToUsePointFieldData();
            m1->SelectColorArray("grad_gl_mag");
            m1->ScalarVisibilityOn();

            vtkNew<vtkDataSetMapper> m2;
            m2->SetInputData(showDs);
            m2->SetScalarModeToUsePointFieldData();
            m2->SelectColorArray("grad_vtk_mag");
            m2->ScalarVisibilityOn();

            vtkNew<vtkActor> a1;
            a1->SetMapper(m1);
            vtkNew<vtkActor> a2;
            a2->SetMapper(m2);

            vtkNew<vtkRenderer> r1;
            vtkNew<vtkRenderer> r2;
            r1->SetViewport(0.0, 0.0, 0.5, 1.0);
            r2->SetViewport(0.5, 0.0, 1.0, 1.0);
            r1->AddActor(a1);
            r2->AddActor(a2);
            r1->SetBackground(0.1, 0.1, 0.15);
            r2->SetBackground(0.1, 0.1, 0.15);

            vtkNew<vtkRenderWindow> win;
            win->SetSize(1200, 600);
            win->AddRenderer(r1);
            win->AddRenderer(r2);

            vtkNew<vtkRenderWindowInteractor> iren;
            iren->SetRenderWindow(win);
            win->Render();
            iren->Start();
        }

        return 0;
    }

#if 0
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

        // ???
        for (int w = 0; w < warmup; ++w) {
            ok = eng.computeRegularFD(obj.points, arrPtr->data, p, grad_tmp);
            if (!ok) { std::cerr << "compute failed (warmup)\n"; return 9; }
        }

        // ?????????????? & GPU??
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

        // ???
        for (int w = 0; w < warmup; ++w) {
            ok = eng.computeUnstructuredWLS(obj.points, obj.pointNeighborOffsets, obj.pointNeighbors, arrPtr->data, wp, grad_tmp);
            if (!ok) { std::cerr << "compute failed (warmup)\n"; return 9; }
        }

        // ?????????????? & GPU??
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

    obj.upsertDataArray("grad_gl", grad_gl, 3 * compsIn, POINT_DATA);
    obj.upsertDataArray("grad_vtk_ref", grad_vtk, vtkOutComps, POINT_DATA);

    std::vector<float> mag_gl(N, 0.0f), mag_vtk(N, 0.0f);
    for (size_t i = 0; i < N; ++i) {
        double s1 = 0.0, s2 = 0.0;
        for (int c = 0; c < compsIn; ++c) {
            size_t gi = i * size_t(3 * compsIn) + c * 3;
            size_t vi = i * size_t(vtkOutComps) + c * 3;
            double gx1 = grad_gl[gi + 0], gy1 = grad_gl[gi + 1], gz1 = grad_gl[gi + 2];
            double gx2 = grad_vtk[vi + 0], gy2 = grad_vtk[vi + 1], gz2 = grad_vtk[vi + 2];
            s1 += gx1 * gx1 + gy1 * gy1 + gz1 * gz1;
            s2 += gx2 * gx2 + gy2 * gy2 + gz2 * gz2;
        }
        mag_gl[i] = static_cast<float>(std::sqrt(s1));
        mag_vtk[i] = static_cast<float>(std::sqrt(s2));
    }
    obj.upsertDataArray("grad_gl_mag", mag_gl, 1, POINT_DATA);
    obj.upsertDataArray("grad_vtk_mag", mag_vtk, 1, POINT_DATA);

    if (conv.convertInternalToVTK()) {
        vtkDataSet* showDs = conv.vtkData;

        vtkNew<vtkDataSetMapper> m1;
        m1->SetInputData(showDs);
        m1->SetScalarModeToUsePointFieldData();
        m1->SelectColorArray("grad_gl_mag");
        m1->ScalarVisibilityOn();

        vtkNew<vtkDataSetMapper> m2;
        m2->SetInputData(showDs);
        m2->SetScalarModeToUsePointFieldData();
        m2->SelectColorArray("grad_vtk_mag");
        m2->ScalarVisibilityOn();

        vtkNew<vtkActor> a1;
        a1->SetMapper(m1);
        vtkNew<vtkActor> a2;
        a2->SetMapper(m2);

        vtkNew<vtkRenderer> r1;
        vtkNew<vtkRenderer> r2;
        r1->SetViewport(0.0, 0.0, 0.5, 1.0);
        r2->SetViewport(0.5, 0.0, 1.0, 1.0);
        r1->AddActor(a1);
        r2->AddActor(a2);
        r1->SetBackground(0.1, 0.1, 0.15);
        r2->SetBackground(0.1, 0.1, 0.15);

        vtkNew<vtkRenderWindow> win;
        win->SetSize(1200, 600);
        win->AddRenderer(r1);
        win->AddRenderer(r2);

        vtkNew<vtkRenderWindowInteractor> iren;
        iren->SetRenderWindow(win);
        win->Render();
        iren->Start();
    }

    return 0;
#endif
}