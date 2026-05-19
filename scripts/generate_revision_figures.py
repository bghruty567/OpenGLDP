from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


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


def font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    if FONT_PATH is None:
        return ImageFont.load_default()
    return ImageFont.truetype(str(FONT_PATH), size)


BLACK = (20, 20, 20)
WHITE = (255, 255, 255)
GRAY = (246, 246, 246)


def draw_centered_box(
    draw: ImageDraw.ImageDraw,
    xy: tuple[int, int, int, int],
    text: str,
    text_font: ImageFont.ImageFont,
    fill: tuple[int, int, int] = WHITE,
) -> None:
    draw.rectangle(xy, outline=BLACK, width=3, fill=fill)
    x1, y1, x2, y2 = xy
    lines = text.split("\n")
    heights = []
    widths = []
    for line in lines:
        bbox = draw.textbbox((0, 0), line, font=text_font)
        widths.append(bbox[2] - bbox[0])
        heights.append(bbox[3] - bbox[1])
    total_h = sum(heights) + max(0, len(lines) - 1) * 8
    y = y1 + (y2 - y1 - total_h) / 2
    for line, width, height in zip(lines, widths, heights):
        draw.text((x1 + (x2 - x1 - width) / 2, y), line, fill=BLACK, font=text_font)
        y += height + 8


def draw_arrow(draw: ImageDraw.ImageDraw, start: tuple[int, int], end: tuple[int, int]) -> None:
    draw.line([start, end], fill=BLACK, width=3)
    x1, y1 = start
    x2, y2 = end
    import math

    angle = math.atan2(y2 - y1, x2 - x1)
    size = 14
    p1 = (x2 - size * math.cos(angle - math.pi / 6), y2 - size * math.sin(angle - math.pi / 6))
    p2 = (x2 - size * math.cos(angle + math.pi / 6), y2 - size * math.sin(angle + math.pi / 6))
    draw.polygon([end, p1, p2], fill=BLACK)


def generate_compute_shader_model() -> None:
    img = Image.new("RGB", (1700, 860), WHITE)
    draw = ImageDraw.Draw(img)
    title_font = font(42)
    box_font = font(28)
    small_font = font(24)

    draw.text((70, 45), "OpenGL Compute Shader 计算模型", fill=BLACK, font=title_font)
    boxes = [
        (90, 260, 340, 390, "CPU 侧\n数据准备"),
        (450, 260, 700, 390, "SSBO\n缓冲区"),
        (810, 230, 1110, 420, "Compute Shader\n工作组/线程"),
        (1220, 260, 1470, 390, "输出\n缓冲区"),
        (570, 610, 1000, 735, "同步与结果读回\n字段写回 / VTK 导出"),
    ]
    for x1, y1, x2, y2, text in boxes:
        draw_centered_box(draw, (x1, y1, x2, y2), text, box_font, GRAY if "Compute" in text else WHITE)
    for start, end in [
        ((340, 325), (450, 325)),
        ((700, 325), (810, 325)),
        ((1110, 325), (1220, 325)),
        ((1345, 390), (1000, 650)),
        ((570, 650), (340, 390)),
    ]:
        draw_arrow(draw, start, end)
    draw.text((145, 425), "点坐标、字段、连接、邻域", fill=BLACK, font=small_font)
    draw.text((825, 455), "每个线程处理一个点或单元\n通过 gl_GlobalInvocationID 定位数据", fill=BLACK, font=small_font)
    draw.text((1040, 640), "glMemoryBarrier / 计时查询", fill=BLACK, font=small_font)
    img.save(OUT / "fig_compute_shader_model.png")


def generate_system_function_flow() -> None:
    img = Image.new("RGB", (1850, 900), WHITE)
    draw = ImageDraw.Draw(img)
    title_font = font(42)
    box_font = font(30)
    small_font = font(24)

    draw.text((70, 45), "系统功能流程图", fill=BLACK, font=title_font)
    boxes = [
        (70, 260, 300, 380, "VTK\n数据文件"),
        (390, 260, 650, 380, "数据读取\n与转换"),
        (740, 260, 1000, 380, "任务选择\n字段检查"),
        (1060, 160, 1435, 330, "梯度计算\n规则网格：有限差分\n非结构化：形函数导数"),
        (1060, 430, 1435, 600, "数据优化\n图双边滤波\n多尺度融合"),
        (1490, 300, 1740, 450, "结果写回\n与导出"),
    ]
    for x1, y1, x2, y2, text in boxes:
        draw_centered_box(draw, (x1, y1, x2, y2), text, box_font)
    for start, end in [
        ((300, 320), (390, 320)),
        ((650, 320), (740, 320)),
        ((1000, 320), (1060, 245)),
        ((1000, 320), (1060, 515)),
        ((1435, 245), (1490, 365)),
        ((1435, 515), (1490, 365)),
    ]:
        draw_arrow(draw, start, end)
    img.save(OUT / "fig_system_function_flow.png")


