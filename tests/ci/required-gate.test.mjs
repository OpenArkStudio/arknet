// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { cellName, expectedCells, validateRequired } from "../../.github/scripts/ci-required.mjs";
import { planChecks } from "../../.github/scripts/ci-scope.mjs";

const workflow = readFileSync(new URL("../../.github/workflows/ci.yml", import.meta.url), "utf8");
const block = name => workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(text => text.startsWith(`  ${name}:\n`));
const steps = name => block(name).split(/(?=^      - )/m).slice(1);
const stepName = step => step.match(/^      - name: (.+)$/m)?.[1] ??
  (step.match(/^      - uses: (.+)$/m)?.[1] ? `Run ${step.match(/^      - uses: (.+)$/m)[1]}` :
    `Run ${step.match(/(?:^      - |^        )run: (.+)$/m)?.[1]}`);
const outputs = plan => Object.fromEntries(["full_cpp", "tier", "source_tests"].map(key => [key, String(plan[key])]));
const nativeExpression = block("test").match(/ARKNET_RUN_NATIVE: \$\{\{ (.+) \}\}/)[1];
const jobNameExpression = block("test").match(/^    name: (.+)$/m)[1];

function actualCells() {
  const matrix = block("test").split("      matrix:\n")[1].split("    runs-on:")[0];
  const values = key => JSON.parse("[" + matrix.match(new RegExp(`^        ${key}: \\[([^\\]]+)\\]$`, "m"))[1]
    .split(",").map(value => JSON.stringify(value.trim().replaceAll('"', ""))).join(",") + "]");
  const cells = [];
  for (const os of values("os")) for (const boost of values("boost")) {
    for (const ssl of values("ssl")) for (const sanitize of values("sanitize")) cells.push({ os, boost, ssl, sanitize });
  }
  for (const [, os, boost, ssl, sanitize] of matrix.matchAll(/- \{os: ([^,]+), boost: "([^"]+)", ssl: "([^"]+)", sanitize: "([^"]+)"\}/g)) {
    cells.push({ os, boost, ssl, sanitize });
  }
  return cells;
}

function fixture(eventName, files, extra = {}) {
  const plan = planChecks({ eventName, known: true, files, ...extra });
  const needs = { docs: { result: "success" }, format: { result: "success", outputs: outputs(plan) }, test: { result: "success" } };
  const jobs = ["docs", "format"].map((name, index) => ({
    databaseId: index + 1, name: block(name).match(/^    name: (.+)$/m)[1], status: "completed", conclusion: "success",
    steps: steps(name).map((step, number) => ({ name: stepName(step), number: number + 1, status: "completed", conclusion: "success" })),
  }));
  for (const [index, matrix] of actualCells().entries()) {
    const context = { matrix, needs, runner: { os: { "ubuntu-latest": "Linux", "macos-latest": "macOS", "windows-latest": "Windows" }[matrix.os] },
      always: () => true };
    context.env = { ARKNET_RUN_NATIVE: String(runInNewContext(nativeExpression, context)) };
    jobs.push({ databaseId: index + 3,
      name: jobNameExpression.replaceAll(/\$\{\{ (.*?) \}\}/g, (_, expression) => runInNewContext(expression, context)),
      status: "completed", conclusion: "success", steps: steps("test").map((step, number) => {
        const expression = step.match(/^        if: \$\{\{ (.+) \}\}$/m)?.[1];
        return { name: stepName(step), number: number + 1, status: "completed",
          conclusion: !expression || runInNewContext(expression, context) ? "success" : "skipped" };
      }) });
  }
  // gh run view includes the still-running aggregate itself; it is not a prerequisite.
  jobs.push({ databaseId: 100, name: "CI - required", status: "in_progress", conclusion: "", steps: [] });
  return { plan, needs, metadata: { jobs }, eventName };
}
const findStep = (job, name) => job.steps.find(step => step.name === name);

test("the independently reviewed manifest matches all twelve actual matrix profiles and names", () => {
  const cells = actualCells();
  assert.equal(cells.length, 12);
  assert.deepEqual(new Set(cells.map(cellName)), new Set(expectedCells.map(cellName)));
  assert.equal(new Set(expectedCells.map(cellName)).size, 12);
  const full = fixture("merge_group", ["tests/rudp.cpp"]);
  assert.deepEqual(new Set(full.metadata.jobs.slice(2, -1).map(job => job.name)), new Set(expectedCells.map(cellName)));
});

