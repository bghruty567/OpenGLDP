from __future__ import annotations

import math
from pathlib import Path
from xml.sax.saxutils import escape

from PIL import Image, ImageChops, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "wendang" / "pic"
OUT.mkdir(parents=True, exist_ok=True)

FONT_CANDIDATES = [
    Path(r"C:\Windows\Fonts\NotoSerifSC-VF.ttf"),
    Path(r"C:\Windows\Fonts\NotoSansSC-VF.ttf"),
    Path(r"C:\Windows\Fonts\simsun.ttc"),
    Path(r"C:\Windows\Fonts\msyh.ttc"),
    Path(r"C:\Windows\Fonts\simhei.ttf"),
]
FONT_PATH = next((path for path in FONT_CANDIDATES if path.exists()), None)

BLACK = (20, 20, 20)
DARK = (70, 70, 70)
MID = (145, 145, 145)
LIGHT = (230, 230, 230)
FILL = (248, 248, 248)
WHITE = (255, 255, 255)
BLUE = (78, 111, 155)
ORANGE = (196, 125, 49)
GREEN = (84, 145, 112)


def font(size: int) -> ImageFont.ImageFont:
    if FONT_PATH is None:
        return ImageFont.load_default()
    return ImageFont.truetype(str(FONT_PATH), size)


SIZE_TITLE = 34
SIZE_LABEL = 25
SIZE_SMALL = 21
SIZE_TINY = 18

F_TITLE = font(SIZE_TITLE)
F_LABEL = font(SIZE_LABEL)
F_SMALL = font(SIZE_SMALL)
F_TINY = font(SIZE_TINY)
SVG_FONT_FAMILY = "'Noto Serif SC','Noto Sans CJK SC','SimSun','Microsoft YaHei',serif"


def canvas(width: int = 1600, height: int = 900) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (width, height), WHITE)
    return img, ImageDraw.Draw(img)


def text_center(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], text: str, f=F_LABEL, fill=BLACK):
    x1, y1, x2, y2 = box
    lines = text.split("\n")
    metrics = [draw.textbbox((0, 0), line, font=f) for line in lines]
    widths = [m[2] - m[0] for m in metrics]
    heights = [m[3] - m[1] for m in metrics]
    total_h = sum(heights) + max(0, len(lines) - 1) * 7
    y = y1 + (y2 - y1 - total_h) / 2
    for line, w, h in zip(lines, widths, heights):
        draw.text((x1 + (x2 - x1 - w) / 2, y), line, font=f, fill=fill)
        y += h + 7


def box(draw: ImageDraw.ImageDraw, xy: tuple[int, int, int, int], text: str, f=F_LABEL, fill=WHITE, width: int = 2):
    draw.rectangle(xy, outline=BLACK, width=width, fill=fill)
    text_center(draw, xy, text, f=f)


def arrow(draw: ImageDraw.ImageDraw, start: tuple[int, int], end: tuple[int, int], width: int = 3):
    draw.line([start, end], fill=BLACK, width=width)
    x1, y1 = start
    x2, y2 = end
    ang = math.atan2(y2 - y1, x2 - x1)
    size = 13
    p1 = (x2 - size * math.cos(ang - math.pi / 6), y2 - size * math.sin(ang - math.pi / 6))
    p2 = (x2 - size * math.cos(ang + math.pi / 6), y2 - size * math.sin(ang + math.pi / 6))
    draw.polygon([end, p1, p2], fill=BLACK)


def title(draw: ImageDraw.ImageDraw, text: str):
    # Figure captions in the thesis carry the title; keep the image itself concise.
    return


def save(img: Image.Image, name: str):
    bg = Image.new(img.mode, img.size, WHITE)
    bbox = ImageChops.difference(img, bg).getbbox()
    if bbox:
        margin = 58
        x1 = max(0, bbox[0] - margin)
        y1 = max(0, bbox[1] - margin)
        x2 = min(img.width, bbox[2] + margin)
        y2 = min(img.height, bbox[3] + margin)
        img = img.crop((x1, y1, x2, y2))
    img.save(OUT / name)


def svg_rgb(color: tuple[int, int, int]) -> str:
    return f"rgb({color[0]},{color[1]},{color[2]})"


def svg_text(
    x: float,
    y: float,
    text: str,
    size: int,
    fill: tuple[int, int, int] = BLACK,
    anchor: str = "start",
) -> str:
    safe_text = escape(text)
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" '
        f'font-family="{SVG_FONT_FAMILY}" font-size="{size}" '
        f'fill="{svg_rgb(fill)}">{safe_text}</text>'
    )


