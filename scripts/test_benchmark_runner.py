#!/usr/bin/env python3
"""Unit tests for reporting and subprocess failure handling (no audio hardware)."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("bench_runner", Path(__file__).with_name("run-benchmarks.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

CASE = dict(suite="core", mode="timing", scenario="delete", frames=131072,
            format="wav", position="begin", history_depth=1)


def report(value=2):
    row = dict(case=CASE, status="ok", milestones_ms={"background_complete": value, "view_ready": None},
               environment=dict(dependencies="abc", cpu_count=4, system_ram_mib=8192,
                                compiler="clang", configuration="Release", renderer="none"))
    return dict(schema_version=1, fixture_version=2, host={"system": "test"}, runs=[row], summaries=runner.summarize([row]))


class ReportingTests(unittest.TestCase):
    def test_comparison_and_missing_scenarios(self):
        a, b = report(2), report(3)
        comparison = runner.compare(a, b)
        self.assertTrue(comparison["compatible"])
        self.assertEqual(comparison["changes"][0]["metrics"]["background_complete_ms"]["ratio"], 1.5)
        b["summaries"] = []
        self.assertEqual(runner.compare(a, b)["changes"][0]["status"], "missing")

    def test_incompatible_environment(self):
        a, b = report(), report()
        b["runs"][0]["environment"]["compiler"] = "different"
        self.assertFalse(runner.compare(a, b)["compatible"])
        b = report(); b["schema_version"] = 99
        self.assertFalse(runner.compare(b, a)["compatible"])

    def test_incomplete_report_is_not_a_baseline(self):
        a, b = report(), report()
        del a["summaries"]
        self.assertFalse(runner.compare(a, b)["compatible"])

    def test_latency_and_query_counts_are_comparable(self):
        row = report()["runs"][0]
        row["event_latency"] = {"max_ms": 80, "p99_ms": None}
        row["waveform_queries"] = {"raw_samples_scanned": 512}
        metrics = runner.summarize([row])[0]["metrics"]
        self.assertEqual(metrics["event_latency_max_ms"]["median"], 80)
        self.assertEqual(metrics["waveform_queries_raw_samples_scanned"]["median"], 512)
        self.assertNotIn("event_latency_p99_ms", metrics)

    def test_missing_measurements_and_failures_stay_visible(self):
        rows = report()["runs"] + [dict(case=CASE, status="timeout")]
        summary = runner.summarize(rows)[0]
        self.assertEqual(summary["failed_repetitions"], 1)
        self.assertNotIn("view_ready_ms", summary["metrics"])
        self.assertIsNone(summary["metrics"]["background_complete_ms"]["stdev"])

    def test_scaling_holds_operation_size_constant(self):
        a, b = report(2), report(8)
        b["runs"][0]["case"] = dict(CASE, frames=524288)
        items = runner.summarize(a["runs"] + b["runs"])
        growth = runner.scaling(items)[0]
        self.assertEqual(growth["frame_ratio"], 4)
        self.assertEqual(growth["ratios"]["background_complete_ms"], 4)

    def test_profiles_and_filters_are_reproducible(self):
        a = list(runner.cases("quick", [1, 4, 16], ["core"]))
        self.assertEqual(a, list(runner.cases("quick", [1, 4, 16], ["core"])))
        self.assertEqual({q["frames"] for q in a}, {131072, 524288, 2097152})
        self.assertIn("waveform_build", {q["scenario"] for q in a})


class ChildTests(unittest.TestCase):
    def test_disk_sampling_tolerates_concurrent_file_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / "existing"
            existing.write_bytes(b"1234")
            listing = SimpleNamespace(rglob=lambda _: [root / "renamed", existing])
            with patch.object(runner, "Path", return_value=listing):
                self.assertEqual(runner.directory_bytes(root), 4)

    def run_fake(self, body, timeout=2, rss=0, max_rss=0):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "fake.py"
            script.write_text("import json,sys,time\nfrom pathlib import Path\n" + body)
            def launch(binary, request, result, log):
                with log.open("w") as output:
                    return subprocess.Popen([runner.sys.executable, str(script), str(request), str(result)], stdout=output, stderr=output)
            with patch.object(runner, "IsolatedProcess", side_effect=launch), patch.object(runner, "resident_bytes", return_value=rss):
                return runner.run_child(script, {}, root, timeout, max_rss, 0)

    def test_validated_success(self):
        result = self.run_fake('Path(sys.argv[2]).write_text(json.dumps({"validated": True}))\n')
        self.assertEqual(result["status"], "ok")

    def test_missing_json_and_nonzero_exit(self):
        self.assertEqual(self.run_fake('sys.exit(3)\n')["status"], "failed")
        self.assertEqual(self.run_fake('Path(sys.argv[2]).write_text("broken")\n')["status"], "failed")

    def test_timeout_and_resource_limit(self):
        self.assertEqual(self.run_fake('time.sleep(5)\n', timeout=.1)["status"], "timeout")
        self.assertEqual(self.run_fake('time.sleep(5)\n', rss=200, max_rss=100)["status"], "memory_limit")

    def test_peak_over_limit_between_samples(self):
        result = self.run_fake('Path(sys.argv[2]).write_text(json.dumps({"validated": True, "peak_process_rss_bytes_including_setup": 200}))\n', max_rss=100)
        self.assertEqual(result["status"], "memory_limit")


if __name__ == "__main__":
    unittest.main()
