// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { cellName, expectedCells, optionalCacheSteps, validateRequired } from "../../.github/scripts/ci-required.mjs";
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
      always: () => true, steps: {
        "vcpkg-cache": { outputs: { enabled: "true" } },
        "compiler-cache": { outputs: { enabled: "true" } },
      } };
    context.env = { ARKNET_RUN_NATIVE: String(runInNewContext(nativeExpression, context)) };
    jobs.push({ databaseId: index + 3,
      name: jobNameExpression.replaceAll(/\$\{\{ (.*?) \}\}/g, (_, expression) => runInNewContext(expression, context)),
      status: "completed", conclusion: "success", steps: steps("test").map((step, number) => {
        const expression = step.match(/^        if: \$\{\{ (.+) \}\}$/m)?.[1];
        return { name: stepName(step), number: number + 1, status: "completed",
          conclusion: !expression || runInNewContext(expression.replace(/steps\.([a-z0-9-]+)/g, 'steps["$1"]'), context) ? "success" : "skipped" };
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
    ...["pull_request", "merge_group"].flatMap(event => [
      [event, ["AGENTS.md", "docs/site/package-lock.json", "docs/site/astro.config.ts",
        "docs/site/src/components/Mermaid.astro", "docs/site/scripts/ui-controls.test.mjs"], {}, 0],
      ...["tests/rudp.cpp", "docs/site/scripts/ci-scope.test.mjs", "docs/site/unknown.config.json"]
        .map(file => [event, ["docs/site/package.json", file], {}, event === "merge_group" ? 12 : 1]),
    ]),
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
      for (const step of actual.steps.filter(step => !optionalCacheSteps.includes(step.name))) {
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

const cacheActionNames = steps("test").filter(step => /^        continue-on-error: true$/m.test(step) && /^        uses:/m.test(step)).map(stepName);
const addPost = (job, name, conclusion) => job.steps.push({ name: `Post ${name}`,
  number: Math.max(...job.steps.map(step => step.number)) + 1, status: "completed", conclusion });

test("the failure allowance matches only explicit optional cache steps and their action posts", () => {
  assert.deepEqual(steps("test").filter(step => /^        continue-on-error: true$/m.test(step)).map(stepName), optionalCacheSteps);
  assert.deepEqual(cacheActionNames, ["Restore dependency archives", "Install compiler cache", "Restore compiler cache"]);
});

test("unavailable optional caches and their posts permit cold builds only when all required native steps succeed", () => {
  for (const event of ["pull_request", "merge_group"]) {
    for (const mode of ["restore-failure", "install-failure", "disabled"]) {
      const input = fixture(event, ["tests/rudp.cpp"]);
      for (const job of input.metadata.jobs.slice(2, -1)) {
        if (findStep(job, "Configure").conclusion !== "success") continue;
        findStep(job, "Restore dependency archives").conclusion = mode === "disabled" ? "skipped" : "failure";
        findStep(job, "Install compiler cache").conclusion = mode === "install-failure" ? "failure" : "success";
        findStep(job, "Restore compiler cache").conclusion = mode === "restore-failure" ? "failure" : "skipped";
        findStep(job, "Start compiler cache after archive restore").conclusion = mode === "restore-failure" ? "success" : "skipped";
        findStep(job, "Report cache statistics").conclusion = "failure";
        for (const name of cacheActionNames) {
          addPost(job, name, findStep(job, name).conclusion === "skipped" ? "skipped" : "failure");
        }
      }
      assert.equal(validateRequired(input).native_cells, event === "merge_group" ? 12 : 1);
      for (const name of ["Configure", "Build planned native targets", "Test"]) {
        const invalid = structuredClone(input);
        const job = invalid.metadata.jobs.find(job => job.steps.some(step => step.name === name && step.conclusion === "success"));
        findStep(job, name).conclusion = "failure";
        assert.throws(() => validateRequired(invalid), `${mode}/${name}`);
      }
    }
  }
});

test("cache fallback rejects unknown outcomes, missing steps, contradictory activation and unknown failed cleanup", () => {
  const valid = fixture("merge_group", ["tests/rudp.cpp"]);
  for (const name of optionalCacheSteps) {
    for (const conclusion of ["cancelled", "neutral", "unknown", "timed_out", "", null, undefined]) {
      const input = structuredClone(valid);
      findStep(input.metadata.jobs[2], name).conclusion = conclusion;
      assert.throws(() => validateRequired(input), `${name}/${conclusion}`);
    }
    const missing = structuredClone(valid);
    missing.metadata.jobs[2].steps = missing.metadata.jobs[2].steps.filter(step => step.name !== name);
    assert.throws(() => validateRequired(missing));
  }
  for (const name of ["Install compiler cache", "Report cache statistics"]) {
    const skipped = structuredClone(valid);
    findStep(skipped.metadata.jobs[2], name).conclusion = "skipped";
    assert.throws(() => validateRequired(skipped));
  }
  for (const name of ["Report cache statistics", "Prepare compiler launcher", "Configure", "unknown cache cleanup"]) {
    const unknown = structuredClone(valid);
    addPost(unknown.metadata.jobs[2], name, "failure");
    assert.throws(() => validateRequired(unknown));
  }
  const contradictory = structuredClone(valid);
  findStep(contradictory.metadata.jobs[2], "Install compiler cache").conclusion = "failure";
  assert.throws(() => validateRequired(contradictory), "failed installation cannot activate a compiler launcher");
  const missingStart = structuredClone(valid);
  findStep(missingStart.metadata.jobs[2], "Start compiler cache after archive restore").conclusion = "skipped";
  assert.throws(() => validateRequired(missingStart), "an enabled restore still requires launcher startup");
  const nonCacheJob = structuredClone(valid);
  nonCacheJob.metadata.jobs[0].steps.push({ name: "Restore dependency archives", number: 999, status: "completed", conclusion: "failure" });
  assert.throws(() => validateRequired(nonCacheJob), "cache exceptions do not apply to documentation jobs");
});

test("scope-only cells cannot execute cache steps or their posts", () => {
  for (const [event, files] of [["pull_request", ["tests/rudp.cpp"]], ["merge_group", ["README.md"]]]) {
    const valid = fixture(event, files);
    const index = valid.metadata.jobs.findIndex(job => job.steps.some(step => step.name === "Configure" && step.conclusion === "skipped"));
    for (const name of optionalCacheSteps) for (const conclusion of ["success", "failure"]) {
      const input = structuredClone(valid);
      findStep(input.metadata.jobs[index], name).conclusion = conclusion;
      assert.throws(() => validateRequired(input), `${name}/${conclusion}`);
    }
    for (const name of cacheActionNames) {
      const skippedPost = structuredClone(valid);
      addPost(skippedPost.metadata.jobs[index], name, "skipped");
      validateRequired(skippedPost);
      for (const conclusion of ["success", "failure", "unknown"]) {
        const input = structuredClone(valid);
        addPost(input.metadata.jobs[index], name, conclusion);
        assert.throws(() => validateRequired(input), `Post ${name}/${conclusion}`);
      }
    }
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

test("the CLI recomputes site maintenance coverage and fails closed for mixed or unknown ranges", t => {
  const root = mkdtempSync(join(tmpdir(), "arknet-required-site-"));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const git = (...args) => execFileSync("git", args, { cwd: root, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
  git("init", "-q");
  git("config", "user.name", "Gate fixture");
  git("config", "user.email", "fixture@example.invalid");
  git("config", "commit.gpgsign", "false");
  const commit = () => { git("add", "-A"); git("commit", "-qm", "test: site fixture"); return git("rev-parse", "HEAD"); };
  writeFileSync(join(root, "README.md"), "# Base\n");
  const base = commit();
  mkdirSync(join(root, "docs/site"), { recursive: true });
  writeFileSync(join(root, "docs/site/astro.config.ts"), "// documentation build\n");
  writeFileSync(join(root, "AGENTS.md"), "# Instructions\n");
  const head = commit();
  const nativeHeads = ["docs/site/unknown.mjs", "docs/site/fixture.cpp", "docs/site/CMakeLists.txt",
    "docs/site/scripts/ci-scope.test.mjs"].map(file => {
    git("checkout", "-q", "--detach", head);
    mkdirSync(join(root, file, ".."), { recursive: true });
    writeFileSync(join(root, file), "// preserve native coverage\n");
    return commit();
  });
  const eventPath = join(root, "event.json"), jobsPath = join(root, "jobs.json");
  const script = fileURLToPath(new URL("../../.github/scripts/ci-required.mjs", import.meta.url));
  for (const eventName of ["pull_request", "merge_group"]) {
    const input = fixture(eventName, ["AGENTS.md", "docs/site/astro.config.ts"]);
    writeFileSync(jobsPath, JSON.stringify(input.metadata));
    const run = actualHead => {
      const event = eventName === "pull_request" ? { pull_request: { base: { sha: base }, head: { sha: actualHead } } }
        : { merge_group: { base_sha: base, head_sha: actualHead } };
      writeFileSync(eventPath, JSON.stringify(event));
      return spawnSync(process.execPath, [script], { cwd: root, encoding: "utf8", env: { ...process.env,
        GITHUB_EVENT_NAME: eventName, GITHUB_EVENT_PATH: eventPath, CI_JOBS_PATH: jobsPath, CI_NEEDS_JSON: JSON.stringify(input.needs) } });
    };
    const docs = run(head);
    assert.equal(docs.status, 0, docs.stderr);
    assert.deepEqual(JSON.parse(docs.stdout), { tier: "documentation", native_cells: 0, scope_only_cells: 12 });
    for (const actualHead of [...nativeHeads, "f".repeat(40), base]) {
      assert.notEqual(run(actualHead).status, 0, `${eventName}: documentation metadata cannot stand in for ${actualHead}`);
    }
  }
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