def save_timing_line_chart_svg(
    name: str,
    caption: str,
    x_labels: list[str],
    vtk_values: list[float],
    sys_values: list[float],
    x_axis_label: str,
    vtk_value_labels: list[str],
    sys_value_labels: list[str],
):
    width, height = 1500, 880
    plot = (180, 170, 1190, 670)
    x1, y1, x2, y2 = plot
    y_min, y_max = log_bounds(vtk_values + sys_values)

    if math.isclose(math.log10(y_max), math.log10(y_min)):
        y_max = y_min * 10

    lo = math.floor(math.log10(y_min))
    hi = math.ceil(math.log10(y_max))
    ticks = [10 ** p for p in range(lo, hi + 1)]
    ticks = [t for t in ticks if y_min <= t <= y_max]
    if y_min not in ticks:
        ticks.insert(0, y_min)
    if y_max not in ticks:
        ticks.append(y_max)

    count = len(x_labels)
    vtk_points: list[tuple[float, float]] = []
    sys_points: list[tuple[float, float]] = []
    for i in range(count):
        x = plot[0] + (plot[2] - plot[0]) * (i + 0.5) / count
        vtk_points.append((x, y_to_px(vtk_values[i], plot, y_min, y_max, True)))
        sys_points.append((x, y_to_px(sys_values[i], plot, y_min, y_max, True)))

    def polyline(points: list[tuple[float, float]], color: tuple[int, int, int], width_px: int) -> str:
        pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
        return f'<polyline points="{pts}" fill="none" stroke="{svg_rgb(color)}" stroke-width="{width_px}" />'

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="{svg_rgb(WHITE)}" />',
        f'<line x1="{x1}" y1="{y2}" x2="{x2}" y2="{y2}" stroke="{svg_rgb(BLACK)}" stroke-width="2" />',
        f'<line x1="{x1}" y1="{y1}" x2="{x1}" y2="{y2}" stroke="{svg_rgb(BLACK)}" stroke-width="2" />',
        svg_text(plot[0], 92, caption, SIZE_LABEL, BLACK, "start"),
        svg_text(x1 - 110, y1 - 45, "时间 / ms", SIZE_SMALL, BLACK, "start"),
    ]

    for t in ticks:
        yy = y_to_px(t, plot, y_min, y_max, True)
        parts.append(f'<line x1="{x1 - 6}" y1="{yy}" x2="{x1}" y2="{yy}" stroke="{svg_rgb(BLACK)}" stroke-width="2" />')
        parts.append(f'<line x1="{x1}" y1="{yy}" x2="{x2}" y2="{yy}" stroke="{svg_rgb(LIGHT)}" stroke-width="1" />')
        parts.append(svg_text(x1 - 58, yy + 6, sci(t), SIZE_TINY, BLACK, "end"))

    for i, label in enumerate(x_labels):
        x = plot[0] + (plot[2] - plot[0]) * (i + 0.5) / count
        parts.append(svg_text(x, plot[3] + 44, label, SIZE_TINY, BLACK, "middle"))

    parts.append(polyline(vtk_points, BLACK, 4))
    parts.append(polyline(sys_points, BLUE, 4))

    for x, y in vtk_points:
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="7" fill="{svg_rgb(BLACK)}" />')
    for x, y in sys_points:
        parts.append(
            f'<rect x="{x - 7:.1f}" y="{y - 7:.1f}" width="14" height="14" '
            f'fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
        )

    for (x, y), label in zip(vtk_points, vtk_value_labels):
        parts.append(svg_text(x, y - 20, label, SIZE_TINY, BLACK, "middle"))
    for (x, y), label in zip(sys_points, sys_value_labels):
        parts.append(svg_text(x, y + 33, label, SIZE_TINY, BLUE, "middle"))

    center_x = (plot[0] + plot[2]) / 2
    parts.append(svg_text(center_x, plot[3] + 96, x_axis_label, SIZE_SMALL, BLACK, "middle"))

    legend_x = 1260
    parts.extend(
        [
            f'<line x1="{legend_x}" y1="240" x2="{legend_x + 70}" y2="240" stroke="{svg_rgb(BLACK)}" stroke-width="4" />',
            f'<circle cx="{legend_x + 36}" cy="240" r="7" fill="{svg_rgb(BLACK)}" />',
            svg_text(legend_x + 92, 250, "VTK 时间", SIZE_SMALL, BLACK, "start"),
            f'<line x1="{legend_x}" y1="305" x2="{legend_x + 70}" y2="305" stroke="{svg_rgb(BLUE)}" stroke-width="4" />',
            f'<rect x="{legend_x + 29}" y="298" width="14" height="14" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            svg_text(legend_x + 92, 315, "系统总时间", SIZE_SMALL, BLACK, "start"),
        ]
    )

    parts.append("</svg>")
    OUT.joinpath(name).write_text("\n".join(parts), encoding="utf-8")


