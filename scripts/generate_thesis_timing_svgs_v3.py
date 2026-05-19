from __future__ import annotations

import csv
from pathlib import Path

from generate_timing_charts_from_results import draw_timing_line_chart, value_label


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "wendang" / "pic"
OUT.mkdir(parents=True, exist_ok=True)

RESULT_DIR = Path(
    r"C:\Users\lenovo\Desktop\bishe\myProj\build-OpenGLDP-Desktop_Qt_5_15_2_MSVC2019_64bit-Debug\results"
)


def load_row(csv_path: Path) -> dict[str, str]:
    with csv_path.open("r", encoding="utf-8-sig", newline="") as f:
        return next(csv.DictReader(f))


def extract_series(csv_paths: list[Path]) -> tuple[list[str], list[float], list[float]]:
    x_labels: list[str] = []
    vtk_values: list[float] = []
    sys_values: list[float] = []
    for path in csv_paths:
        row = load_row(path)
        x_labels.append(str(int(float(row["result_tuples"]))))
        vtk_values.append(float(row["ambient_vtk_parallel_avg_ms"]))
        sys_values.append(float(row["result_wall_avg_ms"]))
    return x_labels, vtk_values, sys_values


def main() -> None:
    charts = [
        (
            "thesis_timing_structured_point_line_v3.png",
            "\u7ed3\u6784\u5316\u7f51\u683c\u70b9\u6570\u636e",
            "\u70b9\u6570",
            [
                RESULT_DIR / "timing_struct_20x20x20point.csv",
                RESULT_DIR / "timing_struct_32x32x32point.csv",
                RESULT_DIR / "timing_struct_48x48x48point.csv",
            ],
        ),
        (
            "thesis_timing_structured_cell_line_v3.png",
            "\u7ed3\u6784\u5316\u7f51\u683c\u5355\u5143\u6570\u636e",
            "\u5355\u5143\u6570",
            [
                RESULT_DIR / "timing_struct_20x20x20cell.csv",
                RESULT_DIR / "timing_struct_32x32x32cell.csv",
                RESULT_DIR / "timing_struct_48x48x48cell.csv",
            ],
        ),
        (
            "thesis_timing_unstructured_point_line_v3.png",
            "\u975e\u7ed3\u6784\u5316\u7f51\u683c\u70b9\u6570\u636e",
            "\u70b9\u6570",
            [
                RESULT_DIR / "timing_uhex_20x20x20point.csv",
                RESULT_DIR / "timing_uhex_32x32x32point.csv",
                RESULT_DIR / "timing_uhex_48x48x48point.csv",
            ],
        ),
        (
            "thesis_timing_unstructured_cell_line_v3.png",
            "\u975e\u7ed3\u6784\u5316\u7f51\u683c\u5355\u5143\u6570\u636e",
            "\u5355\u5143\u6570",
            [
                RESULT_DIR / "timing_uhex_20x20x20cell.csv",
                RESULT_DIR / "timing_uhex_32x32x32cell.csv",
                RESULT_DIR / "timing_uhex_48x48x48cell.csv",
            ],
        ),
    ]

    for png_name, caption, x_axis_label, csv_paths in charts:
        x_labels, vtk_values, sys_values = extract_series(csv_paths)
        draw_timing_line_chart(
            png_name,
            caption,
            x_labels,
            vtk_values,
            sys_values,
            x_axis_label,
            [value_label(v) for v in vtk_values],
            [value_label(v) for v in sys_values],
        )

    print(f"generated timing charts in {OUT}")


if __name__ == "__main__":
    main()
