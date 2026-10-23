// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: BSL-1.0
const positiveId = value => typeof value === 'string' && /^[1-9][0-9]*$/.test(value);

export function cacheMode(env) {
  const mode = env.ARKNET_CACHE_MODE === undefined ? 'auto' : env.ARKNET_CACHE_MODE;
  if (!['auto', 'cold'].includes(mode) || (mode === 'cold' &&
      (env.GITHUB_EVENT_NAME !== 'workflow_dispatch' || !positiveId(env.GITHUB_RUN_ID) || !positiveId(env.GITHUB_RUN_ATTEMPT)))) {
    throw new Error('Invalid native cache mode or cold-run identity');
  }
  return mode;
}

export function archiveSelection(key, env, compiler = false) {
  const mode = cacheMode(env);
  // The nonce changes archive lookup only; callers retain the stable compiler/ABI identity.
  if (mode === 'cold') return { archiveKey: `${key}-cold-${env.GITHUB_RUN_ID}-${env.GITHUB_RUN_ATTEMPT}-archive`, restoreKeys: '' };
  if (compiler && (!positiveId(env.GITHUB_RUN_ID) || !positiveId(env.GITHUB_RUN_ATTEMPT))) {
    throw new Error('Compiler archive requires a valid run identity');
  }
  return compiler ? { archiveKey: `${key}-${env.GITHUB_RUN_ID}-${env.GITHUB_RUN_ATTEMPT}`, restoreKeys: `${key}-` }
    : { archiveKey: key, restoreKeys: `${key}-cold-` };
}
