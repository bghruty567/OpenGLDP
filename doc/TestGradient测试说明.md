# TestGradient 测试说明

## 1. 程序目标

`TestGradient.cpp` 对应的可执行程序是 `opengldp_benchmark`。它的目标不是单纯“跑一下梯度”，而是统一承担以下任务：

- 梯度正确性验证；
- VTK 对照测试；
- 计时统计；
- 批量实验；
- CSV 汇总导出。

## 2. 当前设计要点

新版测试程序围绕 `CAEProcessingFacade` 重构，和 GUI 共用同一条数据加载、字段查询和梯度计算路径。这样做的好处是：

1. GUI 与测试程序不再各走一套逻辑；
2. benchmark 与真实数据都能走同一条算法主路径；
3. 结果更适合作为论文或报告中的正式实验依据。

## 3. 运行模式

当前支持三种模式：

### 3.1 `single`

单案例模式，用于调试某个字段：

```text
opengldp_benchmark --dataset=Data\AngularSector.vtk --assoc=cell --array=benchmark_quadratic --reference=analytic --dump=20
```

### 3.2 `benchmarks`

批量跑解析 benchmark：

```text
opengldp_benchmark --dataset=Data\AngularSector.vtk --assoc=cell --run=benchmarks --reference=analytic --csv=results\angular_cell_bench.csv
```

### 3.3 `fields`

批量跑当前数据集中已有字段：

```text
opengldp_benchmark --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --reference=vtk --analytic-bench=off --csv=results\ship_point_fields.csv
```

## 4. 核心参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--dataset` | `Data\ShipHull_0.vtk` | 输入数据集 |
| `--assoc` | `point` | 选择点数据或单元数据 |
| `--array` | 空 | 单案例模式下指定字段 |
| `--reps` | `5` | 重复次数 |
| `--reference` | `auto` | 参考优先级：解析梯度优先，缺失时退到 VTK |
| `--analytic-bench` | `on` | 是否注入解析 benchmark |
| `--dump` | `20` | 单案例打印样本数 |
| `--run` | `single` | 运行模式 |
| `--filter` | 空 | 仅保留名字包含指定文本的字段 |
| `--csv` | 空 | CSV 导出路径 |

## 5. benchmark 字段

benchmark 会同时注入到：

- 点数据；
- 单元数据。

### 5.1 标量字段

| 字段名 | 特点 | 精确梯度字段 |
| --- | --- | --- |
| `benchmark_linear` | 线性场，适合做线性精确性检查 | `benchmark_linear_exact_grad` |
| `benchmark_quadratic` | 二次场，适合看一般平滑精度 | `benchmark_quadratic_exact_grad` |
| `benchmark_trig` | 三角场，适合看振荡与方向变化 | `benchmark_trig_exact_grad` |
| `benchmark_cubic` | 三次场，适合看更高阶曲率 | `benchmark_cubic_exact_grad` |
| `benchmark_gaussian` | 局部尖峰型平滑场 | `benchmark_gaussian_exact_grad` |

### 5.2 向量字段

| 字段名 | 特点 | 精确梯度字段 |
| --- | --- | --- |
| `benchmark_vec_linear` | 线性向量场 | `benchmark_vec_linear_exact_grad` |
| `benchmark_vec_poly` | 多项式向量场 | `benchmark_vec_poly_exact_grad` |
| `benchmark_vec_trig` | 三角向量场 | `benchmark_vec_trig_exact_grad` |

## 6. 参考来源

程序支持四种参考模式：

- `analytic`  
  只使用 `*_exact_grad`。
- `vtk`  
  只使用 `vtkGradientFilter`。
- `auto`  
  优先解析梯度，缺失时退回到 VTK。
- `none`  
  不做对照，只计算和计时。

建议：

- 做算法验证时用 `analytic`；
- 做真实数据工程对照时用 `vtk`；
- 做批量日常测试时用 `auto`。

## 7. 输出指标

### 7.1 绝对误差

- `VecErr_MAE_abs`
- `VecErr_RMSE_abs`
- `VecErr_MAX_abs`

用于衡量梯度向量与参考值之间的直接差异。

### 7.2 归一化误差

- `NMAE_vec`
- `NRMSE_vec`

适合跨数据集比较。

### 7.3 稳健相对误差

- `SoftRel_tau`
- `SoftRel_mean`
- `SoftRel_median`
- `SoftRel_P90`

这组指标的目的是降低参考梯度很小时相对误差被过分放大的问题。

### 7.4 几何方向指标

- `Angle_mean_deg`
- `Angle_P90_deg`

用于衡量方向偏差。

### 7.5 整体幅值指标

- `ScaleBias`

反映结果整体偏大还是偏小。

## 8. CSV 输出

如果指定 `--csv=<path>`，程序会输出一张汇总表，包含：

- 数据集；
- 点/单元关联；
- 字段名；
- 输入/输出分量数；
- OpenGL 计时；
- 参考来源；
- 误差指标；
- 参考计时。

这张表适合：

- 做论文实验汇总；
- 后续用 Excel / Python 再做统计；
- 记录不同参数组的结果。

## 9. 推荐实验模板

### 9.1 模板 A：线性精确性

```text
opengldp_benchmark --dataset=Data\AngularSector.vtk --assoc=cell --array=benchmark_linear --reference=analytic
```

目的：

- 检查算法是否至少具备线性场正确重构能力。

### 9.2 模板 B：解析 benchmark 套件

```text
opengldp_benchmark --dataset=Data\AngularSector.vtk --assoc=cell --run=benchmarks --reference=analytic --csv=results\benchmarks.csv
```

目的：

- 一次性评估多种平滑场和向量场。

### 9.3 模板 C：真实数据与 VTK 对照

```text
opengldp_benchmark --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --reference=vtk --analytic-bench=off --csv=results\ship_vs_vtk.csv
```

目的：

- 做工程基线比较，而不是数学真值验证。

### 9.4 模板 D：局部排查

```text
opengldp_benchmark --dataset=Data\AngularSector.vtk --assoc=cell --array=benchmark_cubic --reference=analytic --dump=30
```

目的：

- 直接看样本级输出和局部误差表现。

## 10. 结果解释建议

如果解析 benchmark 的结果稳定、误差可接受，而真实数据相对 VTK 差距较大，优先考虑以下解释：

1. 真实数据缺少解析真值；
2. VTK 只是参考实现；
3. 非结构网格局部质量和边界处理会显著影响结果；
4. 某些字段本身不平滑，局部差异会被放大。

也就是说：

- 解析 benchmark 更能说明“算法本身是否正确”；
- 真实数据对 VTK 更能说明“工程上与参考基线差多少”。

## 11. 与代码的对应关系

| 功能 | 位置 |
| --- | --- |
| 命令行解析 | `TestGradient.cpp` |
| benchmark 注入 | `CAEProcessingFacade.cpp` |
| 数据集摘要与字段枚举 | `CAEProcessingFacade` |
| 梯度计算 | `CAEProcessingFacade::computeGradient(...)` |
| VTK 对照 | `vtkGradientFilter` |
| CSV 导出 | `TestGradient.cpp` |

## 12. 相关文档

- [系统原理与理论基础](./系统原理与理论基础.md)
- [系统使用说明](./系统使用说明.md)
- [文献支撑](./文献支撑.md)
- [测试程序指标详解](./测试程序指标详解.md)
