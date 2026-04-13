// VTKDataConverter.cpp
#include "VTKDataConverter.h"
#include <vtkDataSet.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkKdTreePointLocator.h>
#include <iostream>
#include <set>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkStructuredGrid.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkSmartPointer.h>

VTKDataConverter::VTKDataConverter() {}

VTKDataConverter::~VTKDataConverter() {}


void VTKDataConverter::bindVTKDataAndInternalData(vtkDataSet* vtkData, DataObject* internalData) {
    this->vtkData = vtkData;
    this->internalData = internalData;
}


int VTKDataConverter::convertPoints() {
    vtkPoints* VTKPoints = this->vtkData->GetPoints();
    if (!VTKPoints) {
        //std::cerr << "数据集中没有点数据" << std::endl;
        return 0;
    }

    vtkIdType numPoints = VTKPoints->GetNumberOfPoints();
    this->internalData->points.clear();
    this->internalData->points.reserve(numPoints * 3);

    for (vtkIdType i = 0; i < numPoints; i++) {
        double coord[3];
        VTKPoints->GetPoint(i, coord);
        this->internalData->points.push_back(static_cast<float>(coord[0]));
        this->internalData->points.push_back(static_cast<float>(coord[1]));
        this->internalData->points.push_back(static_cast<float>(coord[2]));
    }

    std::cout << "提取了 " << numPoints << " 个点坐标" << std::endl;
    return 1;
}

int VTKDataConverter::convertDataArrays() {
    // 点数组
    if (auto* pointData = this->vtkData->GetPointData()) {
        int nP = pointData->GetNumberOfArrays();
        for (int ai = 0; ai < nP; ++ai) {
            if (auto* da = pointData->GetArray(ai)) {
                DataArray out;
                out.name = da->GetName() ? da->GetName() : "";
                out.numComponents = da->GetNumberOfComponents();
                out.dataType = POINT_DATA;
                vtkIdType tuples = da->GetNumberOfTuples();
                out.data.resize(static_cast<size_t>(tuples * out.numComponents));
                for (vtkIdType t = 0; t < tuples; ++t) {
                    double* tup = da->GetTuple(t);
                    for (int c = 0; c < out.numComponents; ++c) {
                        out.data[static_cast<size_t>(t * out.numComponents + c)] = static_cast<float>(tup[c]);
                    }
                }
                this->internalData->dataArrays.push_back(std::move(out));
            }
        }
		cout << "提取了 " << nP << " 个点数据数组，" << this->internalData->dataArrays.size() << " 个数据数组（包含点数据和单元数据）" << std::endl;
    }

    // 单元数组
    if (auto* cellData = this->vtkData->GetCellData()) {
        int nC = cellData->GetNumberOfArrays();
        for (int ai = 0; ai < nC; ++ai) {
            if (auto* da = cellData->GetArray(ai)) {
                DataArray out;
                out.name = da->GetName() ? da->GetName() : "";
                out.numComponents = da->GetNumberOfComponents();
                out.dataType = CELL_DATA;
                vtkIdType tuples = da->GetNumberOfTuples();
                out.data.resize(static_cast<size_t>(tuples * out.numComponents));
                for (vtkIdType t = 0; t < tuples; ++t) {
                    double* tup = da->GetTuple(t);
                    for (int c = 0; c < out.numComponents; ++c) {
                        out.data[static_cast<size_t>(t * out.numComponents + c)] = static_cast<float>(tup[c]);
                    }
                }
                this->internalData->dataArrays.push_back(std::move(out));
            }
        }
		cout << "提取了 " << nC << " 个单元数据数组，" << this->internalData->dataArrays.size() << " 个数据数组（包含点数据和单元数据）" << std::endl;
    }

    return 1;
}

