from __future__ import annotations

import csv
import re
import shutil
import statistics
import textwrap
from pathlib import Path

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt


ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "doc"
RESULTS = ROOT / "results"
PY_DATE = "2026-05-04"

OUT_COMPLETION = DOC / "课题调研与答辩判断.md"
OUT_ARGUMENT = DOC / "立题论证书-修订版.md"
OUT_PROPOSAL = DOC / "开题报告-修订版.md"
OUT_MIDTERM = DOC / "中期进展报告-修订版.md"
OUT_OUTLINE = DOC / "本科毕业论文详细大纲.md"
OUT_THESIS_MD = DOC / "基于OpenGL的CAE软件数据预处理方法研究与实践_本科毕业论文.md"
OUT_THESIS_DOCX = DOC / "基于OpenGL的CAE软件数据预处理方法研究与实践_本科毕业论文.docx"

BASE_DOCX_CANDIDATES = [
    DOC / "generated" / "基于OpenGL的CAE软件数据预处理方法研究与实践_论文终稿_更新版.docx",
    DOC / "generated" / "基于OpenGL的CAE软件数据预处理方法研究与实践_论文终稿.docx",
]


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def f(row: dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        return float(row.get(key, "") or default)
    except ValueError:
        return default


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def fmt(value: float, digits: int = 3) -> str:
    if abs(value) >= 10000 or (0 < abs(value) < 0.001):
        return f"{value:.{digits}e}"
    return f"{value:.{digits}f}"


def build_metrics() -> dict[str, object]:
    timing_rows = []
    for path in sorted((RESULTS / "timing").glob("*.csv")):
        rows = read_csv_rows(path)
        if rows:
            row = rows[0]
            row["_file"] = path.name
            timing_rows.append(row)

    timing_table = []
    speed_single = []
    speed_parallel = []
    for row in timing_rows:
        gl = f(row, "result_wall_avg_ms")
        vtk_single = f(row, "ambient_vtk_single_avg_ms")
        vtk_parallel = f(row, "ambient_vtk_parallel_avg_ms")
        s1 = vtk_single / gl if gl else 0.0
        sp = vtk_parallel / gl if gl else 0.0
        speed_single.append(s1)
        speed_parallel.append(sp)
        timing_table.append(
            [
                row["_file"].replace(".csv", ""),
                row.get("association", ""),
                row.get("result_tuples", ""),
                fmt(gl),
                fmt(f(row, "result_gpu_avg_ms")),
                fmt(vtk_single),
                fmt(vtk_parallel),
                fmt(s1, 2),
                fmt(sp, 2),
                fmt(f(row, "ambient_nmae"), 2),
            ]
        )

    grad_rows = []
    for path in sorted((RESULTS / "gradient").glob("*.csv")):
        grad_rows.extend(read_csv_rows(path))
    grad_success = [row for row in grad_rows if row.get("success") == "1"]

    point_ms = [row for row in read_csv_rows(RESULTS / "mul" / "multiscale_report+point.csv") if row.get("success") == "1"]
    cell_ms = [row for row in read_csv_rows(RESULTS / "mul" / "multiscale_report+cell.csv") if row.get("success") == "1"]
    all_ms = point_ms + cell_ms

    def ms_summary(rows: list[dict[str, str]]) -> dict[str, float | int]:
        return {
            "total": len(rows),
            "mae_improved": sum(1 for r in rows if f(r, "mae_improvement_ratio", 1.0) < 1.0),
            "rough_improved": sum(1 for r in rows if f(r, "roughness_ratio", 1.0) < 1.0),
            "avg_wall": mean([f(r, "wall_avg_ms") for r in rows]),
            "avg_gpu": mean([f(r, "gpu_avg_ms") for r in rows]),
            "avg_mae_ratio": mean([f(r, "mae_improvement_ratio") for r in rows]),
            "avg_rmse_ratio": mean([f(r, "rmse_improvement_ratio") for r in rows]),
            "avg_rough_ratio": mean([f(r, "roughness_ratio") for r in rows]),
        }

    ms_noise_table = []
    for assoc, rows in [("POINT", point_ms), ("CELL", cell_ms)]:
        for noise in ["gaussian", "grf", "impulse", "mixed"]:
            group = [r for r in rows if r.get("noise") == noise]
            if not group:
                continue
            ms_noise_table.append(
                [
                    assoc,
                    noise,
                    str(len(group)),
                    str(sum(1 for r in group if f(r, "mae_improvement_ratio", 1.0) < 1.0)),
                    fmt(mean([f(r, "mae_improvement_ratio") for r in group])),
                    fmt(mean([f(r, "rmse_improvement_ratio") for r in group])),
                    fmt(mean([f(r, "roughness_ratio") for r in group])),
                    fmt(mean([f(r, "wall_avg_ms") for r in group])),
                    fmt(mean([f(r, "gpu_avg_ms") for r in group])),
                ]
            )

    return {
        "timing_rows": timing_rows,
        "timing_table": timing_table,
        "timing_count": len(timing_rows),
        "timing_avg_speed_single": mean(speed_single),
        "timing_avg_speed_parallel": mean(speed_parallel),
        "grad_total": len(grad_rows),
        "grad_success": len(grad_success),
        "ms_point": ms_summary(point_ms),
        "ms_cell": ms_summary(cell_ms),
        "ms_all": ms_summary(all_ms),
        "ms_noise_table": ms_noise_table,
    }


def md_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(x) for x in row) + " |")
    return "\n".join(lines)


def clean_md(text: str) -> str:
    text = text.strip()
    text = re.sub(r"(?m)^ {8}", "", text)
    text = re.sub(r"(?m)^ {4}(?=#)", "", text)
    return text + "\n"


