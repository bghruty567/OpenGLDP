# TestMultiScale 测试说明

## 1. 程序定位

`TestMultiScale.cpp` 对应的可执行程序是 `opengldp_multiscale_test`。

它的目标不是只做一次简单滤波，而是把“数据构造/加载 -> 多尺度优化 -> 定量评估 -> 导出可视化文件”串成一套完整实验流程，方便你：

- 验证多尺度数据优化模块是否真的起作用；
- 观察不同噪声类型下的优化效果；
- 比较优化前后误差、粗糙度和标准差；
- 导出 `.vtk` 文件到 ParaView 中直接查看原始数据与优化结果。

## 2. 支持的两种运行模式

### 2.1 `synthetic`

合成场测试模式。

程序会先根据当前网格几何自动生成几类干净标量场，然后构造带噪数据，再跑多尺度优化，并把优化前后结果与干净真值做对比。

适合做：

- 算法有效性验证；
- 参数敏感性实验；
- 论文中的可控实验。

### 2.2 `fields`

真实字段测试模式。

程序直接对数据集原有字段做多尺度优化，重点看：

- 优化前后粗糙度变化；
- 优化前后标准差变化；
- 原始字段与优化后字段的差值大小；
- ParaView 中的实际视觉效果。

适合做：

- 工程数据观察；
- 展示“优化前/优化后”的对比图；
- 真实数据的案例分析。

## 3. 常用参数

### 3.1 通用参数

| 参数 | 说明 |
| --- | --- |
| `--dataset=<path>` | 输入 `.vtk` 数据集路径 |
| `--assoc=point|cell` | 选择点数据或单元数据 |
| `--run=synthetic|fields` | 选择运行模式 |
| `--array=<name>` | 只测试指定字段或指定合成场 |
| `--filter=<text>` | 只保留名字包含指定文本的字段 |
| `--reps=<n>` | 重复运行次数，用于稳定计时 |
| `--levels=<n>` | 多尺度层数，当前建议 `1~3` |
| `--iterations=<n>` | 每层迭代次数 |
| `--store-intermediate=on|off` | 是否保留中间层结果 |
| `--csv=<path>` | 导出 CSV 汇总表 |
| `--export=<path-or-dir>` | 导出 ParaView 用 `.vtk` 文件 |
| `--list-fields` | 列出当前数据集可用字段 |
| `--list-synthetic` | 列出当前几何下会生成的合成场 |
| `--show-config` | 打印解析后的运行配置 |

### 3.2 噪声参数

| 参数 | 说明 |
| --- | --- |
| `--noise=gaussian|impulse|mixed|all` | 噪声类型 |
| `--sigma-factor=<x>` | 高斯噪声强度，按信号标准差的比例设置 |
| `--impulse-ratio=<x>` | 脉冲噪声占比 |
| `--impulse-scale=<x>` | 脉冲噪声幅值，按信号标准差的比例设置 |
| `--seed=<n>` | 随机种子 |

### 3.3 多尺度融合参数

| 参数 | 说明 |
| --- | --- |
| `--spatial-sigma-factor=<x>` | 空间权重尺度 |
| `--range-sigma-factor=<x>` | 数值相似度尺度 |
| `--level-scale=<x>` | 层级放大倍数 |
| `--edge-sigma-factor=<x>` | 细节抑制相关参数 |
| `--detail-gain0=<x>` | 第 0 层细节权重 |
| `--detail-gain1=<x>` | 第 1 层细节权重 |
| `--detail-gain2=<x>` | 第 2 层细节权重 |

## 4. 导出 `.vtk` 的新规则

这是这次补强的重点。

### 4.1 单案例导出

如果当前只会跑一个 case，并且 `--export` 指向一个 `.vtk` 文件，那么程序会把这个 case 直接导出到该文件。

示例：

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --array=U --export=results\ship_u_ms.vtk
```

### 4.2 多案例导出

如果当前会跑多个 case，那么 `--export` 会被当作目录或文件名前缀来处理，程序会为每个 case 单独导出一个 `.vtk` 文件。

示例：

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --run=synthetic --noise=all --export=results\multiscale_vtk
```

