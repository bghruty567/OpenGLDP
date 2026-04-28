# TestFieldMetrics 测试说明

## 1. 程序定位

`TestFieldMetrics.cpp` 对应的可执行程序是 `opengldp_field_metrics`。

它的作用是直接比较已经准备好的字段结果，不负责重新运行数据优化模块。适合这类场景：

- 已经有 `u_exact`、`u_interp`、`u_interp_ms_fused`
- 想直接输出误差、粗糙度、改进率
- 后续还想复用到梯度重构误差、网格不规则性、真实字段对比实验

它现在也支持像 `TestGradient.cpp` 一样，在源码里直接设置默认路径和数组名，然后在 QtCreator 里直接点运行。

默认配置位置：

- [TestFieldMetrics.cpp](/C:/Users/lenovo/Desktop/bishe/myProj/OpenGLDP/TestFieldMetrics.cpp)
- `struct Options` 开头那几项默认字符串

## 2. 支持的两种输入方式

### 2.1 同一数据集内比较多个数组

当 `u_exact`、`u_interp`、`u_interp_ms_fused` 都在同一个 `.vtk` 文件里时：

```text
opengldp_field_metrics Data\interp_case.vtk point --reference=u_exact --input=u_interp --fused=u_interp_ms_fused --baseline-label=input --csv=results\interp_eval.csv
```

### 2.2 不同数据集之间比较对应数组

当三者分别保存在不同 `.vtk` 文件里时：

```text
opengldp_field_metrics --assoc=point --reference=Data\u_exact.vtk::u_exact --input=Data\u_interp.vtk::u_interp --fused=Data\u_interp_ms_fused.vtk::u_interp_ms_fused --baseline-label=input --csv=results\interp_eval.csv
```

格式说明：

- `<array>`：表示使用默认数据集中的某个数组
- `<dataset>::<array>`：表示显式指定数据集和数组

## 3. 常用参数

| 参数 | 说明 |
| --- | --- |
| `--dataset=<path>` | 默认数据集路径 |
| `--assoc=point|cell` | 点数据或单元数据 |
| `--reference=<spec>` | 真值参考字段 |
| `--input=<spec>` | 添加名为 `input` 的对比字段 |
| `--fused=<spec>` | 添加名为 `fused` 的对比字段 |
| `--series=<label>=<spec>` | 添加任意命名的对比字段 |
| `--baseline-label=<label>` | 以哪个序列作为改进率基线 |
| `--csv=<path>` | 导出 CSV |
| `--list-fields` | 列出当前数据集字段 |
| `--strict-geometry=on|off` | 是否强制要求样本坐标与参考场一致 |
| `--position-tol=<x>` | 几何一致性检查容差 |

## 4. 输出指标

每个对比字段会输出：

- 值统计：`mean`、`std`、`rms`、`min`、`max`
- 粗糙度：`roughness`
- 相对参考场误差：`mae`、`rmse`、`max_abs`、`nmae`、`nrmse`
- 相关性：`corr`
- 相对基线的比值：
  - `mae/base`
  - `rmse/base`
  - `rough/base`
- 相对基线的改善百分比：
  - `mae_reduction_pct`
  - `rmse_reduction_pct`
  - `roughness_reduction_pct`

其中：

- `baseline-label=input` 最适合“输入场 vs 优化后场”的实验
- 如果 `rmse/base < 1`，说明当前结果比基线更接近真值
- 如果 `rough/base < 1`，说明当前结果比基线更平滑

## 5. 推荐用于当前插值误差实验的命令

如果 `u_exact`、`u_interp`、`u_interp_ms_fused` 在同一个导出文件里：

```text
opengldp_field_metrics Data\your_interp_case.vtk point --reference=u_exact --input=u_interp --fused=u_interp_ms_fused --baseline-label=input --csv=results\interp_eval.csv
```

如果它们分别在三个文件里：

```text
opengldp_field_metrics --assoc=point --reference=Data\u_exact.vtk::u_exact --input=Data\u_interp.vtk::u_interp --fused=Data\u_interp_ms_fused.vtk::u_interp_ms_fused --baseline-label=input --csv=results\interp_eval.csv
```

## 6. 适用范围

这个程序不只适用于插值误差实验，也适用于：

- 梯度重构误差实验
- 非结构网格不规则性实验
- 真实字段优化前后对比
- 后续离散化误差和残差未充分收敛实验

核心要求只有两个：

1. 参考场和待比较场在样本数与分量数上能对齐
2. 最好来自同一网格或一一对应的重采样结果
