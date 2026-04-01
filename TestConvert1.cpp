//#include <vtkDataSetReader.h>
//#include <vtkStructuredGrid.h>
//#include <vtkUnstructuredGrid.h>
//#include <vtkStaticCellLinks.h>
//#include <vtkPoints.h>
//#include <vtkPointData.h>
//#include <vtkCellData.h>
//#include <vtkCell.h>
//#include <vtkIdList.h>
//#include <iostream>
//#include <vector>
//#include <string>
//#include <algorithm>
//#include <cmath>
//#include "VTKDataConverter.h"
//#include "DataObject.h"
//
//static vtkSmartPointer<vtkDataSet> readLegacy(const std::string& path) {
//    vtkNew<vtkDataSetReader> r; r->SetFileName(path.c_str()); r->Update();
//    return vtkDataSet::SafeDownCast(r->GetOutput());
//}
//static bool nearlyEqual(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps * (1.0f + std::max(std::fabs(a), std::fabs(b))); }
//static DataArray* findArray(DataObject& obj, const std::string& name, DataArrayType t) { for (auto& a : obj.dataArrays) { if (a.name == name && a.dataType == t) return &a; } return nullptr; }
//
//static void printHeader(const std::string& t) { std::cout << "==== " << t << " ====" << std::endl; }
//static void printCSR(const std::vector<int>& off, const std::vector<int>& idx, const std::string& name) {
//    printHeader(name);
//    int rows = off.empty() ? 0 : static_cast<int>(off.size() - 1);
//    std::cout << "rows=" << rows << ", nnz=" << idx.size() << std::endl;
//    for (int r = 0; r < rows; ++r) {
//        int b = off[r], e = off[r + 1];
//        std::cout << r << ": ";
//        for (int k = b; k < e; ++k) {
//            std::cout << idx[k] << (k + 1 < e ? " " : "");
//        }
//        std::cout << std::endl;
//    }
//}
//static void printPoints(const DataObject& obj) {
//    printHeader("Points");
//    size_t n = obj.points.size() / 3;
//    std::cout << "count=" << n << std::endl;
//    for (size_t i = 0; i < n; ++i) {
//        std::cout << i << ": " << obj.points[i * 3 + 0] << "," << obj.points[i * 3 + 1] << "," << obj.points[i * 3 + 2] << std::endl;
//    }
//}
//static void printDimensions(const DataObject& obj) {
//    printHeader("Dimensions");
//    std::cout << obj.dimensions[0] << "," << obj.dimensions[1] << "," << obj.dimensions[2] << std::endl;
//}
//static void printArrays(const DataObject& obj) {
//    printHeader("DataArrays");
//    for (const auto& a : obj.dataArrays) {
//        size_t tuples = a.numComponents > 0 ? a.data.size() / static_cast<size_t>(a.numComponents) : 0;
//        std::cout << (a.dataType == POINT_DATA ? "POINT" : "CELL") << " name=" << a.name << " comps=" << a.numComponents << " tuples=" << tuples << std::endl;
//        for (size_t t = 0; t < tuples; ++t) {
//            std::cout << "  " << t << ": ";
//            for (int c = 0; c < a.numComponents; ++c) {
//                std::cout << a.data[t * a.numComponents + c] << (c + 1 < a.numComponents ? " " : "");
//            }
//            std::cout << std::endl;
//        }
//    }
//}
//static void printCells(const DataObject& obj) {
//    printHeader("Cells");
//    std::cout << "numCells=" << (obj.cellOffsets.empty() ? 0 : static_cast<int>(obj.cellOffsets.size() - 1)) << std::endl;
//    std::cout << "Types: ";
//    for (size_t i = 0; i < obj.cellTypes.size(); ++i) { std::cout << obj.cellTypes[i] << (i + 1 < obj.cellTypes.size() ? " " : ""); }
//    std::cout << std::endl;
//    for (size_t c = 0; c + 1 < obj.cellOffsets.size(); ++c) {
//        int b = obj.cellOffsets[c], e = obj.cellOffsets[c + 1];
//        std::cout << c << " [" << b << "," << e << ") : ";
//        for (int k = b; k < e; ++k) { std::cout << obj.cells[k] << (k + 1 < e ? " " : ""); }
//        std::cout << std::endl;
//    }
//}
//static void printCellCenters(const DataObject& obj) {
//    printHeader("CellCenters");
//    size_t n = obj.cellCenters.size() / 3;
//    std::cout << "count=" << n << std::endl;
//    for (size_t i = 0; i < n; ++i) {
//        std::cout << i << ": " << obj.cellCenters[i * 3 + 0] << "," << obj.cellCenters[i * 3 + 1] << "," << obj.cellCenters[i * 3 + 2] << std::endl;
//    }
//}
//
//static bool comparePoints(vtkDataSet* ds, DataObject& obj) {
//    auto pts = ds->GetPoints(); if (!pts) { return obj.points.empty(); }
//    vtkIdType n = pts->GetNumberOfPoints(); if (static_cast<size_t>(n * 3) != obj.points.size()) return false;
//    for (vtkIdType i = 0; i < n; ++i) {
//        double p[3]; pts->GetPoint(i, p);
//        if (!nearlyEqual(static_cast<float>(p[0]), obj.points[i * 3 + 0])) return false;
//        if (!nearlyEqual(static_cast<float>(p[1]), obj.points[i * 3 + 1])) return false;
//        if (!nearlyEqual(static_cast<float>(p[2]), obj.points[i * 3 + 2])) return false;
//    }
//    return true;
//}
//static bool compareDimensions(vtkStructuredGrid* sg, DataObject& obj) {
//    int d[3]; sg->GetDimensions(d);
//    return obj.dimensions[0] == d[0] && obj.dimensions[1] == d[1] && obj.dimensions[2] == d[2];
//}
//static bool comparePointArrays(vtkDataSet* ds, DataObject& obj) {
//    auto pd = ds->GetPointData(); if (!pd) return true;
//    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
//        auto* da = pd->GetArray(i); if (!da) continue;
//        auto* arr = findArray(obj, da->GetName() ? da->GetName() : "", POINT_DATA); if (!arr) return false;
//        vtkIdType tuples = da->GetNumberOfTuples(); int comps = da->GetNumberOfComponents();
//        if (arr->numComponents != comps) return false;
//        if (arr->data.size() != static_cast<size_t>(tuples * comps)) return false;
//        for (vtkIdType t = 0; t < tuples; ++t) {
//            for (int c = 0; c < comps; ++c) {
//                double v = da->GetComponent(t, c);
//                if (!nearlyEqual(static_cast<float>(v), arr->data[static_cast<size_t>(t * comps + c)])) return false;
//            }
//        }
//    }
//    return true;
//}
//static bool compareCellArrays(vtkDataSet* ds, DataObject& obj) {
//    auto cd = ds->GetCellData(); if (!cd) return true;
//    for (int i = 0; i < cd->GetNumberOfArrays(); ++i) {
//        auto* da = cd->GetArray(i); if (!da) continue;
//        auto* arr = findArray(obj, da->GetName() ? da->GetName() : "", CELL_DATA); if (!arr) return false;
//        vtkIdType tuples = da->GetNumberOfTuples(); int comps = da->GetNumberOfComponents();
//        if (arr->numComponents != comps) return false;
//        if (arr->data.size() != static_cast<size_t>(tuples * comps)) return false;
//        for (vtkIdType t = 0; t < tuples; ++t) {
//            for (int c = 0; c < comps; ++c) {
//                double v = da->GetComponent(t, c);
//                if (!nearlyEqual(static_cast<float>(v), arr->data[static_cast<size_t>(t * comps + c)])) return false;
//            }
//        }
//    }
//    return true;
//}
//static bool compareCells(vtkDataSet* ds, DataObject& obj) {
//    vtkIdType nc = ds->GetNumberOfCells();
//    std::vector<int> off; off.reserve(static_cast<size_t>(nc) + 1); off.push_back(0);
//    std::vector<int> conn; conn.reserve(obj.cells.size());
//    std::vector<int> types; types.reserve(static_cast<size_t>(nc));
//    for (vtkIdType i = 0; i < nc; ++i) {
//        auto* cell = ds->GetCell(i);
//        auto* ids = cell->GetPointIds();
//        for (vtkIdType k = 0; k < ids->GetNumberOfIds(); ++k) conn.push_back(static_cast<int>(ids->GetId(k)));
//        off.push_back(static_cast<int>(conn.size()));
//        types.push_back(static_cast<int>(cell->GetCellType()));
//    }
//    if (off != obj.cellOffsets) return false;
//    if (conn != obj.cells) return false;
//    if (types != obj.cellTypes) return false;
//    return true;
//}
//static bool compareCellCenters(vtkDataSet* ds, DataObject& obj) {
//    vtkIdType nc = ds->GetNumberOfCells(); if (static_cast<size_t>(nc * 3) != obj.cellCenters.size()) return false;
//    for (vtkIdType i = 0; i < nc; ++i) {
//        auto* cell = ds->GetCell(i);
//        double pc[3]; int subId = cell->GetParametricCenter(pc);
//        std::vector<double> w(cell->GetNumberOfPoints());
//        double xyz[3]; cell->EvaluateLocation(subId, pc, xyz, w.data());
//        if (!nearlyEqual(static_cast<float>(xyz[0]), obj.cellCenters[static_cast<size_t>(i * 3 + 0)])) return false;
//        if (!nearlyEqual(static_cast<float>(xyz[1]), obj.cellCenters[static_cast<size_t>(i * 3 + 1)])) return false;
//        if (!nearlyEqual(static_cast<float>(xyz[2]), obj.cellCenters[static_cast<size_t>(i * 3 + 2)])) return false;
//    }
//    return true;
//}
//static void buildTopoPointNeighbors(vtkDataSet* ds, std::vector<int>& nbr, std::vector<int>& off) {
//    vtkIdType np = ds->GetNumberOfPoints();
//    off.clear(); off.reserve(static_cast<size_t>(np) + 1); off.push_back(0);
//    vtkNew<vtkIdList> ids;
//    for (vtkIdType p = 0; p < np; ++p) {
//        std::vector<vtkIdType> acc;
//        ds->GetPointCells(p, ids);
//        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
//            auto* cell = ds->GetCell(ids->GetId(i));
//            auto* cpts = cell->GetPointIds();
//            for (vtkIdType k = 0; k < cpts->GetNumberOfIds(); ++k) {
//                vtkIdType q = cpts->GetId(k);
//                if (q != p) acc.push_back(q);
//            }
//        }
//        std::sort(acc.begin(), acc.end());
//        acc.erase(std::unique(acc.begin(), acc.end()), acc.end());
//        for (auto q : acc) nbr.push_back(static_cast<int>(q));
//        off.push_back(static_cast<int>(nbr.size()));
//    }
//}
//static void buildPointInCellNeighbors(vtkDataSet* ds, std::vector<int>& nbr, std::vector<int>& off) {
//    vtkIdType np = ds->GetNumberOfPoints();
//    off.clear(); off.reserve(static_cast<size_t>(np) + 1); off.push_back(0);
//    vtkNew<vtkStaticCellLinks> links; links->SetDataSet(ds); links->BuildLinks();
//    for (vtkIdType p = 0; p < np; ++p) {
//        vtkIdType n = links->GetNumberOfCells(p);
//        const vtkIdType* c = links->GetCells(p);
//        for (vtkIdType i = 0; i < n; ++i) nbr.push_back(static_cast<int>(c[i]));
//        off.push_back(static_cast<int>(nbr.size()));
//    }
//}
//static void buildTopoCellNeighbors(vtkDataSet* ds, std::vector<int>& nbr, std::vector<int>& off) {
//    vtkIdType nc = ds->GetNumberOfCells();
//    off.clear(); off.reserve(static_cast<size_t>(nc) + 1); off.push_back(0);
//    vtkNew<vtkStaticCellLinks> links; links->SetDataSet(ds); links->BuildLinks();
//    for (vtkIdType cid = 0; cid < nc; ++cid) {
//        auto* cell = ds->GetCell(cid);
//        auto* ids = cell->GetPointIds();
//        std::vector<vtkIdType> acc;
//        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
//            vtkIdType pid = ids->GetId(i);
//            vtkIdType n = links->GetNumberOfCells(pid);
//            const vtkIdType* cs = links->GetCells(pid);
//            for (vtkIdType j = 0; j < n; ++j) { if (cs[j] != cid) acc.push_back(cs[j]); }
//        }
//        std::sort(acc.begin(), acc.end());
//        acc.erase(std::unique(acc.begin(), acc.end()), acc.end());
//        for (auto q : acc) nbr.push_back(static_cast<int>(q));
//        off.push_back(static_cast<int>(nbr.size()));
//    }
//}
//static bool compareCSR(const std::vector<int>& a, const std::vector<int>& ao, const std::vector<int>& b, const std::vector<int>& bo) {
//    if (ao != bo) return false; if (a != b) return false; return true;
//}
//
//static void dumpStructured(const DataObject& obj) {
//    std::cout << "GridType=STRUCTURED" << std::endl;
//    printDimensions(obj);
//    printPoints(obj);
//    printArrays(obj);
//    printCells(obj);
//    printCellCenters(obj);
//}
//static void dumpUnstructured(const DataObject& obj) {
//    std::cout << "GridType=UNSTRUCTURED" << std::endl;
//    printPoints(obj);
//    printArrays(obj);
//    printCells(obj);
//    printCellCenters(obj);
//    if (obj.pointNeighborOffsets.size() > 1) printCSR(obj.pointNeighborOffsets, obj.pointNeighbors, "PointNeighbors CSR");
//    if (obj.pointInCellNeighborOffsets.size() > 1) printCSR(obj.pointInCellNeighborOffsets, obj.pointInCellNeighbors, "PointInCellNeighbors CSR");
//    if (obj.cellNeighborsOffsets.size() > 1) printCSR(obj.cellNeighborsOffsets, obj.cellNeighbors, "CellNeighbors CSR");
//}
//
//static int runStructuredGridTest(const std::string& path) {
//    auto ds = readLegacy(path);
//    if (!ds || !vtkStructuredGrid::SafeDownCast(ds)) return 1;
//    DataObject obj;
//    VTKDataConverter conv;
//    conv.bindVTKDataAndInternalData(ds, &obj);
//    if (!conv.convertType()) return 2;
//    if (!conv.convertRegularGrid()) return 3;
//    dumpStructured(obj);
//    bool ok = true;
//    ok &= comparePoints(ds, obj);
//    ok &= compareDimensions(vtkStructuredGrid::SafeDownCast(ds), obj);
//    ok &= comparePointArrays(ds, obj);
//    ok &= compareCellArrays(ds, obj);
//    ok &= compareCells(ds, obj);
//    ok &= compareCellCenters(ds, obj);
//    std::cout << (ok ? "STRUCTURED_GRID OK" : "STRUCTURED_GRID FAILED") << std::endl;
//    return ok ? 0 : 4;
//}
//
//static int runUnstructuredGridTest(const std::string& path) {
//    auto ds = readLegacy(path);
//    if (!ds || !vtkUnstructuredGrid::SafeDownCast(ds)) return 1;
//    DataObject obj;
//    VTKDataConverter conv;
//    conv.bindVTKDataAndInternalData(ds, &obj);
//    if (!conv.convertType()) return 2;
//    if (!conv.convertPoints()) return 3;
//    if (!conv.convertDataArrays()) return 4;
//    if (!conv.convertCellCenters()) return 5;
//    if (!conv.convertCell()) return 6;
//    if (!conv.convertPointNeighbors()) return 7;
//    if (!conv.convertPointInCellNeighbors()) return 8;
//    if (!conv.convertCellNeighbors()) return 9;
//    dumpUnstructured(obj);
//    bool ok = true;
//    ok &= comparePoints(ds, obj);
//    ok &= comparePointArrays(ds, obj);
//    ok &= compareCellArrays(ds, obj);
//    ok &= compareCells(ds, obj);
//    ok &= compareCellCenters(ds, obj);
//    std::vector<int> gtPN, gtPNo, gtPCN, gtPCNo, gtCN, gtCNo;
//    buildTopoPointNeighbors(ds, gtPN, gtPNo);
//    buildPointInCellNeighbors(ds, gtPCN, gtPCNo);
//    buildTopoCellNeighbors(ds, gtCN, gtCNo);
//    ok &= compareCSR(obj.pointNeighbors, obj.pointNeighborOffsets, gtPN, gtPNo);
//    ok &= compareCSR(obj.pointInCellNeighbors, obj.pointInCellNeighborOffsets, gtPCN, gtPCNo);
//    ok &= compareCSR(obj.cellNeighbors, obj.cellNeighborsOffsets, gtCN, gtCNo);
//    std::cout << (ok ? "UNSTRUCTURED_GRID OK" : "UNSTRUCTURED_GRID FAILED") << std::endl;
//    return ok ? 0 : 10;
//}
//
//int main(int argc, char** argv) {
//    std::string p1 = "Data\\structured_grid.vtk";
//    std::string p2 = "Data\\ShipHull_0.vtk";
//    if (argc >= 2) p1 = argv[1];
//    if (argc >= 3) p2 = argv[2];
//    int r1 = runStructuredGridTest(p1);
//    int r2 = runUnstructuredGridTest(p2);
//    return (r1 == 0 && r2 == 0) ? 0 : 1;
//}