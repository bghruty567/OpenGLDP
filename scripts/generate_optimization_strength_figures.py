from __future__ import annotations

import csv
from pathlib import Path
from xml.sax.saxutils import escape

from PIL import Image, ImageChops, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "wendang" / "pic"
OUT.mkdir(parents=True, exist_ok=True)

RESULT_DIR = Path(
    r"C:\Users\lenovo\Desktop\bishe\myProj\build-OpenGLDP-Desktop_Qt_5_15_2_MSVC2019_64bit-Debug\results"
)

FONT_CANDIDATES = [
    Path(r"C:\Windows\Fonts\NotoSerifSC-VF.ttf"),
    Path(r"C:\Windows\Fonts\NotoSansSC-VF.ttf"),
    Path(r"C:\Windows\Fonts\simsun.ttc"),
    Path(r"C:\Windows\Fonts\msyh.ttc"),
    Path(r"C:\Windows\Fonts\simhei.ttf"),
]
FONT_PATH = next((path for path in FONT_CANDIDATES if path.exists()), None)

WHITE = (255, 255, 255)
BLACK = (32, 32, 32)
GRAY = (110, 110, 110)
LIGHT = (228, 228, 228)
BLUE = (78, 111, 155)
SVG_FONT_FAMILY = "'Noto Serif SC','Noto Sans CJK SC','SimSun','Microsoft YaHei',serif"


def font(size: int) -> ImageFont.ImageFont:
    if FONT_PATH is None:
        return ImageFont.load_default()
    return ImageFont.truetype(str(FONT_PATH), size)


F_TITLE = font(28)
F_PANEL = font(24)
F_LABEL = font(21)
F_TICK = font(18)


def svg_rgb(color: tuple[int, int, int]) -> str:
    return f"rgb({color[0]},{color[1]},{color[2]})"


def save_png(img: Image.Image, name: str) -> None:
    bg = Image.new(img.mode, img.size, WHITE)
    bbox = ImageChops.difference(img, bg).getbbox()
    if bbox:
        margin = 44
        x1 = max(0, bbox[0] - margin)
        y1 = max(0, bbox[1] - margin)
        x2 = min(img.width, bbox[2] + margin)
        y2 = min(img.height, bbox[3] + margin)
        img = img.crop((x1, y1, x2, y2))
    img.save(OUT / name)


def value_label(v: float) -> str:
    return f"{v:.3f}"


def load_rows(csv_path: Path) -> list[dict[str, str]]:
    with csv_path.open("r", encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def mean(rows: list[dict[str, str]], key: str) -> float:
    return sum(float(r[key]) for r in rows) / len(rows)


def aggregate_series(paths: list[Path]) -> dict[str, list[float]]:
    sigma: list[float] = []
    input_nrmse: list[float] = []
    fused_nrmse: list[float] = []
    ratio: list[float] = []
    for path in paths:
        rows = load_rows(path)
        sigma.append(float(rows[0]["sigma_factor"]))
        input_nrmse.append(mean(rows, "input_nrmse"))
        fused_nrmse.append(mean(rows, "fused_nrmse"))
        ratio.append(mean(rows, "rmse_improvement_ratio"))
    return {
        "sigma": sigma,
        "input_nrmse": input_nrmse,
        "fused_nrmse": fused_nrmse,
        "ratio": ratio,
    }


def text_bbox(draw: ImageDraw.ImageDraw, text: str, ft: ImageFont.ImageFont) -> tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=ft)
    return box[2] - box[0], box[3] - box[1]


def draw_marker(draw: ImageDraw.ImageDraw,
                x: int,
                y: int,
                color: tuple[int, int, int],
                kind: str) -> None:
    if kind == "circle":
        draw.ellipse((x - 6, y - 6, x + 6, y + 6), fill=color)
    else:
        draw.rectangle((x - 6, y - 6, x + 6, y + 6), fill=WHITE, outline=color, width=3)


def svg_text(x: float,
             y: float,
             text: str,
             size: int,
             fill: tuple[int, int, int] = BLACK,
             anchor: str = "start") -> str:
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" '
        f'font-family="{SVG_FONT_FAMILY}" font-size="{size}" '
        f'fill="{svg_rgb(fill)}">{escape(text)}</text>'
    )