// 获取点所属单元邻域信息std::vector<int> cellNeighbors;[c0,c2,c5,c1,c3...]
int VTKDataConverter::convertPointInCellNeighbors() {
    vtkStaticCellLinks* cellLinks = vtkStaticCellLinks::New();
    cellLinks->SetDataSet(this->vtkData);
    cellLinks->BuildLinks();
    vtkIdType numPoints = this->vtkData->GetNumberOfPoints();
    this->internalData->pointInCellNeighbors.clear();
    this->internalData->pointInCellNeighborOffsets.clear();
    this->internalData->pointInCellNeighborOffsets.push_back(0);
    for (vtkIdType ptId = 0; ptId < numPoints; ++ptId) {
        vtkIdType numCells;
        const vtkIdType* cellIds = cellLinks->GetCells(ptId);
		numCells = cellLinks->GetNumberOfCells(ptId);
        for (vtkIdType i = 0; i < numCells; ++i) {
            this->internalData->pointInCellNeighbors.push_back(static_cast<int>(cellIds[i]));
        }
        int currentOffset = static_cast<int>(this->internalData->pointInCellNeighborOffsets.back());
        this->internalData->pointInCellNeighborOffsets.push_back(currentOffset + static_cast<int>(numCells));
    }
    cellLinks->Delete();
    std::cout << "提取了点所属单元邻域信息" << std::endl;

    return 1;
}


int VTKDataConverter::convertPointNeighbors() {
    vtkStaticCellLinks* cellLinks = vtkStaticCellLinks::New();
    cellLinks->SetDataSet(this->vtkData);
    cellLinks->BuildLinks();
    vtkIdType numPoints = this->vtkData->GetNumberOfPoints();
    this->internalData->pointNeighbors.clear();
    this->internalData->pointNeighborOffsets.clear();
    this->internalData->pointNeighborOffsets.push_back(0);
    for (vtkIdType ptId = 0; ptId < numPoints; ++ptId) {
        std::set<vtkIdType> neighborPointIds;
        vtkIdType numCells;
        const vtkIdType* cellIds = cellLinks->GetCells(ptId);
        numCells = cellLinks->GetNumberOfCells(ptId);
        for (vtkIdType i = 0; i < numCells; ++i) {
            vtkIdType cellId = cellIds[i];
            vtkCell* cell = this->vtkData->GetCell(cellId);
            vtkIdList* pointIds = cell->GetPointIds();
            for (vtkIdType j = 0; j < pointIds->GetNumberOfIds(); ++j) {
                vtkIdType neighborPtId = pointIds->GetId(j);
                if (neighborPtId != ptId) {
                    neighborPointIds.insert(neighborPtId);
                }
            }
        }
        for (const auto& neighborPtId : neighborPointIds) {
            this->internalData->pointNeighbors.push_back(static_cast<int>(neighborPtId));
        }
        int currentOffset = static_cast<int>(this->internalData->pointNeighborOffsets.back());
        this->internalData->pointNeighborOffsets.push_back(currentOffset + static_cast<int>(neighborPointIds.size()));
    }
    cellLinks->Delete();
    std::cout << "提取了点邻域信息" << std::endl;
	return 1;
}


int VTKDataConverter::convertCell(){
    vtkIdType numCells = this->vtkData->GetNumberOfCells();
    this->internalData->cells.clear();
    this->internalData->cellTypes.clear();
    this->internalData->cellOffsets.clear();
    this->internalData->cellOffsets.push_back(0);
    for (vtkIdType cellId = 0; cellId < numCells; ++cellId) {
        vtkCell* cell = this->vtkData->GetCell(cellId);
        vtkIdList* pointIds = cell->GetPointIds();
        vtkIdType numPointsInCell = pointIds->GetNumberOfIds();
        for (vtkIdType i = 0; i < numPointsInCell; ++i) {
            this->internalData->cells.push_back(static_cast<int>(pointIds->GetId(i)));
        }
        this->internalData->cellTypes.push_back(static_cast<int>(cell->GetCellType()));
        int currentOffset = static_cast<int>(this->internalData->cellOffsets.back());
        this->internalData->cellOffsets.push_back(currentOffset + static_cast<int>(numPointsInCell));
    }
    std::cout << "提取了单元连接关系和单元类型信息" << std::endl;
    return 1;
}



