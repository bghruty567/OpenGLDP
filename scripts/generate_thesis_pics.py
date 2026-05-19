from __future__ import annotations

from html import escape
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PIC_DIR = ROOT / "wendang" / "pic"

W, H = 1500, 900
BLACK = "#111111"
GRAY = "#555555"
LIGHT_GRAY = "#eeeeee"
GRID = "#bfbfbf"
BLUE = "#1f4e79"


FIGURE_PROMPTS = {
    "fig_data_structure": (
        "Draw a professional thesis-style vector schematic similar to a LaTeX/TikZ figure in a computer graphics "
        "or finite-element paper. White background, no internal title, no decorative cards, no gradients, no shadows. "
        "Use thin black/gray strokes and one muted blue accent. Represent VTK data as three small stacked tables "
        "(Points, Cells, PointData/CellData), a central DataObject memory-layout table with rows points, cellCenters, "
        "cells, cellOffsets, cellTypes, dataArrays, neighbor arrays, neighborOffsets, dimensions, and right-side GPU "
        "SSBO/output arrays. Include a compact CSR row with offsets cells and neighbors cells, plus a bracket marking "
        "the neighbor interval for object i. Labels in Chinese, concise and technical."
    ),
    "fig_regular_fd_neighborhood": (
        "Draw a professional finite-difference stencil diagram in textbook/TikZ style. White background, no title, "
        "no decorative boxes. Left side: structured 3D grid shown as a 2D i-j slice with a dashed diagonal arrow for "
        "k-1 and k+1 neighboring layers. Mark center node (i,j,k), i±1 and j±1 neighbors. Right side: three aligned "
        "central-difference equations, one boundary one-sided equation, and chain-rule recovery ∇x u=J^{-T}∇ξ u. "
        "Use black/gray line work and a single muted blue accent for selected neighbors."
    ),
    "fig_unstructured_shape_gradient": (
        "Draw a professional finite-element method schematic in a journal-paper line-art style. White background, "
        "no infographic cards. Show four small supported cell icons: triangle, quadrilateral, tetrahedron, hexahedron. "
        "Show a reference element with local coordinates ξ,η,ζ mapped by x(ξ)=ΣNa(ξ)xa to a distorted physical element "
        "with nodes a1...a8. Beneath them, use a clean formula chain: u^e(ξ)=ΣNa ua, J=∂x/∂ξ, ∇xNa=J^{-T}∇ξNa, "
        "∇x u^e=Σua∇xNa. Use restrained black/gray strokes with one blue accent."
    ),
    "fig_cell_field_lift": (
        "Draw a professional finite-element post-processing schematic for cell-field lifting. White background, "
        "no decorative cards, no gradients. Left: unstructured mesh cells with cell-centered scalar values ue. "
        "Middle: an incidence relation from a vertex i to its surrounding cells C_i, shown as a small graph or bracket. "
        "Right: target element with lifted nodal values \\tilde u_a and element-center gradient. Bottom formula: "
        "\\tilde u_i=1/|C_i|Σ_{e∈C_i}u_e followed by ∇x u_e=Σ_a \\tilde u_a∇xNa(ξc). Emphasize reconstruction, "
        "not filtering. Use monochrome technical line art and muted blue only for highlighted vertex/incident cells."
    ),
}