test("real workflow step conditions produce accepted full, source-suite, module and documentation coverage", () => {
  const cases = [
    ["merge_group", ["tests/rudp.cpp"], {}, 12], ["workflow_dispatch", ["README.md"], {}, 12],
    ["pull_request", ["tests/rudp.cpp"], { known: false }, 12],
    ["pull_request", [".github/workflows/ci.yml"], {}, 1], ["pull_request", ["tests/redis_client.cpp"], {}, 1],
    ["pull_request", ["tests/mysql_auth.cpp", "tests/rudp.cpp"], {}, 1],
    ["pull_request", ["README.md"], {}, 0], ["merge_group", ["docs/project/testing.md"], {}, 0],
  ];
  for (const [event, files, extra, native] of cases) {
    const input = fixture(event, files, extra);
    assert.deepEqual(validateRequired(input), { tier: input.plan.tier, native_cells: native, scope_only_cells: 12 - native });
  }
});

test("omitted empty job outputs cannot grant a narrower source plan", () => {
  for (const [event, files] of [["merge_group", ["tests/rudp.cpp"]], ["pull_request", [".github/workflows/ci.yml"]], ["merge_group", ["README.md"]]]) {
    const input = fixture(event, files);
    delete input.needs.format.outputs.source_tests;
    validateRequired(input);
    input.needs.format.outputs.source_tests = null;
    assert.throws(() => validateRequired(input));
    for (const key of ["full_cpp", "tier"]) {
      const missing = fixture(event, files);
      delete missing.needs.format.outputs[key];
      assert.throws(() => validateRequired(missing));
    }
  }
  const modules = fixture("pull_request", ["tests/rudp.cpp"]);
  delete modules.needs.format.outputs.source_tests;
  assert.throws(() => validateRequired(modules));
});

test("failed, skipped, cancelled, pending, duplicate and missing prerequisite jobs cannot pass", () => {
  const valid = fixture("merge_group", ["tests/rudp.cpp"]);
  for (const prerequisite of ["docs", "format", "test"]) for (const result of ["failure", "cancelled", "skipped", "pending", "", undefined]) {
    const input = structuredClone(valid);
    input.needs[prerequisite].result = result;
    assert.throws(() => validateRequired(input));
  }
  for (let index = 0; index < 14; index++) {
    for (const change of [job => { job.status = "in_progress"; }, job => { job.conclusion = "failure"; },
      job => { job.conclusion = "skipped"; }, job => { job.conclusion = undefined; }, job => { job.steps = []; }]) {
      const input = structuredClone(valid);
      change(input.metadata.jobs[index]);
      assert.throws(() => validateRequired(input), `job ${index}`);
    }
    const missing = structuredClone(valid);
    missing.metadata.jobs.splice(index, 1);
    assert.throws(() => validateRequired(missing));
    const duplicate = structuredClone(valid);
    duplicate.metadata.jobs.push(structuredClone(duplicate.metadata.jobs[index]));
    assert.throws(() => validateRequired(duplicate));
  }
  for (const mutation of [input => { input.metadata = {}; }, input => { input.metadata.jobs[2].name += " - wrong profile"; },
    input => { input.metadata.jobs[2].databaseId = input.metadata.jobs[3].databaseId; }]) {
    const input = structuredClone(valid);
    mutation(input);
    assert.throws(() => validateRequired(input));
  }
});

test("successful jobs cannot hide skipped full checks, executed scope-only checks or wrong sanitizer test steps", () => {
  for (const [event, files] of [["merge_group", ["tests/rudp.cpp"]], ["pull_request", ["tests/rudp.cpp"]], ["merge_group", ["README.md"]]]) {
    const valid = fixture(event, files);
    for (let index = 2; index < 14; index++) {
      const actual = valid.metadata.jobs[index];
      for (const step of actual.steps) {
        for (const conclusion of [step.conclusion === "success" ? "skipped" : "success", "failure", "cancelled", "neutral", "unknown", "", undefined]) {
          const input = structuredClone(valid);
          findStep(input.metadata.jobs[index], step.name).conclusion = conclusion;
          assert.throws(() => validateRequired(input), `${event}/${actual.name}/${step.name}/${conclusion}`);
        }
        const absent = structuredClone(valid);
        absent.metadata.jobs[index].steps = absent.metadata.jobs[index].steps.filter(item => item.name !== step.name);
        assert.throws(() => validateRequired(absent));
      }
    }
  }
});

test("documentation and formatting require actual successful steps without duplicate or incomplete outcomes", () => {
  const valid = fixture("merge_group", ["README.md"]);
  for (let index = 0; index < 2; index++) {
    for (const step of valid.metadata.jobs[index].steps) {
      const invalid = structuredClone(valid);
      findStep(invalid.metadata.jobs[index], step.name).conclusion = "unknown";
      assert.throws(() => validateRequired(invalid));
    }
  }
  for (const name of ["Run npm run check --prefix docs/site", "Check project C++ formatting"]) {
    const index = name.startsWith("Run npm") ? 0 : 1;
    for (const conclusion of ["skipped", "failure", undefined]) {
      const invalid = structuredClone(valid);
      findStep(invalid.metadata.jobs[index], name).conclusion = conclusion;
      assert.throws(() => validateRequired(invalid));
    }
    const duplicate = structuredClone(valid);
    duplicate.metadata.jobs[index].steps.push({ ...findStep(duplicate.metadata.jobs[index], name), number: 999 });
    assert.throws(() => validateRequired(duplicate));
    const pending = structuredClone(valid);
    findStep(pending.metadata.jobs[index], name).status = "in_progress";
    assert.throws(() => validateRequired(pending));
  }
});

