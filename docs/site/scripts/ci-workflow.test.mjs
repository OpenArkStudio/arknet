import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { runInNewContext } from "node:vm";

import { planChecks } from "../../../.github/scripts/ci-scope.mjs";

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

  // Preserve the 14 required checks. The required style job validates the scope;
  // all 12 native rows retain their commands, sanitizers and uploads for full
  // merge-group checks. Source PRs run related modules in Linux/Boost/TLS/Release;
  // other cells explicitly record scope-only coverage, not native-test success.
  // An intentional job change needs a new review before updating this fingerprint.
  const jobs = source.slice(source.indexOf("jobs:\n"));
  assert.equal(createHash("sha256").update(jobs).digest("hex"),
    "8ba8b41b26e8980f0289a6905279fe6f502174e7597cadd1365d61583d13dafe",
    "review the complete 14-check and scope contract before changing it");
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
    ["    needs: format", "    needs: []"],
    ["needs.format.outputs.full_cpp == 'true'", "needs.format.outputs.full_cpp != 'false'"],
    ["throw 'Missing or failed CI scope validation'", "Write-Output 'Missing scope'"],
    ["fetch-depth: 0", "fetch-depth: 1"],
    ["needs.format.outputs.tier == 'full'", "needs.format.outputs.tier != 'documentation'"],
    ["Invalid validation tier for this event", "Ignore invalid tier"],
    ["--target arknet_check_build", "--target nonexistent"],
    ["$env:ARKNET_FULL_CPP -cne", "$env:ARKNET_FULL_CPP -ne"],
  ];
  for (const [from, to] of mutations) {
    assert.ok(workflow.includes(from), `mutation must change source: ${from}`);
    assert.throws(() => checkPolicy(workflow.replace(from, to)), { name: "AssertionError" }, from);
  }
});

test("real workflow selector keeps all twelve queue cells and only the source regression cell", () => {
  const expression = workflow.match(/ARKNET_RUN_NATIVE: \$\{\{ (.+) \}\}/)?.[1];
  assert.ok(expression);
  const matrix = [];
  for (const os of ["ubuntu-latest", "macos-latest", "windows-latest"]) {
    for (const boost of ["OFF", "ON"]) matrix.push({ os, boost, ssl: "ON", sanitize: "OFF" });
  }
  for (const boost of ["OFF", "ON"]) {
    matrix.push({ os: "ubuntu-latest", boost, ssl: "OFF", sanitize: "OFF" });
    for (const sanitize of ["ON", "THREAD"]) matrix.push({ os: "ubuntu-latest", boost, ssl: "ON", sanitize });
  }
  const cells = plan => matrix.filter(cell => runInNewContext(expression,
    { needs: { format: { outputs: { ...plan, full_cpp: String(plan.full_cpp) } } }, matrix: cell }, { timeout: 100 }));
  const make = eventName => planChecks({ eventName, known: true, files: ["tests/rudp.cpp"] });
  assert.equal(cells(make("merge_group")).length, 12);
  assert.equal(cells(make("workflow_dispatch")).length, 12);
  // An explicit dispatch on main seeds its shared caches even when a future
  // change detector reports documentation-only files. It must keep every profile.
  const manualSeed = planChecks({ eventName: "workflow_dispatch", known: true, files: ["README.md"] });
  assert.equal(manualSeed.tier, "full");
  assert.equal(manualSeed.source_tests, "");
  assert.equal(cells(manualSeed).length, 12);
  assert.deepEqual(cells(make("pull_request")), [
    { os: "ubuntu-latest", boost: "ON", ssl: "ON", sanitize: "OFF" },
  ]);
  assert.equal(cells(planChecks({ eventName: "pull_request", known: false })).length, 12);
  assert.equal(cells(planChecks({ eventName: "merge_group", known: true, files: ["README.md"] })).length, 0);
  assert.match(workflow, /\$env:GITHUB_EVENT_NAME -cne 'pull_request' -and \$env:ARKNET_VALIDATION_TIER -ceq 'modules'/);
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
