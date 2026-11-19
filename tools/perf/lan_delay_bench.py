"""Loopback-only latency experiment for netplay_latency_bench; no game or GPU."""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import threading
import time


def run(args):
    output = Path(args.output).resolve()
    ready = output.with_suffix(".port.txt")
    if output.exists() or ready.exists():
        raise ValueError("Choose a new output name to preserve earlier evidence")
    output.parent.mkdir(parents=True, exist_ok=True)
    binary = str(Path(args.binary).resolve(strict=True))
    env = os.environ.copy()
    if args.dll_dir:
        env["PATH"] = str(Path(args.dll_dir).resolve(strict=True)) + os.pathsep + env.get("PATH", "")
    if args.fault != "none":
        env["NX_LAN_BENCH_FRAME_TIMEOUT_MS"] = "250"
    delay = args.delay_ms / 1000
    stop = threading.Event()
    errors, observed = [], []
    counts, message_bytes = {}, {}
    pending = {}
    faults = []
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(.1)
    port = listener.getsockname()[1]

    def relay():
        accepted = upstream = None
        try:
            deadline = time.monotonic() + 10
            while not ready.exists():
                if time.monotonic() > deadline:
                    raise TimeoutError("Native benchmark host did not start")
                if stop.wait(.002):
                    return
            # The host publishes the port before the benchmark connects here.
            while not stop.is_set():
                try:
                    accepted, _ = listener.accept()
                    break
                except socket.timeout:
                    if time.monotonic() > deadline:
                        raise TimeoutError("Native benchmark client did not connect")
            if accepted is None:
                return
            upstream = socket.create_connection(("127.0.0.1", int(ready.read_text())), timeout=2)
            sockets = (accepted, upstream)
            for connection in sockets:
                connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                connection.setblocking(False)
            queues = {connection: deque() for connection in sockets}
            pending.update({connection: bytearray() for connection in sockets})
            live = set(sockets)
            while not stop.is_set() and (live or any(queues.values())):
                now = time.monotonic()
                for source in sockets:
                    queue = queues[source]
                    target = upstream if source is accepted else accepted
                    while queue and queue[0][0] <= now:
                        due, data = queue[0]
                        if data is None:
                            target.shutdown(socket.SHUT_WR)
                            queue.popleft()
                            continue
                        try:
                            size = target.send(data)
                        except BlockingIOError:
                            break
                        if size < len(data):
                            queue[0] = (due, data[size:])
                            break
                        observed.append((time.monotonic() - (due - delay)) * 1000)
                        queue.popleft()
                deadlines = [queue[0][0] for queue in queues.values() if queue]
                wait = min(.01, max(0, min(deadlines) - time.monotonic())) if deadlines else .01
                readable, _, _ = select.select(list(live), [], [], wait)
                for source in readable:
                    data = source.recv(65536)
                    if data:
                        stream = pending[source]
                        stream.extend(data)
                        direction = "guest_to_host" if source is accepted else "host_to_guest"
                        forwarded = bytearray()
                        close_after = False
                        while len(stream) >= 40:
                            payload = int.from_bytes(stream[36:40], "little")
                            if payload > 4096:
                                raise ValueError("Relay observed an oversized application packet")
                            size = 72 + payload
                            if len(stream) < size:
                                break
                            kind = str(int.from_bytes(stream[6:8], "little"))
                            key = direction + ":" + kind
                            counts[key] = counts.get(key, 0) + 1
                            message_bytes[key] = message_bytes.get(key, 0) + size
                            packet = bytes(stream[:size])
                            del stream[:size]
                            wanted = "6" if args.fault == "omit-commit" else "5"
                            if args.fault != "none" and not faults and kind == wanted and int.from_bytes(packet[12:20], "little") == args.fault_tick:
                                faults.append(args.fault)
                                if args.fault in ("omit-input", "omit-commit"):
                                    continue
                                if args.fault == "replay-input":
                                    forwarded.extend(packet)
                                elif args.fault == "tamper-input":
                                    packet = packet[:40] + bytes([packet[40] ^ 1]) + packet[41:]
                                elif args.fault == "truncate-input":
                                    packet = packet[:-16]
                                    close_after = True
                            forwarded.extend(packet)
                        if args.fault == "none" or forwarded:
                            queues[source].append((time.monotonic() + delay, data if args.fault == "none" else bytes(forwarded)))
                        if close_after:
                            queues[source].append((time.monotonic() + delay, None))
                    else:
                        live.remove(source)
        except (OSError, ValueError, TimeoutError) as error:
            if not stop.is_set():
                errors.append(str(error))
        finally:
            for connection in (accepted, upstream):
                if connection:
                    connection.close()

    timer = None
    if os.name == "nt":
        import ctypes
        winmm = ctypes.WinDLL("winmm")
        if winmm.timeBeginPeriod(1) == 0:
            timer = winmm
    worker = threading.Thread(target=relay)
    worker.start()
    try:
        result = subprocess.run([binary, str(port), str(ready), str(args.work_ms), str(args.frames),
                                 str(int(args.pipeline))], env=env, text=True, capture_output=True, timeout=args.timeout)
    finally:
        stop.set()
        worker.join(timeout=3)
        listener.close()
        if timer:
            timer.timeEndPeriod(1)
    if result.returncode and args.fault == "none":
        raise RuntimeError(f"Native benchmark failed ({result.returncode}): {result.stderr}")
    report = json.loads(result.stdout)
    report.update(configured_one_way_delay_ms=args.delay_ms, one_frame_pipeline=args.pipeline,
                  relay_errors=errors, application_message_counts=counts, application_message_bytes=message_bytes, limitation="Synthetic exchange throughput, not game FPS or physical controller latency.")
    if args.fault != "none":
        reason = {"replay-input": "LAN session, role or tick mismatch", "tamper-input": "LAN authentication failed",
                  "omit-input": "LAN peer timed out", "omit-commit": "LAN peer timed out", "truncate-input": "LAN peer disconnected"}[args.fault]
        report.update(fault=args.fault, injected_faults=len(faults), child_exit=result.returncode,
                      expected_rejection=bool(result.returncode and len(faults) == 1 and reason in result.stderr))
    if observed:
        report["observed_forward_delay_ms"] = {"mean": sum(observed) / len(observed), "peak": max(observed)}
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    if args.fault != "none":
        return 0 if report["expected_rejection"] else 1
    return 1 if errors or not report["inputs_verified"] else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--dll-dir")
    parser.add_argument("--delay-ms", type=float, default=5)
    parser.add_argument("--work-ms", type=int, default=8)
    parser.add_argument("--frames", type=int, default=80)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--pipeline", action="store_true")
    parser.add_argument("--fault", choices=("none", "replay-input", "tamper-input", "omit-input", "omit-commit", "truncate-input"), default="none")
    parser.add_argument("--fault-tick", type=int, default=3)
    args = parser.parse_args()
    if not 0 <= args.delay_ms <= 1000 or not 0 <= args.work_ms <= 1000 or not 1 <= args.frames <= 10000:
        parser.error("Delay/work must be 0–1000 ms and frame count 1–10000")
    if not 1 <= args.timeout <= 600:
        parser.error("Timeout must be 1–600 seconds")
    if args.fault != "none" and not 2 <= args.fault_tick < args.frames:
        parser.error("Fault tick must be an established-game tick below the frame count")
    try:
        return run(args)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(2, f"LAN latency experiment failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
