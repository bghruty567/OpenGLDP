from __future__ import annotations

import argparse
import copy
import difflib
import re
import zipfile
from dataclasses import dataclass
from pathlib import Path

from lxml import etree


W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS = {"w": W_NS}
W = f"{{{W_NS}}}"

TOKEN_RE = re.compile(r"(\\\(.+?\\\)|\*\*[^*]+\*\*|`[^`]+`)")
IMAGE_RE = re.compile(r"!\[(.*?)\]\((.*?)\)")
LATEX_REPLACEMENTS = {
    r"\nabla": "∇",
    r"\Delta": "Δ",
    r"\delta": "δ",
    r"\xi": "ξ",
    r"\eta": "η",
    r"\zeta": "ζ",
    r"\varepsilon": "ε",
    r"\epsilon": "ε",
    r"\sigma": "σ",
    r"\alpha": "α",
    r"\ell": "l",
    r"\in": "∈",
    r"\mid": "|",
    r"\sum": "∑",
    r"\frac": "frac",
    r"\dfrac": "frac",
    r"\partial": "∂",
    r"\approx": "≈",
    r"\times": "×",
    r"\cdot": "·",
    r"\le": "≤",
    r"\ge": "≥",
    r"\neq": "≠",
    r"\left": "",
    r"\right": "",
    r"\qquad": "    ",
    r"\quad": "  ",
    r"\mathrm": "",
    r"\mathbf": "",
    r"\boldsymbol": "",
    r"\mathcal": "",
    r"\mathbb": r"\mathbb",
}


@dataclass
class MarkdownItem:
    kind: str
    raw: str
    rendered: str
    has_formula: bool


def simplify_latex(text: str) -> str:
    text = text.replace(r"\(", "").replace(r"\)", "")
    text = text.replace(r"\[", "").replace(r"\]", "")

    previous = None
    frac_pattern = re.compile(r"\\d?frac\{([^{}]+)\}\{([^{}]+)\}")
    while previous != text:
        previous = text
        text = frac_pattern.sub(r"(\1)/(\2)", text)

    for command, value in LATEX_REPLACEMENTS.items():
        text = text.replace(command, value)

    text = re.sub(r"\\(?:mathrm|mathbf|boldsymbol|mathcal)\{([^{}]+)\}", r"\1", text)
    text = text.replace(r"\\", "\n")
    text = text.replace("&", " ")
    text = text.replace("{", "").replace("}", "")
    text = re.sub(r"\s+\n", "\n", text)
    text = re.sub(r"\n\s+", "\n", text)
    text = re.sub(r"[ \t]{2,}", " ", text)
    return text.strip()


def render_inline(text: str) -> str:
    output: list[str] = []
    pos = 0
    for match in TOKEN_RE.finditer(text):
        if match.start() > pos:
            output.append(text[pos : match.start()])
        token = match.group(0)
        if token.startswith(r"\("):
            output.append(simplify_latex(token[2:-2]))
        elif token.startswith("**"):
            output.append(token[2:-2])
        else:
            output.append(token[1:-1])
        pos = match.end()
    if pos < len(text):
        output.append(text[pos:])
    return "".join(output).strip()


def split_table_row(line: str) -> list[str]:
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [cell.strip() for cell in line.split("|")]


def is_table_separator(line: str) -> bool:
    cells = split_table_row(line)
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", cell.strip()) for cell in cells)


def parse_markdown(path: Path) -> list[MarkdownItem]:
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    items: list[MarkdownItem] = []
    i = 0

    while i < len(lines):
        stripped = lines[i].strip()
        if not stripped:
            i += 1
            continue

        if stripped == "$$":
            formula_lines: list[str] = []
            i += 1
            while i < len(lines) and lines[i].strip() != "$$":
                formula_lines.append(lines[i].rstrip())
                i += 1
            raw = "\n".join(formula_lines).strip()
            items.append(MarkdownItem("formula", raw, simplify_latex(raw), True))
            i += 1
            continue

        if stripped.startswith("|"):
            while i < len(lines) and lines[i].strip().startswith("|"):
                if not is_table_separator(lines[i]):
                    for cell in split_table_row(lines[i]):
                        items.append(
                            MarkdownItem(
                                "tablecell",
                                cell,
                                render_inline(cell),
                                r"\(" in cell,
                            )
                        )
                i += 1
            continue

        if IMAGE_RE.fullmatch(stripped):
            i += 1
            continue

        if stripped.startswith("#"):
            level = len(stripped) - len(stripped.lstrip("#"))
            text = stripped[level:].strip()
            items.append(MarkdownItem("heading", text, text, False))
            i += 1
            continue

        para_lines = [stripped]
        i += 1
        while i < len(lines):
            nxt = lines[i].strip()
            if not nxt or nxt == "$$" or nxt.startswith("|") or nxt.startswith("#") or IMAGE_RE.fullmatch(nxt):
                break
            para_lines.append(nxt)
            i += 1
        raw = " ".join(para_lines)
        items.append(MarkdownItem("para", raw, render_inline(raw), r"\(" in raw))

    return items


