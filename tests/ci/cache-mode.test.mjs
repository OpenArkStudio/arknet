// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { runInNewContext } from 'node:vm';
import { archiveSelection, cacheMode } from '../../.github/scripts/cache-mode.mjs';
import { cachePlan, prepareCache } from '../../.github/scripts/vcpkg-cache.mjs';
import { prepareCompilerCache, sccacheVersion } from '../../.github/scripts/compiler-cache.mjs';
import { expectedCells, expectedGroups } from '../../.github/scripts/ci-required.mjs';
import { profilePlan } from '../../.github/scripts/native-profile.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const workflow = readFileSync(join(root, '.github/workflows/ci.yml'), 'utf8');
const base = { GITHUB_EVENT_NAME: 'workflow_dispatch', GITHUB_RUN_ID: '1001', GITHUB_RUN_ATTEMPT: '1',
  RUNNER_OS: 'Linux', RUNNER_ARCH: 'X64', RUNNER_TEMP: '/tmp/arknet-cache-mode',
  ImageOS: 'ubuntu24', ImageVersion: '20261022.1', VCPKG_CACHE_REVISION: '0699a19d0c6386247ce50d4dedbb8217d484d536',
  VCPKG_CACHE_FEATURES: 'boost;ssl', VCPKG_CACHE_DEFAULT_FEATURES: 'true', VCPKG_CACHE_PROFILE: 'grouped-v2-ON-OFF-Release',
  VCPKG_DEFAULT_TRIPLET: 'x64-linux', VCPKG_DEFAULT_HOST_TRIPLET: 'x64-linux' };