LITERATURE = [
    ("EN", "OpenGL/Khronos", "Khronos OpenGL Wiki. Compute Shader", "OpenGL 计算着色器的执行模型、工作组和调度方式，是本文将梯度与滤波迁移到 GPU 的底层依据。", "https://www.khronos.org/opengl/wiki/Compute_Shader"),
    ("EN", "OpenGL/Khronos", "Khronos OpenGL Wiki. Shader Storage Buffer Object", "说明 SSBO 的大容量读写缓冲机制，对应项目中 VBO/SSBO 风格的扁平数组和邻接表传输。", "https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object"),
    ("EN", "VTK", "VTK documentation. vtkGradientFilter Class Reference", "给出 VTK 梯度滤波器的官方语义，是项目进行工程一致性对照的主要参考。", "https://vtk.org/doc/nightly/html/classvtkGradientFilter.html"),
    ("EN", "ParaView/VTK", "ParaView User's Guide. Understanding Data", "说明结构网格、非结构网格、点数据和单元数据等科学可视化数据模型。", "https://docs.paraview.org/en/v5.10.0/UsersGuide/understandingData.html"),
    ("EN", "Qt", "Qt Documentation. Qt Widgets", "对应项目 GUI 层的技术基础。", "https://doc.qt.io/qt-5/qtwidgets-index.html"),
    ("EN", "VTK-m", "Moreland K. et al. VTK-m: Accelerating the Visualization Toolkit for Massively Threaded Architectures", "体现科学可视化框架向数据并行和异构并行演进的趋势，可用于论文背景与相关工作。", "https://m.vtk.org/"),
    ("EN", "Mesh Denoising", "Fleishman S., Drori I., Cohen-Or D. Bilateral Mesh Denoising. ACM TOG, 2003", "图/网格双边滤波与特征保持平滑的代表性文献，是数据优化模块的思想来源。", "https://doi.org/10.1145/882262.882368"),
    ("EN", "Image Filtering", "Tomasi C., Manduchi R. Bilateral Filtering for Gray and Color Images. ICCV, 1998", "双边滤波的经典图像处理来源，说明同时考虑空间距离和数值差异的权重思想。", "https://doi.org/10.1109/ICCV.1998.710815"),
    ("EN", "Anisotropic Diffusion", "Perona P., Malik J. Scale-space and edge detection using anisotropic diffusion. IEEE TPAMI, 1990", "解释保边平滑和尺度空间分析的理论背景。", "https://doi.org/10.1109/34.56205"),
    ("EN", "Mesh Fairing", "Taubin G. A signal processing approach to fair surface design. SIGGRAPH, 1995", "可支撑多尺度、平滑和几何信号处理的相关工作。", "https://doi.org/10.1145/218380.218473"),
    ("EN", "Gradient Recovery", "Zienkiewicz O. C., Zhu J. Z. The superconvergent patch recovery and a posteriori error estimates. Part 1. IJNME, 1992", "有限元后处理中的梯度/应力恢复经典方法，可用于讨论形函数导数与恢复型方法的关系。", "https://doi.org/10.1002/nme.1620330702"),
    ("EN", "Gradient Recovery", "Zienkiewicz O. C., Zhu J. Z. The superconvergent patch recovery and a posteriori error estimates. Part 2. IJNME, 1992", "补充说明后验误差估计与恢复场的工程意义。", "https://doi.org/10.1002/nme.1620330703"),
    ("EN", "Unstructured Mesh", "Barth T. J., Jespersen D. C. The design and application of upwind schemes on unstructured meshes. AIAA, 1989", "非结构网格梯度重构和有限体积格式的早期重要工作。", "https://doi.org/10.2514/6.1989-366"),
    ("EN", "Unstructured Mesh", "Mavriplis D. J. Revisiting the least-squares procedure for gradient reconstruction on unstructured meshes", "可作为最小二乘梯度重构的背景材料。", "https://ntrs.nasa.gov/citations/20040070704"),
    ("EN", "Finite Element", "Hughes T. J. R. The Finite Element Method: Linear Static and Dynamic Finite Element Analysis", "有限元形函数、单元映射和导数计算的理论基础。", "https://store.doverpublications.com/products/9780486411811"),
    ("EN", "Visualization", "Schroeder W., Martin K., Lorensen B. The Visualization Toolkit", "VTK 和科学可视化管线的基础参考书。", "https://www.vtk.org/vtk-textbook/"),
    ("EN", "Scientific Visualization", "Ahrens J., Geveci B., Law C. ParaView: An End-User Tool for Large Data Visualization", "说明 ParaView 作为大规模科学可视化工具的定位。", "https://www.paraview.org/publications/"),
    ("EN", "GPU Computing", "Owens J. D. et al. A Survey of General-Purpose Computation on Graphics Hardware. Computer Graphics Forum, 2007", "GPGPU 计算的综述，可支撑从渲染管线到通用数值计算的技术背景。", "https://doi.org/10.1111/j.1467-8659.2007.01012.x"),
    ("EN", "GPU Computing", "Harris M. Mapping Computational Concepts to GPUs. GPU Gems 2, 2005", "解释 GPU 并行计算的早期编程范式。", "https://developer.nvidia.com/gpugems/gpugems2/part-vi-simulation-and-numerical-algorithms"),
    ("CN", "学位论文规范", "国家市场监督管理总局、中国国家标准化管理委员会. GB/T 7713.1-2025 信息与文献 编写规则 第1部分：学位论文", "现行论文编写标准更新方向；2025 发布、2026 年实施，学校若采用新版模板应优先服从。", "https://openstd.samr.gov.cn/"),
    ("CN", "参考文献规范", "GB/T 7714-2015 信息与文献 参考文献著录规则", f"截至 {PY_DATE}，GB/T 7714-2025 尚未到 2026-07-01 实施日期，若学校未另行规定，参考文献仍可按 2015 版执行。", "https://openstd.samr.gov.cn/"),
    ("CN", "教育部要求", "教育部. 普通高等学校本科毕业论文（设计）抽检办法（试行）", "强调本科论文抽检关注选题意义、写作安排、逻辑构建、专业能力和学术规范。", "http://www.moe.gov.cn/srcsite/A08/s7056/202101/t20210107_509524.html"),
    ("CN", "CAE/可视化", "朱伯芳等. 有限元法原理与应用相关教材", "可作为中文有限元理论、单元形函数和后处理解释的基础来源。", "https://book.douban.com/subject_search?search_text=%E6%9C%89%E9%99%90%E5%85%83%E6%B3%95"),
    ("CN", "科学计算可视化", "国内 CAE/科学可视化与 VTK/ParaView 应用研究论文", "用于补充中文场景下工程仿真后处理、VTK 数据模型和可视化管线应用。", "https://kns.cnki.net/"),
    ("CN", "GPU/可视化", "国内基于 GPU 的有限元或 CAE 后处理可视化研究", "用于支撑 GPU 加速后处理在国内工程软件研究中的应用价值。", "https://kns.cnki.net/"),
    ("CN", "非结构网格", "国内非结构网格梯度重构、最小二乘重构和有限体积数值格式研究", "用于解释 WLS/AWLS 的理论来源和适用边界。", "https://kns.cnki.net/"),
    ("CN", "滤波/去噪", "国内点云、网格和科学数据双边滤波/多尺度去噪研究", "用于支撑本文数据优化模块的中文相关工作。", "https://kns.cnki.net/"),
]