def generate_data_model():
    img, d = canvas()
    title(d, "CAE 后处理数据组织")
    box(d, (90, 225, 340, 355), "VTK 数据集", F_LABEL, FILL)
    box(d, (480, 140, 720, 245), "点坐标\npoints", F_SMALL)
    box(d, (480, 285, 720, 390), "单元连接\ncells + offsets", F_SMALL)
    box(d, (480, 430, 720, 535), "点/单元字段\nfield arrays", F_SMALL)
    box(d, (480, 575, 720, 680), "邻域关系\nCSR", F_SMALL)
    box(d, (900, 235, 1250, 360), "统一内部数据对象", F_LABEL, FILL)
    box(d, (900, 500, 1250, 625), "GPU 连续缓冲区", F_LABEL, FILL)
    for y in [192, 337, 482, 627]:
        arrow(d, (340, 290), (480, y))
        arrow(d, (720, y), (900, 295))
    arrow(d, (1075, 360), (1075, 500))
    d.text((935, 390), "保留几何、拓扑、字段关联方式", font=F_SMALL, fill=DARK)
    save(img, "thesis_data_model.png")


def generate_compute_shader():
    img, d = canvas()
    title(d, "Compute Shader 计算模型")
    xs = [90, 400, 710, 1030, 1320]
    labels = ["CPU 数据准备", "SSBO 缓冲", "工作组 / 线程", "输出缓冲", "结果读回"]
    for x, lab in zip(xs, labels):
        box(d, (x, 320, x + 220, 430), lab, F_LABEL, FILL if "线程" in lab else WHITE)
    for i in range(len(xs) - 1):
        arrow(d, (xs[i] + 220, 375), (xs[i + 1], 375))
    d.text((105, 470), "坐标、字段、连接、邻域", font=F_SMALL, fill=DARK)
    d.text((728, 470), "每个调用处理一个点或单元", font=F_SMALL, fill=DARK)
    d.text((1038, 470), "内存屏障保证结果可见", font=F_SMALL, fill=DARK)
    save(img, "thesis_compute_shader_model.png")


def generate_regular_fd():
    img, d = canvas()
    title(d, "规则网格有限差分原理")
    origin = (270, 240)
    step = 110
    for j in range(4):
        for i in range(5):
            x = origin[0] + i * step
            y = origin[1] + j * step
            d.ellipse((x - 8, y - 8, x + 8, y + 8), fill=BLACK)
            if i < 4:
                d.line((x, y, x + step, y), fill=MID, width=2)
            if j < 3:
                d.line((x, y, x, y + step), fill=MID, width=2)
    cx, cy = origin[0] + 2 * step, origin[1] + 1 * step
    d.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), outline=BLACK, width=3, fill=WHITE)
    d.line((cx - step, cy, cx + step, cy), fill=BLACK, width=4)
    d.line((cx, cy - step, cx, cy + step), fill=BLACK, width=4)
    d.text((cx - step - 30, cy + 28), "i-1", font=F_TINY, fill=BLACK)
    d.text((cx - 8, cy + 28), "i", font=F_TINY, fill=BLACK)
    d.text((cx + step - 18, cy + 28), "i+1", font=F_TINY, fill=BLACK)
    box(d, (890, 240, 1260, 350), "参数空间差分", F_LABEL)
    box(d, (890, 485, 1260, 595), "物理空间梯度", F_LABEL)
    arrow(d, (1075, 350), (1075, 485))
    d.text((930, 390), "Jacobian / 链式法则", font=F_SMALL, fill=DARK)
    d.text((880, 650), "内部点中心差分，边界点单边差分", font=F_SMALL, fill=DARK)
    save(img, "thesis_regular_fd.png")


def generate_shape_function():
    img, d = canvas()
    title(d, "非结构化网格形函数导数原理")
    ref = [(190, 520), (440, 520), (315, 285)]
    phy = [(980, 565), (1290, 470), (1110, 260)]
    d.polygon(ref, outline=BLACK, fill=WHITE)
    d.polygon(phy, outline=BLACK, fill=WHITE)
    for p in ref + phy:
        d.ellipse((p[0] - 9, p[1] - 9, p[0] + 9, p[1] + 9), fill=BLACK)
    d.text((245, 555), "参考单元", font=F_LABEL, fill=BLACK)
    d.text((1050, 600), "物理单元", font=F_LABEL, fill=BLACK)
    arrow(d, (470, 410), (930, 410))
    d.text((610, 365), "几何映射 J", font=F_LABEL, fill=BLACK)
    box(d, (545, 510, 855, 615), "∇x N = J^-T ∇ξ N", F_LABEL, FILL)
    d.text((205, 230), "N1, N2, N3", font=F_SMALL, fill=DARK)
    d.text((950, 215), "节点坐标 + 单元连接", font=F_SMALL, fill=DARK)
    save(img, "thesis_shape_function.png")