int VTKDataConverter::convertType(){
    if (this->vtkData->IsA("vtkUnstructuredGrid")){
        this->internalData->gridType = DATA_OBJECT_TYPE_UNSTRUCTURED;
    }
    else if (this->vtkData->IsA("vtkStructuredGrid") || this->vtkData->IsA("vtkRectilinearGrid") || this->vtkData->IsA("vtkImageData")){
        this->internalData->gridType = DATA_OBJECT_TYPE_RegularGrid;
    }
    else {
        std::cerr << "不支持的数据集类型" << std::endl;
        return 0;
	}
    return 1;
}

int VTKDataConverter::convertDimensions() {
    if (this->internalData->gridType == DATA_OBJECT_TYPE_RegularGrid) {
        int dims[3];
        if (vtkStructuredGrid* structuredGrid = vtkStructuredGrid::SafeDownCast(this->vtkData)) {
            structuredGrid->GetDimensions(dims);
        }
        else if (vtkRectilinearGrid* rectilinearGrid = vtkRectilinearGrid::SafeDownCast(this->vtkData)) {
            rectilinearGrid->GetDimensions(dims);
        }
        else if (vtkImageData* imageData = vtkImageData::SafeDownCast(this->vtkData)) {
            imageData->GetDimensions(dims);
        }
        else {
            std::cerr << "无法获取规则网格的维度信息" << std::endl;
            return 0;
        }
        this->internalData->dimensions[0] = dims[0];
        this->internalData->dimensions[1] = dims[1];
        this->internalData->dimensions[2] = dims[2];
    }
    return 1;
}

int VTKDataConverter::convertCellCenters() {
    vtkIdType numCells = this->vtkData->GetNumberOfCells();
    this->internalData->cellCenters.clear();
    this->internalData->cellCenters.reserve(static_cast<size_t>(numCells * 3));

    for (vtkIdType cid = 0; cid < numCells; ++cid) {
        vtkCell* cell = this->vtkData->GetCell(cid);
        double pcoords[3]; int subId = cell->GetParametricCenter(pcoords);
        double xyz[3]; std::vector<double> weights(cell->GetNumberOfPoints());
        cell->EvaluateLocation(subId, pcoords, xyz, weights.data());
        this->internalData->cellCenters.push_back(static_cast<float>(xyz[0]));
        this->internalData->cellCenters.push_back(static_cast<float>(xyz[1]));
        this->internalData->cellCenters.push_back(static_cast<float>(xyz[2]));
    }
    return 1;
}

int VTKDataConverter::convertRegularGrid() {
    if (this->internalData->gridType != DATA_OBJECT_TYPE_RegularGrid) {
        std::cerr << "当前数据集不是规则网格，无法执行规则网格转换" << std::endl;
        return 0;
    }
    this->convertPoints();
    this->convertDimensions();
    this->convertDataArrays();
    this->convertCellCenters();
    this->convertCell();
    return 1;
}

int VTKDataConverter ::convertUnstructuredGrid() {
    if (this->internalData->gridType != DATA_OBJECT_TYPE_UNSTRUCTURED) {
        std::cerr << "当前数据集不是非结构化网格，无法执行非结构化网格转换" << std::endl;
        return 0;
    }
    this->convertPoints();
    this->convertDataArrays();
    this->convertPointInCellNeighbors();
    //this->convertPointNeighborsByKNN(20);
    //this->convertPointNeighborsRobust(48,100);
	//this->convertPointNeighbors();
    if (!this->convertPointNeighborsRobust(12, 24)) {
        this->convertPointNeighbors();
    }
    this->convertCellCenters();
    this->convertCell();
    //this->convertCellNeighborsByKNN(5);
    this->convertCellNeighbors();
    return 1;
}