def normalize_text(text: str) -> str:
    text = text.replace("\xa0", " ")
    text = text.replace("−", "-").replace("–", "-").replace("—", "-")
    text = text.replace("≈", "=").replace("≤ft", "")
    text = re.sub(r"\s+", "", text)
    return text.lower()


def load_document_tree(path: Path) -> tuple[etree._ElementTree, list[etree._Element], list[str]]:
    with zipfile.ZipFile(path) as archive:
        xml = archive.read("word/document.xml")
    tree = etree.fromstring(xml)
    paragraphs = tree.xpath(".//w:p", namespaces=NS)
    texts = [
        "".join(paragraph.xpath(".//w:t/text()", namespaces=NS)).replace("\n", " ").strip()
        for paragraph in paragraphs
    ]
    return tree, paragraphs, texts


def align_block(a_block: list[str], b_block: list[str]) -> list[tuple[int, int, float]]:
    n = len(a_block)
    m = len(b_block)
    skip_penalty = -0.35

    score = [[0.0] * (m + 1) for _ in range(n + 1)]
    prev: list[list[tuple[str, int, int, float] | None]] = [[None] * (m + 1) for _ in range(n + 1)]

    for i in range(1, n + 1):
        score[i][0] = score[i - 1][0] + skip_penalty
        prev[i][0] = ("up", i - 1, 0, 0.0)
    for j in range(1, m + 1):
        score[0][j] = score[0][j - 1] + skip_penalty
        prev[0][j] = ("left", 0, j - 1, 0.0)

    for i in range(1, n + 1):
        for j in range(1, m + 1):
            sim = difflib.SequenceMatcher(None, a_block[i - 1], b_block[j - 1], autojunk=False).ratio()
            candidates = [
                (score[i - 1][j] + skip_penalty, ("up", i - 1, j, 0.0)),
                (score[i][j - 1] + skip_penalty, ("left", i, j - 1, 0.0)),
                (score[i - 1][j - 1] + (sim * 2.0 - 1.0), ("diag", i - 1, j - 1, sim)),
            ]
            best_score, best_prev = max(candidates, key=lambda item: item[0])
            score[i][j] = best_score
            prev[i][j] = best_prev

    pairs: list[tuple[int, int, float]] = []
    i = n
    j = m
    while i > 0 or j > 0:
        step = prev[i][j]
        if step is None:
            break
        direction, next_i, next_j, sim = step
        if direction == "diag":
            pairs.append((i - 1, j - 1, sim))
        i = next_i
        j = next_j

    pairs.reverse()
    return [(ai, bj, sim) for ai, bj, sim in pairs if sim >= 0.45]


def build_alignment(items: list[MarkdownItem], doc_texts: list[str]) -> dict[int, int]:
    rendered_items = [(idx, item) for idx, item in enumerate(items) if item.rendered]
    a_texts = [normalize_text(item.rendered) for _, item in rendered_items]
    b_texts = [normalize_text(text) for text in doc_texts]

    sequence = difflib.SequenceMatcher(None, a_texts, b_texts, autojunk=False)
    mapping: dict[int, int] = {}

    for tag, i1, i2, j1, j2 in sequence.get_opcodes():
        if tag == "equal":
            for offset in range(i2 - i1):
                source_idx = rendered_items[i1 + offset][0]
                mapping[source_idx] = j1 + offset
            continue

        if tag != "replace":
            continue

        block_pairs = align_block(a_texts[i1:i2], b_texts[j1:j2])
        for block_i, block_j, _ in block_pairs:
            source_idx = rendered_items[i1 + block_i][0]
            mapping[source_idx] = j1 + block_j

    return mapping