def generate_cell_lift():
    img, d = canvas()
    title(d, "单元数据梯度恢复路径")
    box(d, (130, 250, 380, 360), "单元字段", F_LABEL)
    box(d, (520, 250, 790, 360), "点级中间场", F_LABEL, FILL)
    box(d, (930, 250, 1220, 360), "单元中心梯度", F_LABEL)
    box(d, (1360, 250, 1540, 360), "单元梯度", F_LABEL)
    arrow(d, (380, 305), (520, 305))
    arrow(d, (790, 305), (930, 305))
    arrow(d, (1220, 305), (1360, 305))
    d.text((420, 330), "邻接单元平均", font=F_SMALL, fill=DARK)
    d.text((815, 330), "形函数导数", font=F_SMALL, fill=DARK)
    # small mesh sketch
    pts = [(470, 550), (610, 500), (760, 560), (620, 650)]
    d.polygon([pts[0], pts[1], pts[3]], outline=MID, fill=None)
    d.polygon([pts[1], pts[2], pts[3]], outline=MID, fill=None)
    for p in pts:
        d.ellipse((p[0]-8, p[1]-8, p[0]+8, p[1]+8), fill=BLACK)
    d.text((520, 695), "单元值先恢复到节点，再在单元中心求导", font=F_SMALL, fill=DARK)
    save(img, "thesis_cell_lift.png")


def generate_bilateral():
    img, d = canvas()
    title(d, "图双边滤波权重")
    cx, cy = 480, 420
    neighbors = [(300, 310), (650, 300), (710, 480), (330, 565), (500, 250), (250, 440)]
    for p in neighbors:
        d.line((cx, cy, p[0], p[1]), fill=MID, width=2)
        d.ellipse((p[0] - 13, p[1] - 13, p[0] + 13, p[1] + 13), outline=BLACK, width=2, fill=WHITE)
    d.ellipse((cx - 18, cy - 18, cx + 18, cy + 18), fill=BLACK)
    d.text((cx - 15, cy + 35), "i", font=F_SMALL, fill=BLACK)
    box(d, (910, 260, 1290, 380), "空间距离", F_LABEL)
    box(d, (910, 465, 1290, 585), "字段差异", F_LABEL)
    arrow(d, (1110, 380), (1110, 465))
    d.text((1005, 420), "共同决定权重", font=F_SMALL, fill=DARK)
    d.text((265, 650), "邻域图 N(i)", font=F_LABEL, fill=BLACK)
    d.text((940, 635), "距离近且数值相近的样本权重更大", font=F_SMALL, fill=DARK)
    save(img, "thesis_bilateral.png")


def generate_multiscale():
    img, d = canvas()
    title(d, "多尺度分解与融合")
    boxes = [
        (120, 310, 300, 410, "u0"),
        (470, 310, 650, 410, "u1"),
        (820, 310, 1000, 410, "u2"),
        (1170, 310, 1350, 410, "uL"),
    ]
    for b in boxes:
        box(d, b[:4], b[4], F_LABEL, FILL if b[4] == "uL" else WHITE)
    for a, b in zip(boxes, boxes[1:]):
        arrow(d, (a[2], 360), (b[0], 360))
    d.text((335, 325), "B0", font=F_LABEL, fill=BLACK)
    d.text((685, 325), "B1", font=F_LABEL, fill=BLACK)
    d.text((1035, 325), "…", font=F_LABEL, fill=BLACK)
    d.line((210, 410, 560, 585), fill=MID, width=2)
    d.line((560, 410, 910, 585), fill=MID, width=2)
    box(d, (450, 585, 670, 680), "d0 = u0-u1", F_SMALL)
    box(d, (800, 585, 1020, 680), "d1 = u1-u2", F_SMALL)
    box(d, (1180, 585, 1460, 680), "uL + Σ αl dl", F_SMALL, FILL)
    arrow(d, (670, 633), (1180, 633))
    arrow(d, (1020, 633), (1180, 633))
    arrow(d, (1260, 410), (1320, 585))
    save(img, "thesis_multiscale.png")


def log_bounds(values: list[float]) -> tuple[float, float]:
    positive = [v for v in values if v > 0]
    if not positive:
        return 0.1, 10.0
    y_min = 10 ** math.floor(math.log10(min(positive) * 0.95))
    y_max = 10 ** math.ceil(math.log10(max(positive) * 1.1))
    y_min = min(y_min, 0.1)
    if y_max <= y_min:
        y_max = y_min * 10
    return y_min, y_max


