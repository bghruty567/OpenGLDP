# TestGradient VTK 并行说明

## 背景

旧版 `TestGradient.cpp` 虽然统计了 `vtk_parallel_*` 时间，但默认沿用了当前 VTK backend。
如果本地 VTK 的默认 `VTK_SMP_IMPLEMENTATION_TYPE` 是 `Sequential`，那么：

- `vtk_parallel_threads` 仍然会显示为 `1`
- `vtk_parallel_avg_ms` 实际上不是多线程结果

也就是说，旧版程序记录了“并行计时框架”，但没有真正切到 VTK 并行 backend。

## 新增参数

现在程序新增：

```text
--vtk-backend=auto|current|sequential|stdthread|openmp|tbb
```

含义如下：

- `auto`
  优先尝试 `TBB -> OpenMP -> STDThread -> Sequential`
- `current`
  直接使用当前 VTK 默认 backend
- `sequential`
  强制使用 `Sequential`
- `stdthread`
  强制使用 `STDThread`
- `openmp`
  强制使用 `OpenMP`
- `tbb`
  强制使用 `TBB`

如果请求的 backend 不可用，程序会输出提示，并回退到当前 backend。

## 推荐用法

如果要测 VTK 真正的并行时间，建议显式指定：

```text
opengldp_benchmark --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --reference=vtk --vtk-backend=stdthread --vtk-parallel-threads=8
```

如果只想让程序自动选择可用的并行 backend，可用：

```text
opengldp_benchmark --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --reference=vtk --vtk-backend=auto
```

## 输出解释

1. 当 `reference=vtk` 时

控制台 `Ambient` / `Intrinsic` 比较块中的以下字段表示参考 VTK 的真实并行信息：

- `vtk_backend`
- `vtk_single_threads`
- `vtk_parallel_threads`
- `vtk_single_avg_ms`
- `vtk_parallel_avg_ms`

2. 当 `result=vtk` 时

控制台会额外输出一行：

```text
ResultVTK vtk_backend=... vtk_single_threads=... vtk_parallel_threads=...
```

用于说明当前被评估结果本身使用了哪个 backend，以及单线程/并行时间分别是多少。

3. CSV

CSV 新增 `vtk_backend_mode` 列，用于记录本次实验请求的 backend 模式。
