# OpenGLDP 项目详细 API 文档

> 说明：本文件面向“使用与二次开发”场景，按模块罗列主要类型与函数，并对其职责、参数与典型用法进行说明。

---

## 1 CAEInterfaceTypes.h —— 对外接口数据结构

### 1.1 枚举类型

#### `enum class CAEFieldAssociation`

- `Point`：字段定义在网格点上（Point Data）；
- `Cell`：字段定义在单元上（Cell Data）。

**用途：** 描述某个物理场或结果数组的关联对象，在梯度计算、字段枚举等 API 中使用。

#### `enum class CAEGridClass`

- `Regular`：规则网格（结构化、Rectilinear、Image 等）；
- `Unstructured`：非结构化网格。

**用途：** 描述数据集整体拓扑类别，便于自动选择梯度算法等。

#### `enum class CAEGradientMethod`

- `Auto`：自动选择（规则网格 => FD，非结构化 => WLS）；
- `FiniteDifference`：显式选择有限差分；
- `WeightedLeastSquares`：显式选择加权最小二乘。

**用途：** 在梯度计算请求中指定算法策略。

### 1.2 结构体类型

#### `struct CAEFieldInfo`

字段信息描述：

- `std::string name`：数组名称；
- `CAEFieldAssociation association`：点/单元；
- `int numComponents`：分量数（1 标量、3 向量等）；
- `std::size_t tupleCount`：元组数（点或单元的数量）。

**用途：** 用于 UI 显示字段列表、用于构造梯度计算请求。

#### `struct CAEGradientRequest`

梯度计算请求参数：

- `std::string datasetId`：目标数据集 id（由 Facade 返回）；
- `std::string inputArrayName`：输入场数组名；
- `CAEFieldAssociation association`：该数组是点数据还是单元数据；
- `CAEGradientMethod method`：梯度计算方法（可设为 `Auto`）；
- `float wlsExponent`：WLS 距离权重指数；
- `float wlsLambda`：WLS 正则化参数。

**用途：** 由上层（如 Qt UI）构造，传入 `CAEProcessingFacade::computeGradient`。

#### `struct CAEGradientResultMeta`

梯度计算结果元信息：

- `std::string resultArrayName`：输出梯度数组名；
- `std::string sourceArrayName`：来源场数组名；
- `CAEFieldAssociation association`：点/单元；
- `CAEGradientMethod method`：实际使用的方法；
- `int inputComponents`：输入分量数；
- `int outputComponents`：输出分量数（通常为 `3 * inputComponents`）；
- `double computeWallMs`：总 wall time（CPU 视角）；
- `double computeGpuMs`：GPU 计算时间（计时查询）。

#### `struct CAEDatasetSummary`

数据集摘要信息：

- `std::string datasetId`：数据集 id；
- `std::string displayName`：供显示用的名称（通常为文件名）；
- `CAEGridClass gridClass`：网格类型；
- `std::size_t pointCount`：点数；
- `std::size_t cellCount`：单元数；
- `std::vector<CAEFieldInfo> fields`：所有字段信息；
- `std::vector<CAEGradientResultMeta> results`：已计算梯度结果列表。

**用途：** 上层 UI 用于展示数据集结构和历史计算记录。

---

## 2 DataObject.h/.cpp —— 内部数据模型

### 2.1 枚举与结构

#### `enum DataArrayType`

- `POINT_DATA`：点数据；
- `CELL_DATA`：单元数据。

**用途：** 区分 `DataArray` 类型，便于查找与导出。

#### `struct DataArray`

- `std::string name`：字段名；
- `std::vector<float> data`：平铺数组，大小为 `tupleCount * numComponents`；
- `int numComponents`：每个元组的分量数；
- `DataArrayType dataType`：点/单元数据。

**用途：** 表示任意标量/向量/张量场，可用于存放原始场、梯度场、模长等。

#### `enum GridType`

- `DATA_OBJECT_TYPE_RegularGrid`：规则网格；
- `DATA_OBJECT_TYPE_UNSTRUCTURED`：非结构化网格。

### 2.2 类 `DataObject`

#### 公有成员变量

- `GridType gridType`：网格类型；
- `std::vector<float> points`：点坐标 `[x0,y0,z0,x1,y1,z1,...]`；
- `std::vector<float> cellCenters`：单元中心坐标；
- `std::vector<DataArray> dataArrays`：所有数据数组；
- `std::vector<int> pointNeighbors` / `pointNeighborOffsets`：
  - 点–点邻接（CSR 形式：offset 表示每个点邻接范围起止）；
