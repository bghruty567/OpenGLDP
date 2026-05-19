from __future__ import annotations

import csv
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
BLUE = (78, 111, 155)
LIGHT = (230, 230, 230)
WHITE = (255, 255, 255)
SVG_FONT_FAMILY = "'Noto Serif SC','Noto Sans CJK SC','SimSun','Microsoft YaHei',serif"


def font(size: int) -> ImageFont.ImageFont:
    if FONT_PATH is None:
        return ImageFont.load_default()
    return ImageFont.truetype(str(FONT_PATH), size)


F_LABEL = font(25)
F_SMALL = font(21)
F_TINY = font(18)


def canvas(width: int = 1500, height: int = 880) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (width, height), WHITE)
    return img, ImageDraw.Draw(img)


def save_png(img: Image.Image, name: str) -> None:
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
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" '
        f'font-family="{SVG_FONT_FAMILY}" font-size="{size}" '
        f'fill="{svg_rgb(fill)}">{escape(text)}</text>'
    )


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


def sci(value: float) -> str:
    if value >= 100:
        return f"{value:.0f}"
    if value >= 10:
        return f"{value:.1f}".rstrip("0").rstrip(".")
    if value >= 1:
        return f"{value:.2f}".rstrip("0").rstrip(".")
    return f"{value:.3f}".rstrip("0").rstrip(".")


def y_to_px(value: float, plot: tuple[int, int, int, int], y_min: float, y_max: float) -> int:
    x1, y1, x2, y2 = plot
    lv = math.log10(max(value, y_min))
    l0 = math.log10(y_min)
    l1 = math.log10(y_max)
    t = 0.0 if math.isclose(l1, l0) else (lv - l0) / (l1 - l0)
    return int(round(y2 - t * (y2 - y1)))


def draw_axes(draw: ImageDraw.ImageDraw, plot: tuple[int, int, int, int], y_min: float, y_max: float) -> None:
    x1, y1, x2, y2 = plot
    draw.line((x1, y2, x2, y2), fill=BLACK, width=2)
    draw.line((x1, y1, x1, y2), fill=BLACK, width=2)

    lo = math.floor(math.log10(y_min))
    hi = math.ceil(math.log10(y_max))
    ticks = [10 ** p for p in range(lo, hi + 1)]
    ticks = [t for t in ticks if y_min <= t <= y_max]
    if y_min not in ticks:
        ticks.insert(0, y_min)
    if y_max not in ticks:
        ticks.append(y_max)

    for t in ticks:
        yy = y_to_px(t, plot, y_min, y_max)
        draw.line((x1 - 6, yy, x1, yy), fill=BLACK, width=2)
        draw.line((x1, yy, x2, yy), fill=LIGHT, width=1)
        label = sci(t)
        tb = draw.textbbox((0, 0), label, font=F_TINY)
        draw.text((x1 - 18 - (tb[2] - tb[0]), yy - (tb[3] - tb[1]) / 2), label, font=F_TINY, fill=BLACK)

    draw.text((x1 - 110, y1 - 45), "时间 / ms", font=F_SMALL, fill=BLACK)


