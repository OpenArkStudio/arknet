// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
import { spawn } from 'node:child_process';
import { appendFileSync, createWriteStream, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { admission, observeResources, selectParallelism } from './build-parallelism.mjs';

export function profilePlan(env) {
  const boost = env.MATRIX_BOOST, ssl = env.MATRIX_SSL, sanitize = env.MATRIX_SANITIZE;
  if (!['ON', 'OFF'].includes(boost) || !['ON', 'OFF'].includes(ssl) || !['OFF', 'ON', 'THREAD'].includes(sanitize) ||
      !['Linux', 'macOS', 'Windows'].includes(env.RUNNER_OS) ||
      (ssl === 'OFF' && (env.RUNNER_OS !== 'Linux' || sanitize !== 'OFF')) ||
      (sanitize !== 'OFF' && env.RUNNER_OS !== 'Linux') ||
      !['full', 'modules'].includes(env.ARKNET_VALIDATION_TIER) ||
      (env.ARKNET_VALIDATION_TIER === 'modules' && (env.GITHUB_EVENT_NAME !== 'pull_request' ||
        env.RUNNER_OS !== 'Linux' || boost !== 'ON' || ssl !== 'ON' || sanitize !== 'OFF')) ||
      (env.ARKNET_VALIDATION_TIER === 'full' && (env.ARKNET_SOURCE_TESTS ?? '') !== '')) {
    throw new Error('Invalid native profile or source scope');
  }
  const build = `build/ci-${boost}-${ssl}-${sanitize}`;
  const debug = sanitize !== 'OFF';
  const flags = sanitize === 'ON' ? 'address,undefined' : 'thread';
  const configure = ['-S', '.', '-B', build, '-G', 'Ninja',
    // The link edge includes vcpkg's post-build DLL deployment. Serialize those
    // edges on Windows without reducing concurrent C++ compilation.
    ...(env.RUNNER_OS === 'Windows' ? ['-DCMAKE_JOB_POOLS=arknet_windows_link=1',
      '-DCMAKE_JOB_POOL_LINK=arknet_windows_link'] : []),
    `-DCMAKE_TOOLCHAIN_FILE=${env.VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`,
    `-DVCPKG_MANIFEST_FEATURES=${env.VCPKG_CACHE_FEATURES}`,
    `-DCMAKE_C_COMPILER_LAUNCHER=${env.ARKNET_COMPILER_LAUNCHER ?? ''}`,
    `-DCMAKE_CXX_COMPILER_LAUNCHER=${env.ARKNET_COMPILER_LAUNCHER ?? ''}`,
    `-DCMAKE_BUILD_TYPE=${debug ? 'Debug' : 'Release'}`,
    `-DARKNET_CHECK_TESTS=${env.ARKNET_SOURCE_TESTS ?? ''}`,
    `-DARKNET_USE_BOOST_ASIO=${boost}`, `-DARKNET_ENABLE_SSL=${ssl}`,
    `-DARKNET_BUILD_BENCHMARKS=${debug ? 'OFF' : 'ON'}`,
    ...(debug ? ['-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g1',
      `-DCMAKE_CXX_FLAGS=-fsanitize=${flags} -fno-omit-frame-pointer`, `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=${flags}`] : [])];
  const testEnv = sanitize === 'ON' ? { ASAN_OPTIONS: 'detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS: 'halt_on_error=1:print_stacktrace=1' }
    : sanitize === 'THREAD' ? { TSAN_OPTIONS: 'halt_on_error=1:second_deadlock_stack=1' } : {};
  const benchmark = env.RUNNER_OS === 'Linux' && ssl === 'ON' && sanitize === 'OFF' &&
    (env.ARKNET_VALIDATION_TIER === 'full' || env.ARKNET_SOURCE_TESTS === '');
  return { boost, ssl, sanitize, build, configure, testEnv, benchmark };
}

async function runCommand(command, args, { env, log }) {
  await new Promise((done, reject) => {
    const stream = createWriteStream(log, { flags: 'a' });
    const child = spawn(command, args, { env, stdio: ['ignore', 'pipe', 'pipe'] });
    child.stdout.on('data', data => { process.stdout.write(data); stream.write(data); });
    child.stderr.on('data', data => { process.stderr.write(data); stream.write(data); });
    child.once('error', error => { stream.end(); reject(error); });
    child.once('close', (code, signal) => stream.end(() => code === 0 ? done() : reject(new Error(`${command} failed: ${signal ?? code}`))));
  });
}

export async function runStage(stage, env = process.env, run = runCommand, observe = observeResources) {
  const plan = profilePlan(env);
  const { build, sanitize } = plan;
  mkdirSync(`${build}/ci-logs`, { recursive: true });
  const log = `${build}/ci-logs/ci-${stage}.log`;
  const command = (name, args, extra = {}) => run(name, args, { env: { ...env, ...extra }, log });
  try {
    if (stage === 'configure') await command('cmake', plan.configure);
    else if (stage === 'parallel') {
      if (sanitize !== 'ON') throw new Error('Parallelism planning is only valid for AddressSanitizer/UndefinedBehaviorSanitizer');
      const resources = observe(env);
      const decision = selectParallelism(resources);
      writeFileSync(`${build}/ci-parallel.json`, JSON.stringify(decision));
      const evidence = JSON.stringify({ profile: build, resources, ...decision });
      console.log(evidence);
      writeFileSync(log, evidence + '\n');
      if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY, `${evidence}\n\nExperimental scheduling heuristic; compiler memory peaks and speedup are not established.\n`);
    } else if (stage === 'build') {
      let parallel = sanitize === 'THREAD' ? 2 : 3;
      if (sanitize === 'ON') {
        parallel = JSON.parse(readFileSync(`${build}/ci-parallel.json`, 'utf8')).parallel;
        if (![1, 2].includes(parallel)) throw new Error('Missing or invalid bounded compiler parallelism');
      }
      if (env.RUNNER_OS === 'Windows' && plan.boost === 'ON' && sanitize === 'OFF') {
        const resources = observe(env);
        const decision = selectParallelism(resources);
        // A missing/mismatched platform observation must retain the existing bound.
        parallel = resources.platform === 'win32' && decision.parallel === 4 ? 4 : 3;
        const evidence = JSON.stringify({ profile: build, resources, admission, ...decision, parallel });
        console.log(evidence);
        appendFileSync(log, evidence + '\n');
        if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY,
          `${evidence}\n\nExperimental Windows Boost Release scheduling heuristic; cold-build duration and compiler memory peaks require hosted validation.\n`);
      }
      await command('cmake', ['--build', build,
        ...(env.ARKNET_VALIDATION_TIER === 'modules' && env.ARKNET_SOURCE_TESTS ? ['--target', 'arknet_check_build'] : []),
        '--parallel', String(parallel)]);
    } else if (stage === 'test') await command('cmake', ['--build', build, '--target', 'arknet_check'], plan.testEnv);
    else if (stage === 'benchmark') {
      if (!plan.benchmark) throw new Error('Benchmark/report checks are outside this profile scope');
      for (const [name, ...args] of [
        ['bash', 'scripts/test_benchmark_matrix.sh'], ['bash', 'scripts/test_rudp_matrix.sh'],
        ['bash', '-n', 'benchmarks/run_rudp_fec.sh'], ['node', 'benchmarks/report_rudp_fec.mjs', '--self-check'],
        ['python3', '-m', 'unittest', 'discover', '-s', 'benchmarks', '-p', 'test_*.py', '-v'],
        ['python3', '-m', 'unittest', 'discover', '-s', 'scripts', '-p', 'test_udp_limits_report.py', '-v'],
      ]) await command(name, args);
    } else throw new Error('Unknown native stage');
    if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY, `${build}: ${stage} succeeded.\n`);
  } catch (error) {
    appendFileSync(log, `${error.message}\n`);
    if (env.GITHUB_STEP_SUMMARY) appendFileSync(env.GITHUB_STEP_SUMMARY, `${build}: ${stage} failed.\n`);
    throw error;
  }
  return plan;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { await runStage(process.argv[2]); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