- `std::vector<int> pointInCellNeighbors` / `pointInCellNeighborOffsets`：
  - 点–单元邻接；
- `std::vector<int> cells` / `cellTypes` / `cellOffsets`：
  - 单元连接（CSR 风格）；
- `std::vector<int> cellNeighbors` / `cellNeighborsOffsets`：
  - 单元–单元邻接；
- `int dimensions[3]`：
  - 规则网格维度 `(nx, ny, nz)`。

#### 公有成员函数

##### `DataArray* findDataArray(const std::string& name, DataArrayType type);`

- **功能：** 按名称和点/单元类型查找数组，可修改返回结果；
- **返回：** 若找到则返回指针，否则 `nullptr`。

##### `const DataArray* findDataArray(const std::string& name, DataArrayType type) const;`

- **功能：** const 版本，用于只读场景；
- **返回：** 同上。

##### `bool upsertDataArray(const std::string& name, const std::vector<float>& data, int numComponents, DataArrayType type);`

- **功能：** 若存在同名同类型数组，则更新数据与分量数；否则插入新数组；
- **返回：** 成功返回 `true`，若 `numComponents <= 0` 则返回 `false`。

##### `size_t pointCount() const;`

- **功能：** 返回点数量，等于 `points.size() / 3`。

##### `size_t cellCount() const;`

- **功能：** 返回单元数量，等于 `cellOffsets.size() - 1`（不足 2 时为 0）。

**备注：** 所有高层算法（梯度、滤波等）都基于 `DataObject` 进行计算和存储结果。

---

## 3 VTKDataConverter.h/.cpp —— VTK 与内部数据转换

### 3.1 成员变量

- `vtkDataSet* vtkData`：当前绑定的 VTK 数据集；
- `DataObject* internalData`：当前绑定的内部数据对象。

### 3.2 公有接口

#### `VTKDataConverter();` / `~VTKDataConverter();`

- 默认构造/析构，无特别逻辑。

#### `void bindVTKDataAndInternalData(vtkDataSet* vtkData, DataObject* internalData);`

- **功能：** 绑定一对 VTK 数据集与内部数据对象，为后续转换准备上下文。

#### `int convertVTKToInternal();`

- **功能：** 将绑定的 `vtkData` 转换为 `internalData`。
- **流程：**
  1. 检查指针合法性；
  2. 调用 `convertType()` 识别网格类型；
  3. 根据 `gridType` 调用 `convertRegularGrid()` 或 `convertUnstructuredGrid()`。
- **返回：** 成功返回 1，失败返回 0。

#### `int convertInternalToVTK();`

- **功能：** 将 `internalData` 中的信息重建为新的 `vtkDataSet`，并赋值给 `vtkData`。
- **返回：** 成功返回 1，失败返回 0。

### 3.3 转换子函数（规则网格/非结构化网格）

#### `int convertType();`

- **功能：** 根据 `vtkData` 实际类型设置 `internalData->gridType`：
  - `vtkUnstructuredGrid` => 非结构化；
  - `vtkStructuredGrid` / `vtkRectilinearGrid` / `vtkImageData` => 规则网格；
  - 其他类型 => 报错并返回 0。

#### `int convertPoints();`

- **功能：** 将 `vtkData->GetPoints()` 拷贝为 `internalData->points`。

#### `int convertDataArrays();`

- **功能：** 遍历 VTK 的 `PointData` 和 `CellData`，构造对应的 `DataArray`；
- **结果：** 所有数组存入 `internalData->dataArrays`。

#### `int convertPointInCellNeighbors();`

- **功能：** 使用 `vtkStaticCellLinks` 构建点–单元邻接表，填充：
  - `pointInCellNeighbors` / `pointInCellNeighborOffsets`。

#### `int convertPointNeighbors();`

- **功能：** 基于拓扑（共单元）构建点–点邻接（去重后）；
- **备注：** 当前 `convertUnstructuredGrid()` 中采用了更鲁棒的方法 `convertPointNeighborsRobust`，该函数保留用于测试对比。

#### `int convertPointNeighborsByKNN(int k);`

- **功能：** 使用 `vtkKdTreePointLocator` 为每个点找到 K 近邻，构建点–点邻接；
- **用途：** 当纯拓扑邻接不足时的 KNN 近邻方案。