def save_timing_line_chart_svg(
    name: str,
    caption: str,
    x_labels: list[str],
    vtk_values: list[float],
    sys_values: list[float],
    x_axis_label: str,
    vtk_value_labels: list[str],
    sys_value_labels: list[str],
) -> None:
    width, height = 1500, 880
    plot = (180, 170, 1190, 670)
    x1, y1, x2, y2 = plot
    y_min, y_max = log_bounds(vtk_values + sys_values)

    lo = math.floor(math.log10(y_min))
    hi = math.ceil(math.log10(y_max))
    ticks = [10 ** p for p in range(lo, hi + 1)]
    ticks = [t for t in ticks if y_min <= t <= y_max]
    if y_min not in ticks:
        ticks.insert(0, y_min)
    if y_max not in ticks:
        ticks.append(y_max)

    def y_px(v: float) -> float:
        lv = math.log10(max(v, y_min))
        l0 = math.log10(y_min)
        l1 = math.log10(y_max)
        t = 0.0 if math.isclose(l1, l0) else (lv - l0) / (l1 - l0)
        return y2 - t * (y2 - y1)

    vtk_points: list[tuple[float, float]] = []
    sys_points: list[tuple[float, float]] = []
    count = len(x_labels)
    for i in range(count):
        x = plot[0] + (plot[2] - plot[0]) * (i + 0.5) / count
        vtk_points.append((x, y_px(vtk_values[i])))
        sys_points.append((x, y_px(sys_values[i])))

    def polyline(points: list[tuple[float, float]], color: tuple[int, int, int], width_px: int) -> str:
        pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
        return f'<polyline points="{pts}" fill="none" stroke="{svg_rgb(color)}" stroke-width="{width_px}" />'

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="{svg_rgb(WHITE)}" />',
        f'<line x1="{x1}" y1="{y2}" x2="{x2}" y2="{y2}" stroke="{svg_rgb(BLACK)}" stroke-width="2" />',
        f'<line x1="{x1}" y1="{y1}" x2="{x1}" y2="{y2}" stroke="{svg_rgb(BLACK)}" stroke-width="2" />',
        svg_text(plot[0], 92, caption, 25, BLACK, "start"),
        svg_text(x1 - 110, y1 - 45, "时间 / ms", 21, BLACK, "start"),
    ]

    for t in ticks:
        yy = y_px(t)
        parts.append(f'<line x1="{x1 - 6}" y1="{yy:.1f}" x2="{x1}" y2="{yy:.1f}" stroke="{svg_rgb(BLACK)}" stroke-width="2" />')
        parts.append(f'<line x1="{x1}" y1="{yy:.1f}" x2="{x2}" y2="{yy:.1f}" stroke="{svg_rgb(LIGHT)}" stroke-width="1" />')
        parts.append(svg_text(x1 - 18, yy + 6, sci(t), 18, BLACK, "end"))

    for i, label in enumerate(x_labels):
        x = plot[0] + (plot[2] - plot[0]) * (i + 0.5) / count
        parts.append(svg_text(x, plot[3] + 44, label, 18, BLACK, "middle"))

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
        parts.append(svg_text(x, y - 20, label, 18, BLACK, "middle"))
    for (x, y), label in zip(sys_points, sys_value_labels):
        parts.append(svg_text(x, y + 33, label, 18, BLUE, "middle"))

    center_x = (plot[0] + plot[2]) / 2
    parts.append(svg_text(center_x, plot[3] + 96, x_axis_label, 21, BLACK, "middle"))

    legend_x = 1260
    parts.extend(
        [
            f'<line x1="{legend_x}" y1="240" x2="{legend_x + 70}" y2="240" stroke="{svg_rgb(BLACK)}" stroke-width="4" />',
            f'<circle cx="{legend_x + 36}" cy="240" r="7" fill="{svg_rgb(BLACK)}" />',
            svg_text(legend_x + 92, 250, "VTK 时间", 21, BLACK, "start"),
            f'<line x1="{legend_x}" y1="305" x2="{legend_x + 70}" y2="305" stroke="{svg_rgb(BLUE)}" stroke-width="4" />',
            f'<rect x="{legend_x + 29}" y="298" width="14" height="14" fill="{svg_rgb(WHITE)}" stroke="{svg_rgb(BLUE)}" stroke-width="3" />',
            svg_text(legend_x + 92, 315, "系统时间", 21, BLACK, "start"),
        ]
    )

    parts.append("</svg>")
    OUT.joinpath(name).write_text("\n".join(parts), encoding="utf-8")


