import { spawnSync } from 'node:child_process';
import { appendFileSync, lstatSync, mkdirSync } from 'node:fs';
import { posix, resolve, win32 } from 'node:path';
import { pathToFileURL } from 'node:url';
import { cachePlan } from './vcpkg-cache.mjs';
import { archiveSelection } from './cache-mode.mjs';

export const sccacheVersion = '0.16.0';

export function prepareCompilerCache({ root, env, run = spawnSync, start = true }) {
  if (!env.GITHUB_ENV || !env.GITHUB_OUTPUT) throw new Error('GitHub file-command channels are required');
  const dependencyPlan = cachePlan({ root, env });
  const paths = env.RUNNER_OS === 'Windows' ? win32 : posix;
  const directory = dependencyPlan.enabled ? paths.join(env.RUNNER_TEMP, 'arknet-sccache') : '';
  const key = dependencyPlan.enabled ? dependencyPlan.key.replace('vcpkg-files-v1', `sccache-${sccacheVersion}-v1`) : '';
  const selection = dependencyPlan.enabled ? archiveSelection(key, env, true) : { archiveKey: '', restoreKeys: '' };
  let enabled = false;
  if (directory && env.SCCACHE_SETUP_OUTCOME === 'success' && env.SCCACHE_PATH &&
      !/[\x00-\x1f\x7f;]/.test(env.SCCACHE_PATH)) {
    try {
      mkdirSync(directory, { recursive: true });
      if (!lstatSync(directory).isDirectory()) throw new Error('cache path is not a directory');
      const cacheEnv = { ...env, SCCACHE_DIR: directory, SCCACHE_GHA_ENABLED: 'false',
        SCCACHE_IGNORE_SERVER_IO_ERROR: '1', SCCACHE_CACHE_SIZE: '512M', SCCACHE_C_CUSTOM_CACHE_BUSTER: key };
      const options = { env: cacheEnv, encoding: 'utf8', timeout: 30_000 };
      const version = run(env.SCCACHE_PATH, ['--version'], options);
      if (version.status === 0 && version.stdout.trim() === `sccache ${sccacheVersion}`) {
        enabled = !start || run(env.SCCACHE_PATH, ['--start-server'], options).status === 0;
      }
    } catch {
      // Optional cache setup must not prevent a normal compiler invocation.
    }
  }
  const launcher = enabled ? env.SCCACHE_PATH.replaceAll('\\', '/') : '';
  appendFileSync(env.GITHUB_ENV, `ARKNET_COMPILER_LAUNCHER=${launcher}\nSCCACHE_DIR=${enabled ? directory : ''}\nSCCACHE_GHA_ENABLED=false\nSCCACHE_IGNORE_SERVER_IO_ERROR=1\nSCCACHE_CACHE_SIZE=512M\nSCCACHE_C_CUSTOM_CACHE_BUSTER=${key}\n`);
  appendFileSync(env.GITHUB_OUTPUT, `enabled=${enabled}\ndirectory=${enabled ? directory : ''}\nkey=${enabled ? key : ''}\narchive_key=${enabled ? selection.archiveKey : ''}\nrestore_keys=${enabled ? selection.restoreKeys : ''}\n`);
  return { enabled, directory, key, launcher, ...selection };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const plan = prepareCompilerCache({ root: process.cwd(), env: process.env, start: !process.argv.includes('--prepare') });
    console.log(plan.enabled ? `sccache ${sccacheVersion} compiler launcher prepared` : 'Compiler cache unavailable; using the compiler directly');
  } catch (error) {
    console.error(/^(Invalid native cache mode|Compiler archive requires)/.test(error.message) ? error.message : 'Unable to export compiler cache configuration to GitHub file-command channels');
    process.exitCode = 1;
  }
}