def linearize_formula(text: str) -> str:
    return " ".join(line.strip() for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n") if line.strip())


def clone_run_properties(paragraph: etree._Element) -> etree._Element | None:
    run = paragraph.find(f"{W}r")
    if run is None:
        return None
    props = run.find(f"{W}rPr")
    return copy.deepcopy(props) if props is not None else None


def make_run(text: str, template_rpr: etree._Element | None) -> etree._Element:
    run = etree.Element(f"{W}r")
    if template_rpr is not None:
        run.append(copy.deepcopy(template_rpr))
    text_el = etree.SubElement(run, f"{W}t")
    if text.startswith(" ") or text.endswith(" "):
        text_el.set("{http://www.w3.org/XML/1998/namespace}space", "preserve")
    text_el.text = text
    return run


def clear_paragraph_content(paragraph: etree._Element) -> None:
    for child in list(paragraph):
        if child.tag != f"{W}pPr":
            paragraph.remove(child)


def paragraph_tokens(raw: str) -> list[tuple[str, str]]:
    tokens: list[tuple[str, str]] = []
    pos = 0
    for match in TOKEN_RE.finditer(raw):
        if match.start() > pos:
            tokens.append(("text", raw[pos : match.start()]))
        token = match.group(0)
        if token.startswith(r"\("):
            tokens.append(("formula", token[2:-2]))
        elif token.startswith("**"):
            tokens.append(("text", token[2:-2]))
        else:
            tokens.append(("text", token[1:-1]))
        pos = match.end()
    if pos < len(raw):
        tokens.append(("text", raw[pos:]))
    return tokens


def rebuild_paragraph(
    paragraph: etree._Element,
    item: MarkdownItem,
    token_counter: list[int],
    formula_map: list[tuple[str, str]],
) -> None:
    template_rpr = clone_run_properties(paragraph)
    clear_paragraph_content(paragraph)

    if item.kind == "formula":
        token_counter[0] += 1
        token = f"__EQ_PLACEHOLDER_{token_counter[0]:04d}__"
        formula_map.append((token, linearize_formula(item.raw)))
        paragraph.append(make_run(token, template_rpr))
        return

    for kind, value in paragraph_tokens(item.raw):
        if not value:
            continue
        if kind == "text":
            paragraph.append(make_run(value, template_rpr))
            continue
        token_counter[0] += 1
        token = f"__EQ_PLACEHOLDER_{token_counter[0]:04d}__"
        formula_map.append((token, linearize_formula(value)))
        paragraph.append(make_run(token, template_rpr))


def write_docx_with_updated_document(src_docx: Path, out_docx: Path, updated_xml: bytes) -> None:
    with zipfile.ZipFile(src_docx, "r") as src, zipfile.ZipFile(out_docx, "w", zipfile.ZIP_DEFLATED) as dst:
        for info in src.infolist():
            data = updated_xml if info.filename == "word/document.xml" else src.read(info.filename)
            dst.writestr(info, data)


def write_formula_map(path: Path, formula_map: list[tuple[str, str]]) -> None:
    lines = [f"{token}\t{formula}\n" for token, formula in formula_map]
    path.write_text("".join(lines), encoding="utf-16")


def prepare_formula_placeholders(src_docx: Path, src_md: Path, out_docx: Path, out_map: Path) -> tuple[int, int]:
    items = parse_markdown(src_md)
    tree, paragraphs, doc_texts = load_document_tree(src_docx)
    mapping = build_alignment(items, doc_texts)

    formula_items = [idx for idx, item in enumerate(items) if item.has_formula]
    missing = [idx for idx in formula_items if idx not in mapping]
    if missing:
        raise RuntimeError(f"Unmapped formula items: {missing[:10]}")

    formula_map: list[tuple[str, str]] = []
    token_counter = [0]
    updated_paragraphs = 0

    for item_idx in formula_items:
        paragraph_idx = mapping[item_idx]
        rebuild_paragraph(paragraphs[paragraph_idx], items[item_idx], token_counter, formula_map)
        updated_paragraphs += 1

    updated_xml = etree.tostring(tree, xml_declaration=True, encoding="UTF-8", standalone="yes")
    out_docx.parent.mkdir(parents=True, exist_ok=True)
    write_docx_with_updated_document(src_docx, out_docx, updated_xml)
    write_formula_map(out_map, formula_map)
    return updated_paragraphs, len(formula_map)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Prepare DOCX formula placeholders for Word formula rendering.")
    parser.add_argument("--src-docx", required=True)
    parser.add_argument("--src-md", required=True)
    parser.add_argument("--out-docx", required=True)
    parser.add_argument("--out-map", required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    updated_paragraphs, formula_count = prepare_formula_placeholders(
        src_docx=Path(args.src_docx).resolve(),
        src_md=Path(args.src_md).resolve(),
        out_docx=Path(args.out_docx).resolve(),
        out_map=Path(args.out_map).resolve(),
    )
    print(f"updated_paragraphs={updated_paragraphs}")
    print(f"formula_count={formula_count}")
    print(f"out_docx={Path(args.out_docx).resolve()}")
    print(f"out_map={Path(args.out_map).resolve()}")


if __name__ == "__main__":
    main()