def draw_timing_line_chart(
    name: str,
    caption: str,
    x_labels: list[str],
    vtk_values: list[float],
    sys_values: list[float],
    x_axis_label: str,
    vtk_value_labels: list[str],
    sys_value_labels: list[str],
) -> None:
    img, d = canvas()
    plot = (180, 170, 1190, 670)
    y_min, y_max = log_bounds(vtk_values + sys_values)
    draw_axes(d, plot, y_min, y_max)

    d.text((plot[0], 92), caption, font=F_LABEL, fill=BLACK)

    count = len(x_labels)
    vtk_points: list[tuple[int, int]] = []
    sys_points: list[tuple[int, int]] = []
    for i, label in enumerate(x_labels):
        x = int(plot[0] + (plot[2] - plot[0]) * (i + 0.5) / count)
        tb = d.textbbox((0, 0), label, font=F_TINY)
        d.text((x - (tb[2] - tb[0]) / 2, plot[3] + 26), label, font=F_TINY, fill=BLACK)
        vtk_points.append((x, y_to_px(vtk_values[i], plot, y_min, y_max)))
        sys_points.append((x, y_to_px(sys_values[i], plot, y_min, y_max)))

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
    d.text((legend_x + 92, 292), "系统时间", font=F_SMALL, fill=BLACK)

    save_png(img, name)
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


def load_row(csv_path: Path) -> dict[str, str]:
    with csv_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        row = next(reader)
    return row


def value_label(v: float) -> str:
    if v >= 1000:
        return f"{v:.2f}".rstrip("0").rstrip(".")
    if v >= 100:
        return f"{v:.3f}".rstrip("0").rstrip(".")
    if v >= 10:
        return f"{v:.4f}".rstrip("0").rstrip(".")
    return f"{v:.5f}".rstrip("0").rstrip(".")


def extract_series(csv_paths: list[Path]) -> tuple[list[str], list[float], list[float]]:
    x_labels: list[str] = []
    vtk_values: list[float] = []
    sys_values: list[float] = []
    for path in csv_paths:
        row = load_row(path)
        x_labels.append(str(int(float(row["result_tuples"]))))
        sys_values.append(float(row["result_wall_avg_ms"]))
        vtk_values.append(float(row["ambient_vtk_parallel_avg_ms"]))
    return x_labels, vtk_values, sys_values


def main() -> None:
    result_dir = Path(
        r"C:\Users\lenovo\Desktop\bishe\myProj\build-OpenGLDP-Desktop_Qt_5_15_2_MSVC2019_64bit-Debug\results"
    )
    suffix = "_reps10_debug"

    charts = [
        (
            "thesis_timing_structured_point_line",
            "结构化网格点数据（reps=10, Debug）",
            "点数",
            [
                result_dir / "timing_struct_20x20x20point.csv",
                result_dir / "timing_struct_32x32x32point.csv",
                result_dir / "timing_struct_48x48x48point.csv",
            ],
        ),
        (
            "thesis_timing_structured_cell_line",
            "结构化网格单元数据（reps=10, Debug）",
            "单元数",
            [
                result_dir / "timing_struct_20x20x20cell.csv",
                result_dir / "timing_struct_32x32x32cell.csv",
                result_dir / "timing_struct_48x48x48cell.csv",
            ],
        ),
        (
            "thesis_timing_unstructured_point_line",
            "非结构化网格点数据（reps=10, Debug）",
            "点数",
            [
                result_dir / "timing_uhex_20x20x20point.csv",
                result_dir / "timing_uhex_32x32x32point.csv",
                result_dir / "timing_uhex_48x48x48point.csv",
            ],
        ),
        (
            "thesis_timing_unstructured_cell_line",
            "非结构化网格单元数据（reps=10, Debug）",
            "单元数",
            [
                result_dir / "timing_uhex_20x20x20cell.csv",
                result_dir / "timing_uhex_32x32x32cell.csv",
                result_dir / "timing_uhex_48x48x48cell.csv",
            ],
        ),
    ]

    for stem, caption, x_axis_label, csv_paths in charts:
        x_labels, vtk_values, sys_values = extract_series(csv_paths)
        draw_timing_line_chart(
            f"{stem}{suffix}.png",
            caption,
            x_labels,
            vtk_values,
            sys_values,
            x_axis_label,
            [value_label(v) for v in vtk_values],
            [value_label(v) for v in sys_values],
        )

    print(f"generated charts in {OUT}")


if __name__ == "__main__":
    main()
