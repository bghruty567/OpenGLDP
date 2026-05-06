from __future__ import annotations

import re
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
W = ROOT / "wendang"
FORBIDDEN = re.compile(r"最小二乘|WLS|AWLS|least\s*squares", re.I)


def write(name: str, content: str) -> None:
    text = textwrap.dedent(content).strip() + "\n"
    if FORBIDDEN.search(text):
        raise ValueError(f"forbidden deprecated method wording in {name}")
    (W / name).write_text(text, encoding="utf-8")


write("00_摘要.md", r"""
# 摘要

计算机辅助工程（CAE）后处理阶段需要将求解器输出的网格模型、节点场、单元场和派生物理量转换为可分析、可比较、可视化的数据结果。梯度计算、局部数值扰动抑制和结果导出是后处理数据准备中的基础环节，直接影响应力集中识别、物理场变化趋势分析以及后续可视化观察。随着仿真数据规模增大，传统 CPU 侧过滤器在交互式分析和批量处理中的时间开销逐渐突出。本文围绕这一问题，设计并实现了一套基于 OpenGL 的 CAE 数据预处理原型系统。

系统采用 C++17、VTK、Qt 和 OpenGL Compute Shader 开发。VTK 用于数据读写、工程参考对照和结果导出，Qt 用于基础图形界面，OpenGL 计算着色器用于执行核心预处理计算。系统首先将 VTK 数据转换为统一内部数据对象，保存点坐标、单元连接、单元类型、点数据、单元数据和邻域关系；随后由门面层根据网格类型和字段关联方式调度不同算法；最后将计算结果写回内部字段数组，并支持导出为 VTK 文件供 ParaView 观察。

在梯度计算方面，本文将技术路线收敛为两条主线：规则网格采用有限差分法，非结构化网格采用基于形函数导数的方法。非结构化网格路径支持一阶三角形、四边形、四面体和六面体四类单元。实验按照解析场验证、与 VTK 结果一致性对比和与 VTK 时间对比三类组织。解析场验证表明，系统在 SampleStructGrid、hexa 和 1_0 数据集上的线性标量场与线性向量场平均相对误差均达到 \(10^{-7}\) 量级；真实字段对比表明，系统结果与 vtkGradientFilter 保持较高一致性；时间实验表明，在统一生成的规则网格族和非结构化六面体网格族上，系统总时间和 GPU 计算时间均明显低于当前测试条件下的 VTK 并行时间。

在数据优化方面，本文不将其表述为通用降噪模块，而是收敛为“面向一类局部随机高频数值扰动的抑制与边缘保持处理”。CAE 仿真数据可能受到离散化误差、迭代收敛误差与舍入误差影响，其中一类扰动表现为局部随机高频波动。本文采用高斯扰动作为代理模型，在 ShipHull_0 数据集上构造干净场并叠加扰动，通过优化前后误差对比验证模块效果。实验结果显示，点数据平均相对误差由 0.296642 降至 0.180858，单元数据平均相对误差由 0.294558 降至 0.121921，说明该模块对目标扰动具有稳定抑制作用。

本文的主要贡献在于：构建了规则网格与非结构化网格统一的数据预处理流程；实现了基于 OpenGL Compute Shader 的梯度计算与数据优化模块；建立了与解析真值、VTK 输出和 VTK 时间对照相结合的实验体系；形成了可运行、可导出、可复现的本科毕业设计原型系统。本文工作不以替代完整 CAE 后处理软件为目标，而是为后处理中的数据准备与 GPU 并行计算提供了一个边界清晰的研究与实践案例。

**关键词**：CAE 后处理；OpenGL；Compute Shader；梯度计算；形函数导数；数据优化

## 摘要撰写依据

摘要内容依据本文系统实现、`毕设进展.docx` 中的实验数据，以及 OpenGL Compute Shader、VTK 数据模型和本科论文写作规范相关资料整理[1-4][14]。
""")


