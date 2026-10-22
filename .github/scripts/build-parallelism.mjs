// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import os from "node:os";
import { appendFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const gib = 1024 ** 3;
export const admission = { cpu: 4, effectiveMemory: 15 * gib, availableMemory: 12 * gib };
const positiveInteger = value => Number.isSafeInteger(value) && value > 0;

export function selectParallelism(resources) {
  const validConstraint = Number.isFinite(resources.constrainedMemory) &&
    Number.isInteger(resources.constrainedMemory) && resources.constrainedMemory >= 0;
  let constraintStatus = resources.constrainedMemory === 0 ? "no-reported-constraint" :
    validConstraint ? "reported-constraint" : "invalid-constraint";
  const fallback = reason => ({ parallel: 1, reason, effectiveMemory: null, constraintStatus });
  if (resources.platform !== "linux" || !["OFF", "ON"].includes(resources.boost) || resources.sanitize !== "ON") {
    return fallback("outside-experimental-profile");
  }
  if (![resources.cpu, resources.totalMemory, resources.availableMemory].every(positiveInteger) ||
      !validConstraint) {
    return fallback("unknown-or-invalid-resource");
  }
  const effectiveMemory = resources.constrainedMemory === 0 ? resources.totalMemory :
    Math.min(resources.totalMemory, resources.constrainedMemory);
  if (!positiveInteger(effectiveMemory)) return fallback("unknown-or-invalid-resource");
  if (resources.constrainedMemory > resources.totalMemory) constraintStatus = "reported-above-host-memory";
  if (resources.availableMemory > effectiveMemory) return fallback("inconsistent-memory-observation");
  const eligible = resources.cpu >= admission.cpu && effectiveMemory >= admission.effectiveMemory &&
    resources.availableMemory >= admission.availableMemory;
  return { parallel: eligible ? 2 : 1, reason: eligible ? "experimental-resource-threshold-met" : "below-resource-threshold", effectiveMemory, constraintStatus };
}

export function observeResources(env, system = os, runtime = process) {
  const read = fn => { try { return fn(); } catch { return null; } };
  return { platform: runtime.platform, boost: env.MATRIX_BOOST, sanitize: env.MATRIX_SANITIZE,
    cpu: read(() => system.availableParallelism()), totalMemory: read(() => system.totalmem()),
    constrainedMemory: read(() => runtime.constrainedMemory()), availableMemory: read(() => runtime.availableMemory()) };
}

export function main(env = process.env) {
  if (!env.GITHUB_OUTPUT) throw new Error("GITHUB_OUTPUT is required for the compiler parallelism decision");
  const resources = observeResources(env);
  const decision = selectParallelism(resources);
  appendFileSync(env.GITHUB_OUTPUT, `parallel=${decision.parallel}\n`);
  const observation = JSON.stringify({ resources, admission, ...decision });
  console.log(observation);
  if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY,
    `Compiler parallelism decision: ${observation}\n\nExperimental scheduling heuristic; compiler memory peaks and speedup are not established.\n`);
  return decision;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
