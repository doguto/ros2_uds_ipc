#!/usr/bin/env python3
"""Render the CSV written by run_latency_matrix.sh into SVG charts (a light and a dark variant each).

Usage: ros2 run ros2_uds_ipc_benchmark plot_latency.py RESULT_CSV OUTPUT_DIR
"""

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402
from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator  # noqa: E402

# Series in a fixed order, so that a series keeps its color whichever other series are present.
SERIES = [
    ('uds', 'UDS'),
    ('uds_reliable', 'UDS (send timeout)'),
    ('rmw_fastrtps_cpp', 'DDS (Fast DDS)'),
    ('rmw_cyclonedds_cpp', 'DDS (Cyclone DDS)'),
    ('rmw_connextdds', 'DDS (Connext)'),
    ('rmw_zenoh_cpp', 'Zenoh'),
]

THEMES = {
    'light': {
        'surface': '#fcfcfb', 'primary': '#0b0b0b', 'secondary': '#52514e', 'muted': '#898781',
        'grid': '#e1e0d9', 'axis': '#c3c2b7',
        'series': ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#008300'],
    },
    'dark': {
        'surface': '#1a1a19', 'primary': '#f0efec', 'secondary': '#c3c2b7', 'muted': '#898781',
        'grid': '#2c2c2a', 'axis': '#383835',
        'series': ['#3987e5', '#d95926', '#199e70', '#c98500', '#d55181', '#008300'],
    },
}

LATENCY_RATE = 1000.0
CPU_RATE = 10000.0
UNLIMITED_RATE = 0.0


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
    return rows


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


def select(rows, series, load, rate):
    selected = [r for r in rows if r['series'] == series and r['load_threads'] == load and r['rate_hz'] == rate]
    return sorted(selected, key=lambda r: r['payload_bytes'])


def set_size_axis(axes, sizes):
    axes.set_xticks(range(len(sizes)))
    axes.set_xticklabels([size_label(size) for size in sizes])


def plot_latency(rows, theme, loads, path):
    rows = [r for r in rows if r['rate_hz'] == LATENCY_RATE]
    if not rows:
        return False
    sizes = sorted({r['payload_bytes'] for r in rows})
    series = series_present(rows)
    figure = Figure(
        theme, 'Cross-process latency at 1 kHz',
        'Publish to subscriber callback, by payload size. Lower is better.', 2, len(loads), 5.9)

    for row_index, (column, name, log_scale) in enumerate((('p50_us', 'Median', False), ('p99_us', 'p99', True))):
        values = [r[column] for r in rows]
        for column_index, load in enumerate(loads):
            axes = figure.axes[row_index][column_index]
            for index, key, _label in series:
                points = select(rows, key, load, LATENCY_RATE)
                axes.plot(
                    [sizes.index(p['payload_bytes']) for p in points], [p[column] for p in points],
                    color=theme['series'][index], linewidth=2, marker='o', markersize=8,
                    markeredgecolor=theme['surface'], markeredgewidth=2, solid_capstyle='round',
                    solid_joinstyle='round', clip_on=False, zorder=3)
            set_size_axis(axes, sizes)
            figure.panel_title(axes, f'{name} — {load_label(load)}')
            if log_scale:
                axes.set_yscale('log')
                ticks = [t for t in (100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000)
                         if min(values) / 2.5 < t < max(values) * 2.5]
                axes.yaxis.set_major_locator(FixedLocator(ticks))
                axes.yaxis.set_minor_locator(NullLocator())
                axes.set_ylim(ticks[0] * 0.8, ticks[-1] * 1.1)
            else:
                axes.set_ylim(0, max(values) * 1.1)
            axes.yaxis.set_major_formatter(FuncFormatter(compact))
            figure.labels(
                axes, x_label='Payload (bytes)' if row_index == 1 else None,
                y_label=f'{name} latency (µs{", log scale" if log_scale else ""})' if column_index == 0 else None)

    figure.legend([
        Line2D([], [], color=theme['series'][index], linewidth=2, marker='o', markersize=8,
               markeredgecolor=theme['surface'], markeredgewidth=2, label=label)
        for index, _key, label in series])
    figure.save(path)
    return True


def plot_grouped_bars(rows, theme, panels, value, title, subtitle, y_label, path):
    """panels: list of (panel title, rows of that panel)."""
    panels = [(name, panel_rows) for name, panel_rows in panels if panel_rows]
    if not panels:
        return False
    all_rows = [r for _name, panel_rows in panels for r in panel_rows]
    sizes = sorted({r['payload_bytes'] for r in all_rows})
    series = series_present(all_rows)
    figure = Figure(theme, title, subtitle, 1, len(panels), 3.7)

    # Bars stay thin and sit next to each other; a surface-colored edge leaves a gap between neighbors.
    slot = min(0.78 / len(series), 0.22)
    bar_width = slot
    top = max(value(r) for r in all_rows) * 1.1
    for column_index, (name, panel_rows) in enumerate(panels):
        axes = figure.axes[0][column_index]
        for position, (index, key, _label) in enumerate(series):
            points = sorted((r for r in panel_rows if r['series'] == key), key=lambda r: r['payload_bytes'])
            offset = (position - (len(series) - 1) / 2) * slot
            axes.bar(
                [sizes.index(p['payload_bytes']) + offset for p in points], [value(p) for p in points],
                width=bar_width, color=theme['series'][index], edgecolor=theme['surface'], linewidth=2, zorder=3)
        set_size_axis(axes, sizes)
        axes.set_ylim(0, top)
        axes.yaxis.set_major_formatter(FuncFormatter(compact))
        figure.panel_title(axes, name)
        figure.labels(axes, x_label='Payload (bytes)', y_label=y_label if column_index == 0 else None)

    figure.legend([Patch(facecolor=theme['series'][index], label=label) for index, _key, label in series])
    figure.save(path)
    return True


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rows = load_rows(sys.argv[1])
    output_dir = Path(sys.argv[2])
    output_dir.mkdir(parents=True, exist_ok=True)

    plt.rcParams['font.family'] = 'sans-serif'
    loads = sorted({r['load_threads'] for r in rows})
    loads = loads if len(loads) <= 2 else [loads[0], loads[-1]]

    # The non-blocking UDS publisher drops messages at an unlimited rate, so its throughput is not comparable.
    lossless = [r for r in rows if r['rate_hz'] == UNLIMITED_RATE and r['publish_failed'] == 0]
    cpu = [r for r in rows if r['rate_hz'] == CPU_RATE and r['load_threads'] == loads[0]]

    for mode, theme in THEMES.items():
        suffix = '' if mode == 'light' else '-dark'
        written = [
            plot_latency(rows, theme, loads, output_dir / f'latency_1khz{suffix}.svg'),
            plot_grouped_bars(
                rows, theme, [(load_label(load), [r for r in lossless if r['load_threads'] == load]) for load in loads],
                lambda r: r['throughput_hz'], 'Throughput without loss',
                'Messages received per second when publishing as fast as possible. Higher is better.',
                'Messages per second', output_dir / f'throughput{suffix}.svg'),
            plot_grouped_bars(
                rows, theme, [(load_label(loads[0]), cpu)],
                lambda r: r['pub_cpu_us_per_msg'] + r['sub_cpu_us_per_msg'], 'CPU time per message at 10 kHz',
                'Publisher and subscriber processes combined. Lower is better.',
                'CPU time per message (µs)', output_dir / f'cpu_per_message{suffix}.svg'),
        ]
        print(f'{mode}: wrote {sum(written)} charts to {output_dir}')


if __name__ == '__main__':
    main()