write("01_绪论.md", r"""
# 第一章 绪论

## 1.1 研究背景

CAE 技术通过数值方法对结构、热、流体和多物理场问题进行求解，是现代工程设计的重要支撑。求解器输出的结果通常由网格几何、单元拓扑、节点场、单元场和派生变量组成。工程人员无法仅凭原始离散数据直接进行判断，必须借助后处理工具完成字段派生、局部分析和可视化展示。后处理并不只是“把数据画出来”，其前端往往包含梯度计算、字段转换、局部平滑、异常观察和结果导出等数据预处理任务。

科学可视化工具链中，VTK 和 ParaView 已经形成成熟生态。ParaView 文档将 VTK 数据模型概括为 mesh 和 attributes，其中 mesh 由 points 和 cells 组成，attributes 则以点数据或单元数据等形式附着在数据集上[4]。这种数据模型与 CAE 网格结果天然对应，因此本文选择 VTK 作为数据读写、工程对照和结果导出的基础。但本文的研究重点不是封装现有过滤器，而是围绕 OpenGL 计算管线实现一条自有的预处理主流程。

随着 GPU 可编程能力增强，图形处理器已不再只服务于图像渲染。GPGPU 综述指出，图形硬件在可编程性和吞吐能力上的提升使其成为通用计算的重要平台[8]。OpenGL Compute Shader 提供了在 OpenGL 环境内执行通用并行计算的能力，SSBO 又允许着色器读写较大规模结构化数据[1][2]。这使得 CAE 后处理中的一部分局部并行计算可以迁移到 GPU 端完成。

```mermaid
flowchart LR
    A["CAE 求解结果"] --> B["VTK 数据读入"]
    B --> C["内部数据对象"]
    C --> D["OpenGL Compute Shader"]
    D --> E["梯度计算 / 数据优化"]
    E --> F["结果字段写回"]
    F --> G["VTK 导出 / ParaView 观察"]
```

图 1-1 给出了本文课题的总体数据流。与一般软件系统类本科论文相似，本文也需要说明需求、设计、实现和测试；但由于课题包含数值计算和科学可视化背景，论文还必须补充算法公式、实验指标和工程对照。

## 1.2 本科毕业论文写作要求对本课题的启示

教育部本科毕业论文抽检办法明确，本科论文抽检重点关注选题意义、写作安排、逻辑构建、专业能力和学术规范[14]。部分地方细则进一步将选题意义、逻辑构建、专业能力、结构规范和参考文献规范列为评议观察点[15][16]。这对工程型计算机毕业设计有直接启示：论文不能只展示界面截图，也不能只堆代码说明，而应围绕“问题为什么成立、系统如何设计、算法如何实现、实验如何验证、结论边界在哪里”展开。

公开的计算机类毕业论文写作示例和指南通常采用“绪论—相关技术—需求分析—系统设计—实现—测试—总结”的结构[19][20]。Cambridge 计算机系的 dissertation 指南也强调 implementation 章节应描述实际完成的程序、设计策略和工程方法，evaluation 章节应证明成果达到了目标[21]。结合这些写法，本文采用“相关技术与理论基础、系统需求与总体设计、核心算法设计与实现、实验设计与结果分析”的组织方式。

## 1.3 问题提出

本文研究的问题可以概括为：如何在 CAE 后处理数据预处理场景中，构建一条从 VTK 数据输入到 GPU 计算再到结果导出的完整原型流程。该问题有三个关键点。

第一，网格类型不同导致计算方式不同。规则网格具有明确逻辑维度，可以使用有限差分进行梯度计算；非结构化网格通过单元连接描述拓扑，更适合从单元形函数导数和几何映射角度计算梯度。若论文不区分两类网格，算法说明会不严谨。

第二，正确性和工程一致性不是同一件事。解析场实验能够提供真值，用于验证算法本身；真实字段与 vtkGradientFilter 的对比能够说明系统结果与成熟工具是否一致。VTK 文档说明 vtkGradientFilter 是用于估计数据集字段梯度的一般过滤器，输出数组与输入字段关联方式一致，并按输入分量输出三方向导数[3]。因此本文把它作为工程基线，但不把它当作解析真值。

第三，数据优化必须收敛适用范围。CAE 仿真数据中的扰动来源复杂，本文只讨论一类表现为局部随机高频波动的数值扰动，并使用高斯扰动作为代理模型。双边滤波的经典思想是根据空间接近性和数值相似性进行局部平滑，同时保留边缘[11]；网格双边去噪研究也表明，该类思想可从图像扩展到网格局部邻域处理[12]。本文的数据优化模块正是在这一思想下进行工程化实现。

## 1.4 研究目标与边界

本文目标包括四项：设计统一内部数据表示；实现规则网格有限差分梯度和非结构化网格形函数导数梯度；实现面向局部随机高频扰动的数据优化模块；建立解析场、VTK 一致性、时间性能和优化效果四类实验。

课题边界也必须明确。本文不是求解器，不负责生成仿真结果；不是完整 ParaView 替代品，不覆盖完整可视分析功能；也不是通用数据清洗框架，只讨论当前定义的一类局部随机高频扰动。边界收敛并不会削弱论文价值，反而能让研究目标、实现内容和实验数据保持一致。

## 1.5 技术路线

本文技术路线如下。

```mermaid
flowchart TD
    A["文献调研与需求收缩"] --> B["VTK 数据模型分析"]
    B --> C["内部数据对象设计"]
    C --> D["规则网格有限差分实现"]
    C --> E["非结构化网格形函数导数实现"]
    C --> F["图双边滤波与多尺度融合实现"]
    D --> G["解析场验证"]
    E --> G
    G --> H["VTK 一致性对比"]
    H --> I["时间性能实验"]
    F --> J["高斯扰动优化实验"]
    I --> K["论文与答辩材料"]
    J --> K
```

图 1-2 说明本文不是先写界面再补实验，而是围绕数据结构和算法主线组织系统实现。该路线也与工程型本科论文“设计—实现—测试”的写法相符[19][21]。

## 1.6 本章参考文献

本章主要参考文献：[1]、[2]、[3]、[4]、[8]、[11]、[12]、[14]、[15]、[16]、[19]、[20]、[21]。
""")


