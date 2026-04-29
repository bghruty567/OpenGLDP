# 基于OpenGL的CAE软件数据预处理方法研究与实践

本文档为自动生成的论文源稿预览，正式排版版本见同目录 `.docx` 文件。

## 摘要
面向 CAE 后处理阶段中的数据预处理需求，本文实现了基于 OpenGL Compute Shader 的梯度计算与结果场优化系统，并通过解析场验证、真实字段与 VTK 对照以及多尺度优化实验进行了评估。

## 主要实验表格
### 表5-2 解析场主验证结果（点数据主线）
| 数据集 | 字段 | 采用指标 | NRMSE | 正文角色 |
| --- | --- | --- | --- | --- |
| SampleStructGrid | benchmark_linear | Ambient NRMSE | 2.820380e-07 | 规则网格标量主验证 |
| SampleStructGrid | benchmark_vec_linear | Ambient NRMSE | 3.302560e-07 | 规则网格向量主验证 |
| hexa | benchmark_linear | Ambient NRMSE | 1.579980e-07 | 体网格点梯度主验证 |
| 1_0 | benchmark_surface_linear | Intrinsic NRMSE | 1.680880e-07 | 曲面点梯度主验证 |

正文主展示口径收敛到四类代表性 benchmark：规则网格标量/向量线性场、体网格点线性场和曲面点切向线性场。更高阶或三角型 benchmark 保留在 CSV 与补充分析中，不作为主结论的唯一支撑。

### 表5-3 1_0 曲面点数据的 ambient / intrinsic 指标对比
| 字段 | Ambient NRMSE | Intrinsic NRMSE |
| --- | --- | --- |
| benchmark_linear | 0.808122 | 9.303870e-08 |
| benchmark_trig | 0.270756 | 0.018959 |
| benchmark_surface_linear | 1.680880e-07 | 1.680880e-07 |
| benchmark_surface_trig | 0.017943 | 0.017943 |

该表用于说明曲面型数据必须使用与几何维度一致的解析场和评价指标，否则会把法向项误差错误混入主结论。

### 表5-4 真实字段与 vtkGradientFilter 的一致性对比
| 数据集 | 关联 | 字段 | NRMSE | SoftRel Mean |
| --- | --- | --- | --- | --- |
| 规则网格 SampleStructGrid | POINT | scalars | 8.224370e-08 | 7.026850e-08 |
| 规则网格 SampleStructGrid | CELL | scalars | 7.627860e-07 | 6.622620e-07 |
| 六面体体网格 hexa | POINT | scalars | 6.272850e-08 | 5.160760e-08 |
| 复杂体网格 limb | CELL | chem_0 | 3.895160e-07 | 2.725550e-06 |
| 曲面网格 ShipHull_0 | POINT | RF | 0.003892 | 0.001156 |
| 曲面网格 ShipHull_0 | CELL | S_Mises | 0.021487 | 0.011457 |
| 曲面网格 1_0 | POINT | RF | 5.634770e-08 | 3.421260e-10 |
| 曲面网格 1_0 | CELL | S_Mises | 7.778060e-08 | 1.372860e-07 |

该表回答的是“工程一致性”问题，而不替代解析真值验证。

### 表5-5 梯度计算时间对比
| 数据集 | 关联/字段 | VTK单线程/ms | VTK并行/ms | 线程数 | 系统总时间/ms | GPU时间/ms |
| --- | --- | --- | --- | --- | --- | --- |
| 规则网格 SampleStructGrid | POINT / scalars | 10.376 | 3.763 | 16 | 1.163 | 0.040 |
| 规则网格 SampleStructGrid | CELL / scalars | 165.003 | 386.046 | 16 | 1.304 | 0.041 |
| 六面体体网格 hexa | POINT / scalars | 184.747 | 285.899 | 16 | 4.250 | 1.322 |
| 复杂体网格 limb | CELL / chem_0 | 140.150 | 80.400 | 16 | 2.363 | 0.621 |
| 曲面网格 ShipHull_0 | POINT / RF | 4.620 | 11.127 | 16 | 0.822 | 0.106 |
| 曲面网格 ShipHull_0 | CELL / S_Mises | 4.893 | 8.316 | 16 | 0.731 | 0.044 |
| 曲面网格 1_0 | POINT / RF | 9.585 | 14.029 | 16 | 1.594 | 0.163 |
| 曲面网格 1_0 | CELL / S_Mises | 5.496 | 8.668 | 16 | 0.692 | 0.044 |

VTK 并行后端已启用为 STDThread，线程数为 16；但并行不一定在每个数据集上都快于单线程，实验中应如实报告。

### 表5-7 点数据多尺度优化平均结果
| 噪声类型 | 平均输入NRMSE | 平均输出NRMSE | 平均RMSE改进率 | 平均粗糙度比 | 平均GPU时间/ms |
| --- | --- | --- | --- | --- | --- |
| gaussian | 0.297 | 0.181 | 0.611 | 0.407 | 0.070 |
| grf | 0.299 | 0.297 | 0.993 | 0.867 | 0.109 |
| impulse | 0.454 | 0.452 | 0.997 | 0.969 | 0.070 |
| mixed | 0.499 | 0.434 | 0.865 | 0.586 | 0.070 |