def draw_nrmse_chart(point: dict[str, list[float]], cell: dict[str, list[float]]) -> None:
    width, height = 1700, 860
    img = Image.new("RGB", (width, height), WHITE)
    draw = ImageDraw.Draw(img)

    title = "不同高斯扰动强度下平均 NRMSE 对比"
    tw, _ = text_bbox(draw, title, F_TITLE)
    draw.text(((width - tw) / 2, 42), title, font=F_TITLE, fill=BLACK)

    panels = [
        ("点数据", point, (120, 170, 780, 690)),
        ("单元数据", cell, (900, 170, 1560, 690)),
    ]
    y_min = 0.0
    y_max = 0.55
    y_ticks = [0.0, 0.1, 0.2, 0.3, 0.4, 0.5]

    svg_parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="{svg_rgb(WHITE)}" />',
        svg_text(width / 2, 74, title, 28, BLACK, "middle"),
    ]

    for panel_title, data, (x1, y1, x2, y2) in panels:
        draw.rectangle((x1, y1, x2, y2), outline=BLACK, width=2)
        ptw, _ = text_bbox(draw, panel_title, F_PANEL)
        draw.text(((x1 + x2 - ptw) / 2, 126), panel_title, font=F_PANEL, fill=BLACK)
        svg_parts.append(f'<rect x="{x1}" y="{y1}" width="{x2 - x1}" height="{y2 - y1}" fill="none" stroke="{svg_rgb(BLACK)}" stroke-width="2" />')
        svg_parts.append(svg_text((x1 + x2) / 2, 148, panel_title, 24, BLACK, "middle"))

        def sy(v: float) -> float:
            return y2 - (v - y_min) / (y_max - y_min) * (y2 - y1)

        for t in y_ticks:
            yy = sy(t)
            draw.line((x1, yy, x2, yy), fill=LIGHT, width=1)
            label = f"{t:.1f}"
            lw, lh = text_bbox(draw, label, F_TICK)
            draw.text((x1 - 18 - lw, yy - lh / 2), label, font=F_TICK, fill=GRAY)
            svg_parts.append(f'<line x1="{x1}" y1="{yy:.1f}" x2="{x2}" y2="{yy:.1f}" stroke="{svg_rgb(LIGHT)}" stroke-width="1" />')
            svg_parts.append(svg_text(x1 - 18, yy + 6, label, 18, GRAY, "end"))

        draw.text((x1 - 26, y1 - 44), "NRMSE", font=F_LABEL, fill=BLACK)
        svg_parts.append(svg_text(x1 - 26, y1 - 24, "NRMSE", 21, BLACK, "start"))

        count = len(data["sigma"])
        xs: list[int] = []
        for i, sigma in enumerate(data["sigma"]):
            x = int(x1 + (x2 - x1) * (i + 0.5) / count)
            xs.append(x)
            label = f"{sigma:.2f}"
            lw, _ = text_bbox(draw, label, F_TICK)
            draw.text((x - lw / 2, y2 + 22), label, font=F_TICK, fill=BLACK)
            svg_parts.append(svg_text(x, y2 + 42, label, 18, BLACK, "middle"))

        center_x = (x1 + x2) / 2
        x_label = "扰动强度系数"
        lw, _ = text_bbox(draw, x_label, F_LABEL)
        draw.text((center_x - lw / 2, y2 + 62), x_label, font=F_LABEL, fill=BLACK)
        svg_parts.append(svg_text(center_x, y2 + 90, x_label, 21, BLACK, "middle"))

        input_points: list[tuple[int, int]] = [(xs[i], int(sy(v))) for i, v in enumerate(data["input_nrmse"])]
        fused_points: list[tuple[int, int]] = [(xs[i], int(sy(v))) for i, v in enumerate(data["fused_nrmse"])]
        draw.line(input_points, fill=BLACK, width=3)
        draw.line(fused_points, fill=BLUE, width=3)
        svg_parts.append(
            f'<polyline points="{" ".join(f"{x},{y}" for x, y in input_points)}" fill="none" stroke="{svg_rgb(BLACK)}" stroke-width="3" />'
        )
        svg_parts.append(
            f'<polyline points="{" ".join(f"{x},{y}" for x, y in fused_points)}" fill="none" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
        )

        for (x, y), v in zip(input_points, data["input_nrmse"]):
            draw_marker(draw, x, y, BLACK, "circle")
            label = value_label(v)
            lw, _ = text_bbox(draw, label, F_TICK)
            draw.text((x - lw / 2, y - 34), label, font=F_TICK, fill=BLACK)
            svg_parts.append(f'<circle cx="{x}" cy="{y}" r="6" fill="{svg_rgb(BLACK)}" />')
            svg_parts.append(svg_text(x, y - 16, label, 18, BLACK, "middle"))

        for (x, y), v in zip(fused_points, data["fused_nrmse"]):
            draw_marker(draw, x, y, BLUE, "square")
            label = value_label(v)
            lw, _ = text_bbox(draw, label, F_TICK)
            draw.text((x - lw / 2, y + 12), label, font=F_TICK, fill=BLUE)
            svg_parts.append(
                f'<rect x="{x - 6}" y="{y - 6}" width="12" height="12" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
            )
            svg_parts.append(svg_text(x, y + 30, label, 18, BLUE, "middle"))

    legend_x = 1370
    legend_y = 84
    draw.line((legend_x, legend_y, legend_x + 54, legend_y), fill=BLACK, width=3)
    draw_marker(draw, legend_x + 27, legend_y, BLACK, "circle")
    draw.text((legend_x + 74, legend_y - 16), "输入 NRMSE", font=F_LABEL, fill=BLACK)

    draw.line((legend_x, legend_y + 52, legend_x + 54, legend_y + 52), fill=BLUE, width=3)
    draw_marker(draw, legend_x + 27, legend_y + 52, BLUE, "square")
    draw.text((legend_x + 74, legend_y + 36), "优化后 NRMSE", font=F_LABEL, fill=BLACK)

    svg_parts.extend(
        [
            f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 54}" y2="{legend_y}" stroke="{svg_rgb(BLACK)}" stroke-width="3" />',
            f'<circle cx="{legend_x + 27}" cy="{legend_y}" r="6" fill="{svg_rgb(BLACK)}" />',
            svg_text(legend_x + 74, legend_y + 7, "输入 NRMSE", 21, BLACK, "start"),
            f'<line x1="{legend_x}" y1="{legend_y + 52}" x2="{legend_x + 54}" y2="{legend_y + 52}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            f'<rect x="{legend_x + 21}" y="{legend_y + 46}" width="12" height="12" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            svg_text(legend_x + 74, legend_y + 59, "优化后 NRMSE", 21, BLACK, "start"),
        ]
    )

    svg_parts.append("</svg>")
    save_png(img, "thesis_optimization_strength_nrmse_v1.png")
    (OUT / "thesis_optimization_strength_nrmse_v1.svg").write_text("\n".join(svg_parts), encoding="utf-8")


