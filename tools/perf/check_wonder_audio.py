"""Check captured runtime PCM for non-silent samples; does not assess audible quality."""
import argparse
from array import array
import json
import math
from pathlib import Path
import sys
import wave


def check_audio(path):
    with wave.open(str(path), 'rb') as stream:
        channels, rate, width, frames = (stream.getnchannels(), stream.getframerate(),
                                         stream.getsampwidth(), stream.getnframes())
        if stream.getcomptype() != 'NONE' or width != 2 or channels < 1 or rate < 1:
            raise ValueError('Expected uncompressed 16-bit runtime PCM')
        remaining, peak, squares, nonzero = frames, 0, 0, 0
        while remaining:
            count = min(remaining, 16384)
            data = stream.readframes(count)
            if len(data) != count * channels * width:
                raise ValueError('Truncated PCM data')
            values = array('h', data)
            if sys.byteorder != 'little':
                values.byteswap()
            peak = max(peak, max(abs(v) for v in values))
            squares += sum(v * v for v in values)
            nonzero += sum(v != 0 for v in values)
            remaining -= count
    samples = frames * channels
    return {'non_silent_audio_observed': nonzero > 0, 'frames': frames, 'sample_rate': rate,
            'channels': channels, 'peak': peak, 'rms': math.sqrt(squares / samples) if samples else 0,
            'nonzero_samples': nonzero,
            'scope': 'captured PCM activity; does not certify audible quality or hardware output'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audio', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    try:
        result = check_audio(args.audio)
    except (OSError, ValueError, EOFError, wave.Error) as error:
        result = {'non_silent_audio_observed': False, 'error': str(error)}
    output = json.dumps(result, indent=2) + '\n'
    if args.json:
        args.json.write_text(output, encoding='utf-8')
    print(output, end='')
    return 0 if result['non_silent_audio_observed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
