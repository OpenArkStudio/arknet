// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { changedFiles, planChecks } from "./ci-scope.mjs";

const cell = (os, boost, ssl, sanitize) => ({ os, boost, ssl, sanitize });
export const expectedCells = [
  ...["ubuntu-latest", "macos-latest", "windows-latest"].flatMap(os =>
    ["OFF", "ON"].map(boost => cell(os, boost, "ON", "OFF"))),
  ...["OFF", "ON"].map(boost => cell("ubuntu-latest", boost, "OFF", "OFF")),
  ...["OFF", "ON"].flatMap(boost => ["ON", "THREAD"].map(sanitize => cell("ubuntu-latest", boost, "ON", sanitize))),
];
export const cellName = ({ os, boost, ssl, sanitize }) =>
  `Test - ${os} - ${boost === "ON" ? "Boost" : "Standalone"} - ${ssl === "ON" ? "TLS" : "NoTLS"} - ${sanitize === "THREAD" ? "TSan" : sanitize === "ON" ? "ASan+UBSan" : "Release"}`;

export const expectedGroups = [
  ...["ON", "OFF"].map(boost => ({ os: "ubuntu-latest", boost, sanitize: "OFF" })),
  { os: "macos-latest", boost: "BOTH", sanitize: "OFF" },
  ...["ON", "OFF"].map(boost => ({ os: "windows-latest", boost, sanitize: "OFF" })),
  ...["ON", "THREAD"].map(sanitize => ({ os: "ubuntu-latest", boost: "BOTH", sanitize })),
];
export const groupName = ({ os, boost, sanitize }) =>
  `Native - ${os} - ${boost === "ON" ? "Boost - " : boost === "OFF" ? "Standalone - " : ""}${sanitize === "THREAD" ? "TSan" : sanitize === "ON" ? "ASan+UBSan" : "Release"}`;
export const requiredJobNames = ["Documentation checks", "Style - clang-format 18", ...expectedGroups.map(groupName)];
export const inGroup = (profile, group) => profile.os === group.os && profile.sanitize === group.sanitize &&
  (group.boost === "BOTH" || profile.boost === group.boost);
export const slotName = ({ boost, ssl }) => `${boost === "ON" ? "Boost" : "Standalone"} - ${ssl === "ON" ? "TLS" : "NoTLS"}`;
const slots = ["ON", "OFF"].flatMap(boost => ["ON", "OFF"].map(ssl => ({ boost, ssl })));

export const optionalCacheSteps = [
  "Restore dependency archives", "Install compiler cache", "Restore compiler cache", "Report cache statistics",
];
const cacheActions = optionalCacheSteps.slice(0, 3);
const optionalCacheFailures = new Set([...optionalCacheSteps, ...cacheActions.map(name => `Post ${name}`)]);
const runNative = (plan, profile) => plan.full_cpp && (plan.tier === "full" ||
  (profile.os === "ubuntu-latest" && profile.boost === "ON" && profile.ssl === "ON" && profile.sanitize === "OFF"));

function completed(job, allowCacheFailure = false) {
  if (job.status !== "completed" || job.conclusion !== "success" || !Array.isArray(job.steps) || job.steps.length === 0) {
    throw new Error(`${job.name}: job must be completed successfully with actual steps`);
  }
  const numbers = new Set();
  for (const step of job.steps) {
    if (!step || typeof step.name !== "string" || !Number.isInteger(step.number) || step.number < 1 || numbers.has(step.number) ||
        step.status !== "completed" || (!["success", "skipped"].includes(step.conclusion) &&
          !(allowCacheFailure && step.conclusion === "failure" && optionalCacheFailures.has(step.name)))) {
      throw new Error(`${job.name}: invalid, incomplete or unsuccessful step`);
    }
    numbers.add(step.number);
  }
}

function outcome(job, name, expected = "success") {
  const steps = job.steps.filter(step => step.name === name);
  const allowed = Array.isArray(expected) ? expected : [expected];
  if (steps.length !== 1 || !allowed.includes(steps[0].conclusion)) {
    throw new Error(`${job.name}: ${name} must occur once with conclusion ${allowed.join(" or ")}`);
  }
  return steps[0].conclusion;
}

function cacheOutcomes(job, native) {
  for (const name of ["Run actions/setup-node@v4", "Prepare dependency archive cache", "Prepare compiler launcher"]) {
    outcome(job, name, native ? "success" : "skipped");
  }
  const installed = outcome(job, "Install compiler cache", native ? ["success", "failure"] : "skipped");
  outcome(job, "Report cache statistics", native ? ["success", "failure"] : "skipped");
  outcome(job, "Restore dependency archives", native ? ["success", "skipped", "failure"] : "skipped");
  const restored = outcome(job, "Restore compiler cache", native ? ["success", "skipped", "failure"] : "skipped");
  outcome(job, "Start compiler cache after archive restore", restored === "skipped" ? "skipped" : "success");
  if (installed === "failure" && restored !== "skipped") {
    throw new Error(`${job.name}: a failed compiler cache installation must use the direct compiler`);
  }
  for (const name of cacheActions) {
    const post = job.steps.filter(step => step.name === `Post ${name}`);
    if (post.length > 0) {
      const ran = native && job.steps.find(step => step.name === name).conclusion !== "skipped";
      outcome(job, `Post ${name}`, ran ? ["success", "skipped", "failure"] : "skipped");
    }
  }
}

