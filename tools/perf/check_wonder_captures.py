"""Detect flat native PPM captures outside the metrics overlay; not a rendering-correctness oracle."""
import argparse
import json
from pathlib import Path


def frame_content(path):
    with path.open('rb') as stream:
        if stream.readline() != b'P6\n':
            raise ValueError('Expected a native binary PPM capture')
        dimensions = stream.readline().split()
        if len(dimensions) != 2 or stream.readline() != b'255\n':
            raise ValueError('Invalid native PPM header')
        width, height = map(int, dimensions)
        if not (4 <= width <= 16384 and 4 <= height <= 16384):
            raise ValueError('Invalid capture dimensions')
        start = stream.tell()
        if path.stat().st_size != start + width * height * 3:
            raise ValueError('Truncated or oversized capture')
        # The middle half avoids the top-left metrics window. Sample rows in one read each.
        left, right = width // 4, width * 3 // 4
        top, bottom = height // 4, height * 3 // 4
        colours = set()
        for y in range(top, bottom, max(1, (bottom - top) // 18)):
            stream.seek(start + (y * width + left) * 3)
            row = stream.read((right - left) * 3)
            for x in range(0, right - left, max(1, (right - left) // 32)):
                colours.add(tuple(row[x * 3:x * 3 + 3]))
        # A solid coloured frame is flat too: measure variation within each channel, not between channels.
        channel_range = max(max(c[k] for c in colours) - min(c[k] for c in colours) for k in range(3))
        return {'frame': path.name, 'sampled_colours': len(colours), 'channel_range': channel_range,
                'nonflat': len(colours) >= 8 and channel_range >= 16}


def check_captures(directory):
    frames = sorted(directory.glob('*.ppm'))
    if not frames:
        raise ValueError('No PPM captures found')
    # Check a spread, including the last picture; transient loading/fades need not be colourful.
    indices = sorted({round(i * (len(frames) - 1) / 9) for i in range(10)})
    samples = [frame_content(frames[i]) for i in indices]
    return {'nonflat_content_observed': any(s['nonflat'] for s in samples), 'samples': samples,
            'scope': 'flat-frame detection outside the metrics overlay; does not certify correct rendering or gameplay'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path, help='A completed run\'s fresh-capture directory')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        result = check_captures(args.captures)
    except (OSError, ValueError) as error:
        result = {'nonflat_content_observed': False, 'error': str(error)}
    output = json.dumps(result, indent=2) + '\n'
    if args.json:
        args.json.write_text(output, encoding='utf-8')
    print(output, end='')
    return 0 if result['nonflat_content_observed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
