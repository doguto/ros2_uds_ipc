#!/usr/bin/env python3
"""Render the CSV written by run_latency_matrix.sh into SVG charts (a light and a dark variant each).

A condition that was measured more than once is plotted as the median of its runs.
Runs that did not finish (the end marker never arrived) are left out.

Usage: ros2 run ros2_uds_ipc_benchmark plot_latency.py RESULT_CSV OUTPUT_DIR [--exclude SERIES ...]
"""

import argparse
import csv
import statistics
from pathlib import Path

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402

# Series in a fixed order, so that a series keeps its color whichever other series are present.
SERIES = [
    ('uds', 'UDS (non-blocking)'),
    ('uds_reliable', 'UDS (variable-size)'),
    ('uds_reliable_fixed', 'UDS (fixed-size)'),
    ('rmw_fastrtps_cpp', 'DDS (Fast DDS)'),
    ('rmw_cyclonedds_cpp', 'DDS (Cyclone DDS)'),
    ('rmw_connextdds', 'DDS (Connext)'),
    ('rmw_zenoh_cpp', 'Zenoh'),
]

THEMES = {
    'light': {
        'surface': '#fcfcfb', 'primary': '#0b0b0b', 'secondary': '#52514e', 'muted': '#898781',
        'grid': '#e1e0d9', 'axis': '#c3c2b7',
        'series': ['#2a78d6', '#eb6834', '#7b4fd1', '#1baf7a', '#eda100', '#e87ba4', '#008300'],
    },
    'dark': {
        'surface': '#1a1a19', 'primary': '#f0efec', 'secondary': '#c3c2b7', 'muted': '#898781',
        'grid': '#2c2c2a', 'axis': '#383835',
        'series': ['#3987e5', '#d95926', '#9a74e8', '#199e70', '#c98500', '#d55181', '#008300'],
    },
}

LATENCY_RATE = 1000.0
CPU_RATE = 10000.0


def load_rows(path):
    rows = []
    with open(path, newline='') as file:
        for row in csv.DictReader(file):
            if not row.get('p50_us'):
                continue
            row['series'] = row['rmw'] if row['transport'] == 'dds' else row['transport']
            for key, value in row.items():
                if key not in ('transport', 'rmw', 'series'):
                    row[key] = float(value)
            rows.append(row)

    # A run whose subscriber gave up before the end marker has no publisher-side data and only the messages
    # that arrived early, so it is left out. A condition with no complete run is not reported at all.
    runs = {}
    dropped = {}
    for row in rows:
        key = (row['series'], row['payload_bytes'], row['rate_hz'], row['load_threads'])
        if row['received'] + row['publish_failed'] < row['count']:
            dropped[key] = dropped.get(key, 0) + 1
        else:
            runs.setdefault(key, []).append(row)

    # Collapse repeated runs of a condition into their median.
    collapsed = []
    for key, group in runs.items():
        row = {name: value if isinstance(value, str) else statistics.median(r[name] for r in group)
               for name, value in group[0].items()}
        row['runs'] = len(group)
        row['runs_dropped'] = dropped.get(key, 0)
        collapsed.append(row)
    return collapsed


def compact(value, _position=None):
    if value >= 1000:
        return f'{value / 1000:g}K'
    return f'{value:g}'


def size_label(size):
    return f'{int(size) // 1024}K' if size >= 1024 else f'{int(size)}'


def load_label(load):
    return 'Idle' if load == 0 else f'{int(load)} busy-loop threads'


class Figure:
    """A row/column grid of panels with a title, a subtitle and one legend above it."""

    def __init__(self, theme, title, subtitle, rows, columns, height):
        self.theme = theme
        self.figure, self.axes = plt.subplots(
            rows, columns, figsize=(9.0, height), squeeze=False, facecolor=theme['surface'])
        top = 1.0 - 1.42 / height
        self.figure.subplots_adjust(left=0.08, right=0.98, top=top, bottom=0.62 / height, wspace=0.16, hspace=0.42)
        self.figure.text(0.02, 1.0 - 0.30 / height, title, color=theme['primary'], fontsize=13, fontweight='bold')
        self.figure.text(0.02, 1.0 - 0.56 / height, subtitle, color=theme['secondary'], fontsize=9.5)
        self.legend_y = 1.0 - 0.66 / height
        for axes in self.axes.flat:
            axes.set_facecolor(theme['surface'])
            for side in ('top', 'right', 'left'):
                axes.spines[side].set_visible(False)
            axes.spines['bottom'].set_color(theme['axis'])
            axes.tick_params(colors=theme['muted'], labelsize=8.5, length=0)
            axes.grid(axis='y', color=theme['grid'], linewidth=1)
            axes.set_axisbelow(True)

    def panel_title(self, axes, text):
        axes.set_title(text, color=self.theme['secondary'], fontsize=9.5, loc='left', pad=6)

    def labels(self, axes, x_label=None, y_label=None):
        if x_label:
            axes.set_xlabel(x_label, color=self.theme['secondary'], fontsize=9)
        if y_label:
            axes.set_ylabel(y_label, color=self.theme['secondary'], fontsize=9)

    def legend(self, handles):
        legend = self.figure.legend(
            handles=handles, loc='upper left', bbox_to_anchor=(0.012, self.legend_y), ncol=len(handles),
            frameon=False, fontsize=9, handlelength=1.6, columnspacing=1.8)
        for text in legend.get_texts():
            text.set_color(self.theme['secondary'])

    def save(self, path):
        self.figure.savefig(path, facecolor=self.theme['surface'])
        plt.close(self.figure)