def literature_markdown() -> str:
    rows = []
    for idx, (lang, area, cite, use, url) in enumerate(LITERATURE, start=1):
        rows.append([str(idx), lang, area, cite, use, f"[链接]({url})"])
    return md_table(["序号", "语种", "方向", "文献/资料", "与本课题关系", "来源"], rows)


def build_completion_report(metrics: dict[str, object]) -> str:
    timing_rows = metrics["timing_table"]
    ms_all = metrics["ms_all"]
    ms_point = metrics["ms_point"]
    ms_cell = metrics["ms_cell"]
    return clean_md(textwrap.dedent(
        f"""
        # 课题调研与答辩支撑判断

        生成日期：{PY_DATE}

        ## 一、课题事实基础

        本项目的实际题目建议统一为“基于 OpenGL 的 CAE 软件数据预处理方法研究与实践”。从代码和实验结果看，项目已经形成了清晰的工程闭环：VTK legacy 数据读入，经 `VTKDataConverter` 转换为内部 `DataObject`，再由 `CAEProcessingFacade` 统一调度 OpenGL 计算着色器完成梯度计算和多尺度数据优化，最后写回内部数组、导出 VTK 或在 Qt GUI 中执行交互操作。

        当前核心实现包括：规则网格有限差分梯度、非结构网格形函数导数梯度、自适应加权最小二乘备用路径、图双边滤波、多尺度平滑/细节分解/融合、VTK 导入导出、Qt 界面、梯度测试程序、多尺度测试程序和字段评价程序。项目不应再表述为“完整 CAE 后处理软件”或“完整 ParaView 替代品”，而应表述为“面向 CAE 后处理阶段的 GPU 数据预处理原型系统”。

        ## 二、与本科毕设要求的匹配判断

        结论：可以算完成本科毕业设计，也可以支撑答辩。这个判断的依据不是已有文档本身，而是代码主线、实验链路和量化数据已经足够支撑“问题提出、方法设计、系统实现、实验验证、局限分析”五个答辩必需环节。

        支撑点包括：

        1. 工程完整性：项目包含 C++17、Qt、VTK、OpenGL 4.6、GLSL compute shader 等多模块实现，不只是单个算法 demo。
        2. 算法工作量：实现了规则网格 FD、非结构网格形函数导数、AWLS 支撑数据、图双边滤波和多尺度融合，算法复杂度达到本科毕设上限附近。
        3. 实验可复现：`results/gradient` 中共有 {metrics["grad_total"]} 条梯度实验记录，其中成功记录 {metrics["grad_success"]} 条；`results/timing` 中有 {metrics["timing_count"]} 组规模化时间对照；`results/mul` 中有点数据和单元数据多尺度优化实验。
        4. 性能结论明确：规模化时间实验中，系统相对 VTK 单线程平均加速约 {fmt(metrics["timing_avg_speed_single"], 2)} 倍，相对当前 VTK 并行配置平均加速约 {fmt(metrics["timing_avg_speed_parallel"], 2)} 倍。应在论文中同时报告墙钟时间和 GPU 时间，避免只拿内核时间夸大性能。
        5. 数据优化结论有边界：多尺度实验共 {ms_all["total"]} 个成功案例，其中 MAE 改善 {ms_all["mae_improved"]} 个，粗糙度改善 {ms_all["rough_improved"]} 个；平均 MAE 比为 {fmt(ms_all["avg_mae_ratio"])}，平均粗糙度比为 {fmt(ms_all["avg_rough_ratio"])}。对 gaussian、grf、mixed 噪声更有效，对 impulse 异常值不应夸大。

        ## 三、答辩时应主动收缩的口径

        1. 不要说“实现了完整 CAE 后处理系统”，应说“实现了面向后处理数据准备的 GPU 预处理原型系统”。
        2. 不要说“全面替代 VTK/ParaView”，应说“以 VTK/ParaView 为数据读写、渲染观察和参考基线，重点验证核心预处理算法的 GPU 实现”。
        3. 不要把“与 VTK 接近”说成“绝对正确”。解析 benchmark 才支撑正确性，真实字段与 VTK 对照支撑工程一致性。
        4. 不要把多尺度优化说成“通用降噪”。它更适合局部随机高频扰动，对脉冲异常值效果有限。
        5. 中期报告中暴露的 WLS 曲面误差问题，最终应解释为“促使后续引入形函数导数法和曲面 intrinsic 评价口径”，而不是回避。

        ## 四、核心实验摘要

        {md_table(["实验文件", "关联", "样本数", "系统墙钟/ms", "GPU/ms", "VTK单线程/ms", "VTK并行/ms", "单线程加速", "并行加速", "NRMAE"], timing_rows)}

        ## 五、多尺度优化摘要

        点数据：成功 {ms_point["total"]} 例，MAE 改善 {ms_point["mae_improved"]} 例，粗糙度改善 {ms_point["rough_improved"]} 例，平均 MAE 比 {fmt(ms_point["avg_mae_ratio"])}，平均粗糙度比 {fmt(ms_point["avg_rough_ratio"])}。

        单元数据：成功 {ms_cell["total"]} 例，MAE 改善 {ms_cell["mae_improved"]} 例，粗糙度改善 {ms_cell["rough_improved"]} 例，平均 MAE 比 {fmt(ms_cell["avg_mae_ratio"])}，平均粗糙度比 {fmt(ms_cell["avg_rough_ratio"])}。

        {md_table(["关联", "噪声", "案例数", "MAE改善数", "平均MAE比", "平均RMSE比", "平均粗糙度比", "墙钟/ms", "GPU/ms"], metrics["ms_noise_table"])}

        ## 六、文献调研清单

        {literature_markdown()}
        """
    ))


