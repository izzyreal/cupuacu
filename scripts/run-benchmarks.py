#!/usr/bin/env python3
"""Run isolated Cupuacu scenarios and compare versioned performance reports."""
import argparse
import atexit
import select
import signal
import stat
import ctypes
import fnmatch
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time

SCHEMA = 1
QUICK = ["open_uncached", "open_cached", "sample_shared", "delete", "gain_fixed",
         "gain_all", "undo", "redo", "scroll", "zoom"]
EXTRA = ["sample", "copy", "paste", "trim", "scroll_dirty", "zoom_dirty", "waveform_build"]
SDL_CASES = ["open_uncached", "open_cached", "scroll", "zoom", "scroll_dirty", "zoom_dirty", "responsive_gain", "responsive_stall"]


def atomic_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def resident_bytes(pid):
    if os.name != "nt":
        reply = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True)
        return int(reply.stdout.strip() or 0) * 1024
    from ctypes import wintypes
    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ("peak", "working", "a", "b", "c", "d", "e", "f")]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000 | 0x10, False, pid)
    if not handle:
        return 0
    try:
        values = Counters()
        values.cb = ctypes.sizeof(values)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
        return values.working if psapi.GetProcessMemoryInfo(handle, ctypes.byref(values), values.cb) else 0
    finally:
        kernel.CloseHandle(handle)


def directory_bytes(root):
    total = 0
    for path in Path(root).rglob("*"):
        try:
            info = path.stat()
        except FileNotFoundError:
            continue  # Workers atomically rename temporary undo/cache files.
        if stat.S_ISREG(info.st_mode):
            total += info.st_size
    return total



class IsolatedProcess:
    """A fresh fork from a pristine, single-threaded benchmark parent on Unix."""
    servers = {}

    def __init__(self, binary, request_path, result_path, log_path):
        key = str(binary)
        if key not in self.servers:
            server = subprocess.Popen([key, "--server"], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                      text=True, bufsize=1)
            if not select.select([server.stdout], [], [], 30)[0]:
                server.kill(); server.wait()
                raise RuntimeError("Benchmark parent initialization timed out")
            if not json.loads(server.stdout.readline()).get("ready"):
                raise RuntimeError("Benchmark parent failed to initialize")
            self.servers[key] = server
        self.server = self.servers[key]
        self.server.stdin.write(json.dumps({"request": str(request_path), "result": str(result_path), "log": str(log_path)}) + "\n")
        self.server.stdin.flush()
        self.pid = json.loads(self.server.stdout.readline())["pid"]
        if self.pid <= 0:
            raise RuntimeError("Could not fork benchmark child")
        self.returncode = None

    def poll(self):
        if self.returncode is None and select.select([self.server.stdout], [], [], 0)[0]:
            message = self.server.stdout.readline()
            self.returncode = json.loads(message)["exit_code"] if message else -1
        return self.returncode

    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def wait(self):
        while self.poll() is None:
            time.sleep(0.005)
        return self.returncode

    @classmethod
    def close(cls):
        for server in cls.servers.values():
            server.stdin.close()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()
        cls.servers.clear()


atexit.register(IsolatedProcess.close)

def run_child(binary, request, workspace, timeout, max_rss, max_disk):
    workspace = Path(workspace)
    request_path, result_path = workspace / "request.json", workspace / "result.json"
    atomic_json(request_path, request)
    started = time.monotonic()
    failure = None
    sampled_peak = 0
    with (workspace / "process.log").open("w+") as log:
        process = (IsolatedProcess(binary, request_path, result_path, workspace / "process.log")
                   if os.name != "nt" else
                   subprocess.Popen([str(binary), str(request_path), str(result_path)], stdout=log, stderr=subprocess.STDOUT))
        try:
            next_disk_check = started
            while process.poll() is None:
                now = time.monotonic()
                sampled_peak = max(sampled_peak, resident_bytes(process.pid))
                if now - started > timeout:
                    failure = "timeout"
                elif max_rss and sampled_peak > max_rss:
                    failure = "memory_limit"
                elif max_disk and now >= next_disk_check:
                    if directory_bytes(workspace) > max_disk:
                        failure = "disk_limit"
                    next_disk_check = now + 0.5
                if failure:
                    process.kill()
                    break
                time.sleep(0.025)
            process.wait()
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        log.seek(0)
        process_log = log.read()
    try:
        data = json.loads(result_path.read_text())
    except (OSError, ValueError):
        data = {}
    if max_rss and data.get("peak_process_rss_bytes_including_setup", 0) > max_rss:
        failure = "memory_limit"
    if failure or process.returncode or not (data.get("validated") or data.get("generated")):
        data.update(status=failure or "failed", error=data.get("error", f"Exit code {process.returncode}"),
                    process_log=process_log[-12000:])
    else:
        data["status"] = "ok"
    data["subprocess_wall_seconds"] = time.monotonic() - started
    data["sampled_process_peak_rss_bytes"] = sampled_peak
    return data


