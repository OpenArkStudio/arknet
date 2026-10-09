import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import report


class ReportTests(unittest.TestCase):
    def fixture(self):
        metric = {field: 10 for field in report.METRICS}
        metric.update(backend="standalone", protocol="tcp", payload_bytes=1024,
                      clients=16, window=16, io_model="shared", io_threads=1, handler_work=0, ok=True)
        return dict(complete=True, source_sha256="same-source", host={"system": "test"},
                    settings={"repetitions": 3, "seconds": 1, "warmup": .25},
                    results=[dict(repetition=index, returncode=0, metrics=copy.deepcopy(metric)) for index in (1, 2, 3)])

    def load(self, *values):
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for index, value in enumerate(values):
                path = Path(folder) / f"{index}.json"
                path.write_text(json.dumps(value), encoding="utf-8")
                paths.append(path)
            return report.load_reports(paths)

    def test_median_and_range(self):
        value = self.fixture()
        for entry, number in zip(value["results"], (10, 100, 20)):
            entry["metrics"]["roundtrips_per_second"] = number
        _, groups = self.load(value)
        self.assertEqual(report.summary(groups)[0]["roundtrips_per_second"], {"median": 20, "min": 10, "max": 100})

    def test_reject_partial_duplicate_failed_and_nonfinite(self):
        for change in ("partial", "duplicate", "failed", "nonfinite", "missing"):
            with self.subTest(change=change):
                value = self.fixture()
                if change == "partial": value["complete"] = False
                elif change == "duplicate": value["results"][1]["repetition"] = 1
                elif change == "failed": value["results"][1]["metrics"]["ok"] = False
                elif change == "nonfinite": value["results"][1]["metrics"]["rtt_p99_us"] = float("inf")
                else: value["results"].pop()
                with self.assertRaises(ValueError): self.load(value)

    def test_reject_incompatible_sources_hosts_and_durations(self):
        for change in ("source_sha256", "host", "settings"):
            with self.subTest(change=change):
                first, second = self.fixture(), self.fixture()
                second[change] = {"source_sha256": "other", "host": {"system": "other"},
                                  "settings": {"repetitions": 3, "seconds": 2, "warmup": .25}}[change]
                with self.assertRaises(ValueError): self.load(first, second)

    def test_reject_repeated_input_file(self):
        with self.assertRaises(ValueError): self.load(self.fixture(), self.fixture())

    def test_machine_table_uses_collected_values_and_explicit_missing_fields(self):
        host = dict(system="Darwin", release="27.0.0", os_version="27.0.1", os_build="26A434", model="Mac16,11",
                    cpu="Apple M4 Pro", machine="arm64", physical_cpus=14, logical_cpus=14,
                    memory_bytes=48 * 1073741824)
        for chinese in (False, True):
            text = report.host_table(host, chinese)
            for value in ("Mac16,11", "Apple M4 Pro", "arm64", "14 / 14", "48 GiB", "27.0.1 (26A434)"):
                self.assertIn(value, text)
            self.assertIn("macOS 27.0.1", text)
            self.assertIn("27.0.0", text)
        self.assertIn("Not recorded", report.host_table({}, False))

    def test_comparison_ratios_use_matching_backend_and_work(self):
        _, groups = self.load(self.fixture())
        first = report.summary(groups)[0]
        rows = []
        for backend, baseline in (("standalone", 100), ("boost", 1000)):
            for work in (0, 10000):
                for model, threads, rate, latency in (("shared", 1, baseline, 20),
                                                     ("shared", 4, baseline * 2, 30),
                                                     ("sharded", 4, baseline * 3, 10)):
                    row = copy.deepcopy(first)
                    row.update(backend=backend, handler_work=work, io_model=model, io_threads=threads)
                    row["roundtrips_per_second"]["median"] = rate
                    row["rtt_p99_us"]["median"] = latency
                    rows.append(row)
        for chinese in (False, True):
            text = report.comparison_table(rows, chinese)
            for value in ("2.00×", "3.00×", "1.50×", "0.50×", "Standalone Asio", "Boost.Asio", "10,000"):
                self.assertIn(value, text)
            self.assertEqual(text.count("2.00×"), 4)
            self.assertIn("1 个 io_context / 4 个线程" if chinese else "1 context / 4 threads", text)
            self.assertIn("4 个 io_context / 4 个线程" if chinese else "4 contexts / 4 threads", text)
            for line in text.splitlines():
                if line.startswith("|"):
                    self.assertEqual(line.count("|"), 4)

    def test_key_findings_pair_throughput_with_latency_and_same_profile_baseline(self):
        _, groups = self.load(self.fixture())
        baseline = report.summary(groups)[0]
        baseline["roundtrips_per_second"]["median"] = 100
        baseline["rtt_p99_us"]["median"] = 20
        faster = copy.deepcopy(baseline)
        faster.update(io_model="sharded", io_threads=4)
        faster["roundtrips_per_second"]["median"] = 200
        faster["rtt_p99_us"]["median"] = 30
        other_payload = copy.deepcopy(faster)
        other_payload["payload_bytes"] = 64
        other_payload["roundtrips_per_second"]["median"] = 10000
        other_backend = copy.deepcopy(faster)
        other_backend["backend"] = "boost"
        other_backend["roundtrips_per_second"]["median"] = 20000
        for chinese in (False, True):
            text = report.key_findings([baseline, faster, other_payload, other_backend], chinese)
            for value in ("Standalone Asio", "2.00×", "30.00 us", "20.00 us"):
                self.assertIn(value, text)
            self.assertNotIn("10,000", text)
            self.assertNotIn("20,000", text)
            self.assertNotIn("Boost.Asio", text)
        self.assertEqual(report.key_findings([], False), "")

    def test_bilingual_pages_use_full_model_names_and_svg(self):
        _, groups = self.load(self.fixture())
        rows = report.summary(groups)
        for chinese in (False, True):
            page = report.protocol_page("tcp", rows, chinese)
            table = report.table(rows, chinese)
            self.assertIn("assets/tcp-work0-throughput.svg", page)
            self.assertIn("assets/tcp-work0-latency.svg", page)
            self.assertIn("1 个 io_context / 1 个线程" if chinese else "1 context / 1 thread", table)
            self.assertIn("无额外业务计算" if chinese else "No added computation", page)
            for text in (page, table, report.comparison_table(rows, chinese)):
                for obsolete in (".png", "work=", "shared:", "sharded:", "夹具", "fixture"):
                    self.assertNotIn(obsolete, text)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "report rendering requires Matplotlib")
    def test_charts_are_vector_svg_with_text_and_no_embedded_bitmap(self):
        _, groups = self.load(self.fixture())
        rows = report.summary(groups)
        with tempfile.TemporaryDirectory() as folder:
            for name, render in (("protocol", lambda output: report.chart(rows, output, "roundtrips_per_second", "TCP", 0)),
                                 ("overview", lambda output: report.overview_chart(rows, output))):
                path = Path(folder) / f"{name}.svg"
                render(path)
                svg = ET.parse(path).getroot()
                self.assertEqual(svg.tag, "{http://www.w3.org/2000/svg}svg")
                text = svg.findall(".//{http://www.w3.org/2000/svg}text")
                self.assertTrue(text)
                self.assertTrue(any("1 context" in "".join(element.itertext()) for element in text))
                self.assertFalse(svg.findall(".//{http://www.w3.org/2000/svg}image"))


if __name__ == "__main__":
    unittest.main()
