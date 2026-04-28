# AI交接文档

## 1. 项目定位

`OpenGLDP` 是一个面向 CAE/可视分析场景的桌面原型系统，目标是把梯度计算与数据优化两类数值处理任务从传统 CPU/VTK 主路径中剥离出来，改为通过 OpenGL Compute Shader 在 GPU 上执行。当前工程包含三类能力：

1. 非结构化/规则网格上的梯度计算。
2. 图双边滤波驱动的多尺度分解与融合。
3. 配套 GUI、基准测试程序与结果导出流程。

需要明确的是：系统目前**不是完全脱离 VTK**。VTK 仍用于数据读写、GUI 渲染和部分测试参考基线；但系统自己的梯度与滤波计算主路径已经下沉到 OpenGL 计算着色器中。相关代码入口见：

- [CAEProcessingFacade.h](../CAEProcessingFacade.h)
- [CAEProcessingFacade.cpp](../CAEProcessingFacade.cpp)
- [VTKDataConverter.h](../VTKDataConverter.h)
- [GLGradientEngine.h](../GLGradientEngine.h)
- [GLFilterEngine.h](../GLFilterEngine.h)

## 2. 代码结构总览

| 模块 | 作用 | 关键文件 |
| --- | --- | --- |
| 门面层 | 对外统一暴露数据加载、梯度计算、数据优化、导出接口 | [CAEProcessingFacade.h](../CAEProcessingFacade.h), [CAEProcessingFacade.cpp](../CAEProcessingFacade.cpp) |
| 接口类型 | 定义请求/结果结构体与枚举 | [CAEInterfaceTypes.h](../CAEInterfaceTypes.h) |
| 内部数据模型 | 将规则/非结构网格统一为扁平数组与 CSR 邻接 | [DataObject.h](../DataObject.h), [DataObject.cpp](../DataObject.cpp) |
| VTK 桥接 | `vtkDataSet` 与 `DataObject` 双向转换 | [VTKDataConverter.h](../VTKDataConverter.h), [VTKDataConverter.cpp](../VTKDataConverter.cpp) |
| OpenGL 运行时 | 创建独立计算上下文并提供 GPU 运行信息 | [OpenGLManager.h](../OpenGLManager.h) |
| 梯度引擎 | 规则网格 FD、非结构网格形函数导数、AWLS | [GLGradientEngine.h](../GLGradientEngine.h), [GLGradientEngine.cpp](../GLGradientEngine.cpp) |
| 数据优化引擎 | 图双边滤波、多尺度细节融合 | [GLFilterEngine.h](../GLFilterEngine.h), [GLFilterEngine.cpp](../GLFilterEngine.cpp) |
| GUI | 文件加载、参数录入、结果显示与导出 | [app/MainWindow.h](../app/MainWindow.h), [app/MainWindow.cpp](../app/MainWindow.cpp) |
| 梯度测试程序 | 精度、时间、VTK 基线对比 | [TestGradient.cpp](../TestGradient.cpp) |
| 多尺度测试程序 | 噪声注入、优化评估、VTK 导出 | [TestMultiScale.cpp](../TestMultiScale.cpp) |
| 字段评价程序 | 对多个已有字段做离线误差/粗糙度评价 | [TestFieldMetrics.cpp](../TestFieldMetrics.cpp) |

## 3. 当前实现状态

### 3.1 数据流

当前主数据流如下：

1. `CAEProcessingFacade::loadDatasetFromVTKFile()` 用 `vtkDataSetReader` 读取 legacy VTK。
2. `VTKDataConverter` 将数据转成内部 `DataObject`。
3. 梯度模块或数据优化模块只读取 `DataObject` 和对应数组，不直接操作 VTK 内存布局。
4. 结果以新数组形式回写 `DataObject`。
5. GUI 显示或文件导出时，再由 `VTKDataConverter::convertInternalToVTK()` 转回 `vtkDataSet`。

这意味着：

- 计算阶段的数据布局是系统自有的，不依赖 VTK 数组接口。
- I/O 与渲染阶段仍依赖 VTK。

