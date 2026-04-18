# OpenGLDP API 文档

## 1. 适用范围

这份文档面向“怎么调用当前系统”的场景，只保留当前代码里仍然有效的接口。

当前梯度模块已经收敛为两条公开算法线：

- 规则网格：`FD`
- 非结构网格：`AWLS`

历史上的 `WeightedLeastSquares`、局部稀疏梯度算子、`vtkCell::Derivatives(...)` 默认主线，都不再是当前公开 API 的组成部分。

## 2. 头文件与核心类型

核心接口定义在 `CAEInterfaceTypes.h` 和 `CAEProcessingFacade.h`。

### 2.1 `CAEFieldAssociation`

表示数组挂在什么实体上：

- `Point`
- `Cell`

### 2.2 `CAEGridClass`

表示数据集被系统归到哪一类：

- `Regular`
- `Unstructured`

### 2.3 `CAEGradientMethod`

当前只保留三种取值：

- `Auto`
- `FiniteDifference`
- `AdaptiveWeightedLeastSquares`

其中 `Auto` 的行为是：

- 规则网格自动转成 `FiniteDifference`
- 非结构网格自动转成 `AdaptiveWeightedLeastSquares`

### 2.4 `CAEGradientRequest`

梯度计算请求结构。最关键的字段如下：

| 字段 | 作用 |
| --- | --- |
| `datasetId` | 数据集 id |
| `inputArrayName` | 输入数组名 |
| `association` | 点数据还是单元数据 |
| `method` | `Auto / FiniteDifference / AdaptiveWeightedLeastSquares` |
| `wlsExponent` | 距离权重指数 |
| `wlsLambda` | 正则系数 |
| `useAdaptiveNeighborhood` | 是否允许支撑扩邻域 |
| `useAdaptiveDimension` | 是否启用局部维数识别 |
| `useAdaptiveRegularization` | 是否启用自适应正则 |
| `minNeighbors / targetNeighbors / maxNeighbors` | 邻域规模控制 |
| `radiusScale` | KNN 补邻域时的局部半径尺度 |
| `planeEigenRatio / lineEigenRatio` | 维数判别阈值 |
| `lambdaAmplify` | 低质量邻域时的正则放大倍率 |

### 2.5 `CAEGradientResultMeta`

返回一次梯度计算的元信息：

- 输出数组名
- 源数组名
- 关联类型
- 实际采用的方法
- 输入/输出分量数
- CPU 墙钟时间
- GPU 计时结果

## 3. `CAEProcessingFacade`

这是当前系统对外最重要的统一入口。

### 3.1 初始化

```cpp
CAEProcessingFacade facade;
facade.initialize(shaderDir);
```

### 3.2 解析场开关

```cpp
facade.setAnalyticBenchmarkEnabled(enabled);
```

这个开关现在只建议测试程序使用。

当前行为是：

- 默认关闭
- GUI 不主动开启
- `TestGradient.cpp` 里只有显式传 `--analytic-bench` 才会开启

### 3.3 数据集加载

```cpp
std::string id = facade.loadDatasetFromVTKFile(path);
```

### 3.4 数据集与字段查询

```cpp
auto datasets = facade.listDatasets();
facade.getDatasetSummary(id, summary);
facade.listFields(id, assoc, fields);
```

### 3.5 梯度计算

```cpp
CAEGradientRequest req;
req.datasetId = id;
req.inputArrayName = "RF";
req.association = CAEFieldAssociation::Point;
req.method = CAEGradientMethod::Auto;

CAEGradientResultMeta meta;
bool ok = facade.computeGradient(req, meta);
```

当前等价的内部调度是：

```text
RegularGrid      -> computeByFD(...)
UnstructuredGrid -> computeByAdaptiveWLS(...)
```

### 3.6 多尺度分解与融合

```cpp
CAEMultiScaleRequest req;
CAEMultiScaleResultMeta meta;
bool ok = facade.computeMultiScaleDecompositionAndFusion(req, meta);
```

### 3.7 导出与数组读取

```cpp
facade.exportDatasetToVTK(id, vtkOut);
facade.saveDatasetToVTKFile(id, filePath);
facade.getArrayData(id, arrayName, assoc, data, comps);
```

## 4. `GLGradientEngine`

这个类负责真正的 GPU 梯度计算。

### 4.1 规则网格

```cpp
computeRegularFD(...)
```

对应着色器：

- `Shaders/FD.glsl`

### 4.2 非结构网格主线

```cpp
computeUnstructuredAdaptiveWLS(...)
```

对应着色器：

- `Shaders/AdaptiveWLS.glsl`

### 4.3 非结构网格内部回退

```cpp
computeUnstructuredWLS(...)
```

对应着色器：

- `Shaders/WLS.glsl`

这条线现在是内部回退内核，不是 GUI 里单独暴露的公开算法模式。

## 5. 当前支持的数据集类型

输入侧目前按 `VTKDataConverter` 的逻辑支持：

- `vtkImageData`
- `vtkRectilinearGrid`
- `vtkStructuredGrid`
- `vtkUnstructuredGrid`

系统内部映射为：

- 前三者 -> `Regular`
- 后者 -> `Unstructured`

## 6. 推荐调用方式

如果你不是在做专门的参数实验，建议直接用：

```cpp
req.method = CAEGradientMethod::Auto;
```

因为当前 `Auto` 已经代表了这版代码的最终公开方案：

- 规则网格自动 FD
- 非结构网格自动 AWLS

## 7. 与论文验证相关的建议

如果你要做毕设定稿验证，建议按下面顺序使用接口：

1. 先用 `TestGradient` 并显式开启 `--analytic-bench`，验证解析场 exact gradient。
2. 再用真实数据和 `vtkGradientFilter` 做工程对照。
3. 论文里把解析场结果作为主要正确性证据，把真实数据对 VTK 的差异作为工程对照和局限分析。
