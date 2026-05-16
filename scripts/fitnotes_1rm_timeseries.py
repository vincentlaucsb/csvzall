#!/usr/bin/env python3
"""Generate Obsidian-friendly FitNotes estimated 1RM time-series charts.

The script intentionally uses csvzall's Python bindings for CSV ingestion and
emits dependency-free SVG charts plus a Markdown report.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import date, datetime
from html import escape
from pathlib import Path
from typing import Iterable

try:
    import csvzall
except ImportError as exc:
    raise SystemExit(
        "Unable to import csvzall. Build/install the Python bindings, or set "
        "PYTHONPATH to the build package directory."
    ) from exc


@dataclass(frozen=True)
class LiftSet:
    day: date
    exercise: str
    weight_lbs: float
    reps: int
    e1rm_lbs: float


@dataclass(frozen=True)
class SeriesConfig:
    key: str
    title: str
    exact_exercises: tuple[str, ...]
    contains_terms: tuple[str, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate FitNotes estimated 1RM charts for Obsidian."
    )
    parser.add_argument("csv", type=Path, help="FitNotes CSV export path")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Markdown output path. Defaults to <csv-dir>/FitNotes_1RM_Timeseries.md",
    )
    parser.add_argument(
        "--asset-dir",
        type=Path,
        default=None,
        help="Chart asset directory. Defaults to <output-stem>_assets beside the Markdown file.",
    )
    parser.add_argument(
        "--bench-exercise",
        action="append",
        default=[],
        help="Exact exercise name to include in the bench chart. Repeatable.",
    )
    parser.add_argument(
        "--bench-contains",
        action="append",
        default=[],
        help="Case-insensitive exercise-name substring to include in the bench chart. Repeatable.",
    )
    parser.add_argument(
        "--deadlift-exercise",
        action="append",
        default=[],
        help="Exact exercise name to include in the deadlift chart. Repeatable.",
    )
    parser.add_argument(
        "--deadlift-contains",
        action="append",
        default=[],
        help="Case-insensitive exercise-name substring to include in the deadlift chart. Repeatable.",
    )
    parser.add_argument(
        "--max-reps",
        type=int,
        default=12,
        help="Ignore sets above this rep count for estimated 1RM reliability. Use 0 for no cap.",
    )
    parser.add_argument(
        "--rows-per-chunk",
        type=int,
        default=4096,
        help="Rows per csvzall chunk.",
    )
    return parser.parse_args()


def parse_date(value: str) -> date:
    return datetime.strptime(value.strip(), "%Y-%m-%d").date()


def parse_float(value: str) -> float | None:
    text = value.replace("$", "").replace(",", "").strip()
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def parse_int(value: str) -> int | None:
    text = value.strip()
    if not text:
        return None
    try:
        return int(float(text))
    except ValueError:
        return None


def to_lbs(weight: float, unit: str) -> float:
    normalized = unit.strip().lower()
    if normalized in {"kg", "kgs", "kilogram", "kilograms"}:
        return weight * 2.2046226218
    return weight


def epley_1rm(weight_lbs: float, reps: int) -> float:
    return weight_lbs * (1.0 + reps / 30.0)


def matches(exercise: str, exact: Iterable[str], contains: Iterable[str]) -> bool:
    if exercise in set(exact):
        return True
    lowered = exercise.lower()
    return any(term.lower() in lowered for term in contains)


def collect_best_daily_sets(
    csv_path: Path,
    configs: list[SeriesConfig],
    rows_per_chunk: int,
    max_reps: int,
) -> dict[str, list[LiftSet]]:
    best_by_day: dict[str, dict[date, LiftSet]] = {config.key: {} for config in configs}

    for doc in csvzall.chunked_csv(csv_path, rows_per_chunk=rows_per_chunk):
        for index in range(len(doc)):
            row = doc.row(index)
            exercise = str(row["Exercise"]).strip()
            weight = parse_float(str(row["Weight"]))
            reps = parse_int(str(row["Reps"]))
            if not exercise or weight is None or reps is None or reps <= 0:
                continue
            if max_reps > 0 and reps > max_reps:
                continue

            try:
                day = parse_date(str(row["Date"]))
            except ValueError:
                continue

            weight_lbs = to_lbs(weight, str(row["Weight Unit"]))
            estimated = epley_1rm(weight_lbs, reps)

            for config in configs:
                if not matches(exercise, config.exact_exercises, config.contains_terms):
                    continue
                candidate = LiftSet(day, exercise, weight_lbs, reps, estimated)
                current = best_by_day[config.key].get(day)
                if current is None or candidate.e1rm_lbs > current.e1rm_lbs:
                    best_by_day[config.key][day] = candidate

    return {
        key: [best_by_day[key][day] for day in sorted(best_by_day[key])]
        for key in best_by_day
    }


def svg_chart(title: str, sets: list[LiftSet], width: int = 1200, height: int = 720) -> str:
    margin_left, margin_right, margin_top, margin_bottom = 90, 36, 80, 92
    plot_w = width - margin_left - margin_right
    plot_h = height - margin_top - margin_bottom

    if not sets:
        return "\n".join(
            [
                f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
                '<rect width="100%" height="100%" fill="#ffffff"/>',
                '<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#17202a}.title{font-size:28px;font-weight:700}.note{font-size:16px;fill:#566573}</style>',
                f'<text class="title" x="80" y="70">{escape(title)}</text>',
                '<text class="note" x="80" y="120">No matching sets were found.</text>',
                "</svg>",
            ]
        )

    days = [item.day for item in sets]
    values = [item.e1rm_lbs for item in sets]
    min_value = min(values)
    max_value = max(values)
    value_span = max_value - min_value
    if value_span == 0:
        min_value -= 10.0
        max_value += 10.0
    else:
        pad = value_span * 0.10
        min_value -= pad
        max_value += pad
    value_span = max_value - min_value

    min_ord = days[0].toordinal()
    max_ord = days[-1].toordinal()
    day_span = max(1, max_ord - min_ord)

    def x_for(day: date) -> float:
        return margin_left + ((day.toordinal() - min_ord) / day_span) * plot_w

    def y_for(value: float) -> float:
        return margin_top + ((max_value - value) / value_span) * plot_h

    points = " ".join(f"{x_for(item.day):.2f},{y_for(item.e1rm_lbs):.2f}" for item in sets)
    area_points = f"{margin_left},{height - margin_bottom} {points} {width - margin_right},{height - margin_bottom}"

    tick_count = 5
    y_ticks = [min_value + (value_span * i / tick_count) for i in range(tick_count + 1)]
    x_indices = sorted(set(round(i * (len(sets) - 1) / 5) for i in range(6)))

    first = sets[0]
    last = sets[-1]
    change = last.e1rm_lbs - first.e1rm_lbs
    change_pct = (change / first.e1rm_lbs * 100.0) if first.e1rm_lbs else 0.0
    best = max(sets, key=lambda item: item.e1rm_lbs)

    svg: list[str] = []
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">')
    svg.append('<rect width="100%" height="100%" fill="#ffffff"/>')
    svg.append(
        "<style>"
        "text{font-family:Segoe UI,Arial,sans-serif;fill:#17202a}"
        ".title{font-size:28px;font-weight:700}.subtitle{font-size:14px;fill:#5d6d7e}"
        ".axis{font-size:12px;fill:#566573}.kpi{font-size:14px;font-weight:600}"
        ".grid{stroke:#e5e8e8;stroke-width:1}.axis-line{stroke:#839192;stroke-width:1.2}"
        ".line{fill:none;stroke:#7d3c98;stroke-width:3;stroke-linejoin:round;stroke-linecap:round}"
        ".area{fill:#eadcf8;opacity:.55}.dot{fill:#7d3c98}.best{fill:#117a65}"
        "</style>"
    )
    svg.append(f'<text class="title" x="{margin_left}" y="42">{escape(title)}</text>')
    svg.append(
        f'<text class="subtitle" x="{margin_left}" y="64">'
        f'Epley estimated 1RM · best set per day · {days[0]:%b %d, %Y} to {days[-1]:%b %d, %Y}'
        "</text>"
    )

    for tick in y_ticks:
        y = y_for(tick)
        svg.append(f'<line class="grid" x1="{margin_left}" y1="{y:.2f}" x2="{width - margin_right}" y2="{y:.2f}"/>')
        svg.append(f'<text class="axis" x="{margin_left - 12}" y="{y + 4:.2f}" text-anchor="end">{tick:,.0f} lb</text>')

    for index in x_indices:
        day = days[index]
        x = x_for(day)
        svg.append(f'<line class="grid" x1="{x:.2f}" y1="{margin_top}" x2="{x:.2f}" y2="{height - margin_bottom}"/>')
        svg.append(f'<text class="axis" x="{x:.2f}" y="{height - margin_bottom + 26}" text-anchor="middle">{day:%b %d}</text>')

    svg.append(f'<line class="axis-line" x1="{margin_left}" y1="{height - margin_bottom}" x2="{width - margin_right}" y2="{height - margin_bottom}"/>')
    svg.append(f'<line class="axis-line" x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" y2="{height - margin_bottom}"/>')
    svg.append(f'<polygon class="area" points="{area_points}"/>')
    svg.append(f'<polyline class="line" points="{points}"/>')

    for item, css_class in [(first, "dot"), (last, "dot"), (best, "best")]:
        x = x_for(item.day)
        y = y_for(item.e1rm_lbs)
        anchor = "start" if x < width / 2 else "end"
        dx = 10 if anchor == "start" else -10
        label = f"{item.e1rm_lbs:,.0f} lb"
        svg.append(f'<circle class="{css_class}" cx="{x:.2f}" cy="{y:.2f}" r="5"/>')
        svg.append(f'<text class="axis" x="{x + dx:.2f}" y="{y - 10:.2f}" text-anchor="{anchor}">{escape(label)}</text>')

    kpi_y = height - 28
    svg.append(f'<text class="kpi" x="{margin_left}" y="{kpi_y}">Start: {first.e1rm_lbs:,.0f} lb</text>')
    svg.append(f'<text class="kpi" x="{margin_left + 190}" y="{kpi_y}">Latest: {last.e1rm_lbs:,.0f} lb</text>')
    svg.append(f'<text class="kpi" x="{margin_left + 390}" y="{kpi_y}">Best: {best.e1rm_lbs:,.0f} lb on {best.day:%b %d}</text>')
    svg.append(f'<text class="kpi" x="{margin_left + 675}" y="{kpi_y}">Change: {change:+,.0f} lb ({change_pct:+.1f}%)</text>')
    svg.append("</svg>")
    return "\n".join(svg)


def obsidian_embed(path: Path, output_path: Path) -> str:
    relative = path.relative_to(output_path.parent).as_posix()
    return f"![[{relative}]]"


def markdown_report(
    source: Path,
    output: Path,
    chart_paths: dict[str, Path],
    configs: list[SeriesConfig],
    series: dict[str, list[LiftSet]],
    max_reps: int,
) -> str:
    lines = [
        "---",
        "tags:",
        "  - exercise",
        "  - fitnotes",
        "  - strength",
        "---",
        "",
        "# Estimated 1RM Time Series",
        "",
        f"Source: `{source.name}`",
        "",
        f"Formula: Epley estimated 1RM = `weight * (1 + reps / 30)`.",
        f"Rep cap: {'none' if max_reps <= 0 else str(max_reps)}.",
        "",
    ]

    for config in configs:
        data = series[config.key]
        lines.append(f"## {config.title}")
        lines.append("")
        criteria = []
        if config.exact_exercises:
            criteria.append(
                "exact: " + ", ".join(f"`{name}`" for name in config.exact_exercises)
            )
        if config.contains_terms:
            criteria.append(
                "contains: " + ", ".join(f"`{term}`" for term in config.contains_terms)
            )
        lines.append("Matched exercises: " + "; ".join(criteria) + ".")
        lines.append("")
        lines.append(obsidian_embed(chart_paths[config.key], output))
        lines.append("")
        if data:
            first = data[0]
            last = data[-1]
            best = max(data, key=lambda item: item.e1rm_lbs)
            lines.extend(
                [
                    "| Metric | Value |",
                    "|---|---:|",
                    f"| First | {first.e1rm_lbs:,.1f} lb ({first.day}) |",
                    f"| Latest | {last.e1rm_lbs:,.1f} lb ({last.day}) |",
                    f"| Best | {best.e1rm_lbs:,.1f} lb ({best.day}, {best.weight_lbs:,.1f} x {best.reps}) |",
                    f"| Data points | {len(data)} |",
                    "",
                ]
            )
        else:
            lines.append("No matching sets found.")
            lines.append("")

    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    csv_path = args.csv.resolve()
    output = (args.output or csv_path.with_name("FitNotes_1RM_Timeseries.md")).resolve()
    asset_dir = (args.asset_dir or output.with_name(f"{output.stem}_assets")).resolve()

    configs = [
        SeriesConfig(
            key="bench_press",
            title="Bench Press Estimated 1RM",
            exact_exercises=tuple(args.bench_exercise or ["Flat Barbell Bench Press"]),
            contains_terms=tuple(args.bench_contains),
        ),
        SeriesConfig(
            key="deadlift",
            title="Deadlift Estimated 1RM",
            exact_exercises=tuple(args.deadlift_exercise or ["Deadlift"]),
            contains_terms=tuple(args.deadlift_contains),
        ),
    ]

    series = collect_best_daily_sets(csv_path, configs, args.rows_per_chunk, args.max_reps)
    asset_dir.mkdir(parents=True, exist_ok=True)

    chart_paths: dict[str, Path] = {}
    for config in configs:
        chart_path = asset_dir / f"{config.key}_estimated_1rm.svg"
        chart_path.write_text(svg_chart(config.title, series[config.key]), encoding="utf-8")
        chart_paths[config.key] = chart_path

    output.write_text(
        markdown_report(csv_path, output, chart_paths, configs, series, args.max_reps),
        encoding="utf-8",
    )

    print(output)
    for config in configs:
        data = series[config.key]
        print(f"{config.key}: {len(data)} points")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