function fixture(t) {
  const directory = mkdtempSync(join(tmpdir(), 'arknet-cache-mode-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  const env = { ...base, RUNNER_TEMP: directory, GITHUB_ENV: join(directory, 'environment'), GITHUB_OUTPUT: join(directory, 'output') };
  writeFileSync(env.GITHUB_ENV, ''); writeFileSync(env.GITHUB_OUTPUT, '');
  return { root, env };
}
const output = env => Object.fromEntries(readFileSync(env.GITHUB_OUTPUT, 'utf8').trim().split('\n').map(line => {
  const split = line.indexOf('='); return [line.slice(0, split), line.slice(split + 1)];
}));

test('cache mode defaults to compatible auto and rejects invalid or nonmanual cold requests', () => {
  assert.equal(cacheMode({}), 'auto');
  for (const event of ['pull_request', 'merge_group', 'workflow_dispatch']) assert.equal(cacheMode({ ...base, GITHUB_EVENT_NAME: event }), 'auto');
  assert.equal(cacheMode({ ...base, ARKNET_CACHE_MODE: 'cold' }), 'cold');
  for (const ARKNET_CACHE_MODE of ['', 'AUTO', 'Cold', 'warm', null, true, 'cold\n']) {
    assert.throws(() => cachePlan({ root, env: { ...base, ARKNET_CACHE_MODE } }), /Invalid native cache mode/);
  }
  for (const GITHUB_EVENT_NAME of ['pull_request', 'merge_group', undefined]) {
    assert.throws(() => cacheMode({ ...base, GITHUB_EVENT_NAME, ARKNET_CACHE_MODE: 'cold' }), /Invalid native cache mode/);
  }
  for (const name of ['GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT']) for (const value of [undefined, '', '0', '-1', '1.5', '1\n', 'x', 1]) {
    assert.throws(() => archiveSelection('identity', { ...base, ARKNET_CACHE_MODE: 'cold', [name]: value }), /cold-run identity/);
  }
});

test('cold keys are unique across seven groups, runs and attempts; auto can restore each saved identity', () => {
  const keys = new Set();
  for (const group of expectedGroups) {
    const os = { 'ubuntu-latest': 'Linux', 'macos-latest': 'macOS', 'windows-latest': 'Windows' }[group.os];
    const env = { ...base, RUNNER_OS: os, RUNNER_TEMP: os === 'Windows' ? 'C:/runner/temp' : base.RUNNER_TEMP,
      VCPKG_CACHE_PROFILE: `grouped-v2-${group.boost}-${group.sanitize}-${group.sanitize === 'OFF' ? 'Release' : 'Debug'}` };
    const stable = cachePlan({ root, env });
    assert.equal(stable.enabled, true);
    assert.deepEqual(cachePlan({ root, env: { ...env, ARKNET_CACHE_MODE: 'cold' } }), stable, 'mode cannot change ABI identity');
    for (const compiler of [false, true]) {
      const identity = compiler ? stable.key.replace('vcpkg-files-v1', `sccache-${sccacheVersion}-v1`) : stable.key;
      const auto = archiveSelection(identity, env, compiler);
      assert.equal(auto.archiveKey, compiler ? `${identity}-1001-1` : identity, 'existing auto primary key is unchanged');
      assert.equal(auto.restoreKeys, compiler ? `${identity}-` : `${identity}-cold-`);
      for (const [run, attempt] of [['1001', '1'], ['1001', '2'], ['1001', '10'], ['1002', '1']]) {
        const cold = archiveSelection(identity, { ...env, ARKNET_CACHE_MODE: 'cold', GITHUB_RUN_ID: run, GITHUB_RUN_ATTEMPT: attempt }, compiler);
        assert.equal(cold.restoreKeys, '', 'cold lookup must never search restore prefixes');
        assert.notEqual(cold.archiveKey, auto.archiveKey);
        assert.equal(keys.has(cold.archiveKey), false, 'no earlier group/run/attempt uses this cold key');
        keys.add(cold.archiveKey);
        assert.ok(cold.archiveKey.startsWith(auto.restoreKeys), 'subsequent auto lookup sees cold-produced entries');
        assert.ok(cold.archiveKey.length < 512);
      }
      const otherIdentity = archiveSelection(identity + 'different', env, compiler);
      assert.ok(!auto.archiveKey.startsWith(otherIdentity.restoreKeys), 'different identities must not share fallback candidates');
    }
  }
  assert.equal(keys.size, 56);
  for (const key of keys) for (const other of keys) if (other !== key) assert.equal(other.startsWith(key), false, 'primary-key partial matching cannot find another cold invocation');
});

test('actual preparation exports cold archive keys without changing compiler busters or within-run storage', t => {
  const f = fixture(t);
  const busters = [], plans = [];
  for (const mode of ['auto', 'cold']) {
    const env = { ...f.env, ARKNET_CACHE_MODE: mode, SCCACHE_SETUP_OUTCOME: 'success', SCCACHE_PATH: '/tool cache/sccache' };
    const dependency = prepareCache({ root, env });
    const dependencyOutput = output(env);
    const compiler = prepareCompilerCache({ root, env, run: (_command, _args, options) => {
      busters.push(options.env.SCCACHE_C_CUSTOM_CACHE_BUSTER);
      return { status: 0, stdout: `sccache ${sccacheVersion}\n` };
    } });
    const compilerOutput = output(env);
    assert.equal(dependencyOutput.restore_keys, mode === 'cold' ? '' : `${dependency.key}-cold-`);
    assert.equal(compilerOutput.restore_keys, mode === 'cold' ? '' : `${compiler.key}-`);
    assert.equal(compilerOutput.archive_key, compiler.archiveKey);
    assert.equal(compilerOutput.key, compiler.key, 'buster identity is exported separately from the archive key');
    if (mode === 'cold') {
      assert.equal(dependencyOutput.key, `${dependency.key}-cold-1001-1-archive`);
      assert.equal(compilerOutput.archive_key, `${compiler.key}-cold-1001-1-archive`);
    }
    plans.push({ dependency, compiler });
  }
  assert.equal(new Set(busters).size, 1, 'run/mode suffixes cannot invalidate compiled objects');
  assert.equal(plans[0].dependency.directory, plans[1].dependency.directory);
  assert.equal(plans[0].compiler.directory, plans[1].compiler.directory);
  assert.equal(plans[0].compiler.key, plans[1].compiler.key);
});

test('both preparation CLIs fail closed before exporting invalid inputs and support optional manual cold mode', t => {
  const f = fixture(t);
  for (const file of ['vcpkg-cache.mjs', 'compiler-cache.mjs']) {
    const script = join(root, '.github/scripts', file);
    const run = extra => spawnSync(process.execPath, [script], { cwd: root, env: { ...f.env, ...extra }, encoding: 'utf8' });
    for (const extra of [{ ARKNET_CACHE_MODE: 'invalid' }, { ARKNET_CACHE_MODE: '' },
      { ARKNET_CACHE_MODE: 'cold', GITHUB_EVENT_NAME: 'merge_group' }, { ARKNET_CACHE_MODE: 'cold', GITHUB_RUN_ATTEMPT: '' }]) {
      const before = readFileSync(f.env.GITHUB_ENV, 'utf8');
      const result = run(extra);
      assert.notEqual(result.status, 0, file + JSON.stringify(extra));
      assert.match(result.stderr, /Invalid native cache mode/);
      assert.equal(readFileSync(f.env.GITHUB_ENV, 'utf8'), before);
    }
    for (const ARKNET_CACHE_MODE of ['auto', 'cold']) assert.equal(run({ ARKNET_CACHE_MODE }).status, 0, file);
  }
});

test('real workflow defaults nonmanual runs to auto, skips scope-only cache work and retains every native graph', () => {
  const expression = workflow.match(/ARKNET_CACHE_MODE: \$\{\{ (.+) \}\}/)?.[1];
  assert.ok(expression);
  for (const event_name of ['pull_request', 'merge_group']) {
    assert.equal(runInNewContext(expression, { github: { event_name }, inputs: { cache_mode: 'cold' } }), 'auto');
  }
  for (const [input, expected] of [[undefined, 'auto'], ['auto', 'auto'], ['cold', 'cold'], ['invalid', 'invalid']]) {
    assert.equal(runInNewContext(expression, { github: { event_name: 'workflow_dispatch' }, inputs: { cache_mode: input } }), expected);
  }
  for (const name of ['Prepare dependency archive cache', 'Restore dependency archives', 'Prepare compiler launcher', 'Restore compiler cache']) {
    const step = workflow.split(/(?=^      - )/m).find(step => step.includes(`name: ${name}\n`));
    assert.match(step, /if: \$\{\{ env.ARKNET_RUN_NATIVE == 'true'/);
  }
  for (const cell of expectedCells) {
    const env = { ...base, RUNNER_OS: { 'ubuntu-latest': 'Linux', 'macos-latest': 'macOS', 'windows-latest': 'Windows' }[cell.os],
      MATRIX_BOOST: cell.boost, MATRIX_SSL: cell.ssl, MATRIX_SANITIZE: cell.sanitize, ARKNET_VALIDATION_TIER: 'full', ARKNET_SOURCE_TESTS: '' };
    assert.deepEqual(profilePlan({ ...env, ARKNET_CACHE_MODE: 'cold' }), profilePlan({ ...env, ARKNET_CACHE_MODE: 'auto' }));
  }
  assert.match(workflow, /timeout-minutes: 60/);
  assert.match(workflow, /cancel-in-progress: false/);
});
