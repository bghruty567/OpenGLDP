#pragma once

#include "DataObject.h"

#include "vtkCellArray.h"
#include "vtkDataArray.h"
#include "vtkDataSet.h"
#include "vtkImageData.h"
#include "vtkPoints.h"
#include "vtkRectilinearGrid.h"
#include "vtkStaticCellLinks.h"
#include "vtkStructuredGrid.h"
#include "vtkUnstructuredGrid.h"

#include <string>
#include <vector>

/// VTK 数据与内部 `DataObject` 之间的双向转换器。
///
/// 它的职责主要有两类：
/// 1. 把 VTK 网格拆成项目内部统一的扁平数组结构；
/// 2. 把内部数据对象重新组装成可导出的 `vtkDataSet`。
///
/// 这是“VTK 世界”和“算法世界”之间的桥梁。
class VTKDataConverter {
public:
    VTKDataConverter();
    ~VTKDataConverter();

    /// 将当前绑定的 `vtkDataSet` 转换为内部 `DataObject`。
    ///
    /// 内部会先识别网格类型，再分别走规则网格或非结构网格转换流程。
    int convertVTKToInternal();

    /// 绑定输入的 VTK 数据对象和输出的内部数据对象。
    void bindVTKDataAndInternalData(vtkDataSet* vtkData, DataObject* internalData);

    /// 将当前绑定的内部数据对象重新转换成 `vtkDataSet`。
    int convertInternalToVTK();

    vtkDataSet* vtkData = nullptr;   ///< 当前待转换的 VTK 数据集
    DataObject* internalData = nullptr; ///< 当前输出或输入的内部数据对象

public:
    /// 提取点坐标。
    int convertPoints();

    /// 提取点数据数组与单元数据数组。
    int convertDataArrays();

    /// 构建“点属于哪些单元”的邻接信息。
    int convertPointInCellNeighbors();

    /// 基于共享单元拓扑构建点邻域。
    int convertPointNeighbors();

    /// 提取单元连接关系、偏移和单元类型。
    int convertCell();

    /// 将 VTK 数据类型映射成内部网格类型。
    int convertType();

    /// 读取规则网格尺寸 `[nx, ny, nz]`。
    int convertDimensions();

    /// 计算单元中心坐标。
    int convertCellCenters();

    /// 规则网格总转换流程。
    int convertRegularGrid();

    /// 非结构网格总转换流程。
    int convertUnstructuredGrid();

    /// 通过 KNN 为点构建邻域。
    ///
    /// 这更偏几何邻域，不一定严格遵从拓扑邻接。
    int convertPointNeighborsByKNN(int k);

    /// 为单元中心构建单元邻域，优先使用拓扑相邻关系。
    int convertCellNeighbors();

    /// 通过 KNN 为单元中心构建邻域。
    int convertCellNeighborsByKNN(int k);

    /// 更稳健的点邻域构建策略。
    ///
    /// 先利用拓扑邻域，若邻域数量不足，再用 KNN 补足。
    int convertPointNeighborsRobust(int minK, int knnK);
};