此时会在 `results\multiscale_vtk\` 下得到一组按 case 分开的 `.vtk` 文件。

如果你写成：

```text
--export=results\ship_ms.vtk
```

并且存在多个 case，则程序会自动把它视作“文件名前缀”，生成类似：

```text
results\ship_ms__1__ms_clean_trig_gaussian.vtk
results\ship_ms__2__ms_clean_trig_impulse.vtk
...
```

### 4.3 导出的 `.vtk` 里包含什么

#### `synthetic` 模式

导出的单个 `.vtk` 文件会包含：

- 原始网格本体；
- 当前 case 的干净场；
- 当前 case 的带噪输入场；
- 当前 case 的优化后融合场；
- 如果 `--store-intermediate=on`，还会包含：
  - 基础层；
  - 各层平滑结果；
  - 各层细节结果。

#### `fields` 模式

导出的单个 `.vtk` 文件会包含：

- 原始数据集本身已有字段；
- 当前被优化的输入字段；
- 当前 case 的优化后融合场；
- 如果 `--store-intermediate=on`，同样会带上 base/smooth/detail 中间结果。

## 5. ParaView 中怎么查看

建议按下面的顺序看：

1. 打开导出的 `.vtk` 文件。
2. 在 `Color By` 中先选择输入字段，例如真实场或带噪场。
3. 观察噪声、锯齿、局部尖峰、色斑等现象。
4. 切换到 `*_ms_fused_*` 对应的融合结果字段。
5. 对比：
   - 是否更平滑；
   - 是否保留主要结构；
   - 是否出现过度平滑；
   - 边缘或高梯度区域是否被抹掉。
6. 如果保留了中间结果，可以继续切换到：
   - `*_ms_base_*`
   - `*_ms_s1_*`, `*_ms_s2_*`, `*_ms_s3_*`
   - `*_ms_d0_*`, `*_ms_d1_*`, `*_ms_d2_*`

这样能更直观看到多尺度分解和融合到底发生了什么。

## 6. 典型命令模板

### 6.1 查看可用字段

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --list-fields
```

### 6.2 查看可生成的合成场

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --list-synthetic
```

### 6.3 单个合成场 + 高斯噪声 + 导出一个 `.vtk`

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --run=synthetic --array=ms_clean_trig --noise=gaussian --export=results\trig_gaussian.vtk --csv=results\trig_gaussian.csv
```

### 6.4 全部合成场 + 全部噪声 + 批量导出

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --run=synthetic --noise=all --levels=3 --iterations=1 --export=results\multiscale_vtk --csv=results\multiscale_synth.csv
```

### 6.5 真实字段优化 + 导出 ParaView 文件

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --array=U --levels=3 --iterations=1 --export=results\ship_u_ms.vtk --csv=results\ship_u_ms.csv
```

### 6.6 批量跑真实字段

```text
opengldp_multiscale_test --dataset=Data\ShipHull_0.vtk --assoc=point --run=fields --filter=stress --export=results\real_fields_vtk --csv=results\real_fields_ms.csv
```

## 7. 控制台和 CSV 中的关键指标

### 7.1 统计量

- `clean_std`
- `input_std`
- `fused_std`

用来观察优化是否明显压制了波动。

### 7.2 图粗糙度

- `clean_roughness`
- `input_roughness`
- `fused_roughness`
- `roughness_ratio`

其中：

- `roughness_ratio < 1` 一般表示优化后比输入更平滑；
- 过小则可能提示过度平滑。

### 7.3 误差指标

只在 `synthetic` 模式下有真值，所以会输出：

- `input_mae / input_rmse / input_nrmse`
- `fused_mae / fused_rmse / fused_nrmse`
- `mae_improvement_ratio`
- `rmse_improvement_ratio`

其中：

- `mae_improvement_ratio < 1` 表示优化后比输入更接近真值；
- `rmse_improvement_ratio < 1` 同理；
- 越小越好，但不能只看误差，还要结合 ParaView 观察是否过度平滑。

## 8. 推荐使用顺序

建议你按下面顺序用：

1. 先用 `--list-fields` 或 `--list-synthetic` 确认输入对象。
2. 先跑单案例，确认导出 `.vtk` 和 ParaView 显示都正常。
3. 再批量跑多个 case，导出整个目录。
4. 用 CSV 做数值排序，用 ParaView 做可视化判断。
5. 最后再回头调参数。

## 9. 与其他文档的关系

- 梯度测试程序说明见：[TestGradient测试说明](./TestGradient测试说明.md)
- 如果要看系统整体使用方式，可再看：[系统使用说明](./系统使用说明.md)