test("scope output substitutions and contradictory event tiers fail closed", () => {
  const valid = fixture("pull_request", ["tests/rudp.cpp"]);
  for (const [key, values] of Object.entries({ full_cpp: [undefined, true, "TRUE", "false"], tier: [undefined, "full", "documentation", "unknown"],
    source_tests: [undefined, "rudp", "rudp;missing", ""] })) {
    for (const value of values) {
      const input = structuredClone(valid);
      input.needs.format.outputs[key] = value;
      assert.throws(() => validateRequired(input));
    }
  }
  for (const mutation of [input => { input.eventName = "merge_group"; }, input => { input.eventName = "workflow_dispatch"; },
    input => { input.eventName = "push"; }, input => { input.plan.full_cpp = false; }, input => { input.plan.tier = "documentation"; }]) {
    const input = structuredClone(valid);
    mutation(input);
    assert.throws(() => validateRequired(input));
  }
});

test("the CLI reclassifies actual event commits and rejects substituted or unavailable metadata", t => {
  const root = mkdtempSync(join(tmpdir(), "arknet-required-gate-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const git = (...args) => execFileSync("git", args, { cwd: root, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
  git("init", "-q");
  git("config", "user.name", "Gate fixture");
  git("config", "user.email", "fixture@example.invalid");
  git("config", "commit.gpgsign", "false");
  mkdirSync(join(root, "tests"));
  writeFileSync(join(root, "tests/rudp.cpp"), "// base\n");
  const commit = () => { git("add", "-A"); git("commit", "-qm", "test: gate fixture"); return git("rev-parse", "HEAD"); };
  const base = commit();
  writeFileSync(join(root, "tests/rudp.cpp"), "// changed\n");
  const head = commit();
  const input = fixture("pull_request", ["tests/rudp.cpp"]);
  const eventPath = join(root, "event.json"), jobsPath = join(root, "jobs.json");
  writeFileSync(eventPath, JSON.stringify({ pull_request: { base: { sha: base }, head: { sha: head } } }));
  writeFileSync(jobsPath, JSON.stringify(input.metadata));
  const script = fileURLToPath(new URL("../../.github/scripts/ci-required.mjs", import.meta.url));
  const run = needs => spawnSync(process.execPath, [script], { cwd: root, encoding: "utf8",
    env: { ...process.env, GITHUB_EVENT_NAME: "pull_request", GITHUB_EVENT_PATH: eventPath, CI_JOBS_PATH: jobsPath, CI_NEEDS_JSON: JSON.stringify(needs) } });
  assert.equal(run(input.needs).status, 0);
  const filtered = structuredClone(input.needs);
  filtered.format.outputs.source_tests = "rudp";
  assert.notEqual(run(filtered).status, 0);
  writeFileSync(eventPath, JSON.stringify({ pull_request: { base: { sha: "f".repeat(40) }, head: { sha: head } } }));
  assert.notEqual(run(input.needs).status, 0, "missing history cannot grant module-only coverage");
  writeFileSync(eventPath, "invalid");
  assert.notEqual(run(input.needs).status, 0);
  rmSync(jobsPath);
  assert.notEqual(run(input.needs).status, 0);
});

test("the appended gate reads same-run metadata with only job-local read permissions", () => {
  const required = block("required");
  assert.match(required, /^    name: CI - required$/m);
  assert.match(required, /^    if: always\(\)$/m);
  assert.match(required, /^    needs: \[docs, format, test\]$/m);
  assert.match(required, /    permissions:\n      contents: read\n      actions: read\n/);
  assert.match(required, /fetch-depth: 0/);
  assert.match(required, /GH_TOKEN: \$\{\{ github.token \}\}/);
  assert.ok(required.includes('gh run view "$GITHUB_RUN_ID" --repo "$GITHUB_REPOSITORY" --json jobs > "$RUNNER_TEMP/arknet-ci-jobs.json"'));
  assert.match(required, /CI_NEEDS_JSON: \$\{\{ toJSON\(needs\) \}\}/);
  assert.match(required, /CI_JOBS_PATH: \$\{\{ runner.temp \}\}\/arknet-ci-jobs.json/);
  assert.match(required, /run: node .github\/scripts\/ci-required.mjs/);
  assert.doesNotMatch(required, /continue-on-error:|contents: write|actions: write|gh api|gh run rerun/);
});
