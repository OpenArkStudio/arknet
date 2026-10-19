import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { changedFiles, isDocumentationPath, moduleTests, planChecks, sourceTests } from "../../../.github/scripts/ci-scope.mjs";

test("only reviewed documentation paths qualify for reduced native checks", () => {
  for (const file of ["README.md", "README_CN.md", "docs/usage_CN.md", "docs/site/src/styles/prose.css",
    "docs/performance/assets/chart.svg", "docs/images/overview.png"]) {
    assert.equal(isDocumentationPath(file), true, file);
  }
  for (const file of ["AGENTS.md", "CMakeLists.txt", "vcpkg.json", "include/arknet/tcp.hpp", "tests/tcp.cpp",
    "benchmarks/loopback.cpp", "scripts/format.sh", ".github/workflows/ci.yml", ".clang-format",
    "docs/site/package.json", "docs/site/astro.config.mjs", "docs/example.cpp", "docs/link.hpp",
    "docs/../include/lib.md", "docs/./usage.md", "/docs/usage.md", "docs\\usage.md", "", null]) {
    assert.equal(isDocumentationPath(file), false, String(file));
  }
});

test("PR and combined merge-group changes qualify only when every path is known documentation", () => {
  for (const eventName of ["pull_request", "merge_group"]) {
    const base = { eventName, known: true, files: ["README.md", "docs/usage.md"] };
    assert.equal(planChecks(base).full_cpp, false);
    for (const changes of [{ known: false }, { known: "true" }, { files: [] }, { files: null },
      { files: ["docs/usage.md", "include/arknet/tcp.hpp"] }, { eventName: "workflow_dispatch" },
      { eventName: "push" }, { eventName: "pull_request_target" }, { files: [undefined] }]) {
      assert.equal(planChecks({ ...base, ...changes }).full_cpp, true, JSON.stringify(changes));
    }
  }
});

test("exact git ranges cover additions, deletions, renames and multi-change queue candidates", () => {
  const repo = mkdtempSync(join(tmpdir(), "arknet-ci-scope-"));
  const git = (...args) => {
    const result = spawnSync("git", args, { cwd: repo, encoding: "utf8" });
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.trim();
  };
  const put = (name, text) => { mkdirSync(dirname(join(repo, name)), { recursive: true }); writeFileSync(join(repo, name), text); };
  const commit = () => {
    git("add", "-A");
    git("-c", "user.name=Scope test", "-c", "user.email=scope@example.invalid", "commit", "-qm", "test: scope");
    return git("rev-parse", "HEAD");
  };
  try {
    git("init", "-q");
    put("README.md", "Overview\n"); put("docs/usage.md", "Usage\n"); put("include/lib.hpp", "#pragma once\n");
    const base = commit();
    put("docs/usage.md", "Updated usage\n"); put("docs/new.md", "New\n");
    const docs = commit();
    const event = { pull_request: { base: { sha: base }, head: { sha: docs } } };
    assert.deepEqual(changedFiles("pull_request", event, repo), { known: true, files: ["docs/new.md", "docs/usage.md"] });
    assert.equal(planChecks({ eventName: "pull_request", ...changedFiles("pull_request", event, repo) }).full_cpp, false);
    rmSync(join(repo, "docs/new.md")); const removed = commit();
    assert.deepEqual(changedFiles("merge_group", { merge_group: { base_sha: docs, head_sha: removed } }, repo),
      { known: true, files: ["docs/new.md"] });
    renameSync(join(repo, "include/lib.hpp"), join(repo, "docs/moved.md"));
    const renamed = commit();
    const queue = { merge_group: { base_sha: base, head_sha: renamed } };
    const changes = changedFiles("merge_group", queue, repo);
    assert.ok(changes.files.includes("include/lib.hpp"));
    assert.ok(changes.files.includes("docs/moved.md"));
    assert.equal(planChecks({ eventName: "merge_group", ...changes }).full_cpp, true);
    assert.equal(changedFiles("merge_group", { merge_group: { base_sha: base, head_sha: "f".repeat(40) } }, repo).known, false);
    assert.equal(changedFiles("merge_group", { merge_group: { base_sha: base, head_sha: base } }, repo).known, false);
    assert.equal(changedFiles("pull_request", { pull_request: { base: { sha: "--help" }, head: { sha: docs } } }, repo).known, false);
    assert.equal(changedFiles("pull_request", { pull_request: { base: { sha: [base] }, head: { sha: docs } } }, repo).known, false);
    assert.equal(changedFiles("workflow_dispatch", queue, repo).known, false);

    const eventPath = join(repo, "event.json"), outputPath = join(repo, "output.txt");
    writeFileSync(eventPath, JSON.stringify(event));
    const script = fileURLToPath(new URL("../../../.github/scripts/ci-scope.mjs", import.meta.url));
    const execute = eventName => spawnSync(process.execPath, [script], { cwd: repo, encoding: "utf8",
      env: { ...process.env, GITHUB_EVENT_NAME: eventName, GITHUB_EVENT_PATH: eventPath, GITHUB_OUTPUT: outputPath } });
    assert.equal(execute("pull_request").status, 0);
    assert.equal(readFileSync(outputPath, "utf8"), "full_cpp=false\ntier=documentation\nsource_tests=\n");
    writeFileSync(eventPath, "invalid JSON");
    assert.equal(execute("pull_request").status, 0);
    assert.equal(readFileSync(outputPath, "utf8"), "full_cpp=false\ntier=documentation\nsource_tests=\nfull_cpp=true\ntier=full\nsource_tests=\n");
  } finally {
    rmSync(repo, { recursive: true, force: true });
  }
});