int VTKDataConverter::convertVTKToInternal() {
    if (!this->vtkData || !this->internalData) {
        std::cerr << "VTK数据集或内部数据对象未绑定" << std::endl;
        return 0;
    }
    if (!this->convertType()) {
        return 0;
    }
    if (this->internalData->gridType == DATA_OBJECT_TYPE_RegularGrid) {
        return this->convertRegularGrid();
    }
    else if (this->internalData->gridType == DATA_OBJECT_TYPE_UNSTRUCTURED) {
        return this->convertUnstructuredGrid();
    }
    else {
        std::cerr << "未知的数据集类型" << std::endl;
        return 0;
    }
}

int VTKDataConverter::convertPointNeighborsByKNN(int K) {
    if (!this->vtkData || !this->internalData) return 0;
    vtkNew<vtkKdTreePointLocator> locator;
    locator->SetDataSet(this->vtkData);
    locator->BuildLocator();
    vtkIdType numPoints = this->vtkData->GetNumberOfPoints();

    this->internalData->pointNeighbors.clear();
    this->internalData->pointNeighborOffsets.clear();
    this->internalData->pointNeighborOffsets.reserve(static_cast<size_t>(numPoints) + 1);
    this->internalData->pointNeighborOffsets.push_back(0);

    vtkNew<vtkIdList> ids;
    for (vtkIdType pid = 0; pid < numPoints; ++pid) {
        double q[3]; this->vtkData->GetPoint(pid, q);
        locator->FindClosestNPoints(K + 1, q, ids);
        size_t before = this->internalData->pointNeighbors.size();
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
            vtkIdType nb = ids->GetId(i);
            if (nb != pid) this->internalData->pointNeighbors.push_back(static_cast<int>(nb));
            if (this->internalData->pointNeighbors.size() - before == static_cast<size_t>(K)) break;
        }
        this->internalData->pointNeighborOffsets.push_back(
            static_cast<int>(this->internalData->pointNeighbors.size()));
    }
    return 1;
}

int VTKDataConverter::convertCellNeighbors() {
    vtkStaticCellLinks* cellLinks = vtkStaticCellLinks::New();
    cellLinks->SetDataSet(this->vtkData);
    cellLinks->BuildLinks();

    vtkIdType numCells = this->vtkData->GetNumberOfCells();
    this->internalData->cellNeighbors.clear();
    this->internalData->cellNeighborsOffsets.clear();
    this->internalData->cellNeighborsOffsets.push_back(0);

    for (vtkIdType cellId = 0; cellId < numCells; ++cellId) {
        std::set<vtkIdType> neighborCellIds;

        vtkCell* cell = this->vtkData->GetCell(cellId);
        if (!cell) {
            // 保持偏移一致（无邻居）
            int currentOffsetEmpty = static_cast<int>(this->internalData->cellNeighborsOffsets.back());
            this->internalData->cellNeighborsOffsets.push_back(currentOffsetEmpty);
            continue;
        }

        vtkIdList* pointIds = cell->GetPointIds();
        for (vtkIdType pi = 0; pi < pointIds->GetNumberOfIds(); ++pi) {
            vtkIdType ptId = pointIds->GetId(pi);
            const vtkIdType* usingCellIds = cellLinks->GetCells(ptId);
            vtkIdType nc = cellLinks->GetNumberOfCells(ptId);
            for (vtkIdType ci = 0; ci < nc; ++ci) {
                vtkIdType nbCellId = usingCellIds[ci];
                if (nbCellId != cellId) {
                    neighborCellIds.insert(nbCellId);
                }
            }
        }

        for (const auto& nb : neighborCellIds) {
            this->internalData->cellNeighbors.push_back(static_cast<int>(nb));
        }
        int currentOffset = static_cast<int>(this->internalData->cellNeighborsOffsets.back());
        this->internalData->cellNeighborsOffsets.push_back(currentOffset + static_cast<int>(neighborCellIds.size()));
    }

    cellLinks->Delete();
    std::cout << "提取了单元邻域信息" << std::endl;
    return 1;
}