#### `int convertPointNeighborsRobust(int minK, int knnK);`

- **功能：** 首先根据拓扑构建邻接；若某点邻居数 < `minK`，则用 KNN（`knnK`）补充；
- **用途：** 提高非结构化网格中邻接构建的稳定性，避免孤立点。

#### `int convertCell();`

- **功能：** 遍历所有 VTK 单元，将：
  - 点 id 写入 `cells`；
  - 单元类型写入 `cellTypes`；
  - 单元起止偏移写入 `cellOffsets`。

#### `int convertDimensions();`

- **功能：** 对规则网格读取维度信息，填入 `internalData->dimensions[3]`。

#### `int convertCellCenters();`

- **功能：** 对每个单元，计算其几何中心（通过 `GetParametricCenter` + `EvaluateLocation`），写入 `cellCenters`。

#### `int convertRegularGrid();`

- **功能：** 规则网格转换流程封装，调用：
  - `convertPoints` / `convertDimensions` / `convertDataArrays` / `convertCellCenters` / `convertCell`。

#### `int convertUnstructuredGrid();`

- **功能：** 非结构化网格转换流程封装，调用：
  - `convertPoints` / `convertDataArrays` / `convertPointInCellNeighbors` /
  - `convertPointNeighborsRobust` / `convertCellCenters` / `convertCell` /
  - `convertCellNeighborsByKNN`。

#### `int convertCellNeighbors();`

- **功能：** 基于 `vtkStaticCellLinks` 构建“共点单元邻接”，填充 `cellNeighbors` / `cellNeighborsOffsets`。

#### `int convertCellNeighborsByKNN(int k);`

- **功能：** 以单元中心为点，构建 KNN 邻接，填充 `cellNeighbors` / `cellNeighborsOffsets`。

---

## 4 GLGradientEngine.h/.cpp —— GPU 梯度计算引擎

### 4.1 内部参数结构

#### `struct RegularParams`

- `int dims[3]`：规则网格尺寸 (nx, ny, nz)；
- `float origin[3]`：网格原点（当前实现中主要保留接口）；
- `float spacing[3]`：网格间距（同上）。

#### `struct WLSParams`

- `float wExponent`：权重函数指数；
- `float lambda`：正则化系数。

### 4.2 公有成员函数

#### `GLGradientEngine();` / `~GLGradientEngine();`

- 构造/析构；析构时调用 `release` 释放 OpenGL 资源。

#### `bool setShaderDir(const std::string& dir);`

- **功能：** 设置着色器目录，例如 `"Shaders"`；
- **备注：** `init()` 中会根据此目录加载 `FD.glsl` 与 `WLS.glsl`。

#### `bool init();`

- **功能：**
  - 检查 `shaderDir` 是否设置；
  - 调用 `buildComputeFromFile` 编译并链接两个 Compute Shader 程序；
  - 初始化成功返回 `true`。

#### `void release();`

- **功能：** 释放所有 OpenGL 程序与 SSBO 资源。

#### `bool computeRegularFD(const std::vector<float>& positions, const std::vector<float>& values, const RegularParams& p, std::vector<float>& outGrad);`

- **功能：** 在规则网格上执行有限差分梯度计算；
- **输入：**
  - `positions`：大小为 `N * 3` 的坐标数组；
  - `values`：大小为 `N * numComponents` 的场值数组；
  - `p`：网格参数（至少 `dims` 要正确）。
- **输出：**
  - `outGrad`：大小为 `N * 3 * numComponents` 的梯度结果；
- **返回：** 成功返回 `true`，否则 `false`（如维度不匹配、shader 未初始化等）。

#### `bool computeUnstructuredWLS(const std::vector<float>& positions, const std::vector<int>& offsets, const std::vector<int>& neighbors, const std::vector<float>& phi, const WLSParams& p, std::vector<float>& outGrad);`

- **功能：** 在非结构化网格上执行加权最小二乘梯度计算；
- **输入：**
  - `positions`：坐标（长度为 `N * 3`）；
  - `offsets` / `neighbors`：CSR 邻接结构；
  - `phi`：场值数组（长度为 `N * numComponents`）；
- **输出：**
  - `outGrad`：大小为 `N * 3 * numComponents` 的梯度结果。

#### `void setEnableGpuTiming(bool on);`

