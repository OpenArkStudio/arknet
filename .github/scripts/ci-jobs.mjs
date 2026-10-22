// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import { spawnSync } from 'node:child_process';
import { appendFileSync, rmSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { requiredJobNames, validateRequired, validationInput } from './ci-required.mjs';

export const collectionLimitMs = 120_000;
const delays = [1000, 2000, 4000, 8000, 15_000];
const pendingStatuses = new Set(['queued', 'pending', 'waiting', 'requested', 'in_progress']);
const terminalConclusions = new Set(['success', 'failure', 'skipped', 'cancelled', 'neutral', 'timed_out', 'action_required', 'startup_failure', 'stale']);
const pendingConclusion = value => value === '' || value === null || value === 'pending';

function identity(env) {
  if (!/^[1-9][0-9]*$/.test(env.GITHUB_RUN_ID ?? '') || !/^[1-9][0-9]*$/.test(env.GITHUB_RUN_ATTEMPT ?? '') ||
      !Number.isSafeInteger(Number(env.GITHUB_RUN_ID)) || !Number.isSafeInteger(Number(env.GITHUB_RUN_ATTEMPT)) ||
      !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(env.GITHUB_REPOSITORY ?? '') || !env.CI_JOBS_PATH) {
    throw new Error('Explicit run ID, current attempt, repository and metadata path are required');
  }
  return { run: Number(env.GITHUB_RUN_ID), attempt: Number(env.GITHUB_RUN_ATTEMPT) };
}

export function inspectSnapshot(metadata, expected) {
  if (metadata?.databaseId !== expected.run || metadata?.attempt !== expected.attempt || !Array.isArray(metadata?.jobs)) {
    throw new Error('Metadata does not identify the requested run and current attempt');
  }
  const names = new Set(), ids = new Set();
  let pending = false;
  for (const job of metadata.jobs) {
    if (!job || typeof job.name !== 'string' || names.has(job.name) || !Number.isSafeInteger(job.databaseId) ||
        job.databaseId <= 0 || ids.has(job.databaseId)) throw new Error('Missing or duplicate job identity');
    if (!requiredJobNames.includes(job.name) && job.name !== 'CI - required') throw new Error(`Unexpected job profile: ${job.name}`);
    names.add(job.name); ids.add(job.databaseId);
    // The aggregate is still executing this collector; its own state is not a prerequisite.
    if (job.name === 'CI - required') continue;
    if (!Array.isArray(job.steps) || (job.status === 'completed' && job.steps.length === 0)) throw new Error(`${job.name}: missing actual steps`);
    if (job.status === 'completed') {
      if (job.conclusion !== 'success' && !pendingConclusion(job.conclusion)) throw new Error(`${job.name}: prerequisite job did not succeed`);
      pending ||= pendingConclusion(job.conclusion);
    } else if (pendingStatuses.has(job.status)) pending = true;
    else throw new Error(`${job.name}: invalid job status`);
    const numbers = new Set();
    for (const step of job.steps) {
      if (!step || typeof step.name !== 'string' || !Number.isInteger(step.number) || step.number < 1 || numbers.has(step.number)) {
        throw new Error(`${job.name}: missing or duplicate step identity`);
      }
      numbers.add(step.number);
      if (step.status === 'completed') {
        if (pendingConclusion(step.conclusion)) pending = true;
        else if (!terminalConclusions.has(step.conclusion)) throw new Error(`${job.name}: invalid step conclusion`);
      } else if (pendingStatuses.has(step.status)) pending = true;
      else throw new Error(`${job.name}: invalid step status`);
    }
  }
  for (const name of requiredJobNames) if (!names.has(name)) {
    throw new Error(`Missing current-attempt prerequisite: ${name}; do not mix jobs from earlier attempts`);
  }
  return pending;
}

export async function collectJobs({ env = process.env, input, run = spawnSync, now = () => performance.now(),
  sleep = ms => new Promise(done => setTimeout(done, ms)), log = message => console.log(message) } = {}) {
  const expected = identity(env);
  const context = input ?? validationInput(env);
  rmSync(env.CI_JOBS_PATH, { force: true });
  const deadline = now() + collectionLimitMs;
  const args = ['run', 'view', env.GITHUB_RUN_ID, '--repo', env.GITHUB_REPOSITORY,
    '--attempt', env.GITHUB_RUN_ATTEMPT, '--json', 'databaseId,attempt,jobs'];
  let reads = 0;
  while (true) {
    const remaining = deadline - now();
    if (remaining <= 0) break;
    const result = run('gh', args, { env, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024,
      timeout: Math.min(20_000, Math.ceil(remaining)) });
    reads++;
    if (result.error || result.status !== 0) throw new Error(`Current-attempt gh run view failed (exit ${result.status ?? 'unavailable'})`);
    let metadata;
    try { metadata = JSON.parse(result.stdout); } catch { throw new Error('Invalid gh run metadata JSON'); }
    const pending = inspectSnapshot(metadata, expected);
    if (now() >= deadline) break;
    if (!pending) {
      const coverage = validateRequired({ ...context, metadata });
      writeFileSync(env.CI_JOBS_PATH, JSON.stringify(metadata));
      const summary = `Collected finalized run ${expected.run}, attempt ${expected.attempt}, after ${reads} snapshot(s); strict coverage ${JSON.stringify(coverage)}.`;
      log(summary);
      if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY, summary + '\n');
      return { metadata, coverage, reads };
    }
    const delay = Math.min(delays[Math.min(reads - 1, delays.length - 1)], deadline - now());
    if (delay <= 0) break;
    log(`Run ${expected.run}, attempt ${expected.attempt}: prerequisite job or step metadata is still pending; refreshing in ${delay} ms.`);
    await sleep(delay);
  }
  throw new Error(`Current-attempt prerequisite metadata did not finalize within ${collectionLimitMs / 1000} seconds`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { await collectJobs(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
