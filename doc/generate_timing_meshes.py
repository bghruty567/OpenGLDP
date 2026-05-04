from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import csv


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "Data" / "timing"
MANIFEST = OUT_DIR / "manifest.csv"


@dataclass(frozen=True)
class GridSpec:
    label: str
    nx: int
    ny: int
    nz: int

    @property
    def points(self) -> int:
        return self.nx * self.ny * self.nz

    @property
    def cells(self) -> int:
        return (self.nx - 1) * (self.ny - 1) * (self.nz - 1)


SPECS = [
    GridSpec("small", 20, 20, 20),
    GridSpec("medium", 32, 32, 32),
    GridSpec("large", 48, 48, 48),
]


def scalar_field(x: float, y: float, z: float) -> float:
    return 1.5 * x - 2.0 * y + 0.75 * z + 0.1


def point_index(i: int, j: int, k: int, nx: int, ny: int) -> int:
    return k * ny * nx + j * nx + i


def iter_points(spec: GridSpec):
    dx = 1.0 / (spec.nx - 1)
    dy = 1.0 / (spec.ny - 1)
    dz = 1.0 / (spec.nz - 1)
    for k in range(spec.nz):
        z = k * dz
        for j in range(spec.ny):
            y = j * dy
            for i in range(spec.nx):
                x = i * dx
                yield x, y, z


def iter_cell_centers(spec: GridSpec):
    dx = 1.0 / (spec.nx - 1)
    dy = 1.0 / (spec.ny - 1)
    dz = 1.0 / (spec.nz - 1)
    for k in range(spec.nz - 1):
        z = (k + 0.5) * dz
        for j in range(spec.ny - 1):
            y = (j + 0.5) * dy
            for i in range(spec.nx - 1):
                x = (i + 0.5) * dx
                yield x, y, z


def write_scalar_block(handle, values, values_per_line: int = 9) -> None:
    line = []
    for value in values:
        line.append(f"{value:.6f}")
        if len(line) == values_per_line:
            handle.write(" ".join(line) + "\n")
            line.clear()
    if line:
        handle.write(" ".join(line) + "\n")


def write_structured_grid(path: Path, spec: GridSpec) -> None:
    with path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write("# vtk DataFile Version 3.0\n")
        handle.write("OpenGLDP timing structured grid\n")
        handle.write("ASCII\n")
        handle.write("DATASET STRUCTURED_GRID\n")
        handle.write(f"DIMENSIONS {spec.nx} {spec.ny} {spec.nz}\n")
        handle.write(f"POINTS {spec.points} float\n")
        for x, y, z in iter_points(spec):
            handle.write(f"{x:.6f} {y:.6f} {z:.6f}\n")

        handle.write(f"\nCELL_DATA {spec.cells}\n")
        handle.write("SCALARS scalars float\n")
        handle.write("LOOKUP_TABLE default\n")
        write_scalar_block(handle, (scalar_field(x, y, z) for x, y, z in iter_cell_centers(spec)))

        handle.write(f"\nPOINT_DATA {spec.points}\n")
        handle.write("SCALARS scalars float\n")
        handle.write("LOOKUP_TABLE default\n")
        write_scalar_block(handle, (scalar_field(x, y, z) for x, y, z in iter_points(spec)))


def iter_hexa_cells(spec: GridSpec):
    nx = spec.nx
    ny = spec.ny
    for k in range(spec.nz - 1):
        for j in range(spec.ny - 1):
            for i in range(spec.nx - 1):
                v0 = point_index(i, j, k, nx, ny)
                v1 = point_index(i + 1, j, k, nx, ny)
                v2 = point_index(i + 1, j + 1, k, nx, ny)
                v3 = point_index(i, j + 1, k, nx, ny)
                v4 = point_index(i, j, k + 1, nx, ny)
                v5 = point_index(i + 1, j, k + 1, nx, ny)
                v6 = point_index(i + 1, j + 1, k + 1, nx, ny)
                v7 = point_index(i, j + 1, k + 1, nx, ny)
                yield v0, v1, v2, v3, v4, v5, v6, v7


def write_unstructured_hexa(path: Path, spec: GridSpec) -> None:
    total_cell_ints = spec.cells * 9
    with path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write("# vtk DataFile Version 3.0\n")
        handle.write("OpenGLDP timing unstructured hexa grid\n")
        handle.write("ASCII\n")
        handle.write("DATASET UNSTRUCTURED_GRID\n")
        handle.write(f"POINTS {spec.points} float\n")
        for x, y, z in iter_points(spec):
            handle.write(f"{x:.6f} {y:.6f} {z:.6f}\n")

        handle.write(f"\nCELLS {spec.cells} {total_cell_ints}\n")
        for cell in iter_hexa_cells(spec):
            handle.write("8 " + " ".join(str(v) for v in cell) + "\n")

        handle.write(f"\nCELL_TYPES {spec.cells}\n")
        line = []
        for _ in range(spec.cells):
            line.append("12")
            if len(line) == 16:
                handle.write(" ".join(line) + "\n")
                line.clear()
        if line:
            handle.write(" ".join(line) + "\n")

        handle.write(f"\nCELL_DATA {spec.cells}\n")
        handle.write("SCALARS scalars float\n")
        handle.write("LOOKUP_TABLE default\n")
        write_scalar_block(handle, (scalar_field(x, y, z) for x, y, z in iter_cell_centers(spec)))

        handle.write(f"\nPOINT_DATA {spec.points}\n")
        handle.write("SCALARS scalars float\n")
        handle.write("LOOKUP_TABLE default\n")
        write_scalar_block(handle, (scalar_field(x, y, z) for x, y, z in iter_points(spec)))


def write_manifest(rows: list[dict[str, str]]) -> None:
    with MANIFEST.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "family",
                "label",
                "file",
                "relative_path",
                "nx",
                "ny",
                "nz",
                "points",
                "cells",
                "association",
                "field",
                "domain",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, str]] = []

    for spec in SPECS:
        struct_name = f"timing_struct_{spec.nx}x{spec.ny}x{spec.nz}.vtk"
        write_structured_grid(OUT_DIR / struct_name, spec)
        rows.append(
            {
                "family": "structured_grid",
                "label": spec.label,
                "file": struct_name,
                "relative_path": f"Data/timing/{struct_name}",
                "nx": str(spec.nx),
                "ny": str(spec.ny),
                "nz": str(spec.nz),
                "points": str(spec.points),
                "cells": str(spec.cells),
                "association": "point,cell",
                "field": "scalars = 1.5x - 2.0y + 0.75z + 0.1",
                "domain": "[0,1]^3",
            }
        )

        uhex_name = f"timing_uhex_{spec.nx}x{spec.ny}x{spec.nz}.vtk"
        write_unstructured_hexa(OUT_DIR / uhex_name, spec)
        rows.append(
            {
                "family": "unstructured_hexa",
                "label": spec.label,
                "file": uhex_name,
                "relative_path": f"Data/timing/{uhex_name}",
                "nx": str(spec.nx),
                "ny": str(spec.ny),
                "nz": str(spec.nz),
                "points": str(spec.points),
                "cells": str(spec.cells),
                "association": "point,cell",
                "field": "scalars = 1.5x - 2.0y + 0.75z + 0.1",
                "domain": "[0,1]^3",
            }
        )

    write_manifest(rows)
    print(f"generated {len(rows)} datasets under: {OUT_DIR}")
    print(f"manifest: {MANIFEST}")


if __name__ == "__main__":
    main()