write("02_相关技术与理论基础.md", r"""
# 第二章 相关技术与理论基础

## 2.1 本章写法说明

工程型本科论文的相关技术章节不应写成工具说明书，也不应堆砌百科式概念。根据本科论文抽检对逻辑构建和专业能力的要求[14]，本章应回答三个问题：本文为什么需要这些技术，这些技术在系统中承担什么角色，它们如何支撑后续算法和实验。基于这一原则，本章围绕 VTK/ParaView 数据模型、OpenGL Compute Shader、规则网格有限差分、非结构化网格形函数导数、图双边滤波和实验评价指标展开。

```mermaid
flowchart LR
    A["相关技术"] --> B["数据模型: VTK/ParaView"]
    A --> C["并行计算: OpenGL Compute Shader"]
    A --> D["梯度理论: FD + 形函数导数"]
    A --> E["数据优化: 图双边滤波"]
    B --> F["系统数据结构"]
    C --> F
    D --> G["梯度模块"]
    E --> H["优化模块"]
```

图 2-1 展示本章技术与系统模块之间的关系。

## 2.2 CAE 后处理数据模型

CAE 后处理数据通常由网格和属性两部分组成。网格描述空间离散结构，属性描述物理量在网格上的分布。ParaView 文档将 VTK 数据模型概括为 mesh 和 attributes，其中 mesh 包含 points 和 cells，cells 可表示四面体、六面体等离散单元[4]。这与有限元和工程仿真数据的组织方式一致。

点数据和单元数据是本文必须区分的两个概念。点数据定义在网格节点上，适合表示节点位移、节点温度或节点场变量；单元数据定义在单元上，常用于表示单元平均值、单元应力或单元级结果。vtkGradientFilter 文档也说明，梯度结果会保持输入字段的关联方式，即点数据输入得到点数据梯度，单元数据输入得到单元数据梯度[3]。因此本文系统在内部字段描述中保留字段名称、分量数和关联方式，避免计算和导出时混淆。

设网格点集合为

$$
\mathcal{P}=\{\boldsymbol{x}_i\in\mathbb{R}^3\mid i=1,2,\ldots,n_p\},
$$

单元集合为

$$
\mathcal{C}=\{c_e=(i_1,i_2,\ldots,i_{n_e})\mid e=1,2,\ldots,n_c\}.
$$

其中 \(\boldsymbol{x}_i=(x_i,y_i,z_i)^T\) 表示第 \(i\) 个点坐标，\(c_e\) 表示第 \(e\) 个单元的节点索引序列，\(n_e\) 表示该单元的节点数。该符号与有限元文献中常用的节点坐标和单元连接记法一致[9]。

## 2.3 VTK、ParaView 与本文系统的关系

VTK 是科学可视化的基础工具库，提供数据结构、读写接口、过滤器和渲染支持。ParaView 则在 VTK 基础上提供交互式可视化环境。本文使用 VTK 的目的不是减少工作量，而是利用其成熟的数据生态：一方面，VTK 能读取和写出实验数据；另一方面，vtkGradientFilter 可以作为真实字段工程一致性对照；此外，导出的 VTK 文件可以在 ParaView 中继续观察。

vtkGradientFilter 是本文实验中的重要参考。文档说明，该过滤器用于估计数据集中字段的梯度，计算方式与输入数据集类型有关，输出分量顺序为每个输入分量对应 \(x,y,z\) 三个方向导数[3]。若输入标量场为 \(u\)，则输出梯度为

$$
\nabla u =
\begin{bmatrix}
\dfrac{\partial u}{\partial x}\\[4pt]
\dfrac{\partial u}{\partial y}\\[4pt]
\dfrac{\partial u}{\partial z}
\end{bmatrix}.
$$

若输入为 \(m\) 分量向量场 \(\boldsymbol{u}=(u_1,\ldots,u_m)^T\)，则每个分量分别输出三方向导数，输出分量数为 \(3m\)。这与本文系统的梯度数组组织方式一致。

## 2.4 OpenGL Compute Shader 与 GPU 缓冲

OpenGL Compute Shader 是 OpenGL 中用于通用计算的着色器阶段。它通过工作组和线程编号执行任务，不直接依赖传统渲染图元。Khronos 文档说明，Compute Shader 可以用于图形管线外的通用 GPU 计算[1]。本文将每个点或单元的局部计算映射为一个或一组线程，使梯度计算和滤波计算能够并行执行。

SSBO 是本文数据传输的重要机制。Khronos 文档说明，Shader Storage Buffer Object 能够在 GLSL 中存储和读取数据，并且容量大于传统 uniform buffer，支持着色器端读写[2]。本文将点坐标、字段值、单元连接、单元类型、邻域偏移和输出结果组织为连续数组并上传到 GPU 缓冲区。着色器端通过索引访问这些数组，避免复杂对象结构进入 GPU 端。

设第 \(g\) 个 GPU 线程处理第 \(i\) 个采样位置，可写作

$$
i = g,\qquad g=\texttt{gl\_GlobalInvocationID.x}.
$$

对规则网格，线程编号还可恢复为

$$
i_x = i \bmod n_x,\quad
i_y = \left\lfloor \frac{i}{n_x} \right\rfloor \bmod n_y,\quad
i_z = \left\lfloor \frac{i}{n_xn_y} \right\rfloor.
$$

该映射使规则网格有限差分能够在 GPU 上按点并行执行。

## 2.5 规则网格有限差分与链式法则

有限差分法适用于具有规则逻辑索引的数据。需要注意的是，本文规则网格路径并不是简单假设物理空间一定为正交均匀网格，而是先在规则网格的逻辑/参数坐标中做差分，再通过链式法则映射到物理空间。该处理方式与曲线网格或结构网格中常见的“计算坐标到物理坐标映射”写法一致。

设规则网格的逻辑坐标为

$$
\boldsymbol{\xi}=(\xi,\eta,\zeta)^T,
$$

物理坐标为

$$
\boldsymbol{x}=\boldsymbol{x}(\boldsymbol{\xi})=(x(\boldsymbol{\xi}),y(\boldsymbol{\xi}),z(\boldsymbol{\xi}))^T.
$$

对标量场 \(u\)，先在参数空间中估计

$$
\nabla_{\boldsymbol{\xi}}u
=
\begin{bmatrix}
\dfrac{\partial u}{\partial \xi}\\[4pt]
\dfrac{\partial u}{\partial \eta}\\[4pt]
\dfrac{\partial u}{\partial \zeta}
\end{bmatrix}.
$$

在内部点处，三个参数方向上的中心差分可写为

$$
\left.\frac{\partial u}{\partial \xi}\right|_{i,j,k}
\approx
\frac{u_{i+1,j,k}-u_{i-1,j,k}}{2},
$$

$$
\left.\frac{\partial u}{\partial \eta}\right|_{i,j,k}
\approx
\frac{u_{i,j+1,k}-u_{i,j-1,k}}{2},\qquad
\left.\frac{\partial u}{\partial \zeta}\right|_{i,j,k}
\approx
\frac{u_{i,j,k+1}-u_{i,j,k-1}}{2}.
$$

边界点缺少一侧邻点时使用单边差分，例如左边界 \(i=0\) 可写为

$$
\left.\frac{\partial u}{\partial \xi}\right|_{0,j,k}
\approx
u_{1,j,k}-u_{0,j,k}.
$$

与此同时，还需要用相同的差分模板估计几何映射导数，构造局部 Jacobian：

$$
\mathbf{J}
=
\frac{\partial\boldsymbol{x}}{\partial\boldsymbol{\xi}}
=
\begin{bmatrix}
\dfrac{\partial x}{\partial \xi} & \dfrac{\partial x}{\partial \eta} & \dfrac{\partial x}{\partial \zeta}\\[6pt]
\dfrac{\partial y}{\partial \xi} & \dfrac{\partial y}{\partial \eta} & \dfrac{\partial y}{\partial \zeta}\\[6pt]
\dfrac{\partial z}{\partial \xi} & \dfrac{\partial z}{\partial \eta} & \dfrac{\partial z}{\partial \zeta}
\end{bmatrix}.
$$

根据链式法则，有

$$
\nabla_{\boldsymbol{\xi}}u
=
\mathbf{J}^{T}\nabla_{\boldsymbol{x}}u,
$$

因此物理空间梯度为

$$
\nabla_{\boldsymbol{x}}u
=
\mathbf{J}^{-T}\nabla_{\boldsymbol{\xi}}u.
$$

该公式也解释了规则网格着色器实现中的两个步骤：先对字段值和样本坐标分别做参数方向差分，构造 \(\nabla_{\boldsymbol{\xi}}u\) 与 \(\mathbf{J}\)；再用 \(\mathbf{J}^{-T}\) 将参数空间导数映射为物理空间梯度。若物理网格恰好是正交均匀笛卡尔网格，则 \(\mathbf{J}\) 退化为对角尺度矩阵，上式自然退化为常见的 \(\Delta x,\Delta y,\Delta z\) 有限差分公式。

## 2.6 非结构化网格形函数导数理论

非结构化网格由不同单元拼接而成，缺少规则索引关系。本文采用基于形函数导数的方法计算其梯度。有限元理论中，单元内部字段通常由节点值和形函数插值表示[9]。对第 \(e\) 个单元，设局部坐标为 \(\boldsymbol{\xi}=(\xi,\eta,\zeta)^T\)，节点数为 \(n_e\)，节点值为 \(u_a\)，形函数为 \(N_a(\boldsymbol{\xi})\)，则单元内字段近似为

$$
u^e(\boldsymbol{\xi})=\sum_{a=1}^{n_e}N_a(\boldsymbol{\xi})u_a.
$$

物理坐标同样由节点坐标插值得到：

$$
\boldsymbol{x}^e(\boldsymbol{\xi})
=\sum_{a=1}^{n_e}N_a(\boldsymbol{\xi})\boldsymbol{x}_a.
$$

局部坐标到物理坐标的 Jacobian 矩阵为

$$
\mathbf{J}
=
\frac{\partial \boldsymbol{x}}{\partial \boldsymbol{\xi}}
=
\sum_{a=1}^{n_e}\boldsymbol{x}_a
\left(\nabla_{\boldsymbol{\xi}}N_a\right)^T.
$$

形函数物理空间导数由链式法则得到：

$$
\nabla_{\boldsymbol{x}}N_a
=
\mathbf{J}^{-T}\nabla_{\boldsymbol{\xi}}N_a.
$$

因此单元内梯度可写为

$$
\nabla_{\boldsymbol{x}}u^e
=
\sum_{a=1}^{n_e}u_a\nabla_{\boldsymbol{x}}N_a.
$$

该组公式采用有限元文献中常见的 \(N_a\)、\(\boldsymbol{\xi}\)、\(\boldsymbol{x}\)、\(\mathbf{J}\) 和 \(\nabla_{\boldsymbol{x}}\) 记法[9][10]，与本文非结构化网格形函数导数路径一致。

## 2.7 图双边滤波与多尺度融合

数据优化模块的理论基础来自保边平滑。Tomasi 和 Manduchi 提出的双边滤波同时考虑空间接近性和数值相似性，能够在平滑图像的同时保留边缘[11]。Fleishman 等将类似思想扩展到网格去噪，说明局部邻域滤波可以在几何数据上抑制噪声并保留特征[12]。

设采样位置 \(i\) 的坐标为 \(\boldsymbol{x}_i\)，字段值为 \(u_i\)，邻域为 \(\mathcal{N}(i)\)。图双边滤波可写为

$$
\tilde{u}_i
=
\frac{1}{W_i}
\sum_{j\in\mathcal{N}(i)}
w_{ij}u_j,
\qquad
W_i=\sum_{j\in\mathcal{N}(i)}w_{ij}.
$$

权重由空间项和值域项相乘：

$$
w_{ij}
=
\exp\left(-\frac{\|\boldsymbol{x}_i-\boldsymbol{x}_j\|^2}{2\sigma_s^2}\right)
\exp\left(-\frac{(u_i-u_j)^2}{2\sigma_r^2}\right).
$$

其中 \(\sigma_s\) 控制空间邻近尺度，\(\sigma_r\) 控制字段值差异尺度。多尺度融合可写为

$$
u^{(0)}=u,\qquad
u^{(\ell+1)}=B_{\sigma_s^{(\ell)},\sigma_r}\left(u^{(\ell)}\right),
$$

$$
d^{(\ell)}=u^{(\ell)}-u^{(\ell+1)},\qquad
\hat{u}=u^{(L)}+\sum_{\ell=0}^{L-1}\alpha_\ell d^{(\ell)}.
$$

其中 \(B\) 表示双边滤波算子，\(d^{(\ell)}\) 表示第 \(\ell\) 层细节，\(\alpha_\ell\) 表示细节回注权重。本文实验只使用高斯扰动代理局部随机高频扰动，不将该模块扩展为通用异常值处理。

## 2.8 实验评价指标

解析场实验使用平均相对误差。设系统结果为 \(g_i\)，参考结果为 \(g_i^\ast\)，样本数为 \(n\)，则可写为

$$
E_{\mathrm{rel}}
=
\frac{1}{n}\sum_{i=1}^{n}
\frac{\|g_i-g_i^\ast\|_2}{\|g_i^\ast\|_2+\varepsilon},
$$

其中 \(\varepsilon\) 为防止分母为零的稳定项。时间实验使用加速比：

$$
S_{\mathrm{VTK}}
=
\frac{T_{\mathrm{VTK}}}{T_{\mathrm{sys}}},
$$

其中 \(T_{\mathrm{VTK}}\) 为 VTK 并行时间，\(T_{\mathrm{sys}}\) 为系统总时间。数据优化实验使用改进比：

$$
R_{\mathrm{imp}}
=
\frac{E_{\mathrm{after}}}{E_{\mathrm{before}}}.
$$

当 \(R_{\mathrm{imp}}<1\) 时，表示优化后误差低于输入误差。

## 2.9 本章参考文献

本章主要参考文献：[1]、[2]、[3]、[4]、[6]、[8]、[9]、[10]、[11]、[12]、[14]。
""")


