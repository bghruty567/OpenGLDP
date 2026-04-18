# VTK网格类型与梯度路径说明

## 1. 这份文档回答什么

这份文档只回答一个问题：当前系统里，不同 VTK 数据集类型到底会走哪条梯度计算线。

结论先写在前面：

- `vtkImageData`、`vtkRectilinearGrid`、`vtkStructuredGrid` 会被系统归为规则网格，默认走 `FD`。
- `vtkUnstructuredGrid` 会被系统归为非结构网格，默认走统一 `AWLS` 主线。
- 当前输入侧并不把 `vtkPolyData` 当成一个可直接加载并计算梯度的数据集类型。
- 对于 `vtkUnstructuredGrid` 里的各种单元类型，系统现在不再按单元类型分很多算法线，而是统一走同一套非结构网格最小二乘框架。

## 2. 代码里的总分发入口

总入口在 `CAEProcessingFacade::computeGradient(...)`。

当前分发规则很简单：

```text
if gridType == RegularGrid:
    method = FiniteDifference
else:
    method = AdaptiveWeightedLeastSquares
```

也就是说：

- 规则网格默认主线：`computeByFD(...)`
- 非结构网格默认主线：`computeByAdaptiveWLS(...)`

其中结果名也已经收敛成两类：

- `*_grad_*_FD`
- `*_grad_*_AWLS`

## 3. VTK 数据集类型到系统路径的对应关系

`VTKDataConverter::convertType()` 当前只识别下面几类：

| VTK 数据集类型 | 系统内部类型 | 默认梯度方法 | 实际入口 |
| --- | --- | --- | --- |
| `vtkImageData` | `DATA_OBJECT_TYPE_RegularGrid` | `FD` | `computeGradient -> computeByFD` |
| `vtkRectilinearGrid` | `DATA_OBJECT_TYPE_RegularGrid` | `FD` | `computeGradient -> computeByFD` |
| `vtkStructuredGrid` | `DATA_OBJECT_TYPE_RegularGrid` | `FD` | `computeGradient -> computeByFD` |
| `vtkUnstructuredGrid` | `DATA_OBJECT_TYPE_UNSTRUCTURED` | `AWLS` | `computeGradient -> computeByAdaptiveWLS` |

不在上表中的数据集类型，当前不属于这套主流程的输入范围。

## 4. 规则网格现在怎么走

规则网格统一走有限差分：

```text
computeGradient(...)
-> computeByFD(...)
-> GLGradientEngine::computeRegularFD(...)
-> Shaders/FD.glsl
```

这条线同时支持：

- 点数据梯度
- 单元数据梯度

区别只在于使用的样本位置不同：

- 点数据用 `data.points`
- 单元数据用 `data.cellCenters`

## 5. 非结构网格现在怎么走

非结构网格统一走 AWLS 主线：

```text
computeGradient(...)
-> computeByAdaptiveWLS(...)
-> ensureAdaptiveSupport(...)
-> GLGradientEngine::computeUnstructuredAdaptiveWLS(...)
-> Shaders/AdaptiveWLS.glsl
```

如果 `AdaptiveWLS.glsl` 这条 GPU 线失败，才会在内部退回：

```text
GLGradientEngine::computeUnstructuredWLS(...)
-> Shaders/WLS.glsl
```

注意这里的 `WLS.glsl` 是内部回退内核，不再是 GUI 里单独暴露的一条公开算法线。

## 6. `vtkUnstructuredGrid` 里的各种单元类型现在是不是分开算

现在不是。

只要数据集已经被读成 `vtkUnstructuredGrid`，无论里面是：

- `VTK_TETRA`
- `VTK_HEXAHEDRON`
- `VTK_VOXEL`
- `VTK_WEDGE`
- `VTK_PYRAMID`
- `VTK_TRIANGLE`
- `VTK_QUAD`
- `VTK_POLYGON`
- `VTK_POLYHEDRON`
- 或混合单元

当前梯度主线都不再按这些单元类型分别切成“单元导数线”“局部稀疏算子线”“patch recovery 线”。

现在统一做的是：

1. 选择样本位置
2. 读取点邻接或单元邻接
3. 构造局部支撑
4. 做局部谱分析，得到 `frame / dimTag / quality / meanNeighborDistance`
5. 在 GPU 上做带权最小二乘求梯度

## 7. 点数据和单元数据分别走什么

对非结构网格来说，点数据和单元数据的区别只在“样本”和“邻接”：

| 关联类型 | 样本位置 | 邻接来源 | 支撑缓存 |
| --- | --- | --- | --- |
| `Point` | `data.points` | `data.pointNeighborOffsets + data.pointNeighbors` | `rec.pointSupport` |
| `Cell` | `data.cellCenters` | `data.cellNeighborsOffsets + data.cellNeighbors` | `rec.cellSupport` |

后面的 GPU 最小二乘求解框架是统一的。

## 8. 为什么现在不再按单元类型写很多分支

这样改有三个直接好处：

- 算法口径统一，论文里更容易说清楚。
- 主流程更接近“纯 OpenGL 计算”，不再把很多 VTK 单元导数逻辑混在默认线里。
- 对混合单元和复杂非结构网格更容易维护，也更方便后续做参数实验。

## 9. 论文里建议怎么表述

建议你在论文中这样写当前实现：

> 本系统将 `vtkImageData`、`vtkRectilinearGrid` 和 `vtkStructuredGrid` 归入规则网格，并采用有限差分计算梯度；将 `vtkUnstructuredGrid` 归入非结构网格，并采用统一的自适应加权最小二乘梯度重构框架。对于非结构网格中的不同单元类型，当前实现不再按单元类型分别切换不同求导算法，而是统一通过邻接支撑构造、局部谱分析和 GPU 最小二乘求解完成梯度计算。
