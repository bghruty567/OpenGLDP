# AI交接文档

## 1. 项目定位

`OpenGLDP` 是一个面向 CAE/可视分析场景的桌面原型系统，目标是把梯度计算与数据优化两类数值处理任务从传统 CPU/VTK 主路径中剥离出来，改为通过 OpenGL Compute Shader 在 GPU 上执行。当前工程包含三类能力：

1. 非结构化/规则网格上的梯度计算。
2. 面向 CAE 后处理结果场的局部随机高频数值扰动抑制。
3. 配套 GUI、基准测试程序与结果导出流程。

其中第 2 项当前的实现形式是“图双边滤波驱动的多尺度分解与融合”，但论文与答辩口径不建议把它描述成“通用去噪器”或“求解误差修正器”。更准确的定位是：

- 面向后处理结果场的稳定化模块；
- 目标对象是其中一类局部随机高频、近邻不一致、具有空间局部性的数值扰动；
- 其意义在于改善结果场连续性、可视化观感和后续处理适配性，而不是消除离散化误差根因。

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

当前实现分三条线，且实际计算主路径都在 OpenGL 侧完成：

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

当前建议采用的模块定位是：

- 面向 CAE 后处理结果场的局部随机高频数值扰动抑制模块；
- 不是对求解器误差、离散化误差根因的直接修正；
- 不是对任意噪声类型都有效的通用去噪器。

当前更稳妥的问题表述是：

1. CAE 仿真结果会因网格质量、空间离散、梯度重构、插值以及单元间不连续等因素出现局部数值扰动。
2. 对于其中一类表现为局部随机高频波动的扰动，可采用高斯扰动作为代理模型进行近似描述。
3. 本模块通过多尺度分解与融合，对这类扰动进行抑制，同时尽量保留边缘和主结构。

当前流程为：

1. 基于点邻域或单元邻域构图。
2. 估计平均邻距与字段标准差。
3. 将无量纲参数转换为当前数据尺度上的 `spatialSigma`、`rangeSigma`、`edgeSigma`。
4. 逐层双边滤波得到平滑层。
5. 相邻平滑层作差得到细节层。
6. 以设定增益融合得到 `fused` 结果。

当前文档与论文中建议补上的边界说明是：

- “高斯扰动”在这里是代理模型，用于受控验证；
- 这不等于宣称真实 CAE 数值扰动整体服从高斯分布；
- 模块当前验证目标是“对一类局部随机高频扰动的抑制能力及边缘保持特性”。

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

如果按当前项目的实际归档结果组织材料，还可以补充说明：

- 已归档结果位于 `results/mul`；
- 当前已有 `POINT` 和 `CELL` 两类结果；
- 当前合成干净场包括 `ms_clean_trig`、`ms_clean_gaussian`、`ms_clean_poly`、`ms_clean_edge`；
- 当前扰动类型包括 `gaussian`、`mixed`、`impulse`。

论文主线建议写法是：

1. 以 `gaussian` 作为主验证扰动类型，支撑主要结论。
2. 以 `mixed` 作为补充结果，说明模块对混合型局部扰动有一定适配性。
3. 将 `impulse` 作为边界案例或负结果，明确当前模块不宜对脉冲型离群扰动作强正向结论。

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

### 5.4 数据优化模块的结论边界要单独写明

当前数据优化模块最容易被质疑的点不是“效果图是否好看”，而是“扰动模型是否有科学依据”。因此后续写作或答辩时必须坚持以下边界：

1. 可以说：真实 CAE 结果场中客观存在局部数值扰动、非光滑、局部振荡和单元间不连续。
2. 可以说：对于其中一类表现为局部随机高频波动的扰动，可采用高斯扰动作为代理模型进行近似描述，并用受控实验验证模块适配性。
3. 不要说：真实 CAE 噪声整体就是高斯噪声。
4. 不要说：当前模块已经证明能消除仿真误差根因。

当前更安全的结论是：

- 该模块适合做 CAE 后处理结果场的稳定化与边缘保持型平滑；
- 它对一类局部随机高频扰动具有抑制能力；
- 其意义在于提高结果场连续性与后续可视化适配性。

## 6. 下一任 AI / 开发者优先事项

1. 先学习 `vtkGradientFilter` 的退化/保护机制，再回头修 `notch_stress`。
2. 若后续还有时间，再补更强的点梯度稳定化策略，例如：
   - 退化 Jacobian 检查；
   - 面内梯度与法向分量分离；
   - 点值汇聚时的几何权重或面积/体积权重。
3. 若要做更严谨的性能对比，补充 VTK 线程控制与记录逻辑，并把单线程、并行线程数、GPU 墙钟、GPU 纯计算时间分开归档。
4. 若要继续加强数据优化模块的科学性，优先做三件事：
   - 从真实结果场中举出局部数值扰动的可视或统计证据；
   - 检查融合策略是否会引入新的局部伪影；
   - 若时间允许，补充更接近局部相关扰动的代理实验，如局部相关随机场。
5. 若论文只保主线，优先保证 `SampleStructGrid + hexa + ShipHull_0 + limb` 的梯度实验闭环，以及 `ShipHull_0` 上的数据优化实验、图表和 ParaView 配图闭环。

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