def draw_timing_line_chart(
    name: str,
    caption: str,
    x_labels: list[str],
    vtk_values: list[float],
    sys_values: list[float],
    x_axis_label: str,
    vtk_value_labels: list[str] | None = None,
    sys_value_labels: list[str] | None = None,
):
    img, d = canvas(1500, 880)
    plot = (180, 170, 1190, 670)
    y_min, y_max = log_bounds(vtk_values + sys_values)
    draw_axes(d, plot, y_min, y_max, True, "时间 / ms")

    d.text((plot[0], 92), caption, font=F_LABEL, fill=BLACK)

    count = len(x_labels)
    vtk_points: list[tuple[int, int]] = []
    sys_points: list[tuple[int, int]] = []
    if vtk_value_labels is None:
        vtk_value_labels = [f"{v:g}" for v in vtk_values]
    if sys_value_labels is None:
        sys_value_labels = [f"{v:g}" for v in sys_values]
    for i, label in enumerate(x_labels):
        x = int(plot[0] + (plot[2] - plot[0]) * (i + 0.5) / count)
        text_box = d.textbbox((0, 0), label, font=F_TINY)
        d.text((x - (text_box[2] - text_box[0]) / 2, plot[3] + 26), label, font=F_TINY, fill=BLACK)
        vtk_points.append((x, y_to_px(vtk_values[i], plot, y_min, y_max, True)))
        sys_points.append((x, y_to_px(sys_values[i], plot, y_min, y_max, True)))

    d.line(vtk_points, fill=BLACK, width=4)
    d.line(sys_points, fill=BLUE, width=4)
    for x, y in vtk_points:
        d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=BLACK)
    for x, y in sys_points:
        d.rectangle((x - 7, y - 7, x + 7, y + 7), fill=WHITE, outline=BLUE, width=3)

    for (x, y), label in zip(vtk_points, vtk_value_labels):
        tb = d.textbbox((0, 0), label, font=F_TINY)
        d.text((x - (tb[2] - tb[0]) / 2, y - 34), label, font=F_TINY, fill=BLACK)
    for (x, y), label in zip(sys_points, sys_value_labels):
        tb = d.textbbox((0, 0), label, font=F_TINY)
        d.text((x - (tb[2] - tb[0]) / 2, y + 12), label, font=F_TINY, fill=BLUE)

    center_x = (plot[0] + plot[2]) / 2
    axis_box = d.textbbox((0, 0), x_axis_label, font=F_SMALL)
    d.text((center_x - (axis_box[2] - axis_box[0]) / 2, plot[3] + 72), x_axis_label, font=F_SMALL, fill=BLACK)

    legend_x = 1260
    d.line((legend_x, 240, legend_x + 70, 240), fill=BLACK, width=4)
    d.ellipse((legend_x + 29, 233, legend_x + 43, 247), fill=BLACK)
    d.text((legend_x + 92, 227), "VTK 时间", font=F_SMALL, fill=BLACK)

    d.line((legend_x, 305, legend_x + 70, 305), fill=BLUE, width=4)
    d.rectangle((legend_x + 29, 298, legend_x + 43, 312), fill=WHITE, outline=BLUE, width=3)
    d.text((legend_x + 92, 292), "系统总时间", font=F_SMALL, fill=BLACK)

    save(img, name)
    save_timing_line_chart_svg(
        Path(name).with_suffix(".svg").name,
        caption,
        x_labels,
        vtk_values,
        sys_values,
        x_axis_label,
        vtk_value_labels,
        sys_value_labels,
    )


def generate_chapter5_timing_lines():
    draw_timing_line_chart(
        "thesis_timing_structured_point_line.png",
        "结构化网格点数据",
        ["8000", "32768", "110592"],
        [2.7573, 3.5564, 8.2716],
        [0.9036, 1.4336, 4.0996],
        "点数",
        ["2.7573", "3.5564", "8.2716"],
        ["0.9036", "1.4336", "4.0996"],
    )
    draw_timing_line_chart(
        "thesis_timing_structured_cell_line.png",
        "结构化网格单元数据",
        ["6859", "29791", "103823"],
        [123.967, 484.785, 1701.75],
        [0.8455, 1.1606, 2.01384],
        "单元数",
        ["123.967", "484.785", "1701.75"],
        ["0.8455", "1.1606", "2.01384"],
    )
    draw_timing_line_chart(
        "thesis_timing_unstructured_point_line.png",
        "非结构化网格点数据",
        ["8000", "32768", "110592"],
        [163.799, 708.650, 3349.36],
        [2.8527, 5.3835, 44.8039],
        "点数",
        ["163.799", "708.650", "3349.36"],
        ["2.8527", "5.3835", "44.8039"],
    )
    draw_timing_line_chart(
        "thesis_timing_unstructured_cell_line.png",
        "非结构化网格单元数据",
        ["6859", "29791", "103823"],
        [29.4648, 99.9974, 470.667],
        [1.6555, 2.1840, 6.3771],
        "单元数",
        ["29.4648", "99.9974", "470.667"],
        ["1.6555", "2.1840", "6.3771"],
    )