export function validateRequired({ needs, metadata, eventName, plan }) {
  if (!["pull_request", "merge_group", "workflow_dispatch"].includes(eventName)) throw new Error("Unsupported workflow event");
  for (const name of ["docs", "format", "test"]) {
    if (needs?.[name]?.result !== "success") throw new Error(`${name}: prerequisite job did not succeed`);
  }
  if (!plan || typeof plan.full_cpp !== "boolean" || !["full", "modules", "documentation"].includes(plan.tier) ||
      typeof plan.source_tests !== "string" || (plan.full_cpp === false) !== (plan.tier === "documentation") ||
      (plan.tier === "modules" && eventName !== "pull_request") ||
      (plan.tier !== "modules" && plan.source_tests !== "") ||
      (eventName === "workflow_dispatch" && plan.tier !== "full")) throw new Error("Invalid actual validation plan");
  for (const key of ["full_cpp", "tier", "source_tests"]) {
    const actual = needs.format.outputs?.[key];
    // Actions may omit an empty job output. Only an actually empty selection
    // permits that omission; it can never grant a narrower native plan.
    if (actual !== String(plan[key]) && !(key === "source_tests" && plan[key] === "" && actual === undefined)) {
      throw new Error(`Scope output does not match the current event: ${key}`);
    }
  }
  if (!Array.isArray(metadata?.jobs)) throw new Error("Missing same-run job metadata");
  const jobs = new Map();
  const identities = new Set();
  const expectedNames = new Set(requiredJobNames);
  for (const job of metadata.jobs) {
    if (!job || typeof job.name !== "string" || jobs.has(job.name) || !Number.isSafeInteger(job.databaseId) ||
        job.databaseId <= 0 || identities.has(job.databaseId)) throw new Error("Missing or duplicate job identity");
    if (!expectedNames.has(job.name) && job.name !== "CI - required") throw new Error(`Unexpected job profile: ${job.name}`);
    jobs.set(job.name, job);
    identities.add(job.databaseId);
  }
  for (const name of expectedNames) {
    const job = jobs.get(name);
    if (!job) throw new Error(`Missing required job profile: ${name}`);
    const group = expectedGroups.find(group => groupName(group) === name);
    completed(job, group !== undefined && expectedCells.some(profile => inGroup(profile, group) && runNative(plan, profile)));
  }
  const docs = jobs.get("Documentation checks");
  outcome(docs, "Run npm ci --prefix docs/site");
  outcome(docs, "Run npm run check --prefix docs/site");
  const format = jobs.get("Style - clang-format 18");
  for (const name of ["Run node --test docs/site/scripts/ci-scope.test.mjs", "Validate selective native check registration",
    "Run node .github/scripts/ci-scope.mjs", "Install clang-format 18", "Check project C++ formatting"]) outcome(format, name);

  let nativeCells = 0;
  for (const group of expectedGroups) {
    const job = jobs.get(groupName(group));
    const native = expectedCells.some(profile => inGroup(profile, group) && runNative(plan, profile));
    cacheOutcomes(job, native);
    outcome(job, "Validate scope result");
    outcome(job, "Record planned coverage", native ? "skipped" : "success");
    for (const name of ["Run actions/checkout@v4", "Run lukka/get-cmake@latest", "Run lukka/run-vcpkg@v11", "Upload test results"]) {
      outcome(job, name, native ? "success" : "skipped");
    }
    outcome(job, "Run ilammy/msvc-dev-cmd@v1", native && group.os === "windows-latest" ? "success" : "skipped");
    outcome(job, "Configure TSan address space", native && group.sanitize === "THREAD" ? "success" : "skipped");
    outcome(job, "Prepare benchmark tools", native && group.os === "ubuntu-latest" && group.sanitize === "OFF" &&
      (plan.tier === "full" || plan.source_tests === "") ? "success" : "skipped");
    for (const slot of slots) {
      const profile = { ...group, ...slot };
      const available = inGroup(profile, group) && expectedCells.some(cell => Object.keys(cell).every(key => cell[key] === profile[key]));
      const active = available && runNative(plan, profile);
      nativeCells += Number(active);
      const suffix = ` - ${slotName(slot)}`;
      outcome(job, `Record planned coverage${suffix}`, active ? "skipped" : "success");
      for (const name of ["Configure", "Build planned native targets", "Test"]) outcome(job, name + suffix, active ? "success" : "skipped");
      if (slot.ssl === "ON") outcome(job, "Select bounded compiler parallelism" + suffix, active && group.sanitize === "ON" ? "success" : "skipped");
      const benchmark = active && (plan.tier === "full" || plan.source_tests === "") &&
        group.os === "ubuntu-latest" && slot.ssl === "ON" && group.sanitize === "OFF";
      if (slot.ssl === "ON") outcome(job, "Check benchmark batch runner and report calculations" + suffix, benchmark ? "success" : "skipped");
    }
  }
  return { tier: plan.tier, native_cells: nativeCells, scope_only_cells: expectedCells.length - nativeCells };
}

export function validationInput(env = process.env) {
  const event = JSON.parse(readFileSync(env.GITHUB_EVENT_PATH, "utf8"));
  const plan = planChecks({ eventName: env.GITHUB_EVENT_NAME, ...changedFiles(env.GITHUB_EVENT_NAME, event) });
  return { needs: JSON.parse(env.CI_NEEDS_JSON), eventName: env.GITHUB_EVENT_NAME, plan };
}

export function main(env = process.env) {
  const coverage = validateRequired({ ...validationInput(env),
    metadata: JSON.parse(readFileSync(env.CI_JOBS_PATH, "utf8")) });
  console.log(JSON.stringify(coverage));
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    main();
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