def draw_nrmse_single_chart(data: dict[str, list[float]], stem: str) -> None:
    width, height = 1220, 760
    img = Image.new("RGB", (width, height), WHITE)
    draw = ImageDraw.Draw(img)

    x1, y1, x2, y2 = 120, 120, 780, 600
    draw.rectangle((x1, y1, x2, y2), outline=BLACK, width=2)

    y_min = 0.0
    y_max = 0.55
    y_ticks = [0.0, 0.1, 0.2, 0.3, 0.4, 0.5]

    svg_parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="{svg_rgb(WHITE)}" />',
        f'<rect x="{x1}" y="{y1}" width="{x2 - x1}" height="{y2 - y1}" fill="none" stroke="{svg_rgb(BLACK)}" stroke-width="2" />',
    ]

    def sy(v: float) -> float:
        return y2 - (v - y_min) / (y_max - y_min) * (y2 - y1)

    for t in y_ticks:
        yy = sy(t)
        draw.line((x1, yy, x2, yy), fill=LIGHT, width=1)
        label = f"{t:.1f}"
        lw, lh = text_bbox(draw, label, F_TICK)
        draw.text((x1 - 18 - lw, yy - lh / 2), label, font=F_TICK, fill=GRAY)
        svg_parts.append(f'<line x1="{x1}" y1="{yy:.1f}" x2="{x2}" y2="{yy:.1f}" stroke="{svg_rgb(LIGHT)}" stroke-width="1" />')
        svg_parts.append(svg_text(x1 - 18, yy + 6, label, 18, GRAY, "end"))

    draw.text((x1 - 26, y1 - 44), "NRMSE", font=F_LABEL, fill=BLACK)
    svg_parts.append(svg_text(x1 - 26, y1 - 24, "NRMSE", 21, BLACK, "start"))

    xs: list[int] = []
    for i, sigma in enumerate(data["sigma"]):
        x = int(x1 + (x2 - x1) * (i + 0.5) / len(data["sigma"]))
        xs.append(x)
        label = f"{sigma:.2f}"
        lw, _ = text_bbox(draw, label, F_TICK)
        draw.text((x - lw / 2, y2 + 22), label, font=F_TICK, fill=BLACK)
        svg_parts.append(svg_text(x, y2 + 42, label, 18, BLACK, "middle"))

    x_label = "扰动强度系数"
    lw, _ = text_bbox(draw, x_label, F_LABEL)
    draw.text(((x1 + x2 - lw) / 2, y2 + 62), x_label, font=F_LABEL, fill=BLACK)
    svg_parts.append(svg_text((x1 + x2) / 2, y2 + 90, x_label, 21, BLACK, "middle"))

    input_points: list[tuple[int, int]] = [(xs[i], int(sy(v))) for i, v in enumerate(data["input_nrmse"])]
    fused_points: list[tuple[int, int]] = [(xs[i], int(sy(v))) for i, v in enumerate(data["fused_nrmse"])]
    draw.line(input_points, fill=BLACK, width=3)
    draw.line(fused_points, fill=BLUE, width=3)
    svg_parts.append(
        f'<polyline points="{" ".join(f"{x},{y}" for x, y in input_points)}" fill="none" stroke="{svg_rgb(BLACK)}" stroke-width="3" />'
    )
    svg_parts.append(
        f'<polyline points="{" ".join(f"{x},{y}" for x, y in fused_points)}" fill="none" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
    )

    for (x, y), v in zip(input_points, data["input_nrmse"]):
        draw_marker(draw, x, y, BLACK, "circle")
        label = value_label(v)
        lw, _ = text_bbox(draw, label, F_TICK)
        draw.text((x - lw / 2, y - 34), label, font=F_TICK, fill=BLACK)
        svg_parts.append(f'<circle cx="{x}" cy="{y}" r="6" fill="{svg_rgb(BLACK)}" />')
        svg_parts.append(svg_text(x, y - 16, label, 18, BLACK, "middle"))

    for (x, y), v in zip(fused_points, data["fused_nrmse"]):
        draw_marker(draw, x, y, BLUE, "square")
        label = value_label(v)
        lw, _ = text_bbox(draw, label, F_TICK)
        draw.text((x - lw / 2, y + 12), label, font=F_TICK, fill=BLUE)
        svg_parts.append(
            f'<rect x="{x - 6}" y="{y - 6}" width="12" height="12" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
        )
        svg_parts.append(svg_text(x, y + 30, label, 18, BLUE, "middle"))

    legend_x = 180
    legend_y = 650
    draw.line((legend_x, legend_y, legend_x + 54, legend_y), fill=BLACK, width=3)
    draw_marker(draw, legend_x + 27, legend_y, BLACK, "circle")
    draw.text((legend_x + 74, legend_y - 16), "输入 NRMSE", font=F_LABEL, fill=BLACK)
    draw.line((legend_x + 300, legend_y, legend_x + 354, legend_y), fill=BLUE, width=3)
    draw_marker(draw, legend_x + 327, legend_y, BLUE, "square")
    draw.text((legend_x + 374, legend_y - 16), "优化后 NRMSE", font=F_LABEL, fill=BLACK)

    svg_parts.extend(
        [
            f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 54}" y2="{legend_y}" stroke="{svg_rgb(BLACK)}" stroke-width="3" />',
            f'<circle cx="{legend_x + 27}" cy="{legend_y}" r="6" fill="{svg_rgb(BLACK)}" />',
            svg_text(legend_x + 74, legend_y + 7, "输入 NRMSE", 21, BLACK, "start"),
            f'<line x1="{legend_x + 300}" y1="{legend_y}" x2="{legend_x + 354}" y2="{legend_y}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            f'<rect x="{legend_x + 321}" y="{legend_y - 6}" width="12" height="12" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            svg_text(legend_x + 374, legend_y + 7, "优化后 NRMSE", 21, BLACK, "start"),
        ]
    )

    svg_parts.append("</svg>")
    save_png(img, f"{stem}.png")
    (OUT / f"{stem}.svg").write_text("\n".join(svg_parts), encoding="utf-8")