write("03_系统需求分析与总体设计.md", r"""
# 第三章 系统需求分析与总体设计

## 3.1 需求分析写法说明

计算机类工程型毕业论文通常需要把“需求分析—系统设计—详细实现—测试验证”写成一条连续逻辑。根据本科论文抽检对逻辑构建和专业能力的要求[14]，需求分析不能只列功能清单，还要说明为什么这些功能与课题目标相关。公开软件系统类毕业论文写作指南也通常要求在需求分析中给出功能需求、非功能需求、模块结构和测试目标[19][20]。本文系统面向 CAE 后处理数据预处理，因此需求分析围绕数据输入、内部表示、GPU 计算、结果导出和实验复现展开。

## 3.2 功能需求

系统功能需求包括五类。第一，数据加载需求。系统应能读取 VTK 格式数据，获得点坐标、单元连接、单元类型、点数据和单元数据。第二，数据转换需求。系统应能把 VTK 数据转换为统一内部数据对象，使规则网格和非结构化网格都能进入同一处理框架。第三，梯度计算需求。系统应根据网格类型选择规则网格有限差分或非结构化网格形函数导数方法。第四，数据优化需求。系统应能针对局部随机高频数值扰动执行平滑与边缘保持处理。第五，结果导出需求。系统应能将派生字段写回数据对象并导出为 VTK 文件。

| 需求类别 | 具体内容 | 对应章节 |
| --- | --- | --- |
| 数据加载 | 读取 VTK 文件，提取点、单元和字段 | 第三章、第四章 |
| 梯度计算 | 规则网格有限差分，非结构化网格形函数导数 | 第四章、第五章 |
| 数据优化 | 高斯扰动代理下的局部高频扰动抑制 | 第四章、第五章 |
| 结果导出 | 写回字段并导出 VTK | 第三章、第四章 |
| 实验复现 | 输出解析场、VTK 对照、时间和优化结果 | 第五章 |

## 3.3 非功能需求

非功能需求主要包括可复现性、可扩展性、性能和口径一致性。可复现性要求实验程序能够重复生成论文中的表格数据。可扩展性要求内部数据结构能够继续支持更多字段和更多单元类型。性能要求核心计算尽可能利用 GPU 并行能力，并在实验中与 VTK 并行时间进行对照。口径一致性要求 GUI、测试程序和论文描述使用同一底层逻辑，避免演示功能与实验功能不一致。

## 3.4 总体架构

```mermaid
flowchart TB
    subgraph UI["界面与实验层"]
        A["Qt GUI"]
        B["TestGradient / TestMultiScale"]
    end
    subgraph Facade["门面层"]
        C["CAEProcessingFacade"]
    end
    subgraph Data["数据层"]
        D["VTKDataConverter"]
        E["DataObject"]
    end
    subgraph GPU["OpenGL 计算层"]
        F["GLGradientEngine"]
        G["GLFilterEngine"]
        H["Compute Shaders"]
    end
    I["VTK / ParaView"]
    A --> C
    B --> C
    C --> D
    D --> E
    E --> F
    E --> G
    F --> H
    G --> H
    H --> E
    E --> D
    D --> I
```

图 3-1 展示系统总体架构。该结构对应工程型论文中常见的分层设计写法[19][21]：界面与实验层负责操作入口，门面层负责业务调度，数据层负责结构转换，OpenGL 计算层负责核心算法。

## 3.5 内部数据对象设计

内部数据对象需要同时适配 CPU 管理和 GPU 访问。点坐标保存为连续浮点数组，单元连接保存为索引数组，单元偏移用于确定每个单元包含的点范围，单元类型用于区分三角形、四边形、四面体和六面体等类型。字段数组保存字段名、分量数、关联方式和扁平数据。

设字段 \(u\) 有 \(m\) 个分量，点数据数组可表示为

$$
\mathbf{U}_{p}\in\mathbb{R}^{n_p\times m},
$$

单元数据数组可表示为

$$
\mathbf{U}_{c}\in\mathbb{R}^{n_c\times m}.
$$

梯度输出对应为

$$
\nabla \mathbf{U}_{p}\in\mathbb{R}^{n_p\times 3m},\qquad
\nabla \mathbf{U}_{c}\in\mathbb{R}^{n_c\times 3m}.
$$

这一表示与 vtkGradientFilter 的输出分量组织保持一致[3]，也便于导出后在 ParaView 中观察。

## 3.6 门面层设计

门面层承担统一调度职责。加载数据时，它调用转换模块生成内部数据对象并保存数据集记录。执行梯度计算时，它检查字段是否存在，再根据网格类型选择有限差分或形函数导数。执行数据优化时，它读取字段和邻域图，调用滤波与融合引擎。计算结束后，门面层统一记录结果字段名、源字段、字段关联方式、输出分量数和时间信息。

## 3.7 GUI 与测试程序设计

GUI 主要用于演示和基础操作，包括打开 VTK 文件、选择字段、执行梯度计算、执行数据优化和导出结果。测试程序主要用于生成实验数据，包括解析场验证、VTK 一致性对比、时间对比和数据优化实验。两者共享同一底层接口，保证论文表格与系统演示使用同一实现路径。

## 3.8 本章参考文献

本章主要参考文献：[3]、[4]、[14]、[19]、[20]、[21]。
""")


