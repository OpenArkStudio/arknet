import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { test } from "node:test";

const workflow = await readFile(new URL("../../../.github/workflows/ci.yml", import.meta.url), "utf8");

// These are explicit contracts for this workflow's layout, not a general YAML parser.
function block(source, name) {
  const lines = source.split("\n");
  const start = lines.findIndex(line => line === `${name}:`);
  assert.notEqual(start, -1, `missing ${name}`);
  let end = start + 1;
  while (end < lines.length && (lines[end] === "" || lines[end].startsWith(" "))) end++;
  return lines.slice(start, end).join("\n").trimEnd();
}

function checkPolicy(source) {
  assert.equal(block(source, "on"), [
    "on:",
    "  pull_request:",
    "    branches: [main]",
    "  merge_group:",
    "    types: [checks_requested]",
    "  workflow_dispatch:",
  ].join("\n"));
  assert.equal(block(source, "permissions"), "permissions:\n  contents: read");
  assert.equal(block(source, "concurrency"), [
    "concurrency:",
    "  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}",
    "  cancel-in-progress: false",
  ].join("\n"));

  // Reviewed source 56f8d2537f793b334064f5d4abdcb0d2035c5300: preserve all jobs,
  // including the 12 native rows, names, timeouts, commands, sanitizers and uploads.
  // An intentional job change needs a new review before updating this fingerprint.
  const jobs = source.slice(source.indexOf("jobs:\n"));
  assert.equal(createHash("sha256").update(jobs).digest("hex"),
    "eb419c11f19747d79088065a47815e5178be81fe1daf8a30eb6afab1692ec132",
    "review the complete 14-check job contract before changing it");
}

test("CI validates PRs and merge candidates without cancelling active runs or repeating main pushes", () => {
  checkPolicy(workflow);
});

test("CI policy rejects changed events, cancellation, permissions and required job coverage", () => {
  const mutations = [
    ["on:\n", "on:\n  push:\n    branches: [main]\n"],
    ["  pull_request:\n    branches: [main]\n", ""],
    ["  merge_group:\n    types: [checks_requested]\n", ""],
    ["  workflow_dispatch:\n", ""],
    ["  cancel-in-progress: false", "  cancel-in-progress: true"],
    ["github.event.pull_request.number || github.ref", "github.ref"],
    ["  contents: read", "  contents: write"],
    ["    name: Documentation checks", "    name: Renamed documentation check"],
    ["    timeout-minutes: 60", "    timeout-minutes: 90"],
    ["sanitize: [\"OFF\"]", "sanitize: [\"ON\"]"],
    ["-fsanitize=address,undefined", "-fsanitize=address"],
    ["build/tests/logs/**/*.log", "build/tests/other/**/*.log"],
  ];
  for (const [from, to] of mutations) {
    assert.ok(workflow.includes(from), `mutation must change source: ${from}`);
    assert.throws(() => checkPolicy(workflow.replace(from, to)), { name: "AssertionError" }, from);
  }
});

test("documentation publishing retains its separate main-push workflow", async () => {
  const pages = await readFile(new URL("../../../.github/workflows/pages.yml", import.meta.url), "utf8");
  assert.equal(block(pages, "on"), [
    "on:",
    "  push:",
    "    branches: [main]",
    "    paths: [\"docs/**\", \".github/workflows/pages.yml\"]",
    "  workflow_dispatch:",
  ].join("\n"));
  const packageFile = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
  assert.ok(packageFile.scripts.check.includes("node --test scripts/*.test.mjs"));
});

test("both README badges describe and select merge-group checks", async () => {
  for (const [file, label] of [["README.md", "Merge queue checks"], ["README_CN.md", "合并队列检查"]]) {
    const source = await readFile(new URL(`../../../${file}`, import.meta.url), "utf8");
    assert.ok(source.includes(`[![${label}]`), file);
    assert.ok(source.includes("ci.yml/badge.svg?event=merge_group"), file);
    assert.ok(source.includes("ci.yml?query=event%3Amerge_group"), file);
  }
});
