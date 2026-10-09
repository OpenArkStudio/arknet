import copy
import importlib.util
from itertools import product
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import dimensions
import report


def matrix_reports():
    values = []
    for backend, name in product(dimensions.BACKENDS, dimensions.NAMES):
        native = name == "native-tcp"
        works = (0, 10000) if native else (0,)
        executions = ("callback", "coroutine") if native else (None,)
        settings = dict(seconds=1, warmup=.25, repetitions=3, payloads=list(dimensions.PAYLOADS),
                        profiles=[dict(clients=16, window=window) for window in dimensions.WINDOWS],
                        protocols=["tcp" if native else name], execution_modes=list(executions) if native else None,
                        udp_throughput_window=None,
                        io_profiles=[dict(io_model=model, io_threads=threads, handler_work=work)
                                     for (model, threads), work in product(dimensions.TOPOLOGIES, works)])
        value = dict(schema_version=2, backend=backend, complete=True, source_sha256="same-source",
                     executable_sha256="test-executable", host={"system": "test", "logical_cpus": 14}, settings=settings,
                     results=[], failed_measurements=0)
        for payload, window, (model, threads), work, execution, repetition in product(
                dimensions.PAYLOADS, dimensions.WINDOWS, dimensions.TOPOLOGIES, works, executions, (1, 2, 3)):
            entry = dict(protocol="tcp" if native else name, payload_bytes=payload, clients=16, window=window,
                         io_model=model, io_threads=threads, handler_work=work, repetition=repetition, returncode=0)
            if execution:
                entry["execution"] = execution
            metric = dict(entry, backend=backend, implementation="asio-" + execution if native else "arknet-callback",
                          ok=True, messages=100, elapsed_seconds=1)
            if native:
                metric["latency_mode"] = "message" if window == 1 else "batch"
                metric.update(total_cpu_seconds=1, warmup_seconds=.25)
            else:
                metric["cpu_seconds"] = .8
            metric.update({field: 100 for field in report.METRICS})
            entry["metrics"] = metric
            value["results"].append(entry)
        values.append(value)
    return values


