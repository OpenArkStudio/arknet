// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import { spawnSync } from 'node:child_process';
import { appendFileSync, rmSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { optionalCacheSteps, requiredJobNames, slotName, validateRequired, validationInput } from './ci-required.mjs';
import { snapshotDiagnostic } from './ci-job-diagnostics.mjs';

export const collectionLimitMs = 120_000;
const delays = [1000, 2000, 4000, 8000, 15_000];
const pendingStatuses = new Set(['queued', 'pending', 'waiting', 'requested', 'in_progress']);
const terminalConclusions = new Set(['success', 'failure', 'skipped', 'cancelled', 'neutral', 'timed_out', 'action_required', 'startup_failure', 'stale']);
const pendingConclusion = value => value === '' || value === null || value === 'pending';

const optionalCacheFailures = new Set([...optionalCacheSteps, ...optionalCacheSteps.slice(0, 3).map(name => `Post ${name}`)]);
const requiredSteps = name => name === 'Documentation checks' ? ['Run npm ci --prefix docs/site', 'Run npm run check --prefix docs/site']
  : name === 'Style - clang-format 18' ? ['Run node --test docs/site/scripts/ci-scope.test.mjs', 'Validate selective native check registration',
    'Run node .github/scripts/ci-scope.mjs', 'Install clang-format 18', 'Check project C++ formatting']
    : ['Validate scope result', 'Record planned coverage', 'Run actions/checkout@v4', 'Run actions/setup-node@v4',
      'Run ilammy/msvc-dev-cmd@v1', 'Run lukka/get-cmake@latest', 'Run lukka/run-vcpkg@v11', 'Upload test results',
      'Prepare dependency archive cache', 'Prepare compiler launcher', 'Start compiler cache after archive restore',
      'Configure TSan address space', 'Prepare benchmark tools', ...optionalCacheSteps,
      ...['ON', 'OFF'].flatMap(boost => ['ON', 'OFF'].flatMap(ssl => {
        const suffix = ` - ${slotName({ boost, ssl })}`;
        return ['Record planned coverage', 'Configure', 'Build planned native targets', 'Test',
          ...(ssl === 'ON' ? ['Select bounded compiler parallelism', 'Check benchmark batch runner and report calculations'] : [])]
          .map(stage => stage + suffix);
      }))];
const unfinished = job => job.status !== 'completed' || pendingConclusion(job.conclusion) ||
  job.steps.some(step => step.status !== 'completed' || pendingConclusion(step.conclusion));

function identity(env) {
  if (!/^[1-9][0-9]*$/.test(env.GITHUB_RUN_ID ?? '') || !/^[1-9][0-9]*$/.test(env.GITHUB_RUN_ATTEMPT ?? '') ||
      !Number.isSafeInteger(Number(env.GITHUB_RUN_ID)) || !Number.isSafeInteger(Number(env.GITHUB_RUN_ATTEMPT)) ||
      !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(env.GITHUB_REPOSITORY ?? '') || !env.CI_JOBS_PATH) {
    throw new Error('Explicit run ID, current attempt, repository and metadata path are required');
  }
  return { run: Number(env.GITHUB_RUN_ID), attempt: Number(env.GITHUB_RUN_ATTEMPT) };
}

export function inspectSnapshot(metadata, expected, context) {
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
    if (job.conclusion !== 'success' && !pendingConclusion(job.conclusion)) throw new Error(`${job.name}: prerequisite job did not succeed`);
    if (job.status === 'completed') {
      pending ||= pendingConclusion(job.conclusion);
    } else if (pendingStatuses.has(job.status)) pending = true;
    else throw new Error(`${job.name}: invalid job status`);
    const numbers = new Set(), stepNames = new Set();
    const native = job.name.startsWith('Native - ') && context?.plan?.full_cpp &&
      (context.plan.tier === 'full' || (context.plan.tier === 'modules' && job.name === 'Native - ubuntu-latest - Boost - Release'));
    for (const step of job.steps) {
      if (!step || typeof step.name !== 'string' || !Number.isInteger(step.number) || step.number < 1 || numbers.has(step.number) || stepNames.has(step.name)) {
        throw new Error(`${job.name}: missing or duplicate step identity`);
      }
      numbers.add(step.number); stepNames.add(step.name);
      if (!pendingConclusion(step.conclusion) && !terminalConclusions.has(step.conclusion)) throw new Error(`${job.name}: invalid step conclusion`);
      if (!pendingConclusion(step.conclusion) && !['success', 'skipped'].includes(step.conclusion) &&
          !(native && step.conclusion === 'failure' && optionalCacheFailures.has(step.name))) {
        throw new Error(`${job.name}: prerequisite step did not succeed`);
      }
      if (step.status === 'completed') pending ||= pendingConclusion(step.conclusion);
      else if (pendingStatuses.has(step.status)) pending = true;
      else throw new Error(`${job.name}: invalid step status`);
    }
    // Completed jobs must already expose the whole named structure, even if a conclusion is stale.
    if (job.status === 'completed') for (const name of requiredSteps(job.name)) {
      if (!stepNames.has(name)) throw new Error(`${job.name}: missing required step ${name}`);
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
  const startedAt = now(), deadline = startedAt + collectionLimitMs;
  let reads = 0, rounds = 0, provenance;
  const read = jobId => {
    const remaining = deadline - now();
    if (remaining <= 0) return null;
    const args = ['run', 'view', env.GITHUB_RUN_ID, '--repo', env.GITHUB_REPOSITORY,
      '--attempt', env.GITHUB_RUN_ATTEMPT, ...(jobId ? ['--job', String(jobId)] : []), '--json', 'databaseId,attempt,jobs'];
    const result = run('gh', args, { env, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024,
      timeout: Math.min(20_000, Math.ceil(remaining)) });
    reads++;
    if (result.error || result.status !== 0) throw new Error(`Current-attempt gh run view failed (exit ${result.status ?? 'unavailable'})`);
    let metadata;
    try { metadata = JSON.parse(result.stdout); } catch { throw new Error('Invalid gh run metadata JSON'); }
    const pending = inspectSnapshot(metadata, expected, context);
    const identities = new Map(metadata.jobs.filter(job => requiredJobNames.includes(job.name)).map(job => [job.name, job.databaseId]));
    if (provenance) {
      for (const [name, id] of provenance) if (identities.get(name) !== id) {
        throw new Error(`${name}: current-attempt prerequisite job identity changed between snapshots`);
      }
    } else provenance = identities;
    log(snapshotDiagnostic(metadata, { read: reads, anchor: jobId ?? null, elapsedMs: now() - startedAt }));
    return { metadata, pending };
  };
  const save = metadata => {
    const coverage = validateRequired({ ...context, metadata });
    writeFileSync(env.CI_JOBS_PATH, JSON.stringify(metadata));
    const summary = `Collected finalized run ${expected.run}, attempt ${expected.attempt}, after ${reads} snapshot(s); strict coverage ${JSON.stringify(coverage)}.`;
    log(summary);
    if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY, summary + '\n');
    return { metadata, coverage, reads };
  };
  while (true) {
    const plain = read();
    if (!plain || now() >= deadline) break;
    if (!plain.pending) return save(plain.metadata);
    // --job changes CLI snapshot retrieval, not the run/attempt or the full validation scope.
    // Rotate only anchor selection; no readiness state is carried between snapshots.
    const candidates = requiredJobNames.map(name => plain.metadata.jobs.find(job => job.name === name)).filter(unfinished);
    const anchor = candidates[rounds % candidates.length];
    const anchored = read(anchor.databaseId);
    if (!anchored || now() >= deadline) break;
    if (!anchored.pending) return save(anchored.metadata);
    const delay = Math.min(delays[Math.min(rounds++, delays.length - 1)], deadline - now());
    if (delay <= 0) break;
    log(`Run ${expected.run}, attempt ${expected.attempt}: both snapshot reads still contain pending prerequisites; refreshing in ${delay} ms.`);
    await sleep(delay);
  }
  throw new Error(`Current-attempt prerequisite metadata did not finalize within ${collectionLimitMs / 1000} seconds`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { await collectJobs(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