def draw_ratio_chart(point: dict[str, list[float]], cell: dict[str, list[float]]) -> None:
    width, height = 1280, 760
    img = Image.new("RGB", (width, height), WHITE)
    draw = ImageDraw.Draw(img)

    title = "不同高斯扰动强度下平均 RMSE 改进比"
    tw, _ = text_bbox(draw, title, F_TITLE)
    draw.text(((width - tw) / 2, 42), title, font=F_TITLE, fill=BLACK)

    x1, y1, x2, y2 = 120, 170, 1020, 610
    draw.rectangle((x1, y1, x2, y2), outline=BLACK, width=2)

    y_min = 0.35
    y_max = 0.70
    y_ticks = [0.35, 0.45, 0.55, 0.65]

    svg_parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="{svg_rgb(WHITE)}" />',
        svg_text(width / 2, 74, title, 28, BLACK, "middle"),
        f'<rect x="{x1}" y="{y1}" width="{x2 - x1}" height="{y2 - y1}" fill="none" stroke="{svg_rgb(BLACK)}" stroke-width="2" />',
    ]

    def sy(v: float) -> float:
        return y2 - (v - y_min) / (y_max - y_min) * (y2 - y1)

    for t in y_ticks:
        yy = sy(t)
        draw.line((x1, yy, x2, yy), fill=LIGHT, width=1)
        label = f"{t:.2f}"
        lw, lh = text_bbox(draw, label, F_TICK)
        draw.text((x1 - 18 - lw, yy - lh / 2), label, font=F_TICK, fill=GRAY)
        svg_parts.append(f'<line x1="{x1}" y1="{yy:.1f}" x2="{x2}" y2="{yy:.1f}" stroke="{svg_rgb(LIGHT)}" stroke-width="1" />')
        svg_parts.append(svg_text(x1 - 18, yy + 6, label, 18, GRAY, "end"))

    draw.text((x1 - 50, y1 - 44), "改进比", font=F_LABEL, fill=BLACK)
    svg_parts.append(svg_text(x1 - 50, y1 - 24, "改进比", 21, BLACK, "start"))

    xs: list[int] = []
    for i, sigma in enumerate(point["sigma"]):
        x = int(x1 + (x2 - x1) * (i + 0.5) / len(point["sigma"]))
        xs.append(x)
        label = f"{sigma:.2f}"
        lw, _ = text_bbox(draw, label, F_TICK)
        draw.text((x - lw / 2, y2 + 22), label, font=F_TICK, fill=BLACK)
        svg_parts.append(svg_text(x, y2 + 42, label, 18, BLACK, "middle"))

    x_label = "扰动强度系数"
    lw, _ = text_bbox(draw, x_label, F_LABEL)
    draw.text(((x1 + x2 - lw) / 2, y2 + 62), x_label, font=F_LABEL, fill=BLACK)
    svg_parts.append(svg_text((x1 + x2) / 2, y2 + 90, x_label, 21, BLACK, "middle"))

    point_points = [(xs[i], int(sy(v))) for i, v in enumerate(point["ratio"])]
    cell_points = [(xs[i], int(sy(v))) for i, v in enumerate(cell["ratio"])]
    draw.line(point_points, fill=BLACK, width=3)
    draw.line(cell_points, fill=BLUE, width=3)
    svg_parts.append(
        f'<polyline points="{" ".join(f"{x},{y}" for x, y in point_points)}" fill="none" stroke="{svg_rgb(BLACK)}" stroke-width="3" />'
    )
    svg_parts.append(
        f'<polyline points="{" ".join(f"{x},{y}" for x, y in cell_points)}" fill="none" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
    )

    for (x, y), v in zip(point_points, point["ratio"]):
        draw_marker(draw, x, y, BLACK, "circle")
        label = value_label(v)
        lw, _ = text_bbox(draw, label, F_TICK)
        draw.text((x - lw / 2, y - 34), label, font=F_TICK, fill=BLACK)
        svg_parts.append(f'<circle cx="{x}" cy="{y}" r="6" fill="{svg_rgb(BLACK)}" />')
        svg_parts.append(svg_text(x, y - 16, label, 18, BLACK, "middle"))

    for (x, y), v in zip(cell_points, cell["ratio"]):
        draw_marker(draw, x, y, BLUE, "square")
        label = value_label(v)
        lw, _ = text_bbox(draw, label, F_TICK)
        draw.text((x - lw / 2, y + 12), label, font=F_TICK, fill=BLUE)
        svg_parts.append(
            f'<rect x="{x - 6}" y="{y - 6}" width="12" height="12" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />'
        )
        svg_parts.append(svg_text(x, y + 30, label, 18, BLUE, "middle"))

    legend_x = 1070
    legend_y = 210
    draw.line((legend_x, legend_y, legend_x + 54, legend_y), fill=BLACK, width=3)
    draw_marker(draw, legend_x + 27, legend_y, BLACK, "circle")
    draw.text((legend_x + 74, legend_y - 16), "点数据", font=F_LABEL, fill=BLACK)
    draw.line((legend_x, legend_y + 52, legend_x + 54, legend_y + 52), fill=BLUE, width=3)
    draw_marker(draw, legend_x + 27, legend_y + 52, BLUE, "square")
    draw.text((legend_x + 74, legend_y + 36), "单元数据", font=F_LABEL, fill=BLACK)

    svg_parts.extend(
        [
            f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 54}" y2="{legend_y}" stroke="{svg_rgb(BLACK)}" stroke-width="3" />',
            f'<circle cx="{legend_x + 27}" cy="{legend_y}" r="6" fill="{svg_rgb(BLACK)}" />',
            svg_text(legend_x + 74, legend_y + 7, "点数据", 21, BLACK, "start"),
            f'<line x1="{legend_x}" y1="{legend_y + 52}" x2="{legend_x + 54}" y2="{legend_y + 52}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            f'<rect x="{legend_x + 21}" y="{legend_y + 46}" width="12" height="12" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            svg_text(legend_x + 74, legend_y + 59, "单元数据", 21, BLACK, "start"),
        ]
    )

    svg_parts.append("</svg>")
    save_png(img, "thesis_optimization_strength_ratio_v1.png")
    (OUT / "thesis_optimization_strength_ratio_v1.svg").write_text("\n".join(svg_parts), encoding="utf-8")


def main() -> None:
    point = aggregate_series([
        RESULT_DIR / "multiscale_report+point0.15.csv",
        RESULT_DIR / "multiscale_report+point0.35.csv",
        RESULT_DIR / "multiscale_report+point0.55.csv",
    ])
    cell = aggregate_series([
        RESULT_DIR / "multiscale_report+cell0.15.csv",
        RESULT_DIR / "multiscale_report+cell0.35.csv",
        RESULT_DIR / "multiscale_report+cell0.55.csv",
    ])
    draw_nrmse_chart(point, cell)
    draw_nrmse_single_chart(point, "thesis_optimization_strength_nrmse_point_v1")
    draw_nrmse_single_chart(cell, "thesis_optimization_strength_nrmse_cell_v1")
    draw_ratio_chart(point, cell)
    print(f"generated optimization strength figures in {OUT}")


if __name__ == "__main__":
    main()
