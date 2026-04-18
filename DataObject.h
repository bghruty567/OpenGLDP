#pragma once

#include <string>
#include <vector>

/// 内部字段数组的归属类型。
///
/// 这里不直接复用 VTK 的点数据/单元数据枚举，而是使用项目自己的轻量枚举，
/// 目的是让后续 GPU 模块、测试程序和导出模块都统一依赖这一套内部定义。
enum DataArrayType {
    POINT_DATA = 0,
    CELL_DATA = 1
};

/// 一个字段数组的完整描述。
///
/// 约定：
/// 1. `data` 采用扁平一维存储；
/// 2. 同一个 tuple 的多个分量连续排布；
/// 3. `numComponents` 决定如何解释每个 tuple。
///
/// 例如：
/// - 标量点场：`[v0, v1, v2, ...]`，`numComponents = 1`
/// - 向量点场：`[u0x, u0y, u0z, u1x, u1y, u1z, ...]`，`numComponents = 3`
struct DataArray {
    std::string name;              ///< 字段名，例如 `pressure`、`U`、`stress_grad_P_AWLS`
    std::vector<float> data;       ///< 扁平数组形式保存的字段值
    int numComponents;             ///< 每个 tuple 的分量数
    DataArrayType dataType;        ///< 该字段属于点数据还是单元数据
};

/// 内部网格类型。
///
/// 当前系统只在主流程上区分两大类：
/// - 规则网格：走有限差分 FD；
/// - 非结构网格：走自适应加权最小二乘 AWLS。
enum GridType {
    DATA_OBJECT_TYPE_RegularGrid = 0,
    DATA_OBJECT_TYPE_UNSTRUCTURED = 1
};

/// 项目内部统一的数据对象。
///
/// 这是 VTK 数据进入系统后的中间表示层，承担三个核心作用：
/// 1. 把不同 VTK 网格统一成扁平数组；
/// 2. 为 GPU 计算提供连续内存布局；
/// 3. 为测试程序和导出模块提供统一访问接口。
///
/// 可以把它理解为“算法层真正关心的数据视图”。
class DataObject {
public:
    DataObject() = default;
    ~DataObject() = default;

    GridType gridType;  ///< 当前数据集属于规则网格还是非结构网格

    /// 所有点坐标，按 `[x0,y0,z0,x1,y1,z1,...]` 扁平存储。
    std::vector<float> points;

    /// 所有单元中心坐标，按 `[cx0,cy0,cz0,cx1,cy1,cz1,...]` 扁平存储。
    ///
    /// 对单元字段做梯度或滤波时，通常以它作为几何采样位置。
    std::vector<float> cellCenters;

    /// 当前数据集持有的全部字段数组。
    ///
    /// 其中既可能包含原始输入字段，也可能包含程序运行后生成的结果字段，
    /// 例如梯度结果、多尺度中间层和融合结果等。
    std::vector<DataArray> dataArrays;

    /// 点邻域的 CSR 风格邻接表“值区”。
    ///
    /// 第 i 个点的邻居索引范围为：
    /// `pointNeighbors[pointNeighborOffsets[i] ... pointNeighborOffsets[i+1]-1]`
    std::vector<int> pointNeighbors;

    /// 点邻域的 CSR 风格邻接表“偏移区”。
    std::vector<int> pointNeighborOffsets;

    /// 点所属单元列表的“值区”。
    ///
    /// 这个结构主要用于建立点与单元之间的拓扑关系，便于后续扩展更多局部算子。
    std::vector<int> pointInCellNeighbors;

    /// 点所属单元列表的“偏移区”。
    std::vector<int> pointInCellNeighborOffsets;

    /// 单元连接关系，即单元由哪些点组成。
    ///
    /// 与 `cellOffsets` 配合后，可恢复每个单元的点编号列表。
    std::vector<int> cells;

    /// 单元类型编码。
    ///
    /// 通常来自 VTK 单元类型，用于在导出或拓扑分析时恢复原始网格语义。
    std::vector<int> cellTypes;

    /// 单元连接关系偏移。
    ///
    /// 第 i 个单元的点索引范围为：
    /// `cells[cellOffsets[i] ... cellOffsets[i+1]-1]`
    std::vector<int> cellOffsets;

    /// 单元邻域的 CSR 风格邻接表“值区”。
    ///
    /// 这里的邻域节点不是点，而是“相邻单元中心”的索引。
    std::vector<int> cellNeighbors;

    /// 单元邻域的 CSR 风格邻接表“偏移区”。
    std::vector<int> cellNeighborsOffsets;

    /// 规则网格尺寸，仅对规则网格有效。
    ///
    /// 约定为 `[nx, ny, nz]`。若是非结构网格，这个数组通常不会被使用。
    int dimensions[3];

    /// 根据“名称 + 归属类型”查找可写字段数组。
    ///
    /// 返回的是内部存储对象的指针，调用方应注意：
    /// 当 `dataArrays` 后续发生扩容时，该指针可能失效。
    DataArray* findDataArray(const std::string& name, DataArrayType type);

    /// 根据“名称 + 归属类型”查找只读字段数组。
    const DataArray* findDataArray(const std::string& name, DataArrayType type) const;

    /// 插入新字段，或覆盖已有同名字段。
    ///
    /// 这是测试程序和门面层最常用的写回接口，例如：
    /// - 写入解析 benchmark；
    /// - 写入梯度结果；
    /// - 写入多尺度分解/融合结果；
    /// - 写入人工加噪后的测试字段。
    bool upsertDataArray(const std::string& name,
                         const std::vector<float>& data,
                         int numComponents,
                         DataArrayType type);

    /// 返回点数量，即 `points.size() / 3`。
    size_t pointCount() const;

    /// 返回单元数量。
    ///
    /// 这里通过 `cellOffsets` 推断，因为单元连接关系才是定义单元个数的主依据。
    size_t cellCount() const;
};