### 3.2 梯度模块

当前梯度模块的公开方法定义见 [CAEInterfaceTypes.h](../CAEInterfaceTypes.h) 中的 `CAEGradientMethod`，核心调度在 [CAEProcessingFacade.cpp](../CAEProcessingFacade.cpp) 的 `computeGradient()`。

当前实现分三条线：

1. 规则网格：`FiniteDifference`
   - 着色器文件：[Shaders/FD.glsl](../Shaders/FD.glsl)
   - 调用入口：`GLGradientEngine::computeRegularFD()`
2. 非结构网格：`ShapeFunctionDerivatives`
   - 点数据：`ShapePointGradient.glsl`
   - 单元数据：`CellDataToPointLift.glsl` + `ShapeCellGradient.glsl`
   - 调用入口：`GLGradientEngine::computeUnstructuredShapeFunctionPoint()` / `computeUnstructuredShapeFunctionCell()`
3. 非结构网格：`AdaptiveWeightedLeastSquares`
   - 着色器文件：[Shaders/AdaptiveWLS.glsl](../Shaders/AdaptiveWLS.glsl)
   - 调用入口：`GLGradientEngine::computeUnstructuredAdaptiveWLS()`

自动分派逻辑已经是：

- 规则网格 `Auto -> FiniteDifference`
- 非结构网格 `Auto -> ShapeFunctionDerivatives`

当前论文主线建议表述为：

- 规则网格采用 GPU 有限差分梯度计算。
- 非结构网格主线采用 GPU 形函数导数梯度计算。
- AWLS 保留在代码中，作为历史实现或扩展算法，不再作为主验证路线。

### 3.3 非结构网格单元支持范围

从着色器实现看，当前 `ShapePointGradient.glsl` / `ShapeCellGradient.glsl` 已写入以下单元的形函数导数：

- `VTK_LINE`
- `VTK_TRIANGLE`
- `VTK_PIXEL`
- `VTK_QUAD`
- `VTK_TETRA`
- `VTK_VOXEL`
- `VTK_HEXAHEDRON`
- `VTK_WEDGE`

但从论文和主实验口径上，建议**聚焦并正式宣称已验证的一阶单元类型**为：

- 三角形 `TRIANGLE`
- 四边形 `QUAD`
- 四面体 `TETRA`
- 六面体 `HEXAHEDRON`

原因有两点：

1. 这四类最符合当前毕设主线和已有实验数据组织。
2. `PIXEL/VOXEL/WEDGE/LINE` 虽有代码路径，但未形成同等强度的论文级验证闭环。

### 3.4 数据优化模块

数据优化模块统一入口是 [CAEProcessingFacade.cpp](../CAEProcessingFacade.cpp) 中的 `computeMultiScaleDecompositionAndFusion()`，底层 GPU 算法在：

- [Shaders/Bilateral.glsl](../Shaders/Bilateral.glsl)
- [Shaders/MultiScaleFuse.glsl](../Shaders/MultiScaleFuse.glsl)

当前流程为：

1. 基于点邻域或单元邻域构图。
2. 估计平均邻距与字段标准差。
3. 将无量纲参数转换为当前数据尺度上的 `spatialSigma`、`rangeSigma`、`edgeSigma`。
4. 逐层双边滤波得到平滑层。
5. 相邻平滑层作差得到细节层。
6. 以设定增益融合得到 `fused` 结果。

## 4. 当前建议实验口径

### 4.1 梯度模块

建议主实验数据为：

- 规则网格：[Data/SampleStructGrid.vtk](../Data/SampleStructGrid.vtk)
- 非结构网格：[Data/hexa.vtk](../Data/hexa.vtk)
- 非结构网格：[Data/ShipHull_0.vtk](../Data/ShipHull_0.vtk)
- 非结构网格：[Data/limb.vtk](../Data/limb.vtk)

建议主实验内容为：