def build_argument_doc() -> str:
    return clean_md(textwrap.dedent(
        """
        # 立题论证书（修订版）

        ## 课题名称

        基于 OpenGL 的 CAE 软件数据预处理方法研究与实践

        ## 一、课题背景与研究意义

        CAE 后处理阶段需要把仿真求解器输出的网格、节点场、单元场和派生物理量转化为可分析、可渲染、可比较的数据结果。梯度场计算、局部平滑、特征保持和多尺度结果组织是后处理中的基础环节，直接影响应力集中区识别、物理场变化趋势分析以及后续颜色映射、等值面观察和 ParaView 等工具中的可视化效果。

        传统工作流通常依赖 VTK、ParaView 或商业 CAE 软件在 CPU 侧完成数据派生和过滤。对于本科毕设而言，直接重建完整 CAE 后处理平台并不现实，也不是本课题的重点。本课题聚焦于“数据预处理”这一较明确的技术切面：研究如何把规则网格和非结构网格转换为 GPU 友好的内部表示，利用 OpenGL Compute Shader 与 SSBO 风格缓冲完成梯度计算和结果场优化，并通过 VTK/ParaView 对照验证工程可用性。

        该选题的意义在于：一是将计算机图形学中的 GPU 管线机制用于 CAE 数值数据处理，体现计算机专业综合应用能力；二是将工程仿真数据、科学可视化和并行计算结合起来，具有较强的实践性；三是形成可运行、可测试、可导出的原型系统，为后续可视化模块提供更稳定的数据基础。

        ## 二、研究目标

        本课题的目标修订为以下四项：

        1. 构建面向规则网格和非结构网格的统一内部数据表示，支持点坐标、单元连接、单元类型、点数据、单元数据和邻域关系。
        2. 基于 OpenGL Compute Shader 实现核心数据预处理算法，包括规则网格有限差分梯度、非结构网格形函数导数梯度和多尺度图滤波融合。
        3. 设计统一门面接口，打通 VTK 数据读入、GPU 计算、结果写回、VTK 导出和 Qt GUI 基础交互。
        4. 建立实验评价体系，从解析 benchmark、真实字段与 VTK 对照、规模化时间实验和多尺度优化统计四个层面验证系统。

        ## 三、主要研究内容

        1. 数据模型与转换：使用 VTK 读入 legacy VTK 数据，将结构网格和非结构网格转换为 `DataObject`，构建点邻域、点-单元关联、单元邻域和 CSR 风格索引数组。
        2. 梯度计算模块：规则网格采用有限差分；非结构网格主线采用基于单元形函数导数的梯度恢复，并保留自适应 WLS 作为补充路线。点数据和单元数据分别设计处理路径。
        3. 数据优化模块：基于图双边滤波构建平滑尺度层，通过相邻尺度差分得到细节层，再按权重进行多尺度融合，以抑制局部随机高频扰动并尽量保持主要结构。
        4. 系统集成：通过 `CAEProcessingFacade` 统一处理数据加载、算法分派、GPU 上下文切换、计时、结果命名和导出；通过 Qt GUI 提供打开文件、选择字段、计算梯度、数据优化和导出结果等操作。
        5. 实验验证：以 VTK/ParaView 作为参考工作流，分别验证正确性、工程一致性、性能和可视化效果。

        ## 四、技术路线

        技术路线为：VTK 文件输入 -> 数据转换 -> 内部扁平数组与邻域图 -> OpenGL SSBO 上传 -> Compute Shader 执行 -> 结果回读 -> 内部数组写回 -> VTK 导出或 GUI 展示。该路线避免把论文写成单纯界面开发，也避免把实验停留在离散算法公式层面。

        ## 五、预期成果

        1. 一套可运行的 OpenGL/Qt/VTK CAE 数据预处理原型系统源码。
        2. 梯度计算、数据优化和字段评价测试程序。
        3. 可复现的 CSV 实验报告、VTK 导出结果和可视化图片。
        4. 系统设计、实验说明、用户使用说明和本科毕业论文。

        ## 六、创新性与可行性

        本课题的创新性不宜夸大为理论算法突破，而应定位为工程实现与实验组织创新：将 CAE 后处理中的多个常用数据准备环节统一到 OpenGL 计算管线中；同时区分解析真值验证、VTK 工程对照和多尺度优化指标，形成较严谨的实验链路。可行性来自项目已具备 VTK 数据转换、OpenGL 计算上下文、GLSL 着色器、测试程序和结果导出的基础。
        """
    ))


