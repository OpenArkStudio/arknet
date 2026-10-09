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

    def test_independent_batch_and_connection_dimensions(self):
        self.assertEqual(runner.traffic_profiles(), [{"clients": 1, "window": 1}, {"clients": 16, "window": 16}])
        profiles = runner.traffic_profiles([1, 16], [1, 4, 16, 64])
        self.assertEqual(len(profiles), 8)
        self.assertIn({"clients": 1, "window": 64}, profiles)
        self.assertIn({"clients": 16, "window": 1}, profiles)

    def test_execution_and_backend_labels_are_verified(self):
        expected = {"protocol": "tcp", "payload_bytes": 64, "clients": 1, "window": 1,
                    "io_model": "shared", "io_threads": 1, "handler_work": 0,
                    "execution": "callback", "backend": "standalone"}
        metric = {**expected, "implementation": "asio-callback", "messages": 10, "elapsed_seconds": 1, "ok": True}
        runner.validate_metrics(metric, expected)
        for change in ({"implementation": "asio-coroutine"}, {"backend": "boost"}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                runner.validate_metrics({**metric, **change}, expected)

    def test_duplicate_protocols_are_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            completed = subprocess.run(
                [sys.executable, str(Path(__file__).with_name("run.py")), "--executable", sys.executable,
                 "--certs", directory, "--output", str(Path(directory) / "result.json"),
                 "--protocols", "tcp", "tcp"], capture_output=True, text=True, timeout=10)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("duplicate dimension", completed.stderr)
            self.assertFalse((Path(directory) / "result.json").exists())

    def test_keep_going_retains_all_failures_and_nonzero_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            completed = subprocess.run(
                [sys.executable, str(Path(__file__).with_name("run.py")), "--executable", sys.executable,
                 "--certs", directory, "--output", str(output), "--protocols", "udp",
                 "--clients", "16", "--windows", "1", "4", "--payloads", "64", "--io-models", "shared",
                 "--io-threads", "1", "--seconds", "0.001", "--warmup", "0", "--keep-going"],
                capture_output=True, text=True, timeout=10)
            self.assertNotEqual(completed.returncode, 0)
            value = json.loads(output.read_text())
            self.assertTrue(value["complete"])
            self.assertEqual(value["failed_measurements"], 6)
            self.assertEqual(len(value["results"]), 6)
            self.assertEqual({entry["window"] for entry in value["results"]}, {1, 4})
            self.assertIsNone(value["settings"]["udp_throughput_window"])

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