test("source PRs select related modules and union mixed changes", () => {
  const examples = { rudp: "include/arknet/fec/transport.hpp", udp: "include/arknet/udp/udp_server.hpp",
    http: "include/arknet/websocket/ws_server.hpp", mysql: "tests/mysql_pool.cpp", redis: "include/arknet/redis/client.hpp" };
  for (const [group, file] of Object.entries(examples)) {
    const plan = planChecks({ eventName: "pull_request", known: true, files: [file, "docs/usage.md"] });
    assert.equal(plan.tier, "modules");
    assert.equal(plan.full_cpp, true);
    assert.deepEqual(plan.source_tests.split(";"), [...moduleTests[group]].sort());
  }
  assert.deepEqual(sourceTests([examples.mysql, examples.redis]),
    [...new Set([...moduleTests.mysql, ...moduleTests.redis])].sort());
  for (const file of ["include/arknet/tcp/tcp_client.hpp", "include/arknet/base/client.hpp",
    "tests/test_main.cpp", "tests/CMakeLists.txt", "vcpkg.json", ".github/workflows/ci.yml", "new-module/file.cpp"]) {
    assert.deepEqual(sourceTests([examples.rudp, file]), [], file);
  }
});

test("queue, manual, unknown ranges and malformed paths cannot narrow native coverage", () => {
  for (const files of [["tests/rudp.cpp"], ["tests/rudp.cpp", "README.md"], [".github/workflows/ci.yml"]]) {
    for (const eventName of ["merge_group", "workflow_dispatch", "unknown"]) {
      const plan = planChecks({ eventName, known: true, files });
      assert.equal(plan.tier, "full");
      assert.equal(plan.source_tests, "");
      assert.equal(plan.full_cpp, true);
    }
  }
  for (const changes of [{ known: false, files: ["tests/rudp.cpp"] }, { known: true, files: [] },
    { known: true, files: [undefined] }, { known: true, files: ["../tests/rudp.cpp"] },
    { known: true, files: ["/tests/rudp.cpp"] }, { known: "true", files: ["tests/rudp.cpp"] }]) {
    assert.equal(planChecks({ eventName: "pull_request", ...changes }).tier, "full");
  }
});