- **功能：** 打开或关闭 GPU 时间测量；
- **行为：**
  - 打开时会创建 `GL_TIME_ELAPSED` 查询对象；
  - 梯度计算时记录 GPU 执行时间。

#### `double getLastGpuTimeMs() const;`

- **功能：** 返回最近一次梯度计算的 GPU 时间（毫秒）。

---

## 5 OpenGLManager.h & gl_context_utils.h —— OpenGL 上下文管理

### 5.1 结构 `OpenGLRuntimeInfo`

- `std::string vendor`：GL_VENDOR；
- `std::string renderer`：GL_RENDERER；
- `std::string version`：GL_VERSION；
- `std::string glsl`：着色器语言版本；
- `int major` / `minor`：OpenGL 主/次版本号。

### 5.2 类 `OpenGLManager`

#### 成员函数

##### `bool initialize(bool offscreen = false);`

- **功能：** 创建一个持久化 OpenGL 上下文（基于 VTK `vtkOpenGLRenderWindow`），并加载 GLAD。
- **参数：**
  - `offscreen`：是否使用离屏渲染窗口。
- **返回：** 成功返回 `true`，失败时 `isReady()` 为 `false`。

##### `bool isReady() const;`

- **功能：** 查询当前 OpenGLManager 是否初始化成功且有可用窗口。

##### `void makeCurrent();`

- **功能：** 将内部保存的 `vtkOpenGLRenderWindow` 设为当前 OpenGL 上下文。

##### `const OpenGLRuntimeInfo& info() const;`

- **功能：** 返回 GPU 运行时信息（厂商、版本等）。

##### `vtkSmartPointer<vtkOpenGLRenderWindow> window() const;`

- **功能：** 返回内部的 VTK OpenGL 渲染窗口。

### 5.3 函数 `createPersistentGLContext(bool offscreen = true)`

所在文件：`gl_context_utils.h`。

- **功能：**
  - 创建 VTK 渲染窗口与渲染器；
  - 支持离屏渲染；
  - 调用 `gladLoadGL()` 加载 OpenGL 函数；
  - 输出当前 GPU/GL 信息；
  - 检查 OpenGL 版本是否 >= 4.3。
- **返回：**
  - 成功：`vtkSmartPointer<vtkOpenGLRenderWindow>`；
  - 失败：返回 `nullptr`。

---

## 6 CAEProcessingFacade.h/.cpp —— 统一业务门面

### 6.1 主要职责

- 负责协调：
  - OpenGL 上下文；
  - GPU 梯度引擎；
  - VTK 与内部数据转换；
  - 数据集管理与结果缓存。
- 对外提供简单 API：
  - 初始化、加载数据集、列出字段、计算梯度、导出结果等；
  - UI 与测试程序无需关心底层细节。

### 6.2 公有成员函数

#### `CAEProcessingFacade();` / `~CAEProcessingFacade();`

- 构造/析构；

#### `bool initialize(const std::string& shaderDir);`

- **功能：**
  - 调用 `m_gl.initialize(false)` 创建 OpenGL 上下文；
  - 设置 `m_engine` 的着色器目录；
  - 初始化梯度引擎，并打开 GPU 计时。
- **返回：** 成功返回 `true`。

#### `std::string loadDatasetFromVTKFile(const std::string& filePath);`

- **功能：** 从 VTK Legacy 文件中加载数据集并转换为内部格式。
- **流程：**
  1. 使用 `vtkDataSetReader` 读取文件；
  2. 创建 `DatasetRecord`；
  3. 使用 `VTKDataConverter` 填充 `DataObject`；
  4. 分配 `datasetId`（形如 `ds_1`），放入内部 map。
- **返回：**
  - 成功：返回新建的 `datasetId`；
  - 失败：返回空字符串。

#### `std::vector<CAEDatasetSummary> listDatasets() const;`

- **功能：** 列出当前所有已加载数据集的摘要信息。

#### `bool getDatasetSummary(const std::string& datasetId, CAEDatasetSummary& outSummary) const;`

- **功能：** 获取指定数据集的摘要信息。

#### `bool listFields(const std::string& datasetId, CAEFieldAssociation assoc, std::vector<CAEFieldInfo>& outFields) const;`

- **功能：** 筛选指定点/单元关联下的字段列表，用于 UI 显示候选场。

#### `bool computeGradient(const CAEGradientRequest& req, CAEGradientResultMeta& outMeta);`

