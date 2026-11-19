"""Verify the median shader's selection network against an independent sort."""
import itertools
from pathlib import Path
import random
import re

shader = (Path(__file__).resolve().parents[1] / "runtime/shaders/framegen/fg_median.comp").read_text()
pairs = [(int(a), int(b)) for a, b in re.findall(r"order\(v\[(\d+)\],\s*v\[(\d+)\]\)", shader)]
assert len(pairs) == 19, "unexpected median selection network"
rng = random.Random(7119)
inputs = itertools.chain(itertools.product((0, 1), repeat=9),
                         (tuple(rng.uniform(-1000, 1000) for _ in range(9)) for _ in range(10000)))
for source in inputs:
    values = list(source)
    for a, b in pairs:
        values[a], values[b] = min(values[a], values[b]), max(values[a], values[b])
    assert values[4] == sorted(source)[4], source
print("median selection: all 512 binary inputs and 10000 finite float inputs pass")
