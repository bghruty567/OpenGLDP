from __future__ import annotations

import math
import re
import textwrap
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
W = ROOT / "wendang"
ASSETS = W / "assets"
ASSETS.mkdir(parents=True, exist_ok=True)

FONT_CANDIDATES = [
    Path("C:/Windows/Fonts/msyh.ttc"),
    Path("C:/Windows/Fonts/msyh.ttf"),
    Path("C:/Windows/Fonts/simsun.ttc"),
    Path("C:/Windows/Fonts/simhei.ttf"),
]


def font(size: int, bold: bool = False):
    for p in FONT_CANDIDATES:
        if p.exists():
            return ImageFont.truetype(str(p), size=size)
    return ImageFont.load_default()


F_TITLE = font(48)
F_SUB = font(30)
F_BODY = font(28)
F_SMALL = font(24)
F_TINY = font(20)

NAVY = "#17324D"
BLUE = "#2F6EA5"
LIGHT_BLUE = "#EAF3FA"
MID_BLUE = "#CFE2F3"
GRAY = "#5D6673"
LIGHT_GRAY = "#F5F7FA"
BORDER = "#9AA8B5"
GREEN = "#2F8F6B"
ORANGE = "#D98C24"
RED = "#C85050"
PURPLE = "#6C5FA7"
BLACK = "#17202A"


def text_size(draw: ImageDraw.ImageDraw, text: str, fnt) -> tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=fnt)
    return box[2] - box[0], box[3] - box[1]


def draw_wrapped_center(draw, box, text, fnt=F_BODY, fill=BLACK, width_chars=12, line_gap=8):
    x0, y0, x1, y1 = box
    lines = []
    for seg in text.split("\n"):
        lines.extend(textwrap.wrap(seg, width=width_chars, break_long_words=False) or [""])
    heights = [text_size(draw, line, fnt)[1] for line in lines]
    total_h = sum(heights) + line_gap * max(0, len(lines) - 1)
    y = y0 + (y1 - y0 - total_h) / 2
    for line, h in zip(lines, heights):
        w, _ = text_size(draw, line, fnt)
        draw.text((x0 + (x1 - x0 - w) / 2, y), line, font=fnt, fill=fill)
        y += h + line_gap


def rounded(draw, box, fill, outline=BORDER, radius=24, width=3):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def arrow(draw, start, end, fill=BLUE, width=5, head=18):
    x0, y0 = start
    x1, y1 = end
    draw.line((x0, y0, x1, y1), fill=fill, width=width)
    ang = math.atan2(y1 - y0, x1 - x0)
    pts = [
        (x1, y1),
        (x1 - head * math.cos(ang - math.pi / 7), y1 - head * math.sin(ang - math.pi / 7)),
        (x1 - head * math.cos(ang + math.pi / 7), y1 - head * math.sin(ang + math.pi / 7)),
    ]
    draw.polygon(pts, fill=fill)


def canvas(title: str, subtitle: str | None, size=(2200, 1100)):
    img = Image.new("RGB", size, "white")
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, size[0], 110), fill=NAVY)
    d.text((70, 28), title, font=F_TITLE, fill="white")
    if subtitle:
        d.text((70, 130), subtitle, font=F_SUB, fill=GRAY)
    return img, d


def save(img: Image.Image, name: str):
    path = ASSETS / name
    img.save(path, quality=95)
    return path


