// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";
import { planChecks } from "../../../.github/scripts/ci-scope.mjs";

const workflow = readFileSync(new URL("../../../.github/workflows/ci.yml", import.meta.url), "utf8");
const block = name => workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(text => text.startsWith(`  ${name}:\n`));
const steps = name => block(name).split(/(?=^      - )/m).slice(1);
const stepName = step => step.match(/^      - name: (.+)$/m)?.[1] ??
  (step.match(/^      - uses: (.+)$/m)?.[1] ? `Run ${step.match(/^      - uses: (.+)$/m)[1]}` :
    `Run ${step.match(/(?:^      - |^        )run: (.+)$/m)?.[1]}`);
const outputs = plan => Object.fromEntries(["full_cpp", "tier", "source_tests"].map(key => [key, String(plan[key])]));
const nativeExpression = block("test").match(/ARKNET_RUN_NATIVE: \$\{\{ (.+) \}\}/)[1];
const jobNameExpression = block("test").match(/^    name: (.+)$/m)[1];

function actualGroups() {
  return [...block("test").matchAll(/- \{os: ([^,]+), boost: "([^"]+)", sanitize: "([^"]+)"\}/g)].map(([, os, boost, sanitize]) => ({ os, boost, sanitize }));
}
function actualCells() {
  return actualGroups().flatMap(group => (group.boost === "BOTH" ? ["OFF", "ON"] : [group.boost]).flatMap(boost =>
    (group.os === "ubuntu-latest" && group.sanitize === "OFF" ? ["ON", "OFF"] : ["ON"])
      .map(ssl => ({ ...group, boost, ssl }))));
}

function fixture(eventName, files, extra = {}) {
  const plan = planChecks({ eventName, known: true, files, ...extra });
  const needs = { docs: { result: "success" }, format: { result: "success", outputs: outputs(plan) }, test: { result: "success" } };
  const jobs = ["docs", "format"].map((name, index) => ({
    databaseId: index + 1, name: block(name).match(/^    name: (.+)$/m)[1], status: "completed", conclusion: "success",
    steps: steps(name).map((step, number) => ({ name: stepName(step), number: number + 1, status: "completed", conclusion: "success" })),
  }));
  for (const [index, matrix] of actualGroups().entries()) {
    const context = { matrix, needs, runner: { os: { "ubuntu-latest": "Linux", "macos-latest": "macOS", "windows-latest": "Windows" }[matrix.os] },
      always: () => true, steps: {
        "vcpkg-cache": { outputs: { enabled: "true" } },
        "compiler-cache": { outputs: { enabled: "true" } },
        "native-setup": { outcome: "success" },
        ...Object.fromEntries(["boost", "standalone"].flatMap(dep => ["tls", "notls"].flatMap(tls =>
          ["configure", "parallel", "build", "test"].map(stage => [`${dep}-${tls}-${stage}`, { outcome: "success" }])))),
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
const findStep = (job, name) => job.steps.find(step => step.name === name) ?? job.steps.find(step => step.name === `${name} - Boost - TLS`);

export { block, steps, stepName, actualCells, fixture, findStep };