1. 解析场基准正确性验证。
2. 与 `vtkGradientFilter` 的工程结果对照。
3. VTK 单线程、VTK 并行、GPU 墙钟时间、GPU 纯计算时间对比。
4. `ShipHull_0` 的 ParaView 对照渲染图。

### 4.2 数据优化模块

建议主实验数据为：

- [Data/ShipHull_0.vtk](../Data/ShipHull_0.vtk)

建议主实验内容为：

1. 合成干净场 `ms_clean_edge`、`ms_clean_gaussian`。
2. 对每个干净场叠加多档高斯噪声。
3. 输出优化前后误差、粗糙度、改进率。
4. 导出 ParaView 对比图。

相关辅助工具：

- [TestMultiScale.cpp](../TestMultiScale.cpp)
- [TestFieldMetrics.cpp](../TestFieldMetrics.cpp)
- [TestHarnessUtils.h](../TestHarnessUtils.h)

## 5. 已知问题与风险

### 5.1 `notch_stress` 点梯度问题尚未闭环

当前在 [Data/notch_stress.vtk](../Data/notch_stress.vtk) 上，点数据形函数导数路径会出现：

- `ShapePointGradient` 输出全零；
- 触发 `ShapeCellGradient + CellDataToPointLift` 回退；
- 回退后结果仍可能与 `vtkGradientFilter` 偏差极大。

对应实现位置：

- [GLGradientEngine.cpp](../GLGradientEngine.cpp)
- [Shaders/ShapePointGradient.glsl](../Shaders/ShapePointGradient.glsl)
- [Shaders/ShapeCellGradient.glsl](../Shaders/ShapeCellGradient.glsl)
- [Shaders/CellDataToPointLift.glsl](../Shaders/CellDataToPointLift.glsl)

因此：

- `notch_stress` 不建议作为当前论文主定量结论的数据集。
- 可以把它作为“复杂曲面/退化情形下的限制案例”单列讨论。

### 5.2 VTK 对照不是数学真值

`vtkGradientFilter` 在当前工程中只作为真实工程字段的参考实现，不应被描述成解析真值。相关代码在 [TestGradient.cpp](../TestGradient.cpp)。

建议论文中明确区分：

1. 解析场实验：验证算法正确性。
2. VTK 对照实验：验证工程可比性和参考一致性。

### 5.3 比较必须保证条件一致

老师强调的比较原则应写进实验设计与答辩口径：

1. 与 VTK 比较时，必须说明比较对象是“结果一致性”还是“时间性能”。
2. 若比较时间，必须分开写：
   - VTK 单线程时间
   - VTK 并行时间与线程数
   - 本系统总墙钟时间
   - 本系统 GPU 纯计算时间
3. 不能把 CPU 的 VTK 时间与 GPU 内核时间直接并列宣称“加速比”。

## 6. 下一任 AI / 开发者优先事项

1. 先学习 `vtkGradientFilter` 的退化/保护机制，再回头修 `notch_stress`。
2. 若后续还有时间，再补更强的点梯度稳定化策略，例如：
   - 退化 Jacobian 检查；
   - 面内梯度与法向分量分离；
   - 点值汇聚时的几何权重或面积/体积权重。
3. 若要做更严谨的性能对比，补充 VTK 线程控制与记录逻辑。
4. 若论文只保主线，优先保证 `SampleStructGrid + hexa + ShipHull_0 + limb` 四组数据与配图闭环。

## 7. 推荐先读文件

建议新的 AI 或开发者按以下顺序接手：

1. [CAEProcessingFacade.cpp](../CAEProcessingFacade.cpp)
2. [GLGradientEngine.cpp](../GLGradientEngine.cpp)
3. [VTKDataConverter.cpp](../VTKDataConverter.cpp)
4. [TestGradient.cpp](../TestGradient.cpp)
5. [TestMultiScale.cpp](../TestMultiScale.cpp)
6. [TestHarnessUtils.h](../TestHarnessUtils.h)
7. [doc/系统设计文档.md](./系统设计文档.md)
8. [doc/实验方案文档.md](./实验方案文档.md)

