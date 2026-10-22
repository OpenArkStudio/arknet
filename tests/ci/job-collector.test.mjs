// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { collectJobs, collectionLimitMs } from '../../.github/scripts/ci-jobs.mjs';
import { validateRequired } from '../../.github/scripts/ci-required.mjs';
import { fixture, findStep } from './fixtures/required-run.mjs';

function harness(t, event = 'merge_group', files = ['tests/rudp.cpp'], attempt = '1') {
  const directory = mkdtempSync(join(tmpdir(), 'arknet-job-collector-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  const env = { GITHUB_RUN_ID: '12345', GITHUB_RUN_ATTEMPT: attempt, GITHUB_REPOSITORY: 'OpenArkStudio/arknet',
    CI_JOBS_PATH: join(directory, 'jobs.json'), GITHUB_STEP_SUMMARY: join(directory, 'summary.md') };
  const input = fixture(event, files), snapshots = [], calls = [], waits = [], messages = [];
  const final = { ...input.metadata, databaseId: 12345, attempt: Number(attempt) };
  let elapsed = 0;
  return { env, input, final, calls, waits, messages, snapshots,
    collect: overrides => collectJobs({ env, input,
      now: () => elapsed, sleep: async ms => { waits.push(ms); elapsed += ms; }, log: message => messages.push(message),
      run: (command, args, options) => {
        calls.push({ command, args, options });
        const value = snapshots.length > 1 ? snapshots.shift() : snapshots[0] ?? final;
        return typeof value === 'function' ? value(options) : { status: 0, stdout: JSON.stringify(value) };
      }, ...overrides }) };
}
const windows = metadata => metadata.jobs.find(job => job.name === 'Native - windows-latest - Boost - Release');

test('attempt-qualified collection refreshes a successful job with stale test and post steps without modifying evidence', async t => {
  const h = harness(t), stale = structuredClone(h.final);
  findStep(windows(stale), 'Test').status = 'in_progress';
  findStep(windows(stale), 'Test').conclusion = '';
  windows(stale).steps.push({ name: 'Post Run actions/checkout@v4', number: 999, status: 'pending', conclusion: null });
  const final = structuredClone(h.final);
  windows(final).steps.push({ name: 'Post Run actions/checkout@v4', number: 999, status: 'completed', conclusion: 'success' });
  assert.throws(() => validateRequired({ ...h.input, metadata: stale }), /invalid, incomplete/);
  h.snapshots.push(stale, final);
  const result = await h.collect();
  assert.equal(result.reads, 2);
  assert.deepEqual(h.waits, [1000]);
  assert.deepEqual(result.metadata, final);
  assert.deepEqual(JSON.parse(readFileSync(h.env.CI_JOBS_PATH, 'utf8')), final);
  assert.deepEqual(result.coverage, { tier: 'full', native_cells: 12, scope_only_cells: 0 });
  assert.match(readFileSync(h.env.GITHUB_STEP_SUMMARY, 'utf8'), /run 12345, attempt 1, after 2 snapshot/);
  for (const call of h.calls) {
    assert.equal(call.command, 'gh');
    assert.deepEqual(call.args, ['run', 'view', '12345', '--repo', 'OpenArkStudio/arknet', '--attempt', '1', '--json', 'databaseId,attempt,jobs']);
    assert.ok(call.options.timeout > 0 && call.options.timeout <= 20_000);
    assert.equal(call.args.includes('api'), false);
  }
  assert.equal(findStep(windows(stale), 'Test').status, 'in_progress');
});

test('queued jobs and missing final conclusions refresh, while the currently running aggregate is excluded', async t => {
  const h = harness(t), queued = structuredClone(h.final), finishing = structuredClone(h.final);
  windows(queued).status = 'queued'; windows(queued).conclusion = ''; windows(queued).steps = [];
  windows(finishing).conclusion = '';
  findStep(windows(finishing), 'Test').conclusion = null;
  h.snapshots.push(queued, finishing, h.final);
  const result = await h.collect();
  assert.deepEqual(h.waits, [1000, 2000]);
  assert.equal(result.metadata.jobs.find(job => job.name === 'CI - required').status, 'in_progress');
});

test('repeated pending metadata exhausts the 120-second budget with bounded backoff and leaves no successful output', async t => {
  const h = harness(t), stale = structuredClone(h.final);
  findStep(windows(stale), 'Test').status = 'in_progress';
  h.snapshots.push(stale);
  writeFileSync(h.env.CI_JOBS_PATH, 'stale previous success');
  await assert.rejects(h.collect(), /did not finalize within 120 seconds/);
  assert.deepEqual(h.waits.slice(0, 5), [1000, 2000, 4000, 8000, 15_000]);
  assert.equal(h.waits.reduce((sum, ms) => sum + ms, 0), collectionLimitMs);
  assert.ok(h.calls.length <= 12);
  assert.equal(existsSync(h.env.CI_JOBS_PATH), false);
});

test('GH command duration consumes the same budget and cannot obtain acceptance after the deadline', async t => {
  const h = harness(t);
  let elapsed = 0;
  await assert.rejects(h.collect({ now: () => elapsed, sleep: async () => assert.fail('no sleep after deadline'),
    run: (_, __, options) => {
      assert.equal(options.timeout, 20_000);
      elapsed += collectionLimitMs;
      return { status: 0, stdout: JSON.stringify(h.final) };
    } }), /did not finalize/);
  assert.equal(existsSync(h.env.CI_JOBS_PATH), false);
});

test('the final CLI read is capped by the remaining overall budget', async t => {
  const h = harness(t), stale = structuredClone(h.final), timeouts = [];
  findStep(windows(stale), 'Test').status = 'in_progress';
  let elapsed = 0;
  await assert.rejects(h.collect({ now: () => elapsed, sleep: async ms => { elapsed += ms; },
    run: (_, __, options) => {
      timeouts.push(options.timeout);
      elapsed += 10_000;
      return { status: 0, stdout: JSON.stringify(stale) };
    } }), /did not finalize/);
  assert.ok(timeouts.every(ms => ms > 0 && ms <= 20_000));
  assert.equal(timeouts.at(-1), 15_000);
  assert.equal(elapsed, collectionLimitMs);
});

test('missing, duplicate and unexpected jobs or steps fail immediately instead of refreshing into success', async t => {
  const changes = [
    metadata => { metadata.jobs.splice(0, 1); },
    metadata => { metadata.jobs.push(structuredClone(metadata.jobs[0])); },
    metadata => { metadata.jobs[0].databaseId = metadata.jobs[1].databaseId; },
    metadata => { metadata.jobs[0].name = 'unexpected native profile'; },
    metadata => { metadata.jobs[0].steps = []; },
    metadata => { metadata.jobs[0].steps[0].status = 'unknown'; },
    metadata => { metadata.jobs[0].steps[0].conclusion = 'unknown'; },
    metadata => { metadata.jobs[0].steps.push({ ...metadata.jobs[0].steps[0] }); },
    metadata => { windows(metadata).steps = windows(metadata).steps.filter(step => step.name !== 'Test - Boost - TLS'); },
    metadata => { metadata.jobs[0].conclusion = 'failure'; },
    metadata => { findStep(windows(metadata), 'Test').conclusion = 'failure'; },
  ];
  for (const change of changes) {
    const h = harness(t), invalid = structuredClone(h.final);
    change(invalid); h.snapshots.push(invalid, h.final);
    await assert.rejects(h.collect());
    assert.equal(h.calls.length, 1);
    assert.deepEqual(h.waits, []);
    assert.equal(existsSync(h.env.CI_JOBS_PATH), false);
  }
});

test('current attempts never substitute the default snapshot, earlier attempts or unchanged jobs from prior attempts', async t => {
  for (const change of [metadata => { metadata.attempt = 1; }, metadata => { delete metadata.attempt; },
    metadata => { metadata.databaseId++; }, metadata => { metadata.jobs = [metadata.jobs.at(-1)]; }]) {
    const h = harness(t, 'merge_group', ['tests/rudp.cpp'], '2'), invalid = structuredClone(h.final);
    change(invalid); h.snapshots.push(invalid, h.final);
    await assert.rejects(h.collect());
    assert.equal(h.calls.length, 1);
    assert.deepEqual(h.calls[0].args.slice(5, 7), ['--attempt', '2']);
    assert.equal(existsSync(h.env.CI_JOBS_PATH), false);
  }
  const h = harness(t, 'merge_group', ['tests/rudp.cpp'], '2');
  const result = await h.collect();
  assert.equal(result.metadata.attempt, 2);
});

test('GH errors, command timeouts and invalid JSON are failures without fallback or leaked stderr', async t => {
  for (const response of [{ status: 1, stdout: '', stderr: 'secret diagnostic fixture' },
    { status: null, error: new Error('ETIMEDOUT'), stdout: '' }, { status: 0, stdout: '{not JSON' }]) {
    const h = harness(t);
    h.snapshots.push(() => response);
    await assert.rejects(h.collect(), error => !error.message.includes('secret diagnostic fixture'));
    assert.equal(h.calls.length, 1);
    assert.deepEqual(h.waits, []);
    assert.equal(existsSync(h.env.CI_JOBS_PATH), false);
  }
});

test('current-attempt collection preserves source, documentation and optional-cache validation', async t => {
  for (const [event, files, native] of [['pull_request', ['tests/rudp.cpp'], 1], ['merge_group', ['README.md'], 0]]) {
    const h = harness(t, event, files);
    assert.equal((await h.collect()).coverage.native_cells, native);
  }
  const h = harness(t);
  const native = h.final.jobs.find(job => job.name === 'Native - ubuntu-latest - Boost - Release');
  findStep(native, 'Restore dependency archives').conclusion = 'failure';
  assert.equal((await h.collect()).coverage.native_cells, 12);
});

test('missing current-attempt identity cannot launch GH or reuse a previous metadata file', async t => {
  for (const key of ['GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'GITHUB_REPOSITORY', 'CI_JOBS_PATH']) {
    const h = harness(t), env = { ...h.env };
    delete env[key];
    await assert.rejects(h.collect({ env }), /required/);
    assert.equal(h.calls.length, 0);
  }
});