int VTKDataConverter::convertCellNeighborsByKNN(int K) {
    if (!this->vtkData || !this->internalData) return 0;
    const size_t n3 = this->internalData->cellCenters.size();
    if (n3 % 3 != 0) return 0;
    vtkIdType numCells = static_cast<vtkIdType>(n3 / 3);
    this->internalData->cellNeighbors.clear();
    this->internalData->cellNeighborsOffsets.clear();
    this->internalData->cellNeighborsOffsets.reserve(static_cast<size_t>(numCells) + 1);
    this->internalData->cellNeighborsOffsets.push_back(0);
    if (numCells == 0) return 1;

    vtkNew<vtkPoints> pts;
    pts->SetNumberOfPoints(numCells);
    for (vtkIdType cid = 0; cid < numCells; ++cid) {
        double x = this->internalData->cellCenters[cid * 3 + 0];
        double y = this->internalData->cellCenters[cid * 3 + 1];
        double z = this->internalData->cellCenters[cid * 3 + 2];
        pts->SetPoint(cid, x, y, z);
    }
    vtkNew<vtkPolyData> pd; pd->SetPoints(pts);
    vtkNew<vtkKdTreePointLocator> locator;
    locator->SetDataSet(pd); locator->BuildLocator();

    vtkNew<vtkIdList> ids;
    for (vtkIdType cid = 0; cid < numCells; ++cid) {
        double q[3]; pts->GetPoint(cid, q);
        locator->FindClosestNPoints(K + 1, q, ids);
        size_t before = this->internalData->cellNeighbors.size();
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
            vtkIdType nb = ids->GetId(i);
            if (nb != cid) this->internalData->cellNeighbors.push_back(static_cast<int>(nb));
            if (this->internalData->cellNeighbors.size() - before == static_cast<size_t>(K)) break;
        }
        this->internalData->cellNeighborsOffsets.push_back(
            static_cast<int>(this->internalData->cellNeighbors.size()));
    }
    return 1;
}


