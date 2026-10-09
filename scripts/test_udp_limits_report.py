import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest

import udp_limits_report


ARCHIVE = Path(__file__).resolve().parents[1] / "benchmarks/results/macmini-m4pro-20261010-udp-limits"


class UdpReportChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reports = {backend: json.loads((ARCHIVE / f"{backend}.json").read_text())
                       for backend in udp_limits_report.BACKENDS}

    def changed_report(self, change):
        reports = copy.deepcopy(self.reports)
        change(reports["standalone"])
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder)
            for backend, value in reports.items():
                (destination / f"{backend}.json").write_text(json.dumps(value), encoding="utf-8")
                for phase in ("before", "after"):
                    for protocol in ("udp", "ip"):
                        name = f"{backend}-{phase}-netstat-{protocol}.txt"
                        shutil.copyfile(ARCHIVE / name, destination / name)
            return udp_limits_report.collect(destination)

    def test_complete_archived_108_measurements_and_kernel_evidence(self):
        summary = udp_limits_report.collect(ARCHIVE)
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
