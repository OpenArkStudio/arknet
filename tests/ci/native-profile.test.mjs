// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { chmodSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { runInNewContext } from 'node:vm';
import { expectedCells, expectedGroups, groupName, slotName } from '../../.github/scripts/ci-required.mjs';
import { profilePlan, runStage } from '../../.github/scripts/native-profile.mjs';
import { cachePlan } from '../../.github/scripts/vcpkg-cache.mjs';

const root = process.cwd();
const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
const native = workflow.split('  test:\n')[1].split('  required:\n')[0];
const steps = native.split(/(?=^      - )/m).slice(1);
const environment = cell => ({ MATRIX_BOOST: cell.boost, MATRIX_SSL: cell.ssl, MATRIX_SANITIZE: cell.sanitize,
  RUNNER_OS: { 'ubuntu-latest': 'Linux', 'macos-latest': 'macOS', 'windows-latest': 'Windows' }[cell.os],
  GITHUB_EVENT_NAME: 'merge_group', ARKNET_VALIDATION_TIER: 'full', ARKNET_SOURCE_TESTS: '',
  VCPKG_ROOT: '/tools with spaces/vcpkg', VCPKG_CACHE_FEATURES: 'boost;ssl', ARKNET_COMPILER_LAUNCHER: '/tools with spaces/sccache' });
function temporary(t) {
  const directory = mkdtempSync(join(tmpdir(), 'arknet-grouped-profile-'));
  process.chdir(directory);
  t.after(() => { process.chdir(root); rmSync(directory, { recursive: true, force: true }); });
}

test('all twelve profiles preserve configure flags, source targets, sanitizers and distinct build trees', async t => {
  temporary(t);
  const builds = new Set();
  for (const cell of expectedCells) {
    const env = environment(cell), calls = [];
    const run = async (command, args, options) => calls.push({ command, args, ...options });
    const plan = await runStage('configure', env, run);
    // OS groups have isolated hosts; a group must never reuse another profile build tree.
    const key = `${cell.os}/${plan.build}`;
    assert.ok(!builds.has(key)); builds.add(key);
    assert.equal(calls[0].command, 'cmake');
    assert.ok(calls[0].args.includes(`-DARKNET_USE_BOOST_ASIO=${cell.boost}`));
    assert.ok(calls[0].args.includes(`-DARKNET_ENABLE_SSL=${cell.ssl}`));
    for (const launcher of ['C', 'CXX']) assert.ok(calls[0].args.includes(`-DCMAKE_${launcher}_COMPILER_LAUNCHER=${env.ARKNET_COMPILER_LAUNCHER}`));
    assert.ok(calls[0].args.includes('-DVCPKG_MANIFEST_FEATURES=boost;ssl'));
    assert.ok(calls[0].args.includes(`-DCMAKE_BUILD_TYPE=${cell.sanitize === 'OFF' ? 'Release' : 'Debug'}`));
    assert.ok(calls[0].args.includes(`-DARKNET_BUILD_BENCHMARKS=${cell.sanitize === 'OFF' ? 'ON' : 'OFF'}`));
    assert.ok(calls[0].args.includes('-DARKNET_CHECK_TESTS='));
    if (cell.sanitize !== 'OFF') {
      assert.ok(calls[0].args.includes('-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g1'));
      assert.ok(calls[0].args.includes(`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=${cell.sanitize === 'ON' ? 'address,undefined' : 'thread'}`));
    }
    if (cell.sanitize === 'ON') {
      assert.ok(calls[0].args.includes('-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'));
      await runStage('parallel', env, run);
    } else if (cell.sanitize === 'THREAD') assert.ok(calls[0].args.includes('-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer'));
    await runStage('build', env, run);
    const parallel = Number(calls[1].args.at(-1));
    assert.ok(cell.sanitize === 'ON' ? [1, 2].includes(parallel) : parallel === (cell.sanitize === 'THREAD' ? 2 : 3));
    await runStage('test', env, run);
    assert.deepEqual(calls[2].args, ['--build', plan.build, '--target', 'arknet_check']);
    assert.deepEqual(Object.fromEntries(Object.keys(plan.testEnv).map(key => [key, calls[2].env[key]])), plan.testEnv);
    if (cell.sanitize === 'ON') assert.equal(calls[2].env.ASAN_OPTIONS, 'detect_leaks=1:halt_on_error=1');
    if (cell.sanitize === 'THREAD') assert.equal(calls[2].env.TSAN_OPTIONS, 'halt_on_error=1:second_deadlock_stack=1');
  }
  const source = { ...environment(expectedCells.find(cell => cell.os === 'ubuntu-latest' && cell.boost === 'ON' && cell.sanitize === 'OFF')),
    GITHUB_EVENT_NAME: 'pull_request', ARKNET_VALIDATION_TIER: 'modules', ARKNET_SOURCE_TESTS: 'rudp;fec' };
  const calls = [];
  await runStage('build', source, async (_, args) => calls.push(args));
  assert.deepEqual(calls[0].slice(2), ['--target', 'arknet_check_build', '--parallel', '3']);
});

