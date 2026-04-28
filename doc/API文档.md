# API文档

## 1. 文档范围

本文档说明当前工程对外暴露的 C++ 接口、数据结构、结果命名规则以及测试程序入口。核心接口定义位于：

- [CAEInterfaceTypes.h](../CAEInterfaceTypes.h)
- [CAEProcessingFacade.h](../CAEProcessingFacade.h)

## 2. 核心枚举

### 2.1 `CAEFieldAssociation`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 枚举值 | 含义 |
| --- | --- |
| `Point` | 点数据 |
| `Cell` | 单元数据 |

### 2.2 `CAEGridClass`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 枚举值 | 含义 |
| --- | --- |
| `Regular` | 规则网格 |
| `Unstructured` | 非结构化网格 |

### 2.3 `CAEGradientMethod`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 枚举值 | 含义 |
| --- | --- |
| `Auto` | 自动选择；当前实现为规则网格走 FD，非结构网格走 Shape Function |
| `FiniteDifference` | 规则网格 GPU 有限差分 |
| `AdaptiveWeightedLeastSquares` | 非结构网格 GPU 自适应加权最小二乘 |
| `ShapeFunctionDerivatives` | 非结构网格 GPU 形函数导数法 |

## 3. 核心结构体

### 3.1 `CAEFieldInfo`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | `std::string` | 字段名 |
| `association` | `CAEFieldAssociation` | 点或单元关联 |
| `numComponents` | `int` | 每个 tuple 的分量数 |
| `tupleCount` | `std::size_t` | tuple 数量 |

### 3.2 `CAEGradientRequest`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `datasetId` | `std::string` | 数据集 ID，由 `loadDatasetFromVTKFile()` 返回 |
| `inputArrayName` | `std::string` | 输入字段名 |
| `association` | `CAEFieldAssociation` | 输入字段关联类型 |
| `method` | `CAEGradientMethod` | 梯度计算方法 |
| `wlsExponent` | `float` | WLS 权重指数，仅 AWLS 路径使用 |
| `wlsLambda` | `float` | WLS 正则项，仅 AWLS 路径使用 |
| `useAdaptiveNeighborhood` | `bool` | 是否使用自适应邻域，仅 AWLS 使用 |
| `useAdaptiveDimension` | `bool` | 是否使用自适应维度判定，仅 AWLS 使用 |
| `useAdaptiveRegularization` | `bool` | 是否使用自适应正则，仅 AWLS 使用 |
| `minNeighbors` | `int` | 最小邻居数，仅 AWLS 使用 |
| `targetNeighbors` | `int` | 目标邻居数，仅 AWLS 使用 |
| `maxNeighbors` | `int` | 最大邻居数，仅 AWLS 使用 |
| `radiusScale` | `float` | 搜索半径尺度，仅 AWLS 使用 |
| `planeEigenRatio` | `float` | 曲面/体判据，仅 AWLS 使用 |
| `lineEigenRatio` | `float` | 线/面判据，仅 AWLS 使用 |
| `lambdaAmplify` | `float` | 正则增强系数，仅 AWLS 使用 |

### 3.3 `CAEGradientResultMeta`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `resultArrayName` | `std::string` | 输出字段名 |
| `sourceArrayName` | `std::string` | 输入字段名 |
| `association` | `CAEFieldAssociation` | 输出字段关联类型 |
| `method` | `CAEGradientMethod` | 实际执行的方法 |
| `inputComponents` | `int` | 输入字段分量数 |
| `outputComponents` | `int` | 输出字段分量数，通常为 `3 * inputComponents` |
| `computeWallMs` | `double` | 总墙钟时间，单位毫秒 |
| `computeGpuMs` | `double` | GPU 纯计算时间，单位毫秒 |