def identity(row):
    q = row["case"]
    return tuple(q.get(k) for k in ("suite", "mode", "scenario", "frames", "format", "position", "history_depth"))


def summarize(rows):
    groups = {}
    for row in rows:
        groups.setdefault(identity(row), []).append(row)
    summaries = []
    for key, values in sorted(groups.items(), key=lambda pair: str(pair[0])):
        valid = [r for r in values if r.get("status") == "ok"]
        summary = {"case": values[0]["case"], "successful_repetitions": len(valid),
                   "failed_repetitions": len(values) - len(valid), "metrics": {}}
        metrics = {}
        for row in valid:
            for k, v in row.get("milestones_ms", {}).items():
                if v is not None:
                    metrics.setdefault(k + "_ms", []).append(v)
            for k, v in (row.get("work") or {}).items():
                metrics.setdefault(k, []).append(v)
            for group in ("event_latency", "waveform_queries", "navigation_dispatch", "navigation_during_work"):
                for k, v in row.get(group, {}).items():
                    if v is not None:
                        metrics.setdefault(group + "_" + k, []).append(v)
            for k in ("tracked_capacity_start_bytes", "tracked_capacity_end_bytes", "tracked_capacity_peak_bytes",
                      "peak_process_rss_bytes_including_setup"):
                if row.get(k) is not None:
                    metrics.setdefault(k, []).append(row[k])
        for name, samples in metrics.items():
            summary["metrics"][name] = {"median": statistics.median(samples),
                "min": min(samples), "max": max(samples),
                "stdev": statistics.stdev(samples) if len(samples) > 1 else None}
        summaries.append(summary)
    return summaries


def compatibility(report):
    envs = []
    for row in report.get("runs", []):
        env = row.get("environment")
        if not env:
            continue
        # Build diagnostic text also includes revision/date; explicit compiler,
        # platform and configuration fields are used for compatibility instead.
        item = {k: env.get(k) for k in ("dependencies", "system_ram_mib", "cpu_count", "compiler", "configuration", "renderer")}
        if item not in envs:
            envs.append(item)
    return {"host": report.get("host"), "fixture_version": report.get("fixture_version"),
            "environments": sorted(envs, key=lambda x: json.dumps(x, sort_keys=True))}


def compare(before, after):
    if before.get("schema_version") != SCHEMA:
        return {"compatible": False, "reason": "Unsupported baseline schema"}
    if "summaries" not in before or "summaries" not in after:
        return {"compatible": False, "reason": "Incomplete report"}
    if compatibility(before) != compatibility(after):
        return {"compatible": False, "reason": "Host, build, renderer, dependencies or fixture version differ"}
    old = {identity(s): s for s in before["summaries"]}
    new = {identity(s): s for s in after["summaries"]}
    changes = []
    for key in sorted(old.keys() | new.keys(), key=str):
        if key not in old or key not in new:
            changes.append({"case": (new.get(key) or old[key])["case"], "status": "added" if key in new else "missing"})
            continue
        delta = {"case": new[key]["case"], "status": "matched", "metrics": {}}
        for metric in old[key]["metrics"].keys() | new[key]["metrics"].keys():
            if metric not in old[key]["metrics"] or metric not in new[key]["metrics"]:
                delta["metrics"][metric] = {"status": "unavailable"}
                continue
            a, b = old[key]["metrics"][metric]["median"], new[key]["metrics"][metric]["median"]
            delta["metrics"][metric] = {"before": a, "after": b, "ratio": b / a if a else None}
        changes.append(delta)
    return {"compatible": True, "changes": changes}


def scaling(summaries):
    groups = {}
    for item in summaries:
        q = item["case"]
        key = tuple((k, v) for k, v in sorted(q.items()) if k != "frames")
        groups.setdefault(key, []).append(item)
    growth = []
    for group in groups.values():
        ordered = sorted(group, key=lambda x: x["case"]["frames"])
        for small, big in zip(ordered, ordered[1:]):
            entry = {"case": big["case"], "previous_frames": small["case"]["frames"],
                     "frame_ratio": big["case"]["frames"] / small["case"]["frames"], "ratios": {}}
            for metric in small["metrics"].keys() & big["metrics"].keys():
                a, b = small["metrics"][metric]["median"], big["metrics"][metric]["median"]
                entry["ratios"][metric] = b / a if a else None
            growth.append(entry)
    return growth


