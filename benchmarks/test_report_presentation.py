import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

import dimensions
import report
import report_presentation as presentation
import test_dimensions
import test_report


def protected_content(markdown):
    """Compare evidence rather than prose wording or component syntax."""
    return {
        "headings": re.findall(r"^#{1,6} .*", markdown, re.MULTILINE),
        "tables": re.findall(r"^\|.*", markdown, re.MULTILINE),
        "charts": re.findall(r"!\[[^\]]*\]\(([^)]+)\)", markdown),
        "code": re.findall(r"^```[^\n]*\n[\s\S]*?^```", markdown, re.MULTILINE),
        "anchors": re.findall(r'<a id="[^"]+"></a>', markdown),
        "numbers": re.findall(r"\d+(?:[,.]\d+)*", markdown),
    }


class PresentationTests(unittest.TestCase):
    def setUp(self):
        self.baseline = test_report.ReportTests()
        self.matrix = test_dimensions.DimensionsTests()
        self.matrix.setUp()

    def assert_presentation(self, module, function, kind, chinese, *args):
        # Invoke the real generator with/without its final presentation stage.
        with patch.object(module, "format_report", side_effect=lambda source, *_: source):
            original = function(*args)
        formatted = function(*args)
        self.assertEqual(formatted, presentation.format_report(original, kind, chinese))
        self.assertEqual(protected_content(formatted), protected_content(original))
        self.assertEqual(presentation.format_report(formatted, kind, chinese), formatted)
        self.assertIn('<Aside ', formatted)
        return formatted

    def test_real_bilingual_generator_pages_preserve_evidence_and_are_idempotent(self):
        reports, groups, failed = self.matrix.load()
        rows = report.summary(groups, 14)
        ratios = dimensions.paired_ratios(groups)
        basic_reports, basic_groups = self.baseline.load(self.baseline.fixture())
        for chinese in (False, True):
            for name in (*report.PROTOCOLS, "coroutine"):
                subset = [row for row in rows if (row["implementation"] == "asio-coroutine" if name == "coroutine"
                          else row["implementation"] == "arknet-callback" and row["protocol"] == name)]
                page = self.assert_presentation(report, report.protocol_page, "protocol", chinese,
                                                name, subset, chinese, 14)
                self.assertIn("<details>", page)
                # All charts remain outside exact-value disclosures.
                for body in re.findall(r"<details>[\s\S]*?</details>", page):
                    self.assertNotIn("![", body)
                    self.assertIn("|", body)
                self.assertNotIn("Backend /", page)
                self.assertNotIn("后端", page)
                self.assertIn("methodology", page)
            overview = self.assert_presentation(report, report.overview, "overview", chinese,
                                                 basic_reports, report.summary(basic_groups, 14), chinese,
                                                 None, 'compiler <version> & -O3')
            self.assertIn('<CardGrid>', overview)
            self.assertIn('compiler <version> & -O3', overview)
            self.assertNotIn("Asio backends", overview)
            self.assertNotIn("后端", overview)
            for name in dimensions.NAMES:
                self.assert_presentation(dimensions, dimensions.detail_page, "dimension", chinese,
                                         name, rows, failed, ratios, chinese, 14)
            self.assert_presentation(dimensions, dimensions.index_page, "dimension-index", chinese,
                                     reports, rows, failed, chinese, None)

    def test_empty_failed_and_missing_cpu_results_keep_their_meaning(self):
        reports, groups, failed = self.matrix.load()
        for chinese in (False, True):
            empty = self.assert_presentation(report, report.protocol_page, "protocol", chinese,
                                              "tcp", [], chinese, None)
            self.assertNotIn("<details>", empty)
            self.assertIn("N/A", empty)
            no_rows = self.assert_presentation(dimensions, dimensions.detail_page, "dimension", chinese,
                                               "native-tcp", [], [], [], chinese, None)
            self.assertIn("TODO", no_rows)
            self.assertIn("N/A", no_rows)
        # Keep a real failed fixture row, including original error counters.
        target = self.matrix.values[1]["results"][0]
        target["returncode"] = 1
        target["metrics"]["ok"] = False
        target["metrics"]["udp_lost_messages"] = 7
        _, groups, failed = self.matrix.load()
        for chinese in (False, True):
            page = self.assert_presentation(dimensions, dimensions.detail_page, "dimension", chinese,
                                             "udp", report.summary(groups), failed,
                                             dimensions.paired_ratios(groups), chinese, None)
            self.assertIn("7", page)
            self.assertIn(dimensions.COUNTER_LABELS["udp_lost_messages"][int(chinese)] + ": 7", page)

    def test_unknown_prose_commands_and_historical_sections_are_opaque(self):
        source = '# Historical report\n\n## Extra observations\n\nMeasured 2.5 us; source `abc123`; --backend stays backend.\n\n- metadata: At recorded host version 7.\n\n'
        source += '```sh\n--backend boost --payload 1024\n```\n\n| CPU% | N/A |\n| --- | --- |\n'
        self.assertEqual(presentation.format_report(source, "protocol", False), source)
        with self.assertRaisesRegex(ValueError, "unknown report kind"):
            presentation.format_report(source, "unsupported", False)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "SVG rendering requires Matplotlib")
    def test_actual_generation_keeps_json_and_svg_bytes_identical(self):
        import matplotlib
        # Deterministic SVG metadata/IDs let this compare actual generated bytes,
        # including native CPU/ratio figures, without publishing fixture outputs.
        with tempfile.TemporaryDirectory() as folder, patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "0"}), \
                matplotlib.rc_context({"svg.hashsalt": "arknet-presentation-test"}):
            root = Path(folder)
            raw = root / 'fixture.json'
            raw.write_text(json.dumps(self.baseline.fixture()), encoding="utf-8")
            for apply in (False, True):
                output = root / ('new' if apply else 'original')
                with contextlib.ExitStack() as stack:
                    if not apply:
                        stack.enter_context(patch.object(report, "format_report", side_effect=lambda source, *_: source))
                        stack.enter_context(patch.object(dimensions, "format_report", side_effect=lambda source, *_: source))
                    stack.enter_context(patch('sys.argv', ['report.py', '--reports', str(raw), '--output-dir',
                                                        str(output / 'baseline'), '--host-description', 'fixture toolchain']))
                    stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                    report.main()
                    reports, groups, failed = self.matrix.load()
                    dimensions.generate(reports, groups, failed, output / 'dimensions', None,
                                        names=('udp', 'native-tcp'))
            original = {str(path.relative_to(root / 'original')): path.read_bytes()
                        for path in (root / 'original').rglob('*') if path.suffix in {'.json', '.svg'}}
            updated = {str(path.relative_to(root / 'new')): path.read_bytes()
                       for path in (root / 'new').rglob('*') if path.suffix in {'.json', '.svg'}}
            self.assertEqual(updated, original)
            self.assertEqual(sum(path.endswith('.svg') for path in original), 19)
            self.assertEqual(sum(path.endswith('.json') for path in original), 2)


if __name__ == "__main__":
    unittest.main()