def fig_data_flow():
    img, d = canvas("图 1-1 CAE 数据预处理总体数据流", "从求解结果到 GPU 计算，再到 VTK 导出与可视化观察", (2400, 900))
    labels = ["CAE\n求解结果", "VTK\n数据读入", "内部\n数据对象", "OpenGL\n计算着色器", "梯度计算\n数据优化", "结果字段\n写回", "VTK导出\nParaView观察"]
    x, y, w, h, gap = 90, 360, 270, 150, 58
    colors = [LIGHT_GRAY, LIGHT_BLUE, LIGHT_BLUE, MID_BLUE, "#E7F3EA", "#FFF2DF", LIGHT_GRAY]
    for i, lab in enumerate(labels):
        box = (x + i * (w + gap), y, x + i * (w + gap) + w, y + h)
        rounded(d, box, colors[i], radius=22)
        draw_wrapped_center(d, box, lab, F_BODY, NAVY, width_chars=7)
        if i < len(labels) - 1:
            arrow(d, (box[2] + 10, y + h // 2), (box[2] + gap - 12, y + h // 2), BLUE)
    d.text((90, 670), "说明：VTK 负责数据生态与工程对照，OpenGL Compute Shader 承担核心预处理计算。", font=F_SMALL, fill=GRAY)
    return save(img, "fig_1_1_data_flow.png")


def fig_technical_route():
    img, d = canvas("图 1-2 本文技术路线", "文献调研、系统设计、算法实现与实验验证形成闭环", (2200, 1250))
    boxes = {
        "文献调研\n需求收缩": (120, 220, 440, 350),
        "VTK 数据\n模型分析": (120, 470, 440, 600),
        "内部数据\n对象设计": (120, 720, 440, 850),
        "规则网格\n有限差分": (700, 390, 1020, 520),
        "非结构网格\n形函数导数": (700, 580, 1020, 710),
        "图双边滤波\n多尺度融合": (700, 770, 1020, 900),
        "解析场验证": (1260, 330, 1560, 450),
        "VTK 一致性\n对比": (1260, 500, 1560, 620),
        "时间性能\n实验": (1260, 670, 1560, 790),
        "高斯扰动\n优化实验": (1260, 840, 1560, 960),
        "论文与\n答辩材料": (1760, 580, 2060, 730),
    }
    for lab, box in boxes.items():
        fill = LIGHT_BLUE if box[0] < 600 else "#E7F3EA" if box[0] < 1100 else "#FFF4E6" if box[0] < 1700 else "#F0EDFA"
        rounded(d, box, fill, radius=24)
        draw_wrapped_center(d, box, lab, F_BODY, NAVY, width_chars=8)
    for a, b in [("文献调研\n需求收缩", "VTK 数据\n模型分析"), ("VTK 数据\n模型分析", "内部数据\n对象设计")]:
        ba, bb = boxes[a], boxes[b]
        arrow(d, ((ba[0]+ba[2])/2, ba[3]+10), ((bb[0]+bb[2])/2, bb[1]-10))
    for b in ["规则网格\n有限差分", "非结构网格\n形函数导数", "图双边滤波\n多尺度融合"]:
        arrow(d, (boxes["内部数据\n对象设计"][2]+10, (boxes["内部数据\n对象设计"][1]+boxes["内部数据\n对象设计"][3])/2), (boxes[b][0]-10, (boxes[b][1]+boxes[b][3])/2))
    pairs = [("规则网格\n有限差分", "解析场验证"), ("非结构网格\n形函数导数", "VTK 一致性\n对比"), ("非结构网格\n形函数导数", "时间性能\n实验"), ("图双边滤波\n多尺度融合", "高斯扰动\n优化实验")]
    for a, b in pairs:
        arrow(d, (boxes[a][2]+10, (boxes[a][1]+boxes[a][3])/2), (boxes[b][0]-10, (boxes[b][1]+boxes[b][3])/2))
    for b in ["解析场验证", "VTK 一致性\n对比", "时间性能\n实验", "高斯扰动\n优化实验"]:
        arrow(d, (boxes[b][2]+10, (boxes[b][1]+boxes[b][3])/2), (boxes["论文与\n答辩材料"][0]-10, (boxes["论文与\n答辩材料"][1]+boxes["论文与\n答辩材料"][3])/2), fill=GREEN)
    return save(img, "fig_1_2_technical_route.png")


def fig_tech_modules():
    img, d = canvas("图 2-1 相关技术与系统模块关系", "技术基础不是孤立概念，而是直接服务系统模块", (2200, 1050))
    center = (820, 470, 1380, 640)
    rounded(d, center, NAVY, outline=NAVY, radius=28)
    draw_wrapped_center(d, center, "CAE 数据预处理\n原型系统", F_SUB, "white", width_chars=12)
    items = [
        ("VTK / ParaView\n数据模型", (120, 260, 560, 420), LIGHT_BLUE),
        ("OpenGL Compute\nShader / SSBO", (120, 690, 560, 850), LIGHT_BLUE),
        ("有限差分\n形函数导数", (1640, 260, 2080, 420), "#E7F3EA"),
        ("图双边滤波\n多尺度融合", (1640, 690, 2080, 850), "#FFF4E6"),
    ]
    for lab, box, fill in items:
        rounded(d, box, fill, radius=24)
        draw_wrapped_center(d, box, lab, F_BODY, NAVY, width_chars=14)
        arrow(d, ((box[0]+box[2])/2, (box[1]+box[3])/2), ((center[0]+center[2])/2, (center[1]+center[3])/2), fill=BLUE)
    d.text((720, 850), "公式、数据结构与实验指标共同支撑论文论证", font=F_SMALL, fill=GRAY)
    return save(img, "fig_2_1_tech_modules.png")


def fig_architecture():
    img, d = canvas("图 3-1 系统总体架构", "界面、门面、数据、GPU 计算和 VTK/ParaView 输出分层组织", (2200, 1300))
    layers = [
        ("界面与实验层", ["Qt GUI", "TestGradient", "TestMultiScale"], 220, LIGHT_BLUE),
        ("门面层", ["CAEProcessingFacade"], 430, "#E7F3EA"),
        ("数据层", ["VTKDataConverter", "DataObject", "字段/邻域/单元连接"], 640, "#FFF4E6"),
        ("OpenGL 计算层", ["GLGradientEngine", "GLFilterEngine", "Compute Shaders"], 850, "#F0EDFA"),
        ("外部工具", ["VTK 文件", "ParaView 观察", "CSV 实验结果"], 1060, LIGHT_GRAY),
    ]
    for title, cards, y, fill in layers:
        d.text((110, y+45), title, font=F_SUB, fill=NAVY)
        d.line((300, y+62, 2060, y+62), fill=BORDER, width=2)
        x = 390
        for c in cards:
            box = (x, y, x+440, y+125)
            rounded(d, box, fill, radius=20)
            draw_wrapped_center(d, box, c, F_BODY, NAVY, width_chars=14)
            x += 520
        if y < 1060:
            arrow(d, (1100, y+140), (1100, y+195), fill=GREEN)
    return save(img, "fig_3_1_system_architecture.png")


def fig_shape_flow():
    img, d = canvas("图 4-2 非结构化网格形函数导数计算流程", "以单元形函数和 Jacobian 映射为核心", (2300, 1050))
    labels = ["读取单元\n节点坐标 x_a", "读取字段值\nu_a", "按单元类型\n选择 N_a", "计算 J 与\nJ^{-T}", "计算\n∇x N_a", "累加得到\n∇x u^e", "写回点/单元\n梯度字段"]
    x, y, w, h, gap = 90, 420, 260, 145, 55
    for i, lab in enumerate(labels):
        box = (x+i*(w+gap), y, x+i*(w+gap)+w, y+h)
        rounded(d, box, LIGHT_BLUE if i < 3 else "#E7F3EA" if i < 6 else "#FFF4E6", radius=22)
        draw_wrapped_center(d, box, lab, F_BODY, NAVY, width_chars=8)
        if i < len(labels)-1:
            arrow(d, (box[2]+10, y+h/2), (box[2]+gap-12, y+h/2), fill=BLUE)
    d.text((90, 710), "核心公式：∇x N_a = J^{-T} ∇ξ N_a，∇x u^e = Σ u_a ∇x N_a", font=F_SMALL, fill=GRAY)
    return save(img, "fig_4_1_shape_gradient_flow.png")


def fig_regular_fd_chain_rule():
    img, d = canvas("图 4-1 规则网格有限差分与链式法则流程", "先在参数空间差分，再通过局部 Jacobian 映射到物理空间", (2400, 1080))
    labels = [
        ("逻辑索引\n(i,j,k)", "线程定位"),
        ("参数邻居\nxi-eta-zeta", "中心/单边差分"),
        ("字段差分\nd_u", "dXi-dEta-dZeta"),
        ("坐标差分\nJ", "x_xi-x_eta-x_zeta"),
        ("链式法则\nJ^(-T)", "逆 Jacobian 映射"),
        ("物理梯度\ng", "gx / gy / gz"),
    ]
    x, y, w, h, gap = 85, 385, 315, 160, 64
    colors = [LIGHT_GRAY, LIGHT_BLUE, LIGHT_BLUE, "#E7F3EA", "#FFF4E6", "#F0EDFA"]
    for i, (title, body) in enumerate(labels):
        box = (x + i * (w + gap), y, x + i * (w + gap) + w, y + h)
        rounded(d, box, colors[i], radius=22)
        draw_wrapped_center(d, (box[0] + 16, box[1] + 16, box[2] - 16, box[1] + 74), title, F_BODY, NAVY, width_chars=14)
        d.line((box[0] + 26, box[1] + 88, box[2] - 26, box[1] + 88), fill=BORDER, width=2)
        draw_wrapped_center(d, (box[0] + 18, box[1] + 92, box[2] - 18, box[3] - 12), body, F_SMALL, GRAY, width_chars=18, line_gap=4)
        if i < len(labels) - 1:
            arrow(d, (box[2] + 11, y + h / 2), (box[2] + gap - 13, y + h / 2), fill=BLUE)

    formula_box = (250, 710, 2150, 860)
    rounded(d, formula_box, "white", outline=BLUE, radius=18, width=3)
    d.text((330, 755), "核心公式：", font=F_BODY, fill=NAVY)
    d.text((520, 755), "grad_xi u = J^T grad_x u,   grad_x u = J^(-T) grad_xi u", font=F_SUB, fill=BLACK)
    d.text((330, 905), "GLSL 对应：xix / etax / zetax 等逆 Jacobian 系数分别参与 gx、gy、gz 的线性组合。", font=F_SMALL, fill=GRAY)
    return save(img, "fig_4_1_regular_fd_chain_rule.png")


def fig_opengl_sequence():
    img, d = canvas("图 4-3 OpenGL 计算执行时序", "CPU 侧门面层、GPU 缓冲区和计算着色器之间的数据交换", (2200, 1200))
    actors = [("CPU / 门面层", 330), ("GPU 缓冲区", 850), ("Compute Shader", 1370), ("DataObject", 1890)]
    for name, x in actors:
        rounded(d, (x-180, 230, x+180, 330), LIGHT_BLUE, radius=20)
        draw_wrapped_center(d, (x-180, 230, x+180, 330), name, F_SMALL, NAVY, width_chars=14)
        d.line((x, 340, x, 1030), fill=BORDER, width=3)
    steps = [
        (330, 850, 430, "上传坐标、字段、连接、邻域"),
        (330, 1370, 560, "设置参数并派发 dispatch"),
        (1370, 850, 690, "并行写入结果缓冲"),
        (850, 330, 820, "同步并回读结果"),
        (330, 1890, 950, "写回字段并导出 VTK"),
    ]
    for x0, x1, y, label in steps:
        arrow(d, (x0+190 if x1>x0 else x0-190, y), (x1-190 if x1>x0 else x1+190, y), fill=BLUE)
        tw, th = text_size(d, label, F_TINY)
        d.text(((x0+x1)/2 - tw/2, y-38), label, font=F_TINY, fill=GRAY)
    return save(img, "fig_4_2_opengl_sequence.png")


def fig_experiment_logic():
    img, d = canvas("图 5-1 实验分层逻辑", "不同实验承担不同论证任务，避免混用结论", (2200, 1000))
    rows = [
        ("解析场验证", "解析真值", "算法正确性", GREEN),
        ("VTK 一致性对比", "vtkGradientFilter", "工程一致性", BLUE),
        ("时间对比", "VTK 并行时间", "计算效率", ORANGE),
        ("高斯扰动优化", "干净场 + 代理扰动", "优化有效性", PURPLE),
    ]
    x1, x2, x3 = 170, 760, 1390
    for i, (a, b, c, col) in enumerate(rows):
        y = 240 + i * 170
        for x, txt, fill in [(x1, a, "#F5F7FA"), (x2, b, "#EAF3FA"), (x3, c, "#E7F3EA")]:
            box = (x, y, x+430, y+105)
            rounded(d, box, fill, outline=col, radius=20, width=4)
            draw_wrapped_center(d, box, txt, F_BODY, NAVY, width_chars=16)
        arrow(d, (x1+440, y+52), (x2-15, y+52), fill=col)
        arrow(d, (x2+440, y+52), (x3-15, y+52), fill=col)
    return save(img, "fig_5_1_experiment_logic.png")


def fig_contributions():
    img, d = canvas("图 6-1 本文成果与验证闭环", "系统实现、算法模块和实验验证共同支撑毕业设计结论", (2200, 1100))
    center = (850, 420, 1350, 610)
    rounded(d, center, NAVY, outline=NAVY, radius=30)
    draw_wrapped_center(d, center, "本文成果\nOpenGL CAE 数据预处理原型", F_SUB, "white", width_chars=18)
    items = [
        ("统一数据对象", (150, 230, 560, 360), LIGHT_BLUE),
        ("规则网格\n有限差分", (150, 730, 560, 860), "#E7F3EA"),
        ("形函数导数\n梯度计算", (1640, 230, 2050, 360), "#E7F3EA"),
        ("图双边滤波\n多尺度融合", (1640, 730, 2050, 860), "#FFF4E6"),
        ("解析场 / VTK / 时间\n实验闭环", (840, 850, 1360, 990), "#F0EDFA"),
    ]
    for lab, box, fill in items:
        rounded(d, box, fill, radius=24)
        draw_wrapped_center(d, box, lab, F_BODY, NAVY, width_chars=16)
        arrow(d, ((box[0]+box[2])/2, (box[1]+box[3])/2), ((center[0]+center[2])/2, (center[1]+center[3])/2), fill=BLUE)
    return save(img, "fig_6_1_contributions.png")


def fig_timing_chart():
    img, d = canvas("图 5-2 VTK 并行时间与系统总时间对比", "纵轴采用 log10(ms)，便于同时展示规则网格和非结构化网格", (2400, 1250))
    data = [
        ("S20", 2.7573, 0.9036),
        ("S32", 3.5564, 1.4336),
        ("S48", 8.2716, 4.0996),
        ("U20", 163.799, 2.8527),
        ("U32", 708.650, 5.3835),
        ("U48", 3349.36, 44.8039),
    ]
    left, top, width, height = 220, 260, 1900, 720
    d.rectangle((left, top, left+width, top+height), outline=BORDER, width=3)
    maxv, minv = math.log10(4000), math.log10(0.5)
    def sy(v): return top + height - (math.log10(v)-minv)/(maxv-minv)*height
    for tick in [1, 10, 100, 1000]:
        y = sy(tick)
        d.line((left, y, left+width, y), fill="#E0E5EA", width=2)
        d.text((80, y-12), str(tick), font=F_TINY, fill=GRAY)
    bw = 90
    group_gap = width / len(data)
    for i, (lab, vtk, sys) in enumerate(data):
        cx = left + group_gap*i + group_gap/2
        for j, (val, col) in enumerate([(vtk, ORANGE), (sys, BLUE)]):
            x0 = cx - bw + j*(bw+14)
            y0 = sy(val)
            d.rectangle((x0, y0, x0+bw, top+height), fill=col)
            d.text((x0-5, y0-34), f"{val:g}", font=F_TINY, fill=BLACK)
        tw, _ = text_size(d, lab, F_SMALL)
        d.text((cx-tw/2, top+height+30), lab, font=F_SMALL, fill=NAVY)
    d.rectangle((1700, 175, 1760, 215), fill=ORANGE)
    d.text((1775, 175), "VTK 并行时间", font=F_SMALL, fill=GRAY)
    d.rectangle((1700, 230, 1760, 270), fill=BLUE)
    d.text((1775, 230), "系统总时间", font=F_SMALL, fill=GRAY)
    return save(img, "fig_5_2_timing_compare.png")


def fig_optimization_chart():
    img, d = canvas("图 5-3 高斯扰动优化前后误差对比", "点数据与单元数据优化后平均相对误差均降低", (2000, 1050))
    data = [("POINT", 0.296642, 0.180858), ("CELL", 0.294558, 0.121921)]
    left, top, width, height = 260, 260, 1500, 600
    d.rectangle((left, top, left+width, top+height), outline=BORDER, width=3)
    maxv = 0.34
    def sy(v): return top + height - v/maxv*height
    for tick in [0.1, 0.2, 0.3]:
        y = sy(tick)
        d.line((left, y, left+width, y), fill="#E0E5EA", width=2)
        d.text((140, y-12), f"{tick:.1f}", font=F_TINY, fill=GRAY)
    group_gap = width / len(data)
    bw = 150
    for i, (lab, before, after) in enumerate(data):
        cx = left + group_gap*i + group_gap/2
        for j, (val, col, tag) in enumerate([(before, ORANGE, "输入"), (after, GREEN, "优化后")]):
            x0 = cx - bw - 20 + j*(bw+40)
            y0 = sy(val)
            d.rectangle((x0, y0, x0+bw, top+height), fill=col)
            tw, _ = text_size(d, f"{val:.3f}", F_SMALL)
            d.text((x0+bw/2-tw/2, y0-42), f"{val:.3f}", font=F_SMALL, fill=BLACK)
        tw, _ = text_size(d, lab, F_SUB)
        d.text((cx-tw/2, top+height+45), lab, font=F_SUB, fill=NAVY)
    d.rectangle((1430, 165, 1490, 205), fill=ORANGE)
    d.text((1505, 165), "输入误差", font=F_SMALL, fill=GRAY)
    d.rectangle((1430, 220, 1490, 260), fill=GREEN)
    d.text((1505, 220), "优化后误差", font=F_SMALL, fill=GRAY)
    return save(img, "fig_5_3_optimization_error.png")


def replace_mermaid_blocks():
    replacements = {
        "01_绪论.md": [
            "![图 1-1 CAE 数据预处理总体数据流](assets/fig_1_1_data_flow.png)",
            "![图 1-2 本文技术路线](assets/fig_1_2_technical_route.png)",
        ],
        "02_相关技术与理论基础.md": [
            "![图 2-1 相关技术与系统模块关系](assets/fig_2_1_tech_modules.png)",
        ],
        "03_系统需求分析与总体设计.md": [
            "![图 3-1 系统总体架构](assets/fig_3_1_system_architecture.png)",
        ],
        "04_核心算法设计与实现.md": [
            "![图 4-2 非结构化网格形函数导数计算流程](assets/fig_4_1_shape_gradient_flow.png)",
            "![图 4-3 OpenGL 计算执行时序](assets/fig_4_2_opengl_sequence.png)",
        ],
        "05_实验设计与结果分析.md": [
            "![图 5-1 实验分层逻辑](assets/fig_5_1_experiment_logic.png)",
        ],
        "06_总结与展望.md": [
            "![图 6-1 本文成果与验证闭环](assets/fig_6_1_contributions.png)",
        ],
    }
    block = re.compile(r"```mermaid\n.*?\n```", re.S)
    for name, reps in replacements.items():
        path = W / name
        text = path.read_text(encoding="utf-8")
        for rep in reps:
            text = block.sub(rep, text, count=1)
        path.write_text(text, encoding="utf-8")

    p = W / "05_实验设计与结果分析.md"
    text = p.read_text(encoding="utf-8")
    if "fig_5_2_timing_compare.png" not in text:
        text = text.replace(
            "### 5.5.1 规则网格时间对比",
            "![图 5-2 VTK 并行时间与系统总时间对比](assets/fig_5_2_timing_compare.png)\n\n### 5.5.1 规则网格时间对比",
        )
    if "fig_5_3_optimization_error.png" not in text:
        text = text.replace(
            "POINT 关联方式下，平均相对误差由 0.296642 降至 0.180858，改进比为 0.610772。",
            "![图 5-3 高斯扰动优化前后误差对比](assets/fig_5_3_optimization_error.png)\n\nPOINT 关联方式下，平均相对误差由 0.296642 降至 0.180858，改进比为 0.610772。",
        )
    p.write_text(text, encoding="utf-8")

    readme = W / "README.md"
    txt = readme.read_text(encoding="utf-8")
    if "assets/*.png" not in txt:
        txt += "\n- 图示已替换为 `assets/*.png` 中的正式位图，不再使用 Mermaid 代码块。\n"
    readme.write_text(txt, encoding="utf-8")


def main():
    fig_data_flow()
    fig_technical_route()
    fig_tech_modules()
    fig_architecture()
    fig_regular_fd_chain_rule()
    fig_shape_flow()
    fig_opengl_sequence()
    fig_experiment_logic()
    fig_contributions()
    fig_timing_chart()
    fig_optimization_chart()
    replace_mermaid_blocks()
    print(f"generated figures in {ASSETS}")


if __name__ == "__main__":
    main()