def build_proposal_doc() -> str:
    return clean_md(textwrap.dedent(
        """
        # 开题报告（修订版）

        ## 一、背景说明

        计算机辅助工程（CAE）在结构仿真、热分析、流体分析和多物理场仿真中被广泛使用。求解完成后，工程人员通常需要在后处理阶段观察物理场分布、识别梯度变化、分析局部异常和生成可视化图像。数据预处理是后处理链路中承上启下的环节，其任务包括数据读写、字段转换、梯度场计算、噪声抑制、多尺度结果组织和结果导出。

        当前主流开源工具 VTK/ParaView 提供了成熟的数据模型和过滤器，但当毕业设计需要研究底层实现、并行机制和算法组织时，仅调用现成过滤器不足以体现工作量。本课题拟在保留 VTK 数据读写和参考对照能力的基础上，研究如何使用 OpenGL Compute Shader 实现核心预处理算法，并建立一个轻量化、可验证、可扩展的 CAE 数据预处理原型。

        ## 二、课题目标

        1. 完成 VTK 数据到内部统一表示的转换，支持规则网格和非结构网格。
        2. 实现 GPU 梯度计算模块：规则网格采用有限差分，非结构网格采用形函数导数法，并保留 WLS/AWLS 作为补充研究路径。
        3. 实现 GPU 数据优化模块：基于图双边滤波构建多尺度平滑层，分离细节并融合重建。
        4. 开发基础 Qt GUI 和测试程序，完成数据加载、字段选择、梯度计算、优化处理、导出与日志显示。
        5. 设计实验体系，使用解析 benchmark、VTK 对照、时间统计和多尺度指标评价系统。

        ## 三、主要内容

        ### 1. 需求分析

        调研 CAE 后处理数据预处理需求，明确本课题只解决“数据准备与派生计算”问题，不承诺实现完整商业 CAE 后处理软件。重点需求包括：多类型 VTK 数据读入、点/单元字段管理、梯度派生、局部平滑、多尺度融合、结果导出和可复现实验。

        ### 2. 算法与 GPU 并行实现

        研究有限差分、形函数导数、加权最小二乘、图双边滤波和多尺度融合方法。通过 SSBO/缓冲对象组织点坐标、字段值、邻域偏移和单元连接，在 Compute Shader 中按点或按单元并行执行。对不同网格类型采用不同算法分派，避免用单一方法覆盖所有场景。

        ### 3. 数据预处理框架设计

        设计 `DataObject` 作为内部数据模型，设计 `CAEProcessingFacade` 作为统一门面，设计 `GLGradientEngine` 和 `GLFilterEngine` 作为计算执行层，设计 `VTKDataConverter` 完成 VTK 与内部表示的双向桥接。界面层只做操作入口和结果展示，核心逻辑由测试程序和门面层共享。

        ### 4. 实验与评价

        梯度实验分为解析真值验证和 VTK 工程对照两类。数据优化实验使用可控干净场加代理扰动的方式，比较输入场和融合场相对真值的 MAE、RMSE、NRMSE 和粗糙度变化。性能实验同时报告墙钟时间、GPU 时间、VTK 单线程时间和 VTK 并行时间。

        ## 四、工作方案

        第一阶段：查阅 CAE 后处理、VTK/ParaView、OpenGL Compute Shader、非结构网格梯度重构和图滤波文献，明确题目边界。

        第二阶段：搭建 C++17、Qt、VTK、OpenGL 开发环境，实现数据读入、内部表示和基本 GUI。

        第三阶段：实现梯度计算模块，先完成规则网格有限差分，再完成非结构网格形函数导数法，并与 vtkGradientFilter 进行对比。

        第四阶段：实现图双边滤波和多尺度融合模块，完成噪声代理实验和可视化导出。

        第五阶段：整理实验数据、图表和论文，形成答辩材料。

        ## 五、进度安排

        第 1-2 周：文献调研、需求收缩、开题准备。

        第 3-4 周：学习 Qt、VTK 和 OpenGL Compute Shader，完成数据模型初步设计。

        第 5-6 周：实现 VTK 数据转换、邻域图构建和 OpenGL 计算上下文。

        第 7-8 周：实现规则网格有限差分和非结构网格初版梯度算法，完成初步测试。

        第 9-10 周：根据测试问题完善非结构网格梯度主线，引入形函数导数法和曲面评价口径；实现数据优化模块。

        第 11-12 周：完成 GUI、导出、批量实验和 VTK/ParaView 对照。

        第 13-14 周：撰写论文、统一过程文档口径、准备答辩。
        """
    ))


