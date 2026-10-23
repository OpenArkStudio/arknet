// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import { admission, observeResources, selectParallelism } from "../../.github/scripts/build-parallelism.mjs";

const gib = 1024 ** 3;
const baseline = { platform: "linux", boost: "ON", sanitize: "ON", cpu: 4,
  totalMemory: 16 * gib, constrainedMemory: 15 * gib, availableMemory: 12 * gib };
const workflow = readFileSync(new URL("../../.github/workflows/ci.yml", import.meta.url), "utf8");
const native = workflow.split("  test:\n")[1].split("  required:\n")[0];

for (const boost of ["OFF", "ON"]) {
  const enough = { ...baseline, boost };

  test(`the ${boost} experimental profile admits exactly two slots only at or above every resource boundary`, () => {
    assert.equal(selectParallelism(enough).parallel, 2);
    assert.equal(selectParallelism({ ...enough, totalMemory: admission.effectiveMemory }).parallel, 2);
    for (const changes of [{ cpu: 3 }, { totalMemory: admission.effectiveMemory - 1 },
      { constrainedMemory: admission.effectiveMemory - 1 }, { availableMemory: admission.availableMemory - 1 }]) {
      assert.equal(selectParallelism({ ...enough, ...changes }).parallel, 1, JSON.stringify(changes));
    }
    assert.equal(selectParallelism({ ...enough, cpu: 64, totalMemory: 128 * gib, constrainedMemory: 64 * gib, availableMemory: 48 * gib }).parallel, 2);
  });

  test(`unknown inconsistent and invalid resources retain one slot for ${boost}`, () => {
    for (const key of ["cpu", "totalMemory", "constrainedMemory", "availableMemory"]) {
      for (const value of [undefined, null, 0, -1, "16", NaN, Infinity, 1.5, Number.MAX_SAFE_INTEGER + 1]) {
        if (key === "constrainedMemory" && (value === 0 || value === Number.MAX_SAFE_INTEGER + 1)) continue;
        assert.equal(selectParallelism({ ...enough, [key]: value }).parallel, 1, `${key}/${value}`);
      }
    }
    assert.equal(selectParallelism({ ...enough, availableMemory: 16 * gib }).parallel, 1, "available memory exceeds the effective limit");
    for (const changes of [{ platform: "darwin" }, { platform: "win32" }, { boost: "unknown" }, { boost: "on" }, { boost: null }, { sanitize: "THREAD" },
      { sanitize: "OFF" }, { platform: undefined }, { boost: undefined }, { sanitize: undefined }]) {
      assert.equal(selectParallelism({ ...enough, ...changes }).parallel, 1);
    }
  });

  test(`the ${boost} large external limit is clipped to validated physical memory without changing admission thresholds`, () => {
    const observed = { platform: "linux", boost, sanitize: "ON", cpu: 4,
      totalMemory: 16766410752, constrainedMemory: 18446744073709552000, availableMemory: 15565606912 };
    const decision = selectParallelism(observed);
    assert.deepEqual(admission, { cpu: 4, effectiveMemory: 15 * gib, availableMemory: 12 * gib });
    assert.equal(decision.parallel, 2);
    assert.equal(decision.effectiveMemory, observed.totalMemory);
    assert.equal(decision.constraintStatus, "reported-above-host-memory");
    assert.equal(observed.constrainedMemory, 18446744073709552000, "keep the raw observation");
    for (const constrainedMemory of [Number.MAX_SAFE_INTEGER + 1, 2 ** 64, Number.MAX_VALUE]) {
      const clipped = selectParallelism({ ...observed, constrainedMemory });
      assert.equal(clipped.parallel, 2);
      assert.equal(clipped.effectiveMemory, observed.totalMemory);
      assert.ok(Number.isSafeInteger(clipped.effectiveMemory));
      assert.equal(clipped.constraintStatus, "reported-above-host-memory");
    }
    for (const changes of [{ cpu: admission.cpu - 1 }, { totalMemory: admission.effectiveMemory - 1 },
      { constrainedMemory: admission.effectiveMemory - 1 }, { availableMemory: admission.availableMemory - 1 },
      { availableMemory: observed.totalMemory + 1 }]) {
      assert.equal(selectParallelism({ ...observed, ...changes }).parallel, 1, JSON.stringify(changes));
    }
    for (const constrainedMemory of [NaN, Infinity, -Infinity, -1, 1.5, "18446744073709552000"]) {
      assert.equal(selectParallelism({ ...observed, constrainedMemory }).parallel, 1);
    }
  });

  test(`zero ${boost} constraint still requires independent resource thresholds`, () => {
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

  test(`actual ${boost} observations use limit-aware APIs and tolerate missing or failing observations conservatively`, () => {
    const env = { MATRIX_BOOST: boost, MATRIX_SANITIZE: "ON" };
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

}

test("grouped workflow retains per-profile bounded planning and the existing job deadline", () => {
  const planners = native.split(/(?=^      - )/m).filter(step => step.includes("name: Select bounded compiler parallelism"));
  assert.equal(planners.length, 2);
  for (const planner of planners) {
    assert.match(planner, /matrix.sanitize == 'ON'/);
    assert.match(planner, /run: node .github\/scripts\/native-profile.mjs parallel/);
    assert.doesNotMatch(planner, /continue-on-error:/);
  }
  assert.match(native, /timeout-minutes: 60/);
});

test("the CLI records real observations, the selected bound and a summary without retrying a build", t => {
  const root = mkdtempSync(join(tmpdir(), "arknet-build-parallelism-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  for (const boost of ["OFF", "ON"]) {
    const output = join(root, `output-${boost}`), summary = join(root, `summary-${boost}`);
    const script = fileURLToPath(new URL("../../.github/scripts/build-parallelism.mjs", import.meta.url));
    const env = { ...process.env, MATRIX_BOOST: boost, MATRIX_SANITIZE: "ON", GITHUB_OUTPUT: output, GITHUB_STEP_SUMMARY: summary };
    const run = spawnSync(process.execPath, [script], { env, encoding: "utf8" });
    assert.equal(run.status, 0, run.stderr);
    const observed = JSON.parse(run.stdout);
    assert.ok([1, 2].includes(observed.parallel));
    assert.equal(readFileSync(output, "utf8"), `parallel=${observed.parallel}\n`);
    assert.equal(observed.resources.platform, process.platform);
    assert.equal(observed.resources.boost, boost);
    assert.match(readFileSync(summary, "utf8"), /Experimental scheduling heuristic/);
    const missingChannel = spawnSync(process.execPath, [script], { env: { ...env, GITHUB_OUTPUT: "" }, encoding: "utf8" });
    assert.notEqual(missingChannel.status, 0);
  }
});

test("Windows Boost Release admits four slots at every threshold and otherwise keeps three", () => {
  const enough = { ...baseline, platform: "win32", sanitize: "OFF" };
  assert.equal(selectParallelism(enough).parallel, 4);
  for (const changes of [{ totalMemory: admission.effectiveMemory }, { constrainedMemory: 0 },
    { constrainedMemory: 2 ** 64 }, { cpu: 64, totalMemory: 128 * gib, constrainedMemory: 64 * gib, availableMemory: 48 * gib }]) {
    assert.equal(selectParallelism({ ...enough, ...changes }).parallel, 4, JSON.stringify(changes));
  }
  for (const changes of [{ cpu: admission.cpu - 1 }, { totalMemory: admission.effectiveMemory - 1 },
    { constrainedMemory: admission.effectiveMemory - 1 }, { availableMemory: admission.availableMemory - 1 },
    { availableMemory: 16 * gib }]) {
    assert.equal(selectParallelism({ ...enough, ...changes }).parallel, 3, JSON.stringify(changes));
  }
  for (const key of ["cpu", "totalMemory", "constrainedMemory", "availableMemory"]) {
    for (const value of [undefined, null, -1, "16", NaN, Infinity, -Infinity, 1.5]) {
      const decision = selectParallelism({ ...enough, [key]: value });
      assert.equal(decision.parallel, 3, `${key}/${value}`);
      assert.equal(decision.reason, "unknown-or-invalid-resource");
    }
    if (key !== "constrainedMemory") assert.equal(selectParallelism({ ...enough, [key]: 0 }).parallel, 3);
  }
  for (const changes of [{ boost: "OFF" }, { boost: "on" }, { sanitize: "ON" }, { sanitize: "THREAD" },
    { platform: "darwin" }, { platform: "linux" }]) {
    assert.notEqual(selectParallelism({ ...enough, ...changes }).parallel, 4);
  }
  const env = { MATRIX_BOOST: "ON", MATRIX_SANITIZE: "OFF" };
  const system = { availableParallelism: () => 4, totalmem: () => 16 * gib };
  const runtime = { platform: "win32", constrainedMemory: () => 15 * gib, availableMemory: () => 12 * gib };
  for (const [owner, key] of [[system, "availableParallelism"], [system, "totalmem"], [runtime, "constrainedMemory"], [runtime, "availableMemory"]]) {
    const original = owner[key];
    for (const replacement of [undefined, () => { throw new Error("unavailable"); }]) {
      owner[key] = replacement;
      assert.equal(selectParallelism(observeResources(env, system, runtime)).parallel, 3);
    }
    owner[key] = original;
  }
});
