"""Exercise authenticated native sessions through a deliberately faulty TCP relay."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser()
p.add_argument("--binary", required=True)
args = p.parse_args()
relay = Path(__file__).resolve().parents[1] / "tools/perf/lan_delay_bench.py"
with tempfile.TemporaryDirectory(prefix="lan-transport-fault-") as temp:
    for buffered in (False, True):
        for fault in ("replay-input", "tamper-input", "omit-input", "omit-commit", "truncate-input"):
            report = Path(temp) / f"{fault}-{buffered}.json"
            command = [sys.executable, str(relay), "--binary", args.binary, "--output", str(report),
                       "--frames", "12", "--work-ms", "0", "--delay-ms", "1", "--timeout", "5", "--fault", fault]
            if buffered:
                command.append("--pipeline")
            result = subprocess.run(command, capture_output=True, text=True, timeout=8)
            if result.returncode:
                raise RuntimeError(f"{fault}, buffered={buffered}: {result.stdout} {result.stderr}")
            data = json.loads(report.read_text(encoding="utf-8"))
            assert data["expected_rejection"] and data["injected_faults"] == 1 and not data["inputs_verified"]
            assert not any(key.endswith(":7") for key in data["application_message_counts"]), "No standalone game Ack in protocol 6"
print("Ten serial/buffered TCP replay, authentication, timeout and truncated-stream regressions passed")
