// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { diagnosticLimits, snapshotDiagnostic } from '../../.github/scripts/ci-job-diagnostics.mjs';
import { fixture, findStep } from './fixtures/required-run.mjs';

const metadata = () => ({ ...fixture('merge_group', ['tests/rudp.cpp']).metadata, databaseId: 12345, attempt: 1 });
const native = value => value.jobs.find(job => job.name === 'Native - windows-latest - Boost - Release');
const diagnostic = (value, options = {}) => snapshotDiagnostic(value, { read: 1, elapsedMs: 12.9, ...options });
const parse = line => JSON.parse(line.slice('Prerequisite snapshot: '.length));

test('normalized state digest ignores read labels, payload extras, ordering and aggregate state', () => {
  const first = metadata(), second = structuredClone(first);
  second.jobs.reverse();
  for (const job of second.jobs) job.steps.reverse();
  second.jobs.find(job => job.name === 'CI - required').status = 'completed';
  second.jobs[0].secretPayload = 'DO_NOT_LOG';
  second.token = 'DO_NOT_LOG';
  const plain = parse(diagnostic(first)), anchored = parse(diagnostic(second, { read: 2, anchor: native(first).databaseId, elapsedMs: 100 }));
  assert.match(plain.digest, /^[a-f0-9]{64}$/);
  assert.equal(plain.digest, anchored.digest);
  assert.deepEqual({ mode: plain.mode, anchor: plain.anchor, elapsed: plain.elapsed_ms }, { mode: 'plain', anchor: null, elapsed: 12 });
  assert.deepEqual({ mode: anchored.mode, anchor: anchored.anchor, read: anchored.read, elapsed: anchored.elapsed_ms },
    { mode: 'anchored', anchor: native(first).databaseId, read: 2, elapsed: 100 });
  assert.deepEqual(plain.pending, []);
  assert.equal(diagnostic(second).includes('DO_NOT_LOG'), false);
});

test('changed job or step state changes the digest and identifies pending prerequisites', () => {
  const first = metadata(), changed = structuredClone(first), job = native(changed), step = findStep(job, 'Test');
  step.status = 'in_progress'; step.conclusion = '';
  const pending = parse(diagnostic(changed));
  assert.notEqual(pending.digest, parse(diagnostic(first)).digest);
  assert.deepEqual(pending.pending, [{ id: job.databaseId, name: job.name, status: 'completed', conclusion: 'success',
    pending_steps: 1, omitted_steps: 0, steps: [{ number: step.number, name: step.name, status: 'in_progress', conclusion: '' }] }]);
  job.status = 'in_progress'; job.conclusion = null;
  assert.notEqual(parse(diagnostic(changed)).digest, pending.digest);
  assert.equal(parse(diagnostic(changed)).pending[0].conclusion, null);
});

test('diagnostics bound text and pending steps, keep one safe line and hash omitted state', () => {
  const value = metadata();
  for (const job of value.jobs.filter(item => item.name !== 'CI - required')) {
    for (let index = 0; index < 200; index++) job.steps.push({ number: 1000 + index,
      name: `untrusted-${index}-` + '\\"\n\r\u001b::warning::'.repeat(100), status: 'pending', conclusion: null });
  }
  const line = diagnostic(value), result = parse(line);
  assert.equal(line.includes('\n'), false);
  assert.equal(line.includes('\r'), false);
  assert.equal(line.includes('\u001b'), false);
  assert.ok(Buffer.byteLength(line) < 16 * 1024);
  assert.equal(result.pending.length, 9);
  assert.equal(result.pending.reduce((sum, job) => sum + job.steps.length, 0), diagnosticLimits.steps);
  for (const job of result.pending) {
    assert.equal(job.pending_steps, 200);
    assert.equal(job.omitted_steps, 200 - job.steps.length);
    assert.ok(job.steps.length <= diagnosticLimits.stepsPerJob);
    for (const step of job.steps) assert.ok(step.name.length <= diagnosticLimits.text);
  }
  native(value).steps.at(-1).status = 'in_progress';
  assert.notEqual(parse(diagnostic(value)).digest, result.digest);
});

test('diagnostics exclude raw fields, timestamps, paths, URLs, environment and command output', () => {
  const value = metadata(), secret = 'SENSITIVE_RAW_PAYLOAD';
  for (const object of [value, native(value), findStep(native(value), 'Test')]) {
    Object.assign(object, { env: { GH_TOKEN: secret }, stdout: secret, stderr: secret,
      url: `https://example.invalid/${secret}`, startedAt: secret, path: `/${secret}`, raw: secret });
  }
  findStep(native(value), 'Test').status = 'pending';
  const line = diagnostic(value);
  assert.equal(line.includes(secret), false);
  assert.equal(line.includes('GH_TOKEN'), false);
  assert.deepEqual(Object.keys(parse(line)), ['run', 'attempt', 'read', 'mode', 'anchor', 'elapsed_ms', 'digest', 'pending']);
});