class DimensionsTests(unittest.TestCase):
    def setUp(self):
        self.values = matrix_reports()

    def load(self, values=None):
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for index, value in enumerate(self.values if values is None else values):
                path = Path(folder) / f"{index}.json"
                path.write_text(json.dumps(value), encoding="utf-8")
                paths.append(path)
            return dimensions.load_reports(paths)

    def test_complete_requested_cartesian_matrix(self):
        reports, groups, failed = self.load()
        self.assertEqual(sum(len(value["results"]) for _, value in reports), 2376)
        self.assertEqual(len(groups), 792)
        self.assertEqual(failed, [])
        self.assertEqual(len(dimensions.paired_ratios(groups)), 144)

    def test_reject_missing_configuration_repetition_protocol_and_duplicate(self):
        for change in ("configuration", "repetition", "protocol", "duplicate", "repeated-file"):
            with self.subTest(change=change):
                values = copy.deepcopy(self.values)
                if change == "configuration":
                    config = dimensions.configuration(values[0]["results"][0])
                    values[0]["results"] = [entry for entry in values[0]["results"]
                                           if dimensions.configuration(entry) != config]
                elif change == "repetition":
                    values[0]["results"].pop()
                elif change == "protocol":
                    values.pop(0)
                elif change == "duplicate":
                    values[0]["results"].append(copy.deepcopy(values[0]["results"][0]))
                else:
                    values.append(copy.deepcopy(values[0]))
                with self.assertRaises(ValueError):
                    self.load(values)

    def test_reject_mixed_provenance_and_duration(self):
        for field, replacement in (("source_sha256", "other-source"), ("host", {"system": "other"}),
                                   ("seconds", 2), ("warmup", .5)):
            with self.subTest(field=field):
                if field in ("seconds", "warmup"):
                    self.values[0]["settings"][field] = replacement
                else:
                    self.values[0][field] = replacement
                with self.assertRaises(ValueError):
                    self.load()
                self.values = matrix_reports()

    def test_reject_wrong_requested_dimensions(self):
        for field, value in (("repetitions", 2), ("payloads", [1024]),
                             ("profiles", [dict(clients=16, window=1)]),
                             ("io_profiles", [dict(io_model="shared", io_threads=1, handler_work=0)]),
                             ("seconds", float("inf")), ("warmup", -1)):
            with self.subTest(field=field):
                self.values[0]["settings"][field] = value
                with self.assertRaises(ValueError):
                    self.load()
                self.values = matrix_reports()

    def test_reject_successful_nonfinite_metrics_and_request_mismatch(self):
        for field, value in (("roundtrips_per_second", float("inf")), ("rtt_p99_us", float("nan")),
                             ("payload_mib_per_second", -1), ("messages", 0), ("window", 99), ("backend", "boost")):
            with self.subTest(field=field):
                self.values[0]["results"][0]["metrics"][field] = value
                with self.assertRaises(ValueError):
                    self.load()
                self.values = matrix_reports()

    def test_failed_repeat_excludes_whole_profile_and_preserves_loss(self):
        entry = self.values[1]["results"][0]
        entry["metrics"].update(ok=False, udp_lost_messages=17)
        entry.update(returncode=1, error="UDP echo loss")
        _, groups, failed = self.load()
        self.assertEqual(len(groups), 791)
        self.assertEqual(len(failed), 1)
        self.assertEqual(failed[0]["successful_repetitions"], 2)
        self.assertEqual(failed[0]["failures"][0]["counters"], {"udp_lost_messages": 17})
        self.assertNotIn("roundtrips_per_second", failed[0])
        for chinese in (False, True):
            table = dimensions.failure_table(failed, chinese)
            self.assertIn("UDP 丢失报文: 17" if chinese else "UDP lost messages: 17", table)
            self.assertNotIn("udp_lost_messages", table)

    def test_nonzero_counter_invalidates_nominal_success(self):
        for index, field in ((0, "content_errors"), (7, "content_or_io_errors")):
            with self.subTest(field=field):
                self.values[index]["results"][0]["metrics"][field] = 1
                _, groups, failed = self.load()
                self.assertEqual(len(groups), 791)
                self.assertEqual(failed[0]["failures"][0]["counters"], {field: 1})
                self.values = matrix_reports()

    def test_reject_invalid_failure_counters(self):
        for value in (-1, None, .5, "one", True, float("inf")):
            with self.subTest(value=value):
                self.values[1]["results"][0]["metrics"].update(ok=False, udp_lost_messages=value)
                with self.assertRaises(ValueError):
                    self.load()

    def test_native_latency_mode_matches_window(self):
        for execution, window in product(("callback", "coroutine"), (1, 4)):
            for mode in (None, "batch" if window == 1 else "message"):
                with self.subTest(execution=execution, window=window, mode=mode):
                    entry = next(value for value in self.values[7]["results"]
                                 if value["execution"] == execution and value["window"] == window)
                    if mode is None:
                        entry["metrics"].pop("latency_mode")
                    else:
                        entry["metrics"]["latency_mode"] = mode
                    with self.assertRaisesRegex(ValueError, "native latency mode"):
                        self.load()
                    self.values = matrix_reports()

    def test_failure_table_translates_all_counters_and_records_computation(self):
        self.assertEqual(set(dimensions.COUNTER_LABELS), set(dimensions.COUNTERS))
        entry = next(value for value in self.values[7]["results"] if value["handler_work"] == 10000)
        entry["metrics"].update({field: 1 for field in dimensions.COUNTERS})
        _, _, failed = self.load()
        for chinese in (False, True):
            table = dimensions.failure_table(failed, chinese)
            self.assertIn("10,000", table)
            for field, labels in dimensions.COUNTER_LABELS.items():
                self.assertIn(labels[int(chinese)] + ": 1", table)
                self.assertNotIn(field, table)

    def test_all_failed_report_uses_explicit_backend(self):
        for entry in self.values[0]["results"]:
            entry.pop("metrics")
            entry.update(returncode=None, error="process timeout")
        _, groups, failed = self.load()
        self.assertEqual(len(groups), 756)
        self.assertEqual(len(failed), 36)
        self.assertTrue(all(row["backend"] == "standalone" for row in failed))

    def test_native_ratios_pair_repetitions_and_exclude_failed_partner(self):
        native = self.values[7]
        target = native["results"][0]
        config = {field: target[field] for field in ("payload_bytes", "window", "io_model", "io_threads", "handler_work")}
        matching = [entry for entry in native["results"] if all(entry[field] == value for field, value in config.items())]
        for entry in matching:
            rates = (10, 100, 20) if entry["execution"] == "callback" else (20, 100, 80)
            entry["metrics"]["roundtrips_per_second"] = rates[entry["repetition"] - 1]
            entry["metrics"]["total_cpu_seconds"] = rates[entry["repetition"] - 1] / 100 * 1.25
        _, groups, _ = self.load()
        ratio = next(row for row in dimensions.paired_ratios(groups)
                     if row["backend"] == "standalone" and all(row[field] == value for field, value in config.items()))
        self.assertEqual(ratio["coroutine_callback_ratio"], {"median": 2, "min": 1, "max": 4})
        self.assertEqual(ratio["coroutine_callback_cpu_ratio"], {"median": 2, "min": 1, "max": 4})
        self.assertTrue(ratio["cpu_percent_estimated"])
        matching[0].update(returncode=1, error="failed callback")
        matching[0]["metrics"]["ok"] = False
        _, groups, _ = self.load()
        self.assertEqual(len(dimensions.paired_ratios(groups)), 143)

    def test_missing_cpu_partner_keeps_throughput_but_requires_all_cpu_repeats(self):
        target = self.values[7]["results"][0]
        target["metrics"]["total_cpu_seconds"] = None
        _, groups, failed = self.load()
        self.assertEqual(failed, [])
        self.assertEqual(len(groups), 792)
        key = ("standalone", *dimensions.configuration(target))
        row = next(row for row in report.summary(groups, 14)
                   if tuple(row[field] for field in dimensions.FIELDS) == key)
        self.assertIsNone(row["cpu_percent"])
        self.assertIsNone(row["cpu_machine_percent"])
        ratios = dimensions.paired_ratios(groups)
        self.assertEqual(len(ratios), 144)
        self.assertEqual(sum(row["coroutine_callback_cpu_ratio"] is None for row in ratios), 1)
        self.assertTrue(all(row["coroutine_callback_ratio"] is not None for row in ratios))
        groups[key].pop()
        with self.assertRaisesRegex(ValueError, "all paired repetitions"):
            dimensions.paired_ratios(groups)

    def test_zero_cpu_callback_does_not_produce_an_infinite_cpu_ratio(self):
        target = self.values[7]["results"][0]
        target["metrics"]["total_cpu_seconds"] = 0
        _, groups, _ = self.load()
        ratios = dimensions.paired_ratios(groups)
        self.assertEqual(sum(row["coroutine_callback_cpu_ratio"] is None for row in ratios), 1)

    def test_address_families_cannot_be_mixed_with_legacy_ipv4(self):
        self.values[7]["results"][0]["metrics"]["address_family"] = "ipv6"
        with self.assertRaisesRegex(ValueError, "address families"):
            self.load()
        for value in self.values:
            for entry in value["results"]:
                entry["metrics"]["address_family"] = "ipv6"
        reports, groups, failed = self.load()
        page = dimensions.index_page(reports, report.summary(groups), failed, False, "https://example.test/raw")
        self.assertIn("IPv6", page)

    def test_bilingual_pages_have_anchors_svg_and_no_large_tables(self):
        reports, groups, failed = self.load()
        rows, ratios = report.summary(groups), dimensions.paired_ratios(groups)
        for chinese in (False, True):
            page = dimensions.index_page(reports, rows, failed, chinese, "https://example.test/raw")
            self.assertIn("IPv4", page)
            self.assertIn("(dimensions-summary.json ':ignore')", page)
            self.assertNotIn("(dimensions-summary.json)", page)
            self.assertFalse(page.splitlines()[2].startswith("["))
            for name in dimensions.NAMES:
                self.assertIn(f'id="{name}"', page)
                detail = dimensions.detail_page(name, rows, failed, ratios, chinese, 14)
                self.assertIn("(dimensions-summary.json ':ignore')", detail)
                self.assertNotIn("(dimensions-summary.json)", detail)
                self.assertIn(".svg)", detail)
                self.assertNotIn(".png", detail)
                self.assertNotIn("shared:", detail)
                self.assertNotIn("work=", detail)
                self.assertNotIn("[English]", detail)
                self.assertNotIn("[中文]", detail)
                self.assertFalse(detail.splitlines()[2].startswith("["))
            native = dimensions.detail_page("native-tcp", rows, failed, ratios, chinese, 14)
            self.assertIn("批次 RTT" if chinese else "batch RTT", native)
            self.assertIn("整批回写" if chinese else "writes the entire reply", native)
            self.assertIn("TODO", native)
            self.assertIn("-cpu.svg)", native)
            self.assertIn("-cpu-ratio.svg)", native)
            self.assertIn("CPU% / 14", native)
            self.assertIn("7.14", native)
            self.assertIn("N/A", native)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "SVG rendering requires Matplotlib")
    def test_svg_has_real_text_and_no_embedded_bitmap(self):
        _, groups, failed = self.load()
        with tempfile.TemporaryDirectory() as folder:
            cases = ((report.summary(groups), "tcp", "rtt_p99_us"),
                     ([], "tcp", "rtt_p99_us"), ([], "native-tcp", "coroutine_callback_ratio"),
                     (report.summary(groups, 14), "native-tcp", "cpu_percent"),
                     (dimensions.paired_ratios(groups), "native-tcp", "coroutine_callback_cpu_ratio"),
                     ([], "native-tcp", "cpu_percent"), ([], "native-tcp", "coroutine_callback_cpu_ratio"))
            for rows, name, metric in cases:
                with self.subTest(name=name, metric=metric, valid=bool(rows)):
                    path = Path(folder) / f"dimensions-{name}-{bool(rows)}.svg"
                    dimensions.chart(rows, failed, path, name, metric)
                    svg = ET.parse(path).getroot()
                    self.assertTrue(svg.findall(".//{http://www.w3.org/2000/svg}text"))
                    self.assertFalse(svg.findall(".//{http://www.w3.org/2000/svg}image"))
                    if not rows:
                        self.assertIn("CPU N/A" if "cpu" in metric else "No valid measurements", path.read_text(encoding="utf-8"))
                    elif "cpu" in metric:
                        self.assertIn("CPU estimated", path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