def cases(profile, sizes, suites):
    for suite in suites:
        scenarios = (QUICK + (EXTRA if profile != "quick" else [])) if suite == "core" else SDL_CASES
        for mib in sizes:
            for scenario in scenarios:
                for mode in (["timing", "diagnostic"] if suite == "core" else ["timing"]):
                    yield {"suite": suite, "mode": mode, "scenario": scenario, "frames": int(mib * 1048576 / 8),
                           "format": "wav" if mib < 4096 else "caf", "position": "begin", "history_depth": 1}
        if suite == "core":
            for scenario in ["responsive_stall", "responsive_gain", "waveform_build"]:
                if scenario in scenarios:
                    continue
                yield {"suite": suite, "mode": "diagnostic" if scenario == "waveform_build" else "timing",
                       "scenario": scenario, "frames": 131072, "format": "wav", "position": "begin", "history_depth": 1}
            if profile != "quick":
                for depth in (1, 8, 32):
                    for mode in ("timing", "diagnostic"):
                        yield {"suite": suite, "mode": mode, "scenario": "history", "frames": 524288,
                               "format": "wav", "position": "begin", "history_depth": depth}
                for mib in sizes:
                    for position in ("middle", "end"):
                        for mode in ("timing", "diagnostic"):
                            yield {"suite": suite, "mode": mode, "scenario": "delete", "frames": int(mib * 1048576 / 8),
                                   "format": "wav", "position": position, "history_depth": 1}
                    for scenario in ("open_uncached", "open_cached"):
                        yield {"suite": suite, "mode": "timing", "scenario": scenario, "frames": int(mib * 1048576 / 8),
                               "format": "flac", "position": "begin", "history_depth": 1}


