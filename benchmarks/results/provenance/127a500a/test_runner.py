import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("benchmark_runner", Path(__file__).with_name("run.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ReportValidation(unittest.TestCase):
    def test_source_hash_tracks_code_without_including_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("project(test)\n")
            (root / "vcpkg.json").write_text("{}\n")
            (root / "include").mkdir()
            source = root / "include" / "sample.hpp"
            source.write_text("int value = 1;\n")
            first = runner.source_hash(root)
            results = root / "benchmarks" / "results"
            results.mkdir(parents=True)
            (results / "baseline.txt").write_text("a result\n")
            self.assertEqual(first, runner.source_hash(root))
            source.write_text("int value = 2;\n")
            self.assertNotEqual(first, runner.source_hash(root))

    def test_failed_or_empty_measurement_is_not_accepted(self):
        expected = {"protocol": "tcp", "payload_bytes": 64, "clients": 1, "window": 1,
                    "io_model": "shared", "io_threads": 4, "handler_work": 0}
        valid = {**expected, "ok": True, "messages": 100, "elapsed_seconds": 1}
        runner.validate_metrics(valid, expected)
        for changed in ({"ok": False}, {"messages": 0}, {"elapsed_seconds": 0}, {"window": 16},
                        {"io_model": "sharded"}, {"io_threads": 1}, {"handler_work": 1000}):
            with self.subTest(changed=changed), self.assertRaises(RuntimeError):
                runner.validate_metrics({**valid, **changed}, expected)

    def test_source_hash_tracks_dependency_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("project(test)\n")
            (root / "vcpkg.json").write_text("{}\n")
            for relative in ("vcpkg-configuration.json", "ports/asio/vcpkg.json",
                             "ports/asio/portfile.cmake"):
                with self.subTest(path=relative):
                    previous = runner.source_hash(root)
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("first\n")
                    created = runner.source_hash(root)
                    self.assertNotEqual(previous, created)
                    path.write_text("changed\n")
                    self.assertNotEqual(created, runner.source_hash(root))

    def test_nonfinite_metrics_are_rejected(self):
        for value in ("NaN", "Infinity", "1e309"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                json.loads(value, parse_float=runner.finite_json_number,
                           parse_constant=runner.finite_json_number)

    def test_child_failure_keeps_partial_report(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            completed = subprocess.run(
                [sys.executable, str(Path(__file__).with_name("run.py")),
                 "--executable", sys.executable, "--certs", directory,
                 "--output", str(output), "--protocols", "tcp"],
                capture_output=True, text=True, timeout=10)
            self.assertNotEqual(completed.returncode, 0)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertFalse(report["complete"])
            self.assertEqual(len(report["results"]), 1)
            self.assertNotEqual(report["results"][0]["returncode"], 0)
            self.assertIn("error", report["results"][0])


if __name__ == "__main__":
    unittest.main()
