"""Check original-picture cadence, independently of configured or generated FPS."""
import argparse
import csv
import json
import statistics
from pathlib import Path


def check(rows, rate, first, last):
    rows = [r for r in rows if first <= int(r['frame']) <= last
            and r.get('original_picture', '1') == '1']
    gaps = [float(r['guest_gap_ms']) for r in rows if float(r['guest_gap_ms']) > 0]
    interval = 60 // rate
    matching = sum(int(r['guest_interval']) == interval for r in rows)
    median = statistics.median(gaps) if gaps else None
    passed = (len(rows) >= 120 and len(gaps) >= 120
              and matching / len(rows) >= .98
              and median is not None and .90 * 1000 / rate <= median <= 1.10 * 1000 / rate)
    return {'passed': passed, 'requested_rate': rate, 'from_frame': first,
            'to_frame': last, 'original_rows': len(rows), 'matching_interval_rows': matching,
            'median_guest_gap_ms': median,
            'scope': 'Original-picture cadence in this trace window; not full-speed gameplay or scanout certification'}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--rate', type=int, choices=(30, 60), required=True)
    p.add_argument('--from-frame', type=int, required=True)
    p.add_argument('--to-frame', type=int, required=True)
    p.add_argument('--json', type=Path)
    a = p.parse_args()
    with a.trace.open(newline='', encoding='utf-8-sig') as stream:
        result = check(list(csv.DictReader(stream)), a.rate, a.from_frame, a.to_frame)
    text = json.dumps(result, indent=2) + '\n'
    if a.json:
        a.json.write_text(text, encoding='utf-8')
    print(text, end='')
    raise SystemExit(0 if result['passed'] else 1)