- **功能：** 根据请求执行梯度计算，是上层最常用的核心接口。
- **行为：**
  1. 根据 `datasetId` 找到 `DatasetRecord`；
  2. 根据 `inputArrayName` + `association` 查找 `DataArray`；
  3. 若 `req.method == Auto`：
     - 规则网格 => FD；
     - 非结构化网格 => WLS；
  4. 调用 `computeByFD` or `computeByWLS`；
  5. 使用 `upsertDataArray` 将结果写回到 `DataObject`；
  6. 填写 `CAEGradientResultMeta` 并追加到 `DatasetRecord::results`。
- **返回：** 成功返回 `true`。

#### `bool exportDatasetToVTK(const std::string& datasetId, vtkSmartPointer<vtkDataSet>& outVtk) const;`

- **功能：** 将内部数据集转换为 VTK 数据集，供可视化或导出使用。
- **返回：** 成功返回 `true`，并在 `outVtk` 中提供结果。

#### `bool getArrayData(const std::string& datasetId, const std::string& arrayName, CAEFieldAssociation assoc, std::vector<float>& outData, int& outComps) const;`

- **功能：** 获取指定数组的原始数据与分量数；
- **用途：**
  - 在测试代码中用于与 VTK 结果作数值对比；
  - 在 Qt 中可用于绘制统计图等。

#### `double getLastComputeWallMs() const;`

- **功能：** 返回最近一次 `computeGradient` 的 wall time（高精度时钟）。

#### `double getLastComputeGpuMs() const;`

- **功能：** 返回最近一次梯度计算的 GPU 时间（由 `GLGradientEngine` 提供）。

### 6.3 典型调用流程示例

1. 初始化：

```cpp
CAEProcessingFacade facade;
if (!facade.initialize("Shaders")) { /* 处理错误 */ }
```

2. 加载数据集：

```cpp
std::string dsId = facade.loadDatasetFromVTKFile("Data\\uGridEx.vtk");
```

3. 枚举字段 & 构造请求：

```cpp
std::vector<CAEFieldInfo> fields;
facade.listFields(dsId, CAEFieldAssociation::Point, fields);
// 选择一个字段名 fields[0].name 作为 inputArrayName
```

4. 梯度计算：

```cpp
CAEGradientRequest req;
req.datasetId = dsId;
req.inputArrayName = fields[0].name;
req.association = CAEFieldAssociation::Point;
req.method = CAEGradientMethod::Auto;

CAEGradientResultMeta meta;
facade.computeGradient(req, meta);
```

5. 导出 VTK 用于渲染：

```cpp
vtkSmartPointer<vtkDataSet> out;
facade.exportDatasetToVTK(dsId, out);
// 将 out 交给 VTK 渲染管线
```

---

## 7 TestGradient.cpp —— 示例与对比程序（非库 API）

虽然 `TestGradient` 不是库接口的一部分，但对理解整体调用顺序很有帮助：

- **主要功能：**
  - 从命令行解析：
    - VTK 文件路径；
    - 字段关联类型（point/cell）；
    - 字段名（可选）；
    - 重复次数 reps（用于计时统计）。
  - 使用 `CAEProcessingFacade` 完成 GPU 端梯度计算；
  - 使用 `vtkGradientFilter` 完成 CPU 端梯度计算；
  - 计算 MAE / RMSE / 最大误差；
  - 打印 OpenGL 与 VTK 的时间统计；
  - 将结果写入导出的 VTK 数据集，并用 VTK 窗口展示梯度模长的对比。

**使用方式：** 可作为后续 Qt UI 调用流程的样例，也可作为性能与精度对比的命令行工具。

---

## 8 使用与扩展建议

- 若仅作为“库”使用，建议只依赖以下文件中的接口：
  - `CAEInterfaceTypes.h`
  - `CAEProcessingFacade.h`
- 若需要对内部算法进行修改（如增加双边滤波、多尺度融合）：
  - 在 `DataObject` 中直接复用现有存储结构（点/单元数组 + 邻接 CSR）；
  - 在 `GLGradientEngine` 中增加新的 Compute Shader 管线；
  - 在 `CAEProcessingFacade` 中添加新的高层 API，复用数据集管理逻辑；
  - 在 Qt 层统一通过 Facade 调用，避免在界面层直接操作 VTK/GL。

通过以上 API 文档，你可以清晰地看到各模块的输入输出边界和责任划分，为后续论文撰写、系统扩展和 UI 开发提供基础说明。