def build_midterm_doc() -> str:
    return clean_md(textwrap.dedent(
        """
        # 中期进展报告（修订版）

        ## 一、目前工作进展及取得的成果

        本课题面向 CAE 后处理阶段的数据预处理问题，围绕“统一数据表示、GPU 梯度计算、数据优化和实验验证”开展设计与实现。到中期阶段，系统已经完成基础架构搭建，并形成了可以继续扩展的主流程。

        在数据结构方面，已设计内部 `DataObject`，能够保存点坐标、单元连接、单元类型、单元中心、点数据、单元数据和邻域关系。对于非结构网格，系统构建了点邻域、点所属单元列表和单元邻域；邻域采用偏移数组加索引数组的 CSR 风格组织，便于后续上传到 GPU 端并进行连续访问。这一设计使规则网格和非结构网格能够被统一纳入测试程序和门面接口。

        在数据转换方面，已实现 VTK 数据集到内部数据结构的转换，并保留由内部结构重新导出 VTK 的设计路线。该功能保证系统可以使用 VTK/ParaView 作为数据来源、对照基线和结果观察工具，同时把核心算法从 VTK 过滤器调用中剥离出来。

        在梯度计算方面，已完成规则网格有限差分 GPU 原型，内部点采用中心差分，边界点采用前向或后向差分。对于非结构网格，已实现基于局部邻域的加权最小二乘初版方案，并通过测试发现其在规则体网格和局部邻域较均匀的数据上表现较好，但在曲面型、壳状或近共面的复杂非结构网格上存在稳定性不足。

        在实验方面，已初步搭建梯度测试程序，可以读取数据、选择点数据或单元数据、调用系统梯度计算、调用 VTK 参考结果并输出误差和时间指标。初步结果表明 GPU 路线在计算时间上具有明显潜力，但非结构网格精度不能只依赖单一 WLS 路线，需要在后续阶段引入更符合有限元单元语义的形函数导数法。

        ## 二、存在问题及拟解决措施

        当前主要问题集中在非结构网格梯度精度和结论口径上。WLS 方法基于邻域点云拟合局部变化趋势，与 VTK 在部分场景中采用的单元形函数导数/贡献单元策略存在差异。在曲面网格中，局部点集近共面，如果直接进行三维拟合，矩阵容易病态，导致法向分量不稳定；在单元数据路径中，单元中心邻域与节点值恢复之间也可能引入额外误差。

        后续拟采取三项措施：第一，引入非结构网格形函数导数法，将其作为非结构网格梯度计算的主线；第二，保留 AWLS 作为补充路线，用于说明不同梯度重构方法的适用边界；第三，在实验评价中区分 ambient 三维对照和曲面 intrinsic 对照，避免把曲面法向差异误解释为算法完全失效。

        数据优化模块尚处于设计阶段。后续将基于已有邻域图实现图双边滤波，构建多尺度平滑层和细节层，再通过加权融合得到优化结果。该模块的评价将不使用“通用降噪”口径，而采用可控代理噪声实验，明确其主要适用于局部随机高频扰动。

        ## 三、下一步工作计划

        第 7-8 周：完善梯度计算模块，引入形函数导数法，支持常见一阶三角形、四边形、四面体和六面体单元；补充点数据和单元数据两条路径。

        第 9-10 周：实现图双边滤波和多尺度融合模块，完成 gaussian、grf、mixed 和 impulse 等代理扰动实验，观察 MAE、RMSE、粗糙度和可视化效果变化。

        第 11-12 周：完善 GUI 与 VTK 导出，完成批量梯度实验、规模化时间实验和 ParaView 图像对照，整理结果表格。

        第 13-14 周：完成论文撰写，重点写清楚题目边界、算法分派、实验口径和局限性，准备答辩。

        ## 四、能否按期完成的评价

        从中期进度看，系统架构、数据转换、GPU 计算上下文和初步梯度模块已经完成，剩余工作主要是非结构网格梯度主线调整、数据优化模块实现、实验数据整理和论文写作。只要后续不再扩大到完整 CAE 后处理平台，而是坚持“GPU 数据预处理原型系统”的边界，本课题可以按期完成并支撑毕业答辩。
        """
    ))


def build_outline(metrics: dict[str, object]) -> str:
    return clean_md(textwrap.dedent(
        f"""
        # 本科毕业论文详细大纲

        题目：基于 OpenGL 的 CAE 软件数据预处理方法研究与实践

        目标篇幅：正文超过 2 万汉字，Word 按 A4、小四宋体、1.5 倍行距、图表和章节分页排版时按 40 页以上准备。建议最终正文控制在 3.5 万至 4.5 万汉字，图表 18-25 个，参考文献 30 篇左右。

        ## 摘要与关键词（1-2 页）

        中文摘要：说明 CAE 后处理数据预处理需求，概括 OpenGL Compute Shader、统一数据结构、梯度计算、多尺度优化和实验结果。

        英文摘要：对应中文摘要，注意不要逐字机翻，应突出 method、implementation、evaluation 和 limitation。

        关键词：CAE 后处理；OpenGL；Compute Shader；梯度计算；非结构网格；多尺度融合。

        ## 第一章 绪论（5-6 页，约 4500-5500 汉字）

        1.1 研究背景：CAE 后处理、科学可视化、数据派生量、GPU 并行趋势。

        1.2 问题提出：大规模网格、点/单元字段、梯度与噪声、CPU 过滤器时间成本、毕业设计可研究切面。

        1.3 国内外研究现状：VTK/ParaView、VTK-m、OpenGL/GPGPU、有限元梯度恢复、非结构网格重构、双边滤波和多尺度方法。这里必须同时写英文经典文献和中文应用研究。

        1.4 本文研究目标与边界：明确不是完整 CAE 软件，而是 GPU 数据预处理原型系统。

        1.5 本文主要工作：数据模型、梯度引擎、滤波融合引擎、GUI/测试程序、实验体系。

        1.6 论文组织结构。

        ## 第二章 相关技术与理论基础（6-7 页，约 5500-6500 汉字）

        2.1 CAE 后处理数据模型：点、单元、字段、结构网格和非结构网格。

        2.2 VTK/ParaView 数据流：为何使用 VTK 读写和对照，不把 VTK 作为核心算法替代品。

        2.3 OpenGL Compute Shader 与 SSBO：工作组、缓冲区、内存布局、GPU 计时。

        2.4 梯度计算理论：有限差分、形函数导数、点数据与单元数据、WLS/AWLS。

        2.5 图双边滤波与多尺度融合：空间权重、值域权重、尺度层、细节层和粗糙度。

        2.6 论文实验指标：MAE、RMSE、NRMSE、soft relative error、角度误差、墙钟时间、GPU 时间。

        ## 第三章 系统需求分析与总体设计（6-7 页，约 5500-6500 汉字）

        3.1 需求分析：功能需求、性能需求、可复现需求、可扩展需求。

        3.2 总体架构：VTK 输入 -> 内部表示 -> GPU 引擎 -> 结果写回 -> VTK/GUI 输出。

        3.3 模块划分：`CAEProcessingFacade`、`DataObject`、`VTKDataConverter`、`GLGradientEngine`、`GLFilterEngine`、Qt GUI、测试程序。

        3.4 数据结构设计：扁平数组、CSR 邻域、字段数组、点/单元关联、规则网格维度。

        3.5 GPU 计算上下文设计：独立 OpenGL 上下文、着色器编译、SSBO 复用、错误处理。

        3.6 结果命名与导出设计：保证 GUI、测试程序、ParaView 的口径一致。

        ## 第四章 核心算法设计与实现（9-10 页，约 8500-10000 汉字）

        4.1 规则网格有限差分梯度：公式、边界处理、GLSL 并行映射。

        4.2 非结构网格形函数导数梯度：单元类型、Jacobian、点梯度和单元梯度路径。

        4.3 AWLS 支撑数据：自适应邻域、局部维度、质量指标、正则化和备用意义。

        4.4 多尺度图双边滤波：邻域图、参数尺度化、迭代平滑。

        4.5 细节层分解与融合：base 层、detail 层、权重回注、边缘保护。

        4.6 门面层算法分派：Auto 模式如何根据网格类型选择 FD 或 shape function。

        4.7 GUI 与测试程序实现：为什么实验程序与 GUI 共享同一底层接口。

        ## 第五章 实验设计与结果分析（10-12 页，约 9000-11000 汉字）

        5.1 实验环境：CPU、GPU、OpenGL、VTK、Visual Studio、数据集。

        5.2 梯度正确性实验：解析 benchmark、点/单元数据、曲面 intrinsic 评价。

        5.3 与 VTK 的工程一致性实验：真实字段、vtkGradientFilter、误差指标解释。

        5.4 时间性能实验：当前 `results/timing` 有 {metrics["timing_count"]} 组规模化对照；表格应同时列系统墙钟、GPU、VTK 单线程、VTK 并行和加速比。

        5.5 数据优化实验：点数据和单元数据共 {metrics["ms_all"]["total"]} 个成功案例；按 gaussian、grf、mixed、impulse 分组讨论有效范围。

        5.6 可视化结果：插入系统梯度、VTK 梯度、数据优化前后、速度对比和多尺度指标图。

        5.7 局限性分析：单元数据解析场敏感、曲面法向评价、脉冲噪声弱、仍依赖 VTK 读写/显示。

        ## 第六章 总结与展望（3-4 页，约 3000-4000 汉字）

        6.1 研究结论：系统已完成 GPU 数据预处理主线。

        6.2 本文特点：实现闭环、实验口径清楚、结论边界明确。

        6.3 不足：不是完整 CAE 平台，复杂退化网格和异常值处理仍需改进。

        6.4 后续工作：更丰富单元类型、TBB/OpenMP 更公平对照、更真实 CAE 噪声模型、与后续渲染模块深度集成。

        ## 参考文献安排

        参考文献建议 30-35 篇，其中英文 18 篇左右，中文 12 篇左右。必须覆盖 OpenGL/SSBO、VTK/ParaView、非结构网格梯度、有限元梯度恢复、双边滤波/多尺度、中文论文规范与本科论文抽检要求。

        ## 图表安排

        建议图：系统架构图、数据流图、梯度模块流程图、多尺度模块流程图、着色器数据绑定图、梯度渲染对比图、数据优化前后图、速度对比图、粗糙度对比图。

        建议表：开发环境表、数据集表、模块文件表、梯度正确性表、真实字段对照表、时间性能表、多尺度分组统计表、局限性与改进表。
        """
    ))


