import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { test } from "node:test";

const workflow = readFileSync(new URL("../../.github/workflows/ci.yml", import.meta.url), "utf8");
const step = workflow.slice(workflow.indexOf("      - name: Validate scope result"), workflow.indexOf("      - name: Record planned coverage"));
const marker = "        run: |\n";
assert.ok(step.includes(marker));
const script = step.slice(step.indexOf(marker) + marker.length).split("\n").map(line => line.slice(10)).join("\n");
const shell = spawnSync("pwsh", ["-NoProfile", "-NonInteractive", "-Command", "$PSVersionTable.PSVersion.ToString()"], { encoding: "utf8", timeout: 15_000 });

test("actual PowerShell scope guard rejects invalid and filtered queue plans", { skip: shell.error?.code === "ENOENT" && !process.env.GITHUB_ACTIONS }, () => {
  assert.equal(shell.status, 0, shell.stderr || shell.error?.message);
  const base = { ARKNET_SCOPE_RESULT: "success", ARKNET_FULL_CPP: "true", ARKNET_VALIDATION_TIER: "full", GITHUB_EVENT_NAME: "merge_group", ARKNET_SOURCE_TESTS: "" };
  const cases = [
    [true, {}], [true, { ARKNET_SOURCE_TESTS: undefined }],
    [true, { ARKNET_VALIDATION_TIER: "modules", GITHUB_EVENT_NAME: "pull_request", ARKNET_SOURCE_TESTS: "rudp;fec" }],
    [true, { ARKNET_VALIDATION_TIER: "documentation", ARKNET_FULL_CPP: "false" }],
    [false, { ARKNET_SCOPE_RESULT: "failure" }], [false, { ARKNET_SCOPE_RESULT: "cancelled" }],
    [false, { ARKNET_SCOPE_RESULT: undefined }], [false, { ARKNET_FULL_CPP: undefined }],
    [false, { ARKNET_FULL_CPP: "TRUE" }], [false, { ARKNET_VALIDATION_TIER: "unknown" }],
    [false, { ARKNET_VALIDATION_TIER: "modules" }], [false, { ARKNET_SOURCE_TESTS: "rudp" }],
    [false, { ARKNET_FULL_CPP: "false" }], [false, { ARKNET_VALIDATION_TIER: "documentation" }],
    [false, { GITHUB_EVENT_NAME: "workflow_dispatch", ARKNET_VALIDATION_TIER: "modules" }],
  ];
  for (const [passes, changes] of cases) {
    const env = { ...process.env, ...base, ...changes };
    for (const key of Object.keys(env)) if (env[key] === undefined) delete env[key];
    const result = spawnSync("pwsh", ["-NoProfile", "-NonInteractive", "-Command", script], { env, encoding: "utf8", timeout: 15_000 });
    assert.equal(result.status === 0, passes, JSON.stringify(changes) + "\n" + result.stdout + result.stderr);
  }
});
