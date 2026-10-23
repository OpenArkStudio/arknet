// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import { createHash } from 'node:crypto';
import { requiredJobNames } from './ci-required.mjs';

export const diagnosticLimits = Object.freeze({ text: 96, stepsPerJob: 4, steps: 16 });
const pendingConclusion = value => value === '' || value === null || value === 'pending';
const unfinished = value => value.status !== 'completed' || pendingConclusion(value.conclusion);
// ASCII and JSON encoding keep control characters and workflow commands off the log line.
const text = (value, limit = diagnosticLimits.text) => value === null ? null
  : value.slice(0, limit).replace(/[^\x20-\x7e]/g, '?');

// Call only after snapshot structure, run/attempt and prerequisite provenance checks.
export function snapshotDiagnostic(metadata, { read, anchor = null, elapsedMs }) {
  const state = requiredJobNames.map(name => {
    const job = metadata.jobs.find(item => item.name === name);
    return { id: job.databaseId, name, status: job.status, conclusion: job.conclusion,
      steps: job.steps.map(step => ({ number: step.number, name: step.name,
        status: step.status, conclusion: step.conclusion })).sort((a, b) => a.number - b.number) };
  });
  // Hash all whitelisted state, before display truncation; ignore payload extras and the aggregate.
  const digest = createHash('sha256').update(JSON.stringify(state)).digest('hex');
  let remainingSteps = diagnosticLimits.steps;
  const pending = state.filter(job => unfinished(job) || job.steps.some(unfinished)).map(job => {
    const steps = job.steps.filter(unfinished);
    const shown = steps.slice(0, Math.min(diagnosticLimits.stepsPerJob, remainingSteps));
    remainingSteps -= shown.length;
    return { id: job.id, name: text(job.name), status: text(job.status, 24), conclusion: text(job.conclusion, 24),
      pending_steps: steps.length, omitted_steps: steps.length - shown.length,
      steps: shown.map(step => ({ number: step.number, name: text(step.name),
        status: text(step.status, 24), conclusion: text(step.conclusion, 24) })) };
  });
  return `Prerequisite snapshot: ${JSON.stringify({ run: metadata.databaseId, attempt: metadata.attempt,
    read, mode: anchor === null ? 'plain' : 'anchored', anchor,
    elapsed_ms: Math.max(0, Math.floor(elapsedMs)), digest, pending })}`;
}
