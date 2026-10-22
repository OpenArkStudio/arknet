import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { prepareCompilerCache, sccacheVersion } from '../../.github/scripts/compiler-cache.mjs';

function fixture(t) {
  const root = mkdtempSync(join(tmpdir(), 'arknet-compiler-cache-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  mkdirSync(join(root, 'ports'));
  writeFileSync(join(root, 'vcpkg.json'), '{}');
  writeFileSync(join(root, 'vcpkg-configuration.json'), '{"overlay-ports":["ports"]}');
  const env = {
    GITHUB_RUN_ID: '1001', GITHUB_RUN_ATTEMPT: '1',
    RUNNER_OS: 'Linux', RUNNER_ARCH: 'X64', RUNNER_TEMP: join(root, 'runner temp'),
    ImageOS: 'ubuntu24', ImageVersion: '20261020.1', VCPKG_CACHE_REVISION: 'a'.repeat(40),
    VCPKG_CACHE_FEATURES: 'boost;ssl', VCPKG_CACHE_DEFAULT_FEATURES: 'true',
    VCPKG_CACHE_PROFILE: 'ON-ON-OFF-Release', VCPKG_DEFAULT_TRIPLET: 'x64-linux',
    VCPKG_DEFAULT_HOST_TRIPLET: 'x64-linux', GITHUB_ENV: join(root, 'env'),
    GITHUB_OUTPUT: join(root, 'output'), SCCACHE_SETUP_OUTCOME: 'success',
    SCCACHE_PATH: '/tool cache/sccache',
  };
  return { root, env };
}

test('compiler launcher uses only the verified pinned version with cache IO fallback', t => {
  const f = fixture(t);
  const calls = [];
  const plan = prepareCompilerCache({ ...f, run: (command, args, options) => {
    calls.push([command, args]);
    assert.equal(options.env.SCCACHE_GHA_ENABLED, 'false');
    assert.equal(options.env.SCCACHE_IGNORE_SERVER_IO_ERROR, '1');
    assert.match(options.env.SCCACHE_C_CUSTOM_CACHE_BUSTER, /arknet-sccache-0.16.0-v1/);
    return { status: 0, stdout: `sccache ${sccacheVersion}\n` };
  } });
  assert.equal(plan.enabled, true);
  assert.equal(plan.launcher, f.env.SCCACHE_PATH);
  assert.deepEqual(calls.map(call => call[1]), [['--version'], ['--start-server']]);
  assert.match(readFileSync(f.env.GITHUB_ENV, 'utf8'), /ARKNET_COMPILER_LAUNCHER=\/tool cache\/sccache/);
});

test('preparation does not start a server before Actions restores the local cache directory', t => {
  const f = fixture(t);
  const calls = [];
  const plan = prepareCompilerCache({ ...f, start: false, run: (_command, args) => {
    calls.push(args);
    return { status: 0, stdout: `sccache ${sccacheVersion}\n` };
  } });
  assert.equal(plan.enabled, true);
  assert.deepEqual(calls, [['--version']]);
});

test('failed setup, wrong version, startup errors and incomplete identity all use the compiler directly', t => {
  for (const extra of [{ SCCACHE_SETUP_OUTCOME: 'failure' }, { SCCACHE_PATH: '' },
    { SCCACHE_PATH: '/unsafe;launcher' }, { ImageVersion: '' }, { VCPKG_DEFAULT_TRIPLET: '' },
    { behavior: 'wrong-version' }, { behavior: 'startup-error' }, { behavior: 'throw' }]) {
    const f = fixture(t);
    const plan = prepareCompilerCache({ root: f.root, env: { ...f.env, ...extra }, run: (_command, args) => {
      if (extra.behavior === 'throw') throw new Error('tool unavailable');
      return { status: args[0] === '--start-server' ? 1 : 0,
        stdout: extra.behavior === 'wrong-version' ? 'sccache 0.1.0' : `sccache ${sccacheVersion}` };
    } });
    assert.equal(plan.enabled, false, JSON.stringify(extra));
    assert.equal(plan.launcher, '');
    assert.match(readFileSync(f.env.GITHUB_ENV, 'utf8'), /^ARKNET_COMPILER_LAUNCHER=\n/);
  }
});

test('CMake passes a launcher path with spaces, keeps the compiler and propagates compilation failures', { skip: process.platform === 'win32' }, t => {
  const f = fixture(t);
  const launcher = join(f.root, 'launcher with spaces.mjs');
  const trace = join(f.root, 'launcher-trace');
  // Run a transparent fixture launcher through the same two CMake launcher arguments as CI.
  writeFileSync(launcher, `import { spawnSync } from 'node:child_process';\nimport { appendFileSync } from 'node:fs';\nappendFileSync(${JSON.stringify(trace)}, process.argv[2] + '\\n');\nconst r = spawnSync(process.argv[2], process.argv.slice(3), { stdio: 'inherit' });\nprocess.exit(r.status ?? 1);\n`);
  writeFileSync(join(f.root, 'CMakeLists.txt'), 'cmake_minimum_required(VERSION 3.21)\nproject(launcher LANGUAGES C CXX)\nadd_executable(check main.cpp helper.c)\n');
  writeFileSync(join(f.root, 'main.cpp'), 'int main() { return 0; }\n');
  writeFileSync(join(f.root, 'helper.c'), 'int helper(void) { return 0; }\n');
  const build = join(f.root, 'build');
  const run = args => spawnSync('cmake', args, { encoding: 'utf8', timeout: 30_000 });
  const configured = run(['-S', f.root, '-B', build, '-G', 'Ninja',
    `-DCMAKE_C_COMPILER_LAUNCHER=${process.execPath};${launcher}`,
    `-DCMAKE_CXX_COMPILER_LAUNCHER=${process.execPath};${launcher}`]);
  assert.equal(configured.status, 0, configured.stdout + configured.stderr);
  const compiled = run(['--build', build]);
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
  assert.equal(readFileSync(trace, 'utf8').trim().split('\n').length, 2);
  writeFileSync(join(f.root, 'main.cpp'), '#error intentional compiler failure\n');
  const failed = run(['--build', build]);
  assert.notEqual(failed.status, 0);
  assert.match(failed.stdout + failed.stderr, /intentional compiler failure/);
  const direct = join(f.root, 'direct');
  assert.equal(run(['-S', f.root, '-B', direct, '-G', 'Ninja',
    '-DCMAKE_C_COMPILER_LAUNCHER=', '-DCMAKE_CXX_COMPILER_LAUNCHER=']).status, 0);
  assert.notEqual(run(['--build', direct]).status, 0, 'cold builds must preserve compiler errors');
});