test('both Linux TLS Release dependency profiles retain every benchmark/report check', async t => {
  temporary(t);
  for (const boost of ['ON', 'OFF']) {
    const env = environment({ os: 'ubuntu-latest', boost, ssl: 'ON', sanitize: 'OFF' }), calls = [];
    await runStage('benchmark', env, async (command, args) => calls.push([command, ...args]));
    assert.deepEqual(calls, [
      ['bash', 'scripts/test_benchmark_matrix.sh'], ['bash', 'scripts/test_rudp_matrix.sh'],
      ['bash', '-n', 'benchmarks/run_rudp_fec.sh'], ['node', 'benchmarks/report_rudp_fec.mjs', '--self-check'],
      ['python3', '-m', 'unittest', 'discover', '-s', 'benchmarks', '-p', 'test_*.py', '-v'],
      ['python3', '-m', 'unittest', 'discover', '-s', 'scripts', '-p', 'test_udp_limits_report.py', '-v'],
    ]);
  }
  const scope = { ...environment({ os: 'ubuntu-latest', boost: 'ON', ssl: 'ON', sanitize: 'OFF' }),
    ARKNET_VALIDATION_TIER: 'modules', ARKNET_SOURCE_TESTS: 'rudp', GITHUB_EVENT_NAME: 'pull_request' };
  await assert.rejects(runStage('benchmark', scope, async () => assert.fail('unplanned benchmark')), /outside/);
});

test('each native command failure propagates once and produces a profile-specific failure log', async t => {
  temporary(t);
  const env = environment(expectedCells[0]);
  for (const stage of ['configure', 'build', 'test', 'benchmark']) {
    let attempts = 0;
    await assert.rejects(runStage(stage, env, async () => { attempts++; throw new Error('fixture compiler or test failure'); }), /fixture/);
    assert.equal(attempts, 1);
    assert.match(readFileSync(`${profilePlan(env).build}/tests/logs/ci-${stage}.log`, 'utf8'), /fixture/);
  }
  const sanitized = { ...env, MATRIX_SANITIZE: 'ON' };
  await assert.rejects(runStage('build', sanitized, async () => assert.fail('must not build without resource decision')));
  writeFileSync(`${profilePlan(sanitized).build}/ci-parallel.json`, '{"parallel":3}');
  await assert.rejects(runStage('build', sanitized, async () => assert.fail('must not exceed sanitizer bound')), /invalid bounded/);
});

test('invalid profiles and narrower source scope cannot execute unplanned coverage', () => {
  const env = environment(expectedCells[0]);
  for (const change of [{ MATRIX_BOOST: 'on' }, { MATRIX_SSL: 'unknown' }, { MATRIX_SANITIZE: 'OFF;evil' },
    { RUNNER_OS: 'Windows', MATRIX_SANITIZE: 'THREAD' }, { ARKNET_SOURCE_TESTS: 'rudp' },
    { ARKNET_VALIDATION_TIER: 'documentation' }, { ARKNET_VALIDATION_TIER: 'modules' }]) {
    assert.throws(() => profilePlan({ ...env, ...change }));
  }
});

test('the CLI streams per-profile logs and exits nonzero for a failing fake tool without retry', { skip: process.platform === 'win32' }, t => {
  temporary(t);
  const tool = join(process.cwd(), 'cmake');
  writeFileSync(tool, `#!${process.execPath}\nprocess.stderr.write('fixture tool failure\\n');process.exit(23);\n`);
  chmodSync(tool, 0o755);
  const cell = expectedCells[0], env = { ...process.env, ...environment(cell), PATH: `${process.cwd()}:${process.env.PATH}` };
  const script = fileURLToPath(new URL('../../.github/scripts/native-profile.mjs', import.meta.url));
  const result = spawnSync(process.execPath, [script, 'test'], { env, encoding: 'utf8', timeout: 10_000 });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /fixture tool failure/);
  assert.match(result.stderr, /cmake failed: 23/);
  assert.match(readFileSync(`${profilePlan(env).build}/tests/logs/ci-test.log`, 'utf8'), /fixture tool failure/);
});