def text_report(report):
    lines = ["# Cupuacu performance baseline", "", f"Profile: {report['profile']}; elapsed: {report['elapsed_seconds']:.1f}s.",
             "", "Times include command execution and resulting background completion. RSS includes setup.", "",
             "| Suite/mode | Scenario | Audio MiB | Median completion ms | Peak RSS MiB | Status |",
             "|---|---|---:|---:|---:|---|"]
    for row in report["summaries"]:
        q, m = row["case"], row["metrics"]
        value = m.get("background_complete_ms", {}).get("median")
        rss = m.get("peak_process_rss_bytes_including_setup", {}).get("median")
        lines.append(f"| {q['suite']}/{q['mode']} | {q['scenario']} ({q['position']}, history {q['history_depth']}, {q['format']}) | "
                     f"{q['frames'] * 8 / 1048576:g} | {value:.3f} | {rss / 1048576:.1f} | "
                     f"{row['successful_repetitions']} ok, {row['failed_repetitions']} failed |" if value is not None and rss is not None else
                     f"| {q['suite']}/{q['mode']} | {q['scenario']} | {q['frames'] * 8 / 1048576:g} | — | — | failed |")
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--profile", choices=("quick", "extended", "large"), default="quick")
    parser.add_argument("--suite", choices=("core", "sdl", "all"), default="core")
    parser.add_argument("--filter", default="*", help="Scenario glob, e.g. '*gain*'")
    parser.add_argument("--sizes-mib", type=float, nargs="+")
    parser.add_argument("--repetitions", type=int)
    parser.add_argument("--output", type=Path, default=Path("dist/benchmarks/latest.json"))
    parser.add_argument("--fixtures", type=Path, default=Path("build/benchmark-fixtures"))
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=120)
    parser.add_argument("--max-rss-mib", type=int)
    parser.add_argument("--max-disk-mib", type=int)
    args = parser.parse_args(argv)
    if args.profile == "large" and not all((args.sizes_mib, args.max_rss_mib, args.max_disk_mib)):
        parser.error("large requires explicit --sizes-mib, --max-rss-mib and --max-disk-mib")
    baseline = json.loads(args.compare.read_text()) if args.compare else None
    if args.max_rss_mib is not None and args.max_rss_mib <= 0 or args.max_disk_mib is not None and args.max_disk_mib <= 0:
        parser.error("Resource limits must be positive")
    sizes = args.sizes_mib or ([1, 4, 16] if args.profile == "quick" else [1, 4, 16, 64, 256])
    repetitions = args.repetitions if args.repetitions is not None else (3 if args.profile == "quick" else 7)
    if any(not 1 <= value <= 1048576 for value in sizes) or repetitions < 1 or args.timeout_seconds <= 0:
        parser.error("sizes must be between 1 and 1048576 MiB; repetitions and timeout must be positive")
    max_rss = (args.max_rss_mib or 2048) * 1048576
    max_disk = (args.max_disk_mib or 4096) * 1048576
    suites = ["core", "sdl"] if args.suite == "all" else [args.suite]
    binaries = {("core", "timing"): "cupuacu-benchmarks", ("core", "diagnostic"): "cupuacu-benchmarks-metrics", ("sdl", "timing"): "cupuacu-benchmarks-sdl"}
    selected = [case for case in cases(args.profile, sizes, suites) if fnmatch.fnmatch(case["scenario"], args.filter)]
    if not selected:
        parser.error("No scenarios selected")
    for key in {(case["suite"], case["mode"]) for case in selected} | {("core", "timing")}:
        binary = args.build_dir.resolve() / (binaries[key] + (".exe" if os.name == "nt" else ""))
        if not binary.is_file():
            parser.error(f"Missing executable: {binary}; build the selected benchmark targets first")
        binaries[key] = binary
    args.fixtures.mkdir(parents=True, exist_ok=True)
    fixture_started = time.monotonic()
    fixture_failures = []
    for frames, fmt in sorted({(q["frames"], q["format"]) for q in selected if q["scenario"].startswith("open_")}):
        path = args.fixtures.resolve() / f"v2-{frames}.{fmt}"
        marker = path.with_suffix(path.suffix + ".json")
        if path.exists() and marker.exists():
            try:
                manifest = json.loads(marker.read_text())
                if manifest.get("version") == 2 and manifest.get("bytes") == path.stat().st_size:
                    continue
            except (OSError, ValueError):
                pass  # Regenerate an interrupted or invalid fixture manifest.
        if directory_bytes(args.fixtures) + frames * 4 > max_disk:
            parser.error("Fixture generation would exceed disk limit")
        with tempfile.TemporaryDirectory(prefix="cupuacu-fixture-") as root:
            generated = run_child(binaries[("core", "timing")], {"generate": True, "fixture": str(path), "frames": frames, "format": fmt}, root,
                                  args.timeout_seconds, max_rss, max_disk)
        if generated["status"] != "ok":
            fixture_failures.append(generated)
        else:
            atomic_json(marker, {"version": 2, "bytes": path.stat().st_size})
    if fixture_failures:
        atomic_json(args.output, {"schema_version": SCHEMA, "fixture_failures": fixture_failures})
        return 1
    report = {"schema_version": SCHEMA, "fixture_version": 2, "profile": args.profile,
        "host": {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
                 "node": platform.node(), "processor": platform.processor()},
        "fixture_generation_seconds": time.monotonic() - fixture_started,
        "os_cache_state": "uncontrolled", "isolation": "spawn" if os.name == "nt" else "fork_before_sdl_and_documents", "selected_suites": suites,
        "runs": [], "limits": {"max_rss_bytes": max_rss, "max_disk_bytes": max_disk, "timeout_seconds": args.timeout_seconds}}
    started = time.monotonic()
    for index, case in enumerate(selected):
        count = repetitions if case["mode"] == "timing" else 1
        print(f"[{index + 1}/{len(selected)}] {case['suite']}/{case['mode']} {case['scenario']} {case['frames'] * 8 / 1048576:g} MiB", flush=True)
        for repetition in range(count):
            with tempfile.TemporaryDirectory(prefix="cupuacu-benchmark-") as root:
                query = dict(case, root=root, timeout_seconds=int(args.timeout_seconds),
                             fixture=str(args.fixtures.resolve() / f"v2-{case['frames']}.{case['format']}"))
                data = run_child(binaries[(case["suite"], case["mode"])], query, root,
                                 args.timeout_seconds, max_rss, max_disk)
            data["case"] = case
            data["repetition"] = repetition
            report["runs"].append(data)
            report["elapsed_seconds"] = time.monotonic() - started
            atomic_json(args.output, report)  # interrupted runs retain completed samples
            if data["status"] != "ok":
                print(f"  {data['status']}: {data.get('error')}", file=sys.stderr)
    report["summaries"] = summarize(report["runs"])
    report["scaling"] = scaling(report["summaries"])
    report["target_duration_exceeded"] = args.profile == "quick" and report["elapsed_seconds"] > 60
    if args.compare:
        report["comparison"] = compare(baseline, report)
    atomic_json(args.output, report)
    args.output.with_suffix(".md").write_text(text_report(report))
    print(f"Saved {args.output}; elapsed {report['elapsed_seconds']:.1f}s", flush=True)
    return int(any(r["status"] != "ok" for r in report["runs"]))


if __name__ == "__main__":
    sys.exit(main())