### 3.4 `CAEMultiScaleRequest`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `datasetId` | `std::string` | 数据集 ID |
| `inputArrayName` | `std::string` | 输入字段名 |
| `association` | `CAEFieldAssociation` | 点或单元关联 |
| `levels` | `int` | 多尺度层数，当前会被裁剪到 `1~3` |
| `iterationsPerLevel` | `int` | 每层迭代次数 |
| `spatialSigmaFactor` | `float` | 空间 sigma 无量纲系数 |
| `rangeSigmaFactor` | `float` | 值域 sigma 无量纲系数 |
| `levelScale` | `float` | 尺度递增因子 |
| `edgeSigmaFactor` | `float` | 融合阶段的边缘抑制参数 |
| `detailGain0` | `float` | 第 0 层细节增益 |
| `detailGain1` | `float` | 第 1 层细节增益 |
| `detailGain2` | `float` | 第 2 层细节增益 |
| `storeIntermediate` | `bool` | 是否把中间层写回数据集 |

### 3.5 `CAEMultiScaleResultMeta`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `sourceArrayName` | `std::string` | 输入字段名 |
| `association` | `CAEFieldAssociation` | 字段关联 |
| `numLevels` | `int` | 实际层数 |
| `inputComponents` | `int` | 输入字段分量数 |
| `smoothArrayNames` | `std::vector<std::string>` | 平滑层字段名列表 |
| `detailArrayNames` | `std::vector<std::string>` | 细节层字段名列表 |
| `baseArrayName` | `std::string` | 基础层字段名 |
| `fusedArrayName` | `std::string` | 融合输出字段名 |
| `computeWallMs` | `double` | 总墙钟时间 |
| `computeGpuMs` | `double` | GPU 纯计算时间 |

### 3.6 `CAEDatasetSummary`

定义位置：[CAEInterfaceTypes.h](../CAEInterfaceTypes.h)

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `datasetId` | `std::string` | 数据集 ID |
| `displayName` | `std::string` | 显示名称 |
| `gridClass` | `CAEGridClass` | 规则/非结构化类别 |
| `pointCount` | `std::size_t` | 点数 |
| `cellCount` | `std::size_t` | 单元数 |
| `fields` | `std::vector<CAEFieldInfo>` | 当前所有字段信息 |
| `results` | `std::vector<CAEGradientResultMeta>` | 当前已生成梯度结果的摘要 |

## 4. `CAEProcessingFacade` 公共接口

定义位置：[CAEProcessingFacade.h](../CAEProcessingFacade.h)

### 4.1 生命周期与初始化

| 接口 | 说明 |
| --- | --- |
| `CAEProcessingFacade()` | 构造门面对象 |
| `~CAEProcessingFacade()` | 析构门面对象 |
| `bool initialize(const std::string& shaderDir)` | 初始化独立 OpenGL 上下文、设置着色器目录并初始化 GPU 引擎 |

调用约束：

1. 计算前必须先成功调用 `initialize()`。
2. `shaderDir` 应指向包含 `FD.glsl`、`ShapePointGradient.glsl`、`Bilateral.glsl` 等文件的目录。

### 4.2 数据集管理

| 接口 | 说明 |
| --- | --- |
| `void setAnalyticBenchmarkEnabled(bool enabled)` | 是否在加载数据时追加解析 benchmark 字段 |
| `bool isAnalyticBenchmarkEnabled() const` | 查询是否启用解析 benchmark |
| `std::string loadDatasetFromVTKFile(const std::string& filePath)` | 读取 legacy VTK 文件并返回数据集 ID |
| `std::vector<CAEDatasetSummary> listDatasets() const` | 列出当前所有已加载数据集 |
| `bool getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const` | 获取指定数据集摘要 |
| `bool listFields(const std::string& datasetId, CAEFieldAssociation assoc, std::vector<CAEFieldInfo>& outFields) const` | 枚举某个关联类型下的字段 |

说明：

- `loadDatasetFromVTKFile()` 内部使用 `vtkDataSetReader` 和 [VTKDataConverter.h](../VTKDataConverter.h)。
- 当前 GUI 与测试程序默认都读取 legacy `.vtk`。

### 4.3 梯度计算

| 接口 | 说明 |
| --- | --- |
| `bool computeGradient(const CAEGradientRequest& req, CAEGradientResultMeta& outMeta)` | 执行梯度计算并将结果写回内部数据集 |

行为特征：

1. 当 `method == Auto` 时，规则网格自动走 `FiniteDifference`，非结构化网格自动走 `ShapeFunctionDerivatives`。
2. 输出结果会以新字段形式写回数据集。
3. 输出分量数固定为 `3 * 输入分量数`。