write("04_核心算法设计与实现.md", r"""
# 第四章 核心算法设计与实现

## 4.1 本章写法说明

算法实现章节应避免只贴代码，也不能只写公式。Cambridge 计算机系 dissertation 指南强调，implementation 章节应描述实际完成的程序、设计策略和工程方法，而大量代码应放入附录或省略[21]。因此本章以公式、流程和模块对应关系说明核心实现。

## 4.2 规则网格有限差分梯度

规则网格具有明确的逻辑维度。本文实现中，规则网格梯度计算包含两个阶段：第一阶段在逻辑/参数坐标 \(\boldsymbol{\xi}=(\xi,\eta,\zeta)^T\) 上进行有限差分；第二阶段通过链式法则将参数空间导数映射到物理空间 \(\boldsymbol{x}=(x,y,z)^T\)。这一点与 `Shaders/FD.glsl` 中的实现一致。

对内部点，字段值在三个参数方向上的差分为

$$
d_{\xi}u
\approx
\frac{u_{i+1,j,k}-u_{i-1,j,k}}{2},\qquad
d_{\eta}u
\approx
\frac{u_{i,j+1,k}-u_{i,j-1,k}}{2},
$$

$$
d_{\zeta}u
\approx
\frac{u_{i,j,k+1}-u_{i,j,k-1}}{2}.
$$

边界点采用单边差分，例如

$$
d_{\xi}u\big|_{0,j,k}
\approx
u_{1,j,k}-u_{0,j,k}.
$$

同一组邻居也用于估计几何映射导数。例如，

$$
\frac{\partial \boldsymbol{x}}{\partial \xi}
\approx
\frac{\boldsymbol{x}_{i+1,j,k}-\boldsymbol{x}_{i-1,j,k}}{2},
$$

边界处则退化为

$$
\frac{\partial \boldsymbol{x}}{\partial \xi}\bigg|_{0,j,k}
\approx
\boldsymbol{x}_{1,j,k}-\boldsymbol{x}_{0,j,k}.
$$

由三个参数方向的几何导数可构造 Jacobian：

$$
\mathbf{J}
=
\left[
\frac{\partial\boldsymbol{x}}{\partial\xi}\quad
\frac{\partial\boldsymbol{x}}{\partial\eta}\quad
\frac{\partial\boldsymbol{x}}{\partial\zeta}
\right].
$$

参数空间梯度为

$$
\nabla_{\boldsymbol{\xi}}u
=
\begin{bmatrix}
d_{\xi}u\\
d_{\eta}u\\
d_{\zeta}u
\end{bmatrix}.
$$

根据链式法则，

$$
\nabla_{\boldsymbol{x}}u
=
\mathbf{J}^{-T}\nabla_{\boldsymbol{\xi}}u.
$$

在着色器实现中，程序先读取 \(i,j,k\) 三个方向的前后邻居，分别计算字段差分和坐标差分；坐标差分构造局部 Jacobian，字段差分构造 \(\nabla_{\boldsymbol{\xi}}u\)；随后显式计算逆 Jacobian 对应的系数，将参数空间导数组合成 \(g_x,g_y,g_z\)。若 Jacobian 退化，程序会将逆映射压为零，避免无效值继续扩散。对于多分量字段，系统对每个分量分别执行上述过程，输出分量数为输入分量数的三倍。

## 4.3 非结构化网格形函数导数梯度

非结构化网格通过单元连接描述拓扑关系。本文支持一阶三角形、四边形、四面体和六面体四类单元。对单元 \(e\)，字段插值写为

$$
u^e(\boldsymbol{\xi})
=
\sum_{a=1}^{n_e} N_a(\boldsymbol{\xi})u_a,
$$

物理坐标映射写为

$$
\boldsymbol{x}^e(\boldsymbol{\xi})
=
\sum_{a=1}^{n_e} N_a(\boldsymbol{\xi})\boldsymbol{x}_a.
$$

根据有限元理论[9]，Jacobian 和形函数物理导数分别为

$$
\mathbf{J}
=
\frac{\partial\boldsymbol{x}}{\partial\boldsymbol{\xi}},\qquad
\nabla_{\boldsymbol{x}} N_a
=
\mathbf{J}^{-T}\nabla_{\boldsymbol{\xi}}N_a.
$$

因此单元内梯度为

$$
\nabla_{\boldsymbol{x}}u^e
=
\sum_{a=1}^{n_e}u_a\nabla_{\boldsymbol{x}}N_a.
$$

```mermaid
flowchart TD
    A["读取单元节点坐标 x_a"] --> B["读取节点/单元字段 u_a"]
    B --> C["根据单元类型选择 N_a"]
    C --> D["计算 J 与 J^{-T}"]
    D --> E["计算 ∇_x N_a"]
    E --> F["累加得到 ∇_x u^e"]
    F --> G["写回点数据或单元数据梯度"]
```

图 4-1 展示非结构化网格形函数导数计算流程。

## 4.4 数据优化算法

设采样位置 \(i\) 的坐标为 \(\boldsymbol{x}_i\)，字段值为 \(u_i\)，邻域为 \(\mathcal{N}(i)\)。双边滤波结果为

$$
\tilde{u}_i
=
\frac{1}{W_i}
\sum_{j\in\mathcal{N}(i)}w_{ij}u_j,
\qquad
W_i=\sum_{j\in\mathcal{N}(i)}w_{ij}.
$$

权重为

$$
w_{ij}
=
\exp\left(-\frac{\|\boldsymbol{x}_i-\boldsymbol{x}_j\|^2}{2\sigma_s^2}\right)
\exp\left(-\frac{(u_i-u_j)^2}{2\sigma_r^2}\right).
$$

该公式沿用双边滤波中空间核和值域核相乘的写法[11]。对于多尺度融合，设 \(u^{(0)}=u\)，第 \(\ell\) 层平滑结果为

$$
u^{(\ell+1)}
=
B_{\sigma_s^{(\ell)},\sigma_r}(u^{(\ell)}),
$$

细节层和最终融合结果为

$$
d^{(\ell)}=u^{(\ell)}-u^{(\ell+1)},\qquad
\hat{u}=u^{(L)}+\sum_{\ell=0}^{L-1}\alpha_\ell d^{(\ell)}.
$$

本文实验仅验证该模块对高斯扰动代理的局部随机高频扰动有效，不扩展到所有噪声类型。

## 4.5 OpenGL 计算流程

```mermaid
sequenceDiagram
    participant CPU as CPU/门面层
    participant BUF as GPU 缓冲区
    participant CS as Compute Shader
    CPU->>BUF: 上传坐标、字段、连接、邻域
    CPU->>CS: 设置参数并派发
    CS->>BUF: 按线程并行计算
    BUF-->>CPU: 回读结果
    CPU->>CPU: 写回 DataObject 并导出 VTK
```

图 4-2 展示 OpenGL 计算流程。SSBO 支持较大规模结构化数据读写[2]，适合保存 CAE 数据中的坐标、字段和连接关系。

## 4.6 本章参考文献

本章主要参考文献：[1]、[2]、[3]、[9]、[10]、[11]、[12]、[21]。
""")