def extract_docx_as_markdown(path: Path) -> str:
    doc = Document(path)
    lines: list[str] = []
    for para in doc.paragraphs:
        text = para.text.strip()
        if not text:
            lines.append("")
            continue
        style = para.style.name if para.style else ""
        if style.startswith("Heading"):
            m = re.search(r"(\d+)", style)
            level = int(m.group(1)) if m else 2
            lines.append("#" * min(level, 4) + " " + text)
        elif re.match(r"^第[一二三四五六七八九十]+章", text):
            lines.append("## " + text)
        elif re.match(r"^[0-9]+\\.[0-9]+", text):
            lines.append("### " + text)
        else:
            lines.append(text)
    for ti, table in enumerate(doc.tables, start=1):
        rows = []
        for row in table.rows:
            rows.append([cell.text.replace("\n", " ").strip() for cell in row.cells])
        if rows:
            lines.append("")
            lines.append(f"表格摘录 {ti}")
            lines.append(md_table(rows[0], rows[1:]))
            lines.append("")
    return "\n".join(lines)


def thesis_appendix(metrics: dict[str, object]) -> str:
    return clean_md(textwrap.dedent(
        f"""

        ## 附录E 文献检索与论文规范补充

        本附录根据 {PY_DATE} 的检索结果补充整理相关文献和写作规范。中文本科毕业论文写作首先应服从学校模板；若学校模板未说明，则可参考国家标准和教育部抽检要求组织。需要特别注意日期口径：GB/T 7713.1-2025 已发布并于 2026 年实施；GB/T 7714-2025 虽已发布，但实施日期为 2026-07-01，因此在 {PY_DATE} 这个时间点，若学校没有提前采用新版要求，参考文献著录仍可按 GB/T 7714-2015 执行。

        教育部本科毕业论文抽检办法强调论文应体现选题意义、写作安排、逻辑构建、专业能力和学术规范。落实到本课题，论文不能只展示界面截图，也不能只堆算法公式，而应围绕“为什么做、怎么设计、怎么实现、怎么验证、边界在哪里”展开。计算机类工程型论文尤其要写清系统需求、架构设计、核心模块、关键数据结构、实验方法和复现路径。

        本文的参考文献应按四条线组织。第一条线是 CAE/科学可视化工具链，包括 VTK、ParaView 和 VTK-m，用于说明数据模型、过滤器和数据并行趋势。第二条线是 OpenGL/GPU 计算，包括 Compute Shader、SSBO 和 GPGPU 综述，用于说明为什么可以把预处理算法迁移到图形 API 的计算管线。第三条线是梯度计算，包括有限差分、形函数导数、最小二乘重构和有限元梯度恢复。第四条线是数据优化，包括双边滤波、保边平滑、尺度空间和网格去噪。

        {literature_markdown()}

        ## 附录F 过程文档与论文口径一致性检查

        修订后的立题论证书、开题报告和中期进展报告均应统一为以下口径：课题不是开发完整 CAE 后处理平台，而是实现“基于 OpenGL 的 CAE 数据预处理原型系统”；核心成果不是动态粒子、等值面或完整可视分析链，而是 VTK 数据转换、GPU 梯度计算、多尺度数据优化、结果导出和实验评价。

        梯度模块的最终口径应以规则网格有限差分和非结构网格形函数导数法为主线，AWLS 作为补充探索和备用路径。这样可以自然解释中期阶段发现的 WLS 曲面误差问题，也能说明后续工作为什么从“邻域拟合”转向“单元形函数导数”。多尺度模块的最终口径应是“面向局部随机高频扰动的结果场优化”，不要写成对所有噪声、异常值和数值误差都有效。

        从实验数据看，当前项目的量化基础充足。梯度实验成功记录 {metrics["grad_success"]} 条，时间实验 {metrics["timing_count"]} 组，多尺度优化实验成功案例 {metrics["ms_all"]["total"]} 个。论文中应把这些数据分层使用：解析 benchmark 证明算法实现正确性；真实字段与 VTK 对照证明工程一致性；时间实验证明 GPU 实现性能；多尺度实验证明数据优化模块的适用范围。

        ## 附录G 主要时间实验摘录

        {md_table(["实验文件", "关联", "样本数", "系统墙钟/ms", "GPU/ms", "VTK单线程/ms", "VTK并行/ms", "单线程加速", "并行加速", "NRMAE"], metrics["timing_table"])}
        """
    ))