class Svg:
    def __init__(self):
        self.parts: list[str] = []

    def add(self, raw: str):
        self.parts.append(raw)

    def line(self, x1, y1, x2, y2, stroke=BLACK, sw=1.8, dash=None, arrow=False):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        marker = ' marker-end="url(#arrow)"' if arrow else ""
        self.add(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
            f'stroke="{stroke}" stroke-width="{sw}"{dash_attr}{marker}/>'
        )

    def path(self, d, stroke=BLACK, sw=1.8, fill="none", dash=None, arrow=False):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        marker = ' marker-end="url(#arrow)"' if arrow else ""
        self.add(f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{dash_attr}{marker}/>')

    def rect(self, x, y, w, h, stroke=BLACK, sw=1.8, fill="white"):
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def circle(self, x, y, r, stroke=BLACK, sw=1.8, fill="white"):
        self.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def poly(self, pts, stroke=BLACK, sw=1.8, fill="white"):
        points = " ".join(f"{x},{y}" for x, y in pts)
        self.add(f'<polygon points="{points}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def text(self, x, y, text, size=22, anchor="middle", weight="400", fill=BLACK):
        self.add(
            f'<text x="{x}" y="{y}" font-family="SimSun, Times New Roman, Microsoft YaHei" '
            f'font-size="{size}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">'
            f"{escape(text)}</text>"
        )

    def multiline(self, x, y, lines, size=21, leading=30, anchor="start", fill=BLACK):
        for i, line in enumerate(lines):
            self.text(x, y + i * leading, line, size=size, anchor=anchor, fill=fill)

    def save(self, path: Path):
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<defs>
<marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
<path d="M 0 0 L 10 5 L 0 10 z" fill="{BLACK}"/>
</marker>
</defs>
<rect x="0" y="0" width="{W}" height="{H}" fill="white"/>
{chr(10).join(self.parts)}
</svg>
'''
        path.write_text(svg, encoding="utf-8")


def arrow(svg: Svg, x1, y1, x2, y2, label=""):
    svg.line(x1, y1, x2, y2, sw=2.0, arrow=True)
    if label:
        svg.text((x1 + x2) / 2, y1 - 14, label, size=20, fill=GRAY)


def small_table(svg: Svg, x, y, w, title, rows, row_h=38, header_h=40):
    total_h = header_h + row_h * len(rows)
    svg.rect(x, y, w, total_h, sw=1.6)
    svg.rect(x, y, w, header_h, sw=1.6, fill=LIGHT_GRAY)
    svg.text(x + w / 2, y + 27, title, size=21, weight="700")
    for i, row in enumerate(rows):
        yy = y + header_h + i * row_h
        svg.line(x, yy, x + w, yy, stroke=GRID, sw=1.2)
        svg.text(x + 16, yy + 26, row, size=19, anchor="start")
    return total_h


def data_structure():
    svg = Svg()

    small_table(svg, 70, 135, 270, "VTK 输入", ["Points", "Cells", "PointData", "CellData", "DataSet Type"], row_h=42)
    arrow(svg, 350, 260, 465, 260, "转换")

    # Central memory-layout table.
    x, y, w = 465, 70, 570
    header_h, row_h = 45, 48
    rows = [
        ("points", "3Np", "点坐标"),
        ("cellCenters", "3Nc", "单元中心"),
        ("cells", "变长", "单元连接"),
        ("cellOffsets", "Nc+1", "连接偏移"),
        ("cellTypes", "Nc", "单元类型"),
        ("dataArrays", "字段数", "字段值与元数据"),
        ("neighbors", "变长", "点/单元邻域"),
        ("neighborOffsets", "N+1", "邻域偏移"),
        ("dimensions", "3", "规则网格维度"),
    ]
    svg.rect(x, y, w, header_h + len(rows) * row_h, sw=2.0)
    svg.rect(x, y, w, header_h, sw=1.6, fill=LIGHT_GRAY)
    svg.text(x + w / 2, y + 30, "DataObject 内部连续数组", size=23, weight="700")
    col1, col2 = x + 190, x + 305
    svg.line(col1, y + header_h, col1, y + header_h + len(rows) * row_h, stroke=GRID, sw=1.2)
    svg.line(col2, y + header_h, col2, y + header_h + len(rows) * row_h, stroke=GRID, sw=1.2)
    for i, (name, length, desc) in enumerate(rows):
        yy = y + header_h + i * row_h
        svg.line(x, yy, x + w, yy, stroke=GRID, sw=1.2)
        svg.text(x + 18, yy + 31, name, size=19, anchor="start")
        svg.text(col1 + 18, yy + 31, length, size=19, anchor="start")
        svg.text(col2 + 18, yy + 31, desc, size=19, anchor="start")

    arrow(svg, 1045, 255, 1160, 255, "上传")
    small_table(svg, 1160, 145, 270, "GPU 侧 SSBO", ["positions", "connectivity", "field values", "offset arrays", "output field"], row_h=41)
    arrow(svg, 1045, 505, 1160, 505, "写回")
    small_table(svg, 1160, 430, 270, "VTK 输出", ["新增 PointData", "新增 CellData", "导出 .vtk/.vtu"], row_h=43)

    # CSR strip.
    svg.text(230, 665, "CSR 邻域表示", size=22, weight="700")
    off_x, off_y = 70, 690
    svg.text(off_x, off_y + 28, "offsets", size=19, anchor="start")
    vals = ["0", "3", "7", "10", "..."]
    for i, val in enumerate(vals):
        svg.rect(off_x + 105 + i * 58, off_y, 58, 40, sw=1.3)
        svg.text(off_x + 134 + i * 58, off_y + 27, val, size=18)
    svg.text(off_x, off_y + 92, "neighbors", size=19, anchor="start")
    neigh = ["2", "8", "9", "1", "4", "6", "7", "3", "5", "11"]
    for i, val in enumerate(neigh):
        svg.rect(off_x + 105 + i * 45, off_y + 64, 45, 40, sw=1.3)
        svg.text(off_x + 127 + i * 45, off_y + 91, val, size=18)
    svg.path(f"M {off_x+105+3*45} {off_y+118} L {off_x+105+7*45} {off_y+118}", stroke=BLUE, sw=2.2)
    svg.path(f"M {off_x+105+3*45} {off_y+112} L {off_x+105+3*45} {off_y+124}", stroke=BLUE, sw=2.2)
    svg.path(f"M {off_x+105+7*45} {off_y+112} L {off_x+105+7*45} {off_y+124}", stroke=BLUE, sw=2.2)
    svg.text(off_x + 330, off_y + 154, "对象 i 的邻域区间：[offsets[i], offsets[i+1])", size=19, fill=BLUE)
    svg.text(985, 755, "字段、拓扑和邻域统一组织后，算法模块只需访问连续数组", size=22)
    svg.save(PIC_DIR / "fig_data_structure.svg")


def regular_fd():
    svg = Svg()
    ox, oy, s = 125, 120, 78
    for i in range(6):
        svg.line(ox, oy + i * s, ox + 5 * s, oy + i * s, stroke=GRID, sw=1.3)
        svg.line(ox + i * s, oy, ox + i * s, oy + 5 * s, stroke=GRID, sw=1.3)
    for r in range(6):
        for c in range(6):
            fill = "white"
            stroke = GRAY
            rad = 10
            if (c, r) == (3, 3):
                fill, stroke, rad = BLACK, BLACK, 13
            elif (c, r) in [(2, 3), (4, 3), (3, 2), (3, 4)]:
                fill, stroke = "#d9e6f2", BLUE
            svg.circle(ox + c * s, oy + r * s, rad, stroke=stroke, fill=fill, sw=1.8)
    cx, cy = ox + 3 * s, oy + 3 * s
    svg.text(cx, cy - 26, "(i,j,k)", size=21, weight="700")
    svg.text(ox + 2 * s - 35, cy + 7, "i-1", size=19)
    svg.text(ox + 4 * s + 34, cy + 7, "i+1", size=19)
    svg.text(cx + 35, oy + 2 * s - 9, "j-1", size=19)
    svg.text(cx + 35, oy + 4 * s + 22, "j+1", size=19)
    svg.line(cx, cy, cx + 128, cy - 115, stroke=BLUE, sw=1.8, dash="7 6", arrow=True)
    svg.line(cx, cy, cx - 128, cy + 115, stroke=BLUE, sw=1.8, dash="7 6", arrow=True)
    svg.text(cx + 150, cy - 118, "k+1", size=19, fill=BLUE)
    svg.text(cx - 145, cy + 138, "k-1", size=19, fill=BLUE)
    svg.text(325, 625, "规则网格中邻域由线性索引和维度隐式恢复", size=21)

    x0 = 660
    svg.text(x0, 135, "中心差分", size=22, weight="700", anchor="start")
    eqs = [
        "uξ(i,j,k) ≈ [u(i+1,j,k) − u(i−1,j,k)] / 2",
        "uη(i,j,k) ≈ [u(i,j+1,k) − u(i,j−1,k)] / 2",
        "uζ(i,j,k) ≈ [u(i,j,k+1) − u(i,j,k−1)] / 2",
    ]
    svg.multiline(x0, 190, eqs, size=22, leading=48)
    svg.line(x0, 340, 1360, 340, stroke=GRID, sw=1.2)
    svg.text(x0, 405, "边界单边差分", size=22, weight="700", anchor="start")
    svg.text(x0, 460, "uξ(0,j,k) ≈ u(1,j,k) − u(0,j,k)", size=22, anchor="start")
    svg.line(x0, 535, 1360, 535, stroke=GRID, sw=1.2)
    svg.text(x0, 600, "物理空间恢复", size=22, weight="700", anchor="start")
    svg.text(x0, 660, "∇x u = J⁻ᵀ ∇ξ u", size=28, anchor="start")
    svg.save(PIC_DIR / "fig_regular_fd_neighborhood.svg")


def hexa(svg: Svg, x, y, w, h, dx, dy, stroke=BLACK, fill="white"):
    front = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    back = [(x + dx, y + dy), (x + w + dx, y + dy), (x + w + dx, y + h + dy), (x + dx, y + h + dy)]
    svg.poly(back, stroke=GRAY, fill="white", sw=1.4)
    svg.poly(front, stroke=stroke, fill=fill, sw=1.8)
    for p1, p2 in zip(front, back):
        svg.line(*p1, *p2, stroke=GRAY, sw=1.4)
    for p in front + back:
        svg.circle(*p, 6, stroke=stroke, sw=1.5, fill="white")


def shape_gradient():
    svg = Svg()
    # Supported cell icons.
    svg.text(150, 90, "支持单元", size=21, weight="700", anchor="start")
    svg.poly([(150, 130), (210, 130), (175, 180)], sw=1.5)
    svg.text(180, 210, "三角形", size=17)
    svg.poly([(250, 130), (315, 130), (315, 180), (250, 180)], sw=1.5)
    svg.text(282, 210, "四边形", size=17)
    svg.poly([(355, 180), (410, 180), (382, 128)], sw=1.5)
    svg.line(382, 128, 385, 155, stroke=GRAY, sw=1.3)
    svg.line(355, 180, 385, 155, stroke=GRAY, sw=1.3)
    svg.line(410, 180, 385, 155, stroke=GRAY, sw=1.3)
    svg.text(382, 210, "四面体", size=17)
    hexa(svg, 455, 135, 55, 45, 18, -16, stroke=GRAY)
    svg.text(492, 210, "六面体", size=17)

    # Mapping diagram.
    hexa(svg, 150, 310, 210, 180, 70, -55, stroke=BLUE, fill="#fbfdff")
    svg.line(150, 490, 360, 490, stroke=BLUE, sw=1.7, arrow=True)
    svg.line(150, 490, 150, 310, stroke=BLUE, sw=1.7, arrow=True)
    svg.line(150, 490, 220, 435, stroke=BLUE, sw=1.7, arrow=True)
    svg.text(378, 497, "ξ", size=19, fill=BLUE)
    svg.text(130, 305, "η", size=19, fill=BLUE)
    svg.text(232, 427, "ζ", size=19, fill=BLUE)
    svg.text(260, 565, "参考单元", size=23, weight="700")
    svg.text(260, 600, "(ξ, η, ζ)", size=21)

    arrow(svg, 485, 400, 655, 400, "x(ξ)=Σ Na(ξ) xa")
    front = [(735, 315), (980, 280), (1015, 465), (760, 510)]
    back = [(810, 235), (1050, 255), (1088, 438), (835, 420)]
    svg.poly(back, stroke=GRAY, sw=1.4)
    svg.poly(front, stroke=BLACK, sw=1.8, fill="white")
    for p1, p2 in zip(front, back):
        svg.line(*p1, *p2, stroke=GRAY, sw=1.4)
    for idx, p in enumerate(front + back, start=1):
        svg.circle(*p, 6, stroke=BLACK, sw=1.5, fill="white")
        dx = 13 if p[0] < 1060 else -12
        anchor = "start" if p[0] < 1060 else "end"
        svg.text(p[0] + dx, p[1] - 8, f"a{idx}", size=17, anchor=anchor)
    svg.text(910, 565, "物理单元", size=23, weight="700")
    svg.text(910, 600, "(x, y, z)", size=21)

    # Formula chain.
    y = 720
    terms = [
        ("uᵉ(ξ)=Σ Na(ξ)ua", 100, 290),
        ("J=∂x/∂ξ", 380, 540),
        ("∇xNa=J⁻ᵀ∇ξNa", 630, 860),
        ("∇x uᵉ=Σua∇xNa", 950, 1240),
    ]
    for text, x1, x2 in terms:
        svg.rect(x1, y - 32, x2 - x1, 64, sw=1.4)
        svg.text((x1 + x2) / 2, y + 8, text, size=21)
    for x1, x2 in [(290, 380), (540, 630), (860, 950)]:
        arrow(svg, x1 + 10, y, x2 - 10, y)
    svg.text(750, 825, "形函数导数经 Jacobian 由参考空间转换到物理空间", size=21)
    svg.save(PIC_DIR / "fig_unstructured_shape_gradient.svg")


def cell_lift():
    svg = Svg()
    cells = [
        [(90, 195), (235, 155), (265, 300), (110, 335)],
        [(235, 155), (400, 178), (420, 322), (265, 300)],
        [(110, 335), (265, 300), (290, 460), (135, 495)],
        [(265, 300), (420, 322), (438, 470), (290, 460)],
        [(400, 178), (555, 220), (570, 360), (420, 322)],
        [(420, 322), (570, 360), (545, 510), (438, 470)],
    ]
    for i, pts in enumerate(cells, start=1):
        svg.poly(pts, stroke=GRAY, sw=1.5)
        cx = sum(x for x, _ in pts) / 4
        cy = sum(y for _, y in pts) / 4
        svg.circle(cx, cy, 5, stroke=BLACK, fill=BLACK, sw=1)
        svg.text(cx, cy - 14, f"u{i}", size=18, weight="700")
    vertices = sorted({p for cell in cells for p in cell})
    for p in vertices:
        svg.circle(*p, 6, stroke=BLUE, fill="white", sw=1.5)
    svg.text(325, 585, "原始单元场", size=23, weight="700")
    svg.text(325, 620, "字段值 ue 定义在单元上", size=20)

    # Incidence relation.
    arrow(svg, 605, 340, 710, 340)
    svg.text(905, 250, "点-单元关联", size=23, weight="700")
    center = (880, 355)
    svg.circle(*center, 9, stroke=BLUE, fill="#d9e6f2", sw=1.6)
    svg.text(center[0] - 22, center[1] - 18, "i", size=20, fill=BLUE)
    incident = [(790, 285), (985, 295), (805, 445), (1000, 435)]
    for k, p in enumerate(incident, start=1):
        svg.circle(*p, 6, stroke=BLACK, fill="white", sw=1.4)
        svg.line(center[0], center[1], p[0], p[1], stroke=GRAY, sw=1.2)
        svg.text(p[0], p[1] - 14, f"e{k}", size=18)
    svg.text(905, 520, "Ci={e | i∈ce}", size=21)
    svg.text(905, 555, "ũi=(1/|Ci|)Σe∈Ci ue", size=21)

    arrow(svg, 1070, 340, 1170, 340)
    target = [(1190, 260), (1380, 285), (1360, 460), (1170, 435)]
    svg.poly(target, sw=1.7)
    for idx, p in enumerate(target, start=1):
        svg.circle(*p, 6, stroke=BLACK, fill="white", sw=1.5)
        svg.text(p[0] + 13, p[1] - 10, f"ũ{idx}", size=18, anchor="start")
    c = (sum(x for x, _ in target) / 4, sum(y for _, y in target) / 4)
    svg.circle(*c, 5, stroke=BLACK, fill=BLACK, sw=1)
    svg.line(c[0], c[1], c[0] + 80, c[1] - 36, sw=1.8, arrow=True)
    svg.text(1305, 235, "∇x ue", size=22, weight="700")
    svg.text(1278, 555, "单元中心梯度", size=23, weight="700")

    svg.line(140, 690, 1360, 690, stroke=GRID, sw=1.3)
    svg.text(750, 742, "单元场提升：  ũi = (1 / |Ci|) Σe∈Ci ue", size=23)
    svg.text(750, 790, "梯度恢复：    ∇x ue = Σa ũa ∇xNa(ξc)", size=23)
    svg.save(PIC_DIR / "fig_cell_field_lift.svg")


def write_prompts():
    names = {
        "fig_data_structure": "内部数据结构组织示意图",
        "fig_regular_fd_neighborhood": "规则网格有限差分邻域示意图",
        "fig_unstructured_shape_gradient": "非结构化单元形函数导数计算示意图",
        "fig_cell_field_lift": "单元场提升过程示意图",
    }
    lines = ["# 论文插图生成提示词", ""]
    for key, title in names.items():
        lines.extend([f"## {title}", FIGURE_PROMPTS[key], ""])
    (PIC_DIR / "figure_prompts.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    PIC_DIR.mkdir(parents=True, exist_ok=True)
    write_prompts()
    data_structure()
    regular_fd()
    shape_gradient()
    cell_lift()
    for path in sorted(PIC_DIR.glob("fig_*.svg")):
        print(path)


if __name__ == "__main__":
    main()