def generate_flat_offset_array() -> None:
    img = Image.new("RGB", (1700, 850), WHITE)
    draw = ImageDraw.Draw(img)
    title_font = font(42)
    box_font = font(28)
    small_font = font(24)

    draw.text((70, 45), "连续数组与偏移数组组织方式", fill=BLACK, font=title_font)
    draw.text((90, 150), "变长单元连接", fill=BLACK, font=box_font)

    source_boxes = [
        (90, 220, 340, 310, "单元 0\n[0, 1, 2]"),
        (90, 350, 340, 440, "单元 1\n[2, 3, 4, 5]"),
        (90, 480, 340, 570, "单元 2\n[5, 6, 7]"),
    ]
    for item in source_boxes:
        draw_centered_box(draw, item[:4], item[4], small_font)

    draw_arrow(draw, (360, 395), (530, 395))
    draw.text((540, 150), "连续连接数组 cells", fill=BLACK, font=box_font)
    x0, y0 = 540, 260
    values = ["0", "1", "2", "2", "3", "4", "5", "5", "6", "7"]
    for idx, val in enumerate(values):
        x1 = x0 + idx * 82
        draw.rectangle((x1, y0, x1 + 80, y0 + 80), outline=BLACK, width=2, fill=WHITE)
        bbox = draw.textbbox((0, 0), val, font=box_font)
        draw.text((x1 + 40 - (bbox[2] - bbox[0]) / 2, y0 + 40 - (bbox[3] - bbox[1]) / 2), val, fill=BLACK, font=box_font)
        draw.text((x1 + 28, y0 + 92), str(idx), fill=BLACK, font=small_font)

    draw.text((540, 500), "偏移数组 cellOffsets", fill=BLACK, font=box_font)
    offsets = ["0", "3", "7", "10"]
    for idx, val in enumerate(offsets):
        x1 = x0 + idx * 120
        draw.rectangle((x1, 585, x1 + 110, 665), outline=BLACK, width=2, fill=WHITE)
        bbox = draw.textbbox((0, 0), val, font=box_font)
        draw.text((x1 + 55 - (bbox[2] - bbox[0]) / 2, 625 - (bbox[3] - bbox[1]) / 2), val, fill=BLACK, font=box_font)

    draw.text((1080, 480), "第 e 个单元的节点范围：", fill=BLACK, font=small_font)
    draw.text((1080, 530), "cells[cellOffsets[e] ... cellOffsets[e+1]-1]", fill=BLACK, font=small_font)
    draw.text((1080, 595), "该结构适合表达节点数不固定的单元，\n也便于 GPU 线程按 offset 定位局部数据。", fill=BLACK, font=small_font)
    img.save(OUT / "fig_flat_offset_array.png")


def generate_gui_layout() -> None:
    img = Image.new("RGB", (1700, 900), WHITE)
    draw = ImageDraw.Draw(img)
    title_font = font(42)
    box_font = font(26)
    small_font = font(22)

    draw.text((70, 45), "系统界面功能区域示意图", fill=BLACK, font=title_font)
    draw.rectangle((90, 150, 1610, 780), outline=BLACK, width=3, fill=WHITE)
    draw.rectangle((90, 150, 1610, 220), outline=BLACK, width=2, fill=GRAY)
    draw.text((120, 170), "CAE 数据预处理系统", fill=BLACK, font=box_font)

    draw_centered_box(draw, (120, 250, 450, 560), "数据集列表\n\n已加载文件\n网格类型\n点数/单元数", small_font)
    draw_centered_box(draw, (500, 250, 970, 430), "字段选择区\n\n点数据 / 单元数据\n字段名称 / 分量数", small_font)
    draw_centered_box(draw, (1020, 250, 1530, 430), "操作区\n\n加载数据  梯度计算\n数据优化  导出结果", small_font)
    draw_centered_box(draw, (500, 470, 1530, 730), "运行日志与结果信息\n\n加载状态、计算耗时、输出字段名称、导出路径", small_font)
    img.save(OUT / "fig_gui_layout.png")


if __name__ == "__main__":
    generate_compute_shader_model()
    generate_system_function_flow()
    generate_flat_offset_array()
    generate_gui_layout()