def set_font(run, size: float = 12, bold: bool = False, name: str = "Times New Roman") -> None:
    run.font.name = name
    run.font.size = Pt(size)
    run.bold = bold
    rpr = run._element.rPr
    if rpr is None:
        rpr = OxmlElement("w:rPr")
        run._element.insert(0, rpr)
    fonts = OxmlElement("w:rFonts")
    fonts.set(qn("w:eastAsia"), "宋体")
    fonts.set(qn("w:ascii"), name)
    fonts.set(qn("w:hAnsi"), name)
    rpr.append(fonts)


def add_heading_docx(doc: Document, text: str, level: int) -> None:
    p = doc.add_paragraph()
    p.style = doc.styles[f"Heading {min(level, 3)}"]
    run = p.add_run(text)
    set_font(run, 16 if level == 1 else 14 if level == 2 else 12, True, "黑体")


def add_para_docx(doc: Document, text: str) -> None:
    p = doc.add_paragraph()
    p.paragraph_format.first_line_indent = Cm(0.74)
    p.paragraph_format.line_spacing = 1.5
    p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    run = p.add_run(text)
    set_font(run, 12)


def append_markdown_to_docx(doc: Document, markdown: str) -> None:
    pending_table: list[list[str]] = []

    def flush_table() -> None:
        nonlocal pending_table
        if len(pending_table) < 2:
            pending_table = []
            return
        rows = [row for row in pending_table if not all(re.fullmatch(r"-+", c.strip()) for c in row)]
        if not rows:
            pending_table = []
            return
        table = doc.add_table(rows=len(rows), cols=len(rows[0]))
        table.style = "Table Grid"
        for i, row in enumerate(rows):
            for j, cell in enumerate(row):
                table.cell(i, j).text = cell
        pending_table = []

    for raw in markdown.splitlines():
        line = raw.strip()
        if not line:
            flush_table()
            continue
        if line.startswith("|") and line.endswith("|"):
            cells = [c.strip() for c in line.strip("|").split("|")]
            pending_table.append(cells)
            continue
        flush_table()
        if line.startswith("### "):
            add_heading_docx(doc, line[4:], 3)
        elif line.startswith("## "):
            doc.add_page_break()
            add_heading_docx(doc, line[3:], 2)
        elif line.startswith("# "):
            doc.add_page_break()
            add_heading_docx(doc, line[2:], 1)
        else:
            for para in textwrap.wrap(line, width=120, break_long_words=False, replace_whitespace=False) or [line]:
                add_para_docx(doc, para)
    flush_table()


def build_thesis_files(metrics: dict[str, object]) -> tuple[int, int]:
    base = next((p for p in BASE_DOCX_CANDIDATES if p.exists()), None)
    if base is None:
        raise FileNotFoundError("未找到可复用的论文 Word 基础稿")

    extracted = extract_docx_as_markdown(base)
    appendix = thesis_appendix(metrics)
    thesis_md = "\n".join(
        [
            "# 基于OpenGL的CAE软件数据预处理方法研究与实践",
            "",
            f"> 生成日期：{PY_DATE}。本文根据当前项目代码、实验结果、文献检索和已有排版素材整理。",
            "",
            extracted,
            "",
            appendix,
            "",
        ]
    )
    OUT_THESIS_MD.write_text(thesis_md, encoding="utf-8")

    shutil.copy2(base, OUT_THESIS_DOCX)
    doc = Document(OUT_THESIS_DOCX)
    for section in doc.sections:
        section.top_margin = Cm(2.5)
        section.bottom_margin = Cm(2.3)
        section.left_margin = Cm(2.7)
        section.right_margin = Cm(2.5)
    append_markdown_to_docx(doc, appendix)
    doc.save(OUT_THESIS_DOCX)

    chinese_chars = len(re.findall(r"[\u4e00-\u9fff]", thesis_md))
    total_chars = len(thesis_md)
    return chinese_chars, total_chars


def main() -> None:
    DOC.mkdir(parents=True, exist_ok=True)
    metrics = build_metrics()

    OUT_COMPLETION.write_text(build_completion_report(metrics), encoding="utf-8")
    OUT_ARGUMENT.write_text(build_argument_doc(), encoding="utf-8")
    OUT_PROPOSAL.write_text(build_proposal_doc(), encoding="utf-8")
    OUT_MIDTERM.write_text(build_midterm_doc(), encoding="utf-8")
    OUT_OUTLINE.write_text(build_outline(metrics), encoding="utf-8")
    zh, total = build_thesis_files(metrics)

    print(f"generated {OUT_COMPLETION}")
    print(f"generated {OUT_ARGUMENT}")
    print(f"generated {OUT_PROPOSAL}")
    print(f"generated {OUT_MIDTERM}")
    print(f"generated {OUT_OUTLINE}")
    print(f"generated {OUT_THESIS_MD}")
    print(f"generated {OUT_THESIS_DOCX}")
    print(f"thesis_md_chinese_chars={zh}")
    print(f"thesis_md_total_chars={total}")


if __name__ == "__main__":
    main()