write("05_实验设计与结果分析.md", r"""
# 第五章 实验设计与结果分析

## 5.1 本章写法说明

本科工程型论文的实验章节应回答“测什么、为什么这样测、怎么测、结果说明什么”。根据本科论文抽检对逻辑构建、专业能力和学术规范的要求[14][15]，实验不能只罗列表格，还要说明每组实验承担的论证任务。本文实验分为四组：解析场验证、与 VTK 结果一致性对比、与 VTK 的时间对比、数据优化实验。四组实验分别支撑算法正确性、工程一致性、性能优势和优化有效性。

```mermaid
flowchart LR
    A["解析场验证"] --> E["算法正确性"]
    B["VTK 一致性对比"] --> F["工程一致性"]
    C["时间对比"] --> G["性能分析"]
    D["高斯扰动优化"] --> H["优化效果"]
```

图 5-1 展示本文实验分层逻辑。

## 5.2 实验指标

解析场和 VTK 对照使用平均相对误差：

$$
E_{\mathrm{rel}}
=
\frac{1}{n}\sum_{i=1}^{n}
\frac{\|g_i-g_i^\ast\|_2}{\|g_i^\ast\|_2+\varepsilon}.
$$

其中 \(g_i\) 为系统计算结果，\(g_i^\ast\) 为解析真值或 VTK 参考结果，\(\varepsilon\) 为稳定项。时间实验使用 VTK 并行时间、系统总时间和 GPU 计算时间。加速比定义为

$$
S_{\mathrm{VTK}}
=
\frac{T_{\mathrm{VTK}}}{T_{\mathrm{sys}}}.
$$

数据优化实验使用改进比

$$
R_{\mathrm{imp}}
=
\frac{E_{\mathrm{after}}}{E_{\mathrm{before}}}.
$$

当 \(R_{\mathrm{imp}}<1\) 时，表示优化后误差降低。

## 5.3 解析场验证实验

解析场验证的目标是证明算法本身正确。由于解析场具有已知真值，系统计算结果可以直接与真值比较。本文分别构造线性标量场和线性向量场。三维数据集在归一化局部坐标上构造一次函数，1_0 数据集在曲面局部切向坐标上构造一次函数。

### 5.3.1 线性标量场

| 数据集 | 场函数说明 | 平均相对误差 |
| --- | --- | --- |
| SampleStructGrid | 三维线性标量场 | 2.82038e-07 |
| hexa | 三维线性标量场 | 1.57998e-07 |
| 1_0 | 曲面切向线性标量场 | 1.68088e-07 |

### 5.3.2 线性向量场

| 数据集 | 场函数说明 | 平均相对误差 |
| --- | --- | --- |
| SampleStructGrid | 三维线性向量场 | 3.30256e-07 |
| hexa | 三维线性向量场 | 2.36149e-07 |
| 1_0 | 曲面切向线性向量场 | 1.32859e-07 |

从结果看，所有平均相对误差均处于 \(10^{-7}\) 量级。SampleStructGrid 结果说明规则网格有限差分路径正确；hexa 结果说明三维非结构化网格形函数导数路径正确；1_0 结果说明曲面切向场构造下二维非结构化曲面网格也能得到稳定结果。因此，解析场验证可以作为梯度模块正确性的直接证据。

## 5.4 与 VTK 结果一致性对比实验

真实字段通常没有解析真值，因此本文使用 vtkGradientFilter 作为工程参考。VTK 文档说明该过滤器用于估计数据集字段梯度，且输出字段关联方式与输入一致[3]。因此，本文将系统结果与 VTK 结果进行对比，用于说明工程输出一致性。

### 5.4.1 点数据对比

| 数据集 | 字段 | 平均相对误差 |
| --- | --- | --- |
| SampleStructGrid | scalars | 8.22437e-08 |
| hexa | scalars | 6.27285e-08 |
| 1_0 | RF | 5.63477e-08 |

### 5.4.2 单元数据对比

| 数据集 | 字段 | 平均相对误差 |
| --- | --- | --- |
| SampleStructGrid | scalars | 7.62786e-07 |
| limb | chem_0 | 3.89516e-07 |
| 1_0 | S_Mises | 7.77806e-08 |

点数据实验中，三个数据集误差均为 \(10^{-8}\) 量级。单元数据实验中，误差为 \(10^{-7}\) 到 \(10^{-6}\) 量级。该结果说明系统在真实字段上与 VTK 保持较高一致性。需要强调，这组实验不替代解析场验证，而是补充说明系统在工程数据上的输出可信度。

## 5.5 与 VTK 的时间对比实验

时间实验使用统一生成的同构网格族，而不是任意真实模型。这样可以减少模型差异对时间结果的影响，使数据规模和计算时间之间的关系更清晰。实验记录 VTK 并行线程数、VTK 并行时间、系统总时间和 GPU 计算时间。

### 5.5.1 规则网格时间对比

| 数据集 | 点数/单元数 | VTK 并行线程数 | VTK 并行时间/ms | 系统总时间/ms | GPU计算时间/ms |
| --- | --- | --- | --- | --- | --- |
| timing_struct_20x20x20 | 8000 / 6859 | 16 | 2.7573 | 0.9036 | 0.025272 |
| timing_struct_32x32x32 | 32768 / 29791 | 16 | 3.5564 | 1.4336 | 0.043732 |
| timing_struct_48x48x48 | 110592 / 103823 | 16 | 8.2716 | 4.0996 | 0.107692 |

规则网格实验中，系统总时间从 0.9036 ms 增至 4.0996 ms，GPU 计算时间从 0.025272 ms 增至 0.107692 ms。对应 VTK 并行时间从 2.7573 ms 增至 8.2716 ms。系统总时间始终低于 VTK 并行时间，说明有限差分路径在当前测试条件下具有较好的执行效率。

### 5.5.2 非结构化网格时间对比

| 数据集 | 点数/单元数 | VTK 并行线程数 | VTK 并行时间/ms | 系统总时间/ms | GPU计算时间/ms |
| --- | --- | --- | --- | --- | --- |
| timing_uhex_20x20x20 | 8000 / 6859 | 16 | 163.799 | 2.8527 | 0.623896 |
| timing_uhex_32x32x32 | 32768 / 29791 | 16 | 708.650 | 5.3835 | 2.53781 |
| timing_uhex_48x48x48 | 110592 / 103823 | 16 | 3349.36 | 44.8039 | 7.91991 |

非结构化网格实验中，系统总时间从 2.8527 ms 增至 44.8039 ms，GPU 计算时间从 0.623896 ms 增至 7.91991 ms。对应 VTK 并行时间从 163.799 ms 增至 3349.36 ms。结果表明，随着规模增大，OpenGL 实现仍保持明显时间优势。该结论只针对当前硬件、数据和 VTK 并行配置，不应泛化为所有环境下的绝对结论。

## 5.6 数据优化实验

数据优化实验的目标是验证系统对局部随机高频数值扰动的抑制能力。实验在 ShipHull_0 数据集上构造干净场，并叠加高斯扰动作为代理模型。随后运行数据优化模块，比较输入数据和优化后数据的平均相对误差。

| 关联方式 | 输入数据平均相对误差 | 优化后数据平均相对误差 | 改进比 |
| --- | --- | --- | --- |
| POINT | 0.296642 | 0.180858 | 0.610772 |
| CELL | 0.294558 | 0.121921 | 0.414806 |

POINT 关联方式下，平均相对误差由 0.296642 降至 0.180858，改进比为 0.610772。CELL 关联方式下，平均相对误差由 0.294558 降至 0.121921，改进比为 0.414806。两类字段优化后误差均低于输入误差，说明该模块对目标扰动具有稳定抑制效果。

## 5.7 实验讨论

本文实验设计的关键是分层论证。解析场验证回答“算法是否正确”，VTK 一致性回答“工程输出是否可靠”，时间实验回答“当前实现是否具有效率优势”，数据优化实验回答“目标扰动下是否有效”。如果将这些实验混在一起，论文结论会变得不清晰。

实验结果也有边界。第一，解析场主要覆盖线性标量场和线性向量场，能够证明当前路径对线性场的正确性，但不等于覆盖所有复杂物理场。第二，VTK 对照说明系统与成熟工具接近，但 VTK 本身不是解析真值。第三，时间结果依赖当前硬件、驱动、VTK 构建和数据规模。第四，数据优化实验只使用高斯扰动代理局部随机高频扰动，不能扩展为对所有噪声类型有效。

## 5.8 本章参考文献

本章主要参考文献：[3]、[4]、[11]、[12]、[14]、[15]、[21]。
""")