test('later configurations still run after any previous profile fails; native errors are never optional', () => {
  for (const group of expectedGroups) {
    for (const step of steps.filter(step => /^      - name: (Configure -|Build planned native targets -|Test -|Select bounded compiler parallelism -|Check benchmark batch runner and report calculations -)/m.test(step))) {
      assert.doesNotMatch(step, /continue-on-error:/);
      assert.match(step, /if: \$\{\{ always\(\)/);
      const boost = step.match(/name: .* - (Boost|Standalone) - (TLS|NoTLS)/)[1];
      const tls = step.match(/name: .* - (Boost|Standalone) - (TLS|NoTLS)/)[2];
      assert.ok(step.includes(`MATRIX_BOOST: '${boost === 'Boost' ? 'ON' : 'OFF'}'`));
      assert.ok(step.includes(`MATRIX_SSL: '${tls === 'TLS' ? 'ON' : 'OFF'}'`));
    }
    const configure = steps.filter(step => /^      - name: Configure - /m.test(step));
    for (const cell of expectedCells.filter(cell => cell.os === group.os && cell.sanitize === group.sanitize)) {
      const step = configure.find(step => step.includes(`name: Configure - ${slotName(cell)}\n`));
      const expression = step.match(/if: \$\{\{ (.+) \}\}/)[1].replace(/steps\.([a-z0-9-]+)/g, 'steps["$1"]');
      // There is no success() dependency on an unrelated earlier profile.
      assert.equal(runInNewContext(expression, { always: () => true, env: { ARKNET_RUN_NATIVE: 'true' },
        matrix: group, needs: { format: { outputs: { tier: 'full' } } }, steps: { 'native-setup': { outcome: 'success' } } }), true);
    }
  }
  assert.equal(new Set(expectedGroups.map(groupName)).size, 5);
  assert.match(native, /timeout-minutes: 60/);
  assert.equal((native.match(/uses: lukka\/run-vcpkg/g) ?? []).length, 1);
  assert.equal((native.match(/name: Restore compiler cache/g) ?? []).length, 1);
});

test('shared archives isolate OS and sanitizer groups while dependency flags stay identical inside each group', () => {
  const env = { RUNNER_OS: 'Linux', RUNNER_ARCH: 'X64', RUNNER_TEMP: '/tmp/cache-fixture', ImageOS: 'ubuntu24', ImageVersion: 'fixture',
    VCPKG_CACHE_REVISION: '0699a19d0c6386247ce50d4dedbb8217d484d536', VCPKG_CACHE_FEATURES: 'boost;ssl',
    VCPKG_CACHE_DEFAULT_FEATURES: 'true', VCPKG_DEFAULT_TRIPLET: 'x64-linux', VCPKG_DEFAULT_HOST_TRIPLET: 'x64-linux' };
  const keys = expectedGroups.map(group => cachePlan({ root, env: { ...env, RUNNER_OS: environment({ ...group, boost: 'ON', ssl: 'ON' }).RUNNER_OS,
    RUNNER_TEMP: group.os === 'windows-latest' ? 'C:/temp/cache-fixture' : env.RUNNER_TEMP,
    VCPKG_CACHE_PROFILE: `grouped-v1-${group.sanitize}-${group.sanitize === 'OFF' ? 'Release' : 'Debug'}` } }).key);
  assert.equal(new Set(keys).size, 5);
  assert.ok(keys.every(Boolean));
  const profile = 'grouped-v1-OFF-Release';
  const cache = cachePlan({ root, env: { ...env, VCPKG_CACHE_PROFILE: profile } });
  for (const boost of ['ON', 'OFF']) for (const ssl of ['ON', 'OFF']) {
    assert.equal(cachePlan({ root, env: { ...env, VCPKG_CACHE_PROFILE: profile, MATRIX_BOOST: boost, MATRIX_SSL: ssl } }).key, cache.key);
  }
});