gaussian 与 mixed 的抑制效果明显；impulse 的 RMSE 改进率接近 1，说明当前模块并不适合脉冲型异常值。

### 表5-8 单元数据多尺度优化平均结果
| 噪声类型 | 平均输入NRMSE | 平均输出NRMSE | 平均RMSE改进率 | 平均粗糙度比 | 平均GPU时间/ms |
| --- | --- | --- | --- | --- | --- |
| gaussian | 0.295 | 0.122 | 0.415 | 0.387 | 0.048 |
| grf | 0.299 | 0.251 | 0.843 | 0.734 | 0.049 |
| impulse | 0.396 | 0.396 | 1.001 | 0.982 | 0.048 |
| mixed | 0.508 | 0.425 | 0.839 | 0.584 | 0.048 |

与点数据相比，单元域上对 gaussian 扰动的平均抑制更强，但 impulse 场景同样没有显著改善。

### 表5-9 代表性场上的数据优化结果
| 关联 | 干净场 | 噪声 | 输入NRMSE | 输出NRMSE | RMSE改进率 | 粗糙度比 |
| --- | --- | --- | --- | --- | --- | --- |
| POINT | ms_clean_trig | gaussian | 0.295 | 0.177 | 0.598 | 0.366 |
| POINT | ms_clean_trig | mixed | 0.559 | 0.506 | 0.905 | 0.654 |
| POINT | ms_clean_trig | impulse | 0.478 | 0.480 | 1.003 | 0.978 |
| POINT | ms_clean_edge | gaussian | 0.338 | 0.206 | 0.611 | 0.408 |
| POINT | ms_clean_edge | mixed | 0.602 | 0.538 | 0.894 | 0.610 |
| POINT | ms_clean_edge | impulse | 0.496 | 0.487 | 0.981 | 0.975 |
| CELL | ms_clean_trig | gaussian | 0.298 | 0.123 | 0.413 | 0.333 |
| CELL | ms_clean_trig | mixed | 0.489 | 0.405 | 0.827 | 0.550 |
| CELL | ms_clean_trig | impulse | 0.424 | 0.424 | 1.000 | 0.971 |
| CELL | ms_clean_edge | gaussian | 0.335 | 0.137 | 0.408 | 0.380 |
| CELL | ms_clean_edge | mixed | 0.538 | 0.432 | 0.803 | 0.529 |
| CELL | ms_clean_edge | impulse | 0.373 | 0.373 | 1.000 | 0.996 |

ms_clean_edge 用于观察边缘保持能力；在 gaussian 与 mixed 场景下，误差和粗糙度都明显下降，说明该模块不是简单的整体模糊化。

### 表5-9a 数据优化模块关键参数与默认设置
| 参数 | 默认值 | 作用 | 调大后的主要影响 |
| --- | --- | --- | --- |
| levels | 3 | 多尺度分解层数 | 增大可增强低频分离，但会增加计算和过平滑风险 |
| iterations | 1 | 每层双边滤波迭代次数 | 增大可加强平滑，但边缘钝化风险同步增加 |
| spatial_sigma_factor | 1.5 | 空间邻域权重尺度 | 增大可扩大平滑影响范围 |
| range_sigma_factor | 0.5 | 数值相似度权重尺度 | 增大后更容易跨数值差异做平均 |
| level_scale | 1.8 | 层间尺度放大倍数 | 增大后高层更偏向大尺度平滑 |
| edge_sigma_factor | 0.35 | 细节回注的边缘抑制尺度 | 减小可更强保护边缘，增大则更平滑 |
| detail_gain[0,1,2] | [1.00, 0.75, 0.50] | 三层细节回注增益 | 从细到粗逐层递减，避免高频细节被过量放回 |
| sigma_factor | 0.35 | 高斯扰动强度系数 | 控制 synthetic 高斯扰动标准差 |
| corr_length / corr_iters / corr_alpha | 1.00 / 4 / 0.80 | 相关高斯扰动的相关长度与迭代参数 | 决定 GRF 扰动的空间相关程度 |
| impulse_ratio / impulse_scale | 0.04 / 2.50 | 脉冲扰动占比与幅值比例 | 决定 impulse / mixed 场景下异常值强度 |

本组参数在小规模观察后固定，用于全文 batch 实验；本文不把参数搜索作为主工作量，而是强调固定口径下的可重复比较。

### 表5-9b 参数选择依据的小型案例验证
| 案例 | 输入NRMSE | 输出NRMSE | 粗糙度比 | 对参数解释的含义 |
| --- | --- | --- | --- | --- |
| POINT / ms_clean_edge / gaussian | 0.338 | 0.206 | 0.408 | 边缘场在默认参数下仍明显降噪，说明当前 range / edge 设置没有把主边界整体抹平。 |
| POINT / ms_clean_trig / grf | 0.294 | 0.289 | 0.854 | 相关噪声改善有限，说明不能仅靠放大平滑参数去强压空间相关扰动。 |
| POINT / ms_clean_edge / impulse | 0.496 | 0.487 | 0.975 | 脉冲扰动几乎无改善，说明问题主要来自噪声类型失配，而不是当前参数略偏弱。 |

该表不是完整参数寻优，而是用于说明当前默认参数为什么被保留下来：它们对 gaussian 场景有效、对 grf 场景保持谨慎、对 impulse 场景不过度夸大能力。