def generate_opengl_sequence():
    img, d = canvas()
    title(d, "OpenGL 计算执行时序")
    cols = [(180, "CPU"), (560, "缓冲区"), (940, "Compute Shader"), (1320, "结果字段")]
    y_top, y_bottom = 180, 760
    for x, lab in cols:
        box(d, (x - 100, 150, x + 100, 215), lab, F_LABEL, FILL)
        d.line((x, 215, x, y_bottom), fill=MID, width=2)
    events = [
        (250, 180, 560, "上传数组"),
        (360, 180, 940, "设置参数并派发"),
        (500, 940, 560, "写入输出缓冲"),
        (610, 560, 180, "同步并读回"),
        (710, 180, 1320, "写回字段并导出"),
    ]
    for y, x1, x2, lab in events:
        arrow(d, (x1, y), (x2, y))
        d.text(((x1 + x2) / 2 - 80, y - 32), lab, font=F_SMALL, fill=BLACK)
    save(img, "thesis_opengl_sequence.png")


def generate_flat_offset():
    img, d = canvas()
    title(d, "连续数组与偏移数组")
    y = 260
    source = [("c0", "[0,1,2]"), ("c1", "[2,3,4,5]"), ("c2", "[5,6,7]")]
    for i, (name, val) in enumerate(source):
        box(d, (120, y + i * 120, 350, y + i * 120 + 75), f"{name}  {val}", F_SMALL)
    arrow(d, (375, 420), (520, 420))
    vals = ["0", "1", "2", "2", "3", "4", "5", "5", "6", "7"]
    x0 = 560
    for i, val in enumerate(vals):
        d.rectangle((x0 + i * 78, 275, x0 + i * 78 + 74, 345), outline=BLACK, width=2)
        text_center(d, (x0 + i * 78, 275, x0 + i * 78 + 74, 345), val, F_SMALL)
    d.text((560, 230), "cells", font=F_LABEL, fill=BLACK)
    offsets = ["0", "3", "7", "10"]
    for i, val in enumerate(offsets):
        d.rectangle((560 + i * 120, 535, 560 + i * 120 + 100, 605), outline=BLACK, width=2)
        text_center(d, (560 + i * 120, 535, 560 + i * 120 + 100, 605), val, F_SMALL)
    d.text((560, 490), "cellOffsets", font=F_LABEL, fill=BLACK)
    d.text((1080, 505), "cells[offset[e] ... offset[e+1]-1]", font=F_SMALL, fill=DARK)
    save(img, "thesis_flat_offset.png")


def generate_gui_layout():
    img, d = canvas()
    title(d, "界面功能区域")
    d.rectangle((130, 155, 1470, 760), outline=BLACK, width=3)
    d.rectangle((130, 155, 1470, 220), outline=BLACK, width=2, fill=FILL)
    d.text((160, 175), "CAE 数据预处理系统", font=F_LABEL, fill=BLACK)
    box(d, (170, 270, 440, 630), "数据集\n列表", F_LABEL)
    box(d, (520, 270, 850, 430), "字段\n选择", F_LABEL)
    box(d, (930, 270, 1340, 430), "加载 / 计算 / 导出", F_LABEL)
    box(d, (520, 500, 1340, 675), "运行日志与结果信息", F_LABEL)
    save(img, "thesis_gui_layout.png")


def draw_axes(
    d: ImageDraw.ImageDraw,
    plot: tuple[int, int, int, int],
    y_min: float,
    y_max: float,
    log_y: bool = False,
    y_label: str = "",
):
    x1, y1, x2, y2 = plot
    d.line((x1, y2, x2, y2), fill=BLACK, width=2)
    d.line((x1, y1, x1, y2), fill=BLACK, width=2)
    ticks = []
    if log_y:
        lo = math.floor(math.log10(y_min))
        hi = math.ceil(math.log10(y_max))
        ticks = [10 ** p for p in range(lo, hi + 1)]
        ticks = [t for t in ticks if y_min <= t <= y_max]
        if y_min not in ticks:
            ticks.insert(0, y_min)
        if y_max not in ticks:
            ticks.append(y_max)
    else:
        step = (y_max - y_min) / 4
        ticks = [y_min + i * step for i in range(5)]
    for t in ticks:
        yy = y_to_px(t, plot, y_min, y_max, log_y)
        d.line((x1 - 6, yy, x1, yy), fill=BLACK, width=2)
        d.line((x1, yy, x2, yy), fill=LIGHT, width=1)
        label = sci(t) if log_y else f"{t:.2g}"
        d.text((x1 - 85, yy - 10), label, font=F_TINY, fill=BLACK)
    if y_label:
        d.text((x1 - 110, y1 - 45), y_label, font=F_SMALL, fill=BLACK)


