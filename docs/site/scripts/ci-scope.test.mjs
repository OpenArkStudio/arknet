import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { changedFiles, isDocumentationPath, moduleTests, planChecks, sourceTests } from "../../../.github/scripts/ci-scope.mjs";

const siteFiles = ["AGENTS.md", "docs/site/.gitignore", "docs/site/LICENSE", "docs/site/astro.config.ts",
  "docs/site/package.json", "docs/site/package-lock.json", "docs/site/tsconfig.json", "docs/site/nimbus.json",
  "docs/site/scripts/check-site.mjs", "docs/site/scripts/content.mjs", "docs/site/scripts/content.test.mjs",
  "docs/site/scripts/prepare-content.mjs", "docs/site/scripts/redirects.mjs", "docs/site/scripts/structure.test.mjs",
  "docs/site/scripts/ui-controls.mjs", "docs/site/scripts/ui-controls.test.mjs", "docs/site/src/content.config.ts",
  "docs/site/src/navigation.ts", "docs/site/src/legacy-routes.json", "docs/site/src/components/Mermaid.astro",
  "docs/site/src/components/ui/search/search.client.ts", "docs/site/src/layouts/BaseLayout.astro",
  "docs/site/src/lib/ui-labels.ts", "docs/site/src/pages/[...slug]/index.md.ts",
  "docs/site/src/styles/prose.css", "docs/site/public/favicon.ico", "docs/site/public/fonts/Inter-Bold.ttf"];
const nativeFiles = [".github/scripts/ci-scope.mjs", ".github/scripts/ci-required.mjs", ".github/workflows/ci.yml",
  "docs/site/scripts/ci-scope.test.mjs", "docs/site/scripts/ci-workflow.test.mjs",
  "docs/site/scripts/release-version.test.mjs", "docs/site/scripts/database-reports.test.mjs",
  "docs/site/scripts/new-check.mjs", "docs/site/scripts/build.sh", "docs/site/astro.config.mjs",
  "docs/site/src/unknown.ts", "docs/site/src/lib/unknown.mjs", "docs/site/src/lib/example.cpp",
  "docs/site/src/components/CMakeLists.txt", "docs/site/public/native.hpp", "docs/site/public/unknown.bin",
  "docs/site/CMakeLists.txt", "docs/site/new.config.json", "docs/site/new-file"];

test("only reviewed documentation paths qualify for reduced native checks", () => {
  for (const file of ["README.md", "README_CN.md", "docs/usage_CN.md", "docs/site/src/styles/prose.css",
    "docs/performance/assets/chart.svg", "docs/images/overview.png", ...siteFiles]) {
    assert.equal(isDocumentationPath(file), true, file);
  }
  for (const file of ["CMakeLists.txt", "vcpkg.json", "include/arknet/tcp.hpp", "tests/tcp.cpp",
    "benchmarks/loopback.cpp", "scripts/format.sh", ".github/workflows/ci.yml", ".clang-format",
    "docs/example.cpp", "docs/link.hpp", ...nativeFiles,
    "docs/../include/lib.md", "docs/./usage.md", "/docs/usage.md", "docs\\usage.md", "", null]) {
    assert.equal(isDocumentationPath(file), false, String(file));
  }
});

test("site maintenance stays documentation-only at both stages without exempting native or CI paths", () => {
  for (const eventName of ["pull_request", "merge_group"]) {
    assert.deepEqual(planChecks({ eventName, known: true, files: siteFiles }), {
      full_cpp: false, tier: "documentation", source_tests: "", changed_files: siteFiles.length,
      reason: "documentation-only",
    });
    for (const file of [...nativeFiles, "tests/rudp.cpp", "docs/example.cpp", "docs/CMakeLists.txt"]) {
      const plan = planChecks({ eventName, known: true, files: [...siteFiles, file] });
      assert.equal(plan.full_cpp, true, `${eventName}: ${file}`);
      assert.equal(plan.tier, eventName === "pull_request" ? "modules" : "full", file);
      assert.equal(plan.source_tests, eventName === "pull_request" && file === "tests/rudp.cpp"
        ? [...moduleTests.rudp].sort().join(";") : "", file);
    }
    for (const changes of [{ known: false }, { known: "true" }, { files: [] }, { files: null },
      { files: [...siteFiles, undefined] }, { files: [...siteFiles, "docs/site/src/../lib/source.ts"] }]) {
      assert.equal(planChecks({ eventName, known: true, files: siteFiles, ...changes }).tier, "full");
    }
  }
  assert.equal(planChecks({ eventName: "workflow_dispatch", known: true, files: siteFiles }).tier, "full");
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
