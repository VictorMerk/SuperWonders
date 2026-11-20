"""Keep diagnostic fragment-uniform reads aligned with the real descriptor API.

This source contract needs no GPU/game or compiler. Negative controls reproduce
the three old stage-4 regressions without modifying the checked-out source.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
TRACE = (ROOT / "runtime/src/nvn/nvn_draw_trace.cpp").read_text(encoding="utf-8")
SETS = (ROOT / "runtime/src/nvn/nvn_sets.cpp").read_text(encoding="utf-8")


def frame_sig_body(source):
    start = source.index("void frame_sig_draw(")
    return source[start:source.index("}  // namespace nx::nvn::replay", start)]


def validate(trace, sets):
    mapping = re.search(r"int api_stage\(ShaderStage s\).*?Fragment\s*\?\s*(\d+)", sets)
    if mapping is None:
        raise ValueError("Review the actual Fragment API stage mapping")
    fragment = int(mapping.group(1))
    body = frame_sig_body(trace)
    direct = re.findall(r"g_state\.uniforms\[(\d+)\]\[n\]", body)
    stages = re.search(r"for\s*\(int st\s*:\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}\)", body)
    if len(direct) != 2 or any(int(stage) != fragment for stage in direct):
        raise ValueError("FrameSig watch/pass hashes must read the actual fragment stage")
    if stages is None or tuple(map(int, stages.groups())) != (0, fragment):
        raise ValueError("FrameSig rewritten/zero-block scan must include vertex and actual fragment stages")


class FrameSigStageGuard(unittest.TestCase):
    def test_actual_sources_agree(self):
        validate(TRACE, SETS)

    def test_old_watch_stage_is_rejected(self):
        body = frame_sig_body(TRACE)
        wrong = body.replace("g_state.uniforms[1][n]", "g_state.uniforms[4][n]", 1)
        with self.assertRaises(ValueError):
            validate(TRACE.replace(body, wrong), SETS)

    def test_old_rewrite_scan_stage_is_rejected(self):
        body = frame_sig_body(TRACE)
        wrong = body.replace("{0, 1}", "{0, 4}", 1)
        with self.assertRaises(ValueError):
            validate(TRACE.replace(body, wrong), SETS)

    def test_old_pass_hash_stage_is_rejected(self):
        body = frame_sig_body(TRACE)
        index = body.rindex("g_state.uniforms[1][n]")
        wrong = body[:index] + body[index:].replace("g_state.uniforms[1][n]", "g_state.uniforms[4][n]", 1)
        with self.assertRaises(ValueError):
            validate(TRACE.replace(body, wrong), SETS)


if __name__ == "__main__":
    unittest.main()
