import copy
import json
from pathlib import Path
import tempfile
import unittest

import udp_limits_report


def synthetic_reports():
    # Deliberately independent of collector constants. These are mathematical
    # inputs, not measurements, source revisions or observed kernel events.
    payloads = (1472, 8192, 16356, 16357, 16384, 65507)
    windows = (1, 16, 64)
    counters = ("client_send_rejections", "client_send_completion_errors",
                "server_send_rejections", "server_send_completion_errors",
                "content_errors", "order_errors", "unexpected_messages", "disconnect_errors",
                "udp_lost_messages", "unfinished_messages", "callback_exceptions",
                "drain_timeouts", "duplicate_completions", "pending_completions", "warmup_errors")
    reports = {}
    for backend in ("standalone", "boost"):
        value = dict(schema_version=2, complete=True, backend=backend,
                     source_sha256="synthetic-input-not-a-measured-source",
                     host={"cpu": "synthetic test input"},
                     settings=dict(protocols=["udp"], payloads=list(payloads), repetitions=3,
                                   profiles=[dict(clients=16, window=window) for window in windows],
                                   io_profiles=[dict(io_model="shared", io_threads=1, handler_work=0)],
                                   seconds=1), failed_measurements=18, results=[])
        for index, payload in enumerate(payloads):
            for window in windows:
                lost = (660 if index < 3 else 661) if window == 64 else 0
                for repetition in (1, 2, 3):
                    identity = dict(protocol="udp", clients=16, io_model="shared", io_threads=1,
                                    handler_work=0, backend=backend, payload_bytes=payload, window=window)
                    metric = dict(identity, **{field: lost if field == "udp_lost_messages" else 0
                                               for field in counters}, ok=lost == 0, warmup_ok=True)
                    value["results"].append(dict(identity, repetition=repetition,
                                                 returncode=1 if lost else 0, metrics=metric))
        reports[backend] = value
    return reports


def write_inputs(destination, reports):
    for backend, value in reports.items():
        (destination / f"{backend}.json").write_text(json.dumps(value), encoding="utf-8")
        for phase in ("before", "after"):
            drops = 0 if phase == "before" else 23778
            (destination / f"{backend}-{phase}-netstat-udp.txt").write_text(
                f"{drops} dropped due to full socket buffers\n", encoding="utf-8")
            (destination / f"{backend}-{phase}-netstat-ip.txt").write_text(
                "0 fragment received\n0 reassembled ok\n0 output datagram fragmented\n0 fragment created\n",
                encoding="utf-8")


class UdpReportChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reports = synthetic_reports()

    def changed_report(self, change):
        reports = copy.deepcopy(self.reports)
        change(reports["standalone"])
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder)
            write_inputs(destination, reports)
            return udp_limits_report.collect(destination)

    def test_complete_synthetic_108_records_and_kernel_deltas(self):
        summary = self.changed_report(lambda value: None)
        self.assertEqual(summary["measurements"], 108)
        self.assertEqual(summary["address_family"], "ipv4")
        self.assertEqual(len(summary["profiles"]), 36)
        for backend in udp_limits_report.BACKENDS:
            profiles = [row for row in summary["profiles"] if row["backend"] == backend]
            self.assertEqual(sum(row["passed_repetitions"] for row in profiles), 36)
            self.assertEqual(sum(3 - row["passed_repetitions"] for row in profiles), 18)
            self.assertEqual(sum(repetition["counters"]["udp_lost_messages"]
                                 for row in profiles for repetition in row["repetitions"]), 11889)
            self.assertTrue(all(row["lost_echoes"]["min"] == row["lost_echoes"]["max"] for row in profiles))
            self.assertEqual(summary["kernel"][backend]["delta"]["full_socket_buffer_drops"], 23778)
            self.assertTrue(all(summary["kernel"][backend]["delta"][field] == 0
                                for field in udp_limits_report.IP_FIELDS))

    def test_missing_or_duplicate_measurements_are_rejected(self):
        for duplicate in (False, True):
            with self.subTest(duplicate=duplicate):
                def change(value):
                    if duplicate:
                        value["results"].append(copy.deepcopy(value["results"][0]))
                    else:
                        value["results"].pop()
                with self.assertRaisesRegex(ValueError, "duplicate|incomplete"):
                    self.changed_report(change)

    def test_invalid_counters_are_rejected(self):
        for counter in (-1, None, .5, True, "one", float("inf")):
            with self.subTest(counter=counter):
                def change(value):
                    value["results"][0]["metrics"]["udp_lost_messages"] = counter
                with self.assertRaises(ValueError):
                    self.changed_report(change)

    def test_metric_parameters_and_report_failure_count_are_checked(self):
        for change in (lambda value: value["results"][0]["metrics"].update(window=99),
                       lambda value: value.update(failed_measurements=0)):
            with self.subTest(change=change):
                with self.assertRaisesRegex(ValueError, "parameters differ|failure count"):
                    self.changed_report(change)

    def test_cross_backend_source_host_and_settings_are_checked(self):
        for change in (lambda value: value.update(source_sha256="different-source"),
                       lambda value: value["host"].update(cpu="different CPU"),
                       lambda value: value["settings"].update(seconds=2)):
            with self.subTest(change=change):
                with self.assertRaisesRegex(ValueError, "different sources, hosts or settings"):
                    self.changed_report(change)

    def test_legacy_and_explicit_ipv4_are_compatible(self):
        def change(value):
            value["address_family"] = "ipv4"
            value["settings"]["address_family"] = "ipv4"
            value["results"][0]["address_family"] = "ipv4"
            value["results"][0]["metrics"]["address_family"] = "ipv4"
        summary = self.changed_report(change)
        self.assertEqual(summary["address_family"], "ipv4")
        self.assertEqual(summary["measurements"], 108)

    def test_ipv6_in_metadata_case_or_metrics_is_rejected(self):
        for location in ("metadata", "settings", "case", "metrics"):
            with self.subTest(location=location):
                def change(value):
                    target = {"metadata": value, "settings": value["settings"],
                              "case": value["results"][0], "metrics": value["results"][0]["metrics"]}[location]
                    target["address_family"] = "ipv6"
                with self.assertRaisesRegex(ValueError, "requires IPv4 data"):
                    self.changed_report(change)


if __name__ == "__main__":
    unittest.main()