### 4.4 多尺度数据优化

| 接口 | 说明 |
| --- | --- |
| `bool computeMultiScaleDecompositionAndFusion(const CAEMultiScaleRequest& req, CAEMultiScaleResultMeta& outMeta)` | 执行多尺度分解与融合，并将结果写回内部数据集 |

### 4.5 导出与数组访问

| 接口 | 说明 |
| --- | --- |
| `bool exportDatasetToVTK(const std::string& datasetId, vtkSmartPointer<vtkDataSet>& outVtk) const` | 导出为 `vtkDataSet` |
| `bool saveDatasetToVTKFile(const std::string& datasetId, const std::string& filePath, bool binary = true) const` | 保存为 legacy VTK 文件 |
| `bool getArrayData(const std::string& datasetId, const std::string& arrayName, CAEFieldAssociation assoc, std::vector<float>& outData, int& outComps) const` | 读取字段扁平数组 |
| `bool upsertArrayData(const std::string& datasetId, const std::string& arrayName, CAEFieldAssociation assoc, const std::vector<float>& data, int numComponents)` | 写入或覆盖字段 |

### 4.6 计时信息

| 接口 | 说明 |
| --- | --- |
| `double getLastComputeWallMs() const` | 最近一次计算的墙钟时间 |
| `double getLastComputeGpuMs() const` | 最近一次计算的 GPU 纯计算时间 |

## 5. 结果命名规则

命名逻辑定义在 [CAEProcessingFacade.cpp](../CAEProcessingFacade.cpp)。

### 5.1 梯度结果

格式：

`<源字段名>_grad_<P|C>_<FD|AWLS|SFD>`

示例：

- `pressure_grad_P_FD`
- `stress_grad_P_SFD`
- `U_grad_C_AWLS`

其中：

- `P` 表示点字段结果；
- `C` 表示单元字段结果；
- `SFD` 表示 Shape Function Derivatives。

### 5.2 多尺度结果

格式：

- 平滑层：`<src>_ms_s<level>_<P|C>`
- 细节层：`<src>_ms_d<level>_<P|C>`
- 基础层：`<src>_ms_base_<P|C>`
- 融合层：`<src>_ms_fused_<P|C>`

## 6. 测试程序命令行入口

### 6.1 `opengldp_benchmark`

源码位置：[TestGradient.cpp](../TestGradient.cpp)

作用：

1. 梯度计算正确性验证
2. 与 `vtkGradientFilter` 的结果对照
3. 时间统计与 CSV 输出

核心模式：

- `--run=single`
- `--run=benchmarks`
- `--run=fields`

核心选项：

- `--reference=auto|analytic|vtk|none`
- `--method=auto|fd|awls|shape`
- `--csv=<path>`

### 6.2 `opengldp_multiscale_test`

源码位置：[TestMultiScale.cpp](../TestMultiScale.cpp)

作用：

1. 合成场 + 噪声注入实验
2. 真实字段多尺度优化实验
3. VTK 导出与 ParaView 对照

核心模式：

- `--run=synthetic`
- `--run=fields`

### 6.3 `opengldp_field_metrics`

源码位置：[TestFieldMetrics.cpp](../TestFieldMetrics.cpp)

作用：

1. 对已有参考场与结果场做离线误差评价
2. 适合独立比较插值、优化或梯度后处理结果

## 7. VTK 的角色说明

从 API 角度必须讲清楚：

1. 系统自己的梯度计算和多尺度计算由 OpenGL 引擎执行。
2. 系统仍通过 VTK 完成输入、输出和可视化。
3. 测试程序在需要工程对照时，会调用 `vtkGradientFilter` 作为参考实现。

因此，若论文中写“完全不依赖 VTK”，该说法只适用于“梯度计算内核路径”，不适用于整个软件系统。

## 8. 相关补充文档

- [系统设计文档](./系统设计文档.md)
- [用户使用说明](./用户使用说明.md)
- [实验方案文档](./实验方案文档.md)
- [AI交接文档](./AI交接文档.md)