int VTKDataConverter::convertPointNeighborsRobust(int minK, int knnK)
{
    vtkIdType numPoints = this->vtkData->GetNumberOfPoints();
    if (numPoints <= 0) return 0;

    vtkNew<vtkStaticCellLinks> cellLinks;
    cellLinks->SetDataSet(this->vtkData);
    cellLinks->BuildLinks();

    vtkNew<vtkKdTreePointLocator> locator;
    locator->SetDataSet(this->vtkData);
    locator->BuildLocator();

    this->internalData->pointNeighbors.clear();
    this->internalData->pointNeighborOffsets.clear();
    this->internalData->pointNeighborOffsets.reserve(static_cast<size_t>(numPoints) + 1);
    this->internalData->pointNeighborOffsets.push_back(0);

    vtkNew<vtkIdList> ids;

    for (vtkIdType ptId = 0; ptId < numPoints; ++ptId)
    {
        std::set<int> nbset;

        vtkIdType numCells = cellLinks->GetNumberOfCells(ptId);
        const vtkIdType* cellIds = cellLinks->GetCells(ptId);
        for (vtkIdType i = 0; i < numCells; ++i)
        {
            vtkCell* cell = this->vtkData->GetCell(cellIds[i]);
            vtkIdList* pointIds = cell->GetPointIds();
            for (vtkIdType j = 0; j < pointIds->GetNumberOfIds(); ++j)
            {
                vtkIdType q = pointIds->GetId(j);
                if (q != ptId) nbset.insert(static_cast<int>(q));
            }
        }

        if ((int)nbset.size() < minK)
        {
            double qpos[3]; this->vtkData->GetPoint(ptId, qpos);
            double topoMeanDist = 0.0;
            int topoCount = 0;
            for (int q : nbset)
            {
                double p[3]; this->vtkData->GetPoint(static_cast<vtkIdType>(q), p);
                double dx = p[0] - qpos[0], dy = p[1] - qpos[1], dz = p[2] - qpos[2];
                topoMeanDist += std::sqrt(dx * dx + dy * dy + dz * dz);
                ++topoCount;
            }
            if (topoCount > 0) topoMeanDist /= static_cast<double>(topoCount);
            double maxAcceptDist = topoCount > 0 ? (2.5 * topoMeanDist) : std::numeric_limits<double>::max();
            locator->FindClosestNPoints(knnK + 1, qpos, ids);
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            {
                vtkIdType q = ids->GetId(i);
                if (q == ptId) continue;
                double p[3]; this->vtkData->GetPoint(q, p);
                double dx = p[0] - qpos[0], dy = p[1] - qpos[1], dz = p[2] - qpos[2];
                double d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > maxAcceptDist) continue;
                nbset.insert(static_cast<int>(q));
                if ((int)nbset.size() >= minK) break;
            }
        }

        for (int q : nbset) this->internalData->pointNeighbors.push_back(q);
        int cur = static_cast<int>(this->internalData->pointNeighborOffsets.back());
        this->internalData->pointNeighborOffsets.push_back(cur + static_cast<int>(nbset.size()));
    }
    return 1;
}
int VTKDataConverter::convertInternalToVTK()
{
    if (!this->internalData) return 0;

    vtkNew<vtkPoints> pts;
    const size_t np = this->internalData->pointCount();
    pts->SetNumberOfPoints(static_cast<vtkIdType>(np));
    for (vtkIdType i = 0; i < static_cast<vtkIdType>(np); ++i) {
        pts->SetPoint(
            i,
            this->internalData->points[size_t(i) * 3 + 0],
            this->internalData->points[size_t(i) * 3 + 1],
            this->internalData->points[size_t(i) * 3 + 2]
        );
    }

    vtkDataSet* out = nullptr;

    if (this->internalData->gridType == DATA_OBJECT_TYPE_RegularGrid) {
        vtkStructuredGrid* sg = vtkStructuredGrid::New();
        sg->SetDimensions(
            this->internalData->dimensions[0],
            this->internalData->dimensions[1],
            this->internalData->dimensions[2]
        );
        sg->SetPoints(pts);
        out = sg;
    }
    else if (this->internalData->gridType == DATA_OBJECT_TYPE_UNSTRUCTURED) {
        vtkUnstructuredGrid* ug = vtkUnstructuredGrid::New();
        ug->SetPoints(pts);

        const size_t nc = this->internalData->cellCount();
        for (size_t c = 0; c < nc; ++c) {
            int b = this->internalData->cellOffsets[c];
            int e = this->internalData->cellOffsets[c + 1];
            if (e <= b) continue;

            std::vector<vtkIdType> ids;
            ids.reserve(static_cast<size_t>(e - b));
            for (int k = b; k < e; ++k) {
                ids.push_back(static_cast<vtkIdType>(this->internalData->cells[k]));
            }

            int cellType = VTK_POLY_VERTEX;
            if (c < this->internalData->cellTypes.size()) {
                cellType = this->internalData->cellTypes[c];
            }
            ug->InsertNextCell(cellType, static_cast<vtkIdType>(ids.size()), ids.data());
        }
        out = ug;
    }
    else {
        return 0;
    }

    const vtkIdType outNP = out->GetNumberOfPoints();
    const vtkIdType outNC = out->GetNumberOfCells();

    for (const auto& a : this->internalData->dataArrays) {
        if (a.numComponents <= 0) continue;
        if (a.data.empty()) continue;

        vtkNew<vtkFloatArray> arr;
        arr->SetName(a.name.c_str());
        arr->SetNumberOfComponents(a.numComponents);
        const vtkIdType tuples = static_cast<vtkIdType>(a.data.size() / size_t(a.numComponents));
        arr->SetNumberOfTuples(tuples);

        for (vtkIdType t = 0; t < tuples; ++t) {
            for (int c = 0; c < a.numComponents; ++c) {
                arr->SetComponent(t, c, a.data[size_t(t) * size_t(a.numComponents) + size_t(c)]);
            }
        }

        if (a.dataType == POINT_DATA && tuples == outNP) {
            out->GetPointData()->AddArray(arr);
        }
        else if (a.dataType == CELL_DATA && tuples == outNC) {
            out->GetCellData()->AddArray(arr);
        }
    }

    this->vtkData = out;
    return 1;
}