write("06_总结与展望.md", r"""
# 第六章 总结与展望

## 6.1 本章写法说明

本科论文的总结与展望不应简单重复摘要，也不应写成空泛口号。根据本科论文抽检对逻辑构建和专业能力的要求[14]，本章应回扣研究目标，说明完成了什么、实验支持什么、还存在哪些不足、后续如何改进。对于工程型系统论文，结论必须与实现和实验对应，不能超过实验数据能够支撑的范围。

## 6.2 工作总结

本文围绕 CAE 后处理阶段的数据预处理问题，完成了一套基于 OpenGL 的原型系统。系统以 VTK 数据读写为基础，以统一内部数据结构为核心，以 OpenGL Compute Shader 为计算执行手段，实现了规则网格梯度计算、非结构化网格梯度计算、局部随机高频扰动优化、结果写回和 VTK 导出。

在梯度计算方面，本文将技术路线收敛为规则网格有限差分法和非结构化网格形函数导数法。规则网格路径利用网格逻辑维度和相邻采样点计算差分，非结构化网格路径利用单元形函数导数和几何映射计算梯度。解析场验证表明，两条路径在对应数据集上均达到 \(10^{-7}\) 量级误差。

在工程一致性方面，本文将系统结果与 vtkGradientFilter 进行对比。点数据真实字段误差达到 \(10^{-8}\) 量级，单元数据真实字段误差主要处于 \(10^{-7}\) 到 \(10^{-6}\) 量级。该结果说明当前系统输出与 VTK 参考结果具有较高一致性。

在性能方面，本文使用统一生成的规则网格族和非结构化六面体网格族进行时间实验。结果显示，系统总时间和 GPU 计算时间均明显低于当前测试条件下的 VTK 并行时间，说明 OpenGL 实现具有较好的并行效率。

在数据优化方面，本文将模块定位为面向一类局部随机高频数值扰动的抑制与边缘保持处理。实验使用高斯扰动作为代理模型。结果显示，点数据和单元数据优化后平均相对误差均低于输入误差，说明该模块在目标场景下有效。

```mermaid
flowchart TD
    A["本文成果"] --> B["统一数据对象"]
    A --> C["规则网格有限差分"]
    A --> D["非结构化网格形函数导数"]
    A --> E["图双边滤波与多尺度融合"]
    A --> F["实验与导出闭环"]
    F --> G["解析场验证"]
    F --> H["VTK 一致性"]
    F --> I["时间对比"]
    F --> J["高斯扰动优化"]
```

图 6-1 总结了本文完成的主要内容。

## 6.3 本文特点

本文第一个特点是系统链路完整。项目不是单独实现一个公式或一个界面，而是形成了从 VTK 数据输入、内部数据转换、GPU 计算、结果写回到 VTK 导出的闭环。这种闭环能够支撑答辩时的功能演示和实验复现。

第二个特点是实验口径清晰。解析场验证、VTK 一致性、时间对比和数据优化实验承担不同论证任务。这样的组织方式符合工程型论文写法，也符合本科论文对逻辑构建和专业能力的要求[14][15]。

第三个特点是结论边界明确。本文没有把系统描述为完整 CAE 后处理平台，也没有把数据优化模块描述为通用噪声处理器，而是严格围绕当前实现和实验数据展开。这种收敛后的表述更符合本科毕业设计的实际完成情况。

## 6.4 不足之处

当前系统仍然是原型系统。首先，非结构化网格单元类型支持范围有限，目前主要覆盖一阶三角形、四边形、四面体和六面体。真实工程数据中可能存在更多高阶单元、混合单元或特殊单元，后续仍需扩展。

其次，数据优化实验使用高斯扰动作为代理模型，能够说明模块对一类局部随机高频扰动有效，但不能覆盖所有真实 CAE 扰动来源。真实工程结果中的误差可能与求解器、网格质量、边界条件、材料模型和后处理恢复方式有关，需要更丰富实验进一步验证。

第三，系统 GUI 目前主要服务于基础演示和结果导出，还不具备完整可视分析软件的交互能力。若要发展为更完整工具，还需要增加渲染控制、字段管理、参数调节、批处理配置和异常提示等功能。

第四，本文时间实验虽然显示当前系统具有优势，但实验环境仍有限。不同 GPU、不同驱动、不同 VTK 并行后端和不同数据规模都可能影响结果。后续若要形成更强工程结论，需要在更多环境中复现实验。

## 6.5 后续展望

后续工作可以从四个方面展开。第一，扩展更多单元类型和更复杂字段类型，使系统能够处理更广泛的工程数据。第二，完善数据优化实验，引入更接近真实 CAE 误差来源的扰动模型和评价指标。第三，增加自动化测试和批处理能力，使实验结果更容易复现。第四，将预处理模块与后续可视化模块更紧密结合，形成更完整的后处理数据准备流程。

从技术发展角度看，VTK-m 等工作说明科学可视化框架正在面向大规模线程架构演进[7]。本文虽然没有使用 VTK-m，但其思路与“将可视化和数据处理算法映射到并行硬件”这一趋势一致。后续可以继续比较 OpenGL、CUDA、VTK-m 等不同技术路线的适用性。

## 6.6 本章参考文献

本章主要参考文献：[7]、[14]、[15]、[16]、[21]。
""")


