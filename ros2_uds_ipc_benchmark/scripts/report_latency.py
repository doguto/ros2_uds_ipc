#!/usr/bin/env python3
"""Render the CSV written by run_latency_matrix.sh into a Markdown report in the style of docs/benchmark.md.

A condition that was measured more than once is reported as the median of its runs.

Usage: ros2 run ros2_uds_ipc_benchmark report_latency.py RESULT_CSV OUTPUT_MD [--charts-dir DIR] [--environment TEXT]
"""

import argparse
import statistics
from pathlib import Path

from plot_latency import SERIES, load_label, load_rows, size_label

LABELS = dict(SERIES)
RATE_ORDER = [100.0, 1000.0, 10000.0, 0.0]


def rate_label(rate):
    if rate == 0:
        return 'Unlimited'
    return f'{rate / 1000:g} kHz' if rate >= 1000 else f'{rate:g} Hz'


def table(header, body):
    lines = ['| ' + ' | '.join(header) + ' |', '|' + '---|' * len(header)]
    lines += ['| ' + ' | '.join(str(cell) for cell in row) + ' |' for row in body]
    return lines


def fmt_us(value):
    return f'{value:.0f}' if value >= 100 else f'{value:.1f}'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('result_csv')
    parser.add_argument('output_md')
    parser.add_argument('--charts-dir', default='charts', help='directory of the charts, relative to the report')
    parser.add_argument('--environment', default='', help='free text describing the machine and software')
    args = parser.parse_args()

    rows = load_rows(args.result_csv)
    if not rows:
        raise SystemExit('no measured rows in ' + args.result_csv)
    series = [(key, label) for key, label in SERIES if any(r['series'] == key for r in rows)]
    sizes = sorted({r['payload_bytes'] for r in rows})
    rates = [rate for rate in RATE_ORDER if any(r['rate_hz'] == rate for r in rows)]
    loads = sorted({r['load_threads'] for r in rows})

    def find(key, size, rate, load):
        return next((r for r in rows if r['series'] == key and r['payload_bytes'] == size
                     and r['rate_hz'] == rate and r['load_threads'] == load), None)

    out = ['# Benchmark report', '']
    if args.environment:
        out += [args.environment, '']
    out += ['Each cell is the median of the repeated runs of that condition.', '']
    out += ['- ' + label for _key, label in series] + ['']

    charts = Path(args.charts_dir)
    out += ['## Charts', '',
            f'![Latency at 1 kHz]({charts}/latency_1khz.svg)', '',
            f'![CPU time per message at 10 kHz]({charts}/cpu_per_message.svg)', '']

    out += ['## Latency (µs)', '', 'Median (p99) from just before `publish()` to the subscriber callback.', '']
    for load in loads:
        out += [f'### {load_label(load)}', '']
        body = []
        for rate in rates:
            for key, label in series:
                cells = []
                for size in sizes:
                    row = find(key, size, rate, load)
                    cells.append(f'{fmt_us(row["p50_us"])} ({fmt_us(row["p99_us"])})' if row else '-')
                body.append([rate_label(rate), label] + cells)
        out += table(['Rate', 'Transport'] + [size_label(s) for s in sizes], body) + ['']
    out += ['At an unlimited rate the latency measures how long messages wait in a queue rather than how fast the '
            'transport is.', '']

    out += ['## Message loss', '',
            'Share of messages that `publish()` reported as not sent. Series that never lost a message are listed '
            'below the table.', '']
    lossy, lossless = [], []
    for key, label in series:
        group = [r for r in rows if r['series'] == key]
        (lossy if any(r['publish_failed'] > 0 for r in group) else lossless).append((key, label))
    if lossy:
        body = []
        for load in loads:
            for rate in rates:
                for key, label in lossy:
                    cells = []
                    for size in sizes:
                        row = find(key, size, rate, load)
                        cells.append(f'{100 * row["publish_failed"] / row["count"]:.1f}%' if row else '-')
                    body.append([load_label(load), rate_label(rate), label] + cells)
        out += table(['CPU', 'Rate', 'Transport'] + [size_label(s) for s in sizes], body) + ['']
    if lossless:
        out += ['No message was lost: ' + ', '.join(label for _key, label in lossless) + '.', '']

    out += ['## Time to connect', '',
            'From creating the publisher until a subscriber that is already running can be reached '
            '(median over all conditions, with the range in parentheses).', '']
    body = []
    for load in loads:
        cells = []
        for key, _label in series:
            values = [r['connect_ms'] for r in rows if r['series'] == key and r['load_threads'] == load]
            cells.append(f'{statistics.median(values):.1f} ms ({min(values):.1f} - {max(values):.1f})' if values else '-')
        body.append([load_label(load)] + cells)
    out += table(['CPU'] + [label for _key, label in series], body) + ['']

    out += ['## Memory', '', 'Peak RSS of the publisher / subscriber process (MB): median, and maximum in parentheses.', '']
    body = []
    for key, label in series:
        group = [r for r in rows if r['series'] == key]
        pub = [r['pub_max_rss_kb'] / 1024 for r in group]
        sub = [r['sub_max_rss_kb'] / 1024 for r in group]
        body.append([label, f'{statistics.median(pub):.0f} ({max(pub):.0f})', f'{statistics.median(sub):.0f} ({max(sub):.0f})'])
    out += table(['Transport', 'Publisher', 'Subscriber'], body) + ['']

    out += ['## CPU time per message (µs, publisher + subscriber)', '']
    body = []
    for load in loads:
        for rate in rates:
            if rate == 0:
                continue
            for key, label in series:
                cells = []
                for size in sizes:
                    row = find(key, size, rate, load)
                    cells.append(fmt_us(row['pub_cpu_us_per_msg'] + row['sub_cpu_us_per_msg']) if row else '-')
                body.append([load_label(load), rate_label(rate), label] + cells)
    out += table(['CPU', 'Rate', 'Transport'] + [size_label(s) for s in sizes], body) + ['']

    Path(args.output_md).write_text('\n'.join(out) + '\n')
    print(f'wrote {args.output_md}')


if __name__ == '__main__':
    main()