def y_to_px(v: float, plot: tuple[int, int, int, int], y_min: float, y_max: float, log_y: bool = False) -> int:
    x1, y1, x2, y2 = plot
    if log_y:
        v = max(v, y_min)
        a = (math.log10(v) - math.log10(y_min)) / (math.log10(y_max) - math.log10(y_min))
    else:
        a = (v - y_min) / (y_max - y_min)
    return int(y2 - a * (y2 - y1))


def sci(v: float) -> str:
    if v <= 0:
        return "0"
    exp = int(round(math.log10(v)))
    if math.isclose(v, 10 ** exp, rel_tol=1e-9):
        return f"1e{exp}"
    return f"{v:g}"


def grouped_bar_log(name: str, title_text: str, categories: list[str], series: list[tuple[str, list[float], tuple[int, int, int]]], y_min: float, y_max: float):
    img, d = canvas(1600, 900)
    title(d, title_text)
    plot = (170, 170, 1490, 720)
    draw_axes(d, plot, y_min, y_max, True, "NMAE")
    group_w = (plot[2] - plot[0]) / len(categories)
    bar_w = min(46, group_w / (len(series) + 1.2))
    for gi, cat in enumerate(categories):
        center = plot[0] + group_w * (gi + 0.5)
        d.text((center - 52, plot[3] + 25), cat, font=F_TINY, fill=BLACK)
        for si, (_, vals, color) in enumerate(series):
            x = center - (len(series) * bar_w) / 2 + si * bar_w
            y = y_to_px(vals[gi], plot, y_min, y_max, True)
            d.rectangle((x, y, x + bar_w * 0.75, plot[3]), fill=color, outline=BLACK)
    lx = plot[2] - 320
    for i, (lab, _, color) in enumerate(series):
        d.rectangle((lx, 95 + i * 38, lx + 32, 120 + i * 38), fill=color, outline=BLACK)
        d.text((lx + 45, 93 + i * 38), lab, font=F_TINY, fill=BLACK)
    save(img, name)


def line_log_timing():
    img, d = canvas(1700, 950)
    title(d, "时间性能对比")
    panels = [
        ((140, 165, 790, 735), "点数据", ["S20", "S32", "S48", "U20", "U32", "U48"],
         [2.7573, 3.5564, 8.2716, 163.799, 708.650, 3349.36],
         [0.9036, 1.4336, 4.0996, 2.8527, 5.3835, 44.8039]),
        ((960, 165, 1610, 735), "单元数据", ["S20", "S32", "S48", "U20", "U32", "U48"],
         [123.967, 484.785, 1701.75, 29.4648, 99.9974, 470.667],
         [0.8455, 1.1606, 2.01384, 1.6555, 2.1840, 6.3771]),
    ]
    for plot, lab, cats, vtk, sys in panels:
        d.text((plot[0], plot[1] - 60), lab, font=F_LABEL, fill=BLACK)
        draw_axes(d, plot, 0.5, 5000, True, "ms")
        n = len(cats)
        pts_vtk, pts_sys = [], []
        for i, cat in enumerate(cats):
            x = plot[0] + (plot[2] - plot[0]) * (i + 0.5) / n
            d.text((x - 22, plot[3] + 24), cat, font=F_TINY, fill=BLACK)
            pts_vtk.append((x, y_to_px(vtk[i], plot, 0.5, 5000, True)))
            pts_sys.append((x, y_to_px(sys[i], plot, 0.5, 5000, True)))
        d.line(pts_vtk, fill=BLACK, width=3)
        d.line(pts_sys, fill=DARK, width=3)
        for p in pts_vtk:
            d.ellipse((p[0]-5, p[1]-5, p[0]+5, p[1]+5), fill=BLACK)
        for p in pts_sys:
            d.rectangle((p[0]-5, p[1]-5, p[0]+5, p[1]+5), fill=WHITE, outline=BLACK, width=2)
    d.line((700, 835, 760, 835), fill=BLACK, width=3)
    d.text((780, 822), "VTK 并行时间", font=F_SMALL, fill=BLACK)
    d.line((1010, 835, 1070, 835), fill=DARK, width=3)
    d.rectangle((1035, 830, 1045, 840), fill=WHITE, outline=BLACK, width=2)
    d.text((1090, 822), "系统总时间", font=F_SMALL, fill=BLACK)
    save(img, "thesis_timing_compare.png")