write("07_参考文献.md", r"""
# 参考文献

[1] Khronos Group. Compute Shader - OpenGL Wiki[EB/OL]. https://www.khronos.org/opengl/wiki/Compute_Shader.

[2] Khronos Group. Shader Storage Buffer Object - OpenGL Wiki[EB/OL]. https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object.

[3] Kitware. vtkGradientFilter Class Reference[EB/OL]. https://vtk.org/doc/nightly/html/classvtkGradientFilter.html.

[4] ParaView Documentation. Understanding Data: VTK data model[EB/OL]. https://docs.paraview.org/en/latest/UsersGuide/understandingData.html.

[5] Qt Group. Qt Widgets Documentation[EB/OL]. https://doc.qt.io/qt-5/qtwidgets-index.html.

[6] Schroeder W, Martin K, Lorensen B. The Visualization Toolkit[M]. Kitware.

[7] Moreland K, Sewell C, Usher W, et al. VTK-m: Accelerating the Visualization Toolkit for Massively Threaded Architectures[J]. IEEE Computer Graphics and Applications, 2016, 36(3): 48-58. https://doi.org/10.1109/MCG.2016.48.

[8] Owens J D, Luebke D, Govindaraju N, et al. A Survey of General-Purpose Computation on Graphics Hardware[J]. Computer Graphics Forum, 2007, 26(1): 80-113. https://doi.org/10.1111/j.1467-8659.2007.01012.x.

[9] Hughes T J R. The Finite Element Method: Linear Static and Dynamic Finite Element Analysis[M]. Dover Publications.

[10] Zienkiewicz O C, Zhu J Z. The Superconvergent Patch Recovery and a Posteriori Error Estimates. Part 1[J]. International Journal for Numerical Methods in Engineering, 1992.

[11] Tomasi C, Manduchi R. Bilateral Filtering for Gray and Color Images[C]//Proceedings of the IEEE International Conference on Computer Vision. 1998: 839-846. https://doi.org/10.1109/ICCV.1998.710815.

[12] Fleishman S, Drori I, Cohen-Or D. Bilateral Mesh Denoising[J]. ACM Transactions on Graphics, 2003, 22(3): 950-953. https://doi.org/10.1145/882262.882368.

[13] Perona P, Malik J. Scale-space and edge detection using anisotropic diffusion[J]. IEEE Transactions on Pattern Analysis and Machine Intelligence, 1990, 12(7): 629-639.

[14] 教育部. 本科毕业论文（设计）抽检办法（试行）[EB/OL]. https://www.moe.gov.cn/srcsite/A11/s7057/202101/t20210107_509019.html.

[15] 上海市教育委员会. 上海市本科毕业论文（设计）抽检实施细则（试行）[EB/OL]. https://service.shanghai.gov.cn/XingZhengWenDangKuJyh/XZGFDetails.aspx?docid=REPORT_NDOC_007422.

[16] 陕西省人民政府. 陕西省本科毕业论文（设计）抽检实施细则（试行）[EB/OL]. https://www.shaanxi.gov.cn/zfxxgk/fdzdgknr/zcwj/gfxwj/202209/t20220907_2250362.html.

[17] 国家市场监督管理总局, 国家标准化管理委员会. GB/T 7713.1-2025 信息与文献 编写规则 第1部分：学位论文[S].

[18] 国家市场监督管理总局, 国家标准化管理委员会. GB/T 7714-2015 信息与文献 参考文献著录规则[S].

[19] 高洪臣. 基于 Qt 的电梯智能卡管理系统上位机设计（本科毕业论文）[D]. 燕山大学, 2016. https://www.researchgate.net/publication/351435511.

[20] 青岛理工大学本科毕业设计论文公开示例：基于虚幻引擎的 2.5D 游戏开发与运行机制研究[EB/OL]. https://ikesallows.top/wp-content/uploads/2024/03/.

[21] University of Cambridge Department of Computer Science and Technology. The Dissertation: Part II Projects[EB/OL]. https://www.cst.cam.ac.uk/teaching/part-ii/projects/dissertation.

[22] 国内 CAE 后处理与科学可视化应用研究文献[DB/OL]. https://kns.cnki.net/.

[23] 国内基于 GPU 的有限元或 CAE 后处理可视化研究文献[DB/OL]. https://kns.cnki.net/.

[24] 国内图像、点云、网格和科学数据保边平滑研究文献[DB/OL]. https://kns.cnki.net/.
""")


write("README.md", r"""
# wendang 章节文件说明

本文件夹按章节拆分论文 Markdown。当前版本根据本科论文抽检要求、公开本科工程型论文结构、OpenGL/VTK/ParaView 文档、有限元形函数导数理论、双边滤波与网格去噪文献进行了修改。

本轮修改包含：

- 每章加入“本章参考文献”或“摘要撰写依据”。
- 第 2、4、5 章加入 LaTeX 公式。
- 第 1、2、3、4、5、6 章加入 Mermaid 图或表格。
- 第 5 章继续使用 `毕设进展.docx` 中的实验数据。
- 正文不写已废弃方案。
""")

print("enhanced wendang chapters")
