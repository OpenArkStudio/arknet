import copy
import importlib.util
import itertools
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import rudp_report


class RUDPReportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.directory = Path(self.folder.name)
        settings = dict(rounds=3, clients=8, seconds=1, warmup=.25, payloads=[1024],
                        windows=[1, 4], profiles=["shared:1", "shared:4", "sharded:4"])
        host = dict(system="Darwin", release="27.0.0", architecture="arm64", logical_cpus=14,
                    model="Mac16,11", cpu="Apple M4 Pro", memory_bytes=48 * 1073741824,
                    os_version="27.0.1", os_build="26A434")
        for backend in ("standalone", "boost"):
            names = []
            for window, profile in itertools.product(settings["windows"], settings["profiles"]):
                model, threads = profile.split(":")
                name = f"{backend}-payload1024-window{window}-{model}{threads}.json"
                names.append(name)
                rounds = []
                for number, sample in enumerate((10, 30, 20), 1):
                    item = {field: sample for field in rudp_report.FIELDS}
                    item.update(round=number, ok=True, failures=0, pending_completions=0,
                                messages=100, messages_sent=100, server_received_messages=100,
                                elapsed_seconds=1, udp_server_receive_buffer_bytes=4194304,
                                udp_client_receive_buffer_min_bytes=4194304,
                                udp_client_receive_buffer_max_bytes=4194304)
                    rounds.append(item)
                value = dict(protocol="rudp", wire_protocol="kcp", api="callback", backend=backend,
                             ok=True, payload_bytes=1024, clients=8, window=window,
                             io_model=model, io_threads=int(threads), requested_seconds=1,
                             warmup_seconds=.25, round_count=3, rounds=rounds, peak_rss_bytes=4096,
                             arq_mtu_bytes=1400, arq_send_window_segments=128,
                             arq_receive_window_segments=128, arq_update_interval_ms=10,
                             arq_congestion_control=False, cpu_percent_per_logical_core=100,
                             udp_receive_buffer_requested_bytes=4194304,
                             provenance=dict(returncode=0, source_sha256="same-source",
                                             binary_sha256=f"{backend}-binary"))
                self.write(name, value)
            meta = dict(complete=True, protocol="rudp", wire_protocol="kcp", api="callback",
                        backend=backend, backend_version="1.38.2" if backend == "standalone" else "1.90.0",
                        build_type="Release", cxx_standard=20, source_sha256="same-source",
                        binary_sha256=f"{backend}-binary", compiler_version="Test compiler 1.0",
                        compiler_flags="-O3 -DNDEBUG", started_at_utc="2026-10-12T10:00:00Z",
                        finished_at_utc="2026-10-12T10:05:00Z", settings=settings, host=host, cases=names)
            self.write(f"metadata-{backend}.json", meta)

    def write(self, name, value):
        (self.directory / name).write_text(json.dumps(value), encoding="utf-8")

    def read(self, name):
        return json.loads((self.directory / name).read_text(encoding="utf-8"))

    def test_median_range_counts_and_cpu_share(self):
        metadata, rows = rudp_report.load_results(self.directory)
        self.assertEqual((len(metadata), len(rows)), (2, 12))
        self.assertEqual(rows[0]["roundtrips_per_second"], dict(median=20, min=10, max=30))
        self.assertEqual(rows[0]["messages"], 300)
        self.assertEqual(rows[0]["cpu_machine_percent"]["median"], 20 / 14)
        self.assertEqual(rows[0]["server_receive_buffer_bytes"]["median"], 4194304)

    def test_reject_incomplete_metadata_duplicate_cases_and_path_escape(self):
        name = "metadata-standalone.json"
        original = self.read(name)
        for variant in ("incomplete", "duplicate", "missing", "path"):
            with self.subTest(variant=variant):
                value = copy.deepcopy(original)
                if variant == "incomplete":
                    value["complete"] = False
                elif variant == "duplicate":
                    value["cases"].append(value["cases"][0])
                elif variant == "missing":
                    value["cases"].pop()
                else:
                    value["cases"][0] = "../other.json"
                self.write(name, value)
                with self.assertRaises(ValueError):
                    rudp_report.load_results(self.directory)
        self.write(name, original)

    def test_reject_invalid_metrics_and_incomplete_rounds(self):
        name = "standalone-payload1024-window1-shared1.json"
        original = self.read(name)
        for field, invalid in (("cpu_percent", "20"), ("cpu_percent", True),
                               ("cpu_percent", float("inf")), ("cpu_percent", float("nan")),
                               ("messages", 100.5), ("udp_server_receive_buffer_bytes", 0),
                               ("udp_client_receive_buffer_max_bytes", "4194304"),
                               ("udp_client_receive_buffer_max_bytes", 1),
                               ("pending_completions", 1), ("failures", 1),
                               ("messages_sent", 99), ("rtt_p50_us", 100)):
            with self.subTest(field=field, invalid=invalid):
                value = copy.deepcopy(original)
                value["rounds"][0][field] = invalid
                self.write(name, value)
                with self.assertRaises(ValueError):
                    rudp_report.load_results(self.directory)
        for rounds in (original["rounds"][:-1], [original["rounds"][0]] * 3):
            value = copy.deepcopy(original)
            value["rounds"] = rounds
            self.write(name, value)
            with self.assertRaises(ValueError):
                rudp_report.load_results(self.directory)
        self.write(name, original)

    def test_reject_incompatible_provenance_and_protocol_configuration(self):
        name = "standalone-payload1024-window1-shared1.json"
        original = self.read(name)
        for field, invalid in (("returncode", 1), ("binary_sha256", "other"), ("source_sha256", "other")):
            value = copy.deepcopy(original)
            value["provenance"][field] = invalid
            self.write(name, value)
            with self.assertRaises(ValueError):
                rudp_report.load_results(self.directory)
        for field, invalid in (("api", "coroutine"), ("peak_rss_bytes", True),
                               ("arq_mtu_bytes", 512), ("arq_congestion_control", "false")):
            value = copy.deepcopy(original)
            value[field] = invalid
            self.write(name, value)
            with self.assertRaises(ValueError):
                rudp_report.load_results(self.directory)
        self.write(name, original)

    def test_reject_different_hosts_sources_and_compilers(self):
        name = "metadata-boost.json"
        original = self.read(name)
        for field, invalid in (("source_sha256", "other"), ("compiler_flags", "-O0"),
                               ("compiler_version", "Other compiler"),
                               ("host", dict(original["host"], cpu="Other CPU"))):
            value = copy.deepcopy(original)
            value[field] = invalid
            self.write(name, value)
            with self.assertRaises(ValueError):
                rudp_report.load_results(self.directory)
        self.write(name, original)

    def test_bilingual_docs_keep_measured_conditions_and_limits(self):
        metadata, rows = rudp_report.load_results(self.directory)
        for chinese in (False, True):
            text = rudp_report.documentation(metadata, rows, chinese, self.directory)
            for expected in ("Mac16,11", "Apple M4 Pro", "48 GiB", "macOS 27.0.1",
                             "Standalone Asio 1.38.2", "Boost.Asio 1.90.0", "ARQ",
                             "rudp-throughput.svg", "rudp-rtt.svg", "rudp-cpu.svg",
                             "4 MiB", "cmake -S", "build-perf/benchmarks/arknet_rudp_benchmark"):
                self.assertIn(expected, text)
            self.assertIn("36", text)
            self.assertIn("不是单个 UDP 数据报大小" if chinese else "not the size of one UDP datagram", text)
            self.assertIn("不能据此推断服务端已并行分片" if chinese else "does not demonstrate parallel server sharding", text)
            self.assertIn("没有测量协程 API" if chinese else "No coroutine API was measured", text)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "Matplotlib is optional for offline reports")
    def test_svg_labels_show_models_units_and_real_ranges(self):
        metadata, rows = rudp_report.load_results(self.directory)
        output = self.directory / "chart.svg"
        rudp_report.chart(metadata, rows, output, "cpu_percent", "CPU utilization", "Process CPU (%)")
        root = ET.parse(output).getroot()
        self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
        labels = " ".join("".join(node.itertext()) for node in root.iter("{http://www.w3.org/2000/svg}text"))
        for expected in ("Process CPU (%)", "1 io_context / 1 thread",
                         "1 io_context / 4 threads", "4 io_contexts / 1 thread each",
                         "whiskers = min/max of 3 measured rounds"):
            self.assertIn(expected, labels)


if __name__ == "__main__":
    unittest.main()