def speedup_chart():
    img, d = canvas(1600, 880)
    title(d, "相对 VTK 并行时间加速比")
    plot = (170, 165, 1490, 710)
    draw_axes(d, plot, 1, 1000, True, "倍")
    cats = ["规则点", "规则单元", "非结构点", "非结构单元"]
    vals = [
        [3.051, 2.481, 2.018],
        [146.620, 417.702, 845.027],
        [57.419, 131.634, 74.756],
        [17.798, 45.786, 73.806],
    ]
    colors = [WHITE, MID, DARK]
    group_w = (plot[2] - plot[0]) / len(cats)
    bar_w = 56
    for gi, cat in enumerate(cats):
        center = plot[0] + group_w * (gi + 0.5)
        d.text((center - 45, plot[3] + 28), cat, font=F_TINY, fill=BLACK)
        for si in range(3):
            x = center - bar_w * 1.5 + si * bar_w
            y = y_to_px(vals[gi][si], plot, 1, 1000, True)
            d.rectangle((x, y, x + 40, plot[3]), fill=colors[si], outline=BLACK, width=2)
    for i, lab in enumerate(["20", "32", "48"]):
        d.rectangle((1120 + i * 100, 95, 1150 + i * 100, 122), fill=colors[i], outline=BLACK)
        d.text((1160 + i * 100, 96), lab, font=F_TINY, fill=BLACK)
    save(img, "thesis_speedup_compare.png")


def generate_experiment_charts():
    grouped_bar_log(
        "thesis_analytic_validation.png",
        "解析场梯度误差",
        ["S 点", "H 点", "1_0 点", "S 单元", "H 单元", "1_0 单元"],
        [
            ("标量场", [2.12896e-7, 1.14546e-7, 5.92162e-8, 2.03698e-7, 7.01308e-2, 1.05548e-1], BLACK),
            ("向量场", [2.56902e-7, 1.83220e-7, 1.11066e-7, 2.47084e-7, 6.78263e-2, 1.00933e-1], MID),
        ],
        1e-8,
        1,
    )
    grouped_bar_log(
        "thesis_vtk_consistency.png",
        "与 VTK 结果一致性误差",
        ["S 点", "H 点", "1_0 点", "S 单元", "L 单元", "1_0 单元"],
        [
            ("NMAE", [7.01208e-8, 5.16345e-8, 2.91327e-8, 6.62593e-7, 4.60322e-7, 7.08797e-8], BLACK),
        ],
        1e-8,
        1e-5,
    )
    line_log_timing()
    speedup_chart()
    grouped_bar_log(
        "thesis_optimization_error.png",
        "数据优化前后误差",
        ["点数据", "单元数据"],
        [
            ("输入", [0.27032675, 0.27004475], BLACK),
            ("优化后", [0.14413250, 0.11083258], MID),
        ],
        0.05,
        0.5,
    )


def generate_render_compare():
    src = ROOT / "wendang" / "assets" / "denoise_render_compare.png"
    if not src.exists():
        return
    img = Image.open(src).convert("RGB")
    # Recompose the ParaView screenshots to remove the slide title, duplicated color bars
    # and stray right-side annotation from the original exported comparison image.
    left = img.crop((40, 160, 990, 475))
    right = img.crop((1210, 160, 2390, 475))
    gap = 48
    margin = 36
    label_h = 48
    out = Image.new("RGB", (left.width + right.width + gap + margin * 2, left.height + label_h + margin), WHITE)
    d = ImageDraw.Draw(out)
    d.text((margin + 8, 8), "优化前", font=F_LABEL, fill=BLACK)
    d.text((margin + left.width + gap + 8, 8), "优化后", font=F_LABEL, fill=BLACK)
    out.paste(left, (margin, label_h))
    out.paste(right, (margin + left.width + gap, label_h))
    out.save(OUT / "thesis_optimization_render.png")


def main():
    generate_data_model()
    generate_compute_shader()
    generate_regular_fd()
    generate_shape_function()
    generate_cell_lift()
    generate_bilateral()
    generate_multiscale()
    generate_opengl_sequence()
    generate_flat_offset()
    generate_gui_layout()
    generate_experiment_charts()
    generate_chapter5_timing_lines()
    generate_render_compare()
    print(f"generated figures in {OUT}")


if __name__ == "__main__":
    main()
