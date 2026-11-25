"""Flag flat blue backgrounds in a map-only menu-cycle capture, outside menu art.

Requires Pillow. This detects the reproduced missing-capture symptom, not blur
quality generally. Do not use it on course pause screens or dark/loading scenes.
Frame bounds refer to presented capture filenames, not simulation ticks.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageStat


def flat_blue_background(image):
    w, h = image.size
    patch = image.convert('RGB').crop((int(w*.03), int(h*.4), int(w*.10), int(h*.7)))
    stat = ImageStat.Stat(patch.resize((32,32)))
    return max(stat.var) < 2 and stat.mean[2] > 60 and stat.mean[0] < 25 and stat.mean[1] < 120


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('frames', type=Path)
    parser.add_argument('--from-frame', type=int, required=True)
    parser.add_argument('--to-frame', type=int, default=1000000)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    selected = sorted(p for p in args.frames.iterdir()
                      if p.suffix.lower() in ('.ppm','.png') and p.stem.isdecimal()
                      and args.from_frame <= int(p.stem) <= args.to_frame)
    flagged=[]
    for path in selected:
        with Image.open(path) as im:
            if flat_blue_background(im): flagged.append(path.name)
    result={'passed': bool(selected) and not flagged, 'captures_checked': len(selected),
            'flat_blue_backgrounds': flagged,
            'scope': 'Flat-blue symptom in a map-only menu cycle; no whole-game or general blur certification'}
    text=json.dumps(result,indent=2)+'\n'
    if args.json: args.json.write_text(text,encoding='utf-8')
    print(text,end='')
    raise SystemExit(0 if result['passed'] else 1)
