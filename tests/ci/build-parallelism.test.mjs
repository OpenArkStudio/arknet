// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { admission, observeResources, selectParallelism } from "../../.github/scripts/build-parallelism.mjs";
import { expectedCells } from "../../.github/scripts/ci-required.mjs";

const gib = 1024 ** 3;
const enough = { platform: "linux", boost: "ON", sanitize: "ON", cpu: 4,
  totalMemory: 16 * gib, constrainedMemory: 15 * gib, availableMemory: 12 * gib };
const workflow = readFileSync(new URL("../../.github/workflows/ci.yml", import.meta.url), "utf8");
const native = workflow.split("  test:\n")[1].split("  required:\n")[0];

test("the experimental profile admits exactly two slots only at or above every resource boundary", () => {
  assert.equal(selectParallelism(enough).parallel, 2);
  assert.equal(selectParallelism({ ...enough, totalMemory: admission.effectiveMemory }).parallel, 2);
  for (const changes of [{ cpu: 3 }, { totalMemory: admission.effectiveMemory - 1 },
    { constrainedMemory: admission.effectiveMemory - 1 }, { availableMemory: admission.availableMemory - 1 }]) {
    assert.equal(selectParallelism({ ...enough, ...changes }).parallel, 1, JSON.stringify(changes));
  }
  assert.equal(selectParallelism({ ...enough, cpu: 64, totalMemory: 128 * gib, constrainedMemory: 64 * gib, availableMemory: 48 * gib }).parallel, 2);
});

test("unknown inconsistent and invalid resources retain one slot", () => {
  for (const key of ["cpu", "totalMemory", "constrainedMemory", "availableMemory"]) {
    for (const value of [undefined, null, 0, -1, "16", NaN, Infinity, 1.5, Number.MAX_SAFE_INTEGER + 1]) {
      if (key === "constrainedMemory" && value === 0) continue;
      assert.equal(selectParallelism({ ...enough, [key]: value }).parallel, 1, `${key}/${value}`);
    }
  }
  assert.equal(selectParallelism({ ...enough, availableMemory: 16 * gib }).parallel, 1, "available memory exceeds the effective limit");
  for (const changes of [{ platform: "darwin" }, { platform: "win32" }, { boost: "OFF" }, { sanitize: "THREAD" },
    { sanitize: "OFF" }, { platform: undefined }, { boost: undefined }, { sanitize: undefined }]) {
    assert.equal(selectParallelism({ ...enough, ...changes }).parallel, 1);
  }
});

test("zero is recorded as no reported constraint while independent resource thresholds still apply", () => {
  const resources = { ...enough, constrainedMemory: 0 };
  const decision = selectParallelism(resources);
  assert.equal(decision.parallel, 2);
  assert.equal(decision.constraintStatus, "no-reported-constraint");
  assert.equal(decision.effectiveMemory, resources.totalMemory);
  for (const changes of [{ totalMemory: admission.effectiveMemory - 1 }, { availableMemory: admission.availableMemory - 1 },
    { cpu: 3 }, { availableMemory: undefined }, { availableMemory: NaN }]) {
    assert.equal(selectParallelism({ ...resources, ...changes }).parallel, 1);
  }
});

test("actual resource observations use limit-aware APIs and tolerate missing or failing observations conservatively", () => {
  const env = { MATRIX_BOOST: "ON", MATRIX_SANITIZE: "ON" };
  const system = { availableParallelism: () => 4, totalmem: () => 16 * gib };
  const runtime = { platform: "linux", constrainedMemory: () => 15 * gib, availableMemory: () => 12 * gib };
  assert.deepEqual(observeResources(env, system, runtime), enough);
  for (const [owner, key] of [[system, "availableParallelism"], [system, "totalmem"], [runtime, "constrainedMemory"], [runtime, "availableMemory"]]) {
    const original = owner[key];
    for (const replacement of [undefined, () => { throw new Error("unavailable"); }]) {
      owner[key] = replacement;
      assert.equal(selectParallelism(observeResources(env, system, runtime)).parallel, 1);
    }
    owner[key] = original;
  }
});

test("the real workflow preserves every other matrix profile and the existing target and failure contracts", () => {
  const planner = native.split(/(?=^      - )/m).find(step => step.includes("name: Select bounded compiler parallelism"));
  const condition = planner.match(/if: \$\{\{ (.+) \}\}/)[1];
  const parallel = native.match(/cmake --build build --parallel \$\{\{ (.+) \}\}/)[1]
    .replace("steps.build-parallel", 'steps["build-parallel"]');
  for (const matrix of expectedCells) for (const runNative of ["true", "false"]) {
    const context = { matrix, env: { ARKNET_RUN_NATIVE: runNative },
      runner: { os: { "ubuntu-latest": "Linux", "macos-latest": "macOS", "windows-latest": "Windows" }[matrix.os] },
      steps: { "build-parallel": { outputs: {} } } };
    const selected = runInNewContext(condition, context);
    const intended = runNative === "true" && matrix.os === "ubuntu-latest" && matrix.boost === "ON" && matrix.sanitize === "ON";
    assert.equal(selected, intended);
    assert.equal(runInNewContext(parallel, context), matrix.sanitize === "ON" ? "1" : matrix.sanitize === "THREAD" ? "2" : "3");
    if (selected) {
      context.steps["build-parallel"].outputs.parallel = "2";
      assert.equal(runInNewContext(parallel, context), "2");
    }
  }
  assert.match(planner, /MATRIX_BOOST: \$\{\{ matrix.boost \}\}/);
  assert.match(planner, /MATRIX_SANITIZE: \$\{\{ matrix.sanitize \}\}/);
  assert.match(planner, /run: node .github\/scripts\/build-parallelism.mjs/);
  assert.doesNotMatch(planner, /continue-on-error:/);
  assert.match(native, /timeout-minutes: 60/);
  assert.match(native, /cmake --build build --target arknet_check_build --parallel 3/);
  assert.match(native, /if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  assert.ok(native.includes('"-DCMAKE_CXX_COMPILER_LAUNCHER=$env:ARKNET_COMPILER_LAUNCHER"'));
  assert.ok(native.includes('"-DCMAKE_C_COMPILER_LAUNCHER=$env:ARKNET_COMPILER_LAUNCHER"'));
});

test("the CLI records real observations, the selected bound and a summary without retrying a build", t => {
  const root = mkdtempSync(join(tmpdir(), "arknet-build-parallelism-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const output = join(root, "output"), summary = join(root, "summary");
  const script = fileURLToPath(new URL("../../.github/scripts/build-parallelism.mjs", import.meta.url));
  const env = { ...process.env, MATRIX_BOOST: "ON", MATRIX_SANITIZE: "ON", GITHUB_OUTPUT: output, GITHUB_STEP_SUMMARY: summary };
  const run = spawnSync(process.execPath, [script], { env, encoding: "utf8" });
  assert.equal(run.status, 0, run.stderr);
  const observed = JSON.parse(run.stdout);
  assert.ok([1, 2].includes(observed.parallel));
  assert.equal(readFileSync(output, "utf8"), `parallel=${observed.parallel}\n`);
  assert.equal(observed.resources.platform, process.platform);
  assert.match(readFileSync(summary, "utf8"), /Experimental scheduling heuristic/);
  const missingChannel = spawnSync(process.execPath, [script], { env: { ...env, GITHUB_OUTPUT: "" }, encoding: "utf8" });
  assert.notEqual(missingChannel.status, 0);
});