def series_present(rows):
    present = {row['series'] for row in rows}
    return [(index, key, label) for index, (key, label) in enumerate(SERIES) if key in present]


def set_size_axis(axes, sizes):
    axes.set_xticks(range(len(sizes)))
    axes.set_xticklabels([size_label(size) for size in sizes])


def plot_grouped_bars(theme, grid, title, subtitle, path):
    """grid: rows of panels, each panel being (panel title, y label, rows, value function)."""
    all_rows = [r for row in grid for _name, _label, panel_rows, _value in row for r in panel_rows]
    if not all_rows:
        return False
    sizes = sorted({r['payload_bytes'] for r in all_rows})
    series = series_present(all_rows)
    figure = Figure(theme, title, subtitle, len(grid), len(grid[0]), 1.3 + 2.4 * len(grid))

    # Bars stay thin and sit next to each other; a surface-colored edge leaves a gap between neighbors.
    slot = min(0.78 / len(series), 0.22)
    for row_index, row in enumerate(grid):
        for column_index, (name, y_label, panel_rows, value) in enumerate(row):
            axes = figure.axes[row_index][column_index]
            for position, (index, key, _label) in enumerate(series):
                points = sorted((r for r in panel_rows if r['series'] == key), key=lambda r: r['payload_bytes'])
                offset = (position - (len(series) - 1) / 2) * slot
                axes.bar(
                    [sizes.index(p['payload_bytes']) + offset for p in points], [value(p) for p in points],
                    width=slot, color=theme['series'][index], edgecolor=theme['surface'], linewidth=2, zorder=3)
            set_size_axis(axes, sizes)
            # Each panel has its own scale: the ranges differ by an order of magnitude between panels.
            axes.set_ylim(0, max((value(r) for r in panel_rows), default=1.0) * 1.1)
            axes.yaxis.set_major_formatter(FuncFormatter(compact))
            figure.panel_title(axes, name)
            figure.labels(
                axes, x_label='Payload (bytes)' if row_index == len(grid) - 1 else None,
                y_label=y_label if column_index == 0 else None)

    figure.legend([Patch(facecolor=theme['series'][index], label=label) for index, _key, label in series])
    figure.save(path)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('result_csv')
    parser.add_argument('output_dir')
    parser.add_argument(
        '--exclude', nargs='+', default=[], metavar='SERIES',
        help='series to leave out: uds, uds_reliable, uds_reliable_fixed, or an RMW implementation name')
    args = parser.parse_args()
    rows = [r for r in load_rows(args.result_csv) if r['series'] not in args.exclude]
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    plt.rcParams['font.family'] = 'sans-serif'
    loads = sorted({r['load_threads'] for r in rows})
    loads = loads if len(loads) <= 2 else [loads[0], loads[-1]]

    latency = [r for r in rows if r['rate_hz'] == LATENCY_RATE]
    cpu = [r for r in rows if r['rate_hz'] == CPU_RATE and r['load_threads'] == loads[0]]

    for mode, theme in THEMES.items():
        suffix = '' if mode == 'light' else '-dark'
        written = [
            plot_grouped_bars(
                theme,
                [[(f'{name} — {load_label(load)}', f'{name} latency (µs)',
                   [r for r in latency if r['load_threads'] == load], lambda r, column=column: r[column])
                  for load in loads]
                 for column, name in (('p50_us', 'Median'), ('p99_us', 'p99'))],
                'Cross-process latency at 1 kHz', 'Publish to subscriber callback, by payload size. Lower is better.',
                output_dir / f'latency_1khz{suffix}.svg'),
            plot_grouped_bars(
                theme,
                [[(load_label(loads[0]), 'CPU time per message (µs)', cpu,
                   lambda r: r['pub_cpu_us_per_msg'] + r['sub_cpu_us_per_msg'])]],
                'CPU time per message at 10 kHz', 'Publisher and subscriber processes combined. Lower is better.',
                output_dir / f'cpu_per_message{suffix}.svg'),
        ]
        print(f'{mode}: wrote {sum(written)} charts to {output_dir}')


if __name__ == '__main__':
    main()
