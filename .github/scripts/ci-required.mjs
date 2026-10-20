// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
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

function completed(job) {
  if (job.status !== "completed" || job.conclusion !== "success" || !Array.isArray(job.steps) || job.steps.length === 0) {
    throw new Error(`${job.name}: job must be completed successfully with actual steps`);
  }
  const numbers = new Set();
  for (const step of job.steps) {
    if (!step || typeof step.name !== "string" || !Number.isInteger(step.number) || step.number < 1 || numbers.has(step.number) ||
        step.status !== "completed" || !["success", "skipped"].includes(step.conclusion)) {
      throw new Error(`${job.name}: invalid, incomplete or unsuccessful step`);
    }
    numbers.add(step.number);
  }
}

function outcome(job, name, expected = "success") {
  const steps = job.steps.filter(step => step.name === name);
  if (steps.length !== 1 || steps[0].conclusion !== expected) {
    throw new Error(`${job.name}: ${name} must occur once with conclusion ${expected}`);
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
  const expectedNames = new Set(["Documentation checks", "Style - clang-format 18", ...expectedCells.map(cellName)]);
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
    completed(job);
  }
  const docs = jobs.get("Documentation checks");
  outcome(docs, "Run npm ci --prefix docs/site");
  outcome(docs, "Run npm run check --prefix docs/site");
  const format = jobs.get("Style - clang-format 18");
  for (const name of ["Run node --test docs/site/scripts/ci-scope.test.mjs", "Validate selective native check registration",
    "Run node .github/scripts/ci-scope.mjs", "Install clang-format 18", "Check project C++ formatting"]) outcome(format, name);

  let nativeCells = 0;
  for (const profile of expectedCells) {
    const job = jobs.get(cellName(profile));
    const native = plan.full_cpp && (plan.tier === "full" ||
      (profile.os === "ubuntu-latest" && profile.boost === "ON" && profile.ssl === "ON" && profile.sanitize === "OFF"));
    nativeCells += Number(native);
    outcome(job, "Validate scope result");
    outcome(job, "Record planned coverage", native ? "skipped" : "success");
    for (const name of ["Run actions/checkout@v4", "Run lukka/get-cmake@latest", "Run lukka/run-vcpkg@v11",
      "Configure", "Build planned native targets", "Upload test results"]) outcome(job, name, native ? "success" : "skipped");
    outcome(job, "Run ilammy/msvc-dev-cmd@v1", native && profile.os === "windows-latest" ? "success" : "skipped");
    outcome(job, "Configure TSan address space", native && profile.sanitize === "THREAD" ? "success" : "skipped");
    for (const [name, sanitizer] of [["Test", "OFF"], ["Test with sanitizers", "ON"], ["Test with thread sanitizer", "THREAD"]]) {
      outcome(job, name, native && profile.sanitize === sanitizer ? "success" : "skipped");
    }
    const benchmark = native && (plan.tier === "full" || plan.source_tests === "") &&
      profile.os === "ubuntu-latest" && profile.ssl === "ON" && profile.sanitize === "OFF";
    outcome(job, "Check benchmark batch runner and report calculations", benchmark ? "success" : "skipped");
  }
  return { tier: plan.tier, native_cells: nativeCells, scope_only_cells: expectedCells.length - nativeCells };
}

export function main(env = process.env) {
  const event = JSON.parse(readFileSync(env.GITHUB_EVENT_PATH, "utf8"));
  const plan = planChecks({ eventName: env.GITHUB_EVENT_NAME, ...changedFiles(env.GITHUB_EVENT_NAME, event) });
  const coverage = validateRequired({ needs: JSON.parse(env.CI_NEEDS_JSON),
    metadata: JSON.parse(readFileSync(env.CI_JOBS_PATH, "utf8")), eventName: env.GITHUB_EVENT_NAME, plan });
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
